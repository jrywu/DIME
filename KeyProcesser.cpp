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
	//debugPrint(L"CCompositionProcessorEngine::AddVirtualKey()");
	if (!wch)
	{
		return FALSE;
	}
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
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
			_pTextService->DoBeep(BEEP_TYPE::BEEP_COMPOSITION_ERROR);
			return FALSE;
		}

	}
	else
	{

		if ((UINT)_keystrokeBuffer.GetLength() >= CConfig::GetMaxCodes())
		{
			_pTextService->DoBeep(BEEP_TYPE::BEEP_WARNING); // do not eat the key if keystroke buffer length >= _maxcodes
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
	//debugPrint(L"CCompositionProcessorEngine::RemoveVirtualKey()");
	DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

	if (dwIndex + 1 < srgKeystrokeBufLen)
	{
		// shift following eles left
		memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
			(BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
			(srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
	}

	_keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);	
	
	if (srgKeystrokeBufLen == 1) // no more virtual keys in buffer after backspace
	{
		_hasWildcardIncludedInKeystrokeBuffer = FALSE;
	}


	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
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
	//debugPrint(L"CCompositionProcessorEngine::PurgeVirtualKey()");
	if (_keystrokeBuffer.Get())
	{
		delete[] _keystrokeBuffer.Get();
		_keystrokeBuffer.Set(NULL, 0);

	}
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC) phoneticSyllable = 0;
	_hasWildcardIncludedInKeystrokeBuffer = FALSE;
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex)
{
	//debugPrint(L"CCompositionProcessorEngine::GetVirtualKey()");
	if (dwIndex < _keystrokeBuffer.GetLength())
	{
		return *(_keystrokeBuffer.Get() + dwIndex);
	}
	return 0;
}

BOOL CCompositionProcessorEngine::isEndComposingKey(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::isEndComposingKey()");
	if (_keystrokeBuffer.Get() == nullptr || _pEndkey == nullptr)// || Global::imeMode != IME_MODE::IME_MODE_PHONETIC)
		goto exit;
	UINT len = (UINT)_keystrokeBuffer.GetLength();
	if (len == 0)
		goto exit;
	
	for (UINT i = 0; i < wcslen(_pEndkey); i++)
	{
		if (*(_pEndkey+i) == wch)
			return TRUE;
	}
	//if (wch == '3' || wch == '4' || wch == '6' || wch == '7')
	//	return TRUE;
exit:
	//debugPrint(L"CCompositionProcessorEngine::isEndComposingKey() reutrn FALSE");
	return FALSE;

}

UINT CCompositionProcessorEngine::addPhoneticKey(WCHAR* pwch)
{
	//debugPrint(L"CCompositionProcessorEngine::addPhoneticKey()");
	WCHAR c = towupper(*pwch);
	if (c < 32 || c > MAX_RADICAL + 32) return phoneticSyllable;

	UINT s, m;
	if (CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT)
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
	//debugPrint(L"CCompositionProcessorEngine::VPSymbolToStandardLayoutChar()");
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
	//debugPrint(L"CCompositionProcessorEngine::removeLastPhoneticSymbol()");

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
// IsSymbolLeading()
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolLeading()
{
	//debugPrint(L"CCompositionProcessorEngine::IsSymbolLeading()");
	if (_keystrokeBuffer.Get() == nullptr || _keystrokeBuffer.GetLength() ==0)
		return FALSE;
	WCHAR c = *_keystrokeBuffer.Get();
	if ((Global::imeMode == IME_MODE::IME_MODE_DAYI && c == L'=') ||
		(Global::imeMode == IME_MODE::IME_MODE_ARRAY && (
			(CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && towupper(c) == L'W') ||
			CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(c) == L'H' || c == L'8'))))
		return TRUE;
	else
		return FALSE;
}
//+---------------------------------------------------------------------------
//
// IsSymbol
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbol()
{
	//debugPrint(L"CCompositionProcessorEngine::IsSymbol()");

	if (_keystrokeBuffer.Get() == nullptr) 
		return FALSE;
	
	WCHAR c = *_keystrokeBuffer.Get();
	UINT len = (UINT) _keystrokeBuffer.GetLength();

	if (Global::imeMode == IME_MODE::IME_MODE_DAYI && (len < 3 && c == L'='))
		return TRUE;
	
	if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && len==2)
	{
		WCHAR c2 = *(_keystrokeBuffer.Get() + 1);
		if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && towupper(c) == L'W' && c2 <= L'9' && c2 >= L'0')
			return TRUE;
		if (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5
			&& (towupper(c) == L'H' || c == L'8') && (c2 == L'\'' || c2 == L'[' || c2 == L']' || c2 == L'-' || c2 == L'='))
			return TRUE;

	}
	//debugPrint(L"CCompositionProcessorEngine::IsSymbol() return FALSE");
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsSymbolChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolChar(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::IsSymbolChar()");
	if (_keystrokeBuffer.Get() == nullptr || _keystrokeBuffer.GetLength() > 1)
		goto exit;
	debugPrint(L"CCompositionProcessorEngine::IsSymbolChar(), len = %d, c = %d", _keystrokeBuffer.GetLength(), *_keystrokeBuffer.Get());
	
	if (_keystrokeBuffer.GetLength() == 0)
	{
		if((Global::imeMode == IME_MODE::IME_MODE_DAYI && wch == L'=') ||
		   (Global::imeMode == IME_MODE::IME_MODE_ARRAY &&
				((CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(wch) == L'W')) ||
				 (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(wch) == L'H' || wch == L'8')))))
					return TRUE;
	}
	else if (_keystrokeBuffer.GetLength() == 1)
	{
		WCHAR c = *_keystrokeBuffer.Get();
		if (Global::imeMode == IME_MODE::IME_MODE_DAYI && towupper(c) == L'=')
		{
			return TRUE;
		}
		else if ( Global::imeMode == IME_MODE::IME_MODE_ARRAY)
		{
			if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(c) == L'W') &&
				(wch >= L'0' && wch <= L'9'))
				return TRUE;
			else if (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5
				&& (towupper(c) == L'H' || c == L'8') &&
				(wch == L'\'' || wch == L'[' || wch == L']' || wch == L'-' || wch == L'='))
				return TRUE;

		}
	}
