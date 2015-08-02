//#define DEBUG_PRINT

#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Globals.h"
#include "resource.h"
#include "DIME.h"
#include "DictionarySearch.h"
#include "File.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"
#include "TfInputProcessorProfile.h"
#include "CompositionProcessorEngine.h"

//static configuration settings initilization
BOOL CConfig::_doBeep = TRUE;
BOOL CConfig::_doBeepNotify = TRUE;
BOOL CConfig::_autoCompose = FALSE;
BOOL CConfig::_threeCodeMode = FALSE;
BOOL CConfig::_arrayForceSP = FALSE;
BOOL CConfig::_arrayNotifySP = TRUE;
BOOL CConfig::_arrowKeySWPages = TRUE;
BOOL CConfig::_spaceAsPageDown = FALSE;
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

CDIMEArray <LanguageProfileInfo>* CConfig::_reverseConvervsionInfoList = new (std::nothrow) CDIMEArray <LanguageProfileInfo>;
CLSID CConfig::_reverseConverstionCLSID = CLSID_NULL;
GUID CConfig::_reverseConversionGUIDProfile = CLSID_NULL;
WCHAR* CConfig::_reverseConversionDescription = nullptr;
BOOL CConfig::_reloadReverseConversion = FALSE;

WCHAR CConfig::_pFontFaceName[] = {L"Microsoft JhengHei"};
COLORREF CConfig::_itemColor = CANDWND_ITEM_COLOR;
COLORREF CConfig::_itemBGColor = GetSysColor(COLOR_3DHIGHLIGHT);
COLORREF CConfig::_selectedColor = CANDWND_SELECTED_ITEM_COLOR;
COLORREF CConfig::_selectedBGColor = CANDWND_SELECTED_BK_COLOR;
COLORREF CConfig::_phraseColor = CANDWND_PHRASE_COLOR;
COLORREF CConfig::_numberColor = CANDWND_NUM_COLOR;
ColorInfo CConfig::colors[6] = 
{
	{IDC_COL_FR,  CANDWND_ITEM_COLOR},
	{IDC_COL_SEFR, CANDWND_SELECTED_ITEM_COLOR},
	{IDC_COL_BG,   GetSysColor(COLOR_3DHIGHLIGHT)},
	{IDC_COL_PHRASE, CANDWND_PHRASE_COLOR},
	{IDC_COL_NU, CANDWND_NUM_COLOR},
	{IDC_COL_SEBG, CANDWND_SELECTED_BK_COLOR}
};

struct _stat CConfig::_initTimeStamp = {0,0,0,0,0,0,0,0,0,0,0}; //zero the timestamp


//+---------------------------------------------------------------------------
//
// ITfFnConfigure::Show
//
//----------------------------------------------------------------------------
// static dialog procedure

