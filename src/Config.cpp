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
//efine DEBUG_PRINT
#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <MLang.h>
#include <shellscalingapi.h>
#include "Globals.h"
#include "DIME.h"
#include "DictionarySearch.h"
#include "File.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"
#include "CompositionProcessorEngine.h"
#ifndef DIMESettings
#include "UIPresenter.h"
#endif
#include <string>
#include <vector>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif


#pragma comment(lib, "Shlwapi.lib")
//static configuration settings initialization
IME_MODE CConfig::_imeMode = IME_MODE::IME_MODE_NONE;
BOOL CConfig::_loadTableMode = FALSE;
ARRAY_SCOPE CConfig::_arrayScope = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
NUMERIC_PAD CConfig::_numericPad = NUMERIC_PAD::NUMERIC_PAD_MUMERIC;
BOOL CConfig::_clearOnBeep = TRUE;
BOOL CConfig::_doBeep = TRUE;
BOOL CConfig::_doBeepNotify = TRUE;
BOOL CConfig::_doBeepOnCandi = FALSE;
BOOL CConfig::_autoCompose = FALSE;
BOOL CConfig::_customTablePriority = FALSE;
BOOL CConfig::_arrayForceSP = FALSE;
BOOL CConfig::_arrayNotifySP = TRUE;
BOOL CConfig::_arrowKeySWPages = TRUE;
BOOL CConfig::_spaceAsPageDown = FALSE;
BOOL CConfig::_spaceAsFirstCandSelkey = FALSE;
UINT CConfig::_fontSize = 12;
UINT CConfig::_fontWeight = FW_NORMAL;
BOOL CConfig::_fontItalic = FALSE;
UINT CConfig::_maxCodes = 4;
BOOL CConfig::_appPermissionSet = FALSE;
BOOL CConfig::_activatedKeyboardMode = TRUE;
BOOL CConfig::_makePhrase = TRUE;
BOOL CConfig::_doHanConvert = FALSE;
BOOL CConfig::_showNotifyDesktop = TRUE;
BOOL CConfig::_dayiArticleMode = FALSE;  // Article mode: input full-shaped symbols with address keys
BOOL CConfig::_customTableChanged = FALSE;
BOOL CConfig::_arraySingleQuoteCustomPhrase = FALSE;
BOOL CConfig::_big5Filter = FALSE;

UINT CConfig::_dpiY = 0;
_T_GetDpiForMonitor CConfig::_GetDpiForMonitor = nullptr;

PHONETIC_KEYBOARD_LAYOUT CConfig::_phoneticKeyboardLayout = PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT;
IME_SHIFT_MODE CConfig::_imeShiftMode = IME_SHIFT_MODE::IME_BOTH_SHIFT;
DOUBLE_SINGLE_BYTE_MODE CConfig::_doubleSingleByteMode = DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE;

CDIMEArray <LanguageProfileInfo>* CConfig::_reverseConvervsionInfoList = new (std::nothrow) CDIMEArray <LanguageProfileInfo>;
CLSID CConfig::_reverseConverstionCLSID = CLSID_NULL;
GUID CConfig::_reverseConversionGUIDProfile = CLSID_NULL;
WCHAR* CConfig::_reverseConversionDescription = nullptr;
WCHAR CConfig::_pwzsDIMEProfile[] = L"\0";
WCHAR CConfig::_pwszINIFileName[] = L"\0";
IME_MODE CConfig::_configIMEMode = IME_MODE::IME_MODE_NONE;
BOOL CConfig::_reloadReverseConversion = FALSE;

WCHAR CConfig::_pFontFaceName[] = { L"微軟正黑體" };
WCHAR CConfig::_pMlFontFaceName[] = { L"細明體" };

COLORREF CConfig::_itemColor = CANDWND_ITEM_COLOR;
COLORREF CConfig::_itemBGColor = GetSysColor(COLOR_3DHIGHLIGHT);
COLORREF CConfig::_selectedColor = CANDWND_SELECTED_ITEM_COLOR;
COLORREF CConfig::_selectedBGColor = CANDWND_SELECTED_BK_COLOR;
COLORREF CConfig::_phraseColor = CANDWND_PHRASE_COLOR;
COLORREF CConfig::_numberColor = CANDWND_NUM_COLOR;
COLORREF CConfig::_lightItemColor = CANDWND_ITEM_COLOR;
COLORREF CConfig::_lightItemBGColor = GetSysColor(COLOR_3DHIGHLIGHT);
COLORREF CConfig::_lightSelectedColor = CANDWND_SELECTED_ITEM_COLOR;
COLORREF CConfig::_lightSelectedBGColor = CANDWND_SELECTED_BK_COLOR;
COLORREF CConfig::_lightPhraseColor = CANDWND_PHRASE_COLOR;
COLORREF CConfig::_lightNumberColor = CANDWND_NUM_COLOR;
COLORREF CConfig::_darkItemColor = CANDWND_DARK_ITEM_COLOR;
COLORREF CConfig::_darkItemBGColor = CANDWND_DARK_ITEM_BG_COLOR;
COLORREF CConfig::_darkSelectedColor = CANDWND_DARK_SELECTED_COLOR;
COLORREF CConfig::_darkSelectedBGColor = CANDWND_DARK_SELECTED_BG_COLOR;
COLORREF CConfig::_darkPhraseColor = CANDWND_DARK_PHRASE_COLOR;
COLORREF CConfig::_darkNumberColor = CANDWND_DARK_NUM_COLOR;
IME_COLOR_MODE CConfig::_colorMode = IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM;
bool CConfig::_colorModeKeyFound = false;
ColorInfo CConfig::colors[6] =
{
	{ IDC_COL_FR, CANDWND_ITEM_COLOR },
	{ IDC_COL_SEFR, CANDWND_SELECTED_ITEM_COLOR },
	{ IDC_COL_BG, GetSysColor(COLOR_3DHIGHLIGHT) },
	{ IDC_COL_PHRASE, CANDWND_PHRASE_COLOR },
	{ IDC_COL_NU, CANDWND_NUM_COLOR },
	{ IDC_COL_SEBG, CANDWND_SELECTED_BK_COLOR }
};

