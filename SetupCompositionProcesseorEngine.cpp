//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT
#include <Shlobj.h>
#include <Shlwapi.h>
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "Globals.h"

void CCompositionProcessorEngine::UpdateDictionaryFile()
{
	debugPrint(L"CCompositionProcessorEngine::UpdateDictionaryFile()");

	CFile* pCurrentDictioanryFile = _pTableDictionaryFile[Global::imeMode];
	
	SetupDictionaryFile(_imeMode);
	if (pCurrentDictioanryFile != _pTableDictionaryFile[Global::imeMode])
	{ // the table is updated.

		debugPrint(L"CCompositionProcessorEngine::UpdateDictionaryFile() the table is loaded from TTS previously and now new cin is loaded");
		SetupKeystroke(Global::imeMode);
		SetupConfiguration(Global::imeMode);
		SetupCandidateListRange(Global::imeMode);

	}

}
void CCompositionProcessorEngine::ReleaseDictionaryFiles()
{

	for (UINT i = 0; i < IM_SLOTS; i++)
	{
		if (_pTableDictionaryFile[i])
		{
			delete _pTableDictionaryFile[i];
			_pTableDictionaryFile[i] = nullptr;
		}
		if (_pCustomTableDictionaryFile[i])
		{
			delete _pCustomTableDictionaryFile[i];
			_pCustomTableDictionaryFile[i] = nullptr;
		}
		if (_pTableDictionaryEngine[i])
		{   // _pTableDictionaryEngine[i] is only a pointer to either _pTTSDictionaryFile[i] or _pCINTableDictionaryEngine. no need to delete it.
			_pTableDictionaryEngine[i] = nullptr;
		}
		if (_pCustomTableDictionaryEngine[i])
		{
			_pCustomTableDictionaryEngine[i] = nullptr;
		}

	}
	// Array short-code
	if (_pArrayShortCodeDictionaryFile)
	{
		delete _pArrayShortCodeDictionaryFile;
		_pArrayShortCodeDictionaryFile = nullptr;
	}
	if (_pArrayShortCodeTableDictionaryEngine)
	{
		delete _pArrayShortCodeTableDictionaryEngine;
		_pArrayShortCodeTableDictionaryEngine = nullptr;
	}

	// Array special-code
	if (_pArraySpecialCodeDictionaryFile)
	{
		delete _pArraySpecialCodeDictionaryFile;
		_pArraySpecialCodeDictionaryFile = nullptr;
	}
	if (_pArraySpecialCodeTableDictionaryEngine)
	{
		delete _pArraySpecialCodeTableDictionaryEngine;
		_pArraySpecialCodeTableDictionaryEngine = nullptr;
	}
	// Array ext-B
	if (_pArrayExtBDictionaryFile)
	{
		delete _pArrayExtBDictionaryFile;
		_pArrayExtBDictionaryFile = nullptr;
	}
	if (_pArrayExtBTableDictionaryEngine)
	{
		delete _pArrayExtBTableDictionaryEngine;
		_pArrayExtBTableDictionaryEngine = nullptr;
	}
	// Array ext-CD
	if (_pArrayExtCDDictionaryFile)
	{
		delete _pArrayExtCDDictionaryFile;
		_pArrayExtCDDictionaryFile = nullptr;
	}
	if (_pArrayExtETableDictionaryEngine)
	{
		delete _pArrayExtETableDictionaryEngine;
		_pArrayExtETableDictionaryEngine = nullptr;
	}



	// TC-SC dictinary
	if (_pTCSCTableDictionaryEngine)
	{
		delete _pTCSCTableDictionaryEngine;
		_pTCSCTableDictionaryEngine = nullptr;
	}
	if (_pTCSCTableDictionaryFile)
	{
		delete _pTCSCTableDictionaryFile;
		_pTCSCTableDictionaryFile = nullptr;
	}
	if (_pTCFreqTableDictionaryEngine)
	{
		delete _pTCFreqTableDictionaryEngine;
		_pTCFreqTableDictionaryEngine = nullptr;
	}
	if (_pTCFreqTableDictionaryFile)
	{
		delete _pTCFreqTableDictionaryFile;
		_pTCFreqTableDictionaryFile = nullptr;
	}




}