HRESULT CDIME::Show(_In_ HWND hwndParent, _In_ LANGID inLangid, _In_ REFGUID inRefGuidProfile)
{
	debugPrint(L"CDIME::Show()");

	if(_IsStoreAppMode() ||_IsUILessMode() || _IsSecureMode()) return S_OK;
	
	CTfInputProcessorProfile* profile = new CTfInputProcessorProfile();
	LANGID langid = inLangid;
	GUID guidProfile = inRefGuidProfile;
	
	if (SUCCEEDED(profile->CreateInstance()))
	{
		if(langid == 0)
			profile->GetCurrentLanguage(&langid);
		if(guidProfile == GUID_NULL)
		{
			CLSID clsid;
			profile->GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile);
		}
		
		CDIMEArray <LanguageProfileInfo> langProfileInfoList;
		profile->GetReverseConversionProviders(langid, &langProfileInfoList);
		CConfig::SetReverseConvervsionInfoList(&langProfileInfoList);
		
	}
	delete profile;

	_LoadConfig();
	
	if(_IsComposing() && _pContext) _EndComposition(_pContext);
	_DeleteCandidateList(TRUE, _pContext);

	

	// comctl32.dll and comdlg32.dll can't be loaded into immersivemode (app container), thus use late binding here.
	HINSTANCE dllCtlHandle = NULL;       
	dllCtlHandle = LoadLibrary(L"comctl32.dll");
	
	_T_CreatePropertySheetPage _CreatePropertySheetPage = NULL;
	_T_PropertySheet _PropertySheet = NULL;

    if(dllCtlHandle)
    {
        _CreatePropertySheetPage = reinterpret_cast<_T_CreatePropertySheetPage> ( GetProcAddress(dllCtlHandle, "CreatePropertySheetPageW"));
		_PropertySheet =  reinterpret_cast<_T_PropertySheet> (GetProcAddress(dllCtlHandle, "PropertySheetW"));
    }

	

	debugPrint(L"CDIME::Show() ,  ITfFnConfigure::Show(), _autoCompose = %d, _threeCodeMode = %d, _doBeep = %d", CConfig::GetAutoCompose(), CConfig::GetThreeCodeMode(), CConfig::GetDoBeep());

	PROPSHEETPAGE psp;
	PROPSHEETHEADER psh;
	struct {
		int id;
		DLGPROC DlgProc;
	} DlgPage[] = {
		{IDD_DIALOG_COMMON,	CConfig::CommonPropertyPageWndProc},
		{IDD_DIALOG_DICTIONARY,	CConfig::DictionaryPropertyPageWndProc},
		
	};
	HPROPSHEETPAGE hpsp[_countof(DlgPage)];
	int i;

	ZeroMemory(&psp, sizeof(PROPSHEETPAGE));
	psp.dwSize = sizeof(PROPSHEETPAGE);
	psp.dwFlags = PSP_PREMATURE;
	psp.hInstance = Global::dllInstanceHandle;
	
	for(i=0; i<_countof(DlgPage); i++)
	{
		psp.pszTemplate = MAKEINTRESOURCE(DlgPage[i].id);
		psp.pfnDlgProc = DlgPage[i].DlgProc;
		if(_CreatePropertySheetPage)
			hpsp[i] = (*_CreatePropertySheetPage)(&psp);
	}

	ZeroMemory(&psh, sizeof(PROPSHEETHEADER));
	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_DEFAULT | PSH_NOCONTEXTHELP;
	psh.hInstance = Global::dllInstanceHandle;
	psh.hwndParent = hwndParent;
	psh.nPages = _countof(DlgPage);
	psh.phpage = hpsp;
	psh.pszCaption = L"DIME User Settings";
	
	//PropertySheet(&psh);
	if(_PropertySheet)
		(*_PropertySheet)(&psh);

	FreeLibrary(dllCtlHandle);



	return S_OK;
}