struct _stat CConfig::_initTimeStamp = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; //zero the timestamp


// Load colors[] from the palette for the given mode.
void CConfig::LoadColorsForMode(IME_COLOR_MODE mode)
{
	switch (mode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:
		colors[0].color = _darkItemColor;
		colors[1].color = _darkSelectedColor;
		colors[2].color = _darkItemBGColor;
		colors[3].color = _darkPhraseColor;
		colors[4].color = _darkNumberColor;
		colors[5].color = _darkSelectedBGColor;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:
		colors[0].color = _itemColor;
		colors[1].color = _selectedColor;
		colors[2].color = _itemBGColor;
		colors[3].color = _phraseColor;
		colors[4].color = _numberColor;
		colors[5].color = _selectedBGColor;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
		if (IsSystemDarkTheme())
		{
			colors[0].color = _darkItemColor;
			colors[1].color = _darkSelectedColor;
			colors[2].color = _darkItemBGColor;
			colors[3].color = _darkPhraseColor;
			colors[4].color = _darkNumberColor;
			colors[5].color = _darkSelectedBGColor;
		}
		else
		{
			colors[0].color = _lightItemColor;
			colors[1].color = _lightSelectedColor;
			colors[2].color = _lightItemBGColor;
			colors[3].color = _lightPhraseColor;
			colors[4].color = _lightNumberColor;
			colors[5].color = _lightSelectedBGColor;
		}
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:
	default:
		colors[0].color = _lightItemColor;
		colors[1].color = _lightSelectedColor;
		colors[2].color = _lightItemBGColor;
		colors[3].color = _lightPhraseColor;
		colors[4].color = _lightNumberColor;
		colors[5].color = _lightSelectedBGColor;
		break;
	}
}

// Save colors[] back to the palette for the given mode.
void CConfig::SaveColorsForMode(IME_COLOR_MODE mode)
{
	switch (mode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:
		_darkItemColor     = colors[0].color;
		_darkSelectedColor = colors[1].color;
		_darkItemBGColor   = colors[2].color;
		_darkPhraseColor   = colors[3].color;
		_darkNumberColor   = colors[4].color;
		_darkSelectedBGColor = colors[5].color;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:
		_itemColor         = colors[0].color;
		_selectedColor     = colors[1].color;
		_itemBGColor       = colors[2].color;
		_phraseColor       = colors[3].color;
		_numberColor       = colors[4].color;
		_selectedBGColor   = colors[5].color;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
		break; // color boxes disabled in SYSTEM mode — nothing to save
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:
	default:
		_lightItemColor     = colors[0].color;
		_lightSelectedColor = colors[1].color;
		_lightItemBGColor   = colors[2].color;
		_lightPhraseColor   = colors[3].color;
		_lightNumberColor   = colors[4].color;
		_lightSelectedBGColor = colors[5].color;
		break;
	}
}

// Detect Windows dark theme (safe on Win7/8/10/11)
bool CConfig::IsSystemDarkTheme()
{
	// Get OS build number once using RtlGetVersion
	static const DWORD build = []() -> DWORD {
		typedef NTSTATUS (WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
		HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
		if (!hNtdll) return 0;
		auto pFn = reinterpret_cast<RtlGetVersionFn>(
			GetProcAddress(hNtdll, "RtlGetVersion")
		);
		RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
		return (pFn && pFn(&osvi) == 0) ? osvi.dwBuildNumber : 0;
	}();

	// On Win10 1809+ (build >= 17763): use ShouldAppsUseDarkMode API
	if (build >= 17763)
	{
		typedef bool (WINAPI* FnShouldAppsUseDarkMode)();
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnShouldAppsUseDarkMode>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132))
			);
			if (pFn) return pFn();
		}
	}

	// Registry fallback (Win7/8/pre-1809 always land here)
	DWORD lightTheme = 1;
	DWORD cb = sizeof(lightTheme);
	RegGetValueW(HKEY_CURRENT_USER,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_REG_DWORD, nullptr, &lightTheme, &cb);
	return lightTheme == 0;
}

