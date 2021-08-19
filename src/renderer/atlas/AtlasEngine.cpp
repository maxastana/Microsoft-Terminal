// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AtlasEngine.h"

#include "../../interactivity/win32/CustomWindowMessages.h"

#include "shader_vs.h"
#include "shader_ps.h"

#pragma warning(disable : 4100)

using namespace Microsoft::Console::Render;

// Like gsl::narrow but living fast and dying young.
// I don't want to handle users passing fonts larger than 65535pt.
template<typename T, typename U>
constexpr T yolo_narrow(U u) noexcept
{
    const auto t = static_cast<T>(u);
    if (static_cast<U>(t) != u || std::is_signed_v<T> != std::is_signed_v<U> && t < T{} != u < U{})
    {
        FAIL_FAST();
    }
    return t;
}

template<typename T>
constexpr AtlasEngine::vec2<T> yolo_vec2(COORD val) noexcept
{
    return { yolo_narrow<T>(val.X), yolo_narrow<T>(val.Y) };
}

template<typename T>
constexpr AtlasEngine::vec2<T> yolo_vec2(SIZE val) noexcept
{
    return { yolo_narrow<T>(val.cx), yolo_narrow<T>(val.cy) };
}

#define getLocaleName(varName)               \
    wchar_t varName[LOCALE_NAME_MAX_LENGTH]; \
    getLocaleNameImpl(varName);

static void getLocaleNameImpl(wchar_t (&localeName)[LOCALE_NAME_MAX_LENGTH])
{
    if (!GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH))
    {
        static constexpr wchar_t fallback[] = L"en-US";
        memcpy(localeName, fallback, sizeof(fallback));
    }
    // GetUserDefaultLocaleName can return bullshit locales with trailing underscores. Strip it off.
    // See: https://docs.microsoft.com/en-us/windows/win32/intl/locale-names
    else if (auto p = wcschr(localeName, L'_'))
    {
        *p = L'\0';
    }
}

static uint32_t utf16utf32(const std::wstring_view& str)
{
    uint32_t codepoint;
#pragma warning(suppress : 26446) // Prefer to use gsl::at() instead of unchecked subscript operator (bounds.4).
    switch (str.size())
    {
    case 1:
        codepoint = str[0];
        break;
    case 2:
        codepoint = (str[0] & 0x3FF) << 10;
        codepoint |= str[1] & 0x3FF;
        codepoint += 0x10000;
        break;
    default:
        codepoint = 0xFFFD; // UNICODE_REPLACEMENT
        break;
    }
    return codepoint;
}

static uint32_t utf32utf16(uint32_t in, wchar_t (&out)[2])
{
    if (in < 0x10000)
    {
        out[0] = static_cast<wchar_t>(in);
        return 1;
    }

    in -= 0x10000;
    out[0] = static_cast<wchar_t>(in >> 10);
    out[1] = static_cast<wchar_t>(in & 0x3FF);
    return 2;
}

AtlasEngine::AtlasEngine()
{
    THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, _uuidof(_sr.d2dFactory), _sr.d2dFactory.put_void()));
    THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(_sr.dwriteFactory), _sr.dwriteFactory.put_unknown()));
    _sr.isWindows10OrGreater = IsWindows10OrGreater();
    _r.glyphQueue.reserve(64);
}

#pragma region IRenderEngine