INT_PTR CALLBACK CConfig::CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	HWND hwnd;
	size_t i;
	WCHAR num[16];
	WCHAR fontname[LF_FACESIZE];
	int fontpoint =12, fontweight = FW_NORMAL, x, y;
	BOOL fontitalic = FALSE;
	CHOOSEFONT cf;
	LOGFONT lf;
	HDC hdc;
	HFONT hFont;
	RECT rect;
	POINT pt;
	UINT sel=0;
	WCHAR *pwszFontFaceName;


	CHOOSECOLORW cc;
	static COLORREF colCust[16];
	PAINTSTRUCT ps;

	HINSTANCE dllDlgHandle = NULL;       
	dllDlgHandle = LoadLibrary(L"comdlg32.dll");
	_T_ChooseColor _ChooseColor = NULL;
	_T_ChooseFont _ChooseFont = NULL;
	if(dllDlgHandle)
	{
		_ChooseColor = reinterpret_cast<_T_ChooseColor> ( GetProcAddress(dllDlgHandle, "ChooseColorW"));
		_ChooseFont = reinterpret_cast<_T_ChooseFont> ( GetProcAddress(dllDlgHandle, "ChooseFontW"));
	}

	switch(message)
	{
	case WM_INITDIALOG:
		
		wcsncpy_s(fontname, _pFontFaceName , _TRUNCATE);

		fontpoint = _fontSize;
		fontweight = _fontWeight;
		fontitalic = _fontItalic;

		if(fontpoint < 8 || fontpoint > 72)
		{
			fontpoint = 12;
		}
		if(fontweight < 0 || fontweight > 1000)
		{
			fontweight = FW_NORMAL;
		}
		if(fontitalic != TRUE && fontitalic != FALSE)
		{
			fontitalic = FALSE;
		}

		SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, fontname);
		hdc = GetDC(hDlg);
		hFont = CreateFont(-MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0,
			fontweight, fontitalic, FALSE, FALSE, CHINESEBIG5_CHARSET,
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

		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) L"(無)");
		if(IsEqualCLSID(_reverseConversionGUIDProfile, CLSID_NULL))
			SendMessage(hwnd, CB_SETCURSEL, (WPARAM)0, 0);
		for(i=0; i< _reverseConvervsionInfoList->Count(); i++)
		{
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) _reverseConvervsionInfoList->GetAt(i)->description);
			if(IsEqualCLSID(_reverseConversionGUIDProfile, _reverseConvervsionInfoList->GetAt(i)->guidProfile))
				SendMessage(hwnd, CB_SETCURSEL, (WPARAM)i+1, 0);
		}
		

		_snwprintf_s(num, _TRUNCATE, L"%d", _maxCodes);
		SetDlgItemTextW(hDlg, IDC_EDIT_MAXWIDTH, num);
		CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWNOTIFY, (_showNotifyDesktop)?BST_CHECKED:BST_UNCHECKED);

		CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, (_autoCompose)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, (_doBeep)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY, (_doBeepNotify) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_THREECODEMODE,(_threeCodeMode)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE, (_dayiArticleMode) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_FORCESP,(_arrayForceSP)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP,(_arrayNotifySP)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, (_makePhrase)?BST_CHECKED:BST_UNCHECKED);	
		
		CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_OPEN, (_activatedKeyboardMode)?BST_CHECKED:BST_UNCHECKED);
		if(!IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN))
		{
			CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_CLOSE, BST_CHECKED);
		}
		CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHS, (_doHanConvert)?BST_CHECKED:BST_UNCHECKED);
		if(!IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS))
		{
			CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHT, BST_CHECKED);
		}
		CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, (_spaceAsPageDown)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES, (_arrowKeySWPages)?BST_CHECKED:BST_UNCHECKED);
		// hide autocompose and space as pagedown option in DAYI.
		/*
		if(Global::imeMode==IME_MODE_DAYI || Global::imeMode==IME_MODE_ARRAY)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN), SW_HIDE);
		}*/
		if(Global::imeMode==IME_MODE_ARRAY || Global::imeMode==IME_MODE_PHONETIC)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_EDIT_MAXWIDTH), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_STATIC_EDIT_MAXWIDTH), SW_HIDE);
		}
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_THREECODEMODE), SW_HIDE);  //Always hide 3code option
		if(Global::imeMode!=IME_MODE_DAYI)
		{
		
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE), SW_HIDE);
		}
		if(Global::imeMode!=IME_MODE_ARRAY)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), SW_HIDE);
		}
		ret = TRUE;
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_BUTTON_CHOOSEFONT:

			hdc = GetDC(hDlg);

			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			lf.lfHeight = -MulDiv(GetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, NULL, FALSE), GetDeviceCaps(hdc, LOGPIXELSY), 72);
			lf.lfCharSet = CHINESEBIG5_CHARSET;

			ZeroMemory(&cf, sizeof(cf));
			cf.lStructSize = sizeof(CHOOSEFONT);
			cf.hwndOwner = hDlg;
			cf.lpLogFont = &lf;
			cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_SELECTSCRIPT;
			
			//if(ChooseFont(&cf) == TRUE)
			if(_ChooseFont && ( (*_ChooseFont)(&cf) == TRUE))
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);

				SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, lf.lfFaceName);
				lf.lfHeight = -MulDiv(10,  GetDeviceCaps(hdc, LOGPIXELSY), 72);
				fontweight = lf.lfWeight;
				fontitalic = lf.lfItalic;
				hFont = CreateFontIndirect(&lf);
				SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
				SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, cf.iPointSize / 10, FALSE);
			}

			ReleaseDC(hDlg, hdc);
			ret = TRUE;
			break;

		case IDC_EDIT_MAXWIDTH:
			switch(HIWORD(wParam))
			{
			case EN_CHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_REVERSE_CONVERSION:
			switch(HIWORD(wParam))
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

		case IDC_CHECKBOX_AUTOCOMPOSE:
		case IDC_CHECKBOX_DOBEEP:
		case IDC_CHECKBOX_DOBEEPNOTIFY:
		case IDC_RADIO_KEYBOARD_OPEN:
		case IDC_RADIO_KEYBOARD_CLOSE:
		case IDC_RADIO_OUTPUT_CHT:
		case IDC_RADIO_OUTPUT_CHS:
		case IDC_CHECKBOX_THREECODEMODE:
		case IDC_CHECKBOX_DAYIARTICLEMODE:
		case IDC_CHECKBOX_ARRAY_FORCESP:
		case IDC_CHECKBOX_ARRAY_NOTIFYSP:
		case IDC_CHECKBOX_PHRASE:
		case IDC_CHECKBOX_ARROWKEYSWPAGES:
		case IDC_CHECKBOX_SPACEASPAGEDOWN:
		case IDC_CHECKBOX_SHOWNOTIFY:
			PropSheet_Changed(GetParent(hDlg), hDlg);
			ret = TRUE;
			break;
		case IDOK:
			CConfig::WriteConfig();
			ret = TRUE;
			break;
		default:
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		for(i=0; i<_countof(colors); i++)
		{
			hwnd = GetDlgItem(hDlg, colors[i].id);
			GetWindowRect(hwnd, &rect);
			pt.x = x = GET_X_LPARAM(lParam);
			pt.y = y = GET_Y_LPARAM(lParam);
			ClientToScreen(hDlg, &pt);

			if(rect.left <= pt.x && pt.x <= rect.right &&
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

				if(_ChooseColor && ( (*_ChooseColor)(&cc)))
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
		for(i=0; i<_countof(colors); i++)
		{
			DrawColor(GetDlgItem(hDlg, colors[i].id), hdc, colors[i].color);
		}
		EndPaint(hDlg, &ps);
		ret = TRUE;
		break;


	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:	
			_autoCompose = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED;
			_threeCodeMode = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_THREECODEMODE) == BST_CHECKED;
			_dayiArticleMode = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE) == BST_CHECKED;
			_doBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP) == BST_CHECKED;
			_doBeepNotify = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY) == BST_CHECKED;
			_makePhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_PHRASE) == BST_CHECKED;
			_activatedKeyboardMode = IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN) == BST_CHECKED;
			_doHanConvert = IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS) == BST_CHECKED;
			_showNotifyDesktop = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SHOWNOTIFY) == BST_CHECKED;
			_spaceAsPageDown = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN) == BST_CHECKED;
			_arrowKeySWPages = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES) == BST_CHECKED;
			_arrayForceSP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_FORCESP) == BST_CHECKED;
			_arrayNotifySP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP) == BST_CHECKED;


			GetDlgItemText(hDlg, IDC_EDIT_MAXWIDTH, num, _countof(num));
			_maxCodes = _wtol(num);
			
			GetDlgItemText(hDlg, IDC_EDIT_FONTPOINT, num, _countof(num));
			_fontSize = _wtol(num);
			
			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			_fontWeight = lf.lfWeight;
			_fontItalic = lf.lfItalic;

			pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
			GetDlgItemText(hDlg, IDC_EDIT_FONTNAME, pwszFontFaceName, LF_FACESIZE);
			StringCchCopy(_pFontFaceName, LF_FACESIZE, pwszFontFaceName);
				
			_itemColor = colors[0].color ;
			_selectedColor = colors[1].color;
			_itemBGColor = colors[2].color;
			_phraseColor = colors[3].color;
			_numberColor = colors[4].color;
			_selectedBGColor = colors[5].color;

			hwnd = GetDlgItem(hDlg, IDC_COMBO_REVERSE_CONVERSION);
			sel = (UINT) SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected reverse convertion item is %d",  sel);
			if(sel==0)
			{
				_reverseConverstionCLSID = CLSID_NULL;
				_reverseConversionGUIDProfile = CLSID_NULL;
				_reverseConversionDescription = new (std::nothrow) WCHAR[4];
				StringCchCopy(_reverseConversionDescription, 4, L"(無)");
			}
			else
			{
				sel--;
				_reverseConverstionCLSID = _reverseConvervsionInfoList->GetAt(sel)->clsid;
				_reverseConversionGUIDProfile = _reverseConvervsionInfoList->GetAt(sel)->guidProfile;
				_reverseConversionDescription = new (std::nothrow) WCHAR[wcslen( _reverseConvervsionInfoList->GetAt(sel)->description)+1];
				StringCchCopy(_reverseConversionDescription, wcslen( _reverseConvervsionInfoList->GetAt(sel)->description)+1,  _reverseConvervsionInfoList->GetAt(sel)->description);
			}

			CConfig::WriteConfig();
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

	WCHAR targetName[MAX_PATH];
	WCHAR wszAppData[MAX_PATH];
	WCHAR pathToLoad[MAX_PATH];
	WCHAR pathToWrite[MAX_PATH];
		
	//CSIDL_APPDATA  personal roadming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);

	if(dllDlgHandle)
	{
		_GetOpenFileName = reinterpret_cast<_T_GetOpenFileName> ( GetProcAddress(dllDlgHandle, "GetOpenFileNameW"));
	}

	switch(message)
	{
	case WM_INITDIALOG:
		if( Global::imeMode!=IME_MODE_ARRAY)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SC), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SP), SW_HIDE);
		}
	
		ret = TRUE;
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_BUTTON_LOAD_MAIN:
			if(Global::imeMode == IME_MODE_DAYI)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Dayi.cin");
			else if(Global::imeMode == IME_MODE_ARRAY)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array.cin");
			else if(Global::imeMode == IME_MODE_PHONETIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phone.cin");
			else if(Global::imeMode == IME_MODE_GENERIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Generic.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_PHRASE:
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phrase.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SC:
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-shortcode.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SP:
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-special.cin");
			goto LoadFile;
LoadFile:
			pathToLoad[0] = L'\0';
			ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
			ofn.lStructSize = sizeof(OPENFILENAMEW);
			ofn.hwndOwner = hDlg;
			ofn.lpstrFile = pathToLoad;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_FILEMUSTEXIST;
			ofn.lpstrFilter = L"CIN Files\0*.cin\0\0";
			
			if(_GetOpenFileName && (_GetOpenFileName)(&ofn) != 0)
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);
				debugPrint(L"file name: %s selected", pathToLoad);

				StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, targetName);
				
				FILE *fpr, *fpw;
				errno_t ret;
				ret = _wfopen_s(&fpr, pathToLoad, L"r, ccs=UTF-8");
				if(ret != 0)
				{
					MessageBox(GetFocus(), L"指定檔案無法開啟!!", L"File open error!", MB_ICONERROR);
				}
				else
				{
					if( _wfopen_s(&fpw, pathToWrite, L"w+, ccs=UTF-16LE") ==0 )
					{
						WCHAR line[256], key[256], value[256], escapedKey[256], escapedValue[256], 
							sep[256], others[256];
						BOOL doEscape = FALSE;
						while( fgetws(line, 256, fpr) != NULL)
						{
							if (swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3)
							{
								if(!doEscape)
									fwprintf_s(fpw, L"%s", line);
							}
							else 
							{
								if(	(CompareString(1028, NORM_IGNORECASE , key, (int) wcslen(key), L"%keyname", 8) == CSTR_EQUAL
									&& CompareString(1028, NORM_IGNORECASE , value, (int) wcslen(value), L"begin", 5) == CSTR_EQUAL)||
									(CompareString(1028, NORM_IGNORECASE , key, (int) wcslen(key), L"%chardef", 8) == CSTR_EQUAL
									&& CompareString(1028, NORM_IGNORECASE , value, (int) wcslen(value), L"begin", 5)== CSTR_EQUAL))
								{
									doEscape = TRUE;
									fwprintf_s(fpw, L"%s %s\n", key, value);
									continue;
								}
								else if((CompareString(1028, NORM_IGNORECASE , key, (int) wcslen(key), L"%keyname", 8)== CSTR_EQUAL
									&& CompareString(1028, NORM_IGNORECASE , value, (int) wcslen(value), L"end", 3)== CSTR_EQUAL)||
									(CompareString(1028, NORM_IGNORECASE , key, (int) wcslen(key), L"%chardef", 8)== CSTR_EQUAL
									&& CompareString(1028, NORM_IGNORECASE , value, (int) wcslen(value), L"end", 3)== CSTR_EQUAL))
								{
									doEscape = FALSE;
									fwprintf_s(fpw, L"%s %s\n", key, value);
									continue;
								}

								if(doEscape)
								{
									StringCchCopy(escapedKey, _countof(escapedKey), L"\"");
									for(UINT i = 0; i < wcslen(key); i++)
									{
										if(key[i] == '"') //escape " .
										{
											StringCchCat(escapedKey, _countof(escapedKey), L"\\\"");
										}
										else if (key[i] == '\\') // escaoe \ .
										{
											StringCchCat(escapedKey, _countof(escapedKey), L"\\\\");
										}
										else
										{
											StringCchCatN(escapedKey, _countof(escapedKey),&key[i], 1);
										}
									}
									StringCchCat(escapedKey, _countof(escapedKey), L"\"");
									StringCchCopy(escapedValue, _countof(escapedValue), L"\"");
									for(UINT i = 0; i < wcslen(value); i++)
									{
										if(value[i] == '"') //escape " .
										{
											StringCchCat(escapedValue, _countof(escapedValue), L"\\\"");
										}
										else if (value[i] == '\\') // escaoe \ .
										{
											StringCchCat(escapedValue, _countof(escapedValue), L"\\\\");
										}
										else
										{
											StringCchCatN(escapedValue, _countof(escapedValue),&value[i], 1);
										}
									}
									StringCchCat(escapedValue, _countof(escapedValue), L"\"");
									fwprintf_s(fpw, L"%s\t%s\n", escapedKey, escapedValue);
								}
								else
									fwprintf_s(fpw, L"%s\t%s\n", key, value);
							}
						}
						fclose(fpw);
						MessageBox(GetFocus(), L"檔案載入完成。", L"File loaded!", MB_ICONINFORMATION);

					}
					else
					{
						MessageBox(GetFocus(), L"檔案載入發生錯誤 !!", L"File open error!", MB_ICONERROR);
					}
				}
				fclose(fpr);
				
				
				
			}
			break;

		default:
			break;
		}
		break;

	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:	
			

			CConfig::WriteConfig();
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

