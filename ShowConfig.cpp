
#include "Globals.h"
#include "DIME.h"
#include "TfInputProcessorProfile.h"


//+---------------------------------------------------------------------------
//
// ITfFnConfigure::Show
//
//----------------------------------------------------------------------------
// static dialog procedure

HRESULT CDIME::Show(_In_ HWND hwndParent, _In_ LANGID inLangid, _In_ REFGUID inRefGuidProfile)
{
	debugPrint(L"CDIME::Show()");

	if (_IsStoreAppMode() || _IsUILessMode() || _IsSecureMode()) return S_OK;

	CTfInputProcessorProfile* profile = new CTfInputProcessorProfile();
	LANGID langid = inLangid;
	GUID guidProfile = inRefGuidProfile;
	// called from hotkey in IME
	if (guidProfile == GUID_NULL)
		CConfig::SetIMEMode(Global::imeMode);
	else if (guidProfile == Global::DIMEDayiGuidProfile)
		CConfig::SetIMEMode(IME_MODE_DAYI);
	else if (guidProfile == Global::DIMEPhoneticGuidProfile)
		CConfig::SetIMEMode(IME_MODE_PHONETIC);
	else if (guidProfile == Global::DIMEArrayGuidProfile)
		CConfig::SetIMEMode(IME_MODE_ARRAY);
	else if (guidProfile == Global::DIMEGenericGuidProfile)
		CConfig::SetIMEMode(IME_MODE_GENERIC);

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

	_LoadConfig(FALSE,CConfig::GetIMEMode());  // prevent set _imeMode being set if config triggered by control panel options button

	if (_IsComposing() && _pContext) _EndComposition(_pContext);
	_DeleteCandidateList(TRUE, _pContext);



	// comctl32.dll and comdlg32.dll can't be loaded into immersivemode (app container), thus use late binding here.
	HINSTANCE dllCtlHandle = NULL;
	dllCtlHandle = LoadLibrary(L"comctl32.dll");

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
	HPROPSHEETPAGE hpsp[_countof(DlgPage)];
	int i;

	ZeroMemory(&psp, sizeof(PROPSHEETPAGE));
	psp.dwSize = sizeof(PROPSHEETPAGE);
	psp.dwFlags = PSP_PREMATURE;
	psp.hInstance = Global::dllInstanceHandle;

	for (i = 0; i<_countof(DlgPage); i++)
	{
		psp.pszTemplate = MAKEINTRESOURCE(DlgPage[i].id);
		psp.pfnDlgProc = DlgPage[i].DlgProc;
		if (_CreatePropertySheetPage)
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

	if (CConfig::GetIMEMode() == IME_MODE_DAYI)
		psh.pszCaption = L"DIME 大易輸入法設定";
	else if (CConfig::GetIMEMode() == IME_MODE_ARRAY)
		psh.pszCaption = L"DIME 行列輸入法設定";
	else if (CConfig::GetIMEMode() == IME_MODE_GENERIC)
		psh.pszCaption = L"DIME 自建輸入法設定";
	else if (CConfig::GetIMEMode() == IME_MODE_PHONETIC)
		psh.pszCaption = L"DIME 傳統注音輸入法設定";


	//PropertySheet(&psh);
	if (_PropertySheet)
		(*_PropertySheet)(&psh);

	FreeLibrary(dllCtlHandle);



	return S_OK;
}