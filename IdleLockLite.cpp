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

#define ILL_EXITCODE_MULTIPLEINSTANCES 1;
#define ILL_EXITCODE_FAILEDTOPARSECMDLINE 2;
#define ILL_EXITCODE_INVALIDARGC 3;
#define ILL_EXITCODE_ARGVNAN 4;

// globals

HINSTANCE instance = 0;

// last time the hook was invoked
ULONGLONG lastInteraction = 0;

// last time we printed this
ULONGLONG lastDebugOutputTickCount = 0;

// starting value for tick duration calculator
ULONGLONG tickCalculatorStart = 0;

// number of times hook has been called
ULONGLONG hookCalls = 0;

// low level hooks
HHOOK llkeyboardHandle = 0;
HHOOK llmouseHandle = 0;

// timer
UINT_PTR timer = 0;
UINT_PTR calculateTicksTimer = 0;

/* The rough number of ticks we consider to be an idle condition.
Let's measure the ms per tick, roughly.
*/
ULONGLONG roughTicksConsideredIdle = 0;
ULONGLONG gracePeriod = 0;
ULONGLONG msPerTick = 16;

long int idleSeconds = 0;
long int gracePeriodSeconds = 0;

constexpr int timerFrequency = 10000;


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
		return ILL_EXITCODE_MULTIPLEINSTANCES;
	}

	// get command line args to determine what our idle time should be
	LPWSTR* argList;
	int argCount = 0;

	argList = CommandLineToArgvW(GetCommandLine(), &argCount);
	if (argList == nullptr) {
		OutputDebugString(L"Failed to parse command line");
		return ILL_EXITCODE_FAILEDTOPARSECMDLINE;
	}

	// expect two command line arguments (+1 for app name) -- length of time to consider idle, and the timeout on the dialogue before the lock occurs
	if (argCount != 3) {
		LocalFree(argList);
		MessageBox(NULL, L"Please provide two arguments on the command line, or in the shortcut to this application.\n * length of time to consider idle (seconds)\n * timeout on the dialogue before the lock occurs (seconds)\n", L"Invalid command line arguments", MB_OK);
		return ILL_EXITCODE_INVALIDARGC;
	}


	for (int i = 0; i < argCount; i++) {
		switch (i) {
		case 1:
			// idle time
			idleSeconds = wcstol(argList[i], nullptr, 10);
			if (idleSeconds == 0) {
				LocalFree(argList);
				MessageBox(NULL, L"The length of time to consider idle (seconds) could not be understood as a number.", L"Invalid command line arguments", MB_OK);
				return ILL_EXITCODE_ARGVNAN;
			}
			break;

		case 2:
			// dialogue box grace period
			gracePeriodSeconds = wcstol(argList[i], nullptr, 10);
			if (gracePeriodSeconds == 0) {
				LocalFree(argList);
				MessageBox(NULL, L"The timeout on the dialogue before the lock occurs (seconds) could not be understood as a number.", L"Invalid command line arguments", MB_OK);
				return ILL_EXITCODE_ARGVNAN;
			}
			break;
		}
	}

	LocalFree(argList); // tidy up as CommandLineToArgvW requires us to do

	DEBUG_BUFFER;
	if (swprintf_s(debugStrBuffer, bufLen, L"Started IdleLockLite with arguments %ld %ld\n", idleSeconds, gracePeriodSeconds)) {
		OutputDebugString(debugStrBuffer);
	}

	
	// Set up our class

	const wchar_t CLASS_NAME[] = L"IdleLockLite";
	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.lpszClassName = CLASS_NAME;
	RegisterClass(&wc);

	llkeyboardHandle = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionKeyboard"), hInstance, 0);
	llmouseHandle = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)GetProcAddress(hInstance, "UpdateLastInteractionMouse"), hInstance, 0);


	if (swprintf_s(debugStrBuffer, bufLen, L"Began with tick count %lld\n", GetTickCount64()) > 0) {
		OutputDebugString(debugStrBuffer);
	}

	lastInteraction = GetTickCount64(); // seed this so we don't evaluate with 0

	// set a timer to evaluate if we should spawn the lock dialogue because of inactivity -- this won't do anything until the tick duration is calculated
	timer = SetTimer(NULL, 0, timerFrequency, EvaluateIdleConditions);

	// calculate the tick duration and set the ticks considered idle
	tickCalculatorStart = GetTickCount64();
	calculateTicksTimer = SetTimer(NULL, 0, timerFrequency, CalculateTickDuration);

	// main message loop
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Cleanup();
	
	return 0;
}


void Cleanup()
{
	// clean up

	assert(llmouseHandle != NULL);
	assert(llkeyboardHandle != NULL);
	UnhookWindowsHookEx(llmouseHandle);
	UnhookWindowsHookEx(llkeyboardHandle);
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
		Cleanup();
		PostQuitMessage(0);
		return 0;

	case WM_ENDSESSION:
		Cleanup();
		return 0;
		break;

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

extern "C" __declspec(dllexport) void EvaluateIdleConditions(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount)
{
	DEBUG_BUFFER;

	if (roughTicksConsideredIdle == 0) {
		// not yet calculated -- bail
		return;
	}

	ULONGLONG currentTicks = GetTickCount64();

	if (swprintf_s(debugStrBuffer, bufLen, L"Evaluate idle: current %lld, lastinteraction %lld, roughTicksConsideredIdle %lld, target %lld\n", currentTicks, lastInteraction, roughTicksConsideredIdle, (lastInteraction + roughTicksConsideredIdle))) {
		OutputDebugString(debugStrBuffer);
	}

	if (currentTicks > lastInteraction + roughTicksConsideredIdle) {
		OutputDebugString(L"Idle detected\n");

	}

}

extern "C" __declspec(dllexport) void CalculateTickDuration(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount)
{
	DEBUG_BUFFER;

	// we ran for timerFrequency ms -- so the number of milliseconds per tick is now - start divided by timerFrequency
	ULONGLONG ticksNow = GetTickCount64();
	msPerTick = (ticksNow - tickCalculatorStart) / timerFrequency;

	if (swprintf_s(debugStrBuffer, bufLen, L"Calculate tick duration: now %lld, start %lld, frequency %d, result %lld\n", ticksNow, tickCalculatorStart, timerFrequency, msPerTick)) {
		OutputDebugString(debugStrBuffer);
	}

	// what do those numbers mean in terms of ticks?
	roughTicksConsideredIdle = (idleSeconds * (ULONGLONG)1000) / msPerTick;
	gracePeriod = (gracePeriodSeconds * (ULONGLONG)1000) / msPerTick;

	if (swprintf_s(debugStrBuffer, bufLen, L"Rough ticks considered idle: %lld\n", roughTicksConsideredIdle)) {
		OutputDebugString(debugStrBuffer);
	}
	if (swprintf_s(debugStrBuffer, bufLen, L"Grace period: %lld\n", gracePeriod)) {
		OutputDebugString(debugStrBuffer);
	}

	KillTimer(wnd, timerIdentifier); // unhook ourselves
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