//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke(IME_MODE imeMode)
{
	if (!IsDictionaryAvailable(imeMode))
	{
		return;
	}
	if (_pTableDictionaryEngine[imeMode] == nullptr || _pTableDictionaryEngine[imeMode]->GetRadicalMap() == nullptr ||
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() == 0 || _pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() > MAX_RADICAL) return;

	_KeystrokeComposition.Clear();

	for (int i = 0; i < MAX_RADICAL + 1; i++)
	{
		_KEYSTROKE* pKS = nullptr;
		pKS = _KeystrokeComposition.Append();
		pKS->Function = FUNCTION_NONE;
		pKS->Modifiers = 0;
		pKS->Index = i;
		pKS->Printable = 0;
		pKS->VirtualKey = 0;
	}

	if (imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[imeMode]->GetRadicalMap() &&
		(_pTableDictionaryEngine[imeMode]->GetRadicalMap()->find('=') == _pTableDictionaryEngine[imeMode]->GetRadicalMap()->end()))
	{ //dayi symbol prompt
		WCHAR* pwchEqual = new (std::nothrow) WCHAR[2];
		if (pwchEqual)
		{
			pwchEqual[0] = L'=';
			pwchEqual[1] = L'\0';
			(*_pTableDictionaryEngine[imeMode]->GetRadicalMap())['='] = pwchEqual;
		}
	}

	for (_T_RadicalMap::iterator item = _pTableDictionaryEngine[imeMode]->GetRadicalMap()->begin(); item !=
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->end(); ++item)
	{
		_KEYSTROKE* pKS = nullptr;

		WCHAR c = towupper(item->first);
		if (c < 32 || c > MAX_RADICAL + 32) continue;
		pKS = _KeystrokeComposition.GetAt(c - 32);
		if (pKS == nullptr)
			break;

		pKS->Function = FUNCTION_INPUT;
		UINT vKey, modifier;
		WCHAR key = item->first;
		pKS->Printable = key;
		GetVKeyFromPrintable(key, &vKey, &modifier);
		pKS->VirtualKey = vKey;
		pKS->Modifiers = modifier;
	}

	return;
}

