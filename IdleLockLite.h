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
#include <Windows.h>
#include <tchar.h>
#include <assert.h>

#define DEBUG_BUFFER const int bufLen = 512; wchar_t debugStrBuffer[bufLen];

#pragma once

BOOLEAN AlreadyRunning();
void Cleanup();
LRESULT CALLBACK WndProc(_In_ HWND hWnd,
    _In_ UINT message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);
void DebugShowTickCount(LPCWSTR context, DWORD hookCalls);
extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionKeyboard(int nCode, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) LRESULT UpdateLastInteractionMouse(int nCode, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) void EvaluateIdleConditions(HWND wnd, UINT message, UINT_PTR timerIdentifier, DWORD tickCount);