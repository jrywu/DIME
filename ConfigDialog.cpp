#define DEBUG_PRINT

#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Globals.h"
#include "Private.h"
#include "resource.h"
#include "TSFTTS.h"
#include "DictionarySearch.h"
#include "FileMapping.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"

//static configuration settings initilization
BOOL CTSFTTS::_doBeep = FALSE;
BOOL CTSFTTS::_autoCompose = FALSE;
BOOL CTSFTTS::_threeCodeMode = FALSE;
UINT CTSFTTS::_fontSize = 14;
UINT CTSFTTS::_maxCodes = 4;
BOOL CTSFTTS::_appPermissionSet = FALSE;



static struct {
	int id;
	COLORREF color;
} colors[8] = {
	{IDC_COL_BG,  RGB(0xFF,0xFF,0xFF)},
	{IDC_COL_FR,  RGB(0x00,0x00,0x00)},
	{IDC_COL_SE,  RGB(0x00,0x00,0xFF)},
	{IDC_COL_CO,  RGB(0x80,0x80,0x80)},
	{IDC_COL_CA,  RGB(0x00,0x00,0x00)},
	{IDC_COL_SC,  RGB(0x80,0x80,0x80)},
	{IDC_COL_AN,  RGB(0x80,0x80,0x80)},
	{IDC_COL_NO,  RGB(0x00,0x00,0x00)}
};


INT_PTR CALLBACK CTSFTTS::CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	
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


	CHOOSECOLORW cc;
	static COLORREF colCust[16];
	PAINTSTRUCT ps;

	switch(message)
	{
	case WM_INITDIALOG:
		
		wcsncpy_s(fontname, L"Microsoft JhengHei" , _TRUNCATE);

		fontpoint = _fontSize;

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
		hFont = CreateFontW(-MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0,
			fontweight, fontitalic, FALSE, FALSE, SHIFTJIS_CHARSET,
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

		for(i=0; i<_countof(colors); i++)
		{
		
		}

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
		CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, (_autoCompose)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, (_doBeep)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_THREECODEMODE,(_threeCodeMode)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, (_autoCompose)?BST_CHECKED:BST_UNCHECKED);
		
		
		CheckDlgButton(hDlg, IDC_RADIO_ANNOTATLST, BST_CHECKED);
		if(!IsDlgButtonChecked(hDlg, IDC_RADIO_ANNOTATLST))
		{
			CheckDlgButton(hDlg, IDC_RADIO_ANNOTATALL, BST_CHECKED);
		}
		CheckDlgButton(hDlg, IDC_CHECKBOX_DELOKURICNCL, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_BACKINCENTER, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_ADDCANDKTKN, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWMODEIMM, BST_CHECKED);
		return TRUE;

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

			if(ChooseFont(&cf) == TRUE)
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);

				SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, lf.lfFaceName);
				lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
				hFont = CreateFontIndirect(&lf);
				SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
				SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, cf.iPointSize / 10, FALSE);
			}

			ReleaseDC(hDlg, hdc);
			return TRUE;

		case IDC_EDIT_MAXWIDTH:
			switch(HIWORD(wParam))
			{
			case EN_CHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				return TRUE;
			default:
				break;
			}
			break;

		case IDC_COMBO_UNTILCANDLIST:
			switch(HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				return TRUE;
			default:
				break;
			}
			break;

		case IDC_CHECKBOX_AUTOCOMPOSE:
		case IDC_CHECKBOX_DOBEEP:
		case IDC_RADIO_ANNOTATALL:
		case IDC_RADIO_ANNOTATLST:
		case IDC_CHECKBOX_THREECODEMODE:
		case IDC_CHECKBOX_PHRASE:
		case IDC_CHECKBOX_DELOKURICNCL:
		case IDC_CHECKBOX_BACKINCENTER:
		case IDC_CHECKBOX_ADDCANDKTKN:
		case IDC_CHECKBOX_SHOWMODEIMM:
			PropSheet_Changed(GetParent(hDlg), hDlg);
			return TRUE;

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
				if(ChooseColorW(&cc))
				{
					hdc = GetDC(hDlg);
					DrawColor(hwnd, hdc, cc.rgbResult);
					ReleaseDC(hDlg, hdc);
					colors[i].color = cc.rgbResult;
					PropSheet_Changed(GetParent(hDlg), hDlg);
					return TRUE;
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
		return TRUE;
		break;

	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:	
			_autoCompose = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED;
			_threeCodeMode = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_THREECODEMODE) == BST_CHECKED;
			_doBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP) == BST_CHECKED;
			GetDlgItemTextW(hDlg, IDC_EDIT_MAXWIDTH, num, _countof(num));
			_maxCodes = _wtol(num);
			GetDlgItemTextW(hDlg, IDC_EDIT_FONTPOINT, num, _countof(num));
			_fontSize = _wtol(num);
			WriteConfig();
			return TRUE;

		default:
			break;
		}
		break;

	default:
		break;
	}
	
	return FALSE;
	
	
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

