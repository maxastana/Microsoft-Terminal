/*++
Copyright (c) Microsoft Corporation

Module Name:
- GlyphWidth.hpp

Abstract:
- Helpers for determining the width of a particular string of chars.

*/

#include <functional>
#include <string_view>

bool IsGlyphFullWidth(const std::wstring_view& glyph) noexcept;
bool IsGlyphFullWidth(const wchar_t wch) noexcept;
void SetGlyphWidthFallback(std::function<bool(const std::wstring_view&)> pfnFallback);
void NotifyGlyphWidthFontChanged() noexcept;
