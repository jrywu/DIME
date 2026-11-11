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
#include <sys/stat.h>
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "Globals.h"
#include "KeyHandlerEditSession.h"


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
		if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
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
// SetupBig5Engine
//
// Creates (or recreates) a Big5-filtered CMemoryFile + CTableDictionaryEngine
// for the given imeMode slot, wrapping the already-loaded main dictionary
// file/engine for that slot.  Any previous Big5 engine/file is deleted first.
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupBig5Engine(IME_MODE imeMode, LCID locale)
{
	UINT slot = (UINT)imeMode;

	// Big5 engines are only needed for the currently active IME mode.
	// SetupDictionaryFile is also called for secondary modes by the reverse-conversion
	// provider; those loads must not trigger a Big5 engine build or debug dump.
	if (imeMode != Global::imeMode) return;

	if (_pBig5TableDictionaryEngine[slot])
	{
		delete _pBig5TableDictionaryEngine[slot];
		_pBig5TableDictionaryEngine[slot] = nullptr;
	}
	if (_pBig5TableDictionaryFile[slot])
	{
		delete _pBig5TableDictionaryFile[slot];
		_pBig5TableDictionaryFile[slot] = nullptr;
	}

	// Skip building when Big5 filtering is not active for this mode
	bool needsBig5 = (imeMode == IME_MODE::IME_MODE_ARRAY)
		? (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY30_BIG5)
		: CConfig::GetBig5Filter();
	if (!needsBig5) return;

	if (!_pTableDictionaryEngine[slot] || !_pTableDictionaryFile[slot] ||
		!_pTableDictionaryFile[slot]->GetReadBufferPointer()) return;

	_pBig5TableDictionaryFile[slot] = new (std::nothrow) CMemoryFile(_pTableDictionaryFile[slot]);
	if (!_pBig5TableDictionaryFile[slot]) return;

	_pBig5TableDictionaryEngine[slot] = CTableDictionaryEngine::Create(
		locale, _pBig5TableDictionaryFile[slot], _pTableDictionaryEngine[slot]->GetDictionaryType());
	if (_pBig5TableDictionaryEngine[slot])
		_pBig5TableDictionaryEngine[slot]->ParseConfig(imeMode);
}

//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile(IME_MODE imeMode, LCID locale)
{
#if defined(DIME_USE_RUST_ENGINE)
	return _SetupDictionaryFileRust(imeMode, locale);
#else
	return _SetupDictionaryFileCpp(imeMode, locale);
#endif
}

