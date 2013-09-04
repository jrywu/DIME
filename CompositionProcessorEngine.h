//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

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
    CCompositionProcessorEngine(_In_ CTSFTTS *pTextService);
    ~CCompositionProcessorEngine(void);
	
	

    BOOL IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, _Out_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL AddVirtualKey(WCHAR wch);
    void RemoveVirtualKey(DWORD_PTR dwIndex);
    void PurgeVirtualKey();

    DWORD_PTR GetVirtualKeyLength() { return _keystrokeBuffer.GetLength(); }
    WCHAR GetVirtualKey(DWORD_PTR dwIndex);

    void GetReadingStrings(_Inout_ CTSFTTSArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded);
    void GetCandidateList(_Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch);
    void GetCandidateStringInConverted(CStringRange &searchString, _In_ CTSFTTSArray<CCandidateListItem> *pCandidateList);

	//reverse converion
	HRESULT GetReverConversionResults(IME_MODE imeMode, _In_ LPCWSTR lpstrToConvert, _Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList);

	//Han covert
	BOOL GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString);
	BOOL GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString);

    // Preserved key handler
    void OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    // Symbol mode
    BOOL IsSymbolChar(WCHAR wch);
	BOOL IsSymbol();

	//Dayi Address characters direct input
	BOOL IsDayiAddressChar(WCHAR wch);
	WCHAR GetDayiAddressChar(WCHAR wch);

	//Array short code and special code
	BOOL IsArrayShortCode();
	DWORD_PTR CollectWordFromArraySpeicalCode(_Outptr_result_maybenull_ const WCHAR **ppwchSpecialCodeResultString);
	BOOL GetArraySpeicalCodeFromConvertedText(_In_ CStringRange *inword, _Out_ CStringRange *csrResult);
	
    BOOL IsDoubleSingleByte(WCHAR wch);
    BOOL IsWildcard() { return _isWildcard; }
    BOOL IsDisableWildcardAtFirst() { return _isDisableWildcardAtFirst; }
    BOOL IsWildcardChar(WCHAR wch) { return ((IsWildcardOneChar(wch) || IsWildcardAllChar(wch)) ? TRUE : FALSE); }
    BOOL IsWildcardOneChar(WCHAR wch) { return (wch==L'?' ? TRUE : FALSE); }
    BOOL IsWildcardAllChar(WCHAR wch) { return (wch==L'*' ? TRUE : FALSE); }
    BOOL IsKeystrokeSort() { return _isKeystrokeSort; }

    // Dictionary engine
    BOOL IsDictionaryAvailable() { return (_pTableDictionaryEngine ? TRUE : FALSE); }

  

    inline CCandidateRange *GetCandidateListIndexRange() { return &_candidateListIndexRange; }
    inline UINT GetCandidateListPhraseModifier() { return _candidateListPhraseModifier; }
    inline UINT GetCandidateWindowWidth() { return _candidateWndWidth; }

	// Play system warning beep
	void DoBeep();

	
	struct _KEYSTROKE
    {
		WCHAR Printable;
        UINT VirtualKey;
        UINT Modifiers;
        KEYSTROKE_FUNCTION Function;

        _KEYSTROKE()
        {
			Printable = '\0';
            VirtualKey = 0;
            Modifiers = 0;
            Function = FUNCTION_NONE;
        }
    };
	_KEYSTROKE _keystrokeTable[MAX_RADICAL];
    
    void SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void SetupConfiguration();
	void SetupKeystroke(IME_MODE imeMode);
	BOOL SetupDictionaryFile(IME_MODE imeMode);
	void ReleaseDictionaryFiles();
	BOOL SetupHanCovertTable();

	void UpdateDictionaryFile();

	void GetVKeyFromPrintable(WCHAR printable, UINT* vKey, UINT* modifier);

	IME_MODE GetImeModeFromGuidProfile(REFGUID guidLanguageProfile);
	_T_RacialMap* GetRadicalMap(IME_MODE imeMode) {return _pTableDictionaryEngine[imeMode]->GetRadicalMap();  }

private:

    BOOL IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function);
    BOOL IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CTSFTTSArray<_KEYSTROKE> *pKeystrokeMetric);
    BOOL IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode);

    void SetInitialCandidateListRange();


    class XPreservedKey;
    void SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey);
    BOOL InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL CheckShiftKeyOnly(_In_ CTSFTTSArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable);
  


    CFile* GetDictionaryFile();

private:

	CTSFTTS* _pTextService;
    

    CTableDictionaryEngine* _pTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pTTSTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pCINTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pArrayShortCodeTableDictionaryEngine;
	CTableDictionaryEngine* _pArraySpecialCodeTableDictionaryEngine;
	CTableDictionaryEngine* _pTCSCTableDictionaryEngine;

	CFile* _pTTSDictionaryFile[IM_SLOTS];
	CFile* _pCINDictionaryFile[IM_SLOTS];
	CFile* _pArrayShortCodeDictionaryFile;
	CFile* _pArraySpecialCodeDictionaryFile;
	CFile* _pTCSCTableDictionaryFile;


    CStringRange _keystrokeBuffer;

    BOOL _hasWildcardIncludedInKeystrokeBuffer;

    TfClientId  _tfClientId;

    CTSFTTSArray<_KEYSTROKE> _KeystrokeComposition;
    CTSFTTSArray<_KEYSTROKE> _KeystrokeCandidate;
    CTSFTTSArray<_KEYSTROKE> _KeystrokeCandidateWildcard;
    CTSFTTSArray<_KEYSTROKE> _KeystrokeCandidateSymbol;
    CTSFTTSArray<_KEYSTROKE> _KeystrokeSymbol;

    // Preserved key data
    class XPreservedKey
    {
    public:
        XPreservedKey();
        ~XPreservedKey();
        BOOL UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr);

    public:
        CTSFTTSArray<TF_PRESERVEDKEY> TSFPreservedKeyTable;
        GUID Guid;
        LPCWSTR Description;
    };

    XPreservedKey _PreservedKey_IMEMode;
    XPreservedKey _PreservedKey_DoubleSingleByte;
	XPreservedKey _PreservedKey_Config;
 


    // Configuration data
    BOOL _isWildcard : 1;
    BOOL _isDisableWildcardAtFirst : 1;
    BOOL _isKeystrokeSort : 1;
	CCandidateRange _candidateListIndexRange;
    UINT _candidateListPhraseModifier;
    UINT _candidateWndWidth; 

    static const int OUT_OF_FILE_INDEX = -1;
};
#endif

