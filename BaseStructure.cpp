//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Globals.h"

//---------------------------------------------------------------------
//
// CLSIDToString
//
//---------------------------------------------------------------------

const BYTE GuidSymbols[] = {
    3, 2, 1, 0, '-', 5, 4, '-', 7, 6, '-', 8, 9, '-', 10, 11, 12, 13, 14, 15
};

const WCHAR HexDigits[] = L"0123456789ABCDEF";

BOOL CLSIDToString(REFGUID refGUID, _Out_writes_(39) WCHAR *pCLSIDString)
{
    WCHAR* pTemp = pCLSIDString;
    const BYTE* pBytes = (const BYTE *) &refGUID;

    DWORD j = 0;
    pTemp[j++] = L'{';
    for (int i = 0; i < sizeof(GuidSymbols) && j < (CLSID_STRLEN - 2); i++)
    {
        if (GuidSymbols[i] == '-')
        {
            pTemp[j++] = L'-';
        }
        else
        {
            pTemp[j++] = HexDigits[ (pBytes[GuidSymbols[i]] & 0xF0) >> 4 ];
            pTemp[j++] = HexDigits[ (pBytes[GuidSymbols[i]] & 0x0F) ];
        }
    }

    pTemp[j++] = L'}';
    pTemp[j] = L'\0';

    return TRUE;
}

//---------------------------------------------------------------------
//
// SkipWhiteSpace
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

HRESULT SkipWhiteSpace(LCID locale, _In_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ DWORD_PTR *pdwIndex)
{
    DWORD_PTR index = 0;

    *pdwIndex = 0;
    while (*pwszBuffer && IsSpace(locale, *pwszBuffer) && dwBufLen)
    {
        dwBufLen--;
        pwszBuffer++;
        index++;
    }

    if (*pwszBuffer && dwBufLen)
    {
        *pdwIndex = index;
        return S_OK;
    }

    return E_FAIL;
}

//---------------------------------------------------------------------
//
// FindChar
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

HRESULT FindChar(WCHAR wch, _In_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ DWORD_PTR *pdwIndex)
{
	if(pwszBuffer == NULL) return E_FAIL;
    DWORD_PTR index = 0;

    *pdwIndex = 0;
    while (*pwszBuffer && (*pwszBuffer != wch) && dwBufLen)
    {
        dwBufLen--;
        pwszBuffer++;
        index++;
    }

    if (*pwszBuffer && dwBufLen)
    {
        *pdwIndex = index;
        return S_OK;
    }

    return E_FAIL;
}

//---------------------------------------------------------------------
//
// IsSpace
//
//---------------------------------------------------------------------

BOOL IsSpace(LCID locale, WCHAR wch)
{
    WORD wCharType = 0;

    GetStringTypeEx(locale, CT_CTYPE1, &wch, 1, &wCharType);
    return (wCharType & C1_SPACE);
}

CStringRange::CStringRange()
{
    _stringBufLen = 0;
    _pStringBuf = nullptr;
}

CStringRange::~CStringRange() 
{ 
}

const WCHAR *CStringRange::Get() const
{
    return _pStringBuf;
}

const DWORD_PTR CStringRange::GetLength() const
{
    return _stringBufLen;
}

void CStringRange::Clear()
{
    _stringBufLen = 0;
    _pStringBuf = nullptr;
}

CStringRange& CStringRange::Set(const WCHAR *pwch, DWORD_PTR dwLength)
{
    _stringBufLen = dwLength;
    _pStringBuf = pwch;
	return *this;
}

CStringRange& CStringRange::Set(CStringRange &sr)
{
    *this = sr;
	return *this;
}

CStringRange& CStringRange::operator =(const CStringRange& sr)
{
    _stringBufLen = sr._stringBufLen;
    _pStringBuf = sr._pStringBuf;
    return *this;
}

void CStringRange::CharNext(_Inout_ CStringRange* pCharNext)
{
    if (!_stringBufLen)
    {
        pCharNext->_stringBufLen = 0;
        pCharNext->_pStringBuf = nullptr;
    }
    else
    {
        pCharNext->_stringBufLen = _stringBufLen;
        pCharNext->_pStringBuf = _pStringBuf;

        while (pCharNext->_stringBufLen)
        {
            BOOL isSurrogate = (IS_HIGH_SURROGATE(*pCharNext->_pStringBuf) || IS_LOW_SURROGATE(*pCharNext->_pStringBuf));
            pCharNext->_stringBufLen--;
            pCharNext->_pStringBuf++;
            if (!isSurrogate)
            {
                break;
            }
        }
    }
}

