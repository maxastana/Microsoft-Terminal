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
    template<typename T, typename U>
    CodepointWidth _getCodepointWidth(const T& table, U codepoint, const std::wstring_view& glyph) const noexcept
    {
    #pragma warning(suppress : 26447) // The function is declared 'noexcept' but calls function 'lower_bound<...>()' which may throw exceptions (f.6).
        const auto it = std::lower_bound(table.begin(), table.end(), codepoint);

        // (it->upperBound - it->boundWidth) is equal to the lowerBound of the code point range.
        if (it != table.end() && codepoint >= (it->upperBound - it->boundWidth)) {
            return it->isAmbiguous ? _checkFallbackViaCache(codepoint, glyph) : CodepointWidth::Wide;
        }

        return CodepointWidth::Narrow;
    }

    CodepointWidth _checkFallbackViaCache(uint32_t codepoint, const std::wstring_view& glyph) const noexcept;

    mutable std::unordered_map<uint32_t, CodepointWidth> _fallbackCache;
    std::function<bool(const std::wstring_view&)> _pfnFallbackMethod;
};
