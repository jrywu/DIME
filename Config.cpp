//#define DEBUG_PRINT

#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Globals.h"
#include "resource.h"
#include "TSFTTS.h"
#include "DictionarySearch.h"
#include "FileMapping.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"

//static configuration settings initilization
BOOL CConfig::_doBeep = TRUE;
BOOL CConfig::_autoCompose = FALSE;
BOOL CConfig::_threeCodeMode = FALSE;
BOOL CConfig::_arrowKeySWPages = TRUE;
BOOL CConfig::_spaceAsPageDown = FALSE;
UINT CConfig::_fontSize = 14;
UINT CConfig::_fontWeight = FW_NORMAL;
BOOL CConfig::_fontItalic = FALSE;
UINT CConfig::_maxCodes = 4;
BOOL CConfig::_appPermissionSet = FALSE;
BOOL CConfig::_activatedKeyboardMode = TRUE;
BOOL CConfig::_makePhrase = TRUE;
BOOL CConfig::_showNotifyDesktop = TRUE;
WCHAR* CConfig::_pFontFaceName = L"Microsoft JhengHei";
COLORREF CConfig::_itemColor = CANDWND_ITEM_COLOR;
COLORREF CConfig::_itemBGColor = GetSysColor(COLOR_3DHIGHLIGHT);
COLORREF CConfig::_selectedColor = CANDWND_SELECTED_ITEM_COLOR;
COLORREF CConfig::_selectedBGColor = CANDWND_SELECTED_BK_COLOR;
COLORREF CConfig::_phraseColor = CANDWND_PHRASE_COLOR;
COLORREF CConfig::_numberColor = CANDWND_NUM_COLOR;

_stat CConfig::_initTimeStamp = {0,0,0,0,0,0,0,0,0,0,0}; //zero the timestamp


static struct {
	int id;
	COLORREF color;
} colors[6] = {
	{IDC_COL_FR,  CANDWND_ITEM_COLOR},
	{IDC_COL_SEFR, CANDWND_SELECTED_ITEM_COLOR},
	{IDC_COL_BG,   GetSysColor(COLOR_3DHIGHLIGHT)},
	{IDC_COL_PHRASE, CANDWND_PHRASE_COLOR},
	{IDC_COL_NU, CANDWND_NUM_COLOR},
	{IDC_COL_SEBG, CANDWND_SELECTED_BK_COLOR}
};

typedef BOOL (__stdcall * _T_ChooseColor)(_Inout_  LPCHOOSECOLOR lpcc);
typedef BOOL (__stdcall * _T_ChooseFont)(_Inout_  LPCHOOSEFONT lpcf);

