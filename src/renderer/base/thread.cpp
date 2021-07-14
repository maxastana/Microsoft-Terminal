// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"

#include "thread.hpp"

#pragma hdrstop

using namespace Microsoft::Console::Render;

RenderThread::RenderThread() :
    _pRenderer(nullptr),
    _hThread(nullptr),
    _fKeepRunning(true),
    _hPaintCompletedEvent(true),
    _hPaintEnabledEvent(false),
    _fNextFrameRequested(false)
{
}

RenderThread::~RenderThread()
{
    if (_hThread)
    {
        _fKeepRunning.store(false, std::memory_order_relaxed); // stop loop after final run
        EnablePainting(); // if we want to get the last frame out, we need to make sure it's enabled
        WaitForSingleObject(_hThread, INFINITE); // wait for thread to finish.

        CloseHandle(_hThread);
    }
}

// Method Description:
// - Create all of the Events we'll need, and the actual thread we'll be doing
//      work on.
// Arguments:
// - pRendererParent: the IRenderer that owns this thread, and which we should
//      trigger frames for.
// Return Value:
// - S_OK if we succeeded, else an HRESULT corresponding to a failure to create
//      an Event or Thread.
[[nodiscard]] HRESULT RenderThread::Initialize(IRenderer* const pRendererParent) noexcept
{
    _pRenderer = pRendererParent;

    _hThread = CreateThread(
        nullptr, // non-inheritable security attributes
        0, // use default stack size
        s_ThreadProc,
        this,
        0, // create immediately
        nullptr // we don't need the thread ID
    );
    RETURN_LAST_ERROR_IF_NULL(_hThread);

    // SetThreadDescription only works on 1607 and higher. If we cannot find it,
    // then it's no big deal. Just skip setting the description.
    auto func = GetProcAddressByFunctionDeclaration(GetModuleHandleW(L"kernel32.dll"), SetThreadDescription);
    if (func)
    {
        LOG_IF_FAILED(func(_hThread, L"Rendering Output Thread"));
    }

    return S_OK;
}

DWORD WINAPI RenderThread::s_ThreadProc(_In_ LPVOID lpParameter)
{
    RenderThread* const pContext = static_cast<RenderThread*>(lpParameter);

    if (pContext != nullptr)
    {
        return pContext->_ThreadProc();
    }
    else
    {
        return (DWORD)E_INVALIDARG;
    }
}

DWORD RenderThread::_ThreadProc()
{
    while (_fKeepRunning.load(std::memory_order_relaxed))
    {
        //Sleep(8); // frame rate limit to ~60 FPS in practice

        _hPaintEnabledEvent.pass();
        _fNextFrameRequested.acquire();

        _hPaintCompletedEvent.store(false, std::memory_order_relaxed);

        _pRenderer->WaitUntilCanRender();
        LOG_IF_FAILED(_pRenderer->PaintFrame());

        _hPaintCompletedEvent.store(true, std::memory_order_relaxed);
    }

    return S_OK;
}

void RenderThread::NotifyPaint()
{
    _fNextFrameRequested.release();
}

void RenderThread::EnablePainting()
{
    _hPaintEnabledEvent.open();
}

void RenderThread::DisablePainting()
{
    _hPaintEnabledEvent.close();
}

void RenderThread::WaitForPaintCompletionAndDisable(const DWORD dwTimeoutMs)
{
    // When rendering takes place via DirectX, and a console application
    // currently owns the screen, and a new console application is launched (or
    // the user switches to another console application), the new application
    // cannot take over the screen until the active one relinquishes it. This
    // blocking mechanism goes as follows:
    //
    // 1. The console input thread of the new console application connects to
    // ConIoSrv;
    // 2. While servicing the new connection request, ConIoSrv sends an event to
    // the active application letting it know that it has lost focus;
    // 3.1 ConIoSrv waits for a reply from the client application;
    // 3.2 Meanwhile, the active application receives the focus event and calls
    // this method, waiting for the current paint operation to
    // finish.
    //
    // This means that the new application is waiting on the connection request
    // reply from ConIoSrv, ConIoSrv is waiting on the active application to
    // acknowledge the lost focus event to reply to the new application, and the
    // console input thread in the active application is waiting on the renderer
    // thread to finish its current paint operation.
    //
    // Question: what should happen if the wait on the paint operation times
    // out?
    //
    // There are three options:
    //
    // 1. On timeout, the active console application could reply with an error
    // message and terminate itself, effectively relinquishing control of the
    // display;
    //
    // 2. ConIoSrv itself could time out on waiting for a reply, and forcibly
    // terminate the active console application;
    //
    // 3. Let the wait time out and let the user deal with it. Because the wait
    // occurs on a single iteration of the renderer thread, it seemed to me that
    // the likelihood of failure is extremely small, especially since the client
    // console application that the active conhost instance is servicing has no
    // say over what happens in the renderer thread, only by proxy. Thus, the
    // chance of failure (timeout) is minimal and since the OneCoreUAP console
    // is not a massively used piece of software, it didn’t seem that it would
    // be a good use of time to build the requisite infrastructure to deal with
    // a timeout here, at least not for now. In case of a timeout DirectX will
    // catch the mistake of a new application attempting to acquire the display
    // while another one still owns it and will flag it as a DWM bug. Right now,
    // the active application will wait one second for the paint operation to
    // finish.
    //
    // TODO: MSFT: 11833883 - Determine action when wait on paint operation via
    //       DirectX on OneCoreUAP times out while switching console
    //       applications.

    _hPaintEnabledEvent.close();

    while (!_hPaintCompletedEvent.load(std::memory_order_relaxed))
    {
        til::atomic_wait(_hPaintCompletedEvent, false, dwTimeoutMs);
    }
}
