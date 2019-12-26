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

// globals

// last time the hook was invoked
DWORD lastInteraction = 0;

// last time we printed this
DWORD lastDebugOutputTickCount = 0;

// number of times hook has been called
ULONGLONG hookCalls = 0;

// low level hooks
HHOOK llkeyboardHandle = 0;
HHOOK llmouseHandle = 0;


int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow
)
{

	llkeyboardHandle = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionKeyboard"), hInstance, 0);
	llmouseHandle = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionMouse"), hInstance, 0);

	OutputDebugString(L"Set hooks");

	MSG msg = {  };
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
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
	if (hookCalls & 0xF != 0) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	lastInteraction = GetTickCount();

	DebugShowTickCount(L"Keyboard", hookCalls);

	//TryKillShutdownProcess(lastInteraction);

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}



extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionMouse(int nCode, WPARAM wParam, LPARAM lParam)
{
	hookCalls++;

	// reduce frequency of calling GetTickCount
	if (hookCalls & 0xF != 0) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	DebugShowTickCount(L"Mouse", hookCalls);
	lastInteraction = GetTickCount();
	//TryKillShutdownProcess(lastInteraction);

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}



void DebugShowTickCount(LPCWSTR context, DWORD hookCalls)
{
	const int bufLen = 512;
	DWORD ticks = GetTickCount();


	if (ticks - lastDebugOutputTickCount > 1500) {
		wchar_t debugStrBuffer[bufLen];
		if (swprintf_s(debugStrBuffer, bufLen, L"%s: Update tick count %d (internal 0x%x)\n", context, ticks, hookCalls) > 0) {
			OutputDebugString(debugStrBuffer);
		}
		lastDebugOutputTickCount = ticks;
	}
}