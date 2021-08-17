// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#ifdef NDEBUG
constexpr void mark_as_foreground_thread() {}
constexpr void assert_foreground_thread() {}
constexpr void assert_background_thread() {}
#else
void mark_as_foreground_thread();
void assert_foreground_thread();
void assert_background_thread();
#endif