//+---------------------------------------------------------------------------
//
// writeConfig
//
//----------------------------------------------------------------------------

VOID CConfig::WriteConfig()
{
	debugPrint(L"CDIME::updateConfig() \n");
	WCHAR wszAppData[MAX_PATH] = {'\0'};
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);	
	WCHAR wzsDIMEProfile[MAX_PATH] = {'\0'};
	WCHAR *pwszINIFileName = new (std::nothrow) WCHAR[MAX_PATH];

	if (!pwszINIFileName)  goto ErrorExit;

	*pwszINIFileName = L'\0';

	StringCchPrintf(wzsDIMEProfile, MAX_PATH, L"%s\\DIME", wszAppData);
	if(!PathFileExists(wzsDIMEProfile))
	{
		if(CreateDirectory(wzsDIMEProfile, NULL)==0) goto ErrorExit;
	}
	else
	{
		if(Global::imeMode == IME_MODE_DAYI)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_ARRAY)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_PHONETIC)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\PhoneConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_GENERIC)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\GenericConfig.ini", wzsDIMEProfile);
		else
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsDIMEProfile);

		FILE *fp;
		_wfopen_s(&fp, pwszINIFileName, L"w, ccs=UTF-16LE"); // overwrite the file
	if(fp)
	{
		fwprintf_s(fp, L"[Config]\n");
		fwprintf_s(fp, L"AutoCompose = %d\n", _autoCompose?1:0);
		fwprintf_s(fp, L"SpaceAsPageDown = %d\n", _spaceAsPageDown?1:0);
		fwprintf_s(fp, L"ArrowKeySWPages = %d\n", _arrowKeySWPages?1:0);
		fwprintf_s(fp, L"DoBeep = %d\n", _doBeep?1:0);
		fwprintf_s(fp, L"DoBeepNotify = %d\n", _doBeepNotify ? 1 : 0);
		fwprintf_s(fp, L"ActivatedKeyboardMode = %d\n", _activatedKeyboardMode?1:0);
		fwprintf_s(fp, L"MakePhrase = %d\n", _makePhrase?1:0);
		fwprintf_s(fp, L"MaxCodes = %d\n", _maxCodes);
		fwprintf_s(fp, L"ShowNotifyDesktop = %d\n", _showNotifyDesktop?1:0);
		fwprintf_s(fp, L"DoHanConvert = %d\n", _doHanConvert?1:0);
		fwprintf_s(fp, L"FontSize = %d\n", _fontSize);
		fwprintf_s(fp, L"FontItalic = %d\n", _fontItalic?1:0);
		fwprintf_s(fp, L"FontWeight = %d\n", _fontWeight);
		fwprintf_s(fp, L"FontFaceName = %s\n", _pFontFaceName);
		fwprintf_s(fp, L"ItemColor = 0x%06X\n", _itemColor);
		fwprintf_s(fp, L"PhraseColor = 0x%06X\n", _phraseColor);
		fwprintf_s(fp, L"NumberColor = 0x%06X\n", _numberColor);
		fwprintf_s(fp, L"ItemBGColor = 0x%06X\n", _itemBGColor);
		fwprintf_s(fp, L"SelectedItemColor = 0x%06X\n", _selectedColor);
		fwprintf_s(fp, L"SelectedBGItemColor = 0x%06X\n", _selectedBGColor);
		//reversion conversion
		fwprintf_s(fp, L"ReloadReversionConversion = %d\n", _reloadReverseConversion);
		BSTR pbstr;
		if (SUCCEEDED(StringFromCLSID(_reverseConverstionCLSID, &pbstr)))
		{
			fwprintf_s(fp, L"ReversionConversionCLSID = %s\n", pbstr);
		}
		if (SUCCEEDED(StringFromCLSID(_reverseConversionGUIDProfile, &pbstr)))
		{
			fwprintf_s(fp, L"ReversionConversionGUIDProfile = %s\n", pbstr);
		}
		
		fwprintf_s(fp, L"ReversionConversionDescription = %s\n", _reverseConversionDescription);


		if(Global::isWindows8)
			fwprintf_s(fp, L"AppPermissionSet = %d\n", _appPermissionSet?1:0);
		if(Global::imeMode == IME_MODE_DAYI)
			fwprintf_s(fp, L"ThreeCodeMode = %d\n", _threeCodeMode?1:0);
			fwprintf_s(fp, L"DayiArticleMode = %d\n", _dayiArticleMode ? 1 : 0);
		
		if(Global::imeMode == IME_MODE_ARRAY)
		{
			fwprintf_s(fp, L"ArrayForceSP = %d\n", _arrayForceSP?1:0);
			fwprintf_s(fp, L"ArrayNotifySP = %d\n", _arrayNotifySP?1:0);
		}
		


		fclose(fp);
	}
	}

