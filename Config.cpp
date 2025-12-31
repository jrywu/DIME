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
//#define DEBUG_PRINT
#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <MLang.h>
#include <shellscalingapi.h>
#include "Globals.h"
#include "resource.h"
#include "DIME.h"
#include "DictionarySearch.h"
#include "File.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"
#include "CompositionProcessorEngine.h"


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
COLORREF CConfig::_itemColor = CANDWND_ITEM_COLOR;
COLORREF CConfig::_itemBGColor = GetSysColor(COLOR_3DHIGHLIGHT);
COLORREF CConfig::_selectedColor = CANDWND_SELECTED_ITEM_COLOR;
COLORREF CConfig::_selectedBGColor = CANDWND_SELECTED_BK_COLOR;
COLORREF CConfig::_phraseColor = CANDWND_PHRASE_COLOR;
COLORREF CConfig::_numberColor = CANDWND_NUM_COLOR;
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


void DrawColor(HWND hwnd, HDC hdc, COLORREF col)
{
	RECT rect;

	hdc = GetDC(hwnd);
	SelectObject(hdc, GetStockObject(BLACK_PEN));
	SetDCBrushColor(hdc, col);
	SelectObject(hdc, GetStockObject(DC_BRUSH));
	GetClientRect(hwnd, &rect);
	Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
	ReleaseDC(hwnd, hdc);
}

