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

#ifndef COMPOSITINPROCESSORENGINE_H
#define COMPOSITINPROCESSORENGINE_H

#pragma once

#include "sal.h"
#include "TableDictionaryEngine.h"
#include "KeyHandlerEditSession.h"
#include "BaseStructure.h"
#include "File.h"
#include "define.h"

class CCompositionProcessorEngine
{
public:
    CCompositionProcessorEngine(_In_ CDIME *pTextService);
    ~CCompositionProcessorEngine(void);
	
	

	BOOL IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, UINT candiCount, INT candiSelection, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL AddVirtualKey(WCHAR wch);
    void RemoveVirtualKey(DWORD_PTR dwIndex);
    void PurgeVirtualKey();

    DWORD_PTR GetVirtualKeyLength() { return _keystrokeBuffer.GetLength(); }
    WCHAR GetVirtualKey(DWORD_PTR dwIndex);

	void GetReadingString(_Inout_ CStringRange *pReadingString, _Inout_opt_ BOOL *pIsWildcardIncluded, _In_opt_ CStringRange *pKeyCode = nullptr);
    void GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch, BOOL isArrayPhraseEnding = FALSE);
    void GetCandidateStringInConverted(CStringRange &searchString, _In_ CDIMEArray<CCandidateListItem> *pCandidateList);

	//reverse converion
	HRESULT GetReverseConversionResults(IME_MODE imeMode, _In_ LPCWSTR lpstrToConvert, _Inout_ CDIMEArray<CCandidateListItem> *pCandidateList);

	//Han covert
	BOOL GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString);
	BOOL GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString);
	int GetTCFreq(CStringRange* stringToFind);

    // Preserved key handler
    void OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    // Symbol mode
    BOOL IsSymbolChar(WCHAR wch);
	BOOL IsSymbol();
	BOOL IsSymbolLeading();

	//Phonetic composingkey
	BOOL isEndComposingKey(WCHAR wch);


	//Dayi Address characters direct input
	BOOL IsDayiAddressChar(WCHAR wch);
	WCHAR GetDayiAddressChar(WCHAR wch);

	//Array short code and special code
	BOOL IsArrayShortCode();
	DWORD_PTR CollectWordFromArraySpeicalCode(_Inout_opt_ const WCHAR **ppwchSpecialCodeResultString);
	BOOL GetArraySpeicalCodeFromConvertedText(_In_ CStringRange *inword, _Inout_opt_ CStringRange *csrResult);
	
    BOOL IsDoubleSingleByte(WCHAR wch);
    //BOOL IsWildcard() { return _isWildcard; }
    //BOOL IsDisableWildcardAtFirst() { return _isDisableWildcardAtFirst; }
    BOOL IsWildcardChar(WCHAR wch) { return ((IsWildcardOneChar(wch) || IsWildcardAllChar(wch)) ? TRUE : FALSE); }
    BOOL IsWildcardOneChar(WCHAR wch) { return (wch==L'?' ? TRUE : FALSE); }
    BOOL IsWildcardAllChar(WCHAR wch) { return (wch==L'*' ? TRUE : FALSE); }
    BOOL IsKeystrokeSort() { return _isKeystrokeSort; }

    // Dictionary engine
	BOOL IsDictionaryAvailable(IME_MODE imeMode) { return (_pTableDictionaryEngine[(UINT)imeMode] && 
		_pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap()&&!_pTableDictionaryEngine[(UINT)imeMode] ->GetRadicalMap()->empty() ? TRUE : FALSE);}

  

    inline CCandidateRange *GetCandidateListIndexRange() { return _pActiveCandidateListIndexRange; }
    inline UINT GetCandidateListPhraseModifier() { return _candidateListPhraseModifier; }
    inline UINT GetCandidateWindowWidth() { return _candidateWndWidth; }
	inline UINT GetCandidatePageSize() { return _candidatePageSize; }


	_KEYSTROKE _keystrokeTable[MAX_RADICAL];
    
    void SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
	void SetupConfiguration(IME_MODE imeMode);
	void SetupKeystroke(IME_MODE imeMode);
	BOOL SetupDictionaryFile(IME_MODE imeMode);
	void SetupCandidateListRange(IME_MODE imeMode);

	void ReleaseDictionaryFiles();
	BOOL SetupHanCovertTable();
	BOOL SetupTCFreqTable();

	//BOOL UpdateDictionaryFile();

	void GetVKeyFromPrintable(WCHAR printable, UINT* vKey, UINT* modifier);

	IME_MODE GetImeModeFromGuidProfile(REFGUID guidLanguageProfile);
	void SetImeMode(REFGUID guidLanguageProfile) { _imeMode = GetImeModeFromGuidProfile(guidLanguageProfile);}
	void SetImeMode(IME_MODE imeMode) {_imeMode = imeMode;}
	IME_MODE GetImeMode() { return _imeMode;}

	_T_RadicalMap* GetRadicalMap(IME_MODE imeMode) {if(_pTableDictionaryEngine[(UINT)imeMode] )
														return _pTableDictionaryEngine[(UINT)imeMode]->GetRadicalMap();
													else return nullptr; }