bool CConfig::GetEffectiveDarkMode()
{
	switch (_colorMode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:  return false;
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:   return true;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM: return false; // custom uses user colors, no dark preset
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
	default:
		return IsSystemDarkTheme();
	}
}

COLORREF CConfig::GetActiveItemBGColor()
{
	switch (_colorMode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:   return _darkItemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:  return _lightItemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM: return _itemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
	default:
		return IsSystemDarkTheme() ? _darkItemBGColor : _lightItemBGColor;
	}
}

#ifndef DIMESettings
void CConfig::ApplyIMEColorSet(CUIPresenter* pPresenter, bool isPhraseMode)
{
	if (!pPresenter) return;

	bool dark = GetEffectiveDarkMode();

	if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
	{
		// Use the 6 user-configured colors
		COLORREF textColor = isPhraseMode ? _phraseColor : _itemColor;
		pPresenter->_SetCandidateTextColor(textColor, _itemBGColor);
		pPresenter->_SetCandidateNumberColor(_numberColor, _itemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_selectedColor, _selectedBGColor);
		pPresenter->_SetCandidateFillColor(_itemBGColor);
	}
	else if (dark)
	{
		COLORREF textColor = isPhraseMode ? _darkPhraseColor : _darkItemColor;
		pPresenter->_SetCandidateTextColor(textColor, _darkItemBGColor);
		pPresenter->_SetCandidateNumberColor(_darkNumberColor, _darkItemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_darkSelectedColor, _darkSelectedBGColor);
		pPresenter->_SetCandidateFillColor(_darkItemBGColor);
	}
	else
	{
		// Light palette (customizable; defaults match compile-time constants)
		COLORREF textColor = isPhraseMode ? _lightPhraseColor : _lightItemColor;
		pPresenter->_SetCandidateTextColor(textColor, _lightItemBGColor);
		pPresenter->_SetCandidateNumberColor(_lightNumberColor, _lightItemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_lightSelectedColor, _lightSelectedBGColor);
		pPresenter->_SetCandidateFillColor(_lightItemBGColor);
	}
}
#endif // !DIMESettings

//+---------------------------------------------------------------------------
//
// writeConfig
//
//----------------------------------------------------------------------------

