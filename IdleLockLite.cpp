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
#include "resource.h"
#include <Windows.h>
#include <tchar.h>
#include <assert.h>
#include <TlHelp32.h>
#include <CommCtrl.h>
#include <WtsApi32.h>
#include <Shellapi.h>

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

// timers
UINT_PTR timer = 0;
UINT_PTR calculateTicksTimer = 0;
UINT_PTR stepProgressBarTimer = 0;

// idle dialogue
HWND idleDialogue = 0;

// window to receive WM_WTSSESSION_CHANGE
HWND hiddenWindow = 0;

// progress bar
HWND progressBar = 0;

/* The rough number of ticks we consider to be an idle condition.
Let's measure the ms per tick, roughly.
*/
ULONGLONG roughTicksConsideredIdle = 0;
ULONGLONG gracePeriod = 0;
ULONGLONG msPerTick = 16;


BOOL idleDetectionEnabled = false;


long int idleSeconds = 0;
long int gracePeriodSeconds = 0;
long int gracePeriodSecondsRemaining = 0;

constexpr int timerFrequency = 10000;


int APIENTRY WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow
)
{
	instance = hInstance;

	//InitCommonControls();

	if (AlreadyRunning())
	{
		OutputDebugString(L"Cannot run multiple instances\n");
		return ILL_EXITCODE_MULTIPLEINSTANCES;
	}

	// get command line args to determine what our idle time should be
	LPWSTR* argList;
	int argCount = 0;

	argList = CommandLineToArgvW(GetCommandLine(), &argCount);
	if (argList == nullptr) {
		OutputDebugString(L"Failed to parse command line\n");
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
			gracePeriodSecondsRemaining = gracePeriodSeconds;
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

	// hidden window to receive notifications of WTS_SESSION_UNLOCK
	hiddenWindow = CreateWindowExW(0, CLASS_NAME, L"IdleLockLite hidden", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, 0);

	// wait a little bit before actually hooking this -- RPC_S_INVALID_BINDING may be returned if Remote Desktop Services is not ready

	// hook keyboard and mouse to determine non-idleness
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
		if (!IsWindow(idleDialogue) || !IsDialogMessage(idleDialogue, &msg)) {
			/*if (msg.message == WM_TIMER) {
				OutputDebugString(L"Handling WM_TIMER outside dialogue");
			}*/
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	Cleanup();
	
	return 0;
}


void Cleanup()
{
	// clean up

	CleanupProgressBarTimer(idleDialogue);

	assert(llmouseHandle != NULL);
	assert(llkeyboardHandle != NULL);
	UnhookWindowsHookEx(llmouseHandle);
	UnhookWindowsHookEx(llkeyboardHandle);

	WTSUnRegisterSessionNotification(hiddenWindow);

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

	case WM_TIMER:

		OutputDebugString(L"IdleDialogueProcedure received WM_TIMER");
		return 0;
		break;

	case WM_PAINT:
		return 0;

	case WM_WTSSESSION_CHANGE:

		// determine if we are interested in this
		switch (wParam) {
		case WTS_SESSION_LOCK:
			// if the user locked the workstation (or if we triggered this), stop idle detection
			OutputDebugString(L"Session locked -- disabling detection\n");
			DestroyIdleDialogue();
			idleDetectionEnabled = false;
			break;
		case WTS_SESSION_UNLOCK:
			// if the user just unlocked the workstation, re-enable the idle detection
			OutputDebugString(L"Session unlocked -- enabling detection\n");
			lastInteraction = GetTickCount64();
			idleDetectionEnabled = true;
			break;
			
		}

		break;

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

	if (!idleDetectionEnabled) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	lastInteraction = GetTickCount64();

	DebugShowTickCount(L"Keyboard", hookCalls);
	if (nullptr != idleDialogue) { // performance we'll check for nullness inline
		DestroyIdleDialogue();
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}



extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionMouse(int nCode, WPARAM wParam, LPARAM lParam)
{
	hookCalls++;

	// reduce frequency of calling GetTickCount
	if ((hookCalls & 0xF) != 0) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	if (!idleDetectionEnabled) {
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	DebugShowTickCount(L"Mouse", hookCalls);
	lastInteraction = GetTickCount64();
	if (nullptr != idleDialogue) { // performance we'll check for nullness inline
		DestroyIdleDialogue();
	}
	

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void EvaluateIdleConditions(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount)
{
	DEBUG_BUFFER;

	if (roughTicksConsideredIdle == 0 || !idleDetectionEnabled) {
		// not yet calculated -- bail
		return;
	}

	// idle will not be detected if the user is in presentation mode etc.
	QUERY_USER_NOTIFICATION_STATE notificationState = {};
	if (S_OK == SHQueryUserNotificationState(&notificationState)) {
		if (notificationState == QUNS_NOT_PRESENT ||
			notificationState == QUNS_BUSY ||
			notificationState == QUNS_RUNNING_D3D_FULL_SCREEN ||
			notificationState == QUNS_QUIET_TIME) {
			OutputDebugString(L"Will not detect idle while user notification state contra-indicates idle\n");
			return;
		}
	}

	ULONGLONG currentTicks = GetTickCount64();

	if (swprintf_s(debugStrBuffer, bufLen, L"Evaluate idle: current %lld, lastinteraction %lld, roughTicksConsideredIdle %lld, target %lld\n", currentTicks, lastInteraction, roughTicksConsideredIdle, (lastInteraction + roughTicksConsideredIdle))) {
		OutputDebugString(debugStrBuffer);
	}

	if (currentTicks > lastInteraction + roughTicksConsideredIdle) {
		OutputDebugString(L"Idle detected\n");
		if (!IsWindow(idleDialogue)) {
			idleDialogue = CreateDialog(instance, MAKEINTRESOURCE(IDLEDIALOGUE), nullptr, (DLGPROC)IdleDialogueProcedure);
			ShowWindow(idleDialogue, SW_SHOW);
		}
		
	}

}

void CalculateTickDuration(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount)
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
	idleDetectionEnabled = true;

	if (nullptr != hiddenWindow) {
		WTSRegisterSessionNotification(hiddenWindow, NOTIFY_FOR_THIS_SESSION);
		OutputDebugString(L"Registering for session notifications\n");
	}

}

void DestroyIdleDialogue() {
	if (IsWindow(idleDialogue)) {

		CleanupProgressBarTimer(idleDialogue);

		DestroyWindow(idleDialogue);
		idleDialogue = nullptr;
		progressBar = nullptr; // needed to re-init this if it appears again
	}
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

BOOL CALLBACK IdleDialogueProcedure(HWND hwndDialogue, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL fError;


	switch (message)
	{
	case WM_INITDIALOG:

		// init the progress bar if necessary -- is this the right place for this??
		if (nullptr == progressBar) {
			OutputDebugString(L"Init progress bar\n");
			DEBUG_BUFFER;
			progressBar = GetDlgItem(hwndDialogue, PROGRESSBAR);
			if (swprintf_s(debugStrBuffer, bufLen, L"Error: %d\n", GetLastError()) > 0) {
				OutputDebugString(debugStrBuffer);
			}
			SendMessage(progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, gracePeriodSeconds));
			SendMessage(progressBar, PBM_SETSTEP, (WPARAM)1, 0);

			if (swprintf_s(debugStrBuffer, bufLen, L"Range: %d\n", gracePeriodSeconds) > 0) {
				OutputDebugString(debugStrBuffer);
			}

			// set up the timer
			if (NULL != stepProgressBarTimer) {
				OutputDebugString(L"Tidying existing progress bar timer\n");
				CleanupProgressBarTimer(hwndDialogue);
			}
			stepProgressBarTimer = SetTimer(hwndDialogue, 1, 1000, StepProgressBar);
			OutputDebugString(L"Set progress bar timer\n");
		}
		return TRUE;

	case WM_WINDOWPOSCHANGED:
		SetWindowPos(hwndDialogue, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
		break;

	case WM_PAINT:



		break;

	case WM_TIMER:
		OutputDebugString(L"IdleDialogueProcedure received WM_TIMER");
		return TRUE;
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			
				return TRUE;

		case IDCANCEL:
			CleanupProgressBarTimer(hwndDialogue);
			DestroyWindow(hwndDialogue);
			idleDialogue = NULL;
			return TRUE;
		}
	}
	return FALSE;
}

void CleanupProgressBarTimer(const HWND& hwndDialogue)
{
	if (NULL != stepProgressBarTimer) {
		KillTimer(hwndDialogue, stepProgressBarTimer);
		stepProgressBarTimer = NULL;
	}
	gracePeriodSecondsRemaining = gracePeriodSeconds;
}


void StepProgressBar(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount)
{
	DEBUG_BUFFER;

	if (swprintf_s(debugStrBuffer, bufLen, L"Step progress bar: %lld\n", GetTickCount64()) > 0) {
		OutputDebugString(debugStrBuffer);
	}

	if (nullptr != progressBar) {
		SendMessage(progressBar, PBM_STEPIT, 0, 0);
	}

	gracePeriodSecondsRemaining -= 1;

	
	if (swprintf_s(debugStrBuffer, bufLen, L"Grace period remaining: %ld\n", gracePeriodSecondsRemaining) > 0) {
		OutputDebugString(debugStrBuffer);
	}

	if (gracePeriodSecondsRemaining < 1) {
		LockScreen();
	}
}

void LockScreen() {
	CleanupProgressBarTimer(idleDialogue);
	DestroyIdleDialogue();

	LockWorkStation();
}