VOID CTSFTTS::WriteConfig()
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
		StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsTSFTTSProfile);
		FILE *fp;
		_wfopen_s(&fp, pwszINIFileName, L"w, ccs=UTF-16LE"); // overwrite the file
	if(fp)
	{
		fwprintf_s(fp, L"[Config]\n");
		fwprintf_s(fp, L"AutoCompose = %d\n", _autoCompose?1:0);
		fwprintf_s(fp, L"ThreeCodeMode = %d\n", _threeCodeMode?1:0);
		fwprintf_s(fp, L"DoBeep = %d\n", _doBeep?1:0);
		fwprintf_s(fp, L"MaxCodes = %d\n", _maxCodes);
		fwprintf_s(fp, L"FontSize = %d\n", _fontSize);
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

VOID CTSFTTS::LoadConfig()
{	
	debugPrint(L"CTSFTTS::loadConfig() \n");
	WCHAR wszAppData[MAX_PATH] = {'\0'};
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);	
	WCHAR wzsTSFTTSProfile[MAX_PATH] = {'\0'};//L"\\TSFTTS";
	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;

	WCHAR *pwszINIFileName = new (std::nothrow) WCHAR[MAX_PATH];
    
	if (!pwszINIFileName)  goto ErrorExit;

	*pwszINIFileName = L'\0';

	StringCchPrintf(wzsTSFTTSProfile, MAX_PATH, L"%s\\TSFTTS", wszAppData);
	if(PathFileExists(wzsTSFTTSProfile))
	{ 
		StringCchPrintf(pwszINIFileName, MAX_PATH, L"%s\\config.ini", wzsTSFTTSProfile);
		if(PathFileExists(pwszINIFileName))
		{
			CFileMapping *iniDictionaryFile;
			iniDictionaryFile = new (std::nothrow) CFileMapping();
			if ((iniDictionaryFile)->CreateFile(pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				CTableDictionaryEngine * iniTableDictionaryEngine;
				iniTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(GetLocale(), iniDictionaryFile,L'=', this);
				if (iniTableDictionaryEngine)
				{
					debugPrint(L"CTSFTTS::loadConfig() config.ini found. parse config now. \n");
					iniTableDictionaryEngine->ParseConfig(); //parse config first.
					debugPrint(L"CTSFTTS::loadConfig() , _autoCompose = %d, _threeCodeMode = %d, _doBeep = %d", _autoCompose, _threeCodeMode, _doBeep);
				}
				delete iniTableDictionaryEngine; // delete after config.ini config are pasrsed
				delete iniDictionaryFile;
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

	// In store app mode, the dll is loaded into app container which does not even have read right for IME profile in APPDATA.
	// Here, the read right is granted once to "ALL APPLICATION PACKAGES" when loaded in desktop mode for all metro apps can at least read the user settings in config.ini.
	if(Global::isWindows8 && !_IsStoreAppMode() && ! _appPermissionSet ) 
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
ErrorExit:
	if(pNewDACL != NULL) 
		LocalFree(pNewDACL);
	if(pSD != NULL)
		LocalFree(pSD);
    delete []pwszINIFileName;

}
