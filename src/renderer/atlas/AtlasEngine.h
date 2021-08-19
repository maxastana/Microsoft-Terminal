// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <d2d1.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dxgi.h>

#include <robin_hood.h>
#include <til/pair.h>

#include "../../renderer/inc/IRenderEngine.hpp"

namespace Microsoft::Console::Render
{
    class AtlasEngine final : public IRenderEngine
    {
    public:
        explicit AtlasEngine();

        AtlasEngine(const AtlasEngine&) = delete;
        AtlasEngine& operator=(const AtlasEngine&) = delete;

        // IRenderEngine
        [[nodiscard]] HRESULT StartPaint() noexcept override;
        [[nodiscard]] HRESULT EndPaint() noexcept override;
        [[nodiscard]] bool RequiresContinuousRedraw() noexcept override;
        void WaitUntilCanRender() noexcept override;
        [[nodiscard]] HRESULT Present() noexcept override;
        [[nodiscard]] HRESULT PrepareForTeardown(_Out_ bool* const pForcePaint) noexcept override;
        [[nodiscard]] HRESULT ScrollFrame() noexcept override;
        [[nodiscard]] HRESULT Invalidate(const SMALL_RECT* const psrRegion) noexcept override;
        [[nodiscard]] HRESULT InvalidateCursor(const SMALL_RECT* const psrRegion) noexcept override;
        [[nodiscard]] HRESULT InvalidateSystem(const RECT* const prcDirtyClient) noexcept override;
        [[nodiscard]] HRESULT InvalidateSelection(const std::vector<SMALL_RECT>& rectangles) noexcept override;
        [[nodiscard]] HRESULT InvalidateScroll(const COORD* const pcoordDelta) noexcept override;
        [[nodiscard]] HRESULT InvalidateAll() noexcept override;
        [[nodiscard]] HRESULT InvalidateCircling(_Out_ bool* const pForcePaint) noexcept override;
        [[nodiscard]] HRESULT InvalidateTitle() noexcept override;
        [[nodiscard]] HRESULT PrepareRenderInfo(const RenderFrameInfo& info) noexcept override;
        [[nodiscard]] HRESULT ResetLineTransform() noexcept override;
        [[nodiscard]] HRESULT PrepareLineTransform(const LineRendition lineRendition, const size_t targetRow, const size_t viewportLeft) noexcept override;
        [[nodiscard]] HRESULT PaintBackground() noexcept override;
        [[nodiscard]] HRESULT PaintBufferLine(gsl::span<const Cluster> const clusters, const COORD coord, const bool fTrimLeft, const bool lineWrapped) noexcept override;
        [[nodiscard]] HRESULT PaintBufferGridLines(const GridLines lines, const COLORREF color, const size_t cchLine, const COORD coordTarget) noexcept override;
        [[nodiscard]] HRESULT PaintSelection(const SMALL_RECT rect) noexcept override;
        [[nodiscard]] HRESULT PaintCursor(const CursorOptions& options) noexcept override;
        [[nodiscard]] HRESULT UpdateDrawingBrushes(const TextAttribute& textAttributes, const gsl::not_null<IRenderData*> pData, const bool usingSoftFont, const bool isSettingDefaultBrushes) noexcept override;
        [[nodiscard]] HRESULT UpdateFont(const FontInfoDesired& FontInfoDesired, _Out_ FontInfo& FontInfo) noexcept override;
        [[nodiscard]] HRESULT UpdateSoftFont(const gsl::span<const uint16_t> bitPattern, const SIZE cellSize, const size_t centeringHint) noexcept override;
        [[nodiscard]] HRESULT UpdateDpi(const int iDpi) noexcept override;
        [[nodiscard]] HRESULT UpdateViewport(const SMALL_RECT srNewViewport) noexcept override;
        [[nodiscard]] HRESULT GetProposedFont(const FontInfoDesired& FontInfoDesired, _Out_ FontInfo& FontInfo, const int iDpi) noexcept override;
        [[nodiscard]] HRESULT GetDirtyArea(gsl::span<const til::rectangle>& area) noexcept override;
        [[nodiscard]] HRESULT GetFontSize(_Out_ COORD* const pFontSize) noexcept override;
        [[nodiscard]] HRESULT IsGlyphWideByFont(const std::wstring_view& glyph, _Out_ bool* const pResult) noexcept override;
        [[nodiscard]] HRESULT UpdateTitle(const std::wstring_view newTitle) noexcept override;

        // Just for compatibility with DxEngine, but can be removed at some point.
        HRESULT Enable()
        {
            return S_OK;
        }