INT_PTR CALLBACK CConfig::CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	HWND hwnd;
	HDC hdc;
	HFONT hFont;
	CHOOSEFONT cf;
	LOGFONT lf = { 0 };
	int fontpoint = 12, fontweight = FW_NORMAL, x, y;
	size_t i;
	BOOL fontitalic = FALSE;
	WCHAR fontname[LF_FACESIZE] = { 0 };
	WCHAR* pwszFontFaceName;
	WCHAR num[16] = { 0 };
	RECT rect;
	POINT pt = { 0,0 };
	UINT sel = 0;

	CHOOSECOLORW cc = { 0 };
	static COLORREF colCust[16];
	PAINTSTRUCT ps;

	HINSTANCE dllDlgHandle = NULL;
	dllDlgHandle = LoadLibrary(L"comdlg32.dll");
	_T_ChooseColor _ChooseColor = NULL;
	_T_ChooseFont _ChooseFont = NULL;
	if (dllDlgHandle)
	{
		_ChooseColor = reinterpret_cast<_T_ChooseColor> (GetProcAddress(dllDlgHandle, "ChooseColorW"));
		_ChooseFont = reinterpret_cast<_T_ChooseFont> (GetProcAddress(dllDlgHandle, "ChooseFontW"));
	}

	switch (message)
	{
	case WM_INITDIALOG:

		ParseConfig(hDlg, TRUE);
		ret = TRUE;
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON_CHOOSEFONT:

			hdc = GetDC(hDlg);

			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			lf.lfHeight = -MulDiv(GetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, NULL, FALSE), GetDeviceCaps(hdc, LOGPIXELSY), 72);
			lf.lfCharSet = DEFAULT_CHARSET;

			ZeroMemory(&cf, sizeof(cf));
			cf.lStructSize = sizeof(CHOOSEFONT);
			cf.hwndOwner = hDlg;
			cf.lpLogFont = &lf;
			cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;// should not include CF_SELECTSCRIPT so as user can change the characterset

			if (_ChooseFont && ((*_ChooseFont)(&cf) == TRUE))
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);

				SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, lf.lfFaceName);
				lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
				fontweight = lf.lfWeight;
				fontitalic = lf.lfItalic;
				hFont = CreateFontIndirect(&lf);
				SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
				SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, cf.iPointSize / 10, FALSE);
			}

			ReleaseDC(hDlg, hdc);
			ret = TRUE;
			break;
		case IDC_BUTTON_RESTOREDEFAULT:
			wcsncpy_s(fontname, { L"微軟正黑體" }, _TRUNCATE);

			fontpoint = 12;
			fontweight = FW_NORMAL;
			fontitalic = FALSE;
			SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, fontname);
			hdc = GetDC(hDlg);
			hFont = CreateFont(-MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0,
				fontweight, fontitalic, FALSE, FALSE, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, fontname);
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
			ReleaseDC(hDlg, hdc);
			SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, fontpoint, FALSE);


			colors[0].color = CANDWND_ITEM_COLOR;
			colors[1].color = CANDWND_SELECTED_ITEM_COLOR;
			colors[2].color = GetSysColor(COLOR_3DHIGHLIGHT);
			colors[3].color = CANDWND_PHRASE_COLOR;
			colors[4].color = CANDWND_NUM_COLOR;
			colors[5].color = CANDWND_SELECTED_BK_COLOR;

			hdc = BeginPaint(hDlg, &ps);
			for (i = 0; i < _countof(colors); i++)
			{
				DrawColor(GetDlgItem(hDlg, colors[i].id), hdc, colors[i].color);
			}
			EndPaint(hDlg, &ps);
			PropSheet_Changed(GetParent(hDlg), hDlg);
			ret = TRUE;
			break;
		case IDC_EDIT_MAXWIDTH:
			switch (HIWORD(wParam))
			{
			case EN_CHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;


		case IDC_COMBO_IME_SHIFT_MODE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_DOUBLE_SINGLE_BYTE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_REVERSE_CONVERSION:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				_reloadReverseConversion = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_ARRAY_SCOPE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
				_arrayScope = (ARRAY_SCOPE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
				//reset autocompose mode if ARRAY_SCOPE::ARRAY40 is selected.
				if (_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)
				{
					_autoCompose = FALSE;
					_spaceAsPageDown = TRUE;
					CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, BST_UNCHECKED);
					CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_CHECKED);
				}
				else // autocompose is always true in ARRAY30
					_autoCompose = TRUE;

				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE),
					(_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);

				debugPrint(L"selected array scope item is %d", _arrayScope);
				break;
			default:
				break;
			}
			break;
		case IDC_COMBO_NUMERIC_PAD:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;
		case IDC_COMBO_PHONETIC_KEYBOARD:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;
		case IDC_CHECKBOX_AUTOCOMPOSE:
			if(IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED)
			{
				_spaceAsPageDown = FALSE;
				CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_UNCHECKED);
			}
			else
			{
				_spaceAsPageDown = TRUE;
				CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_CHECKED);
			}
		case IDC_CHECKBOX_CLEAR_ONBEEP:
		case IDC_CHECKBOX_DOBEEP:
		case IDC_CHECKBOX_DOBEEPNOTIFY:
		case IDC_RADIO_KEYBOARD_OPEN:
		case IDC_RADIO_KEYBOARD_CLOSE:
		case IDC_RADIO_OUTPUT_CHT:
		case IDC_RADIO_OUTPUT_CHS:
		case IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY:
		case IDC_CHECKBOX_DAYIARTICLEMODE:
		case IDC_CHECKBOX_ARRAY_FORCESP:
		case IDC_CHECKBOX_ARRAY_NOTIFYSP:
		case IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE:
		case IDC_CHECKBOX_PHRASE:
		case IDC_CHECKBOX_ARROWKEYSWPAGES:
		case IDC_CHECKBOX_SPACEASPAGEDOWN:
		case IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY:
		case IDC_CHECKBOX_SHOWNOTIFY:
			PropSheet_Changed(GetParent(hDlg), hDlg);
			ret = TRUE;
			break;
		case IDOK:
			WriteConfig(TRUE);
			ret = TRUE;
			break;
		default:
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		for (i = 0; i < _countof(colors); i++)
		{
			hwnd = GetDlgItem(hDlg, colors[i].id);
			GetWindowRect(hwnd, &rect);
			pt.x = x = GET_X_LPARAM(lParam);
			pt.y = y = GET_Y_LPARAM(lParam);
			ClientToScreen(hDlg, &pt);

			if (rect.left <= pt.x && pt.x <= rect.right &&
				rect.top <= pt.y && pt.y <= rect.bottom)
			{
				cc.lStructSize = sizeof(cc);
				cc.hwndOwner = hDlg;
				cc.hInstance = NULL;
				cc.rgbResult = colors[i].color;
				cc.lpCustColors = colCust;
				cc.Flags = CC_FULLOPEN | CC_RGBINIT;
				cc.lCustData = NULL;
				cc.lpfnHook = NULL;
				cc.lpTemplateName = NULL;

				if (_ChooseColor && ((*_ChooseColor)(&cc)))
				{
					hdc = GetDC(hDlg);
					DrawColor(hwnd, hdc, cc.rgbResult);
					ReleaseDC(hDlg, hdc);
					colors[i].color = cc.rgbResult;
					PropSheet_Changed(GetParent(hDlg), hDlg);
					ret = TRUE;
				}
				break;
			}
		}
		break;

	case WM_PAINT:
		hdc = BeginPaint(hDlg, &ps);
		for (i = 0; i < _countof(colors); i++)
		{
			DrawColor(GetDlgItem(hDlg, colors[i].id), hdc, colors[i].color);
		}
		EndPaint(hDlg, &ps);
		ret = TRUE;
		break;


	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:
			_autoCompose = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED;
			_customTablePriority = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY) == BST_CHECKED;
			_dayiArticleMode = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE) == BST_CHECKED;
			_clearOnBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_CLEAR_ONBEEP) == BST_CHECKED;
			_doBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP) == BST_CHECKED;
			_doBeepNotify = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY) == BST_CHECKED;
			_doBeepOnCandi = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP_CANDI) == BST_CHECKED;
			_makePhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_PHRASE) == BST_CHECKED;
			_activatedKeyboardMode = IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN) == BST_CHECKED;
			_doHanConvert = IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS) == BST_CHECKED;
			_showNotifyDesktop = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SHOWNOTIFY) == BST_CHECKED;
			_spaceAsPageDown = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN) == BST_CHECKED;
			_spaceAsFirstCandSelkey = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY) == BST_CHECKED;
			_arrowKeySWPages = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES) == BST_CHECKED;
			_arrayForceSP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_FORCESP) == BST_CHECKED;
			_arrayNotifySP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP) == BST_CHECKED;
			_arraySingleQuoteCustomPhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE) == BST_CHECKED;


			GetDlgItemText(hDlg, IDC_EDIT_MAXWIDTH, num, _countof(num));
			_maxCodes = _wtol(num);

			GetDlgItemText(hDlg, IDC_EDIT_FONTPOINT, num, _countof(num));
			_fontSize = _wtol(num);

			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			_fontWeight = lf.lfWeight;
			_fontItalic = lf.lfItalic;

			pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
			if (pwszFontFaceName)
			{
				GetDlgItemText(hDlg, IDC_EDIT_FONTNAME, pwszFontFaceName, LF_FACESIZE);
				if (_pFontFaceName)
					StringCchCopy(_pFontFaceName, LF_FACESIZE, pwszFontFaceName);
			}

			_itemColor = colors[0].color;
			_selectedColor = colors[1].color;
			_itemBGColor = colors[2].color;
			_phraseColor = colors[3].color;
			_numberColor = colors[4].color;
			_selectedBGColor = colors[5].color;

			hwnd = GetDlgItem(hDlg, IDC_COMBO_IME_SHIFT_MODE);
			_imeShiftMode = (IME_SHIFT_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected IME shift hotkey mode is %d", _imeShiftMode);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_DOUBLE_SINGLE_BYTE);
			_doubleSingleByteMode = (DOUBLE_SINGLE_BYTE_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected double single byte mode is %d", _doubleSingleByteMode);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_NUMERIC_PAD);
			_numericPad = (NUMERIC_PAD)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected Numeric pad mode is %d", _numericPad);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_REVERSE_CONVERSION);
			sel = (UINT)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected reverse conversion item is %d", sel);
			if (sel == 0)
			{
				_reverseConverstionCLSID = CLSID_NULL;
				_reverseConversionGUIDProfile = CLSID_NULL;
				_reverseConversionDescription = new (std::nothrow) WCHAR[4];
				if(_reverseConversionDescription)
					StringCchCopy(_reverseConversionDescription, 4, L"(無)");
			}
			else
			{
				sel--;
				_reverseConverstionCLSID = _reverseConvervsionInfoList->GetAt(sel)->clsid;
				_reverseConversionGUIDProfile = _reverseConvervsionInfoList->GetAt(sel)->guidProfile;
				_reverseConversionDescription = new (std::nothrow) WCHAR[wcslen(_reverseConvervsionInfoList->GetAt(sel)->description) + 1];
				if(_reverseConversionDescription)
					StringCchCopy(_reverseConversionDescription, wcslen(_reverseConvervsionInfoList->GetAt(sel)->description) + 1, _reverseConvervsionInfoList->GetAt(sel)->description);
			}

			if (_imeMode == IME_MODE::IME_MODE_ARRAY)
			{
				hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
				_arrayScope = (ARRAY_SCOPE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
				if (_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5)
					_autoCompose = TRUE;
				
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)?SW_HIDE:SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), 
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), 
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE),
					(_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				
				debugPrint(L"selected array scope item is %d", _arrayScope);
			}

			if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				hwnd = GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD);
				_phoneticKeyboardLayout = (PHONETIC_KEYBOARD_LAYOUT)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
				debugPrint(L"selected phonetic keyboard layout is %d", _phoneticKeyboardLayout);
			}


			WriteConfig(TRUE);
			ParseConfig(hDlg, FALSE);
			ret = TRUE;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	FreeLibrary(dllDlgHandle);
	return ret;


}


