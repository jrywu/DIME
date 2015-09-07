//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef DIMEBASESTRUCTURE_H
#define DIMEBASESTRUCTURE_H

#pragma once
#include <vector>
#include <map>
#include <iostream>
#include "stdafx.h"
#include "assert.h"



using std::cout;
using std::endl;
using std::map;

typedef  map <WCHAR, PWCH> _T_RadicalMap;
typedef  map <WCHAR, DWORD_PTR> _T_RadicalIndexMap;

//---------------------------------------------------------------------
// defined keyword
//---------------------------------------------------------------------
template<class VALUE>
struct _DEFINED_KEYWORD
{
    LPCWSTR _pwszKeyword;
    VALUE _value;
};


struct _DAYI_ADDRESS_DIRECT_INPUT
{
    WCHAR _Code;
    WCHAR _AddressChar;
};

//---------------------------------------------------------------------
// enum
//---------------------------------------------------------------------

enum DICTIONARY_TYPE
{
	TTS_DICTIONARY,
	CIN_DICTIONARY,
	LIME_DICTIONARY
};

enum KEYSTROKE_CATEGORY
{
    CATEGORY_NONE = 0,
    CATEGORY_COMPOSING,
    CATEGORY_CANDIDATE,
    CATEGORY_PHRASE,
    CATEGORY_PHRASEFROMKEYSTROKE,
    CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION
};

enum KEYSTROKE_FUNCTION
{
    FUNCTION_NONE = 0,
    FUNCTION_INPUT,

    FUNCTION_CANCEL,
    FUNCTION_FINALIZE_TEXTSTORE,
    FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT,
    FUNCTION_FINALIZE_CANDIDATELIST,
    FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT,
    FUNCTION_CONVERT,
    FUNCTION_CONVERT_WILDCARD,
    FUNCTION_SELECT_BY_NUMBER,
    FUNCTION_BACKSPACE,
    FUNCTION_MOVE_LEFT,
    FUNCTION_MOVE_RIGHT,
    FUNCTION_MOVE_UP,
    FUNCTION_MOVE_DOWN,
    FUNCTION_MOVE_PAGE_UP,
    FUNCTION_MOVE_PAGE_DOWN,
    FUNCTION_MOVE_PAGE_TOP,
    FUNCTION_MOVE_PAGE_BOTTOM,

    // Function Double/Single byte
    FUNCTION_DOUBLE_SINGLE_BYTE,

    // Function Punctuation
    FUNCTION_ADDRESS_DIRECT_INPUT
};

//---------------------------------------------------------------------
// candidate list
//---------------------------------------------------------------------
enum CANDIDATE_MODE
{
    CANDIDATE_NONE = 0,
    CANDIDATE_ORIGINAL,
    CANDIDATE_PHRASE,
    CANDIDATE_INCREMENTAL,
    CANDIDATE_WITH_NEXT_COMPOSITION
};

//---------------------------------------------------------------------
// IME MODE
//---------------------------------------------------------------------
enum IME_MODE
{
    IME_MODE_DAYI = 0,
	IME_MODE_ARRAY,
	IME_MODE_PHONETIC,
	IME_MODE_GENERIC,
	IME_MODE_NONE
};

//---------------------------------------------------------------------
// SEARCH SECTION
//---------------------------------------------------------------------
enum SEARCH_SECTION
{
    SEARCH_SECTION_TEXT = 0,
	SEARCH_SECTION_SYMBOL,
	SEARCH_SECTION_PHRASE,
	SEARCH_SECTION_PRHASE_FROM_KEYSTROKE
};

//---------------------------------------------------------------------
// NOTIFY_TYPE
//---------------------------------------------------------------------
enum NOTIFY_TYPE
{
	NOTIFY_CHN_ENG,
	NOTIFY_SINGLEDOUBLEBYTE,
	NOTIFY_BEEP,
	NOTIFY_OTHERS
};

//---------------------------------------------------------------------
// structure
//---------------------------------------------------------------------
struct _KEYSTROKE_STATE
{
    KEYSTROKE_CATEGORY Category;
    KEYSTROKE_FUNCTION Function;
};


