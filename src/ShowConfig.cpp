/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "Globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "TfInputProcessorProfile.h"
#include "BuildInfo.h"

//+---------------------------------------------------------------------------
//
// ITfFnConfigure::Show
//
//----------------------------------------------------------------------------
// static dialog procedure

// Set to false to use new DIMESettings.exe UI, true to use legacy inline PropertySheet.
// Toggle this flag to switch between new and old settings UI during development.
static bool s_useLegacyUI = false;

HRESULT CDIME::Show(_In_opt_ HWND hwndParent, _In_ LANGID inLangid, _In_ REFGUID inRefGuidProfile)
{
	debugPrint(L"CDIME::Show()");

	if (_IsStoreAppMode() || _IsUILessMode() || _IsSecureMode()) return S_OK;

	// Determine IME mode for the settings UI
	IME_MODE settingsMode = Global::imeMode;
	if (inRefGuidProfile == GUID_NULL)
		settingsMode = Global::imeMode;
	else if (inRefGuidProfile == Global::DIMEDayiGuidProfile)
		settingsMode = IME_MODE::IME_MODE_DAYI;
	else if (inRefGuidProfile == Global::DIMEPhoneticGuidProfile)
		settingsMode = IME_MODE::IME_MODE_PHONETIC;
	else if (inRefGuidProfile == Global::DIMEArrayGuidProfile)
		settingsMode = IME_MODE::IME_MODE_ARRAY;
	else if (inRefGuidProfile == Global::DIMEGenericGuidProfile)
		settingsMode = IME_MODE::IME_MODE_GENERIC;

	if (_IsComposing() && _pContext) _EndComposition(_pContext);
	_DeleteCandidateList(TRUE, _pContext);

	// ================================================================
	// New UI: launch DIMESettings.exe --mode <mode>
	// ================================================================
	if (!s_useLegacyUI) {
		// Map IME mode to --mode argument string
		const wchar_t* modeName = L"dayi";
		switch (settingsMode) {
		case IME_MODE::IME_MODE_DAYI:     modeName = L"dayi"; break;
		case IME_MODE::IME_MODE_ARRAY:    modeName = L"array"; break;
		case IME_MODE::IME_MODE_PHONETIC: modeName = L"phonetic"; break;
		case IME_MODE::IME_MODE_GENERIC:  modeName = L"generic"; break;
		default: break;
		}

		// Build path to DIMESettings.exe in %ProgramFiles%\DIME\ (or %ProgramW6432%\DIME\)
		WCHAR exePath[MAX_PATH] = {};
		WCHAR programFiles[MAX_PATH] = {};
		if (GetEnvironmentVariable(L"ProgramW6432", programFiles, MAX_PATH) == 0)
			GetEnvironmentVariable(L"ProgramFiles", programFiles, MAX_PATH);
		StringCchPrintf(exePath, MAX_PATH, L"%s\\DIME\\DIMESettings.exe", programFiles);

		// Launch DIMESettings.exe --mode <mode>
		WCHAR cmdLine[MAX_PATH] = {};
		StringCchPrintf(cmdLine, MAX_PATH, L"\"%s\" --mode %s", exePath, modeName);
		debugPrint(L"CDIME::Show() launching: %s", cmdLine);

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (CreateProcessW(exePath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
			// Don't wait — DIMESettings runs independently.
			// If already running, DIMESettings handles single-instance via mutex + WM_COPYDATA.
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			return S_OK;
		}
		debugPrint(L"CDIME::Show() CreateProcess failed: %d — falling through to legacy UI", GetLastError());
		// Fall through to legacy UI if DIMESettings.exe not found
	}

	// ================================================================
	// Legacy UI: inline PropertySheet (old dialog)
	// ================================================================
	CConfig::SetIMEMode(settingsMode);

	CTfInputProcessorProfile* profile = new CTfInputProcessorProfile();
	LANGID langid = inLangid;
	GUID guidProfile = inRefGuidProfile;

	if (SUCCEEDED(profile->CreateInstance()))
	{
		if (langid == 0)
			profile->GetCurrentLanguage(&langid);
		if (guidProfile == GUID_NULL)
		{
			CLSID clsid;
			profile->GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile);
		}

		CDIMEArray <LanguageProfileInfo> langProfileInfoList;
		profile->GetReverseConversionProviders(langid, &langProfileInfoList);
		CConfig::SetReverseConvervsionInfoList(&langProfileInfoList);
	}
	delete profile;

	_LoadConfig(FALSE, CConfig::GetIMEMode());

	// comctl32.dll and comdlg32.dll can't be loaded into immersivemode (app container), thus use late binding here.
	HINSTANCE dllCtlHandle = NULL;
	dllCtlHandle = LoadLibrary(L"comctl32.dll");

	// Load Rich Edit control library (must be loaded before dialog creation)
	HINSTANCE dllRichEditHandle = LoadLibrary(L"msftedit.dll");

	_T_CreatePropertySheetPage _CreatePropertySheetPage = NULL;
	_T_PropertySheet _PropertySheet = NULL;

	if (dllCtlHandle)
	{
		_CreatePropertySheetPage = reinterpret_cast<_T_CreatePropertySheetPage> (GetProcAddress(dllCtlHandle, "CreatePropertySheetPageW"));
		_PropertySheet = reinterpret_cast<_T_PropertySheet> (GetProcAddress(dllCtlHandle, "PropertySheetW"));
	}

	debugPrint(L"CDIME::Show() ,  ITfFnConfigure::Show(), _autoCompose = %d,  _doBeep = %d", CConfig::GetAutoCompose(),  CConfig::GetDoBeep());

	PROPSHEETPAGE psp;
	PROPSHEETHEADER psh;
	struct {
		int id;
		DLGPROC DlgProc;
	} DlgPage[] = {
		{ IDD_DIALOG_COMMON, CConfig::CommonPropertyPageWndProc },
		{ IDD_DIALOG_DICTIONARY, CConfig::DictionaryPropertyPageWndProc }
	};
	HPROPSHEETPAGE hpsp[_countof(DlgPage)]{};
	int i;

	ZeroMemory(&psp, sizeof(PROPSHEETPAGE));
	psp.dwSize = sizeof(PROPSHEETPAGE);
	psp.dwFlags = PSP_PREMATURE;
	psp.hInstance = Global::dllInstanceHandle;

	for (i = 0; i < _countof(DlgPage); i++)
	{
		psp.pszTemplate = MAKEINTRESOURCE(DlgPage[i].id);
		psp.pfnDlgProc = DlgPage[i].DlgProc;
		DialogContext* pCtx = new (std::nothrow) DialogContext();
		if (pCtx) {
			pCtx->imeMode = CConfig::GetIMEMode();
			pCtx->maxCodes = CConfig::GetMaxCodes();
			pCtx->pEngine = _pCompositionProcessorEngine;
			pCtx->engineOwned = false;
			psp.lParam = (LPARAM)pCtx;
		} else {
			psp.lParam = 0;
		}
		if (_CreatePropertySheetPage)
			hpsp[i] = (*_CreatePropertySheetPage)(&psp);
		if (!hpsp[i] && pCtx) { delete pCtx; }
	}

	ZeroMemory(&psh, sizeof(PROPSHEETHEADER));
	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_DEFAULT | PSH_NOCONTEXTHELP | PSH_USECALLBACK;
	psh.pfnCallback = CConfig::PropSheetCallback;
	psh.hInstance = Global::dllInstanceHandle;
	psh.hwndParent = hwndParent;
	psh.nPages = _countof(DlgPage);
	psh.phpage = hpsp;
	psh.pszCaption = L"DIME User Settings";

	WCHAR dialogCaption[MAX_PATH] = { 0 };
	if (CConfig::GetIMEMode() == IME_MODE::IME_MODE_DAYI)
		StringCchCat(dialogCaption, MAX_PATH, L"DIME 大易輸入法設定");
	else if (CConfig::GetIMEMode() == IME_MODE::IME_MODE_ARRAY)
		StringCchCat(dialogCaption, MAX_PATH, L"DIME 行列輸入法設定");
	else if (CConfig::GetIMEMode() == IME_MODE::IME_MODE_GENERIC)
		StringCchCat(dialogCaption, MAX_PATH, L"DIME 自建輸入法設定");
	else if (CConfig::GetIMEMode() == IME_MODE::IME_MODE_PHONETIC)
		StringCchCat(dialogCaption, MAX_PATH, L"DIME 傳統注音輸入法設定");

	StringCchPrintf(dialogCaption, MAX_PATH, L"%s v%d.%d.%d.%d", dialogCaption,
		BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT, BUILD_DATE_1);
	psh.pszCaption = dialogCaption;

	if (_PropertySheet)
		(*_PropertySheet)(&psh);

	// Reload config after settings dialog closes
	_LoadConfig(FALSE, CConfig::GetIMEMode());
	if (_pUIPresenter) _pUIPresenter->ClearNotify();

	if (dllCtlHandle)
		FreeLibrary(dllCtlHandle);
	if (dllRichEditHandle)
		FreeLibrary(dllRichEditHandle);
	return S_OK;
}