INT_PTR CALLBACK CConfig::DictionaryPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	//HWND hwnd;
	OPENFILENAMEW ofn;


	HINSTANCE dllDlgHandle = NULL;
	dllDlgHandle = LoadLibrary(L"comdlg32.dll");
	_T_GetOpenFileName _GetOpenFileName = NULL;
	_T_GetSaveFileName _GetSaveFileName = NULL;

	WCHAR customTableName[MAX_PATH] = L"\0";
	WCHAR targetName[MAX_PATH] = L"\0";;
	WCHAR wszAppData[MAX_PATH] = L"\0";;
	WCHAR wszUserDoc[MAX_PATH] = L"\0";
	WCHAR pathToLoad[MAX_PATH] = L"\0";;
	WCHAR pathToWrite[MAX_PATH] = L"\0";;

	enum {
		LOAD_CIN_TABLE,
		IMPORT_CUSTOM_TABLE,
		EXPORT_CUSTOM_TABLE,
		OPEN_NULL
	}openFileType = OPEN_NULL;


	//CSIDL_APPDATA  personal roaming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);
	SHGetSpecialFolderPath(NULL, wszUserDoc, CSIDL_MYDOCUMENTS, TRUE);

	if (dllDlgHandle)
	{
		_GetOpenFileName = reinterpret_cast<_T_GetOpenFileName> (GetProcAddress(dllDlgHandle, "GetOpenFileNameW"));
		_GetSaveFileName = reinterpret_cast<_T_GetSaveFileName> (GetProcAddress(dllDlgHandle, "GetSaveFileNameW"));
	}

	switch (message)
	{
	case WM_INITDIALOG:
		if (_imeMode == IME_MODE::IME_MODE_DAYI)
			StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-Custom.txt");
		else if (_imeMode == IME_MODE::IME_MODE_ARRAY)
			StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-Custom.txt");
		else if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
			StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-Custom.txt");
		else if (_imeMode == IME_MODE::IME_MODE_GENERIC)
			StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-Custom.txt");
		importCustomTableFile(hDlg, customTableName);
		_customTableChanged = FALSE;

		if (!(_loadTableMode || _imeMode == IME_MODE::IME_MODE_GENERIC))
		{
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_MAIN), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_PHRASE), SW_HIDE);
		}
		if (!(_loadTableMode && _imeMode == IME_MODE::IME_MODE_ARRAY))
		{
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SP), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_B), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_CD), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_EFG), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_PHRASE), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY40), SW_HIDE);
		}

		ret = TRUE;
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON_LOAD_MAIN:
			openFileType = LOAD_CIN_TABLE;
			if (_imeMode == IME_MODE::IME_MODE_DAYI)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Dayi.cin");
			else if (_imeMode == IME_MODE::IME_MODE_ARRAY)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array.cin");
			else if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phone.cin");
			else if (_imeMode == IME_MODE::IME_MODE_GENERIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Generic.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_PHRASE:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phrase.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SC:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-shortcode.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SP:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-special.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_B:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-B.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_CD:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-CD.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_EFG:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-EF.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY40:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array40.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_PHRASE:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Phrase.cin");
			goto LoadFile;
		case IDC_BUTTON_EXPORT_CUSTOM:
			openFileType = EXPORT_CUSTOM_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\CUSTOM.txt");
			goto LoadFile;
		case IDC_BUTTON_IMPORT_CUSTOM:
			openFileType = IMPORT_CUSTOM_TABLE;
			_customTableChanged = TRUE;
			if (_imeMode == IME_MODE::IME_MODE_DAYI)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\DAYI-CUSTOM.txt");
			else if (_imeMode == IME_MODE::IME_MODE_ARRAY)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\ARRAY-CUSTOM.txt");
			else if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\PHONETIC-CUSTOM.txt");
			else
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\GENERIC-CUSTOM.txt");
			goto LoadFile;
		LoadFile:
			pathToLoad[0] = L'\0';
			if (openFileType == EXPORT_CUSTOM_TABLE)
				StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszUserDoc, L"\\customTable.txt");
			//else
			//StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszUserDoc, L"\\");
			ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
			ofn.lStructSize = sizeof(OPENFILENAMEW);
			ofn.hwndOwner = hDlg;
			ofn.lpstrFile = pathToLoad;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = (openFileType != EXPORT_CUSTOM_TABLE) ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT;
			ofn.lpstrFilter = (openFileType == LOAD_CIN_TABLE) ? L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0" : L"詞庫文字檔(*.txt)\0*.txt\0\0";


			if (openFileType == EXPORT_CUSTOM_TABLE && _GetSaveFileName && (_GetSaveFileName)(&ofn) != 0)
			{
				exportCustomTableFile(hDlg, pathToLoad);
			}
			else if (openFileType != EXPORT_CUSTOM_TABLE  && _GetOpenFileName && (_GetOpenFileName)(&ofn) != 0)
			{
				//PropSheet_Changed(GetParent(hDlg), hDlg);
				debugPrint(L"file name: %s selected", pathToLoad);

				StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, targetName);

				if (openFileType == IMPORT_CUSTOM_TABLE)
				{
					importCustomTableFile(hDlg, pathToLoad);
				}
				else
				{
					if (parseCINFile(pathToLoad, pathToWrite))
						MessageBox(GetFocus(), L"自訂碼表載入完成。", L"DIME 自訂碼表", MB_ICONINFORMATION);
					else
						MessageBox(GetFocus(), L"自訂碼表載入發生錯誤 !!", L"DIME 自訂碼表", MB_ICONERROR);


				}

			}
			break;
		case IDC_EDIT_CUSTOM_TABLE:
			switch (HIWORD(wParam))
			{
			case EN_CHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				_customTableChanged = TRUE;
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		default:
			break;
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:
			if (_customTableChanged)
			{
				if (_imeMode == IME_MODE::IME_MODE_DAYI)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-CUSTOM.cin");
				}
				else if (_imeMode == IME_MODE::IME_MODE_ARRAY)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-CUSTOM.cin");

				}
				else if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-CUSTOM.cin");
				}
				else
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-CUSTOM.cin");
				}
				exportCustomTableFile(hDlg, pathToLoad);
				// parse custom.txt file to UTF-16 and internal format
				if (!parseCINFile(pathToLoad, pathToWrite, TRUE))
					MessageBox(GetFocus(), L"自建詞庫載入發生錯誤 !!", L"DIME 自訂詞庫", MB_ICONERROR);
			}

			//CConfig::WriteConfig();
			ret = TRUE;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	FreeLibrary(dllDlgHandle);
	return ret;


}