INT_PTR CALLBACK CConfig::CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	HWND hwnd;
	size_t i;
	WCHAR num[16];
	WCHAR fontname[LF_FACESIZE];
	int fontpoint =14, fontweight = FW_NORMAL, x, y;
	BOOL fontitalic = FALSE;
	CHOOSEFONT cf;
	LOGFONT lf;
	HDC hdc;
	HFONT hFont;
	RECT rect;
	POINT pt;
	LONG w=0;
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
			fontpoint = 14;
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

		SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
		if(w < 0 || w > rect.right)
		{
			w = rect.right;
		}
		_snwprintf_s(num, _TRUNCATE, L"%d", w);
		SetDlgItemText(hDlg, IDC_EDIT_MAXWIDTH, num);

		ZeroMemory(&colCust, sizeof(colCust));

		colors[0].color = _itemColor;
		colors[1].color = _selectedColor;
		colors[2].color = _itemBGColor;
		colors[3].color = _phraseColor;
		colors[4].color = _numberColor;
		colors[5].color = _selectedBGColor;

		hwnd = GetDlgItem(hDlg, IDC_COMBO_UNTILCANDLIST);
		num[1] = L'\0';
		for(i=0; i<=8; i++)
		{
			num[0] = L'0' + (WCHAR)i;
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)num);
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)i, 0);

		_snwprintf_s(num, _TRUNCATE, L"%d", _maxCodes);
		SetDlgItemTextW(hDlg, IDC_EDIT_MAXWIDTH, num);
		CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWNOTIFY, (_showNotifyDesktop)?BST_CHECKED:BST_UNCHECKED);

		CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, (_autoCompose)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, (_doBeep)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_THREECODEMODE,(_threeCodeMode)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, (_makePhrase)?BST_CHECKED:BST_UNCHECKED);	
		
		CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_OPEN, (_activatedKeyboardMode)?BST_CHECKED:BST_UNCHECKED);
		if(!IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN))
		{
			CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_CLOSE, BST_CHECKED);
		}
		CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, (_spaceAsPageDown)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES, (_arrowKeySWPages)?BST_CHECKED:BST_UNCHECKED);
		// hide autocompose and space as pagedown option in DAYI.
			
		if(Global::imeMode==IME_MODE_DAYI)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN), SW_HIDE);
		}
		if(Global::imeMode!=IME_MODE_DAYI)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_THREECODEMODE), SW_HIDE);
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

		case IDC_COMBO_UNTILCANDLIST:
			switch(HIWORD(wParam))
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
		case IDC_CHECKBOX_DOBEEP:
		case IDC_RADIO_KEYBOARD_OPEN:
		case IDC_RADIO_KEYBOARD_CLOSE:
		case IDC_CHECKBOX_THREECODEMODE:
		case IDC_CHECKBOX_PHRASE:
		case IDC_CHECKBOX_ARROWKEYSWPAGES:
		case IDC_CHECKBOX_SPACEASPAGEDOWN:
		case IDC_CHECKBOX_SHOWNOTIFY:
			PropSheet_Changed(GetParent(hDlg), hDlg);
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
				
				//if(ChooseColor(&cc))
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
			_doBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP) == BST_CHECKED;
			_makePhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_PHRASE) == BST_CHECKED;
			_activatedKeyboardMode = IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN) == BST_CHECKED;
			_showNotifyDesktop = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SHOWNOTIFY) == BST_CHECKED;
			_spaceAsPageDown = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN) == BST_CHECKED;
			_arrowKeySWPages = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES) == BST_CHECKED;



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
			_pFontFaceName = pwszFontFaceName;
				
			_itemColor = colors[0].color ;
			_selectedColor = colors[1].color;
			_itemBGColor = colors[2].color;
			_phraseColor = colors[3].color;
			_numberColor = colors[4].color;
			_selectedBGColor = colors[5].color;

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
	debugPrint(L"CTSFTTS::updateConfig() \n");
	WCHAR wszAppData[MAX_PATH] = {'\0'};
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);	
	WCHAR wzsTSFTTSProfile[MAX_PATH] = {'\0'};
	
	WCHAR *pwszINIFileName = new (std::nothrow) WCHAR[MAX_PATH];
    
	if (!pwszINIFileName)  goto ErrorExit;

	*pwszINIFileName = L'\0';

	StringCchPrintf(wzsTSFTTSProfile, MAX_PATH, L"%s\\TSFTTS", wszAppData);
	if(!PathFileExists(wzsTSFTTSProfile))
	{
		if(CreateDirectory(wzsTSFTTSProfile, NULL)==0) goto ErrorExit;
	}
	else
	{
		if(Global::imeMode == IME_MODE_DAYI)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", wzsTSFTTSProfile);
		else if(Global::imeMode == IME_MODE_ARRAY)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", wzsTSFTTSProfile);
		else
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsTSFTTSProfile);

		FILE *fp;
		_wfopen_s(&fp, pwszINIFileName, L"w, ccs=UTF-16LE"); // overwrite the file
	if(fp)
	{
		fwprintf_s(fp, L"[Config]\n");
		fwprintf_s(fp, L"AutoCompose = %d\n", _autoCompose?1:0);
		fwprintf_s(fp, L"ThreeCodeMode = %d\n", _threeCodeMode?1:0);
		fwprintf_s(fp, L"SpaceAsPageDown = %d\n", _spaceAsPageDown?1:0);
		fwprintf_s(fp, L"ArrowKeySWPages = %d\n", _arrowKeySWPages?1:0);
		fwprintf_s(fp, L"DoBeep = %d\n", _doBeep?1:0);
		fwprintf_s(fp, L"ActivatedKeyboardMode = %d\n", _activatedKeyboardMode?1:0);
		fwprintf_s(fp, L"MakePhrase = %d\n", _makePhrase?1:0);
		fwprintf_s(fp, L"MaxCodes = %d\n", _maxCodes);
		fwprintf_s(fp, L"ShowNotifyDesktop = %d\n", _showNotifyDesktop?1:0);
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
		if(Global::isWindows8)
			fwprintf_s(fp, L"AppPermissionSet = %d\n", _appPermissionSet?1:0);

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
	debugPrint(L"CTSFTTS::loadConfig() \n");
	WCHAR wszAppData[MAX_PATH] = {'\0'};
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);	
	WCHAR wzsTSFTTSProfile[MAX_PATH] = {'\0'};
	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;

	WCHAR *pwszINIFileName = new (std::nothrow) WCHAR[MAX_PATH];
    
	if (!pwszINIFileName)  goto ErrorExit;

	*pwszINIFileName = L'\0';

	StringCchPrintf(wzsTSFTTSProfile, MAX_PATH, L"%s\\TSFTTS", wszAppData);
	if(PathFileExists(wzsTSFTTSProfile))
	{ 
		
		if(Global::imeMode == IME_MODE_DAYI)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", wzsTSFTTSProfile);
		else if(Global::imeMode == IME_MODE_ARRAY)
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", wzsTSFTTSProfile);
		else
			StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsTSFTTSProfile);

		if(PathFileExists(pwszINIFileName))
		{
			struct _stat initTimeStamp;
			if (_wstat(pwszINIFileName, &initTimeStamp) || //error for retrieving timestamp
				(long(initTimeStamp.st_mtime>>32) != long(_initTimeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
				(long(initTimeStamp.st_mtime) != long(_initTimeStamp.st_mtime)) )			// then load the config. skip otherwise
			{
				_initTimeStamp.st_mtime = initTimeStamp.st_mtime;

				CFileMapping *iniDictionaryFile;
				iniDictionaryFile = new (std::nothrow) CFileMapping();
				if ((iniDictionaryFile)->CreateFile(pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
				{
					CTableDictionaryEngine * iniTableDictionaryEngine;
					iniTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), iniDictionaryFile,L'=');//CHT:1028
					if (iniTableDictionaryEngine)
					{
						iniTableDictionaryEngine->ParseConfig(); //parse config first.
					}
					delete iniTableDictionaryEngine; // delete after config.ini config are pasrsed
					delete iniDictionaryFile;
					SetDefaultTextFont();
				}

				// In store app mode, the dll is loaded into app container which does not even have read right for IME profile in APPDATA.
				// Here, the read right is granted once to "ALL APPLICATION PACKAGES" when loaded in desktop mode for all metro apps can at least read the user settings in config.ini.
				if(Global::isWindows8 && ! CTSFTTS::_IsStoreAppMode() && ! _appPermissionSet ) 
				{
					EXPLICIT_ACCESS ea;
					// Get a pointer to the existing DACL (Conditionaly).
					DWORD dwRes = GetNamedSecurityInfo(wzsTSFTTSProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
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
						SetNamedSecurityInfo(wzsTSFTTSProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);

					_appPermissionSet = TRUE;
					WriteConfig(); // update the config file.

				}

			}
			
		}
		else
		{
			WriteConfig(); // config.ini is not there. create one.
		}
	}
	else
	{
		//TSFTTS roadming profile is not exist. Create one.
		if(CreateDirectory(wzsTSFTTSProfile, NULL)==0) goto ErrorExit;
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