VOID CConfig::WriteConfig(IME_MODE imeMode, BOOL confirmUpdated)
{
	debugPrint(L"CConfig::WriteConfig() \n");

	struct _stat initTimeStamp;
	BOOL failed = _wstat(_pwszINIFileName, &initTimeStamp) != 0;
	BOOL updated = FALSE;
		if(!failed)
			updated = difftime(initTimeStamp.st_mtime, _initTimeStamp.st_mtime) > 0;

	if(confirmUpdated && !failed && updated)
	{
		//The config file is updated
		MessageBox(GetFocus(), L"新設定未生效!\n設定檔已被其他程式更新，請避免在兩個程式同時開啟設定頁面。",
			L"設定錯誤", MB_ICONERROR);
		LoadConfig(imeMode);
	}
	if(!confirmUpdated || failed || !updated)
	{
		FILE* fp;
		_wfopen_s(&fp, _pwszINIFileName, L"w, ccs=UTF-16LE"); // overwrite the file
		if (fp)
		{
			fwprintf_s(fp, L"[Config]\n");
			fwprintf_s(fp, L"AutoCompose = %d\n", _autoCompose ? 1 : 0);
			fwprintf_s(fp, L"SpaceAsPageDown = %d\n", _spaceAsPageDown ? 1 : 0);
			fwprintf_s(fp, L"SpaceAsFirstCandSelkey = %d\n", _spaceAsFirstCandSelkey ? 1 : 0);
			fwprintf_s(fp, L"ArrowKeySWPages = %d\n", _arrowKeySWPages ? 1 : 0);
			fwprintf_s(fp, L"ClearOnBeep = %d\n", _clearOnBeep ? 1 : 0);
			fwprintf_s(fp, L"DoBeep = %d\n", _doBeep ? 1 : 0);
			fwprintf_s(fp, L"DoBeepNotify = %d\n", _doBeepNotify ? 1 : 0);
			fwprintf_s(fp, L"DoBeepOnCandi = %d\n", _doBeepOnCandi ? 1 : 0);
			fwprintf_s(fp, L"ActivatedKeyboardMode = %d\n", _activatedKeyboardMode ? 1 : 0);
			fwprintf_s(fp, L"MakePhrase = %d\n", _makePhrase ? 1 : 0);
			fwprintf_s(fp, L"MaxCodes = %d\n", _maxCodes);
			fwprintf_s(fp, L"IMEShiftMode  = %d\n", _imeShiftMode);
			fwprintf_s(fp, L"DoubleSingleByteMode = %d\n", _doubleSingleByteMode);
			fwprintf_s(fp, L"ShowNotifyDesktop = %d\n", _showNotifyDesktop ? 1 : 0);
			fwprintf_s(fp, L"DoHanConvert = %d\n", _doHanConvert ? 1 : 0);
			fwprintf_s(fp, L"FontSize = %d\n", _fontSize);
			fwprintf_s(fp, L"FontItalic = %d\n", _fontItalic ? 1 : 0);
			fwprintf_s(fp, L"FontWeight = %d\n", _fontWeight);
			fwprintf_s(fp, L"FontFaceName = %s\n", _pFontFaceName);
			fwprintf_s(fp, L"ColorMode = %d\n", static_cast<int>(_colorMode));
			fwprintf_s(fp, L"ItemColor = 0x%06X\n", _itemColor);
			fwprintf_s(fp, L"PhraseColor = 0x%06X\n", _phraseColor);
			fwprintf_s(fp, L"NumberColor = 0x%06X\n", _numberColor);
			fwprintf_s(fp, L"ItemBGColor = 0x%06X\n", _itemBGColor);
			fwprintf_s(fp, L"SelectedItemColor = 0x%06X\n", _selectedColor);
			fwprintf_s(fp, L"SelectedBGItemColor = 0x%06X\n", _selectedBGColor);
			fwprintf_s(fp, L"LightItemColor = 0x%06X\n", _lightItemColor);
			fwprintf_s(fp, L"LightPhraseColor = 0x%06X\n", _lightPhraseColor);
			fwprintf_s(fp, L"LightNumberColor = 0x%06X\n", _lightNumberColor);
			fwprintf_s(fp, L"LightItemBGColor = 0x%06X\n", _lightItemBGColor);
			fwprintf_s(fp, L"LightSelectedItemColor = 0x%06X\n", _lightSelectedColor);
			fwprintf_s(fp, L"LightSelectedBGItemColor = 0x%06X\n", _lightSelectedBGColor);
			fwprintf_s(fp, L"DarkItemColor = 0x%06X\n", _darkItemColor);
			fwprintf_s(fp, L"DarkPhraseColor = 0x%06X\n", _darkPhraseColor);
			fwprintf_s(fp, L"DarkNumberColor = 0x%06X\n", _darkNumberColor);
			fwprintf_s(fp, L"DarkItemBGColor = 0x%06X\n", _darkItemBGColor);
			fwprintf_s(fp, L"DarkSelectedItemColor = 0x%06X\n", _darkSelectedColor);
			fwprintf_s(fp, L"DarkSelectedBGItemColor = 0x%06X\n", _darkSelectedBGColor);
			fwprintf_s(fp, L"CustomTablePriority = %d\n", _customTablePriority ? 1 : 0);
			//reversion conversion
			fwprintf_s(fp, L"ReloadReverseConversion = %d\n", _reloadReverseConversion);
			BSTR pbstr;
			if (SUCCEEDED(StringFromCLSID(_reverseConverstionCLSID, &pbstr)))
			{
				fwprintf_s(fp, L"ReverseConversionCLSID = %s\n", pbstr);
			}
			if (SUCCEEDED(StringFromCLSID(_reverseConversionGUIDProfile, &pbstr)))
			{
				fwprintf_s(fp, L"ReverseConversionGUIDProfile = %s\n", pbstr);
			}

			fwprintf_s(fp, L"ReverseConversionDescription = %s\n", _reverseConversionDescription);
			fwprintf_s(fp, L"AppPermissionSet = %d\n", _appPermissionSet ? 1 : 0);


			if (imeMode == IME_MODE::IME_MODE_DAYI)
			{
				fwprintf_s(fp, L"DayiArticleMode = %d\n", _dayiArticleMode ? 1 : 0);
			}

			if (imeMode == IME_MODE::IME_MODE_ARRAY)
			{
				fwprintf_s(fp, L"ArrayScope = %d\n", _arrayScope);
				fwprintf_s(fp, L"ArrayForceSP = %d\n", _arrayForceSP ? 1 : 0);
				fwprintf_s(fp, L"ArrayNotifySP = %d\n", _arrayNotifySP ? 1 : 0);
				fwprintf_s(fp, L"ArraySingleQuoteCustomPhrase = %d\n", _arraySingleQuoteCustomPhrase ? 1 : 0);
			}

			if (imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				fwprintf_s(fp, L"PhoneticKeyboardLayout = %d\n", _phoneticKeyboardLayout);
			}
			if (imeMode != IME_MODE::IME_MODE_ARRAY)
			{
				fwprintf_s(fp, L"Big5Filter = %d\n", _big5Filter ? 1 : 0);
			}
			fwprintf_s(fp, L"NumericPad = %d\n", _numericPad);
			if (_loadTableMode) fwprintf_s(fp, L"LoadTableMode = 1\n");
			fclose(fp);
			_wstat(_pwszINIFileName, &initTimeStamp);
			_initTimeStamp.st_mtime = initTimeStamp.st_mtime;
		}
	}


}

