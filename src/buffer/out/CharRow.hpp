/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- CharRow.hpp

Abstract:
- contains data structure for UCS2 encoded character data of a row

Author(s):
- Michael Niksa (miniksa) 10-Apr-2014
- Paul Campbell (paulcam) 10-Apr-2014

Revision History:
- From components of output.h/.c
  by Therese Stowell (ThereseS) 1990-1991
- Pulled into its own file from textBuffer.hpp/cpp (AustDi, 2017)
--*/

#pragma once

#include "DbcsAttribute.hpp"
#include "CharRowCellReference.hpp"
#include "CharRowCell.hpp"
#include "UnicodeStorage.hpp"

class ROW;

enum class DelimiterClass
{
    ControlChar,
    DelimiterChar,
    RegularChar
};

// the characters of one row of screen buffer
// we keep the following values so that we don't write
// more pixels to the screen than we have to:
// left is initialized to screenbuffer width.  right is
// initialized to zero.
//
//      [     foo.bar    12-12-61                       ]
//       ^    ^                  ^                     ^
//       |    |                  |                     |
//     Chars Left               Right                end of Chars buffer
class CharRow final
{
public:
    using glyph_type = wchar_t;
    using value_type = CharRowCell;
    using reference = CharRowCellReference;

    CharRow(CharRowCell* buffer, size_t rowWidth, ROW* const pParent) noexcept;

    size_t size() const noexcept;
    void Resize(CharRowCell* buffer, const size_t newSize) noexcept;
    size_t MeasureLeft() const noexcept;
    size_t MeasureRight() const;
    bool ContainsText() const noexcept;
    const DbcsAttribute& DbcsAttrAt(const size_t column) const;
    DbcsAttribute& DbcsAttrAt(const size_t column);
    void ClearGlyph(const size_t column);

    const DelimiterClass DelimiterClassAt(const size_t column, const std::wstring_view wordDelimiters) const;

    // working with glyphs
    const reference GlyphAt(const size_t column) const;
    reference GlyphAt(const size_t column);

    auto begin() noexcept
    {
        // gsl::span uses strict bounds checking even in Release mode.
        // While this can be useful, not even our STL is that strict.
        // --> Reduce iteration overhead in Release by returning pointers.
#ifdef NDEBUG
        return _data.data();
#else
        return _data.begin();
#endif
    }

    auto begin() const noexcept
    {
#ifdef NDEBUG
        return _data.data();
#else
        return _data.begin();
#endif
    }

    auto end() noexcept
    {
#ifdef NDEBUG
#pragma warning(suppress : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1).
        return _data.data() + _data.size();
#else
        return _data.end();
#endif
    }

    auto end() const noexcept
    {
#ifdef NDEBUG
#pragma warning(suppress : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1).
        return _data.data() + _data.size();
#else
        return _data.end();
#endif
    }

    UnicodeStorage& GetUnicodeStorage() noexcept;
    const UnicodeStorage& GetUnicodeStorage() const noexcept;
    COORD GetStorageKey(const size_t column) const noexcept;

    void UpdateParent(ROW* const pParent);

    friend CharRowCellReference;
    friend class ROW;

private:
    void Reset() noexcept;
    void ClearCell(const size_t column);
    std::wstring GetText() const;

protected:
    // storage for glyph data and dbcs attributes
    gsl::span<CharRowCell> _data;

    // ROW that this CharRow belongs to
    ROW* _pParent;
};

template<typename InputIt1, typename InputIt2, typename OutputIt>
void OverwriteColumns(InputIt1 startChars, InputIt1 endChars, InputIt2 startAttrs, OutputIt outIt)
{
    std::transform(
        startChars,
        endChars,
        startAttrs,
        outIt,
        [](const wchar_t wch, const DbcsAttribute attr) {
            return CharRow::value_type{ wch, attr };
        });
}