BOOL CCompositionProcessorEngine::_SetupDictionaryFileCpp(IME_MODE imeMode, LCID locale)
{
	debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() \n");
	BOOL bRet = FALSE;

	WCHAR pwszProgramFiles[MAX_PATH] = { 0 }; // Initialize the array to zero.;
	WCHAR pwszAppData[MAX_PATH] = { 0 }; // Initialize the array to zero.;
	bool big5EngineRebuilt = false;


	if (GetEnvironmentVariable(L"ProgramW6432", pwszProgramFiles, MAX_PATH) == 0)
	{//on 64-bit vista only 32bit app has this environment variable.  Which means the call failed when the apps running in 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles environment variable now will retrieve the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", pwszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roaming application data.
	if (pwszAppData)
		Global::GetAppDataPath(pwszAppData, TRUE);

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
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode]  &&
			CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
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
				(CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5) ? L"\\DIME\\Array40.cin" : L"\\DIME\\Array.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Array.cin in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}

		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode] &&
			CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
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
		if (PathFileExists(pwszCINFileName) && _pTableDictionaryFile[(UINT)imeMode] &&
			CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[(UINT)imeMode]->GetFileName(), -1) != CSTR_EQUAL)
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

	// Tracks whether SetupBig5Engine was called inside one of the loading branches
	// below.  If not (config-only change, dictionary already loaded), it is called
	// unconditionally after both blocks so that toggling Big5Filter takes effect.

	if (PathFileExists(pwszCINFileName))  //create cin CFile object
	{
		if (_pTableDictionaryFile[(UINT)imeMode] == nullptr)
		{
			//create CFile object
			_pTableDictionaryFile[(UINT)imeMode] = new (std::nothrow) CFile();
			if ((_pTableDictionaryFile[(UINT)imeMode]) && 
				(_pTableDictionaryFile[(UINT)imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pTableDictionaryEngine[(UINT)imeMode])
					delete _pTableDictionaryEngine[(UINT)imeMode];
				_pTableDictionaryEngine[(UINT)imeMode] = //cin files use tab as delimiter
					CTableDictionaryEngine::Create(locale, _pTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::CIN_DICTIONARY);
				_pTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode); //parse config first.
			SetupBig5Engine(imeMode, locale);
			big5EngineRebuilt = true;

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
			SetupBig5Engine(imeMode, locale);
			big5EngineRebuilt = true;
			// Reload Keystroke and CandidateListRage when table is updated
			_SetupKeystrokeCpp(Global::imeMode);
			CConfig::LoadConfig(Global::imeMode);
			SetupConfiguration(Global::imeMode);
#ifndef DIMESettings
			_SetupCandidateListRangeCpp(Global::imeMode);
#endif
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
			else
			{
				_pTableDictionaryEngine[(UINT)imeMode] = //TTS file use '=' as delimiter
					CTableDictionaryEngine::Create(locale, _pTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::TTS_DICTIONARY);
				if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr)  goto ErrorExit;

				_pTableDictionaryEngine[(UINT)imeMode]->ParseConfig(imeMode); //parse config first.
				SetupBig5Engine(imeMode, locale);
				big5EngineRebuilt = true;

			}
		}
	}

	// Config-change path: if neither of the loading branches above called
	// SetupBig5Engine (dictionary file was already loaded and unchanged on disk,
	// only the Big5Filter setting was toggled), rebuild the Big5 engine now so
	// the new setting takes effect immediately.  Works for both CIN and TTS modes.
	if (!big5EngineRebuilt && _pTableDictionaryEngine[(UINT)imeMode])
		SetupBig5Engine(imeMode, locale);

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
			if (_pPhraseDictionaryFile && _pPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				if (_pPhraseTableDictionaryEngine)
					delete _pPhraseTableDictionaryEngine;
				_pPhraseTableDictionaryEngine =
					CTableDictionaryEngine::Create(locale, _pPhraseDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
		else  // no user provided phrase table present and we are not in ARRAY or DAYI, thus we load TTS DAYI table to provide phrase table
		{
			if (_pPhraseTableDictionaryEngine)
				delete _pPhraseTableDictionaryEngine;
			_pPhraseTableDictionaryEngine =
				CTableDictionaryEngine::Create(locale, _pPhraseDictionaryFile, DICTIONARY_TYPE::TTS_DICTIONARY); //TTS file use '=' as delimiter
			if (!_pPhraseTableDictionaryEngine)  goto ErrorExit;

			_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.

		}
	}


	// now load Array unicode ext-b, ext-cd, ext-e , special code and short-code table
	if (imeMode == IME_MODE::IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal roaming profile
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
			else if (_pArrayExtBDictionaryFile &&
				CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtBDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtBDictionaryFile;
				_pArrayExtBDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtBDictionaryFile == nullptr)
				{
					_pArrayExtBDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtBDictionaryFile && 
						_pArrayExtBDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayExtBTableDictionaryEngine)
							delete _pArrayExtBTableDictionaryEngine;
						_pArrayExtBTableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArrayExtBDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			else if (_pArrayExtCDDictionaryFile &&
				CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtCDDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtCDDictionaryFile;
				_pArrayExtCDDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtCDDictionaryFile == nullptr)
				{
					_pArrayExtCDDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtCDDictionaryFile && 
						_pArrayExtCDDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayExtCDTableDictionaryEngine)
							delete _pArrayExtCDTableDictionaryEngine;
						_pArrayExtCDTableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArrayExtCDDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			else if (_pArrayExtEDictionaryFile &&
				CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtEDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ // cin filename is different. force reload the cin file
				delete _pArrayExtEDictionaryFile;
				_pArrayExtEDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayExtEDictionaryFile == nullptr)
				{
					_pArrayExtEDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayExtEDictionaryFile && 
						_pArrayExtEDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						_pArrayExtETableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArrayExtEDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
					if (_pArrayPhraseDictionaryFile &&
						_pArrayPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayPhraseTableDictionaryEngine)
							delete _pArrayPhraseTableDictionaryEngine;
						_pArrayPhraseTableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArrayPhraseDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			else if (_pArraySpecialCodeDictionaryFile && 
				CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pArraySpecialCodeDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ //indicate the previous table is built with system preload file in program files, and now user provides their own.
				delete _pArraySpecialCodeDictionaryFile;
				_pArraySpecialCodeDictionaryFile = nullptr;
			}

			if (PathFileExists(pwszCINFileName))
			{
				if (_pArraySpecialCodeDictionaryFile == nullptr)
				{
					_pArraySpecialCodeDictionaryFile = new (std::nothrow) CFile();
					if (_pArraySpecialCodeDictionaryFile && 
						_pArraySpecialCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArraySpecialCodeTableDictionaryEngine)
							_pArraySpecialCodeTableDictionaryEngine;
						_pArraySpecialCodeTableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArraySpecialCodeDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			else if (_pArrayShortCodeDictionaryFile &&
				CompareString(locale, NORM_IGNORECASE, pwszCINFileName, -1, _pArrayShortCodeDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
			{ //indicate the previous table is built with system preload file in program files, and now user provides their own.
				delete _pArrayShortCodeDictionaryFile;
				_pArrayShortCodeDictionaryFile = nullptr;
			}
			if (PathFileExists(pwszCINFileName))
			{
				if (_pArrayShortCodeDictionaryFile == nullptr)
				{
					_pArrayShortCodeDictionaryFile = new (std::nothrow) CFile();
					if (_pArrayShortCodeDictionaryFile && 
						_pArrayShortCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
					{
						if (_pArrayShortCodeTableDictionaryEngine)
							delete _pArrayShortCodeTableDictionaryEngine;
						_pArrayShortCodeTableDictionaryEngine =
							CTableDictionaryEngine::Create(locale, _pArrayShortCodeDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
			if (_pCustomTableDictionaryFile[(UINT)imeMode] && 
				_pCustomTableDictionaryFile[(UINT)imeMode]->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
			{
				if (_pCustomTableDictionaryEngine[(UINT)imeMode])
					delete _pCustomTableDictionaryEngine[(UINT)imeMode];
				_pCustomTableDictionaryEngine[(UINT)imeMode] =
					CTableDictionaryEngine::Create(locale, _pCustomTableDictionaryFile[(UINT)imeMode], DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
        if (_pBig5TableDictionaryEngine[i])
        {
            delete _pBig5TableDictionaryEngine[i];
            _pBig5TableDictionaryEngine[i] = nullptr;
        }
        if (_pBig5TableDictionaryFile[i])
        {
            delete _pBig5TableDictionaryFile[i];
            _pBig5TableDictionaryFile[i] = nullptr;
        }
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
#if defined(DIME_USE_RUST_ENGINE)
	_SetupKeystrokeRust(imeMode);
#else
	_SetupKeystrokeCpp(imeMode);
#endif
}

void CCompositionProcessorEngine::_SetupKeystrokeCpp(IME_MODE imeMode)
{
	debugPrint(L"SetupKeystroke for imeMode %d", imeMode);
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

#ifndef DIMESettings
//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
#if defined(DIME_USE_RUST_ENGINE)
	_SetupPreservedRust(pThreadMgr, tfClientId);
#else
	_SetupPreservedCpp(pThreadMgr, tfClientId);
#endif
}

void CCompositionProcessorEngine::_SetupPreservedCpp(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
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


	TF_PRESERVEDKEY preservedKeyHanConvert = { 0 };
	preservedKeyHanConvert.uVKey = VK_OEM_5;  // backslash '\' key
	preservedKeyHanConvert.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
	SetPreservedKey(Global::DIMEGuidHanConvertPreserveKey, preservedKeyHanConvert, L"Toggle Simplified/Traditional", &_PreservedKey_HanConvert);

	InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_Config, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_HanConvert, pThreadMgr, tfClientId);

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
#if defined(DIME_USE_RUST_ENGINE)
	_OnPreservedKeyRust(rguid, pIsEaten, pThreadMgr, tfClientId);
#else
	_OnPreservedKeyCpp(rguid, pIsEaten, pThreadMgr, tfClientId);
#endif
}

void CCompositionProcessorEngine::_OnPreservedKeyCpp(REFGUID rguid, _Out_ BOOL* pIsEaten, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
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
		if (_pTextService) _pTextService->_showChnEngNotifyCpp(isOpen ? FALSE : TRUE);

		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
	{
		if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
		{
			// When not in Shift-Space switching mode, inject a space character instead
			ITfDocumentMgr* pDocMgrFocus = nullptr;
			ITfContext* pContext = nullptr;
			
			if (SUCCEEDED(pThreadMgr->GetFocus(&pDocMgrFocus)) && pDocMgrFocus)
			{
				if (SUCCEEDED(pDocMgrFocus->GetTop(&pContext)) && pContext)
				{
					// Inject space character via shift English input handler
					_KEYSTROKE_STATE keyState;
					keyState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
					keyState.Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
					
					if (_pTextService)
					{
						// Create an edit session to inject the space character
						CKeyHandlerEditSession* pEditSession = new (std::nothrow) CKeyHandlerEditSession(_pTextService, pContext, VK_SPACE, L' ', keyState);
						if (pEditSession)
						{
							HRESULT hrSession = S_OK;
							pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
							pEditSession->Release();
						}
					}
					
					pContext->Release();
				}
				pDocMgrFocus->Release();
			}
			
			*pIsEaten = TRUE;
			return;
		}
		BOOL isDouble = FALSE;
		CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
		CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
		CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
		//show notify
		if (_pTextService) _pTextService->_showFullHalfShapeNotifyCpp(isDouble ? FALSE : TRUE);
		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_Config.Guid) && _pTextService)
	{
		// call config dialog
		_pTextService->Show(GetForegroundWindow(), 0, GUID_NULL);
	}
	else if (IsEqualGUID(rguid, _PreservedKey_HanConvert.Guid))
	{
		// Bypass when in English mode — let the app handle the key
		BOOL isOpen = FALSE;
		if (Global::isWindows8) {
			CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
		}
		else {
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentIMEMode);
			CompartmentIMEMode._GetCompartmentBOOL(isOpen);
		}
		if (!isOpen)
		{
			*pIsEaten = FALSE;
			return;
		}

		BOOL currentState = CConfig::GetDoHanConvert();
		CConfig::SetDoHanConvert(currentState ? FALSE : TRUE);
		CConfig::WriteConfig(Global::imeMode);

		// Lazy-load TCSC table if enabling for the first time
		if (!currentState)
			SetupHanCovertTable();

		// Show notification: 繁 or 簡
		if (_pTextService)
			_pTextService->_showHanConvertNotifyCpp(currentState ? FALSE : TRUE);

		*pIsEaten = TRUE;
	}

	else
	{
		*pIsEaten = FALSE;
	}
	*pIsEaten = TRUE;
}


BOOL CCompositionProcessorEngine::SetupHanCovertTable()
{
#if defined(DIME_USE_RUST_ENGINE)
	return _SetupHanCovertTableRust();
#else
	return _SetupHanCovertTableCpp();
#endif
}

BOOL CCompositionProcessorEngine::_SetupHanCovertTableCpp()
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
		Global::GetAppDataPath(wszAppData, TRUE);

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
					CTableDictionaryEngine::Create(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
#if defined(DIME_USE_RUST_ENGINE)
	return _SetupTCFreqTableRust();
#else
	return _SetupTCFreqTableCpp();
#endif
}

BOOL CCompositionProcessorEngine::_SetupTCFreqTableCpp()
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
	Global::GetAppDataPath(wszAppData, TRUE);

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
				CTableDictionaryEngine::Create(_pTextService->GetLocale(), _pTCFreqTableDictionaryFile, DICTIONARY_TYPE::CIN_DICTIONARY); //cin files use tab as delimiter
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
#if defined(DIME_USE_RUST_ENGINE)
	_SetupCandidateListRangeRust(imeMode);
#else
	_SetupCandidateListRangeCpp(imeMode);
#endif
}

void CCompositionProcessorEngine::_SetupCandidateListRangeCpp(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupCandidateListRange() ");

	_candidateListIndexRange.Clear();
	_phraseCandidateListIndexRange.Clear();

	PWCH pSelkey = nullptr;
	if (_pTableDictionaryEngine[(UINT)imeMode])
		pSelkey = _pTableDictionaryEngine[(UINT)imeMode]->GetSelkey();
	WCHAR localSelKey[MAX_CAND_SELKEY];
	WCHAR phraseSelKey[MAX_CAND_SELKEY];

	// Dayi: a fixed select-key set overrides the table's %selkey.
	//  - Numeric (1-0): ten keys, space does not take a slot (it still converts
	//    to the highlighted candidate through the VK_SPACE convert path).
	//  - Dayi keys (' [ ] - \ " { } _): as in the Dayi manual, space is the
	//    first select key (candidate 0), ' the second, [ the third, ...: ten slots.
	BOOL spaceAsFirstCandSelkey = CConfig::GetSpaceAsFirstCaniSelkey();
	const DAYI_SELKEY_MODE dayiSelkeyMode = (imeMode == IME_MODE::IME_MODE_DAYI) ?
		CConfig::GetDayiSelkeyMode() : DAYI_SELKEY_MODE::DAYI_SELKEY_FROM_TABLE;

	if (dayiSelkeyMode == DAYI_SELKEY_MODE::DAYI_SELKEY_NUMERIC)
	{
		StringCchCopy(localSelKey, MAX_CAND_SELKEY, L"1234567890");
		pSelkey = localSelKey;
		_candidatePageSize = (UINT)wcslen(pSelkey);
		spaceAsFirstCandSelkey = FALSE;
	}
	else if (dayiSelkeyMode == DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI)
	{
		StringCchCopy(localSelKey, MAX_CAND_SELKEY, L"'[]-\\\"{}_");
		pSelkey = localSelKey;
		_candidatePageSize = (UINT)wcslen(pSelkey) + (spaceAsFirstCandSelkey ? 1 : 0);
	}
	else if (pSelkey == nullptr || wcslen(pSelkey) == 0)
	{
		StringCchCopy(localSelKey, MAX_CAND_SELKEY, (imeMode == IME_MODE::IME_MODE_DAYI) ? L"'[]-\\\"{}_" : L"1234567890");
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
			if (spaceAsFirstCandSelkey)
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
			StringCchCopy(phraseSelKey, MAX_CAND_SELKEY, L"!@#$%^&*()");
			pNewPhraseIndexRange->Printable = phraseSelKey[i];
			
			UINT vKey, modifier;
			GetVKeyFromPrintable(phraseSelKey[i], &vKey, &modifier);
			pNewPhraseIndexRange->VirtualKey = vKey;
			pNewPhraseIndexRange->Modifiers = modifier;
			if (i != 9)
			{
				pNewPhraseIndexRange->Index = i + 1;
				pNewPhraseIndexRange->CandIndex = WCHAR(i + 1 +0x30);  //ASCII 0x3x = x 
				pNewPhraseIndexRange->Modifiers = TF_MOD_SHIFT;
			}
			else
			{
				pNewPhraseIndexRange->CandIndex = L'0';
				pNewPhraseIndexRange->Index = 0;
				pNewPhraseIndexRange->Modifiers = TF_MOD_SHIFT;

			}

		}
	}
	
}

#endif

#if defined(DIME_USE_RUST_ENGINE) || (defined(DIME_UNIT_TESTING) && defined(DIME_RUST_AVAILABLE))
// Rust owns the load / keystroke / range / preserved-key decisions. This host
// applies them to the CFile and CTableDictionaryEngine members. Callback order
// matches DimeSetupHostVtbl in dime_ffi.h and rust/crates/dime-ffi/src/setup.rs.

static_assert(sizeof(DimeKeystrokeOut) == 20, "DimeKeystrokeOut abi");

static void FillKeystroke(_KEYSTROKE* dst, const DimeKeystrokeOut& src)
{
	if (!dst) return;
	dst->Index = src.index;
	dst->Printable = (WCHAR)src.printable;
	dst->CandIndex = (WCHAR)src.cand_index;
	dst->VirtualKey = src.virtual_key;
	dst->Modifiers = src.modifiers;
	dst->Function = (KEYSTROKE_FUNCTION)src.function;
}

static bool TermPath(const uint16_t* path, size_t len, WCHAR* buf, size_t cap)
{
	if (!buf || cap == 0) return false;
	if (!path) len = 0;
	if (len >= cap) len = cap - 1;
	if (len) CopyMemory(buf, path, len * sizeof(WCHAR));
	buf[len] = L'\0';
	return true;
}

static void ResolveSetupPaths(WCHAR* app, WCHAR* pf)
{
	app[0] = L'\0';
	pf[0] = L'\0';
	if (GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH) == 0)
		GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
	Global::GetAppDataPath(app, TRUE);
}

struct CEngineSetupHost
{
	CCompositionProcessorEngine* eng;

	static CEngineSetupHost* Self(DimeSetupHost* h)
	{
		return (h && h->ctx) ? static_cast<CEngineSetupHost*>(h->ctx) : nullptr;
	}

	CFile** FileSlot(int32_t id)
	{
		if (!eng || id < 0) return nullptr;
		int kind = id / 16;
		int slot = id % 16;
		switch (kind)
		{
		case 0: return (slot < IM_SLOTS) ? &eng->_pTableDictionaryFile[slot] : nullptr;
		case 1: return (slot < IM_SLOTS) ? &eng->_pCustomTableDictionaryFile[slot] : nullptr;
		case 2: return &eng->_pPhraseDictionaryFile;
		case 3: return &eng->_pArrayExtBDictionaryFile;
		case 4: return &eng->_pArrayExtCDDictionaryFile;
		case 5: return &eng->_pArrayExtEDictionaryFile;
		case 6: return &eng->_pArrayPhraseDictionaryFile;
		case 7: return &eng->_pArraySpecialCodeDictionaryFile;
		case 8: return &eng->_pArrayShortCodeDictionaryFile;
		case 9: return &eng->_pTCSCTableDictionaryFile;
		case 10: return &eng->_pTCFreqTableDictionaryFile;
		default: return nullptr;
		}
	}

	CTableDictionaryEngine** EngineSlot(int32_t id)
	{
		if (!eng || id < 0) return nullptr;
		int kind = id / 16;
		int slot = id % 16;
		switch (kind)
		{
		case 0: return (slot < IM_SLOTS) ? &eng->_pTableDictionaryEngine[slot] : nullptr;
		case 1: return (slot < IM_SLOTS) ? &eng->_pCustomTableDictionaryEngine[slot] : nullptr;
		case 2: return &eng->_pPhraseTableDictionaryEngine;
		case 3: return &eng->_pArrayExtBTableDictionaryEngine;
		case 4: return &eng->_pArrayExtCDTableDictionaryEngine;
		case 5: return &eng->_pArrayExtETableDictionaryEngine;
		case 6: return &eng->_pArrayPhraseTableDictionaryEngine;
		case 7: return &eng->_pArraySpecialCodeTableDictionaryEngine;
		case 8: return &eng->_pArrayShortCodeTableDictionaryEngine;
		case 9: return &eng->_pTCSCTableDictionaryEngine;
		case 10: return &eng->_pTCFreqTableDictionaryEngine;
		default: return nullptr;
		}
	}

	static int PathExists(DimeSetupHost* h, const uint16_t* path, size_t len)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		return PathFileExistsW(buf) ? 1 : 0;
	}

	static int FileMtime(DimeSetupHost* h, const uint16_t* path, size_t len, int64_t* mtime)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		struct _stat st;
		if (_wstat(buf, &st) != 0) return 0;
		if (mtime) *mtime = (int64_t)st.st_mtime;
		return 1;
	}

	static void CopyFileCb(DimeSetupHost* h, const uint16_t* src, size_t slen, const uint16_t* dst, size_t dlen, int failIfExists)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		WCHAR s[MAX_PATH], d[MAX_PATH];
		if (!TermPath(src, slen, s, MAX_PATH) || !TermPath(dst, dlen, d, MAX_PATH)) return;
		CopyFileW(s, d, failIfExists ? TRUE : FALSE);
	}

	static void CreateDirCb(DimeSetupHost* h, const uint16_t* path, size_t len)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return;
		CreateDirectoryW(buf, nullptr);
	}

	static int PathEqual(DimeSetupHost* h, uint32_t locale, const uint16_t* a, size_t alen, const uint16_t* b, size_t blen)
	{
		if (!Self(h)) return 0;
		if (alen == 0 && blen == 0) return 1;
		if (!a || !b) return 0;
		if (alen > 0x7fffffff) alen = 0x7fffffff;
		if (blen > 0x7fffffff) blen = 0x7fffffff;
		int r = CompareStringW((LCID)locale, NORM_IGNORECASE, (LPCWSTR)a, (int)alen, (LPCWSTR)b, (int)blen);
		return r == CSTR_EQUAL ? 1 : 0;
	}

	static int HasFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		return (s && *s) ? 1 : 0;
	}

	static int HasEngine(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		return (s && *s) ? 1 : 0;
	}

	static int DictType(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return -1;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		return (s && *s) ? (int)(*s)->GetDictionaryType() : -1;
	}

	static int FileUpdated(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		return (s && *s && (*s)->IsFileUpdated()) ? 1 : 0;
	}

	static size_t FileName(DimeSetupHost* h, int32_t id, uint16_t* buf, size_t cap)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !buf || cap == 0) return 0;
		CFile** s = self->FileSlot(id);
		if (!s || !*s || !(*s)->GetFileName()) return 0;
		size_t n = wcslen((*s)->GetFileName());
		size_t w = n < cap ? n : cap;
		if (w) CopyMemory(buf, (*s)->GetFileName(), w * sizeof(uint16_t));
		return w;
	}

	static void DropFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CFile** s = self->FileSlot(id);
		if (!s || !*s) return;
		delete *s;
		*s = nullptr;
	}

	static void DropEngine(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		if (!s || !*s) return;
		delete *s;
		*s = nullptr;
	}

	static int NewFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		if (!s) return 0;
		*s = new (std::nothrow) CFile();
		return *s ? 1 : 0;
	}

	static int CreateFileCb(DimeSetupHost* h, int32_t id, const uint16_t* path, size_t len, uint32_t share)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		if (!s || !*s) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		return (*s)->CreateFile(buf, GENERIC_READ, OPEN_EXISTING, share) ? 1 : 0;
	}

	static int CreateEngine(DimeSetupHost* h, int32_t id, uint32_t locale, int32_t dictType)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** file = self->FileSlot(id);
		CTableDictionaryEngine** eng = self->EngineSlot(id);
		if (!file || !*file || !eng) return 0;
		*eng = CTableDictionaryEngine::Create((LCID)locale, *file, (DICTIONARY_TYPE)dictType);
		return *eng ? 1 : 0;
	}

	static void ParseConfigCb(DimeSetupHost* h, int32_t id, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CTableDictionaryEngine** eng = self->EngineSlot(id);
		if (!eng || !*eng) return;
		(*eng)->ParseConfig((IME_MODE)mode);
	}

	static void AliasPhrase(DimeSetupHost* h, int32_t slot)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng || slot < 0 || slot >= IM_SLOTS) return;
		self->eng->_pPhraseTableDictionaryEngine = self->eng->_pTableDictionaryEngine[slot];
	}

	static void SetupBig5Cb(DimeSetupHost* h, int32_t mode, uint32_t locale)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupBig5Engine((IME_MODE)mode, (LCID)locale);
	}

	static void SetupKeystrokeCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupKeystroke((IME_MODE)mode);
	}

	static void LoadConfigCb(DimeSetupHost* h, int32_t mode)
	{
		if (!Self(h)) return;
		CConfig::LoadConfig((IME_MODE)mode);
	}

	static void SetupConfigurationCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupConfiguration((IME_MODE)mode);
	}

	static void SetupCandidateRangeCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupCandidateListRange((IME_MODE)mode);
	}

	static int HasTextService(DimeSetupHost* h)
	{
		CEngineSetupHost* self = Self(h);
		return (self && self->eng && self->eng->_pTextService) ? 1 : 0;
	}

	static int ArrayScopeCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return (int)CConfig::GetArrayScope();
	}

	static int ActiveModeCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return (int)Global::imeMode;
	}

	static int DoHanConvertCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return CConfig::GetDoHanConvert() ? 1 : 0;
	}

	static const DimeSetupHostVtbl Vtbl;
};

