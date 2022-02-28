//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
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
	if (dwIndex < _keystrokeBuffer.GetLength())
	{
		return *(_keystrokeBuffer.Get() + dwIndex);
	}
	return 0;
}

BOOL CCompositionProcessorEngine::isPhoneticComposingKey()
{
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
	if (_keystrokeBuffer.Get() == nullptr) return FALSE;
	if (Global::imeMode == IME_MODE_DAYI)
		return (_keystrokeBuffer.GetLength() < 3 && *_keystrokeBuffer.Get() == L'=');

	else if (Global::imeMode == IME_MODE_ARRAY)
		return (_keystrokeBuffer.GetLength() == 2 && towupper(*_keystrokeBuffer.Get()) == L'W'
			&& *(_keystrokeBuffer.Get() + 1) <= L'9' && *(_keystrokeBuffer.Get() + 1) >= L'0');

	else
		return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsSymbolChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolChar(WCHAR wch)
{
	if (_keystrokeBuffer.Get() == nullptr) return FALSE;
	if ((_keystrokeBuffer.GetLength() == 1) &&
		(*_keystrokeBuffer.Get() == L'=') &&
		Global::imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[IME_MODE_DAYI])
		//&&_pTableDictionaryEngine[IME_MODE_DAYI]->GetDictionaryType() == TTS_DICTIONARY)
	{
		for (UINT i = 0; i < wcslen(Global::DayiSymbolCharTable); i++)
		{
			if (Global::DayiSymbolCharTable[i] == wch)
			{
				return TRUE;
			}
		}

	}
	if ((_keystrokeBuffer.GetLength() == 1) && (towupper(*_keystrokeBuffer.Get()) == L'W') && Global::imeMode == IME_MODE_ARRAY)
	{
		if (wch >= L'0' && wch <= L'9')
		{
			return TRUE;
		}

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
	if (Global::imeMode != IME_MODE_DAYI)
		//|| (_pTableDictionaryEngine[IME_MODE_DAYI] && _pTableDictionaryEngine[IME_MODE_DAYI]->GetDictionaryType() != TTS_DICTIONARY)) 
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
	if (Global::imeMode == IME_MODE_ARRAY && _keystrokeBuffer.GetLength() < 3) return TRUE;
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
	if (pKeyState)
	{
		pKeyState->Category = CATEGORY_NONE;
		pKeyState->Function = FUNCTION_NONE;
	}
	
	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		fComposing = FALSE;
	}

	if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
	{

		if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
			(IsWildcard() && IsWildcardChar(*pwch) && !(IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength() == 0 && IsWildcardAllChar(*pwch))))
		{
			if (pKeyState)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = FUNCTION_INPUT;
			}
			return TRUE;
		}
		if (!(IsWildcard() && IsWildcardChar(*pwch)) && IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_NONE))
		{
			return TRUE;
		}
		else if (_hasWildcardIncludedInKeystrokeBuffer && uCode != VK_SHIFT && uCode != VK_BACK &&
			(uCode == VK_SPACE || uCode == VK_RETURN || candidateMode == CANDIDATE_INCREMENTAL) && Global::imeMode != IME_MODE_ARRAY)
		{
			if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT_WILDCARD; } return TRUE;
		}
	}
	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		// Candidate list could not handle key. We can try to restart the composition.
		if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
		{
			return TRUE;
		}
		if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_INPUT))
		{
			if (candidateMode != CANDIDATE_ORIGINAL)
			{
				return TRUE;
			}
			else
			{
				if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT; }
				return TRUE;
			}
		}
	}
	
	

	if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION)
	{

		if (!(IsWildcard() && IsWildcardChar(*pwch)) && IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, FUNCTION_INPUT))
		{
			return TRUE;
		}
	}
	// System pre-defined keystroke
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
						if (CConfig::GetSpaceAsPageDown()
							&& (candiCount > UINT((Global::imeMode == IME_MODE_PHONETIC) ? 9 : 10)))
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
					//pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT;
				}
				return TRUE;

			}

		}
		else if (candidateMode == CANDIDATE_INCREMENTAL)
		{
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
							if (CConfig::GetSpaceAsPageDown())
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
				case VK_OEM_7: if (pKeyState && Global::imeMode == IME_MODE_ARRAY)//Array phrase ending
				{
					pKeyState->Category = CATEGORY_COMPOSING;
					if (_hasWildcardIncludedInKeystrokeBuffer)
						pKeyState->Function = FUNCTION_CONVERT_ARRAY_PHRASE_WILDCARD;
					else
						pKeyState->Function = FUNCTION_CONVERT_ARRAY_PHRASE;
					return TRUE;
				}

			}
		}
	}
	
	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
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
					if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION) // space finalized the associate here instead of choose the first one (selected index = -1 for phrase candidates).
					{
						pKeyState->Category = CATEGORY_CANDIDATE;
						pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST;
					}
					else if (candidateMode != CANDIDATE_PHRASE
						&& CConfig::GetSpaceAsPageDown() && (candiCount > UINT((Global::imeMode == IME_MODE_PHONETIC) ? 9 : 10)))
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
					if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
					{
						pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
						pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
						return TRUE;
					}
					else
					{
						pKeyState->Category = CATEGORY_CANDIDATE;
						pKeyState->Function = FUNCTION_CANCEL;
						return TRUE;
					}
				}
				

		}
		
		
		if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
		{
			if (IsVirtualKeyKeystrokeComposition(uCode, pwch, NULL, FUNCTION_NONE))
			{
				if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT; } return TRUE;
			}
		}
	}
	
	if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
	{
		return TRUE;
	}
	else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
	{
		return FALSE;
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

	if (candidateMode == CANDIDATE_PHRASE && !(GetCandidateListPhraseModifier() == 0 && uCode == VK_SHIFT) && pKeyState)  // cancel assciated phrase with anykey except shift
	{
		pKeyState->Category = CATEGORY_CANDIDATE;
		pKeyState->Function = FUNCTION_CANCEL;
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
		else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
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
		}
		else if (candidateMode != CANDIDATE_NONE)
		{
			pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
	}
	return FALSE;
}