void CCompositionProcessorEngine::GetVKeyFromPrintable(WCHAR printable, UINT* vKey, UINT* modifier)
{
	/*
	#define VK_OEM_1          0xBA   // ';:' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_COMMA      0xBC   // ',' any country
	#define VK_OEM_MINUS      0xBD   // '-' any country
	#define VK_OEM_PERIOD     0xBE   // '.' any country
	#define VK_OEM_2          0xBF   // '/?' for US
	#define VK_OEM_3          0xC0   // '`/~' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_4          0xDB  //  '[{' for US
	#define VK_OEM_5          0xDC  //  '\|' for US
	#define VK_OEM_6          0xDD  //  ']}' for US
	#define VK_OEM_7          0xDE  //  ''"' for US
	*/
	* modifier = 0;

	if ((printable >= '0' && printable <= '9') || (printable >= 'A' && printable <= 'Z'))
	{
		*vKey = printable;
	}
	else if (printable == '!')
	{
		*vKey = '1';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '@')
	{
		*vKey = '2';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '#')
	{
		*vKey = '3';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '$')
	{
		*vKey = '4';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '%')
	{
		*vKey = '5';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '^')
	{
		*vKey = '6';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '&')
	{
		*vKey = '7';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '*')
	{
		*vKey = '8';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '(')
	{
		*vKey = '9';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ')')
	{
		*vKey = '0';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ',')
		*vKey = VK_OEM_COMMA;
	else if (printable == '<')
	{
		*vKey = VK_OEM_COMMA;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '.')
		*vKey = VK_OEM_PERIOD;
	else if (printable == '>')
	{
		*vKey = VK_OEM_PERIOD;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ';')
		*vKey = VK_OEM_1;
	else if (printable == ':')
	{
		*vKey = VK_OEM_1;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '/')
		*vKey = VK_OEM_2;
	else if (printable == '?')
	{
		*vKey = VK_OEM_2;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '`')
		*vKey = VK_OEM_3;
	else if (printable == '~')
	{
		*vKey = VK_OEM_3;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '[')
		*vKey = VK_OEM_4;
	else if (printable == '{')
	{
		*vKey = VK_OEM_4;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\\')
		*vKey = VK_OEM_5;
	else if (printable == '|')
	{
		*vKey = VK_OEM_5;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ']')
		*vKey = VK_OEM_6;
	else if (printable == '}')
	{
		*vKey = VK_OEM_6;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\'')
		*vKey = VK_OEM_7;
	else if (printable == '"')
	{
		*vKey = VK_OEM_7;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '-')
		*vKey = VK_OEM_MINUS;
	else if (printable == '_')
	{
		*vKey = VK_OEM_MINUS;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '=')
		*vKey = VK_OEM_PLUS;
	else if (printable == '+')
	{
		*vKey = VK_OEM_PLUS;
		*modifier = TF_MOD_SHIFT;
	}
	else
	{
		*vKey = printable;
	}

}

//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	TF_PRESERVEDKEY preservedKeyImeMode;
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
	SetPreservedKey(Global::DIMEGuidImeModePreserveKey, preservedKeyImeMode, Global::ImeModeDescription, &_PreservedKey_IMEMode);

	TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
	preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
	preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
	SetPreservedKey(Global::DIMEGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);

	TF_PRESERVEDKEY preservedKeyConfig;
	preservedKeyConfig.uVKey = VK_OEM_5; // '\\'
	preservedKeyConfig.uModifiers = TF_MOD_CONTROL;
	SetPreservedKey(Global::DIMEGuidConfigPreserveKey, preservedKeyConfig, L"Show Config Pages", &_PreservedKey_Config);


	InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_Config, pThreadMgr, tfClientId);

	return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY& tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey* pXPreservedKey)
{
	if (pXPreservedKey == nullptr) return;
	pXPreservedKey->Guid = clsid;

	TF_PRESERVEDKEY* ptfPskey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
	if (!ptfPskey1)
	{
		return;
	}
	*ptfPskey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
	{
		return;
	}
	pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
	if (!pXPreservedKey->Description)
	{
		return;
	}

	StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

	return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey* pXPreservedKey, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	if (pThreadMgr == nullptr || pXPreservedKey == nullptr) return FALSE;
	ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

	if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK || pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
		preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
		{
			return FALSE;
		}
		pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY>* pTSFPreservedKeyTable)
{
	if (pTSFPreservedKeyTable == nullptr) return FALSE;
	for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
	{
		TF_PRESERVEDKEY* ptfPskey = pTSFPreservedKeyTable->GetAt(i);

		if (((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly))
		{
			return FALSE;
		}
	}

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL* pIsEaten, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	debugPrint(L"CCompositionProcessorEngine::OnPreservedKey() \n");
	if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid))
	{
		IME_SHIFT_MODE imeShiftMode = CConfig::GetIMEShiftMode();

		if (imeShiftMode == IME_NO_SHIFT ||
			!CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable) ||
			(imeShiftMode == IME_RIGHT_SHIFT_ONLY && (Global::ModifiersValue & (TF_MOD_LSHIFT))) ||
			(imeShiftMode == IME_LEFT_SHIFT_ONLY && (Global::ModifiersValue & (TF_MOD_RSHIFT)))
			)
		{
			*pIsEaten = FALSE;
			return;
		}
		BOOL isOpen = FALSE;


		if (Global::isWindows8) {
			isOpen = FALSE;
			CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
			CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}
		else
		{
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentIMEMode);
			CompartmentIMEMode._GetCompartmentBOOL(isOpen);
			CompartmentIMEMode._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}

		//show notify
		if (_pTextService) _pTextService->showChnEngNotify(!isOpen);

		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
	{
		if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
			//|| !CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
		{
			*pIsEaten = FALSE;
			return;
		}
		BOOL isDouble = FALSE;
		CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
		CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
		CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
		//show notify
		if (_pTextService) _pTextService->showFullHalfShapeNotify(!isDouble);
		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_Config.Guid) && _pTextService)
	{
		// call config dialog
		_pTextService->Show(GetForegroundWindow(), 0, GUID_NULL);
	}

	else
	{
		*pIsEaten = FALSE;
	}
	*pIsEaten = TRUE;
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupConfiguration() \n");
	_isWildcard = TRUE;
	_isDisableWildcardAtFirst = FALSE;
	_isKeystrokeSort = FALSE;
	_isWildCardWordFreqSort = TRUE;

	if (imeMode == IME_MODE_DAYI)
	{
		CConfig::SetAutoCompose(FALSE);
	}
	else if (imeMode == IME_MODE_ARRAY)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
		CConfig::SetAutoCompose(TRUE);
	}
	else if (imeMode == IME_MODE_PHONETIC)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
	}



	return;
}