BOOL CLSIDToString(REFGUID refGUID, _Out_writes_ (39) WCHAR *pCLSIDString);

HRESULT SkipWhiteSpace(LCID locale, _In_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ DWORD_PTR *pdwIndex);
HRESULT FindChar(WCHAR wch, _In_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ DWORD_PTR *pdwIndex);

BOOL IsSpace(LCID locale, WCHAR wch);

template<class T>
class CDIMEArray

{
    typedef typename std::vector<T> CDIMEInnerArray;
    typedef typename std::vector<T>::iterator CDIMEInnerIter;

public:
    CDIMEArray(): _innerVect()
    {
    }

    explicit CDIMEArray(size_t count): _innerVect(count)
    {
    }

    virtual ~CDIMEArray()
    {
    }

    inline T* GetAt(size_t index)
    {
        assert(index >= 0);
        assert(index < _innerVect.size());

        T& curT = _innerVect.at(index);

        return &(curT);
    }

    inline const T* GetAt(size_t index) const
    {
        assert(index >= 0);
        assert(index < _innerVect.size());

        T& curT = _innerVect.at(index);

        return &(curT);
    }

    void RemoveAt(size_t index)
    {
        assert(index >= 0);
        assert(index < _innerVect.size());

        CDIMEInnerIter iter = _innerVect.begin();
        _innerVect.erase(iter + index);
    }

    UINT Count() const 
    { 
        return static_cast<UINT>(_innerVect.size());
    }

    T* Append()
    {
        T newT;
        _innerVect.push_back(newT);
        T& backT = _innerVect.back();

        return &(backT);
    }

    void reserve(size_t Count)
    {
        _innerVect.reserve(Count);
    }

    void Clear()
    {
        _innerVect.clear();
    }

private:
    CDIMEInnerArray _innerVect;
};

class CCandidateRange
{
public:
    CCandidateRange(void);
    ~CCandidateRange(void);

    BOOL IsRange(UINT vKey, CANDIDATE_MODE candidateMode);
    int GetIndex(UINT vKey, CANDIDATE_MODE candidateMode);

    inline int Count() const 
    { 
        return _CandidateListIndexRange.Count(); 
    }
    inline DWORD *GetAt(int index) 
    { 
        return _CandidateListIndexRange.GetAt(index); 
    }
    inline DWORD *Append() 
    { 
        return _CandidateListIndexRange.Append(); 
    }
	inline void *Clear() 
    { 
		_CandidateListIndexRange.Clear(); 
		return nullptr;
    }
private:
    CDIMEArray<DWORD> _CandidateListIndexRange;
};

class CStringRange
{
public:
    CStringRange();
    ~CStringRange();

    const WCHAR *Get() const;
    const DWORD_PTR GetLength() const;
    void Clear();
    CStringRange& Set(const WCHAR *pwch, DWORD_PTR dwLength);
    CStringRange& Set(CStringRange &sr);
    CStringRange& operator=(const CStringRange& sr);
    void CharNext(_Inout_ CStringRange* pCharNext);
    static int Compare(LCID locale, _In_ CStringRange* pString1, _In_ CStringRange* pString2);
    static BOOL WildcardCompare(LCID locale, _In_ CStringRange* stringWithWildcard, _In_ CStringRange* targetString);

protected:
    DWORD_PTR _stringBufLen;         // Length is in character count.
    const WCHAR *_pStringBuf;    // Buffer which is not add zero terminate.
};

//---------------------------------------------------------------------
// CCandidateListItem
//	_ItemString - candidate string
//	_FindKeyCode - tailing string
//---------------------------------------------------------------------
struct CCandidateListItem
{
    CStringRange _ItemString;
    CStringRange _FindKeyCode;

	CCandidateListItem& CCandidateListItem::operator =( const CCandidateListItem& rhs)
	{
		_ItemString = rhs._ItemString;
		_FindKeyCode = rhs._FindKeyCode;
		return *this;
	}
};

struct LanguageProfileInfo
{
	CLSID	clsid;
	GUID	guidProfile;
	PWCH	description;
};
#endif