int CStringRange::Compare(LCID locale, _In_ CStringRange* pString1, _In_ CStringRange* pString2)
{
    return CompareString(locale, 
        NORM_IGNORECASE, 
        pString1->Get(), 
        (DWORD)pString1->GetLength(), 
        pString2->Get(), 
        (DWORD)pString2->GetLength());
}

BOOL CStringRange::WildcardCompare(LCID locale, _In_ CStringRange* stringWithWildcard, _In_ CStringRange* targetString)
{
    if (stringWithWildcard->GetLength() == 0)
    {
        return targetString->GetLength() == 0 ? TRUE : FALSE;
    }

    CStringRange stringWithWildcard_next;
    CStringRange targetString_next;
    stringWithWildcard->CharNext(&stringWithWildcard_next);
    targetString->CharNext(&targetString_next);

    if (*stringWithWildcard->Get() == L'*')
    {
        return WildcardCompare(locale, &stringWithWildcard_next, targetString) || ((targetString->GetLength() != 0) && WildcardCompare(locale, stringWithWildcard, &targetString_next));
    }
    if (*stringWithWildcard->Get() == L'?')
    {
        return ((targetString->GetLength() != 0) && WildcardCompare(locale, &stringWithWildcard_next, &targetString_next));
    }

    BOOL isSurrogate1 = (IS_HIGH_SURROGATE(*stringWithWildcard->Get()) || IS_LOW_SURROGATE(*stringWithWildcard->Get()));
    BOOL isSurrogate2 = (IS_HIGH_SURROGATE(*targetString->Get()) || IS_LOW_SURROGATE(*targetString->Get()));

    return ((CompareString(locale,
        NORM_IGNORECASE,
        stringWithWildcard->Get(),
        (isSurrogate1 ? 2 : 1),
        targetString->Get(),
        (isSurrogate2 ? 2 : 1)) == CSTR_EQUAL) && WildcardCompare(locale, &stringWithWildcard_next, &targetString_next));
}

CCandidateRange::CCandidateRange(void)
{
}


CCandidateRange::~CCandidateRange(void)
{
}


BOOL CCandidateRange::IsRange(UINT vKey, WCHAR Printable, UINT Modifiers, CANDIDATE_MODE candidateMode)
{
	debugPrint(L"CCandidateRange::IsRange(): vKey = %d, Modifiers = %d, candiMode = %d. ", vKey, Modifiers, candidateMode);
	if (candidateMode != CANDIDATE_NONE)
	{
		for (UINT i = 0; i < _CandidateListIndexRange.Count(); i++)
		{
			if (Printable == _CandidateListIndexRange.GetAt(i)->Printable)
			{
                if (i == 0 && vKey == VK_SPACE && CConfig::GetSpaceAsPageDown())
                    return FALSE;
                else
				    return TRUE;
			}
			else if ((VK_NUMPAD0 <= vKey) && (vKey <= VK_NUMPAD9))
			{
				if ((vKey - VK_NUMPAD0) == _CandidateListIndexRange.GetAt(i)->Index)
				{
					return TRUE;
				}
			}
		}
	}
	return FALSE;

}

int CCandidateRange::GetIndex(UINT vKey, WCHAR Printable, CANDIDATE_MODE candidateMode)
{
	debugPrint(L"CCandidateRange::GetIndex(): vKey = %d, candidateMode = %d ", vKey, candidateMode);
	if(candidateMode != CANDIDATE_NONE)
	{
		for (UINT i = 0; i < _CandidateListIndexRange.Count(); i++)
		{
			if (Printable == _CandidateListIndexRange.GetAt(i)->Printable)
			{
				return i;
			}
			else if ((VK_NUMPAD0 <= vKey) && (vKey <= VK_NUMPAD9))
			{
				if ((vKey - VK_NUMPAD0) == _CandidateListIndexRange.GetAt(i)->Index)
				{
					return i;
				}
			}
		}
	}
	return -1;

}


