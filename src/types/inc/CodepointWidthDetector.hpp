/*++
Copyright (c) Microsoft Corporation

Module Name:
- CodepointWidthDetector.hpp

Abstract:
- Object used to measure the width of a codepoint when it's rendered

Author:
- Austin Diviness (AustDi) 18-May-2018
--*/

#pragma once

#include "convert.hpp"
#include <functional>

// use to measure the width of a codepoint
class CodepointWidthDetector final
{
public:
    CodepointWidthDetector() noexcept = default;

    CodepointWidthDetector(const CodepointWidthDetector&) = delete;
    CodepointWidthDetector(CodepointWidthDetector&&) = delete;
    CodepointWidthDetector& operator=(const CodepointWidthDetector&) = delete;
    CodepointWidthDetector& operator=(CodepointWidthDetector&&) = delete;

    CodepointWidth GetWidth(const std::wstring_view& glyph) const noexcept;
    bool IsWide(const std::wstring_view& glyph) const noexcept;
    bool IsWide(const wchar_t wch) const noexcept;
    void SetFallbackMethod(std::function<bool(const std::wstring_view&)> pfnFallback) noexcept;
    void NotifyFontChanged() const noexcept;

#ifdef UNIT_TESTING
    friend class CodepointWidthDetectorTests;
#endif

private:
    CodepointWidth _lookupGlyphWidth(uint32_t codepoint) const;
    bool _checkFallbackViaCache(uint32_t codepoint, const std::wstring_view& glyph) const;

    mutable robin_hood::unordered_flat_map<uint32_t, bool> _fallbackCache;
    std::function<bool(const std::wstring_view&)> _pfnFallbackMethod;
};