const DimeSetupHostVtbl CEngineSetupHost::Vtbl = {
	PathExists,
	FileMtime,
	CopyFileCb,
	CreateDirCb,
	PathEqual,
	HasFile,
	HasEngine,
	DictType,
	FileUpdated,
	FileName,
	DropFile,
	DropEngine,
	NewFile,
	CreateFileCb,
	CreateEngine,
	ParseConfigCb,
	AliasPhrase,
	SetupBig5Cb,
	SetupKeystrokeCb,
	LoadConfigCb,
	SetupConfigurationCb,
	SetupCandidateRangeCb,
	HasTextService,
	ArrayScopeCb,
	ActiveModeCb,
	DoHanConvertCb,
};

static void BindSetup(CCompositionProcessorEngine* eng, DimeSetupHost* ffi, CEngineSetupHost* host)
{
	host->eng = eng;
	ffi->vtbl = &CEngineSetupHost::Vtbl;
	ffi->ctx = host;
}

BOOL CCompositionProcessorEngine::_SetupDictionaryFileRust(IME_MODE imeMode, LCID locale)
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	return dime_setup_dictionary(&ffi, (int32_t)imeMode, (uint32_t)locale, 0,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

void CCompositionProcessorEngine::_SetupKeystrokeRust(IME_MODE imeMode)
{
	if (!IsDictionaryAvailable(imeMode)) return;
	CTableDictionaryEngine* table = _pTableDictionaryEngine[(UINT)imeMode];
	_T_RadicalMap* map = (table) ? table->GetRadicalMap() : nullptr;
	if (!table || !map || map->empty() || map->size() > MAX_RADICAL) return;

	uint16_t radicals[MAX_RADICAL];
	size_t radicalCount = 0;
	for (_T_RadicalMap::iterator it = map->begin(); it != map->end() && radicalCount < MAX_RADICAL; ++it)
		radicals[radicalCount++] = (uint16_t)it->first;

	const uint16_t* end = _pEndkey ? (const uint16_t*)_pEndkey : nullptr;
	size_t endLen = _pEndkey ? wcslen(_pEndkey) : 0;
	DimeKeystrokeOut rows[MAX_RADICAL + 1];
	int32_t inserted = 0;
	size_t count = dime_build_keystrokes((int32_t)imeMode, radicals, radicalCount, end, endLen,
		rows, MAX_RADICAL + 1, &inserted);
	if (count > MAX_RADICAL + 1) count = MAX_RADICAL + 1;

	if (inserted && map->find(L'=') == map->end())
	{
		WCHAR* eq = new (std::nothrow) WCHAR[2];
		if (eq)
		{
			eq[0] = L'=';
			eq[1] = L'\0';
			(*map)[L'='] = eq;
		}
	}

	_KeystrokeComposition.Clear();
	for (size_t i = 0; i < count; i++)
		FillKeystroke(_KeystrokeComposition.Append(), rows[i]);
}

#ifndef DIMESettings
BOOL CCompositionProcessorEngine::_SetupHanCovertTableRust()
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	LCID locale = _pTextService ? _pTextService->GetLocale() : 0;
	return dime_setup_han_convert(&ffi, (uint32_t)locale,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

BOOL CCompositionProcessorEngine::_SetupTCFreqTableRust()
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	LCID locale = _pTextService ? _pTextService->GetLocale() : 0;
	return dime_setup_tc_freq(&ffi, (uint32_t)locale,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

void CCompositionProcessorEngine::_SetupCandidateListRangeRust(IME_MODE imeMode)
{
	const WCHAR* sel = nullptr;
	size_t selLen = 0;
	UINT slot = (UINT)imeMode;
	if (slot < IM_SLOTS && _pTableDictionaryEngine[slot] && _pTableDictionaryEngine[slot]->GetSelkey())
	{
		sel = _pTableDictionaryEngine[slot]->GetSelkey();
		selLen = wcslen(sel);
	}
	DimeKeystrokeOut cand[16];
	DimeKeystrokeOut phrase[16];
	uint32_t page = 0;
	int32_t dayiMode = (imeMode == IME_MODE::IME_MODE_DAYI) ? (int32_t)CConfig::GetDayiSelkeyMode() : 0;
	size_t n = dime_build_candidate_range((int32_t)imeMode, (int32_t)Global::imeMode, dayiMode,
		CConfig::GetSpaceAsFirstCaniSelkey() ? 1 : 0, (int32_t)CConfig::GetArrayScope(),
		(const uint16_t*)sel, selLen, cand, phrase, 16, &page);
	_candidateListIndexRange.Clear();
	_phraseCandidateListIndexRange.Clear();
	_candidatePageSize = page;
	if (n > 16) n = 16;
	if (n > page) n = page;
	for (size_t i = 0; i < n; i++)
	{
		FillKeystroke(_candidateListIndexRange.Append(), cand[i]);
		FillKeystroke(_phraseCandidateListIndexRange.Append(), phrase[i]);
	}
}

void CCompositionProcessorEngine::_SetupPreservedRust(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	int n = dime_preserved_key_count();
	for (int i = 0; i < n; i++)
	{
		DimePreservedKey key = {};
		uint16_t desc[64];
		if (!dime_preserved_key_at(i, &key, desc, 64)) continue;
		TF_PRESERVEDKEY pk = {};
		pk.uVKey = key.vkey;
		pk.uModifiers = key.modifiers;
		const GUID* guid = nullptr;
		XPreservedKey* slot = nullptr;
		const WCHAR* text = nullptr;
		WCHAR local[64];
		local[0] = L'\0';
		switch (key.id)
		{
		case 0:
			guid = &Global::DIMEGuidImeModePreserveKey;
			slot = &_PreservedKey_IMEMode;
			text = Global::ImeModeDescription;
			break;
		case 1:
			guid = &Global::DIMEGuidDoubleSingleBytePreserveKey;
			slot = &_PreservedKey_DoubleSingleByte;
			text = Global::DoubleSingleByteDescription;
			break;
		case 2:
			guid = &Global::DIMEGuidConfigPreserveKey;
			slot = &_PreservedKey_Config;
			break;
		case 3:
			guid = &Global::DIMEGuidHanConvertPreserveKey;
			slot = &_PreservedKey_HanConvert;
			break;
		default:
			continue;
		}
		if (key.desc_len != 0)
		{
			size_t cch = key.desc_len;
			if (cch >= 64) cch = 63;
			CopyMemory(local, desc, cch * sizeof(WCHAR));
			local[cch] = L'\0';
			text = local;
		}
		if (guid && text)
			SetPreservedKey(*guid, pk, text, slot);
	}
	InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_Config, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_HanConvert, pThreadMgr, tfClientId);
}
#endif
#endif
