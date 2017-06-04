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
    CCompositionProcessorEngine(_In_ CDIME *pTextService);
    ~CCompositionProcessorEngine(void);
	
	

	BOOL IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, UINT candiCount, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL AddVirtualKey(WCHAR wch);
    void RemoveVirtualKey(DWORD_PTR dwIndex);
    void PurgeVirtualKey();

    DWORD_PTR GetVirtualKeyLength() { return _keystrokeBuffer.GetLength(); }
    WCHAR GetVirtualKey(DWORD_PTR dwIndex);

	void GetReadingString(_Inout_ CStringRange *pReadingString, _Inout_opt_ BOOL *pIsWildcardIncluded, _In_opt_ CStringRange *pKeyCode = nullptr);
    void GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch);
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

	
	

	//Dayi Address characters direct input
	BOOL IsDayiAddressChar(WCHAR wch);
	WCHAR GetDayiAddressChar(WCHAR wch);

	//Array short code and special code
	BOOL IsArrayShortCode();
	DWORD_PTR CollectWordFromArraySpeicalCode(_Inout_opt_ const WCHAR **ppwchSpecialCodeResultString);
	BOOL GetArraySpeicalCodeFromConvertedText(_In_ CStringRange *inword, _Inout_opt_ CStringRange *csrResult);
	
    BOOL IsDoubleSingleByte(WCHAR wch);
    BOOL IsWildcard() { return _isWildcard; }
    BOOL IsDisableWildcardAtFirst() { return _isDisableWildcardAtFirst; }
    BOOL IsWildcardChar(WCHAR wch) { return ((IsWildcardOneChar(wch) || IsWildcardAllChar(wch)) ? TRUE : FALSE); }
    BOOL IsWildcardOneChar(WCHAR wch) { return (wch==L'?' ? TRUE : FALSE); }
    BOOL IsWildcardAllChar(WCHAR wch) { return (wch==L'*' ? TRUE : FALSE); }
    BOOL IsKeystrokeSort() { return _isKeystrokeSort; }

    // Dictionary engine
	BOOL IsDictionaryAvailable(IME_MODE imeMode) { return (_pTableDictionaryEngine[imeMode] && _pTableDictionaryEngine[imeMode]->GetRadicalMap()&&!_pTableDictionaryEngine[imeMode] ->GetRadicalMap()->empty() ? TRUE : FALSE);}

  

    inline CCandidateRange *GetCandidateListIndexRange() { return _pActiveCandidateListIndexRange; }
    inline UINT GetCandidateListPhraseModifier() { return _candidateListPhraseModifier; }
    inline UINT GetCandidateWindowWidth() { return _candidateWndWidth; }


	
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
	void SetupConfiguration(IME_MODE imeMode);
	void SetupKeystroke(IME_MODE imeMode);
	BOOL SetupDictionaryFile(IME_MODE imeMode);
	void ReleaseDictionaryFiles();
	BOOL SetupHanCovertTable();
	BOOL SetupTCFreqTable();

	void UpdateDictionaryFile();

	void GetVKeyFromPrintable(WCHAR printable, UINT* vKey, UINT* modifier);

	IME_MODE GetImeModeFromGuidProfile(REFGUID guidLanguageProfile);
	void SetImeMode(REFGUID guidLanguageProfile) { _imeMode = GetImeModeFromGuidProfile(guidLanguageProfile);}
	void SetImeMode(IME_MODE imeMode) {_imeMode = imeMode;}
	IME_MODE GetImeMode() { return _imeMode;}

	_T_RadicalMap* GetRadicalMap(IME_MODE imeMode) {if(_pTableDictionaryEngine[imeMode] ) 
														return _pTableDictionaryEngine[imeMode]->GetRadicalMap();  
													else return nullptr; }

private:

    BOOL IsVirtualKeyKeystrokeComposition(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function);
    BOOL IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CDIMEArray<_KEYSTROKE> *pKeystrokeMetric);
    BOOL IsKeystrokeRange(UINT uCode, _Inout_opt_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode);

	void SetInitialCandidateListRange(IME_MODE imeMode);


    class XPreservedKey;
    void SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey);
    BOOL InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable);
  


    CFile* GetDictionaryFile();

private:

	CDIME* _pTextService;
    
	IME_MODE _imeMode;

    CTableDictionaryEngine* _pTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pCustomTableDictionaryEngine[IM_SLOTS];
	CTableDictionaryEngine* _pPhraseTableDictionaryEngine;
	CTableDictionaryEngine* _pArrayShortCodeTableDictionaryEngine;
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
	CFile* _pArrayExtBDictionaryFile;
	CFile* _pArrayExtCDDictionaryFile;
	CFile* _pArrayExtEDictionaryFile;
	CFile* _pArraySpecialCodeDictionaryFile;
	CFile* _pTCSCTableDictionaryFile;
	CFile* _pTCFreqTableDictionaryFile;

	//void sortListItemByFindWordFreq(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList);

    CStringRange _keystrokeBuffer;

    BOOL _hasWildcardIncludedInKeystrokeBuffer;

    TfClientId  _tfClientId;

    CDIMEArray<_KEYSTROKE> _KeystrokeComposition;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidate;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidateWildcard;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidateSymbol;


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
    BOOL _isWildcard;
    BOOL _isDisableWildcardAtFirst;
    BOOL _isKeystrokeSort;
	BOOL _isWildCardWordFreqSort;
	CCandidateRange* _pActiveCandidateListIndexRange;
	CCandidateRange _candidateListIndexRange;
	CCandidateRange _phraseCandidateListIndexRange;
    UINT _candidateListPhraseModifier;
    UINT _candidateWndWidth; 

    static const int OUT_OF_FILE_INDEX = -1;
	
	private:  //phonetic composition
	//PHONETIC_KEYBOARD_LAYOUT phoneticKeyboardLayout;
	UINT phoneticSyllable;
	UINT addPhoneticKey(WCHAR* pwch);
	UINT removeLastPhoneticSymbol();
	
	CStringRange buildKeyStrokesFromPhoneticSyllable(UINT syllable);
	WCHAR VPSymbolToStandardLayoutChar(UINT syllable);

	public:
		//Phonetic composingkey
	BOOL isPhoneticComposingKey();
};
#endif