//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() \n");
	BOOL bRet = FALSE;

	WCHAR pwszProgramFiles[MAX_PATH];
	WCHAR pwszAppData[MAX_PATH];

	if (GetEnvironmentVariable(L"ProgramW6432", pwszProgramFiles, MAX_PATH) == 0)
	{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running in 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", pwszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roadming application data.
	if (pwszAppData)
		SHGetSpecialFolderPath(NULL, pwszAppData, CSIDL_APPDATA, TRUE);

	WCHAR* pwszTTSFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR* pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR* pwszCINFileNameProgramFiles = new (std::nothrow) WCHAR[MAX_PATH];

	struct _stat programFilesTimeStamp = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, appDataTimeStamp = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	if (!pwszTTSFileName)  goto ErrorExit;
	if (!pwszCINFileName)  goto ErrorExit;
	if (!pwszCINFileNameProgramFiles)  goto ErrorExit;

	*pwszTTSFileName = L'\0';
	*pwszCINFileName = L'\0';
	*pwszCINFileNameProgramFiles = L'\0';

	//tableTextService (TTS) dictionary file 
	if (imeMode != IME_MODE_ARRAY)
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\TableTextServiceDaYi.txt");
	else
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\TableTextServiceArray.txt");

	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME");

	if (!PathFileExists(pwszCINFileName))
	{
		//DIME roadming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);
	}
	// load main table file now
	if (imeMode == IME_MODE_DAYI) //dayi.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Dayi.cin");
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE_ARRAY) //array.cin in personal romaing profile
	{
		BOOL cinFound = FALSE, wstatFailed = TRUE, updated = TRUE;

		if (CConfig::GetArrayScope() == ARRAY40_BIG5)
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array40.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array40.cin");
		}
		else
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array.cin");
		}
		if (PathFileExists(pwszCINFileNameProgramFiles))
			_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
		if (PathFileExists(pwszCINFileName))
			wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
		updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
		if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
			CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

		if (!PathFileExists(pwszCINFileName)) //failed back to try preload array.cin in program files.
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, 
				(CConfig::GetArrayScope() == ARRAY40_BIG5)?L"\\DIME\\Array40.cin":L"\\DIME\\Array.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Array.cin in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}

		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE_PHONETIC) //phone.cin in personal romaing profile
	{
		BOOL cinFound = FALSE, wstatFailed = TRUE, updated = TRUE;
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Phone.cin");
		StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Phone.cin");

		if (PathFileExists(pwszCINFileNameProgramFiles))
			_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
		if (PathFileExists(pwszCINFileName))
			wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
		updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
		if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
			CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

		if (!PathFileExists(pwszCINFileName)) //failed back to pre-install Phone.cin in program files.
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Phone.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Array.in in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE_GENERIC) //phone.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Generic.cin");
		// we don't provide preload Generic.cin in program files
	}

	if (PathFileExists(pwszCINFileName))  //create cin CFile object
	{
		if (_pTableDictionaryFile[imeMode] == nullptr)
		{
			//create CFile object
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if ((_pTableDictionaryFile[imeMode]) && _pTextService &&
				(_pTableDictionaryFile[imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pTableDictionaryEngine[imeMode])
					delete _pTableDictionaryEngine[imeMode];
				_pTableDictionaryEngine[imeMode] = //cin files use tab as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], CIN_DICTIONARY);
				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.		

			}
			else
			{ // error on createfile. do cleanup
				delete _pTableDictionaryFile[imeMode];
				_pTableDictionaryFile[imeMode] = nullptr;
				goto ErrorExit;
			}
		}
		else if (_pTableDictionaryEngine[imeMode] && _pTableDictionaryFile[imeMode]->IsFileUpdated())
		{
			_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config to reload updated dictionary
			// Reload Keystroke and CandiateListRage when table is updated
			SetupKeystroke(Global::imeMode);
			SetupConfiguration(Global::imeMode);
			SetupCandidateListRange(Global::imeMode);
		}

	}
	if (_pTableDictionaryEngine[imeMode] == nullptr && (imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY))		//failed back to load windows preload tabletextservice table.
	{
		if (_pTableDictionaryEngine[imeMode] == nullptr)
		{
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if (_pTableDictionaryFile[imeMode] == nullptr)  goto ErrorExit;

			if (!PathFileExists(pwszTTSFileName))
			{
				if (imeMode != IME_MODE_ARRAY)
					StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
				else
					StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");
				CopyFile(pwszCINFileNameProgramFiles, pwszTTSFileName, TRUE);

			}


			if (!(_pTableDictionaryFile[imeMode])->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				goto ErrorExit;
			}
			else if (_pTextService)
			{
				_pTableDictionaryEngine[imeMode] = //TTS file use '=' as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], TTS_DICTIONARY);
				if (_pTableDictionaryEngine[imeMode] == nullptr)  goto ErrorExit;

				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load phrase table
	*pwszCINFileName = L'\0';
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Phrase.cin");

	if (PathFileExists(pwszCINFileName))
	{
		if (_pPhraseDictionaryFile == nullptr ||
			(_pPhraseTableDictionaryEngine && _pPhraseTableDictionaryEngine->GetDictionaryType() == TTS_DICTIONARY))
		{
			if (_pPhraseDictionaryFile)
				delete _pPhraseDictionaryFile;
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (_pPhraseDictionaryFile && _pTextService && _pPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pPhraseTableDictionaryEngine)
					delete _pPhraseTableDictionaryEngine;
				_pPhraseTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.				

			}
		}
		else if (_pPhraseDictionaryFile->IsFileUpdated())
		{
			_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config to reload updated dictionary
		}
	}
	else if (_pPhraseTableDictionaryEngine == nullptr &&
		(imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY) && _pTableDictionaryFile[imeMode] &&
		_pTableDictionaryEngine[imeMode]->GetDictionaryType() == TTS_DICTIONARY)
	{
		_pPhraseTableDictionaryEngine = _pTableDictionaryEngine[imeMode];
	}
	else if (_pPhraseTableDictionaryEngine == nullptr)
	{
		_pPhraseDictionaryFile = new (std::nothrow) CFile();
		if (_pPhraseDictionaryFile == nullptr)  goto ErrorExit;

		if (!PathFileExists(pwszTTSFileName))
		{
			if (imeMode != IME_MODE_ARRAY)
				StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
			else
				StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");
			CopyFile(pwszCINFileNameProgramFiles, pwszTTSFileName, TRUE);

		}

		if (!_pPhraseDictionaryFile->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
		{
			goto ErrorExit;
		}
		else if (_pTextService) // no user provided phrase table present and we are not in ARRAY or DAYI, thus we load TTS DAYI table to provide phrase table
		{
			if (_pPhraseTableDictionaryEngine)
				delete _pPhraseTableDictionaryEngine;
			_pPhraseTableDictionaryEngine =
				new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, TTS_DICTIONARY); //TTS file use '=' as delimiter
			if (!_pPhraseTableDictionaryEngine)  goto ErrorExit;

			_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.

		}
	}


	// now load Array unicode ext-b, ext-cd, ext-e , special code and short-code table
	if ( imeMode == IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal romaing profile
	{
		BOOL wstatFailed = TRUE, updated = TRUE;
		if (CConfig::GetArrayScope() != ARRAY40_BIG5)
		{
			//Ext-B
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-Ext-B.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-B.cin");

			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-B.cin");
			else if (_pArrayExtBDictionaryFile && _pTextService &&
				CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtBDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtBDictionaryFile;
				_pArrayExtBDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtBDictionaryFile == nullptr)
				{
					_pArrayExtBDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtBDictionaryFile && _pTextService &&
						_pArrayExtBDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayExtBTableDictionaryEngine)
							delete _pArrayExtBTableDictionaryEngine;
						_pArrayExtBTableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtBDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArrayExtBTableDictionaryEngine)
							_pArrayExtBTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
					}
				}
				else if (_pArrayExtBTableDictionaryEngine && _pArrayExtBDictionaryFile->IsFileUpdated())
				{
					_pArrayExtBTableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}
			//Ext-CD
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-Ext-CD.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-CD.cin");

			wstatFailed = TRUE; updated = TRUE;
			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-CD.cin");
			else if (_pArrayExtCDDictionaryFile && _pTextService &&
				CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtCDDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtCDDictionaryFile;
				_pArrayExtCDDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtCDDictionaryFile == nullptr)
				{
					_pArrayExtCDDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtCDDictionaryFile && _pTextService &&
						_pArrayExtCDDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayExtCDTableDictionaryEngine)
							delete _pArrayExtCDTableDictionaryEngine;
						_pArrayExtCDTableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtCDDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArrayExtCDTableDictionaryEngine)
							_pArrayExtCDTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
					}
				}
				else if (_pArrayExtCDTableDictionaryEngine && _pArrayExtCDDictionaryFile->IsFileUpdated())
				{
					_pArrayExtCDTableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}

			//Ext-EFG
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-Ext-EF.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-EF.cin");

			wstatFailed = TRUE; updated = TRUE;
			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Ext-EF.cin");
			else if (_pArrayExtEDictionaryFile && _pTextService &&
				CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtEDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtEDictionaryFile;
				_pArrayExtEDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtEDictionaryFile == nullptr)
				{
					_pArrayExtEDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtEDictionaryFile && _pTextService &&
						_pArrayExtEDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						_pArrayExtETableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtEDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArrayExtETableDictionaryEngine)
							_pArrayExtETableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
					}
				}
				else if (_pArrayExtETableDictionaryEngine && _pArrayExtEDictionaryFile->IsFileUpdated())
				{
					_pArrayExtETableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}
			//Array-phrase
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-Phrase.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Phrase.cin");

			wstatFailed = TRUE; updated = TRUE;
			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-phrase.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-Phrase.cin");

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayPhraseDictionaryFile == nullptr)
				{
					_pArrayPhraseDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayPhraseDictionaryFile && _pTextService &&
						_pArrayPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayPhraseTableDictionaryEngine)
							delete _pArrayPhraseTableDictionaryEngine;
						_pArrayPhraseTableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayPhraseDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArrayPhraseTableDictionaryEngine)
							_pArrayPhraseTableDictionaryEngine->ParseConfig(imeMode);// to release the file handle
					}
				}
				else if (_pArrayPhraseTableDictionaryEngine && _pArrayPhraseDictionaryFile->IsFileUpdated())
				{
					_pArrayPhraseTableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}
			//Special
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-special.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-special.cin");

			wstatFailed = TRUE; updated = TRUE;
			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-special.cin");
			else if (_pArraySpecialCodeDictionaryFile && _pTextService &&
				CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArraySpecialCodeDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
				delete _pArraySpecialCodeDictionaryFile;
				_pArraySpecialCodeDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArraySpecialCodeDictionaryFile == nullptr)
				{
					_pArraySpecialCodeDictionaryFile = new (std::nothrow) CFile();
					if (_pArraySpecialCodeDictionaryFile && _pTextService &&
						_pArraySpecialCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArraySpecialCodeTableDictionaryEngine)
							_pArraySpecialCodeTableDictionaryEngine;
						_pArraySpecialCodeTableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArraySpecialCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArraySpecialCodeTableDictionaryEngine)
							_pArraySpecialCodeTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
					}
				}
				else if (_pArraySpecialCodeTableDictionaryEngine && _pArraySpecialCodeDictionaryFile->IsFileUpdated())
				{
					_pArraySpecialCodeTableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}
			//Short-code
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Array-shortcode.cin");
			StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-shortcode.cin");

			wstatFailed = TRUE; updated = TRUE;
			if (PathFileExists(pwszCINFileNameProgramFiles))
				_wstat(pwszCINFileNameProgramFiles, &programFilesTimeStamp);
			if (PathFileExists(pwszCINFileName))
				wstatFailed = _wstat(pwszCINFileName, &appDataTimeStamp) == -1;
			updated = difftime(programFilesTimeStamp.st_mtime, appDataTimeStamp.st_mtime) > 0;
			if (!PathFileExists(pwszCINFileName) || (!wstatFailed && updated))
				CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, FALSE);

			if (!PathFileExists(pwszCINFileName)) //failed back to preload array-shortcode.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\DIME\\Array-shortcode.cin");
			else if (_pArrayShortCodeDictionaryFile && _pTextService &&
				CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayShortCodeDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
				delete _pArrayShortCodeDictionaryFile;
				_pArrayShortCodeDictionaryFile = nullptr;
			}
			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayShortCodeDictionaryFile == nullptr)
				{
					_pArrayShortCodeDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayShortCodeDictionaryFile && _pTextService &&
						_pArrayShortCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayShortCodeTableDictionaryEngine)
							delete _pArrayShortCodeTableDictionaryEngine;
						_pArrayShortCodeTableDictionaryEngine =
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayShortCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
						if (_pArrayShortCodeTableDictionaryEngine)
							_pArrayShortCodeTableDictionaryEngine->ParseConfig(imeMode);// to release the file handle
					}
				}
				else if (_pArrayShortCodeTableDictionaryEngine && _pArrayShortCodeDictionaryFile->IsFileUpdated())
				{
					_pArrayShortCodeTableDictionaryEngine->ParseConfig(imeMode); // parse config to reload dictionary
				}
			}
		}


	}

	// now load Dayi custom table
	if (imeMode == IME_MODE_DAYI)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\DAYI-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE_ARRAY)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\ARRAY-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE_PHONETIC)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\PHONETIC-CUSTOM.cin");
	else
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\GENERIC-CUSTOM.cin");
	if (PathFileExists(pwszCINFileName))
	{
		if (_pCustomTableDictionaryEngine[imeMode] == nullptr)
		{
			_pCustomTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if (_pCustomTableDictionaryFile[imeMode] && _pTextService &&
				_pCustomTableDictionaryFile[imeMode]->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
			{
				if (_pCustomTableDictionaryEngine[imeMode])
					delete _pCustomTableDictionaryEngine[imeMode];
				_pCustomTableDictionaryEngine[imeMode] =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pCustomTableDictionaryFile[imeMode], CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pCustomTableDictionaryEngine[imeMode])
					_pCustomTableDictionaryEngine[imeMode]->ParseConfig(imeMode);// to release the file handle
			}
		}
		else if (_pCustomTableDictionaryEngine && _pCustomTableDictionaryFile[imeMode]->IsFileUpdated())
		{
			_pCustomTableDictionaryEngine[imeMode]->ParseConfig(imeMode);
		}
	}


	bRet = TRUE;