        // DxRenderer - getter
        [[nodiscard]] bool GetRetroTerminalEffect() const noexcept;
        [[nodiscard]] float GetScaling() const noexcept;
        [[nodiscard]] HANDLE GetSwapChainHandle();
        [[nodiscard]] ::Microsoft::Console::Types::Viewport GetViewportInCharacters(const ::Microsoft::Console::Types::Viewport& viewInPixels) const noexcept;
        [[nodiscard]] ::Microsoft::Console::Types::Viewport GetViewportInPixels(const ::Microsoft::Console::Types::Viewport& viewInCharacters) const noexcept;
        // DxRenderer - setter
        void SetAntialiasingMode(const D2D1_TEXT_ANTIALIAS_MODE antialiasingMode) noexcept;
        void SetCallback(std::function<void()> pfn);
        void SetDefaultTextBackgroundOpacity(const float opacity) noexcept;
        void SetForceFullRepaintRendering(bool enable) noexcept;
        [[nodiscard]] HRESULT SetHwnd(const HWND hwnd) noexcept;
        void SetPixelShaderPath(std::wstring_view value) noexcept;
        void SetRetroTerminalEffect(bool enable) noexcept;
        void SetSelectionBackground(const COLORREF color, const float alpha = 0.5f) noexcept;
        void SetSoftwareRendering(bool enable) noexcept;
        void SetWarningCallback(std::function<void(const HRESULT)> pfn);
        [[nodiscard]] HRESULT SetWindowSize(const SIZE pixels) noexcept;
        void ToggleShaderEffects();
        [[nodiscard]] HRESULT UpdateFont(const FontInfoDesired& pfiFontInfoDesired, FontInfo& fiFontInfo, const std::unordered_map<std::wstring_view, uint32_t>& features, const std::unordered_map<std::wstring_view, float>& axes) noexcept;
        void UpdateHyperlinkHoveredId(const uint16_t hoveredId) noexcept;

        // Some helper classes for the implementation.
        // public because I don't want to sprinkle the code with friends.
    public:
        template<typename T>
        struct aligned_buffer
        {
            constexpr aligned_buffer() noexcept = default;

            explicit aligned_buffer(size_t size, size_t alignment) :
                _data{ THROW_IF_NULL_ALLOC(static_cast<T*>(_aligned_malloc(size * sizeof(T), alignment))) },
                _size{ size }
            {
            }

            ~aligned_buffer()
            {
                _aligned_free(_data);
            }

            aligned_buffer(aligned_buffer&& other) noexcept :
                _data{ std::exchange(other._data, nullptr) },
                _size{ std::exchange(other._size, 0) }
            {
            }

            aligned_buffer& operator=(aligned_buffer&& other) noexcept
            {
                _aligned_free(_data);
                _data = std::exchange(other._data, nullptr);
                _size = std::exchange(other._size, 0);
                return *this;
            }

            T* data()
            {
                return _data;
            }

            size_t size()
            {
                return _size;
            }

        private:
            T* _data = nullptr;
            size_t _size = 0;
        };

        template<typename T>
        struct vec2
        {
            T x{};
            T y{};

            bool operator==(const vec2& other) const noexcept
            {
                return memcmp(this, &other, sizeof(vec2)) == 0;
            }

            bool operator!=(const vec2& other) const noexcept
            {
                return memcmp(this, &other, sizeof(vec2)) != 0;
            }

            vec2 operator*(const vec2& other) const noexcept
            {
                return { static_cast<T>(x * other.x), static_cast<T>(y * other.y) };
            }

            vec2 operator/(const vec2& other) const noexcept
            {
                return { static_cast<T>(x / other.x), static_cast<T>(y / other.y) };
            }

            template<typename U = T>
            U area() const noexcept
            {
                return static_cast<U>(x) * static_cast<U>(y);
            }
        };

        template<typename T>
        struct vec4
        {
            T x{};
            T y{};
            T z{};
            T w{};
        };

        using u8 = uint8_t;
        using u16 = uint16_t;
        using u16x2 = vec2<u16>;
        using u32 = uint32_t;
        using u32x2 = vec2<u32>;
        using f32 = float;
        using f32x2 = vec2<f32>;
        using f32x4 = vec4<f32>;

        union glyph_entry
        {
            uint32_t value;
            struct
            {
                uint32_t codepoint : 20;
                uint32_t wide : 1;
                uint32_t bold : 1;
                uint32_t italic : 1;
            };

            constexpr bool operator==(const glyph_entry& other) const noexcept
            {
                return value == other.value;
            }
        };

        struct glyph_entry_hasher
        {
            constexpr size_t operator()(glyph_entry entry) const noexcept
            {
                uint64_t x = entry.value;
                x ^= x >> 33;
                x *= UINT64_C(0xff51afd7ed558ccd);
                x ^= x >> 33;
                return static_cast<size_t>(x);
            }
        };

    private:
        // D3D constant buffers sizes must be a multiple of 16 bytes.
        struct alignas(16) const_buffer
        {
            f32x4 viewport;
            u32x2 cellSize;
            u32 cellCountX;
            u32 backgroundColor;
            u32 selectionColor;
#pragma warning(suppress : 4324) // structure was padded due to alignment specifier
        };

        struct cell
        {
            union
            {
                u32 glyphIndex;
                u16x2 glyphIndex16;
            };
            u32 flags;
            u32x2 color;
        };

