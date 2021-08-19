// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once

#include <string>
#include <wrl.h>
#include <ShlObj.h>

using namespace Microsoft::WRL;

struct __declspec(uuid("74cd627e-aad1-4ee4-9427-7c4db186ee59")) OpenTerminalHere : public RuntimeClass<RuntimeClassFlags<ClassicCom | InhibitFtmBase>, IExplorerCommand>
{
#pragma region IExplorerCommand
    STDMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pBindContext);
    STDMETHODIMP GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfoTip);
    STDMETHODIMP GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName);
    STDMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState);
    STDMETHODIMP GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon);
    STDMETHODIMP GetFlags(EXPCMDFLAGS* pFlags);
    STDMETHODIMP GetCanonicalName(GUID* pguidCommandName);
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum);
#pragma endregion

private:
    std::wstring _GetPathFromExplorer() const;
};

CoCreatableClass(OpenTerminalHere);
