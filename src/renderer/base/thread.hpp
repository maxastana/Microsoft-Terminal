/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- Thread.hpp

Abstract:
- This is the definition of our rendering thread designed to throttle and compartmentalize drawing operations.

Author(s):
- Michael Niksa (MiNiksa) Feb 2016
--*/

#pragma once

#include "../inc/IRenderer.hpp"
#include "../inc/IRenderThread.hpp"

#include <til/gate.h>

namespace Microsoft::Console::Render
{
    class RenderThread final : public IRenderThread
    {
    public:
        RenderThread();
        virtual ~RenderThread() override;

        [[nodiscard]] HRESULT Initialize(_In_ IRenderer* const pRendererParent) noexcept;

        void NotifyPaint() override;

        void EnablePainting() override;
        void DisablePainting() override;
        void WaitForPaintCompletionAndDisable(const DWORD dwTimeoutMs) override;

    private:
        static DWORD WINAPI s_ThreadProc(_In_ LPVOID lpParameter);
        DWORD _ThreadProc();

        HANDLE _hThread;
        IRenderer* _pRenderer; // Non-ownership pointer

        std::atomic<bool> _fKeepRunning;
        std::atomic<bool> _hPaintCompletedEvent;
        til::gate_relaxed _hPaintEnabledEvent;
        til::gate_relaxed _fNextFrameRequested;
    };
}
