/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- RenderEngineBase.hpp

Abstract:
- Implements a set of functions with common behavior across all render engines.
  For example, the behavior for setting the title. The title may change many
  times in the course of a single frame, but the RenderEngine should only
  actually perform its update operation if at the start of a frame, the new
  window title will be different then the last frames, and it should only ever
  update the title once per frame.

Author(s):
- Mike Griese (migrie) 10-July-2018
--*/
#include "IRenderEngine.hpp"

#pragma once
namespace Microsoft::Console::Render
{
    class RenderEngineBase : public IRenderEngine
    {
    public:
        RenderEngineBase() = default;
        ~RenderEngineBase() = 0;
        RenderEngineBase(const RenderEngineBase&) = default;
        RenderEngineBase(RenderEngineBase&&) = default;
        RenderEngineBase& operator=(const RenderEngineBase&) = default;
        RenderEngineBase& operator=(RenderEngineBase&&) = default;

        [[nodiscard]] HRESULT InvalidateTitle() noexcept override;
        [[nodiscard]] HRESULT UpdateTitle(const std::wstring_view newTitle) noexcept override;
        [[nodiscard]] HRESULT UpdateSoftFont(const gsl::span<const uint16_t> bitPattern, const SIZE cellSize, const size_t centeringHint) noexcept override;
        [[nodiscard]] HRESULT PrepareRenderInfo(const RenderFrameInfo& info) noexcept override;
        [[nodiscard]] HRESULT ResetLineTransform() noexcept override;
        [[nodiscard]] HRESULT PrepareLineTransform(const LineRendition lineRendition, const size_t targetRow, const size_t viewportLeft) noexcept override;
        [[nodiscard]] virtual bool RequiresContinuousRedraw() noexcept override;
        void WaitUntilCanRender() noexcept override;
        [[nodiscard]] HRESULT IsGlyphWideByFont(const std::wstring_view& glyph, _Out_ bool* const pResult) noexcept override;

    protected:
        [[nodiscard]] virtual HRESULT _DoUpdateTitle(const std::wstring_view newTitle) noexcept = 0;

        bool _titleChanged = false;
    };

    inline Microsoft::Console::Render::RenderEngineBase::~RenderEngineBase() {}
}