private:

    BOOL IsVirtualKeyKeystrokeComposition(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function);
    //BOOL IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CDIMEArray<_KEYSTROKE> *pKeystrokeMetric);
    BOOL IsKeystrokeRange(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode);

	
    class XPreservedKey;
    void SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey);
    BOOL InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable);
  


    CFile* GetDictionaryFile() const;

	CDIME* _pTextService;
    
	IME_MODE _imeMode;

    CTableDictionaryEngine* _pTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pCustomTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pPhraseTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayShortCodeTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayPhraseTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayExtBTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayExtCDTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayExtETableDictionaryEngine;
	CTableDictionaryEngine* _pArraySpecialCodeTableDictionaryEngine;
	CTableDictionaryEngine* _pTCSCTableDictionaryEngine;
	CTableDictionaryEngine* _pTCFreqTableDictionaryEngine;

	CFile* _pTableDictionaryFile[IM_SLOTS];
	CFile* _pCustomTableDictionaryFile[IM_SLOTS];
	CFile* _pPhraseDictionaryFile;
	CFile* _pArrayShortCodeDictionaryFile;
	CFile* _pArrayPhraseDictionaryFile;
	CFile* _pArrayExtBDictionaryFile;
	CFile* _pArrayExtCDDictionaryFile;
	CFile* _pArrayExtEDictionaryFile;
	CFile* _pArraySpecialCodeDictionaryFile;
	CFile* _pTCSCTableDictionaryFile;
	CFile* _pTCFreqTableDictionaryFile;

	//void sortListItemByFindWordFreq(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList);

    CStringRange _keystrokeBuffer;

	WCHAR* _pEndkey;

    BOOL _hasWildcardIncludedInKeystrokeBuffer;

    TfClientId  _tfClientId;

    CDIMEArray<_KEYSTROKE> _KeystrokeComposition;

    // Preserved key data
    class XPreservedKey
    {
    public:
        XPreservedKey();
        ~XPreservedKey();
        BOOL UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr);

    public:
        CDIMEArray<TF_PRESERVEDKEY> TSFPreservedKeyTable;
        GUID Guid;
        LPCWSTR Description;
    };

    XPreservedKey _PreservedKey_IMEMode;
    XPreservedKey _PreservedKey_DoubleSingleByte;
	XPreservedKey _PreservedKey_Config;
 
	

    // Configuration data
    //BOOL _isWildcard;
    //BOOL _isDisableWildcardAtFirst;
    BOOL _isKeystrokeSort;
	BOOL _isWildCardWordFreqSort;

	CCandidateRange* _pActiveCandidateListIndexRange;
	CCandidateRange _candidateListIndexRange;
	CCandidateRange _phraseCandidateListIndexRange;
    UINT _candidateListPhraseModifier;
    UINT _candidateWndWidth; 
	UINT _candidatePageSize;

    static const int OUT_OF_FILE_INDEX = -1;
	
	private:  //phonetic composition
	//PHONETIC_KEYBOARD_LAYOUT phoneticKeyboardLayout;
	UINT phoneticSyllable;
	UINT addPhoneticKey(WCHAR* pwch);
	UINT removeLastPhoneticSymbol();
	
	CStringRange buildKeyStrokesFromPhoneticSyllable(UINT syllable);
	WCHAR VPSymbolToStandardLayoutChar(UINT syllable);

	
};
#endif