        enum class invalidation_flags : u8
        {
            none = 0,
            device = 1 << 0,
            size = 1 << 1,
            font = 1 << 2,
            cbuffer = 1 << 3,
            title = 1 << 4,
        };
        friend constexpr invalidation_flags operator~(invalidation_flags v) noexcept { return static_cast<invalidation_flags>(~static_cast<u8>(v)); }
        friend constexpr invalidation_flags operator|(invalidation_flags lhs, invalidation_flags rhs) noexcept { return static_cast<invalidation_flags>(static_cast<u8>(lhs) | static_cast<u8>(rhs)); }
        friend constexpr invalidation_flags operator&(invalidation_flags lhs, invalidation_flags rhs) noexcept { return static_cast<invalidation_flags>(static_cast<u8>(lhs) & static_cast<u8>(rhs)); }
        friend constexpr void operator|=(invalidation_flags& lhs, invalidation_flags rhs) noexcept { lhs = lhs | rhs; }
        friend constexpr void operator&=(invalidation_flags& lhs, invalidation_flags rhs) noexcept { lhs = lhs & rhs; }

        // resource handling
        [[nodiscard]] HRESULT _handleException(const wil::ResultException& exception) noexcept;
        __declspec(noinline) void _createResources();
        __declspec(noinline) void _recreateSizeDependentResources();
        __declspec(noinline) void _recreateFontDependentResources();
        void _setShaderResources() const;
        void _updateConstantBuffer() const;

        // text handling
        IDWriteTextFormat* _getTextFormat(bool bold, bool italic) const noexcept { return _r.textFormats[italic][bold].get(); }
        wil::com_ptr<IDWriteTextFormat> _createTextFormat(const wchar_t* fontFamilyName, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, float fontSize, const wchar_t* localeName) const;
        u16x2 _allocateAtlasCell() noexcept;
        void _drawGlyph(const til::pair<glyph_entry, std::array<u16x2, 2>>& pair) const;
        void _drawCursor() const;
        void _copyScratchpadCell(uint32_t scratchpadIndex, u16x2 target, uint32_t copyFlags = 0) const;

        template<typename T1, typename T2>
        cell* _getCell(T1 x, T2 y) noexcept
        {
            return _r.cells.data() + static_cast<size_t>(_api.cellCount.x) * y + x;
        }

        struct static_resources
        {
            wil::com_ptr<ID2D1Factory> d2dFactory;
            wil::com_ptr<IDWriteFactory> dwriteFactory;
            bool isWindows10OrGreater = true;
        } _sr;

        struct resources
        {
            // D3D resources
            wil::com_ptr<ID3D11Device> device;
            wil::com_ptr<ID3D11DeviceContext1> deviceContext;
            wil::com_ptr<IDXGISwapChain1> swapChain;
            wil::unique_handle swapChainHandle;
            wil::unique_handle frameLatencyWaitableObject;
            wil::com_ptr<ID3D11RenderTargetView> renderTargetView;
            wil::com_ptr<ID3D11VertexShader> vertexShader;
            wil::com_ptr<ID3D11PixelShader> pixelShader;
            wil::com_ptr<ID3D11Buffer> constantBuffer;
            wil::com_ptr<ID3D11Buffer> cellBuffer;
            wil::com_ptr<ID3D11ShaderResourceView> cellView;

            // D2D resources
            wil::com_ptr<ID3D11Texture2D> glyphBuffer;
            wil::com_ptr<ID3D11ShaderResourceView> glyphView;
            wil::com_ptr<ID3D11Texture2D> glyphScratchpad;
            wil::com_ptr<ID2D1RenderTarget> d2dRenderTarget;
            wil::com_ptr<ID2D1Brush> brush;
            wil::com_ptr<IDWriteTextFormat> textFormats[2][2];

            // Resources dependent on _api.sizeInPixel
            aligned_buffer<cell> cells;
            // Resources dependent on _api.cellSize
            robin_hood::unordered_flat_map<glyph_entry, std::array<u16x2, 2>, glyph_entry_hasher> glyphs;
            std::vector<til::pair<glyph_entry, std::array<u16x2, 2>>> glyphQueue;
            u16x2 atlasSizeInPixel;
            u16x2 atlasPosition;
        } _r;

        struct api_state
        {
            f32x2 cellSizeDIP; // invalidation_flags::font
            u16x2 cellSize; // invalidation_flags::size
            u16x2 cellCount; // caches `sizeInPixel / cellSize`
            u16x2 sizeInPixel; // invalidation_flags::size

            std::wstring fontName; // invalidation_flags::font|size
            u16 fontSize = 0; // invalidation_flags::font|size
            u16 fontWeight = DWRITE_FONT_WEIGHT_NORMAL; // invalidation_flags::font
            u16 dpi = USER_DEFAULT_SCREEN_DPI; // invalidation_flags::font|size
            u16 antialiasingMode = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE; // invalidation_flags::font

            std::function<void()> swapChainChangedCallback;
            HWND hwnd = nullptr;
        } _api;

        struct render_api_state
        {
            til::rectangle dirtyArea;
            u32x2 currentColor{};
            glyph_entry attributes{};
            u32 backgroundColor = ~u32(0);
            u32 selectionColor = 0x7fffffff;
        } _rapi;

        invalidation_flags _invalidations = invalidation_flags::device;
    };
}