exit:
	debugPrint(L"CCompositionProcessorEngine::IsSymbolChar() return FALSE");
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsDayiAddressChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsDayiAddressChar(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::IsDayiAddressChar()");
	if (Global::imeMode != IME_MODE::IME_MODE_DAYI)
		return FALSE;

	for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
	{
		if (Global::dayiAddressCharTable[i]._Code == wch)
		{
			return TRUE;
		}
	}
	//debugPrint(L"CCompositionProcessorEngine::IsDayiAddressChar() return FALSE");
	return FALSE;
}


//+---------------------------------------------------------------------------
//
// IsArrayShortCode
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsArrayShortCode()
{
	//debugPrint(L"CCompositionProcessorEngine::IsArrayShortCode()");
	if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope()!=ARRAY_SCOPE::ARRAY40_BIG5 &&
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
	//debugPrint(L"CCompositionProcessorEngine::IsDoubleSingleByte()");
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
	hasCandidateWithWildcard;
	//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyNeed() uCode = %d, pwch = %c, pKeyState = %d, fComposing = %d, candidateMode = %d, hasCandidateWithWildcard = %d, candiCount = %d", uCode, *pwch, pKeyState, fComposing, candidateMode, hasCandidateWithWildcard, candiCount);
	if (pKeyState)
	{
		pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
		pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;
	}

	if (candidateMode == CANDIDATE_MODE::CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE)
	{
		fComposing = FALSE;
	}
	// Processing dayi address input ---------------------------------------------------------
	// // Symbol mode start with L'=' for dayi, L'w' for array30; L'H' and L'8' for array40
	if (IsSymbolChar(*pwch) && uCode != VK_SHIFT && candidateMode != CANDIDATE_MODE::CANDIDATE_ORIGINAL &&
		!(CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC && uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
	{
		if (pKeyState)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT;
			
		}
		return TRUE;
	}

	// Address characters direct input mode  "'[]-\"
	if (IsDayiAddressChar(*pwch) && 
		(candidateMode == CANDIDATE_MODE::CANDIDATE_NONE || candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE) &&
		!(CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC && uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
	{
		if (pKeyState)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_ADDRESS_DIRECT_INPUT;
		}
		return TRUE;
	}
	
	//Processing cand keys ------------------------------------------------------------------------------
	if (candidateMode == CANDIDATE_MODE::CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE)
	{
		switch (uCode)
		{
			case VK_UP:     if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP; } return TRUE;
			case VK_DOWN:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN; } return TRUE;
			case VK_PRIOR:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_UP; } return TRUE;
			case VK_NEXT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN; } return TRUE;
			case VK_HOME:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP; } return TRUE;
			case VK_END:    if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
			case VK_LEFT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT; } return TRUE;
			case VK_RIGHT:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT; } return TRUE;
			case VK_RETURN: if (pKeyState)
			{
				if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE && candiSelection == -1)
				{
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL;
					return FALSE;
				}
				else
				{
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
					return TRUE;
				}
			}
			case VK_SPACE:  if (pKeyState)
			{
				if (candidateMode != CANDIDATE_MODE::CANDIDATE_PHRASE   
					&& CConfig::GetSpaceAsPageDown() && (candiCount > _candidatePageSize))
				{//Do page down with space key when the option is on and candiCoutn > cand page size in original candi.
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN;
				}
				else if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE && candiSelection == -1)
				{//Space key will terminate phrase candi
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL;
					return FALSE;
				}
				else
				{//Select first item
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
				}
				return TRUE;
			}
			case VK_BACK:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL; } return TRUE;
			case VK_INSERT:
			case VK_DELETE:
			case VK_ESCAPE: if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL; } return TRUE;
		}
		// Check if the key is valid cand selkeys and set corersponding pKeystate category&function.
		if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
		{
			return TRUE;
		}
		// User send next valid keystroke without select cand.  Select the first item and start new composition
		if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, KEYSTROKE_FUNCTION::FUNCTION_NONE))
		{
			if (pKeyState && candidateMode == CANDIDATE_MODE::CANDIDATE_ORIGINAL)
			{ 
				pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
				pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT;
			}
			return TRUE;
		}
		
	}
	// cancel associated phrase with anykey except shift
	if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE
		&& !(GetCandidateListPhraseModifier() == 0 && uCode == VK_SHIFT))
	{
		if (pKeyState)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL;
		}
		return FALSE;
	}
	//Processing Composing keys -------------------------------------------------------------------------------------------
	if (fComposing)
	{
		if ((candidateMode == CANDIDATE_MODE::CANDIDATE_NONE))
		{
			switch (uCode)
			{
				case VK_LEFT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT; } return TRUE;
				case VK_RIGHT:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT; } return TRUE;
				case VK_INSERT:
				case VK_DELETE:
				case VK_ESCAPE: if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL; } return TRUE;
				case VK_BACK:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_BACKSPACE; } return TRUE;
				case VK_UP:     if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP; } return TRUE;
				case VK_DOWN:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN; } return TRUE;
				case VK_PRIOR:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_UP; } return TRUE;
				case VK_NEXT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

				case VK_HOME:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP; } return TRUE;
				case VK_END:    if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
				case VK_RETURN:
				case VK_SPACE:  if (pKeyState)
				{ // End composition with covert or cover_wildcard.
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
					if (_hasWildcardIncludedInKeystrokeBuffer)
						pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT_WILDCARD;
					else
						pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
				}
				return TRUE;

			}
		}
		else if (candidateMode == CANDIDATE_MODE::CANDIDATE_INCREMENTAL)
		{	
			switch (uCode)
			{
				case VK_LEFT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT; } return TRUE;
				case VK_RIGHT:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT; } return TRUE;
				case VK_RETURN: if (pKeyState)
				{
					if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
						!_hasWildcardIncludedInKeystrokeBuffer)
					{// Do composing in Array 30 short code candi or no candi
						pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
						pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
					}
					else
					{
						pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
						pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
					}
				}
					return TRUE;
				case VK_ESCAPE: if (pKeyState) {
					pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL; } return TRUE;
				case VK_BACK:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_BACKSPACE; } return TRUE;
				case VK_UP:     if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP; } return TRUE;
				case VK_DOWN:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN; } return TRUE;
				case VK_PRIOR:  if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_UP; } return TRUE;
				case VK_NEXT:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN; } return TRUE;
				case VK_HOME:   if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP; } return TRUE;
				case VK_END:    if (pKeyState) { pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE; pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
				case VK_SPACE:  if (pKeyState)
				{
					if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
						!_hasWildcardIncludedInKeystrokeBuffer)
					{// Do composing in Array 30 short code candi or no candi
						pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
						pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
					}
					else
					{
						pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
						if (CConfig::GetSpaceAsPageDown() && (candiCount > _candidatePageSize))
							pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN;
						else
							pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
					}

				}
					return TRUE;							
				case VK_OEM_7:	if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5) //Array phrase ending key '
				{
					if (pKeyState)
					{
						pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
						if (_hasWildcardIncludedInKeystrokeBuffer)
							pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT_ARRAY_PHRASE_WILDCARD;
						else
							pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT_ARRAY_PHRASE;
					}
					return TRUE;
				}
					
			}
			// Check if the key is valid cand selkeys and set corersponding pKeystate category&function.
			if (IsKeystrokeRange(uCode, pwch, pKeyState, candidateMode))
			{
				return TRUE;
			}
		}
	}
	if (fComposing || candidateMode == CANDIDATE_MODE::CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_MODE::CANDIDATE_NONE)
	{
		// Bypassing wildcard keys 
		if (IsWildcardChar(*pwch))
		{
			if (pKeyState)
			{
				pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
				// Send wildcard char directly if it's the first key in composition
				pKeyState->Function = (_keystrokeBuffer.GetLength() == 0)? KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT: KEYSTROKE_FUNCTION::FUNCTION_INPUT;

			}
			return TRUE;
		}
		
		// Check if valid composition
		if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, KEYSTROKE_FUNCTION::FUNCTION_NONE))
		{
			if (_hasWildcardIncludedInKeystrokeBuffer && pKeyState->Function == KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT)
				pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT_WILDCARD;
			return TRUE;
		}

		// Endkey end composition 
		if (isEndComposingKey(*pwch))
		{
			if (pKeyState)
			{
				pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
				if (_hasWildcardIncludedInKeystrokeBuffer)
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT_WILDCARD;
				else
					pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
			}
			return TRUE;
		}
		//End composition if the key is not a valid keystroke
		if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, KEYSTROKE_FUNCTION::FUNCTION_NONE))
		{
			if (pKeyState)
			{				
				pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
				pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_TEXTSTORE;
			}
			return FALSE;
		}
		
		
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

	pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
	pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;


	_KEYSTROKE* pKeystroke = nullptr;


	WCHAR c = towupper(*pwch);
	if (c < 32 || c > 32 + MAX_RADICAL) return FALSE;

	if (CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC_COMPOSITION_ONLY &&
		!(uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
		return FALSE;

	//Convert ETEN keyboard to standard keyboard first if keyboard layout is ETEN
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC && CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT)
	{
		c = vpEtenKeyToStandardKeyTable[c - 32];
		if (c == 0) c = 32;  // set c = 32 (0 after -32) when key is not mapped.
	}
	//Check if valid keystroke in keystroke table
	pKeystroke = _KeystrokeComposition.GetAt(c - 32);

	if (c >= 'A' && c <= 'Z' && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) return FALSE; //  input English with shift-a~z 

	if (pKeystroke != nullptr && pKeystroke->Function != KEYSTROKE_FUNCTION::FUNCTION_NONE &&
		!(CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC && uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
	{
		if (function == KEYSTROKE_FUNCTION::FUNCTION_NONE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
			pKeyState->Function = pKeystroke->Function;
			return TRUE;
		}
		else if (function == pKeystroke->Function)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
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
	//debugPrint(L"IsKeystrokeRange() ucode=%d, pwch=%c, candidateMode=%d", uCode, *pwch, candidateMode);
	if (pKeyState == nullptr)
	{
		return FALSE;
	}

	pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
	pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;

	if (_pActiveCandidateListIndexRange->IsRange(uCode, *pwch, Global::ModifiersValue, candidateMode))
	{

		if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
		else if (candidateMode != CANDIDATE_MODE::CANDIDATE_NONE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
	}
	return FALSE;
}


