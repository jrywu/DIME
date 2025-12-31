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
#include <Shlobj.h>
#include <Shlwapi.h>
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "Globals.h"

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
        _pTableDictionaryEngine[i] = nullptr;  
        _pCustomTableDictionaryEngine[i] = nullptr;  
    }  

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
    if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr || 
        _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap() == nullptr ||
        _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->size() == 0 ||
        _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->size() > MAX_RADICAL) 
    {
        return;
    }

    _KeystrokeComposition.Clear();

    for (int i = 0; i < MAX_RADICAL + 1; i++)
    {
        _KEYSTROKE* pKS = nullptr;
        pKS = _KeystrokeComposition.Append();
        if (pKS) // Ensure pKS is not null before accessing it
        {
            pKS->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;
            pKS->Modifiers = 0;
            pKS->Index = i;
            pKS->Printable = 0;
            pKS->VirtualKey = 0;
        }
    }

    if (imeMode == IME_MODE::IME_MODE_DAYI && _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap() &&
        (_pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->find('=') == _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->end()))
    { 
        // Dayi symbol prompt
        WCHAR* pwchEqual = new (std::nothrow) WCHAR[2];
        if (pwchEqual)
        {
            pwchEqual[0] = L'=';
            pwchEqual[1] = L'\0';
            (*_pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap())['='] = pwchEqual;
        }
    }

    for (_T_RadicalMap::iterator item = _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->begin(); 
         item != _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()->end(); ++item)
    {
        _KEYSTROKE* pKS = nullptr;

        WCHAR c = towupper(item->first);
        if (c < 32 || c > MAX_RADICAL + 32) 
        {
            continue;
        }
        pKS = _KeystrokeComposition.GetAt(static_cast<size_t>(c) - 32);
        if (pKS == nullptr)
        {
            break;
        }

        pKS->Function = KEYSTROKE_FUNCTION::FUNCTION_INPUT;
        UINT vKey = 0, modifier = 0;
        WCHAR key = item->first;
        pKS->Printable = key;
        GetVKeyFromPrintable(key, &vKey, &modifier);
        pKS->VirtualKey = vKey;
        pKS->Modifiers = modifier;
    }

    // End composing key
    if (_pEndkey)
    {
        for (UINT i = 0; i < wcslen(_pEndkey); i++)
        {
            _KEYSTROKE* pKS = nullptr;
            WCHAR c = *(_pEndkey + i);
            if (c < 32 || c > MAX_RADICAL + 32) 
            {
                continue;
            }
            pKS = _KeystrokeComposition.GetAt(static_cast<size_t>(c) - 32);
            if (pKS == nullptr) 
            {
                break;
            }
            if (pKS->Function == KEYSTROKE_FUNCTION::FUNCTION_INPUT)
            {
                pKS->Function = KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT;
            }
            else
            {
                pKS->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
            }
        }
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
    TF_PRESERVEDKEY preservedKeyImeMode = { 0 }; // Initialize the structure to zero
    preservedKeyImeMode.uVKey = VK_SHIFT;
    preservedKeyImeMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
    SetPreservedKey(Global::DIMEGuidImeModePreserveKey, preservedKeyImeMode, Global::ImeModeDescription, &_PreservedKey_IMEMode);

	TF_PRESERVEDKEY preservedKeyDoubleSingleByte = { 0 }; // Initialize the structure to zero
	preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
	preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
	SetPreservedKey(Global::DIMEGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);

	TF_PRESERVEDKEY preservedKeyConfig = { 0 }; // Initialize the structure to zero
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

		if (imeShiftMode == IME_SHIFT_MODE::IME_NO_SHIFT ||
			!CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable) ||
			(imeShiftMode == IME_SHIFT_MODE::IME_RIGHT_SHIFT_ONLY && (Global::ModifiersValue & (TF_MOD_LSHIFT))) ||
			(imeShiftMode == IME_SHIFT_MODE::IME_LEFT_SHIFT_ONLY && (Global::ModifiersValue & (TF_MOD_RSHIFT)))
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

		debugPrint(L"CCompositionProcessorEngine::OnPreservedKey() isOpen=%d", isOpen);
		//show notify
		if (_pTextService) _pTextService->showChnEngNotify(isOpen ? FALSE : TRUE);

		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
	{
		if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
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
		if (_pTextService) _pTextService->showFullHalfShapeNotify(isDouble ? FALSE : TRUE);
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
	_isKeystrokeSort = FALSE;
	_isWildCardWordFreqSort = TRUE;

	//Override options for specific IME.
	if (imeMode == IME_MODE::IME_MODE_DAYI)
	{
		CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
	}
	else if (imeMode == IME_MODE::IME_MODE_ARRAY)
	{
		CConfig::SetMaxCodes(5);
		CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
		if(CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
			CConfig::SetAutoCompose(TRUE);
	}
	else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
	}
	if (_pTableDictionaryEngine[(UINT)imeMode])
		_pEndkey = _pTableDictionaryEngine[(UINT)imeMode]->GetEndkey();

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

	WCHAR pwszProgramFiles[MAX_PATH] = { 0 }; // Initialize the array to zero.;
	WCHAR pwszAppData[MAX_PATH] = { 0 }; // Initialize the array to zero.;

	if (GetEnvironmentVariable(L"ProgramW6432", pwszProgramFiles, MAX_PATH) == 0)
	{//on 64-bit vista only 32bit app has this environment variable.  Which means the call failed when the apps running in 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles environment variable now will retrieve the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", pwszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roaming application data.
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
	if (imeMode != IME_MODE::IME_MODE_ARRAY)
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\TableTextServiceDaYi.txt");
	else
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\TableTextServiceArray.txt");

	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME");

	if (!PathFileExists(pwszCINFileName))
	{
		//DIME roaming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);
	}
	// load main table file now
	if (imeMode == IME_MODE::IME_MODE_DAYI) //dayi.cin in personal roaming profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Dayi.cin");
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[(UINT)imeMode];
			_pTableDictionaryFile[(UINT)imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE::IME_MODE_ARRAY) //array.cin in personal roaming profile
	{
		BOOL cinFound = FALSE, wstatFailed = TRUE, updated = TRUE;

		if (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5)
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
				(CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5)?L"\\DIME\\Array40.cin":L"\\DIME\\Array.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Array.cin in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}

		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[(UINT)imeMode];
			_pTableDictionaryFile[(UINT)imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE::IME_MODE_PHONETIC) //phone.cin in personal roaming profile
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
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin filename is different. force reload the cin file
			delete _pTableDictionaryFile[(UINT)imeMode];
			_pTableDictionaryFile[(UINT)imeMode] = nullptr;
		}
	}
	else if (imeMode == IME_MODE::IME_MODE_GENERIC) //phone.cin in personal roaming profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Generic.cin");
		// we don't provide preload Generic.cin in program files
	}

	if (PathFileExists(pwszCINFileName))  //create cin CFile object
	{
		if (_pTableDictionaryFile[(UINT)imeMode] == nullptr)
		{
			//create CFile object
			_pTableDictionaryFile[(UINT)imeMode] = new (std::nothrow) CFile();
			if ((_pTableDictionaryFile[(UINT)imeMode]) && _pTextService &&
				(_pTableDictionaryFile[(UINT)imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pTableDictionaryEngine[(UINT)imeMode])
					delete _pTableDictionaryEngine[(UINT)imeMode];
				_pTableDictionaryEngine[(UINT)imeMode] = //cin files use tab as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::CIN_DICTIONARY);
				_pTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode); //parse config first.		

			}
			else
			{ // error on create file. do cleanup
				delete _pTableDictionaryFile[(UINT)imeMode];
				_pTableDictionaryFile[(UINT)imeMode] = nullptr;
				goto ErrorExit;
			}
		}
		else if (_pTableDictionaryEngine[(UINT)imeMode] && _pTableDictionaryFile[(UINT)imeMode]->IsFileUpdated())
		{
			_pTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode); //parse config to reload updated dictionary
			// Reload Keystroke and CandidateListRage when table is updated
			SetupKeystroke(Global::imeMode);
			CConfig::LoadConfig(Global::imeMode);
			SetupConfiguration(Global::imeMode);
			SetupCandidateListRange(Global::imeMode);
		}

	}
	if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr && (imeMode == IME_MODE::IME_MODE_DAYI || imeMode == IME_MODE::IME_MODE_ARRAY))		//failed back to load windows preload tabletextservice table.
	{
		if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr)
		{
			_pTableDictionaryFile[(UINT)imeMode] = new (std::nothrow) CFile();
			if (_pTableDictionaryFile[(UINT)imeMode] == nullptr)  goto ErrorExit;

			if (!PathFileExists(pwszTTSFileName))
			{
				if (imeMode != IME_MODE::IME_MODE_ARRAY)
					StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
				else
					StringCchPrintf(pwszCINFileNameProgramFiles, MAX_PATH, L"%s%s", pwszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");
				CopyFile(pwszCINFileNameProgramFiles, pwszTTSFileName, TRUE);

			}


			if (!(_pTableDictionaryFile[(UINT)imeMode])->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				goto ErrorExit;
			}
			else if (_pTextService)
			{
				_pTableDictionaryEngine[(UINT)imeMode] = //TTS file use '=' as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::TTS_DICTIONARY);
				if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr)  goto ErrorExit;

				_pTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load phrase table
	*pwszCINFileName = L'\0';
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\Phrase.cin");

	if (PathFileExists(pwszCINFileName))
	{
		if (_pPhraseDictionaryFile == nullptr ||
			(_pPhraseTableDictionaryEngine && _pPhraseTableDictionaryEngine->GetDictionaryType() == DICTIONARY_TYPE::TTS_DICTIONARY))
		{
			if (_pPhraseDictionaryFile)
				delete _pPhraseDictionaryFile;
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (_pPhraseDictionaryFile && _pTextService && _pPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pPhraseTableDictionaryEngine)
					delete _pPhraseTableDictionaryEngine;
				_pPhraseTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.				

			}
		}
		else if (_pPhraseDictionaryFile->IsFileUpdated())
		{
			_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config to reload updated dictionary
		}
	}
	else if (_pPhraseTableDictionaryEngine == nullptr &&
		(imeMode == IME_MODE::IME_MODE_DAYI || imeMode == IME_MODE::IME_MODE_ARRAY) && _pTableDictionaryFile[(UINT)imeMode] &&
		_pTableDictionaryEngine[(UINT)imeMode]->GetDictionaryType() == DICTIONARY_TYPE::TTS_DICTIONARY)
	{
		_pPhraseTableDictionaryEngine = _pTableDictionaryEngine[(UINT)imeMode];
	}
	else if (_pPhraseTableDictionaryEngine == nullptr)
	{
		_pPhraseDictionaryFile = new (std::nothrow) CFile();
		if (_pPhraseDictionaryFile == nullptr)  goto ErrorExit;

		if (!PathFileExists(pwszTTSFileName))
		{
			if (imeMode != IME_MODE::IME_MODE_ARRAY)
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
				new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, DICTIONARY_TYPE::TTS_DICTIONARY); //TTS file use '=' as delimiter
			if (!_pPhraseTableDictionaryEngine)  goto ErrorExit;

			_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.

		}
	}


	// now load Array unicode ext-b, ext-cd, ext-e , special code and short-code table
	if ( imeMode == IME_MODE::IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal roaming profile
	{
		BOOL wstatFailed = TRUE, updated = TRUE;
		if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtBDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtCDDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtEDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayPhraseDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			{ //indicate the previous table is built with system preload file in program files, and now user provides their own.
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArraySpecialCodeDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			{ //indicate the previous table is built with system preload file in program files, and now user provides their own.
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
							new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayShortCodeDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
	if (imeMode == IME_MODE::IME_MODE_DAYI)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\DAYI-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE::IME_MODE_ARRAY)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\ARRAY-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\PHONETIC-CUSTOM.cin");
	else
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", pwszAppData, L"\\DIME\\GENERIC-CUSTOM.cin");
	if (PathFileExists(pwszCINFileName))
	{
		if (_pCustomTableDictionaryEngine[(UINT)imeMode] == nullptr)
		{
			_pCustomTableDictionaryFile[(UINT)imeMode] = new (std::nothrow) CFile();
			if (_pCustomTableDictionaryFile[(UINT)imeMode] && _pTextService &&
				_pCustomTableDictionaryFile[(UINT)imeMode]->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
			{
				if (_pCustomTableDictionaryEngine[(UINT)imeMode])
					delete _pCustomTableDictionaryEngine[(UINT)imeMode];
				_pCustomTableDictionaryEngine[(UINT)imeMode] =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pCustomTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pCustomTableDictionaryEngine[(UINT)imeMode])
					_pCustomTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode);// to release the file handle
			}
		}
		else if (_pCustomTableDictionaryEngine && _pCustomTableDictionaryFile[(UINT)imeMode]->IsFileUpdated())
		{
			_pCustomTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode);
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
		{//on 64-bit vista only 32bit app has this environment variable.  Which means the call failed when the apps running is 64-bit.
			//on 32-bit windows, this will definitely failed.  Get ProgramFiles environment variable now will retrieve the correct program files path.
			GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
		}

		//CSIDL_APPDATA  personal roaming application data.
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
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
	{//on 64-bit vista only 32bit app has this environment variable.  Which means the call failed when the apps running is 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles environment variable now will retrieve the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roaming application data.
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
				new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCFreqTableDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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

CFile* CCompositionProcessorEngine::GetDictionaryFile() const
{
	return _pTableDictionaryFile[(UINT)Global::imeMode];
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
	if (_pTableDictionaryEngine[(UINT)imeMode])
		pSelkey = _pTableDictionaryEngine[(UINT)imeMode]->GetSelkey();
	WCHAR localSelKey[MAX_CAND_SELKEY];
	WCHAR phraseSelKey[MAX_CAND_SELKEY];

	if (pSelkey == nullptr || wcslen(pSelkey) == 0)
	{
		StringCchCopy(localSelKey, MAX_CAND_SELKEY, (imeMode == IME_MODE::IME_MODE_DAYI) ? L"'[]\\-\"{}|" : L"1234567890");
		pSelkey = localSelKey;
		_candidatePageSize = (imeMode == IME_MODE::IME_MODE_PHONETIC) ? 9 : 10;
	}
	else
	{
		_candidatePageSize = (UINT)wcslen(pSelkey);
		if (CConfig::GetSpaceAsFirstCaniSelkey())
			if (_candidatePageSize + 1 > 10) _candidatePageSize = 10;
		else if (_candidatePageSize > 10) _candidatePageSize = 10;
	}
	for (DWORD i = 0; i < _candidatePageSize; i++)
	{
		_KEYSTROKE* pNewIndexRange = nullptr;
		_KEYSTROKE* pNewPhraseIndexRange = nullptr;

		pNewIndexRange = _candidateListIndexRange.Append();
		pNewPhraseIndexRange = _phraseCandidateListIndexRange.Append();
		if (pNewIndexRange != nullptr)
		{
			if (CConfig::GetSpaceAsFirstCaniSelkey())
			{
				if (i == 0)
				{
					pNewIndexRange->Printable = ' ';
					pNewIndexRange->CandIndex = ' ';
					pNewIndexRange->VirtualKey = VK_SPACE;
				}
				else
				{
					pNewIndexRange->Printable = pSelkey[i - 1];
					pNewIndexRange->CandIndex = pSelkey[i - 1];
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
				if(Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope()== ARRAY_SCOPE::ARRAY40_BIG5)
					pNewIndexRange->CandIndex = WCHAR(i + 1 + 0x30);  //ASCII 0x3x = x 
				else
					pNewIndexRange->CandIndex = pSelkey[i];
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
                       // Use digit keys for phrase candidate selection so SHIFT is not required.
                       StringCchCopy(phraseSelKey, MAX_CAND_SELKEY, L"1234567890");
                       pNewPhraseIndexRange->Printable = phraseSelKey[i];
			
			UINT vKey, modifier;
			GetVKeyFromPrintable(phraseSelKey[i], &vKey, &modifier);
			pNewPhraseIndexRange->VirtualKey = vKey;
			pNewPhraseIndexRange->Modifiers = modifier;
			if (i != 9)
			{
                               pNewPhraseIndexRange->Index = i + 1;
                               pNewPhraseIndexRange->CandIndex = WCHAR(i + 1 +0x30);  //ASCII 0x3x = x
			}
			else
			{
                               pNewPhraseIndexRange->CandIndex = L'0';
                               pNewPhraseIndexRange->Index = 0;

			}

		}
	}
	
}