ErrorExit:
	if (pwszTTSFileName)  delete[]pwszTTSFileName;
	if (pwszCINFileName)  delete[]pwszCINFileName;
	if (pwszCINFileNameProgramFiles) delete[]pwszCINFileNameProgramFiles;
	return bRet;
}

BOOL CCompositionProcessorEngine::SetupHanCovertTable()
{
	debugPrint(L"CCompositionProcessorEngine::SetupHanCovertTable() \n");

	BOOL bRet = FALSE;

	if (CConfig::GetDoHanConvert() && _pTCSCTableDictionaryEngine == nullptr)
	{
		WCHAR wszProgramFiles[MAX_PATH];
		WCHAR wszAppData[MAX_PATH];

		if (GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) == 0)
		{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running is 64-bit.
			//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
			GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
		}

		//CSIDL_APPDATA  personal roadming application data.
		SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);

		debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() :wszProgramFiles = %s", wszProgramFiles);

		WCHAR* pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
		WCHAR* pwszCINFileNameProgramFiles = new (std::nothrow) WCHAR[MAX_PATH];

		if (pwszCINFileName == NULL || pwszCINFileNameProgramFiles == NULL)  goto ErrorExit;
		*pwszCINFileName = L'\0';
		*pwszCINFileNameProgramFiles = L'\0';

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\TCSC.cin");
		StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\TCSC.cin");
		if (PathFileExists(pwszCINFileNameProgramFiles) && !PathFileExists(pwszCINFileName))
			CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, TRUE);
		if (!PathFileExists(pwszCINFileName)) //failed back to try program files
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\TCSC.cin");

		if (_pTCSCTableDictionaryFile == nullptr)
		{
			_pTCSCTableDictionaryFile = new (std::nothrow) CFile();
			if (_pTCSCTableDictionaryFile && _pTextService &&
				(_pTCSCTableDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pTCSCTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
			}
		}
		bRet = TRUE;
	ErrorExit:
		if (pwszCINFileName)  delete[]pwszCINFileName;
		if (pwszCINFileNameProgramFiles) delete[]pwszCINFileNameProgramFiles;

	}
	return bRet;
}