// StartPaint() is called while the console buffer lock is being held.
// --> Put as little in here as possible.
[[nodiscard]] HRESULT AtlasEngine::StartPaint() noexcept
try
{
    if (_api.hwnd)
    {
        RECT rect;
        LOG_IF_WIN32_BOOL_FALSE(GetClientRect(_api.hwnd, &rect));
        (void)SetWindowSize({ rect.right - rect.left, rect.bottom - rect.top });

        if (WI_IsFlagSet(_invalidations, invalidation_flags::title))
        {
            LOG_IF_WIN32_BOOL_FALSE(PostMessageW(_api.hwnd, CM_UPDATE_TITLE, 0, 0));
            WI_ClearFlag(_invalidations, invalidation_flags::title);
        }
    }

    // It's important that we invalidate here instead of in Present() with the rest.
    // Other functions, those called before Present(), might depend on _r fields.
    // But most of the time _invalidations will be ::none, making this very cheap.
    if (_invalidations != invalidation_flags::none)
    {
        FAIL_FAST_IF(_api.sizeInPixel == u16x2{} || _api.cellSize == u16x2{} || _api.cellCount == u16x2{});

        if (WI_IsFlagSet(_invalidations, invalidation_flags::device))
        {
            _createResources();
            WI_ClearFlag(_invalidations, invalidation_flags::device);
        }
        if (WI_IsFlagSet(_invalidations, invalidation_flags::size))
        {
            _recreateSizeDependentResources();
            WI_ClearFlag(_invalidations, invalidation_flags::size);
        }
        if (WI_IsFlagSet(_invalidations, invalidation_flags::font))
        {
            _recreateFontDependentResources();
            WI_ClearFlag(_invalidations, invalidation_flags::font);
        }
    }

    _rapi.dirtyArea = til::rectangle{ 0u, 0u, static_cast<size_t>(_api.cellCount.x), static_cast<size_t>(_api.cellCount.y) };
    return S_OK;
}
catch (const wil::ResultException& exception)
{
    return _handleException(exception);
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::EndPaint() noexcept
{
    return S_OK;
}

[[nodiscard]] bool AtlasEngine::RequiresContinuousRedraw() noexcept
{
    return false;
}

void AtlasEngine::WaitUntilCanRender() noexcept
{
    if (_r.frameLatencyWaitableObject)
    {
        WaitForSingleObjectEx(_r.frameLatencyWaitableObject.get(), 1000, true);
    }
    else
    {
        Sleep(8);
    }
}

// Present() is called without the console buffer lock being held.
// --> Put as much in here as possible.
[[nodiscard]] HRESULT AtlasEngine::Present() noexcept
try
{
    if (!_r.glyphQueue.empty())
    {
        for (const auto& pair : _r.glyphQueue)
        {
            _drawGlyph(pair);
        }
        _r.glyphQueue.clear();
    }

    // The values the constant buffer depends on are potentially updated after BeginPaint().
    if (WI_IsFlagSet(_invalidations, invalidation_flags::cbuffer))
    {
        _updateConstantBuffer();
        WI_ClearFlag(_invalidations, invalidation_flags::cbuffer);
    }

    {
#pragma warning(suppress : 26494) // Variable 'mapped' is uninitialized. Always initialize an object (type.5).
        D3D11_MAPPED_SUBRESOURCE mapped;
        THROW_IF_FAILED(_r.deviceContext->Map(_r.cellBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        assert(mapped.RowPitch >= _r.cells.size() * sizeof(cell));
        memcpy(mapped.pData, _r.cells.data(), _r.cells.size() * sizeof(cell));
        _r.deviceContext->Unmap(_r.cellBuffer.get(), 0);
    }

    // After Present calls, the back buffer needs to explicitly be
    // re-bound to the D3D11 immediate context before it can be used again.
    _r.deviceContext->OMSetRenderTargets(1, _r.renderTargetView.addressof(), nullptr);
    _r.deviceContext->Draw(3, 0);

    THROW_IF_FAILED(_r.swapChain->Present(1, 0));

    // On some GPUs with tile based deferred rendering (TBDR) architectures, binding
    // RenderTargets that already have contents in them (from previous rendering) incurs a
    // cost for having to copy the RenderTarget contents back into tile memory for rendering.
    //
    // On Windows 10 with DXGI_SWAP_EFFECT_FLIP_DISCARD we get this for free.
    if (!_sr.isWindows10OrGreater)
    {
        _r.deviceContext->DiscardView(_r.renderTargetView.get());
    }

    return S_OK;
}
catch (const wil::ResultException& exception)
{
    return _handleException(exception);
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::PrepareForTeardown(_Out_ bool* const pForcePaint) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pForcePaint);
    *pForcePaint = false;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::ScrollFrame() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::Invalidate(const SMALL_RECT* const psrRegion) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateCursor(const SMALL_RECT* const psrRegion) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateSystem(const RECT* const prcDirtyClient) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateSelection(const std::vector<SMALL_RECT>& rectangles) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateScroll(const COORD* const pcoordDelta) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateAll() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateCircling(_Out_ bool* const pForcePaint) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pForcePaint);
    *pForcePaint = false;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateTitle() noexcept
{
    WI_SetFlag(_invalidations, invalidation_flags::title);
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PrepareRenderInfo(const RenderFrameInfo& info) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::ResetLineTransform() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PrepareLineTransform(const LineRendition lineRendition, const size_t targetRow, const size_t viewportLeft) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintBackground() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintBufferLine(const gsl::span<const Cluster> clusters, const COORD coord, const bool fTrimLeft, const bool lineWrapped) noexcept
try
{
    auto data = _getCell(coord.X, coord.Y);
    for (const auto& cluster : clusters)
    {
        const auto codepoint = utf16utf32(cluster.GetText());
        const uint32_t wide = cluster.GetColumns() != 1;
        const uint32_t cells = wide + 1;

        auto entry = _rapi.attributes;
        entry.codepoint = codepoint;
        entry.wide = wide;

        auto& coords = _r.glyphs[entry];
        if (coords[0] == u16x2{})
        {
            coords[0] = _allocateAtlasCell();
            if (wide)
            {
                coords[1] = _allocateAtlasCell();
            }
            _r.glyphQueue.emplace_back(entry, coords);
        }

        static_assert(sizeof(data->glyphIndex) == sizeof(coords[0]));

        for (uint32_t i = 0; i < cells; ++i)
        {
            data[i].glyphIndex16 = coords[i];
            data[i].flags = 0;
            data[i].color = _rapi.currentColor;
        }

#pragma warning(suppress : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1).
        data += cluster.GetColumns();
    }

    assert(data <= (_r.cells.data() + _r.cells.size()));
    return S_OK;
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::PaintBufferGridLines(const GridLines lines, const COLORREF color, const size_t cchLine, const COORD coordTarget) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintSelection(const SMALL_RECT rect) noexcept
{
    const auto width = rect.Right - rect.Left;
    auto row = _getCell(rect.Left, rect.Top);

    for (auto x = rect.Top, x1 = rect.Bottom; x < x1; ++x, row += _api.cellCount.x)
    {
        for (auto data = row, dataEnd = row + width; data != dataEnd; ++data)
        {
            data->flags |= 2;
        }
    }

    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintCursor(const CursorOptions& options) noexcept
{
    if (options.isOn)
    {
        auto data = _getCell(options.coordCursor.X, options.coordCursor.Y);
        const auto end = std::min(data + options.fIsDoubleWidth + 1, _r.cells.data() + _r.cells.size());

        for (; data != end; ++data)
        {
            data->flags |= 1;
        }
    }

    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateDrawingBrushes(const TextAttribute& textAttributes, const gsl::not_null<IRenderData*> pData, const bool usingSoftFont, const bool isSettingDefaultBrushes) noexcept
{
    const auto [fg, bg] = pData->GetAttributeColors(textAttributes);

    if (!isSettingDefaultBrushes)
    {
        _rapi.currentColor = { fg, bg };
        _rapi.attributes.bold = textAttributes.IsBold();
        _rapi.attributes.italic = textAttributes.IsItalic();
    }
    else if (textAttributes.BackgroundIsDefault() && bg != _rapi.backgroundColor)
    {
        _rapi.backgroundColor = bg;
        WI_SetFlag(_invalidations, invalidation_flags::cbuffer);
    }

    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateFont(const FontInfoDesired& fontInfoDesired, _Out_ FontInfo& fontInfo) noexcept
{
    return UpdateFont(fontInfoDesired, fontInfo, {}, {});
}

[[nodiscard]] HRESULT AtlasEngine::UpdateSoftFont(const gsl::span<const uint16_t> bitPattern, const SIZE cellSize, const size_t centeringHint) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateDpi(const int dpi) noexcept
{
    const auto newDPI = yolo_narrow<u16>(dpi);
    if (_api.dpi != newDPI)
    {
        _api.dpi = newDPI;
        WI_SetFlag(_invalidations, invalidation_flags::font);
    }
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateViewport(const SMALL_RECT srNewViewport) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::GetProposedFont(const FontInfoDesired& fontInfoDesired, _Out_ FontInfo& fontInfo, const int dpi) noexcept
{
    const auto scaling = GetScaling();
    const auto coordFontRequested = fontInfoDesired.GetEngineSize();
    wil::unique_hfont hfont;
    COORD coordSize;

    if (fontInfoDesired.IsDefaultRasterFont())
    {
        hfont.reset(static_cast<HFONT>(GetStockObject(OEM_FIXED_FONT)));
        RETURN_HR_IF(E_FAIL, !hfont);
    }
    else if (fontInfoDesired.GetFaceName() == DEFAULT_RASTER_FONT_FACENAME)
    {
        // For future reference, here is the engine weighting and internal details on Windows Font Mapping:
        // https://msdn.microsoft.com/en-us/library/ms969909.aspx
        // More relevant links:
        // https://support.microsoft.com/en-us/kb/94646

        LOGFONTW lf;
        lf.lfHeight = yolo_narrow<LONG>(std::ceil(coordFontRequested.Y * scaling));
        lf.lfWidth = 0;
        lf.lfEscapement = 0;
        lf.lfOrientation = 0;
        lf.lfWeight = fontInfoDesired.GetWeight();
        lf.lfItalic = FALSE;
        lf.lfUnderline = FALSE;
        lf.lfStrikeOut = FALSE;
        lf.lfCharSet = OEM_CHARSET;
        lf.lfOutPrecision = OUT_RASTER_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = PROOF_QUALITY;
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        wmemcpy(lf.lfFaceName, DEFAULT_RASTER_FONT_FACENAME, std::size(DEFAULT_RASTER_FONT_FACENAME));

        hfont.reset(CreateFontIndirectW(&lf));
        RETURN_HR_IF(E_FAIL, !hfont);
    }

    if (hfont)
    {
        wil::unique_hdc hdc(CreateCompatibleDC(nullptr));
        RETURN_HR_IF(E_FAIL, !hdc);

        DeleteObject(SelectObject(hdc.get(), hfont.get()));

        SIZE sz;
        RETURN_HR_IF(E_FAIL, !GetTextExtentPoint32W(hdc.get(), L"M", 1, &sz));

        coordSize.X = yolo_narrow<SHORT>(sz.cx);
        coordSize.Y = yolo_narrow<SHORT>(sz.cy);
    }
    else
    {
        getLocaleName(localeName);
        const auto textFormat = _createTextFormat(
            fontInfoDesired.GetFaceName().c_str(),
            static_cast<DWRITE_FONT_WEIGHT>(fontInfoDesired.GetWeight()),
            DWRITE_FONT_STYLE_NORMAL,
            fontInfoDesired.GetEngineSize().Y,
            localeName);

        wil::com_ptr<IDWriteTextLayout> textLayout;
        RETURN_IF_FAILED(_sr.dwriteFactory->CreateTextLayout(L"M", 1, textFormat.get(), FLT_MAX, FLT_MAX, textLayout.put()));

        DWRITE_TEXT_METRICS metrics;
        RETURN_IF_FAILED(textLayout->GetMetrics(&metrics));

        coordSize.X = yolo_narrow<SHORT>(std::ceil(metrics.width * scaling));
        coordSize.Y = yolo_narrow<SHORT>(std::ceil(metrics.height * scaling));
    }

    fontInfo.SetFromEngine(
        fontInfoDesired.GetFaceName(),
        fontInfoDesired.GetFamily(),
        fontInfoDesired.GetWeight(),
        false,
        coordSize,
        fontInfoDesired.GetEngineSize());
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::GetDirtyArea(gsl::span<const til::rectangle>& area) noexcept
{
    area = gsl::span{ &_rapi.dirtyArea, 1 };
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::GetFontSize(_Out_ COORD* const pFontSize) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pFontSize);
    pFontSize->X = gsl::narrow_cast<SHORT>(_api.cellSize.x);
    pFontSize->Y = gsl::narrow_cast<SHORT>(_api.cellSize.y);
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::IsGlyphWideByFont(const std::wstring_view& glyph, _Out_ bool* const pResult) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pResult);

    wil::com_ptr<IDWriteTextLayout> textLayout;
    RETURN_IF_FAILED(_sr.dwriteFactory->CreateTextLayout(glyph.data(), yolo_narrow<uint32_t>(glyph.size()), _getTextFormat(false, false), FLT_MAX, FLT_MAX, textLayout.put()));

    DWRITE_TEXT_METRICS metrics;
    RETURN_IF_FAILED(textLayout->GetMetrics(&metrics));

    *pResult = static_cast<unsigned int>(std::ceil(metrics.width)) > _api.cellSize.x;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateTitle(const std::wstring_view newTitle) noexcept
{
    return S_OK;
}

#pragma endregion

#pragma region DxRenderer

[[nodiscard]] bool AtlasEngine::GetRetroTerminalEffect() const noexcept
{
    return false;
}

[[nodiscard]] float AtlasEngine::GetScaling() const noexcept
{
    return static_cast<float>(_api.dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] HANDLE AtlasEngine::GetSwapChainHandle()
{
    if (!_r.device)
    {
        _createResources();
    }
    return _r.swapChainHandle.get();
}

[[nodiscard]] ::Microsoft::Console::Types::Viewport AtlasEngine::GetViewportInCharacters(const ::Microsoft::Console::Types::Viewport& viewInPixels) const noexcept
{
    return ::Microsoft::Console::Types::Viewport::FromDimensions(viewInPixels.Origin(), COORD{ gsl::narrow_cast<short>(viewInPixels.Width() / _api.cellSize.x), gsl::narrow_cast<short>(viewInPixels.Height() / _api.cellSize.y) });
}

[[nodiscard]] ::Microsoft::Console::Types::Viewport AtlasEngine::GetViewportInPixels(const ::Microsoft::Console::Types::Viewport& viewInCharacters) const noexcept
{
    return ::Microsoft::Console::Types::Viewport::FromDimensions(viewInCharacters.Origin(), COORD{ gsl::narrow_cast<short>(viewInCharacters.Width() * _api.cellSize.x), gsl::narrow_cast<short>(viewInCharacters.Height() * _api.cellSize.y) });
}

void AtlasEngine::SetAntialiasingMode(const D2D1_TEXT_ANTIALIAS_MODE antialiasingMode) noexcept
{
    _api.antialiasingMode = yolo_narrow<u16>(antialiasingMode);
    WI_SetFlag(_invalidations, invalidation_flags::font);
}

void AtlasEngine::SetCallback(std::function<void()> pfn)
{
    _api.swapChainChangedCallback = std::move(pfn);
}

void AtlasEngine::SetDefaultTextBackgroundOpacity(const float opacity) noexcept
{
}

void AtlasEngine::SetForceFullRepaintRendering(bool enable) noexcept
{
}

[[nodiscard]] HRESULT AtlasEngine::SetHwnd(const HWND hwnd) noexcept
{
    _api.hwnd = hwnd;
    return S_OK;
}

void AtlasEngine::SetPixelShaderPath(std::wstring_view value) noexcept
{
}

void AtlasEngine::SetRetroTerminalEffect(bool enable) noexcept
{
}

void AtlasEngine::SetSelectionBackground(const COLORREF color, const float alpha) noexcept
{
    const u32 selectionColor = color | static_cast<u32>(std::lroundf(alpha * 255.0f)) << 24;

    if (_rapi.selectionColor != selectionColor)
    {
        _rapi.selectionColor = selectionColor;
        WI_SetFlag(_invalidations, invalidation_flags::cbuffer);
    }
}

void AtlasEngine::SetSoftwareRendering(bool enable) noexcept
{
}

void AtlasEngine::SetWarningCallback(std::function<void(const HRESULT)> pfn)
{
}

[[nodiscard]] HRESULT AtlasEngine::SetWindowSize(const SIZE pixels) noexcept
{
    // At the time of writing:
    // When Win+D is pressed a render pass is initiated. As conhost is in the background, GetClientRect will return {0,0}.
    // This isn't a valid value for _api.sizeInPixel and would crash _recreateSizeDependentResources().
    if (const auto newSize = yolo_vec2<u16>(pixels); _api.sizeInPixel != newSize && newSize != u16x2{})
    {
        _api.sizeInPixel = newSize;
        _api.cellCount = _api.sizeInPixel / _api.cellSize;
        WI_SetFlag(_invalidations, invalidation_flags::size);
    }

    return S_OK;
}

void AtlasEngine::ToggleShaderEffects()
{
}

[[nodiscard]] HRESULT AtlasEngine::UpdateFont(const FontInfoDesired& fontInfoDesired, FontInfo& fontInfo, const std::unordered_map<std::wstring_view, uint32_t>& features, const std::unordered_map<std::wstring_view, float>& axes) noexcept
try
{
    RETURN_IF_FAILED(GetProposedFont(fontInfoDesired, fontInfo, _api.dpi));

    _api.fontSize = fontInfoDesired.GetEngineSize().Y;
    _api.fontName = fontInfo.GetFaceName();
    _api.fontWeight = yolo_narrow<u16>(fontInfo.GetWeight());

    WI_SetFlag(_invalidations, invalidation_flags::font);

    if (const auto newSize = yolo_vec2<u16>(fontInfo.GetSize()); _api.cellSize != newSize)
    {
        const auto scaling = GetScaling();
        _api.cellSizeDIP.x = static_cast<float>(newSize.x) / scaling;
        _api.cellSizeDIP.y = static_cast<float>(newSize.y) / scaling;
        _api.cellSize = newSize;
        _api.cellCount = _api.sizeInPixel / _api.cellSize;
        WI_SetFlag(_invalidations, invalidation_flags::size);
    }

    return S_OK;
}
CATCH_RETURN()

void AtlasEngine::UpdateHyperlinkHoveredId(const uint16_t hoveredId) noexcept
{
}

#pragma endregion

[[nodiscard]] HRESULT AtlasEngine::_handleException(const wil::ResultException& exception) noexcept
{
    const auto hr = exception.GetErrorCode();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET)
    {
        _r = {};
        WI_SetFlag(_invalidations, invalidation_flags::device);
        return E_PENDING; // Indicate a retry to the renderer
    }
    return hr;
}

void AtlasEngine::_createResources()
{
#ifdef NDEBUG
    static constexpr
#endif
        auto deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifndef NDEBUG
    // DXGI debug messages + enabling D3D11_CREATE_DEVICE_DEBUG if the Windows SDK was installed.
    if (const wil::unique_hmodule module{ LoadLibraryExW(L"dxgidebug.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) })
    {
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

        const auto DXGIGetDebugInterface = reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(module.get(), "DXGIGetDebugInterface"));
        THROW_LAST_ERROR_IF(!DXGIGetDebugInterface);

        wil::com_ptr<IDXGIInfoQueue> infoQueue;
        if (SUCCEEDED(DXGIGetDebugInterface(IID_PPV_ARGS(infoQueue.addressof()))))
        {
            // I didn't want to link with dxguid.lib just for getting DXGI_DEBUG_ALL.
            static constexpr GUID dxgiDebugAll = { 0xe48ae283, 0xda80, 0x490b, { 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8 } };
            for (const auto severity : std::array{ DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING })
            {
                infoQueue->SetBreakOnSeverity(dxgiDebugAll, severity, true);
            }
        }
    }
#endif // NDEBUG

    // D3D device setup (basically a D3D class factory)
    {
        wil::com_ptr<ID3D11DeviceContext> deviceContext;

        static constexpr std::array driverTypes{
            D3D_DRIVER_TYPE_HARDWARE,
            D3D_DRIVER_TYPE_WARP,
        };
        static constexpr std::array featureLevels{
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_11_1,
        };

        HRESULT hr = S_OK;
        for (const auto driverType : driverTypes)
        {
            hr = D3D11CreateDevice(
                /* pAdapter */ nullptr,
                /* DriverType */ driverType,
                /* Software */ nullptr,
                /* Flags */ deviceFlags,
                /* pFeatureLevels */ featureLevels.data(),
                /* FeatureLevels */ gsl::narrow_cast<UINT>(featureLevels.size()),
                /* SDKVersion */ D3D11_SDK_VERSION,
                /* ppDevice */ _r.device.put(),
                /* pFeatureLevel */ nullptr,
                /* ppImmediateContext */ deviceContext.put());
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
        THROW_IF_FAILED(hr);

        _r.deviceContext = deviceContext.query<ID3D11DeviceContext1>();
    }

#ifndef NDEBUG
    // D3D debug messages
    if (deviceFlags & D3D11_CREATE_DEVICE_DEBUG)
    {
        const auto infoQueue = _r.device.query<ID3D11InfoQueue>();
        for (const auto severity : std::array{ D3D11_MESSAGE_SEVERITY_CORRUPTION, D3D11_MESSAGE_SEVERITY_ERROR, D3D11_MESSAGE_SEVERITY_WARNING })
        {
            infoQueue->SetBreakOnSeverity(severity, true);
        }
    }
#endif // NDEBUG

    // D3D swap chain setup (the thing that allows us to present frames on the screen)
    {
        const auto supportsFrameLatencyWaitableObject = IsWindows8Point1OrGreater();

        // With C++20 we'll finally have designated initializers.
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = _api.sizeInPixel.x;
        desc.Height = _api.sizeInPixel.y;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_NONE;
        desc.SwapEffect = _sr.isWindows10OrGreater ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = supportsFrameLatencyWaitableObject ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;

        wil::com_ptr<IDXGIFactory2> dxgiFactory;
        THROW_IF_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.put())));

        if (_api.hwnd)
        {
            if (FAILED(dxgiFactory->CreateSwapChainForHwnd(_r.device.get(), _api.hwnd, &desc, nullptr, nullptr, _r.swapChain.put())))
            {
                desc.Scaling = DXGI_SCALING_STRETCH;
                THROW_IF_FAILED(dxgiFactory->CreateSwapChainForHwnd(_r.device.get(), _api.hwnd, &desc, nullptr, nullptr, _r.swapChain.put()));
            }
        }
        else
        {
            // We can't link with dcomp.lib, as dcomp.dll doesn't exist on Windows 7.
            const wil::unique_hmodule module{ LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) };
            THROW_LAST_ERROR_IF(!module);

            const auto DCompositionCreateSurfaceHandle = reinterpret_cast<HRESULT(WINAPI*)(DWORD, SECURITY_ATTRIBUTES*, HANDLE*)>(GetProcAddress(module.get(), "DCompositionCreateSurfaceHandle"));
            THROW_LAST_ERROR_IF(!DCompositionCreateSurfaceHandle);

            // As per: https://docs.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatesurfacehandle
            static constexpr DWORD COMPOSITIONSURFACE_ALL_ACCESS = 0x0003L;
            THROW_IF_FAILED(DCompositionCreateSurfaceHandle(COMPOSITIONSURFACE_ALL_ACCESS, nullptr, _r.swapChainHandle.put()));
            THROW_IF_FAILED(dxgiFactory.query<IDXGIFactoryMedia>()->CreateSwapChainForCompositionSurfaceHandle(_r.device.get(), _r.swapChainHandle.get(), &desc, nullptr, _r.swapChain.put()));
        }

        if (supportsFrameLatencyWaitableObject)
        {
            _r.frameLatencyWaitableObject.reset(_r.swapChain.query<IDXGISwapChain2>()->GetFrameLatencyWaitableObject());
            THROW_LAST_ERROR_IF(!_r.frameLatencyWaitableObject);
        }
    }

    // Our constant buffer will never get resized
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(const_buffer);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        THROW_IF_FAILED(_r.device->CreateBuffer(&desc, nullptr, _r.constantBuffer.put()));
    }

    THROW_IF_FAILED(_r.device->CreateVertexShader(shader_vs, sizeof(shader_vs), nullptr, _r.vertexShader.put()));
    THROW_IF_FAILED(_r.device->CreatePixelShader(shader_ps, sizeof(shader_ps), nullptr, _r.pixelShader.put()));

    if (_api.swapChainChangedCallback)
    {
        try
        {
            _api.swapChainChangedCallback();
        }
        CATCH_LOG();
    }

    WI_SetAllFlags(_invalidations, invalidation_flags::size | invalidation_flags::font | invalidation_flags::cbuffer);
}

void AtlasEngine::_recreateSizeDependentResources()
{
    // ResizeBuffer() docs:
    //   Before you call ResizeBuffers, ensure that the application releases all references [...].
    //   You can use ID3D11DeviceContext::ClearState to ensure that all [internal] references are released.
    _r.renderTargetView.reset();
    _r.deviceContext->ClearState();

    THROW_IF_FAILED(_r.swapChain->ResizeBuffers(0, _api.sizeInPixel.x, _api.sizeInPixel.y, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));

    // The RenderTargetView is later used with OMSetRenderTargets
    // to tell D3D where stuff is supposed to be rendered at.
    {
        wil::com_ptr<ID3D11Texture2D> buffer;
        THROW_IF_FAILED(_r.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void()));
        THROW_IF_FAILED(_r.device->CreateRenderTargetView(buffer.get(), nullptr, _r.renderTargetView.put()));
    }

    // Tell D3D which parts of the render target will be visible.
    // Everything outside of the viewport will be black.
    //
    // In the future this should cover the entire _api.sizeInPixel.x/_api.sizeInPixel.y.
    // The pixel shader should draw the remaining content in the configured background color.
    {
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(_api.sizeInPixel.x);
        viewport.Height = static_cast<float>(_api.sizeInPixel.y);
        _r.deviceContext->RSSetViewports(1, &viewport);
    }

    if (const auto totalCellCount = _api.cellCount.area<size_t>(); totalCellCount != _r.cells.size())
    {
        // Prevent a memory usage spike, by first deallocating and then allocating.
        _r.cells = {};
        // Our render loop heavily relies on memcpy() which is between 1.5x
        // and 40x as fast for allocations with an alignment of 32 or greater.
        // (AMD Zen1-3 have a rep movsb performance bug for certain unaligned allocations.)
        _r.cells = aligned_buffer<cell>{ totalCellCount, 32 };

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = _api.cellCount.x * _api.cellCount.y * sizeof(cell);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(cell);
        THROW_IF_FAILED(_r.device->CreateBuffer(&desc, nullptr, _r.cellBuffer.put()));
        THROW_IF_FAILED(_r.device->CreateShaderResourceView(_r.cellBuffer.get(), nullptr, _r.cellView.put()));
    }

    // We have called _r.deviceContext->ClearState() in the beginning and lost all D3D state.
    // This forces us to set up everything up from scratch again.
    {
        _r.deviceContext->VSSetShader(_r.vertexShader.get(), nullptr, 0);
        _r.deviceContext->PSSetShader(_r.pixelShader.get(), nullptr, 0);

        // Our vertex shader uses a trick from Bill Bilodeau published in
        // "Vertex Shader Tricks" at GDC14 to draw a fullscreen triangle
        // without vertex/index buffers. This prepares our context for this.
        _r.deviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        _r.deviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        _r.deviceContext->IASetInputLayout(nullptr);
        _r.deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        _r.deviceContext->PSSetConstantBuffers(0, 1, _r.constantBuffer.addressof());

        _setShaderResources();
    }

    WI_SetFlag(_invalidations, invalidation_flags::cbuffer);
}

void AtlasEngine::_recreateFontDependentResources()
{
    {
        static constexpr size_t glyphCellsRequired = 16 * 1024;

        // I want my atlas texture to be square.
        // Not just so that we can better fit into the current 8k/16k texture size limit,
        // but also because that makes inspecting the texture in a debugger easier.
        const size_t csx = _api.cellSize.x;
        const size_t csy = _api.cellSize.y;
        const auto areaRequired = glyphCellsRequired * csx * csy;
        // I wrote a integer-based ceil(sqrt()) function but couldn't justify putting it into such a cold path.
        const auto pxOptimal = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(areaRequired))));
        // We want to fit whole glyphs into our texture. --> Round up to fit entire glyphs in.
        const auto xFit = (pxOptimal + csx - 1) / csx;
        const auto yFit = (glyphCellsRequired + xFit - 1) / xFit;

        _r.glyphs = {};
        _r.glyphQueue = {};
        _r.atlasSizeInPixel = _api.cellSize * u16x2{ yolo_narrow<u16>(xFit), yolo_narrow<u16>(yFit) };
        // The first cell at {0, 0} is always our cursor texture.
        // --> The first glyph starts at {1, 0}.
        _r.atlasPosition = _api.cellSize * u16x2{ 1, 0 };
    }

    // D3D
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = _r.atlasSizeInPixel.x;
        desc.Height = _r.atlasSizeInPixel.y;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        THROW_IF_FAILED(_r.device->CreateTexture2D(&desc, nullptr, _r.glyphBuffer.put()));
        THROW_IF_FAILED(_r.device->CreateShaderResourceView(_r.glyphBuffer.get(), nullptr, _r.glyphView.put()));
    }
    {
        // We only support regular narrow and wide characters at the moment.
        // A width of cellSize.x*2 is thus enough.
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = _api.cellSize.x * 2;
        desc.Height = _api.cellSize.y;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        THROW_IF_FAILED(_r.device->CreateTexture2D(&desc, nullptr, _r.glyphScratchpad.put()));
    }

    _setShaderResources();

    // D2D
    {
        D2D1_RENDER_TARGET_PROPERTIES props{};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat = { DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED };
        props.dpiX = static_cast<float>(_api.dpi);
        props.dpiY = static_cast<float>(_api.dpi);
        const auto surface = _r.glyphScratchpad.query<IDXGISurface>();
        THROW_IF_FAILED(_sr.d2dFactory->CreateDxgiSurfaceRenderTarget(surface.get(), &props, _r.d2dRenderTarget.put()));
        // We don't really use D2D for anything except DWrite, but it
        // can't hurt to ensure that everything it does is pixel aligned.
        _r.d2dRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        _r.d2dRenderTarget->SetTextAntialiasMode(static_cast<D2D1_TEXT_ANTIALIAS_MODE>(_api.antialiasingMode));
    }
    {
        static constexpr D2D1_COLOR_F color{ 1, 1, 1, 1 };
        wil::com_ptr<ID2D1SolidColorBrush> brush;
        THROW_IF_FAILED(_r.d2dRenderTarget->CreateSolidColorBrush(&color, nullptr, brush.put()));
        _r.brush = brush.query<ID2D1Brush>();
    }
    {
        getLocaleName(localeName);
        for (auto style = 0; style < 2; ++style)
        {
            for (auto weight = 0; weight < 2; ++weight)
            {
                _r.textFormats[style][weight] = _createTextFormat(
                    _api.fontName.c_str(),
                    weight ? DWRITE_FONT_WEIGHT_BOLD : static_cast<DWRITE_FONT_WEIGHT>(_api.fontWeight),
                    static_cast<DWRITE_FONT_STYLE>(style * DWRITE_FONT_STYLE_ITALIC),
                    _api.fontSize,
                    localeName);
            }
        }
    }

    _drawCursor();

    WI_SetAllFlags(_invalidations, invalidation_flags::cbuffer);
}

void AtlasEngine::_setShaderResources() const
{
    const std::array resources{ _r.cellView.get(), _r.glyphView.get() };
    _r.deviceContext->PSSetShaderResources(0, gsl::narrow_cast<UINT>(resources.size()), resources.data());
}

void AtlasEngine::_updateConstantBuffer() const
{
    const_buffer data;
    data.viewport.x = 0;
    data.viewport.y = 0;
    data.viewport.z = static_cast<float>(_api.cellCount.x * _api.cellSize.x);
    data.viewport.w = static_cast<float>(_api.cellCount.y * _api.cellSize.y);
    data.cellSize.x = _api.cellSize.x;
    data.cellSize.y = _api.cellSize.y;
    data.cellCountX = _api.cellCount.x;
    data.backgroundColor = _rapi.backgroundColor;
    data.selectionColor = _rapi.selectionColor;
    _r.deviceContext->UpdateSubresource(_r.constantBuffer.get(), 0, nullptr, &data, 0, 0);
}

wil::com_ptr<IDWriteTextFormat> AtlasEngine::_createTextFormat(const wchar_t* fontFamilyName, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, float fontSize, const wchar_t* localeName) const
{
    wil::com_ptr<IDWriteTextFormat> textFormat;
    THROW_IF_FAILED(_sr.dwriteFactory->CreateTextFormat(fontFamilyName, nullptr, fontWeight, fontStyle, DWRITE_FONT_STRETCH_NORMAL, fontSize, localeName, textFormat.addressof()));
    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return textFormat;
}

AtlasEngine::u16x2 AtlasEngine::_allocateAtlasCell() noexcept
{
    const auto ret = _r.atlasPosition;

    _r.atlasPosition.x += _api.cellSize.x;
    if (_r.atlasPosition.x >= _r.atlasSizeInPixel.x)
    {
        _r.atlasPosition.x = 0;
        _r.atlasPosition.y += _api.cellSize.y;
        if (_r.atlasPosition.y >= _r.atlasSizeInPixel.y)
        {
            _r.atlasPosition.x = _api.cellSize.x;
            _r.atlasPosition.y = 0;
        }
    }

    return ret;
}

void AtlasEngine::_drawGlyph(const til::pair<glyph_entry, std::array<u16x2, 2>>& pair) const
{
    wchar_t chars[2];
    const auto entry = pair.first;
    const auto charsLength = utf32utf16(entry.codepoint, chars);
    const auto cells = entry.wide + UINT32_C(1);
    const bool bold = entry.bold;
    const bool italic = entry.italic;
    const auto textFormat = _getTextFormat(bold, italic);

    D2D1_RECT_F rect;
    rect.left = 0.0f;
    rect.top = 0.0f;
    rect.right = static_cast<float>(cells) * _api.cellSizeDIP.x;
    rect.bottom = _api.cellSizeDIP.y;

    {
        // See D2DFactory::DrawText
        wil::com_ptr<IDWriteTextLayout> textLayout;
        THROW_IF_FAILED(_sr.dwriteFactory->CreateTextLayout(chars, charsLength, textFormat, rect.right, rect.bottom, textLayout.put()));

        _r.d2dRenderTarget->BeginDraw();
        _r.d2dRenderTarget->Clear();
        _r.d2dRenderTarget->DrawTextLayout({}, textLayout.get(), _r.brush.get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        THROW_IF_FAILED(_r.d2dRenderTarget->EndDraw());
    }

    for (uint32_t i = 0; i < cells; ++i)
    {
        // Specifying NO_OVERWRITE means that the system can assume that existing references to the surface that
        // may be in flight on the GPU will not be affected by the update, so the copy can proceed immediately
        // (avoiding either a batch flush or the system maintaining multiple copies of the resource behind the scenes).
        //
        // Since our shader only draws whatever is in the atlas, and since we don't replace glyph cells that are in use,
        // we can safely (?) tell the GPU that we don't overwrite parts of our atlas that are in use.
        _copyScratchpadCell(i, pair.second[i], D3D11_COPY_NO_OVERWRITE);
    }
}

void AtlasEngine::_drawCursor() const
{
    D2D1_RECT_F rect;
    rect.left = 0;
    rect.top = _api.cellSizeDIP.y * 0.81f;
    rect.right = _api.cellSizeDIP.x;
    rect.bottom = _api.cellSizeDIP.y;

    _r.d2dRenderTarget->BeginDraw();
    _r.d2dRenderTarget->Clear();
    _r.d2dRenderTarget->FillRectangle(&rect, _r.brush.get());
    THROW_IF_FAILED(_r.d2dRenderTarget->EndDraw());

    _copyScratchpadCell(0, {});
}

void AtlasEngine::_copyScratchpadCell(uint32_t scratchpadIndex, u16x2 target, uint32_t copyFlags) const
{
    D3D11_BOX box;
    box.left = scratchpadIndex * _api.cellSize.x;
    box.top = 0;
    box.front = 0;
    box.right = (scratchpadIndex + 1) * _api.cellSize.x;
    box.bottom = _api.cellSize.y;
    box.back = 1;
    _r.deviceContext->CopySubresourceRegion1(_r.glyphBuffer.get(), 0, target.x, target.y, 0, _r.glyphScratchpad.get(), 0, &box, copyFlags);
}
