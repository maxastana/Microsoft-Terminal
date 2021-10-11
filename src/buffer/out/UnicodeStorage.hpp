/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- UnicodeStorage.hpp

Abstract:
- dynamic storage location for glyphs that can't normally fit in the output buffer

Author(s):
- Austin Diviness (AustDi) 02-May-2018
--*/

#pragma once

namespace std
{
    template<>
    struct hash<COORD>
    {
        // We take COORD by value not just because it neatly fits into a register...
        // Reading unaligned pointers doesn't work
        size_t operator()(COORD coord) const noexcept
        {
            return std::hash<uint32_t>{}(til::bit_cast<uint32_t>(coord));
        }
    };
}

class UnicodeStorage final
{
public:
    std::wstring_view GetText(const COORD key) const;
    void StoreGlyph(const COORD key, const std::wstring_view& glyph);
    void Erase(const COORD key) noexcept;
    void Remap(const std::unordered_map<SHORT, SHORT>& rowMap, const std::optional<SHORT> width);

private:
    std::unordered_map<COORD, std::array<wchar_t, 2>> _map;

#ifdef UNIT_TESTING
    friend class UnicodeStorageTests;
    friend class TextBufferTests;
#endif
};