void CConfig::SetIMEMode(IME_MODE imeMode)
{
	if (_imeMode != imeMode)
	{
		_imeMode = imeMode;
		GUID guidProfile = CLSID_NULL;
		WCHAR wszAppData[MAX_PATH] = L"\0";
		SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);

		StringCchPrintf(_pwzsDIMEProfile, MAX_PATH, L"%s\\DIME", wszAppData);
		if (!PathFileExists(_pwzsDIMEProfile))
		{
			if (CreateDirectory(_pwzsDIMEProfile, NULL) == 0) return;
		}

		if (!PathFileExists(_pwzsDIMEProfile))
		{   //DIME roaming profile is not exist. Create one.
			if (CreateDirectory(_pwzsDIMEProfile, NULL) == 0) return;
		}
		if (imeMode == IME_MODE::IME_MODE_DAYI)
		{
			guidProfile = Global::DIMEDayiGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_ARRAY)
		{
			guidProfile = Global::DIMEArrayGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
		{
			guidProfile = Global::DIMEPhoneticGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\PhoneConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_GENERIC)
		{
			guidProfile = Global::DIMEGenericGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\GenericConfig.ini", _pwzsDIMEProfile);
		}
		else
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\config.ini", _pwzsDIMEProfile);


		// filter out self from reverse conversion list
		for (UINT i = 0; i < _reverseConvervsionInfoList->Count(); i++)
		{
			if (IsEqualCLSID(_reverseConvervsionInfoList->GetAt(i)->guidProfile, guidProfile))
				_reverseConvervsionInfoList->RemoveAt(i);
		}
	}
	return;

}

//+---------------------------------------------------------------------------
//
// loadConfig
//
//----------------------------------------------------------------------------

//+---------------------------------------------------------------------------
//
// SetIMEModeDefaults
//
// Set IME-specific default values based on mode
// This ensures corrupted config values fall back to appropriate defaults
//
//----------------------------------------------------------------------------
void CConfig::SetIMEModeDefaults(IME_MODE imeMode)
{
	if (imeMode == IME_MODE::IME_MODE_ARRAY)
	{
		_arrayScope = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
		_autoCompose = TRUE;
		_maxCodes = 5;
		_spaceAsPageDown = 0;
		_spaceAsFirstCandSelkey = 0;
	}
	else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		_autoCompose = FALSE;
		_maxCodes = 4;
		_spaceAsPageDown = 1;
		_spaceAsFirstCandSelkey = 0;
		_big5Filter = FALSE;
	}
	else if (imeMode == IME_MODE::IME_MODE_DAYI)
	{
		_autoCompose = FALSE;
		_maxCodes = 4;
		_spaceAsPageDown = 0;
		_spaceAsFirstCandSelkey = 1;
		_big5Filter = FALSE;
	}
	else
	{
		_autoCompose = FALSE;
		_maxCodes = 4;
		_spaceAsPageDown = 0;
		_spaceAsFirstCandSelkey = 0;
		_big5Filter = FALSE;
	}
}

//+---------------------------------------------------------------------------
//
// ResetAllDefaults
//
// Reset every config setting to its initial (C++ static initialiser) value,
// then apply mode-specific overrides via SetIMEModeDefaults().
// Used by the CLI --reset command so that global settings like FontSize are
// also restored even when a config file already exists on disk.
//
//----------------------------------------------------------------------------
void CConfig::ResetAllDefaults(IME_MODE imeMode)
{
	// Global defaults (mirrors the static-member initialisers in Config.cpp)
	_clearOnBeep                  = TRUE;
	_doBeep                       = TRUE;
	_doBeepNotify                 = TRUE;
	_doBeepOnCandi                = FALSE;
	_autoCompose                  = FALSE;
	_customTablePriority          = FALSE;
	_arrayForceSP                 = FALSE;
	_arrayNotifySP                = TRUE;
	_arraySingleQuoteCustomPhrase = FALSE;
	_arrowKeySWPages              = TRUE;
	_spaceAsPageDown              = FALSE;
	_spaceAsFirstCandSelkey       = FALSE;
	_fontSize                     = 12;
	_fontWeight                   = FW_NORMAL;
	_fontItalic                   = FALSE;
	_maxCodes                     = 4;
	_activatedKeyboardMode        = TRUE;
	_makePhrase                   = TRUE;
	_doHanConvert                 = FALSE;
	_showNotifyDesktop            = TRUE;
	_dayiArticleMode              = FALSE;
	_big5Filter                   = FALSE;
	_numericPad                   = NUMERIC_PAD::NUMERIC_PAD_MUMERIC;
	_phoneticKeyboardLayout       = PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT;
	_imeShiftMode                 = IME_SHIFT_MODE::IME_BOTH_SHIFT;
	_doubleSingleByteMode         = DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE;
	_arrayScope                   = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
	_colorMode = Global::isWindows1809OrLater
		? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM
		: IME_COLOR_MODE::IME_COLOR_MODE_LIGHT;
	// Default colour palette
	_itemColor           = CANDWND_ITEM_COLOR;
	_itemBGColor         = GetSysColor(COLOR_3DHIGHLIGHT);
	_selectedColor       = CANDWND_SELECTED_ITEM_COLOR;
	_selectedBGColor     = CANDWND_SELECTED_BK_COLOR;
	_phraseColor         = CANDWND_PHRASE_COLOR;
	_numberColor         = CANDWND_NUM_COLOR;
	_lightItemColor      = CANDWND_ITEM_COLOR;
	_lightItemBGColor    = GetSysColor(COLOR_3DHIGHLIGHT);
	_lightSelectedColor  = CANDWND_SELECTED_ITEM_COLOR;
	_lightSelectedBGColor= CANDWND_SELECTED_BK_COLOR;
	_lightPhraseColor    = CANDWND_PHRASE_COLOR;
	_lightNumberColor    = CANDWND_NUM_COLOR;
	_darkItemColor       = CANDWND_DARK_ITEM_COLOR;
	_darkItemBGColor     = CANDWND_DARK_ITEM_BG_COLOR;
	_darkSelectedColor   = CANDWND_DARK_SELECTED_COLOR;
	_darkSelectedBGColor = CANDWND_DARK_SELECTED_BG_COLOR;
	_darkPhraseColor     = CANDWND_DARK_PHRASE_COLOR;
	_darkNumberColor     = CANDWND_DARK_NUM_COLOR;
	// Font face name
	StringCchCopyW(_pFontFaceName, _countof(_pFontFaceName), L"微軟正黑體");
	// Reverse conversion (clear to empty)
	_reverseConverstionCLSID    = CLSID_NULL;
	_reverseConversionGUIDProfile = CLSID_NULL;
	// Apply mode-specific overrides on top of global defaults
	SetIMEModeDefaults(imeMode);
}

