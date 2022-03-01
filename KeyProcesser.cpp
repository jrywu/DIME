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
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "PhoneticComposition.h"

//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
	debugPrint(L"CCompositionProcessorEngine::AddVirtualKey()");
	if (!wch)
	{
		return FALSE;
	}
	if (Global::imeMode == IME_MODE_PHONETIC)
	{
		UINT oldSyllable = phoneticSyllable;
		//DWORD_PTR len = _keystrokeBuffer.GetLength();

		if (oldSyllable != addPhoneticKey(&wch))
		{
			CStringRange csr = buildKeyStrokesFromPhoneticSyllable(phoneticSyllable);
			if (csr.GetLength()) {
				if (_keystrokeBuffer.Get())
				{
					delete[] _keystrokeBuffer.Get();
				}

				_keystrokeBuffer = csr;
			}
			return TRUE;
		}
		else
		{
			_pTextService->DoBeep(BEEP_COMPOSITION_ERROR);
			return FALSE;
		}

	}
	else
	{

		if ((UINT)_keystrokeBuffer.GetLength() >= CConfig::GetMaxCodes())
		{
			_pTextService->DoBeep(BEEP_WARNING); // do not eat the key if keystroke buffer length >= _maxcodes
			return FALSE;
		}
		//
		// append one keystroke in buffer.
		//
		DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
		PWCHAR pwch = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
		if (!pwch)
		{
			return FALSE;
		}

		memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
		pwch[srgKeystrokeBufLen] = wch;

		if (_keystrokeBuffer.Get())
		{
			delete[] _keystrokeBuffer.Get();
		}

		_keystrokeBuffer.Set(pwch, srgKeystrokeBufLen + 1);

		return TRUE;
	}
}

//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
	debugPrint(L"CCompositionProcessorEngine::RemoveVirtualKey()");
	DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

	if (dwIndex + 1 < srgKeystrokeBufLen)
	{
		// shift following eles left
		memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
			(BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
			(srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
	}

	_keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);	if (srgKeystrokeBufLen == 1) // no more virtual keys in buffer after backspace
	{
		_hasWildcardIncludedInKeystrokeBuffer = FALSE;
	}


	if (Global::imeMode == IME_MODE_PHONETIC)
	{
		phoneticSyllable = removeLastPhoneticSymbol();
	}
}

//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
	debugPrint(L"CCompositionProcessorEngine::PurgeVirtualKey()");
	if (_keystrokeBuffer.Get())
	{
		delete[] _keystrokeBuffer.Get();
		_keystrokeBuffer.Set(NULL, 0);

	}
	if (Global::imeMode == IME_MODE_PHONETIC) phoneticSyllable = 0;
	_hasWildcardIncludedInKeystrokeBuffer = FALSE;
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex)
{
	debugPrint(L"CCompositionProcessorEngine::GetVirtualKey()");
	if (dwIndex < _keystrokeBuffer.GetLength())
	{
		return *(_keystrokeBuffer.Get() + dwIndex);
	}
	return 0;
}

BOOL CCompositionProcessorEngine::isPhoneticComposingKey()
{
	debugPrint(L"CCompositionProcessorEngine::isPhoneticComposingKey()");
	if (_keystrokeBuffer.Get() == nullptr || Global::imeMode != IME_MODE_PHONETIC)
		return FALSE;
	UINT len = (UINT)_keystrokeBuffer.GetLength();
	if (len == 0)
		return FALSE;
	WCHAR wch = *(_keystrokeBuffer.Get() + len - 1);

	if (wch == '3' || wch == '4' || wch == '6' || wch == '7')
		return TRUE;

	return FALSE;

}