BOOL CCompositionProcessorEngine::SetupTCFreqTable()
{
	debugPrint(L"CCompositionProcessorEngine::SetupTCFreqTable() \n");

	BOOL bRet = FALSE;

	WCHAR wszProgramFiles[MAX_PATH];
	WCHAR wszAppData[MAX_PATH];

	if (GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) == 0)
	{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running is 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roadming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);

	debugPrint(L"CCompositionProcessorEngine::SetupTCFreqTable() :wszProgramFiles = %s", wszProgramFiles);

	WCHAR* pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR* pwszCINFileNameProgramFiles = new (std::nothrow) WCHAR[MAX_PATH];
	if (pwszCINFileName == NULL || pwszCINFileNameProgramFiles == NULL)  goto ErrorExit;
	*pwszCINFileName = L'\0';
	*pwszCINFileNameProgramFiles = L'\0';

	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\TCFreq.cin");
	StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\TCFreq.cin");
	if (PathFileExists(pwszCINFileNameProgramFiles) && !PathFileExists(pwszCINFileName))
		CopyFile(pwszCINFileNameProgramFiles, pwszCINFileName, TRUE);
	if (!PathFileExists(pwszCINFileName)) //failed back to try program files
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\TCFreq.cin");

	if (_pTCFreqTableDictionaryFile == nullptr)
	{
		_pTCFreqTableDictionaryFile = new (std::nothrow) CFile();
		if (_pTCFreqTableDictionaryFile && _pTextService &&
			(_pTCFreqTableDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
		{
			_pTCFreqTableDictionaryEngine =
				new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCFreqTableDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
		}
	}

	bRet = TRUE;
ErrorExit:
	if (pwszCINFileName)  delete[]pwszCINFileName;
	if (pwszCINFileNameProgramFiles) delete[]pwszCINFileNameProgramFiles;
	return bRet;
}
BOOL CCompositionProcessorEngine::GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	stringToConvert; convertedString;
	//not yet implemented
	return FALSE;
}
int CCompositionProcessorEngine::GetTCFreq(CStringRange* stringToFind)
{
	debugPrint(L"CCompositionProcessorEngine::GetTCFreq()");

	if (_pTCFreqTableDictionaryEngine == nullptr) SetupTCFreqTable();
	if (_pTCFreqTableDictionaryEngine == nullptr) return -1;

	if (stringToFind->GetLength() > 1) return (int)stringToFind->GetLength();

	CDIMEArray<CCandidateListItem> candidateList;
	_pTCFreqTableDictionaryEngine->CollectWord(stringToFind, &candidateList);
	if (candidateList.Count() == 1)
	{
		return _wtoi(candidateList.GetAt(0)->_ItemString.Get());
	}
	else
	{
		return 0;
	}


}