//+---------------------------------------------------------------------------
//
// LoadConfig
//
//----------------------------------------------------------------------------

BOOL CConfig::LoadConfig(IME_MODE imeMode)
{
	debugPrint(L"CConfig::loadConfig() \n");
	SetIMEMode (imeMode);

	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;
	BOOL bRET = FALSE;

	if (PathFileExists(_pwszINIFileName))
	{
		debugPrint(L"CConfig::loadConfig() config file = %s exists\n", _pwszINIFileName);
		struct _stat initTimeStamp;
		BOOL failed = _wstat(_pwszINIFileName, &initTimeStamp) != 0;  //error for retrieving timestamp
		BOOL updated = FALSE;
		if (!failed)
			updated = difftime(initTimeStamp.st_mtime, _initTimeStamp.st_mtime) > 0;

		debugPrint(L"CConfig::loadConfig() wstat failed = %d, config file updated = %d\n", failed, updated);
		if (failed || updated || _configIMEMode!=imeMode)
		{
			bRET = TRUE;

			// Set IME-specific defaults before parsing
			// Valid config values will override defaults; corrupted values are skipped keeping defaults
			SetIMEModeDefaults(imeMode);

			CFile* iniDictionaryFile;
			iniDictionaryFile = new (std::nothrow) CFile();
			if (iniDictionaryFile && (iniDictionaryFile)->CreateFile(_pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
			{
				CTableDictionaryEngine* iniTableDictionaryEngine;
				iniTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), iniDictionaryFile, DICTIONARY_TYPE::INI_DICTIONARY);//CHT:1028
				if (iniTableDictionaryEngine)
				{
					_loadTableMode = TRUE; // reset _loadTableMode first. If no _loadTableMode is exist we should not should load tables buttons
					_colorModeKeyFound = false; // reset before parse so absence of key is detectable
					_colorMode = IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM;
					iniTableDictionaryEngine->ParseConfig(imeMode); //parse config first.
					debugPrint(L"CConfig::loadConfig() parsed. _loadTableMode = %d\n", _loadTableMode);

					// Backward compat: old INI has no ColorMode key but may have the
					// legacy 6 color keys (ItemColor etc.) from before Light/Dark/Custom
					// palettes were introduced.  If the user had customized those colors,
					// migrate to CUSTOM to preserve them.  If still factory defaults,
					// migrate to SYSTEM (or LIGHT on pre-1809 Windows).
					if (!_colorModeKeyFound)
					{
						bool isDefault =
							_itemColor       == CANDWND_ITEM_COLOR &&
							_selectedColor   == CANDWND_SELECTED_ITEM_COLOR &&
							_selectedBGColor == CANDWND_SELECTED_BK_COLOR &&
							_phraseColor     == CANDWND_PHRASE_COLOR &&
							_numberColor     == CANDWND_NUM_COLOR &&
							_itemBGColor     == GetSysColor(COLOR_3DHIGHLIGHT);
						_colorMode = isDefault
							? (Global::isWindows1809OrLater
								? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM
								: IME_COLOR_MODE::IME_COLOR_MODE_LIGHT)
							: IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM;
					}
				}
				delete iniTableDictionaryEngine; // delete after config.ini config are parsed
				delete iniDictionaryFile;
				SetDefaultTextFont();
				_initTimeStamp.st_mtime = initTimeStamp.st_mtime;
				_configIMEMode = imeMode;
			}

			// In store app mode, the dll is loaded into app container which does not even have read right for IME profile in APPDATA.
			// Here, the read right is granted once to "ALL APPLICATION PACKAGES" when loaded in desktop mode, so as all metro apps can at least read the user settings in config.ini.
#ifdef DIMESettings
			if (!_appPermissionSet && imeMode != IME_MODE::IME_MODE_NONE)
#else
			if (!CDIME::_IsStoreAppMode() && !_appPermissionSet && imeMode != IME_MODE::IME_MODE_NONE)
#endif
			{
				EXPLICIT_ACCESS ea;
				// Get a pointer to the existing DACL (Conditionally).
				DWORD dwRes = GetNamedSecurityInfo(_pwzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
				if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				// Initialize an EXPLICIT_ACCESS structure for the new ACE.
				ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
				ea.grfAccessPermissions = GENERIC_READ;
				ea.grfAccessMode = GRANT_ACCESS;
				ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
				ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
				ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
				ea.Trustee.ptstrName = (LPWCH) L"Everyone";
				// Create a new ACL that merges the new ACE into the existing DACL.
				dwRes = SetEntriesInAcl(1, &ea, pOldDACL, &pNewDACL);
				if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				if (Global::isWindows8)
				{
					ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
					ea.Trustee.ptstrName = (LPWCH) L"ALL APPLICATION PACKAGES";
					dwRes = SetEntriesInAcl(1, &ea, pNewDACL, &pNewDACL);
					if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				}
				if (pNewDACL)
					SetNamedSecurityInfo(_pwzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);

				_appPermissionSet = TRUE;
				WriteConfig(imeMode, FALSE);
			}
		}
	}
	else
	{
		// Config file doesn't exist - set IME-specific defaults
		SetIMEModeDefaults(imeMode);
		// Use SYSTEM only on Win10 1809+; fall back to LIGHT on older Windows
		// so the combo shows the correct selection when settings first open.
		_colorMode = Global::isWindows1809OrLater
			? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM
			: IME_COLOR_MODE::IME_COLOR_MODE_LIGHT;

		if (imeMode != IME_MODE::IME_MODE_NONE)
			WriteConfig(imeMode, FALSE); // config.ini is not there. create one.
	}





ErrorExit:
	if (pNewDACL != NULL)
		LocalFree(pNewDACL);
	if (pSD != NULL)
		LocalFree(pSD);
	return bRET;  // return TRUE if the config file updated
}

void CConfig::SetDefaultTextFont(HWND hWnd)
{
	debugPrint(L"CConfig::SetDefaultTextFont()");
	UINT dpiY = 0;
	if (hWnd && _GetDpiForMonitor)
	{
		HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		UINT dpiX;
		_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
	}

	if (Global::defaultlFontHandle != nullptr && (hWnd == nullptr || ( Global::isWindows8 && _dpiY != dpiY) ))
	{
		debugPrint(L"CConfig::SetDefaultTextFont() delete old font");
		DeleteObject((HGDIOBJ)Global::defaultlFontHandle);
		Global::defaultlFontHandle = nullptr;
	}
	if (Global::defaultlFontHandle == nullptr)
	{
		HDC hDC = GetDC(nullptr);
		int logPixelY = GetDeviceCaps(hDC, LOGPIXELSY);
		if (Global::isWindows8 && dpiY > 0)
		{
			logPixelY = dpiY;
			_dpiY = dpiY;
		}
		int logFontSize = -MulDiv(_fontSize, logPixelY, 72);
		debugPrint(L"CConfig::SetDefaultTextFont() create new font. Logical y pixels = %d, font size =%d,  log font size = %d, _dpiY = %d",
			logPixelY, _fontSize, logFontSize, dpiY);

		Global::defaultlFontHandle = CreateFont(logFontSize, 0, 0, 0, _fontWeight, _fontItalic, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, _pFontFaceName);
		if (!Global::defaultlFontHandle)
		{
			LOGFONT lf = { 0 };
			SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
			// Fall back to the default GUI font on failure.
			Global::defaultlFontHandle = CreateFont(-MulDiv(_fontSize, GetDeviceCaps(hDC, LOGPIXELSY), 72),
				0, 0, 0, FW_MEDIUM, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, lf.lfFaceName);
		}
		if(hDC) ReleaseDC(nullptr, hDC);
	}

}


void CConfig::SetReverseConvervsionInfoList(CDIMEArray <LanguageProfileInfo> *reverseConvervsionInfoList)
{
	// clear _reverseConvervsionInfoList first.
	clearReverseConvervsionInfoList();

	for (UINT index = 0; index < reverseConvervsionInfoList->Count(); index++)
	{
		LanguageProfileInfo *infoItem = nullptr;
		infoItem = reverseConvervsionInfoList->GetAt(index);

		LanguageProfileInfo *_infoItem = nullptr;
		_infoItem = _reverseConvervsionInfoList->Append();
		_infoItem->clsid = infoItem->clsid;
		_infoItem->guidProfile = infoItem->guidProfile;
		PWCH _description = new (std::nothrow) WCHAR[wcslen(infoItem->description) + 1];
		if (_description)
		{
			*_description = L'\0';
			StringCchCopy(_description, wcslen(infoItem->description) + 1, infoItem->description);
			_infoItem->description = _description;
		}
	}
}

void CConfig::clearReverseConvervsionInfoList()
{
	for (UINT index = 0; index < _reverseConvervsionInfoList->Count(); index++)
	{
		LanguageProfileInfo *infoItem = nullptr;
		infoItem = _reverseConvervsionInfoList->GetAt(index);
		delete[] infoItem->description;
	}
	_reverseConvervsionInfoList->Clear();
}

BOOL CConfig::parseCINFile(_In_ LPCWSTR pathToLoad, _In_ LPCWSTR pathToWrite, _In_ BOOL customTableMode, _In_ BOOL suppressUI)
{
	FILE *fpr, *fpw;
	errno_t ret;
	BOOL success = TRUE;
	ret = _wfopen_s(&fpr, pathToLoad, (customTableMode) ? L"r, ccs=UTF-16LE" : L"r, ccs=UTF-8"); // custom table custom.txt in roaming profile is in UTF-16LE encoding.
	if (ret != 0)
	{
		if (!suppressUI)
			MessageBox(GetFocus(), L"指定檔案無法開啟!!", L"File open error!", MB_ICONERROR);
		return FALSE;
	}
	else
	{
		if (_wfopen_s(&fpw, pathToWrite, L"w+, ccs=UTF-16LE") == 0)
		{
			WCHAR line[MAX_COMMIT_LENGTH] = { 0 }, key[MAX_KEY_LENGTH] = { 0 }, value[MAX_VALUE_LENGTH] = { 0 }, escapedKey[MAX_KEY_LENGTH] = { 0 },
				escapedValue[MAX_VALUE_LENGTH] = { 0 },	sep[MAX_KEY_LENGTH] = { 0 }, others[MAX_COMMIT_LENGTH] = { 0 };
			BOOL doEscape = FALSE;

			if (customTableMode) {
				fwprintf_s(fpw, L"%s", L"%chardef begin\n");
				doEscape = TRUE;
			}

			while (fgetws(line, MAX_TABLE_LINE_LENGTH, fpr) != NULL)
			{
				// Try parsing key-value pair with 3 format patterns:
				// 1) key (with spaces, delimited by tab/newline) + whitespace sep + value (no spaces)
				// 2) key (no spaces) + whitespace sep + value (no spaces)
				// 3) key (no spaces) + whitespace sep + value (with spaces)
				if (swscanf_s(line, L"%[^\t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3
					&& swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3
					&& swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^\t\r\n]", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value)) != 3)
				{
					// All 3 patterns failed to parse a key-value pair, so treat the line as unparsable.
					// Write unparsable lines as-is only outside of escape sections;
					// inside escape sections, skip them to avoid corrupting escaped output.
					if (!doEscape)
						fwprintf_s(fpw, L"%s", line);
					continue;
				}
				if ((CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%keyname", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"begin", 5) == CSTR_EQUAL) ||
					(CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%chardef", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"begin", 5) == CSTR_EQUAL))
				{
					doEscape = TRUE;
					fwprintf_s(fpw, L"%s %s\n", key, value);
					continue;
				}
				else if ((CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%keyname", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"end", 3) == CSTR_EQUAL) ||
					(CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%chardef", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"end", 3) == CSTR_EQUAL))
				{
					doEscape = FALSE;
					fwprintf_s(fpw, L"%s %s\n", key, value);
					continue;
				}
				else if (CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%encoding", 9) == CSTR_EQUAL)
				{
					fwprintf_s(fpw, L"%s\tUTF-16LE\n", key);
					continue;
				}

				if (doEscape)
				{
					// Check if key and value are already quoted (start and end with ")
					size_t keyLen = wcslen(key);
					size_t valLen = wcslen(value);
					BOOL keyAlreadyQuoted = (keyLen >= 2 && key[0] == L'"' && key[keyLen - 1] == L'"');
					BOOL valAlreadyQuoted = (valLen >= 2 && value[0] == L'"' && value[valLen - 1] == L'"');

					if (keyAlreadyQuoted && valAlreadyQuoted)
					{
						// Already in escaped format — write as-is to avoid double-escaping
						fwprintf_s(fpw, L"%s\t%s\n", key, value);
					}
					else
					{
						StringCchCopy(escapedKey, _countof(escapedKey), L"\"");
						for (UINT i = 0; i < wcslen(key); i++)
						{
							if (key[i] == '"') //escape " .
							{
								StringCchCat(escapedKey, _countof(escapedKey), L"\\\"");
							}
							else if (key[i] == '\\') // escape \ .
							{
								StringCchCat(escapedKey, _countof(escapedKey), L"\\\\");
							}
							else
							{
								StringCchCatN(escapedKey, _countof(escapedKey), &key[i], 1);
							}
						}
						StringCchCat(escapedKey, _countof(escapedKey), L"\"");
						StringCchCopy(escapedValue, _countof(escapedValue), L"\"");
						for (UINT i = 0; i < wcslen(value); i++)
						{
							if (value[i] == '"') //escape " .
							{
								StringCchCat(escapedValue, _countof(escapedValue), L"\\\"");
							}
							else if (value[i] == '\\') // escape \ .
							{
								StringCchCat(escapedValue, _countof(escapedValue), L"\\\\");
							}
							else
							{
								StringCchCatN(escapedValue, _countof(escapedValue), &value[i], 1);
							}
						}
						StringCchCat(escapedValue, _countof(escapedValue), L"\"");
						fwprintf_s(fpw, L"%s\t%s\n", escapedKey, escapedValue);
					}
				}
				else
					fwprintf_s(fpw, L"%s\t%s\n", key, value);

		}
		if (customTableMode) fwprintf_s(fpw, L"%s", L"%chardef end\n");
		fclose(fpw);


	}
		else
		{
			success = FALSE;
		}
}
fclose(fpr);
return success;
}