void CConfig::ParseConfig(HWND hDlg, BOOL initDiag)
{
	HWND hwnd;
	size_t i;
	WCHAR num[16] = { 0 };
	WCHAR fontname[LF_FACESIZE] = { 0 };
	int fontpoint = 12, fontweight = FW_NORMAL, logPixelY, LogFontSize;
	BOOL fontitalic = FALSE;
	HDC hdc;
	HFONT hFont;
	static COLORREF colCust[16];

	wcsncpy_s(fontname, _pFontFaceName, _TRUNCATE);

	fontpoint = _fontSize;
	fontweight = _fontWeight;
	fontitalic = _fontItalic;

	if (fontpoint < 8 || fontpoint > 72)
	{
		fontpoint = 12;
	}
	if (fontweight < 0 || fontweight > 1000)
	{
		fontweight = FW_NORMAL;
	}
	if (fontitalic != TRUE && fontitalic != FALSE)
	{
		fontitalic = FALSE;
	}

	SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, fontname);
	hdc = GetDC(hDlg);
	logPixelY = GetDeviceCaps(hdc, LOGPIXELSY);
	if (_GetDpiForMonitor)
	{
		HMONITOR monitor = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
		UINT dpiX, dpiY;
		_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		if (dpiY > 0) logPixelY = dpiY;
	}
	LogFontSize = -MulDiv(10, logPixelY, 72);

	hFont = CreateFont(LogFontSize, 0, 0, 0,
		fontweight, fontitalic, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, fontname);
	SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
	ReleaseDC(hDlg, hdc);

	SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, fontpoint, FALSE);

	ZeroMemory(&colCust, sizeof(colCust));

	colors[0].color = _itemColor;
	colors[1].color = _selectedColor;
	colors[2].color = _itemBGColor;
	colors[3].color = _phraseColor;
	colors[4].color = _numberColor;
	colors[5].color = _selectedBGColor;

	hwnd = GetDlgItem(hDlg, IDC_COMBO_REVERSE_CONVERSION);

	if(initDiag)
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"(無)");
	
	if (IsEqualCLSID(_reverseConversionGUIDProfile, CLSID_NULL))
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)0, 0);
	for (i = 0; i < _reverseConvervsionInfoList->Count(); i++)
	{
		if (initDiag)
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)_reverseConvervsionInfoList->GetAt(i)->description);
		
		if (IsEqualCLSID(_reverseConversionGUIDProfile, _reverseConvervsionInfoList->GetAt(i)->guidProfile))
			SendMessage(hwnd, CB_SETCURSEL, (WPARAM)i + 1, 0);
	}


	_snwprintf_s(num, _TRUNCATE, L"%d", _maxCodes);
	SetDlgItemTextW(hDlg, IDC_EDIT_MAXWIDTH, num);
	CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWNOTIFY, (_showNotifyDesktop) ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, (_autoCompose) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_CLEAR_ONBEEP, (_clearOnBeep) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, (_doBeep) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY, (_doBeepNotify) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP_CANDI, (_doBeepOnCandi) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY, (_customTablePriority) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE, (_arraySingleQuoteCustomPhrase) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE, (_dayiArticleMode) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_FORCESP, (_arrayForceSP) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP, (_arrayNotifySP) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, (_makePhrase) ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_OPEN, (_activatedKeyboardMode) ? BST_CHECKED : BST_UNCHECKED);
	if (!IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN))
	{
		CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_CLOSE, BST_CHECKED);
	}
	CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHS, (_doHanConvert) ? BST_CHECKED : BST_UNCHECKED);
	if (!IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS))
	{
		CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHT, BST_CHECKED);
	}
	CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, (_spaceAsPageDown) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY, (_spaceAsFirstCandSelkey) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES, (_arrowKeySWPages) ? BST_CHECKED : BST_UNCHECKED);

	hwnd = GetDlgItem(hDlg, IDC_COMBO_IME_SHIFT_MODE);
	if(initDiag)
	{	
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"左右SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"右SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"左SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"無(僅Ctrl-Space鍵)");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_imeShiftMode, 0);

	hwnd = GetDlgItem(hDlg, IDC_COMBO_DOUBLE_SINGLE_BYTE);
	if (initDiag)
	{
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"以 Shift-Space 熱鍵切換");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"半型");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"全型");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_doubleSingleByteMode, 0);

	// initial Numeric pad combobox
	hwnd = GetDlgItem(hDlg, IDC_COMBO_NUMERIC_PAD);
	if (initDiag)
	{
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"數字鍵盤輸入數字符號");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"數字鍵盤輸入字根");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"僅用數字鍵盤輸入字根");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_numericPad, 0);

	if (_imeMode != IME_MODE::IME_MODE_GENERIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY), SW_HIDE);
	}
	if (_imeMode != IME_MODE::IME_MODE_PHONETIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PHONETIC_KEYBOARD), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD), SW_HIDE);
	}
	if (_imeMode != IME_MODE::IME_MODE_DAYI)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_DOBEEP_CANDI), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE), SW_HIDE);
	}
	if (_imeMode != IME_MODE::IME_MODE_ARRAY)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_ARRAY_SCOPE), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE), SW_HIDE);
	}
	
	if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_EDIT_MAXWIDTH), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_EDIT_MAXWIDTH), SW_HIDE);
		hwnd = GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD);
		if (initDiag)
		{
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"標準鍵盤");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"倚天鍵盤");
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_phoneticKeyboardLayout, 0);
	}

	if (_imeMode == IME_MODE::IME_MODE_ARRAY)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_EDIT_MAXWIDTH), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_EDIT_MAXWIDTH), SW_HIDE);
		if (_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), SW_HIDE);
		}
		else
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE), SW_HIDE);
		// initial Array scope combobox
		hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
		if (initDiag)
		{
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-AB");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A~D");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A~G");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列40 Big5");
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_arrayScope, 0);

	}
}