ErrorExit:
    delete []pwszINIFileName;
}

//+---------------------------------------------------------------------------
//
// loadConfig
//
//----------------------------------------------------------------------------

VOID CConfig::LoadConfig()
{	
	debugPrint(L"CDIME::loadConfig() \n");
	WCHAR wszAppData[MAX_PATH] = {'\0'};
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);	
	WCHAR wzsDIMEProfile[MAX_PATH] = {'\0'};
	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;

	WCHAR *pwszINIFileName = new (std::nothrow) WCHAR[MAX_PATH];
    
	if (!pwszINIFileName)  goto ErrorExit;

	*pwszINIFileName = L'\0';

	StringCchPrintf(wzsDIMEProfile, MAX_PATH, L"%s\\DIME", wszAppData);
	if(PathFileExists(wzsDIMEProfile))
	{ 
		
		if(Global::imeMode == IME_MODE_DAYI)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_ARRAY)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_PHONETIC)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\PhoneConfig.ini", wzsDIMEProfile);
		else if(Global::imeMode == IME_MODE_GENERIC)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\GenericConfig.ini", wzsDIMEProfile);
		else
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsDIMEProfile);

		if(PathFileExists(pwszINIFileName))
		{
			struct _stat initTimeStamp;
			if (_wstat(pwszINIFileName, &initTimeStamp) || //error for retrieving timestamp
				(long(initTimeStamp.st_mtime>>32) != long(_initTimeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
				(long(initTimeStamp.st_mtime) != long(_initTimeStamp.st_mtime)) )			// then load the config. skip otherwise
			{
				_initTimeStamp.st_mtime = initTimeStamp.st_mtime;

				CFile *iniDictionaryFile;
				iniDictionaryFile = new (std::nothrow) CFile();
				if ((iniDictionaryFile)->CreateFile(pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
				{
					CTableDictionaryEngine * iniTableDictionaryEngine;
					iniTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), iniDictionaryFile, TTS_DICTIONARY);//CHT:1028
					if (iniTableDictionaryEngine)
					{
						iniTableDictionaryEngine->ParseConfig(IME_MODE_NONE); //parse config first.
					}
					delete iniTableDictionaryEngine; // delete after config.ini config are pasrsed
					delete iniDictionaryFile;
					SetDefaultTextFont();
				}

				// In store app mode, the dll is loaded into app container which does not even have read right for IME profile in APPDATA.
				// Here, the read right is granted once to "ALL APPLICATION PACKAGES" when loaded in desktop mode, so as all metro apps can at least read the user settings in config.ini.				
				if(Global::isWindows8 && ! CDIME::_IsStoreAppMode() && ! _appPermissionSet ) 
				{
					EXPLICIT_ACCESS ea;
					// Get a pointer to the existing DACL (Conditionaly).
					DWORD dwRes = GetNamedSecurityInfo(wzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
					if(ERROR_SUCCESS != dwRes) goto ErrorExit;
					// Initialize an EXPLICIT_ACCESS structure for the new ACE. 
					ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
					ea.grfAccessPermissions = GENERIC_READ;
					ea.grfAccessMode = GRANT_ACCESS;
					ea.grfInheritance= SUB_CONTAINERS_AND_OBJECTS_INHERIT;
					ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
					ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
					ea.Trustee.ptstrName = L"ALL APPLICATION PACKAGES";	

					// Create a new ACL that merges the new ACE into the existing DACL.
					dwRes = SetEntriesInAcl(1, &ea, pOldDACL, &pNewDACL);
					if(ERROR_SUCCESS != dwRes) goto ErrorExit;
					if(pNewDACL)
						SetNamedSecurityInfo(wzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);

					_appPermissionSet = TRUE;
					WriteConfig(); // update the config file.

				}

				

			}
			

			
		}
		else
		{
			//should do IM specific default here.
			WriteConfig(); // config.ini is not there. create one.
		}
	}
	else
	{
		//DIME roadming profile is not exist. Create one.
		if(CreateDirectory(wzsDIMEProfile, NULL)==0) goto ErrorExit;
	}

	

ErrorExit:
	if(pNewDACL != NULL) 
		LocalFree(pNewDACL);
	if(pSD != NULL)
		LocalFree(pSD);
    delete []pwszINIFileName;

}

void CConfig::SetDefaultTextFont()
{
	//if(_pCompositionProcessorEngine == nullptr) return;
    // Candidate Text Font
    if (Global::defaultlFontHandle != nullptr)
	{
		DeleteObject ((HGDIOBJ) Global::defaultlFontHandle);
		Global::defaultlFontHandle = nullptr;
	}
	if (Global::defaultlFontHandle == nullptr)
    {
			Global::defaultlFontHandle = CreateFont(-MulDiv(_fontSize, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72), 
			0, 0, 0, _fontWeight, _fontItalic, 0, 0, CHINESEBIG5_CHARSET, 0, 0, 0, 0, _pFontFaceName);
        if (!Global::defaultlFontHandle)
        {
			LOGFONT lf;
			SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
            // Fall back to the default GUI font on failure.
            Global::defaultlFontHandle = CreateFont(-MulDiv(_fontSize, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72), 0, 0, 0, FW_MEDIUM, 0, 0, 0, CHINESEBIG5_CHARSET, 0, 0, 0, 0, lf.lfFaceName);
        }
    }
}


void CConfig::SetReverseConvervsionInfoList (CDIMEArray <LanguageProfileInfo> *reverseConvervsionInfoList)
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
		PWCH _description = new (std::nothrow) WCHAR[wcslen(infoItem->description) +1];
		*_description = L'\0';
		StringCchCopy(_description, wcslen(infoItem->description) +1, infoItem->description);
		_infoItem->description = _description;
	}
}

void CConfig::clearReverseConvervsionInfoList()
{
	for (UINT index = 0; index < _reverseConvervsionInfoList->Count(); index++)
	{
		LanguageProfileInfo *infoItem = nullptr;
		infoItem = _reverseConvervsionInfoList->GetAt(index);
		delete [] infoItem->description;
	}
	_reverseConvervsionInfoList->Clear();
}