BOOL CCompositionProcessorEngine::GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	debugPrint(L"CCompositionProcessorEngine::GetSCFromTC()");
	if (!CConfig::GetDoHanConvert() || stringToConvert == nullptr || convertedString == nullptr) return FALSE;

	if (_pTCSCTableDictionaryEngine == nullptr) SetupHanCovertTable();
	if (_pTCSCTableDictionaryEngine == nullptr) return FALSE;

	UINT lenToConvert = (UINT)stringToConvert->GetLength();
	PWCHAR pwch = new (std::nothrow) WCHAR[static_cast<size_t>(lenToConvert) + 1];
	if (pwch == nullptr) return FALSE;
	*pwch = L'\0';
	CStringRange wcharToCover;

	for (UINT i = 0; i < lenToConvert; i++)
	{
		CDIMEArray<CCandidateListItem> candidateList;
		_pTCSCTableDictionaryEngine->CollectWord(&wcharToCover.Set(stringToConvert->Get() + i, 1), &candidateList);
		if (candidateList.Count() == 1)
			StringCchCatN(pwch, static_cast<size_t>(lenToConvert) + 1, candidateList.GetAt(0)->_ItemString.Get(), 1);
		else
			StringCchCatN(pwch, static_cast<size_t>(lenToConvert) + 1, wcharToCover.Get(), 1);
	}
	convertedString->Set(pwch, lenToConvert);
	return TRUE;
}