UINT CCompositionProcessorEngine::addPhoneticKey(WCHAR* pwch)
{
	debugPrint(L"CCompositionProcessorEngine::addPhoneticKey()");
	WCHAR c = towupper(*pwch);
	if (c < 32 || c > MAX_RADICAL + 32) return phoneticSyllable;

	UINT s, m;
	if (CConfig::getPhoneticKeyboardLayout() == PHONETIC_ETEN_KEYBOARD_LAYOUT)
		c = vpEtenKeyToStandardKeyTable[c - 32];

	s = vpStandardKeyTable[(c == 0) ? 0 : c - 32];


	if (s == vpAny)
	{
		if (phoneticSyllable == 0)
			phoneticSyllable |= vpAnyConsonant; //no syllable present, add wildcard as vpAnyConsonant.
		else if ((phoneticSyllable & vpConsonantMask) && !(phoneticSyllable & (vpMiddleVowelMask | vpVowelMask | vpToneMask)))
			phoneticSyllable |= vpAnyMiddleVowel; // consonant is present and no middle vowel present, add wildcard as vpAnyMiddleVowel
		else if (phoneticSyllable & (vpMiddleVowelMask) && !(phoneticSyllable & (vpVowelMask | vpToneMask)))
			phoneticSyllable |= vpAnyTone;
		else if (phoneticSyllable & (vpMiddleVowelMask | vpToneMask) && !(phoneticSyllable & vpVowelMask))
			phoneticSyllable |= vpAnyVowel;
		else if (phoneticSyllable & (vpMiddleVowelMask | vpVowelMask) && !(phoneticSyllable & vpToneMask))
			phoneticSyllable |= vpAnyTone;

		return phoneticSyllable;
	}


	if ((m = (s & vpToneMask)) != 0 && phoneticSyllable != 0) // do not add tones to empty syllable
		phoneticSyllable = (phoneticSyllable & (~vpToneMask)) | m;
	else
	{
		UINT oldSyllable = phoneticSyllable;
		if ((m = (s & vpVowelMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpVowelMask)) | m;
		else if ((m = (s & vpMiddleVowelMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpMiddleVowelMask)) | m;
		else if ((m = (s & vpConsonantMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpConsonantMask)) | m;

		if ((phoneticSyllable & vpToneMask) && oldSyllable != phoneticSyllable)
			phoneticSyllable = phoneticSyllable & (~vpToneMask);// clear tones if it's already present

	}

	return phoneticSyllable;

}
WCHAR CCompositionProcessorEngine::VPSymbolToStandardLayoutChar(UINT syllable)
{
	debugPrint(L"CCompositionProcessorEngine::VPSymbolToStandardLayoutChar()");
	if (syllable == vpAnyConsonant || syllable == vpAnyMiddleVowel || syllable == vpAnyVowel || syllable == vpAnyTone)
		return '?';
	UINT oridinal = 0, ss;
	if ((ss = (syllable & vpConsonantMask)) != 0)
		oridinal = ss;
	else if ((ss = (syllable & vpMiddleVowelMask)) != 0)
		oridinal = (ss >> 5) + 21;
	else if ((ss = (syllable & vpVowelMask)) != 0)
		oridinal = (ss >> 8) + 24;
	else if ((ss = (syllable & vpToneMask)) != 0)
		oridinal = (ss >> 12) + 37;

	if (oridinal > 41) return 0;
	return vpStandardLayoutCode[oridinal];
}
UINT CCompositionProcessorEngine::removeLastPhoneticSymbol()
{
	debugPrint(L"CCompositionProcessorEngine::removeLastPhoneticSymbol()");

	if (phoneticSyllable & vpToneMask)
		return (phoneticSyllable & ~vpToneMask);
	else if (phoneticSyllable & vpVowelMask)
		return (phoneticSyllable & ~vpVowelMask);
	else if (phoneticSyllable & vpMiddleVowelMask)
		return (phoneticSyllable & ~vpMiddleVowelMask);
	else
		return 0;
}
//+---------------------------------------------------------------------------
//
// IsSymbol
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbol()
{
	debugPrint(L"CCompositionProcessorEngine::IsSymbol()");

	if (_keystrokeBuffer.Get() == nullptr) 
		return FALSE;
	
	WCHAR c = *_keystrokeBuffer.Get();
	UINT len = (UINT) _keystrokeBuffer.GetLength();

	if (Global::imeMode == IME_MODE_DAYI && (len < 3 && c == L'='))
		return TRUE;
	
	if (Global::imeMode == IME_MODE_ARRAY && len==2)
	{
		WCHAR c2 = *(_keystrokeBuffer.Get() + 1);
		if (CConfig::GetArrayScope() != ARRAY40_BIG5 && towupper(c) == L'W' && c2 <= L'9' && c2 >= L'0')
			return TRUE;
		if (CConfig::GetArrayScope() == ARRAY40_BIG5
			&& (towupper(c) == L'H' || c == L'8') && (c2 == L'\'' || c2 == L'[' || c2 == L']' || c2 == L'-' || c2 == L'='))
			return TRUE;

	}
	
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsSymbolChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolChar(WCHAR wch)
{
	debugPrint(L"CCompositionProcessorEngine::IsSymbolChar()");
	if (_keystrokeBuffer.Get() == nullptr) 
		return FALSE;
	
	if ((_keystrokeBuffer.GetLength() == 1) &&
		(*_keystrokeBuffer.Get() == L'=') &&
		Global::imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[IME_MODE_DAYI])
	{
		for (UINT i = 0; i < wcslen(Global::DayiSymbolCharTable); i++)
		{
			if (Global::DayiSymbolCharTable[i] == wch)
				return TRUE;
		}

	}
	if ((_keystrokeBuffer.GetLength() == 1) && Global::imeMode == IME_MODE_ARRAY)
	{
		WCHAR c = *_keystrokeBuffer.Get();
		if( CConfig::GetArrayScope() != ARRAY40_BIG5 && (towupper(c)== L'W') &&
			(wch >= L'0' && wch <= L'9') )
			return TRUE;
		if (CConfig::GetArrayScope() == ARRAY40_BIG5
			&& (towupper(c) == L'H' || c == L'8') &&
			(wch == L'\'' || wch == L'[' || wch == L']' || wch == L'-' || wch == L'=') )
			return TRUE;

	}
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsDayiAddressChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsDayiAddressChar(WCHAR wch)
{
	debugPrint(L"CCompositionProcessorEngine::IsDayiAddressChar()");
	if (Global::imeMode != IME_MODE_DAYI)
		return FALSE;

	for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
	{
		if (Global::dayiAddressCharTable[i]._Code == wch)
		{
			return TRUE;
		}
	}

	return FALSE;
}


//+---------------------------------------------------------------------------
//
// IsArrayShortCode
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsArrayShortCode()
{
	debugPrint(L"CCompositionProcessorEngine::IsArrayShortCode()");
	if (Global::imeMode == IME_MODE_ARRAY && CConfig::GetArrayScope()!=ARRAY40_BIG5 &&
		_keystrokeBuffer.GetLength() < 3) 
		return TRUE;
	else
		return FALSE;
}


//+---------------------------------------------------------------------------
//
// IsDoubleSingleByte
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsDoubleSingleByte(WCHAR wch)
{
	debugPrint(L"CCompositionProcessorEngine::IsDoubleSingleByte()");
	if (L' ' <= wch && wch <= L'~')
	{
		return TRUE;
	}
	return FALSE;
}



//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//	   [in] hasCandidateWithWildcard - Specified if candi with wildcard
//     [in] candiCount     - The number of candiddates in candi window
//     [in] candiSelection - The current selected index in candi window
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR* pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, UINT candiCount, INT candiSelection, _Inout_opt_ _KEYSTROKE_STATE* pKeyState)
{
	debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyNeed() uCode = %d, pKeyState = %d, fComposing = %d, candidateMode = %d, hasCandidateWithWildcard = %d, candiCount = %d", uCode, pKeyState, fComposing, candidateMode, hasCandidateWithWildcard, candiCount);
	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE)//|| candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		fComposing = FALSE;
	}
	
	//Processing end keys, array/dayi symbol mode, and dayi address input ---------------------------------------------------------
	if (pKeyState)
	{
		pKeyState->Category = CATEGORY_NONE;
		pKeyState->Function = FUNCTION_NONE;
	}
	
	// Symbol mode start with L'=' for dayi or L'w' for array
	if (pKeyState && IsSymbolChar(*pwch))
	{
		pKeyState->Category = CATEGORY_COMPOSING;
		pKeyState->Function = FUNCTION_INPUT;
		return TRUE;
	}
	// Address characters direct input mode  "'[]-\"
	if (pKeyState && IsDayiAddressChar(*pwch) && candidateMode != CANDIDATE_ORIGINAL)
	{
		pKeyState->Category = CATEGORY_COMPOSING;
		pKeyState->Function = FUNCTION_ADDRESS_DIRECT_INPUT;
		return TRUE;
	}

	//Processing cand keys ------------------------------------------------------------------------------
	
	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE)// || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		switch (uCode)
		{
			case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
			case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
			case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
			case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;
			case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
			case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
			case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
			case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
			case VK_RETURN: if (pKeyState)
			{
				if (candidateMode == CANDIDATE_PHRASE && candiSelection == -1)
				{
					pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
					pKeyState->Function = FUNCTION_CANCEL;
					return FALSE;
				}
				else
				{
					pKeyState->Category = CATEGORY_CANDIDATE;
					pKeyState->Function = FUNCTION_CONVERT;
					return TRUE;
				}
			}

			case VK_SPACE:  if (pKeyState)
			{
				/*if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION) // space finalized the associate here instead of choose the first one (selected index = -1 for phrase candidates).
				{
					pKeyState->Category = CATEGORY_CANDIDATE;
					pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST;
				}
				else  */
				if (candidateMode != CANDIDATE_PHRASE
					&& CConfig::GetSpaceAsPageDown() && (candiCount > _candidatePageSize))
				{
					pKeyState->Category = CATEGORY_CANDIDATE;
					pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN;
				}
				else if (candidateMode == CANDIDATE_PHRASE && candiSelection == -1)
				{
					pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
					pKeyState->Function = FUNCTION_CANCEL;
					return FALSE;
				}
				else
				{
					pKeyState->Category = CATEGORY_CANDIDATE;
					pKeyState->Function = FUNCTION_CONVERT;
				}
				return TRUE;
			}
			case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
			case VK_INSERT:
			case VK_DELETE:
			case VK_ESCAPE: if (pKeyState)
			{
				/*if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
				{
					pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
					pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
					return TRUE;
				}
				else
				{*/
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_CANCEL;
				return TRUE;
				// }
			}


		}
		
		if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
		{
			return TRUE;
		}
		if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_INPUT))
		{
			if (pKeyState && candidateMode == CANDIDATE_ORIGINAL)
			{ 
				pKeyState->Category = CATEGORY_CANDIDATE; 
				pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT;
			}
			return TRUE;
		}
	}
	
	// cancel assciated phrase with anykey except shift
	if (pKeyState && candidateMode == CANDIDATE_PHRASE && !(GetCandidateListPhraseModifier() == 0 && uCode == VK_SHIFT))  
	{
		pKeyState->Category = CATEGORY_CANDIDATE;
		pKeyState->Function = FUNCTION_CANCEL;
	}


	//Processing Composing keys -------------------------------------------------------------------------------------------
	
	
	if (fComposing)
	{
		if ((candidateMode != CANDIDATE_INCREMENTAL))
		{
			switch (uCode)
			{
				case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
				case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
				case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
				case VK_INSERT:
				case VK_DELETE:
				case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
				case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;
				case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
				case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
				case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
				case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

				case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
				case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

				case VK_SPACE:  if (pKeyState)
				{ 
					if (candidateMode != CANDIDATE_NONE)
					{
						if (CConfig::GetSpaceAsPageDown() && (candiCount > _candidatePageSize))
						{
							pKeyState->Category = CATEGORY_CANDIDATE;
							pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN;
						}
					}
					else
					{
						pKeyState->Category = CATEGORY_COMPOSING;
						pKeyState->Function = FUNCTION_CONVERT;
					}
					
				}
				return TRUE;

			}

		}
		else if (candidateMode == CANDIDATE_INCREMENTAL)
		{
			if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
			{
				return TRUE;
			}
			switch (uCode)
			{
				case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
				case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
				case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
				case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

					// VK_BACK - remove one char from reading string.
				case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

				case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
				case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
				case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
				case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

				case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
				case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

				case VK_SPACE:  if (pKeyState)
				{
					if (Global::imeMode == IME_MODE_ARRAY)
					{
						if (_hasWildcardIncludedInKeystrokeBuffer)
						{
							pKeyState->Category = CATEGORY_CANDIDATE;
							if (CConfig::GetSpaceAsPageDown() && (candiCount > _candidatePageSize))
								pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN;
							else
								pKeyState->Function = FUNCTION_CONVERT;
						}
						else
						{
							pKeyState->Category = CATEGORY_COMPOSING;
							pKeyState->Function = FUNCTION_CONVERT;
						}
						return TRUE;
					}
					else
					{
						pKeyState->Category = CATEGORY_CANDIDATE;
						pKeyState->Function = FUNCTION_CONVERT;
					}
				}
							 return TRUE;
				case VK_OEM_7: if (pKeyState
					&& Global::imeMode == IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY40_BIG5)//Array phrase ending
				{
					pKeyState->Category = CATEGORY_COMPOSING;
					if (_hasWildcardIncludedInKeystrokeBuffer)
						pKeyState->Function = FUNCTION_CONVERT_ARRAY_PHRASE_WILDCARD;
					else
						pKeyState->Function = FUNCTION_CONVERT_ARRAY_PHRASE;
					return TRUE;
				}

			}
			
			/*
			else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
			{
				return FALSE;
			}*/
			
		}
	}
	
	
	if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
	{

		if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
			(IsWildcard() && IsWildcardChar(*pwch) &&
				!(IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength() == 0 && IsWildcardAllChar(*pwch))))
		{
			if (pKeyState)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = FUNCTION_INPUT;
			}
			return TRUE;
		}
		if (!(IsWildcard() && IsWildcardChar(*pwch))
			&& IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_NONE))
		{
			return TRUE;
		}
		else if (_hasWildcardIncludedInKeystrokeBuffer && uCode != VK_SHIFT && uCode != VK_BACK &&
			(uCode == VK_SPACE || uCode == VK_RETURN || candidateMode == CANDIDATE_INCREMENTAL)
			&& Global::imeMode != IME_MODE_ARRAY)
		{
			if (pKeyState)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = FUNCTION_CONVERT_WILDCARD;
			}
			return TRUE;
		}
	}


	if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE)// && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION)
	{

		if (!(IsWildcard() && IsWildcardChar(*pwch))
			&& IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_INPUT))
		{
			return TRUE;
		}
	}

	if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_NONE))
	{
		if (pKeyState)
		{
			pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
			pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
		}
		return FALSE;
	}

	
	
	return FALSE;
}
//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE* pKeyState, KEYSTROKE_FUNCTION function)
{
	if (!IsDictionaryAvailable(_imeMode) || pKeyState == nullptr || _KeystrokeComposition.Count() == 0)
	{
		return FALSE;
	}

	pKeyState->Category = CATEGORY_NONE;
	pKeyState->Function = FUNCTION_NONE;


	_KEYSTROKE* pKeystroke = nullptr;


	WCHAR c = towupper(*pwch);
	if (c < 32 || c > 32 + MAX_RADICAL) return FALSE;

	//Convert ETEN keyboard to standard keyboard first if keyboard layout is ETEN
	if (Global::imeMode == IME_MODE_PHONETIC && CConfig::getPhoneticKeyboardLayout() == PHONETIC_ETEN_KEYBOARD_LAYOUT)
	{
		c = vpEtenKeyToStandardKeyTable[c - 32];
		if (c == 0) c = 32;  // set c = 32 (0 after -32) when key is not mapped.
	}

	pKeystroke = _KeystrokeComposition.GetAt(c - 32);
	if (c >= 'A' && c <= 'Z' && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) return FALSE; //  input English with shift-a~z 

	if (pKeystroke != nullptr && pKeystroke->Function != FUNCTION_NONE && uCode == pKeystroke->VirtualKey)
	{
		if (function == FUNCTION_NONE)
		{
			pKeyState->Category = CATEGORY_COMPOSING;
			pKeyState->Function = pKeystroke->Function;
			return TRUE;
		}
		else if (function == pKeystroke->Function)
		{
			pKeyState->Category = CATEGORY_COMPOSING;
			pKeyState->Function = pKeystroke->Function;
			return TRUE;
		}
	}


	return FALSE;
}


//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE* pKeyState, CANDIDATE_MODE candidateMode)
{
	if (pKeyState == nullptr)
	{
		return FALSE;
	}

	pKeyState->Category = CATEGORY_NONE;
	pKeyState->Function = FUNCTION_NONE;

	if (_pActiveCandidateListIndexRange->IsRange(uCode, *pwch, Global::ModifiersValue, candidateMode))
	{
		if (candidateMode == CANDIDATE_PHRASE)
		{
			// Candidate phrase could specify *modifier
			if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) || //shift + 123...
				(GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
			{
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
				return TRUE;
			}
			else
			{
				pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
				pKeyState->Function = pKeyState->Function = FUNCTION_CANCEL;  //No shift present, cancel phrsae mode.
				return FALSE;
			}
		}
		/*else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
		{
			// Candidate phrase could specify *modifier
			if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) || //shift + 123...
				(GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
			{
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
				return TRUE;
			}
			// else next composition
		}*/
		else if (candidateMode != CANDIDATE_NONE)
		{
			pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
	}
	return FALSE;
}


