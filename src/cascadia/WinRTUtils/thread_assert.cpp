// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "thread_assert.h"

#ifndef NDEBUG

static const DWORD foreground_thread_id = []() -> DWORD {
    const auto id = GetCurrentThreadId();
    OutputDebugStringW((std::wstring(L"#### ") + std::to_wstring(id) + L" ####").c_str());
    return id;
}();

void assert_foreground_thread() {
    if (foreground_thread_id != GetCurrentThreadId()) {
        //throw std::exception("expected to be called from a foreground thread");
    }
}

void assert_background_thread() {
    if (foreground_thread_id != GetCurrentThreadId()) {
        throw std::exception("expected to be called from a background thread");
    }
}

#endif // NDEBUG