//+---------------------------------------------------------------------------
//
// GetDictionaryFile
//
//----------------------------------------------------------------------------

CFile* CCompositionProcessorEngine::GetDictionaryFile()
{
	return _pTableDictionaryFile[Global::imeMode];
}



//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr* pThreadMgr)
{
	ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

	if (IsEqualGUID(Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr && FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
	{
		return FALSE;
	}

	if (pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
		pPreservedKey.uModifiers &= 0xffff;

		pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
	Guid = GUID_NULL;
	Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
	ITfThreadMgr* pThreadMgr = nullptr;

	HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
	if (SUCCEEDED(hr) && pThreadMgr)
	{
		UninitPreservedKey(pThreadMgr);
		pThreadMgr->Release();
		pThreadMgr = nullptr;
	}

	if (Description)
	{
		delete[] Description;
	}
}



void CCompositionProcessorEngine::SetupCandidateListRange(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupCandidateListRange() ");

	_candidateListIndexRange.Clear();
	_phraseCandidateListIndexRange.Clear();

	PWCH pSelkey = nullptr;
	if (_pTableDictionaryEngine[imeMode])
		pSelkey = _pTableDictionaryEngine[imeMode]->GetSelkey();
	WCHAR localSelKey[MAX_CAND_SELKEY];
	WCHAR phraseSelKey[MAX_CAND_SELKEY];

	if (pSelkey == nullptr || wcslen(pSelkey) == 0)
	{
		StringCchCopy(localSelKey, MAX_CAND_SELKEY, (imeMode == IME_MODE_DAYI) ? L"'[]\\-\"{}|" : L"1234567890");
		pSelkey = localSelKey;
		_candidatePageSize = (imeMode == IME_MODE_PHONETIC) ? 9 : 10;
	}
	else
		_candidatePageSize = (UINT)wcslen(pSelkey);

	for (DWORD i = 0; i < _candidatePageSize; i++)
	{
		_KEYSTROKE* pNewIndexRange = nullptr;
		_KEYSTROKE* pNewPhraseIndexRange = nullptr;

		pNewIndexRange = _candidateListIndexRange.Append();
		pNewPhraseIndexRange = _phraseCandidateListIndexRange.Append();
		if (pNewIndexRange != nullptr)
		{
			if (imeMode == IME_MODE_DAYI)
			{
				if (i == 0)
				{
					pNewIndexRange->Printable = ' ';
					pNewIndexRange->VirtualKey = VK_SPACE;
				}
				else
				{
					pNewIndexRange->Printable = pSelkey[i - 1];
					UINT vKey, modifier;
					GetVKeyFromPrintable(pSelkey[i - 1], &vKey, &modifier);
					pNewIndexRange->VirtualKey = vKey;
					pNewIndexRange->Modifiers = modifier;
				}
				pNewIndexRange->Index = i;
			}
			else
			{
				pNewIndexRange->Printable = pSelkey[i];
				UINT vKey, modifier;
				GetVKeyFromPrintable(pSelkey[i], &vKey, &modifier);
				pNewIndexRange->VirtualKey = vKey;
				pNewIndexRange->Modifiers = modifier;
				if (i != 9)
				{
					pNewIndexRange->Index = i + 1;
				}
				else
				{
					pNewIndexRange->Index = 0;
				}
			}
		}
		if (pNewPhraseIndexRange != nullptr)
		{
			StringCchCopy(phraseSelKey, MAX_CAND_SELKEY, L"!@#$%^&*()");
			pNewPhraseIndexRange->Printable = phraseSelKey[i];
			UINT vKey, modifier;
			GetVKeyFromPrintable(phraseSelKey[i], &vKey, &modifier);
			pNewPhraseIndexRange->VirtualKey = vKey;
			pNewPhraseIndexRange->Modifiers = modifier;
			if (i != 9)
			{
				pNewPhraseIndexRange->Index = i + 1;
				pNewPhraseIndexRange->Modifiers = TF_MOD_SHIFT;
			}
			else
			{
				pNewPhraseIndexRange->Index = 0;
				pNewPhraseIndexRange->Modifiers = TF_MOD_SHIFT;

			}

		}
	}
	_pActiveCandidateListIndexRange = &_candidateListIndexRange;  // Preset the active cand list range
}
