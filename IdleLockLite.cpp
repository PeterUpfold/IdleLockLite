/*
 *
 * IdleLockLite
 *
 * Copyright (C) 2019-2020 Peter Upfold.
 *
 * This file is licensed to you under the Apache License, version 2.0 (the "License").
 * You may not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 */

#ifndef UNICODE
#define UNICODE
#endif 

#include "IdleLockLite.h"
#include <Windows.h>
#include <tchar.h>
#include <assert.h>
#include <TlHelp32.h>

// globals

HINSTANCE instance = 0;

// last time the hook was invoked
ULONGLONG lastInteraction = 0;

// last time we printed this
ULONGLONG lastDebugOutputTickCount = 0;

// number of times hook has been called
ULONGLONG hookCalls = 0;

// low level hooks
HHOOK llkeyboardHandle = 0;
HHOOK llmouseHandle = 0;


int APIENTRY WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow
)
{
	instance = hInstance;


	if (AlreadyRunning())
	{
		OutputDebugString(L"Cannot run multiple instances");
		return 1;
	}

	const wchar_t CLASS_NAME[] = L"IdleLockLite";
	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.lpszClassName = CLASS_NAME;
	RegisterClass(&wc);

	//HWND window = CreateWindowEx(
	//	0,
	//	CLASS_NAME,
	//	L"IdleLockLite",
	//	WS_OVERLAPPEDWINDOW,
	//	CW_USEDEFAULT,
	//	CW_USEDEFAULT,
	//	CW_USEDEFAULT,
	//	CW_USEDEFAULT,
	//	NULL,
	//	NULL,
	//	instance,
	//	NULL
	//);

	//assert(window != 0);
	//ShowWindow(window, nCmdShow);

	llkeyboardHandle = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionKeyboard"), hInstance, 0);
	llmouseHandle = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionMouse"), hInstance, 0);


	DEBUG_BUFFER;
	if (swprintf_s(debugStrBuffer, bufLen, L"Began with tick count %lld\n", GetTickCount64()) > 0) {
		OutputDebugString(debugStrBuffer);
	}

	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		if (swprintf_s(debugStrBuffer, bufLen, L"Calls: %lld\n", hookCalls)) {
			OutputDebugString(debugStrBuffer);
		}

	}

	// clean up
	
	assert(llmouseHandle != NULL);
	assert(llkeyboardHandle != NULL);
	UnhookWindowsHookEx(llmouseHandle);
	UnhookWindowsHookEx(llkeyboardHandle);
	
	return 0;
}

LRESULT CALLBACK WndProc(
	_In_ HWND hWnd,
	_In_ UINT message,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_PAINT:
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionKeyboard(int nCode, WPARAM wParam, LPARAM lParam)
{
	hookCalls++;

	// reduce frequency of calling GetTickCount
	if ((hookCalls & 0xF) != 0) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	lastInteraction = GetTickCount64();

	DebugShowTickCount(L"Keyboard", hookCalls);

	//TryKillShutdownProcess(lastInteraction);

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}



extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionMouse(int nCode, WPARAM wParam, LPARAM lParam)
{
	hookCalls++;

	// reduce frequency of calling GetTickCount
	if ((hookCalls & 0xF) != 0) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	DebugShowTickCount(L"Mouse", hookCalls);
	lastInteraction = GetTickCount64();
	//TryKillShutdownProcess(lastInteraction);

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}



void DebugShowTickCount(LPCWSTR context, DWORD hookCalls)
{
	const int bufLen = 512;
	ULONGLONG ticks = GetTickCount64();


	if (ticks - lastDebugOutputTickCount > 1500) {
		wchar_t debugStrBuffer[bufLen];
		if (swprintf_s(debugStrBuffer, bufLen, L"%s: Update tick count %lld (internal 0x%x)\n", context, ticks, hookCalls) > 0) {
			OutputDebugString(debugStrBuffer);
		}
		lastDebugOutputTickCount = ticks;
	}
}

BOOLEAN AlreadyRunning() {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	HANDLE process;
	PROCESSENTRY32 pe32;

	if (snapshot == INVALID_HANDLE_VALUE) {
		OutputDebugString(L"Process snapshot had INVALID_HANDLE_VALUE\n");
		return FALSE;
	}

	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(snapshot, &pe32)) {
		OutputDebugString(L"Process32First failed\n");
		CloseHandle(snapshot);
		return FALSE;
	}

	do {
		if (_wcsicmp(pe32.szExeFile, L"idlelocklite.exe") == 0) {

			// ignore current instance
			if (pe32.th32ProcessID == GetCurrentProcessId()) {
				continue;
			}

			OutputDebugString(L"Found existing instance!\n");

			CloseHandle(snapshot);
			return TRUE;
		}
	} while (Process32Next(snapshot, &pe32));
	CloseHandle(snapshot);
	return FALSE;
}