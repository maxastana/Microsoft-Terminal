// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "OpenTerminalHere.h"

#include <filesystem>

#include <wil/common.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>

#include <ShlObj_core.h>
#include <shlwapi.h>

#include <winrt/Windows.ApplicationModel.Resources.Core.h>

HRESULT OpenTerminalHere::Invoke(IShellItemArray* psiItemArray, IBindCtx* /*pBindContext*/)
try
{
    std::wstring path;

    if (psiItemArray == nullptr)
    {
        path = _GetPathFromExplorer();
        if (path.empty())
        {
            return S_FALSE;
        }
    }
    else
    {
        DWORD count;
        psiItemArray->GetCount(&count);

        winrt::com_ptr<IShellItem> psi;
        wil::unique_cotaskmem_string pszName;
        RETURN_IF_FAILED(psiItemArray->GetItemAt(0, psi.put()));
        RETURN_IF_FAILED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszName));

        path = std::wstring{ pszName.get() };
    }

    // Append a "\." to the given path, so that this will work in "C:\"
    path.append(L"\\.");

    std::filesystem::path applicationName{ wil::GetModuleFileNameW<std::wstring>(wil::GetModuleInstanceHandle()) };
    applicationName.replace_filename(L"..\\Host.EXE\\OpenConsole.exe");

    std::wstring commandLine;
    commandLine.push_back(L'"');
    commandLine.append(applicationName);
    commandLine.append(L"\" pwsh.exe");

    wil::unique_process_information _piClient;
    STARTUPINFOEX siEx{ 0 };
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEX);

    RETURN_IF_WIN32_BOOL_FALSE(CreateProcessW(
        applicationName.c_str(), // lpApplicationName
        commandLine.data(),
        nullptr, // lpProcessAttributes
        nullptr, // lpThreadAttributes
        false, // bInheritHandles
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, // dwCreationFlags
        nullptr, // lpEnvironment
        path.data(),
        &siEx.StartupInfo, // lpStartupInfo
        &_piClient // lpProcessInformation
        ));

    return S_OK;
}
CATCH_RETURN()

HRESULT OpenTerminalHere::GetToolTip(IShellItemArray* /*psiItemArray*/, LPWSTR* ppszInfoTip)
{
    *ppszInfoTip = nullptr;
    return E_NOTIMPL;
}

HRESULT OpenTerminalHere::GetTitle(IShellItemArray* /*psiItemArray*/, LPWSTR* ppszName)
{
    return SHStrDupW(L"Open in OpenConsole", ppszName);
}

HRESULT OpenTerminalHere::GetState(IShellItemArray* /*psiItemArray*/, BOOL /*fOkToBeSlow*/, EXPCMDSTATE* pCmdState)
{
    *pCmdState = ECS_ENABLED;
    return S_OK;
}

HRESULT OpenTerminalHere::GetIcon(IShellItemArray* /*psiItemArray*/, LPWSTR* ppszIcon)
try
{
    std::filesystem::path path{ wil::GetModuleFileNameW<std::wstring>(wil::GetModuleInstanceHandle()) };
    path.replace_filename(L"..\\Host.EXE\\OpenConsole.exe,0");
    return SHStrDupW(path.c_str(), ppszIcon);
}
CATCH_RETURN();

HRESULT OpenTerminalHere::GetFlags(EXPCMDFLAGS* pFlags)
{
    *pFlags = ECF_DEFAULT;
    return S_OK;
}

HRESULT OpenTerminalHere::GetCanonicalName(GUID* pguidCommandName)
{
    *pguidCommandName = __uuidof(this);
    return S_OK;
}

HRESULT OpenTerminalHere::EnumSubCommands(IEnumExplorerCommand** ppEnum)
{
    *ppEnum = nullptr;
    return E_NOTIMPL;
}

std::wstring OpenTerminalHere::_GetPathFromExplorer() const
{
    using namespace std;
    using namespace winrt;

    wstring path;
    HRESULT hr = NOERROR;

    auto hwnd = ::GetForegroundWindow();
    if (hwnd == nullptr)
    {
        return path;
    }

    TCHAR szName[MAX_PATH] = { 0 };
    ::GetClassName(hwnd, szName, MAX_PATH);
    if (0 == StrCmp(szName, L"WorkerW") ||
        0 == StrCmp(szName, L"Progman"))
    {
        //special folder: desktop
        hr = ::SHGetFolderPath(NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, szName);
        if (FAILED(hr))
        {
            return path;
        }

        path = szName;
        return path;
    }

    if (0 != StrCmp(szName, L"CabinetWClass"))
    {
        return path;
    }

    com_ptr<IShellWindows> shell;
    try
    {
        shell = create_instance<IShellWindows>(CLSID_ShellWindows, CLSCTX_ALL);
    }
    catch (...)
    {
        //look like try_create_instance is not available no more
    }

    if (shell == nullptr)
    {
        return path;
    }

    com_ptr<IDispatch> disp;
    wil::unique_variant variant;
    variant.vt = VT_I4;

    com_ptr<IWebBrowserApp> browser;
    // look for correct explorer window
    for (variant.intVal = 0;
         shell->Item(variant, disp.put()) == S_OK;
         variant.intVal++)
    {
        com_ptr<IWebBrowserApp> tmp;
        if (FAILED(disp->QueryInterface(tmp.put())))
        {
            disp = nullptr; // get rid of DEBUG non-nullptr warning
            continue;
        }

        HWND tmpHWND = NULL;
        hr = tmp->get_HWND(reinterpret_cast<SHANDLE_PTR*>(&tmpHWND));
        if (hwnd == tmpHWND)
        {
            browser = tmp;
            disp = nullptr; // get rid of DEBUG non-nullptr warning
            break; //found
        }

        disp = nullptr; // get rid of DEBUG non-nullptr warning
    }

    if (browser)
    {
        wil::unique_bstr url;
        hr = browser->get_LocationURL(&url);
        if (FAILED(hr))
        {
            return path;
        }

        wstring sUrl(url.get(), SysStringLen(url.get()));
        DWORD size = MAX_PATH;
        hr = ::PathCreateFromUrl(sUrl.c_str(), szName, &size, NULL);
        if (SUCCEEDED(hr))
        {
            path = szName;
        }
    }

    return path;
}