//+---------------------------------------------------------------------------
//
// writeConfig
//
//----------------------------------------------------------------------------

VOID CConfig::WriteConfig(BOOL confirmUpdated)
{
	debugPrint(L"CDIME::WriteConfig() \n");

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
		LoadConfig(_imeMode);
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
			fwprintf_s(fp, L"ItemColor = 0x%06X\n", _itemColor);
			fwprintf_s(fp, L"PhraseColor = 0x%06X\n", _phraseColor);
			fwprintf_s(fp, L"NumberColor = 0x%06X\n", _numberColor);
			fwprintf_s(fp, L"ItemBGColor = 0x%06X\n", _itemBGColor);
			fwprintf_s(fp, L"SelectedItemColor = 0x%06X\n", _selectedColor);
			fwprintf_s(fp, L"SelectedBGItemColor = 0x%06X\n", _selectedBGColor);
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


			if (_imeMode == IME_MODE::IME_MODE_DAYI)
			{
				fwprintf_s(fp, L"DayiArticleMode = %d\n", _dayiArticleMode ? 1 : 0);
			}

			if (_imeMode == IME_MODE::IME_MODE_ARRAY)
			{
				fwprintf_s(fp, L"ArrayScope = %d\n", _arrayScope);
				fwprintf_s(fp, L"ArrayForceSP = %d\n", _arrayForceSP ? 1 : 0);
				fwprintf_s(fp, L"ArrayNotifySP = %d\n", _arrayNotifySP ? 1 : 0);
				fwprintf_s(fp, L"ArraySingleQuoteCustomPhrase = %d\n", _arraySingleQuoteCustomPhrase ? 1 : 0);
			}

			if (_imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				fwprintf_s(fp, L"PhoneticKeyboardLayout = %d\n", _phoneticKeyboardLayout);
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

BOOL CConfig::LoadConfig(IME_MODE imeMode)
{
	debugPrint(L"CDIME::loadConfig() \n");
	SetIMEMode (imeMode);
	
	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;
	BOOL bRET = FALSE; 

	if (PathFileExists(_pwszINIFileName))
	{
		debugPrint(L"CDIME::loadConfig() config file = %s exists\n", _pwszINIFileName);
		struct _stat initTimeStamp;
		BOOL failed = _wstat(_pwszINIFileName, &initTimeStamp) != 0;  //error for retrieving timestamp
		BOOL updated = FALSE;
		if (!failed)
			updated = difftime(initTimeStamp.st_mtime, _initTimeStamp.st_mtime) > 0;
		
		debugPrint(L"CDIME::loadConfig() wstat failed = %d, config file updated = %d\n", failed, updated);
		if (failed || updated || _configIMEMode!=imeMode)
		{
			bRET = TRUE;
			CFile* iniDictionaryFile;
			iniDictionaryFile = new (std::nothrow) CFile();
			if (iniDictionaryFile && (iniDictionaryFile)->CreateFile(_pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
			{
				CTableDictionaryEngine* iniTableDictionaryEngine;
				iniTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), iniDictionaryFile, DICTIONARY_TYPE::INI_DICTIONARY);//CHT:1028
				if (iniTableDictionaryEngine)
				{
					_loadTableMode = FALSE; // reset _loadTableMode first. If no _loadTableMode is exist we should not should load tables buttons
					iniTableDictionaryEngine->ParseConfig(imeMode); //parse config first.
					debugPrint(L"CDIME::loadConfig() parsed. _loadTableMode = %d\n", _loadTableMode);
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
				WriteConfig(FALSE);
			}
		}
	}
	else
	{
		//should do IM specific default here.
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
		}
		else if (imeMode == IME_MODE::IME_MODE_DAYI)
		{
			_autoCompose = FALSE;
			_maxCodes = 4;
			_spaceAsPageDown = 0;
			_spaceAsFirstCandSelkey = 1;
		}
		else
		{
			_autoCompose = FALSE;
			_maxCodes = 4;
			_spaceAsPageDown = 0;
			_spaceAsFirstCandSelkey = 0;
		}

		if (imeMode != IME_MODE::IME_MODE_NONE)
			WriteConfig(FALSE); // config.ini is not there. create one.
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

BOOL CConfig::importCustomTableFile(_In_ HWND hDlg, _In_ LPCWSTR pathToLoad)
{
	BOOL success = FALSE;

	if (PathFileExists(pathToLoad)) //failed back to try preload Dayi.cin in program files.
	{
		success = TRUE;
		HANDLE hCustomTable = NULL;
		DWORD dwDataLen = 0;
		LPCWSTR customText = nullptr;
		
		if ((hCustomTable = CreateFile(pathToLoad, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
		{	// Error
			success = FALSE;
			goto Cleanup;
		}
		// Get file size
		if (hCustomTable && (dwDataLen = GetFileSize(hCustomTable, NULL)) == INVALID_FILE_SIZE)
		{	// Error
			success = FALSE;
			goto Cleanup;
		}
		// Create a buffer for the custom table text
		
		customText = new (std::nothrow) WCHAR[dwDataLen + 1];
		if (customText == nullptr)
		{// Error
			success = FALSE;
			goto Cleanup;
		}
		ZeroMemory((LPVOID)customText, (dwDataLen + 1) * sizeof (WCHAR));
		// Read the file
		if (hCustomTable && !ReadFile(hCustomTable, (LPVOID)customText, dwDataLen, &dwDataLen, NULL))
		{	// Error
			success = FALSE;
			goto Cleanup;
		}

		if (!IsTextUnicode(customText, dwDataLen, NULL))
		{
			WCHAR* outWStr = nullptr;
			UINT codepage = 0;

			//IMultiLanguage initialization
			if(FAILED(CoInitialize(NULL)))
			{	// Error
				success = FALSE;
				goto Cleanup;
			}
			IMultiLanguage2 *lang = NULL;
			HRESULT hr = CoCreateInstance(CLSID_CMultiLanguage, NULL,
				CLSCTX_ALL, IID_IMultiLanguage2, (LPVOID*)&lang);


			int detectEncCount = 1;
			DetectEncodingInfo detectEnc;
			INT inlen = dwDataLen;
			lang->DetectInputCodepage(MLDETECTCP_HTML, 0, (char *)customText, &inlen, &detectEnc, &detectEncCount);
			codepage = detectEnc.nCodePage;

			DWORD pdwMode = 0;
			UINT outlen = 0;
			UINT uinlen = dwDataLen;
			if (SUCCEEDED(hr)) {
				hr = lang->ConvertStringToUnicode(&pdwMode, codepage, (char *)customText, &uinlen, NULL, &outlen);
				outWStr = new (std::nothrow) WCHAR[outlen + 1];
				if (outWStr == nullptr)
				{// Error
					success = FALSE;
					goto Cleanup;
				}
				ZeroMemory(outWStr, (outlen + 1)*sizeof(WCHAR));
			}

			//convert to unicode
			if (SUCCEEDED(hr)) {
				hr = lang->ConvertStringToUnicode(&pdwMode, codepage, (char *)customText, &uinlen, outWStr, &outlen);
			}

			if (lang)
				lang->Release();
			CoUninitialize();

			if (outWStr)
			{
				SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, outWStr);
				delete[]outWStr;
			}

		}
		else if(customText)
		{
			SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, customText);
		}

	Cleanup:
		if (hCustomTable) CloseHandle(hCustomTable);
		if (customText) delete[]customText;
	}
	return success;
}


BOOL CConfig::exportCustomTableFile(_In_ HWND hDlg, _In_ LPCWSTR pathToWrite)
{
	//write the edittext context into custom.txt
	BOOL success = TRUE;
	DWORD dwDataLen;
	LPWSTR customText = nullptr;
	HANDLE hCustomTableFile = NULL;
	DWORD lpNumberOfBytesWritten = 0;
	WCHAR byteOrder = 0xFEFF;

	dwDataLen = GetWindowTextLength(GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE));
	customText = new (std::nothrow) WCHAR[dwDataLen + 1];
	if (customText == nullptr)
	{
		// Error
		success = FALSE;
		goto Cleanup;
	}
	ZeroMemory(customText, (dwDataLen + 1)*sizeof(WCHAR));
	GetDlgItemText(hDlg, IDC_EDIT_CUSTOM_TABLE, customText, dwDataLen + 1);

	// Create a file to save custom table
	if ((hCustomTableFile = CreateFile(pathToWrite, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{	// Error
		success = FALSE;
		goto Cleanup;
	}

	//Write Byte order mark to the file if the first byte of buf is not BOM
	
	if (customText[0] != byteOrder && !WriteFile(hCustomTableFile, (LPCVOID)&byteOrder, (DWORD)sizeof(WCHAR), &lpNumberOfBytesWritten, NULL))
	{	// Error
		success = FALSE;
		goto Cleanup;
	}
	// Write the custom table text to the file
	if (!WriteFile(hCustomTableFile, (LPCVOID)customText, (DWORD)dwDataLen*sizeof(WCHAR), &lpNumberOfBytesWritten, NULL))
	{	// Error
		success = FALSE;
		goto Cleanup;
	}
Cleanup:
	if (hCustomTableFile) CloseHandle(hCustomTableFile);
	if (customText) delete[]customText;

	return success;
}

BOOL CConfig::parseCINFile(_In_ LPCWSTR pathToLoad, _In_ LPCWSTR pathToWrite, _In_ BOOL customTableMode)
{
	FILE *fpr, *fpw;
	errno_t ret;
	BOOL success = TRUE;
	ret = _wfopen_s(&fpr, pathToLoad, (customTableMode) ? L"r, ccs=UTF-16LE" : L"r, ccs=UTF-8"); // custom table custom.txt in roaming profile is in UTF-16LE encoding.
	if (ret != 0)
	{
		MessageBox(GetFocus(), L"指定檔案無法開啟!!", L"File open error!", MB_ICONERROR);
	}
	else
	{
		if (_wfopen_s(&fpw, pathToWrite, L"w+, ccs=UTF-16LE") == 0)
		{
			WCHAR line[MAX_COMMIT_LENGTH] = { 0 }, key[MAX_KEY_LENGTH] = { 0 }, value[MAX_VALUE_LENGTH] = { 0 }, escapedKey[MAX_KEY_LENGTH] = { 0 }, 
				escapedValue[MAX_VALUE_LENGTH] = { 0 },	sep[MAX_KEY_LENGTH] = { 0 }, others[MAX_COMMIT_LENGTH] = { 0 };
			BOOL doEscape = FALSE;

			if (customTableMode) fwprintf_s(fpw, L"%s", L"%chardef begin\n");

			while (fgetws(line, 256, fpr) != NULL)
			{
				if (swscanf_s(line, L"%[^\t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3)
				{
					if (swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3)
					{
						if (!doEscape)
							fwprintf_s(fpw, L"%s", line);
						continue;
					}
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

				if (doEscape)
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
