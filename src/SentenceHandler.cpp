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

// Sentence mode ("continuous input") for CDIME.
//
// While a sentence is pending, the composition holds the converted sentence
// with the reading of the character being typed inserted at the cursor.
// Typing a code and pressing Space (or an end key) adds a character position
// with all of the code's candidates instead of opening the candidate window;
// the conversion then re-picks the candidates of the whole sentence.
//
//   Enter / Space (no reading)  commit the sentence
//   Esc (no reading)            discard the sentence
//   Backspace / Delete          remove the character before / under the cursor
//   Left / Right / Home / End   move the cursor between characters
//   Down                        choose the character under the cursor (pins it)
//   select keys / arrows in the candidate window pin the character
//
// Characters pinned by hand are learned on commit (_SentenceLearn): the code's
// preferred character and the pairs it forms with its neighbours, saved to
// %APPDATA%\DIME\SentenceUser.txt and used by later conversions.
//
// Anything that outputs text directly (punctuation, address keys, Shift+key
// English, unhandled keys) commits the sentence first.

#include "Private.h"
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "File.h"
#ifdef DIME_USE_RUST_ENGINE
#include "RustSessionHost.h"
#endif

namespace
{
	// %APPDATA%\DIME\<name> when present, otherwise <Program Files>\DIME\<name>.
	BOOL ResolveDataFile(_In_z_ LPCWSTR name, _Out_writes_(MAX_PATH) WCHAR* path)
	{
		WCHAR appData[MAX_PATH] = { 0 };
		WCHAR programFiles[MAX_PATH] = { 0 };

		if (Global::GetAppDataPath(appData, FALSE))
		{
			StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\%s", appData, name);
			if (PathFileExists(path)) return TRUE;
		}
		if (GetEnvironmentVariable(L"ProgramW6432", programFiles, MAX_PATH) == 0)
			GetEnvironmentVariable(L"ProgramFiles", programFiles, MAX_PATH);
		StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\%s", programFiles, name);
		return PathFileExists(path);
	}

	size_t LoadDataFile(_In_z_ LPCWSTR name, CSentenceLexicon* pLexicon, BOOL isFreqTable)
	{
		WCHAR path[MAX_PATH] = { 0 };
		if (!ResolveDataFile(name, path)) return 0;

		CFile file;
		if (!file.CreateFile(path, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ)) return 0;
		const WCHAR* pBuffer = file.GetReadBufferPointer();
		if (pBuffer == nullptr) return 0;
		size_t length = (size_t)(file.GetFileSize() / sizeof(WCHAR));
		return isFreqTable ? pLexicon->LoadFreqTable(pBuffer, length) : pLexicon->LoadCinValues(pBuffer, length);
	}
}

//+---------------------------------------------------------------------------
//
// _IsSentenceMode
//
//----------------------------------------------------------------------------

#ifdef DIME_USE_RUST_ENGINE
BOOL CDIME::_IsSentencePending()
{
	return (_pRustSession && dime_session_sentence_pending(_pRustSession)) ? TRUE : FALSE;
}
#endif

BOOL CDIME::_IsSentenceMode()
{
	return CConfig::GetSentenceMode() && Global::imeMode == IME_MODE::IME_MODE_DAYI;
}

//+---------------------------------------------------------------------------
//
// _IsSentenceKeyEaten
//
// Called by _IsKeyEaten while a sentence is pending with no reading and no
// candidate window.  Returns TRUE (and the key state) for keys that act on
// the sentence; other keys take the normal path.
//----------------------------------------------------------------------------

BOOL CDIME::_IsSentenceKeyEaten(UINT code, _Inout_opt_ _KEYSTROKE_STATE *pKeyState)
{
	KEYSTROKE_CATEGORY category = KEYSTROKE_CATEGORY::CATEGORY_SENTENCE;
	KEYSTROKE_FUNCTION function = KEYSTROKE_FUNCTION::FUNCTION_NONE;

	// Modified keys (Ctrl/Alt shortcuts) are not ours.
	if (Global::ModifiersValue & (TF_MOD_CONTROL | TF_MOD_ALT | TF_MOD_LCONTROL | TF_MOD_RCONTROL | TF_MOD_LALT | TF_MOD_RALT))
		return FALSE;

	switch (code)
	{
	case VK_RETURN:
	case VK_SPACE:  function = KEYSTROKE_FUNCTION::FUNCTION_CONVERT; break;
	case VK_BACK:
	case VK_DELETE: function = KEYSTROKE_FUNCTION::FUNCTION_BACKSPACE; break;
	case VK_LEFT:   function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT; break;
	case VK_RIGHT:  function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT; break;
	case VK_HOME:   function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP; break;
	case VK_END:    function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM; break;
	case VK_DOWN:   function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN; break;
	case VK_UP:
	case VK_PRIOR:
	case VK_NEXT:   function = KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP; break;   // eaten, no effect
	case VK_ESCAPE:
		category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;                  // _HandleCancel discards
		function = KEYSTROKE_FUNCTION::FUNCTION_CANCEL;
		break;
	default:
		return FALSE;
	}

	if (pKeyState)
	{
		pKeyState->Category = category;
		pKeyState->Function = function;
	}
	return TRUE;
}

//+---------------------------------------------------------------------------
//
// _GetSentenceLexicon
//
// Loaded on first use: character frequencies from TCFreq.cin and the words of
// Array-Phrase.cin (the codes are ignored, only the words are used).
//----------------------------------------------------------------------------

CSentenceLexicon* CDIME::_GetSentenceLexicon()
{
	if (!_sentenceLexiconLoaded)
	{
		_sentenceLexiconLoaded = TRUE;
		if (CSentenceLexicon::IsSupported())
		{
			_pSentenceLexicon = new (std::nothrow) CSentenceLexicon();
			if (_pSentenceLexicon)
			{
				size_t freq = LoadDataFile(L"TCFreq.cin", _pSentenceLexicon, TRUE);
				size_t words = LoadDataFile(L"Array-Phrase.cin", _pSentenceLexicon, FALSE);
				debugPrint(L"CDIME::_GetSentenceLexicon() freq lines = %d, words = %d", (int)freq, (int)words);
			}

			_pSentenceUser = new (std::nothrow) CSentenceUserModel();
			_pSentencePending = new (std::nothrow) CSentenceUserModel();
			WCHAR path[MAX_PATH] = { 0 };
			if (_pSentenceUser && _SentenceUserFilePath(path))
				_pSentenceUser->LoadFile(path);
		}
	}
	return _pSentenceLexicon;
}

//+---------------------------------------------------------------------------
//
// _SentenceUserFilePath
//
// %APPDATA%\DIME\SentenceUser.txt: what sentence mode learned from the
// characters the user fixed by hand.  Delete the file to forget.
//----------------------------------------------------------------------------

BOOL CDIME::_SentenceUserFilePath(_Out_writes_(MAX_PATH) WCHAR* path)
{
	*path = L'\0';
	WCHAR appData[MAX_PATH] = { 0 };
	if (!Global::GetAppDataPath(appData, FALSE))
		return FALSE;
	return SUCCEEDED(StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\SentenceUser.txt", appData));
}

void CDIME::_SentenceConvert()
{
	CSentenceLexicon* pLexicon = _GetSentenceLexicon();
	_sentence.Convert(pLexicon, _pSentenceUser);
}

//+---------------------------------------------------------------------------
//
// _SentenceLearn
//
// Called with the sentence about to be committed.  Characters the user fixed
// by hand are learned in memory right away and saved to the shared file: the
// file is re-read and only this process's new data added, so several
// processes do not overwrite each other.  Store apps cannot write %APPDATA%
// (read-only for AppContainer), so there the data stays in memory.
//----------------------------------------------------------------------------

void CDIME::_SentenceLearn()
{
	if (!_IsSentencePending() || _pSentenceUser == nullptr || _pSentencePending == nullptr)
		return;
#ifdef DIME_USE_RUST_ENGINE
	if (!dime_session_sentence_learn(_pRustSession, _pSentenceUser->Handle()))
		return;
	dime_session_sentence_learn(_pRustSession, _pSentencePending->Handle());
#else
	if (!_pSentenceUser->Learn(_sentence))
		return;
	_pSentencePending->Learn(_sentence);
#endif

	if (_IsStoreAppMode() || isLowIntegrityProcess())
		return;

	WCHAR path[MAX_PATH] = { 0 };
	if (!_SentenceUserFilePath(path))
		return;

	CSentenceUserModel* pFresh = new (std::nothrow) CSentenceUserModel();
	if (pFresh == nullptr)
		return;
	pFresh->LoadFile(path);
	pFresh->Merge(*_pSentencePending);
	if (pFresh->SaveFile(path))
	{
		_pSentencePending->Clear();
		delete _pSentenceUser;
		_pSentenceUser = pFresh;  // now also includes what other processes learned
	}
	else
	{
		delete pFresh;  // keep the pending data for the next save
	}
}

std::wstring CDIME::_GetKeystrokeCode()
{
	std::wstring code;
	if (_pCompositionProcessorEngine)
	{
		DWORD_PTR len = _pCompositionProcessorEngine->GetVirtualKeyLength();
		for (DWORD_PTR i = 0; i < len; i++)
			code += _pCompositionProcessorEngine->GetVirtualKey(i);
	}
	return code;
}

//+---------------------------------------------------------------------------
//
// _SentenceWrite
//
// Replace the composition text and put the caret at caret.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceWrite(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& text, size_t caret, BOOL withAttributes)
{
	if (_pComposition == nullptr || pContext == nullptr) return S_FALSE;

	ITfRange* pRange = nullptr;
	HRESULT hr = _pComposition->GetRange(&pRange);
	if (FAILED(hr) || pRange == nullptr) return hr;

	_sentenceDisplay = text;
	hr = pRange->SetText(ec, 0, _sentenceDisplay.c_str(), (LONG)_sentenceDisplay.length());
	if (SUCCEEDED(hr))
	{
		lastReadingString.Set(_sentenceDisplay.c_str(), _sentenceDisplay.length());

		if (withAttributes)
		{
			_SetCompositionLanguage(ec, pContext);
			_SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);
		}

		ITfRange* pSelection = nullptr;
		if (SUCCEEDED(pRange->Clone(&pSelection)) && pSelection)
		{
			LONG shifted = 0;
			pSelection->Collapse(ec, TF_ANCHOR_START);
			if (caret > 0) pSelection->ShiftEnd(ec, (LONG)caret, &shifted, nullptr);
			pSelection->Collapse(ec, TF_ANCHOR_END);

			TF_SELECTION sel;
			sel.range = pSelection;
			sel.style.ase = TF_AE_NONE;
			sel.style.fInterimChar = FALSE;
			pContext->SetSelection(ec, 1, &sel);
			pSelection->Release();
		}
	}
	pRange->Release();
	return hr;
}

HRESULT CDIME::_SentenceRender(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& reading)
{
	return _SentenceWrite(ec, pContext, _sentence.Display(reading), _sentence.DisplayCaret(reading), TRUE);
}

//+---------------------------------------------------------------------------
//
// _SentenceAppend
//
// Add the character for the current reading at the cursor, re-convert and
// show the sentence.  The reading and any candidate window are cleared.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceAppend(TfEditCookie ec, _In_ ITfContext *pContext, const std::vector<std::wstring>& candidates, int pinned)
{
	std::wstring code = _GetKeystrokeCode();

	if (!_IsComposing())
		_StartComposition(pContext);

	_DeleteCandidateList(TRUE, pContext);  // purges the reading too
	_sentenceEditPos = -1;

	if (!_sentence.Insert(code, candidates, pinned))
		return _SentenceRender(ec, pContext, L"");

	_SentenceConvert();

	if (_sentence.Count() >= CSentenceBuffer::MaxPositions)
		return _HandleSentenceCommit(ec, pContext);

	return _SentenceRender(ec, pContext, L"");
}

HRESULT CDIME::_SentenceAppendFromCandidateList(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CDIMEArray<CCandidateListItem> *pCandidateList, int pinned)
{
	std::vector<std::wstring> candidates;
	for (UINT i = 0; i < pCandidateList->Count(); i++)
	{
		CCandidateListItem* pItem = pCandidateList->GetAt(i);
		if (pItem && pItem->_ItemString.GetLength())
			candidates.emplace_back(pItem->_ItemString.Get(), pItem->_ItemString.GetLength());
	}
	return _SentenceAppend(ec, pContext, candidates, pinned);
}

//+---------------------------------------------------------------------------
//
// _SentenceSelectCandidate
//
// A candidate was finalized from the candidate window (Space, Enter, select
// key, or typing on).  Either it fixes the character being edited (Down), or
// it adds the character for the current reading.  isExplicit pins it.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceSelectCandidate(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& selected, BOOL isExplicit)
{
	if (_sentenceEditPos >= 0)
	{
		size_t pos = (size_t)_sentenceEditPos;
		_sentenceEditPos = -1;
		if (pos < _sentence.Count())
		{
			const std::vector<std::wstring>& candidates = _sentence.At(pos).candidates;
			for (size_t i = 0; i < candidates.size(); i++)
			{
				if (candidates[i] == selected)
				{
					_sentence.Pin(pos, (int)i);
					break;
				}
			}
		}
		_DeleteCandidateList(TRUE, pContext);
		_SentenceConvert();
		return _SentenceRender(ec, pContext, L"");
	}

	std::vector<std::wstring> candidates;
	int index = -1;
	if (!_isCandidateWithWildcard && _pCompositionProcessorEngine && !_pCompositionProcessorEngine->IsEscapeInputLeading())
	{
		CDIMEArray<CCandidateListItem> exact;
		_pCompositionProcessorEngine->GetCandidateList(&exact, FALSE, FALSE);
		for (UINT i = 0; i < exact.Count(); i++)
		{
			CCandidateListItem* pItem = exact.GetAt(i);
			if (pItem == nullptr || pItem->_ItemString.GetLength() == 0) continue;
			candidates.emplace_back(pItem->_ItemString.Get(), pItem->_ItemString.GetLength());
			if (index < 0 && candidates.back() == selected) index = (int)candidates.size() - 1;
		}
	}
	if (index < 0)
	{
		// Wildcard, symbol, or incremental (prefix) match: keep exactly what was chosen.
		candidates.assign(1, selected);
		index = 0;
		isExplicit = TRUE;
	}
	return _SentenceAppend(ec, pContext, candidates, isExplicit ? index : -1);
}

//+---------------------------------------------------------------------------
//
// _SentenceOpenCandidates
//
// Down: show the candidates of the character under the cursor.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceOpenCandidates(TfEditCookie ec, _In_ ITfContext *pContext)
{
	if (_sentence.IsEmpty() || _pUIPresenter == nullptr || _pCompositionProcessorEngine == nullptr)
		return S_OK;

	size_t pos = _sentence.EditTarget();
	const CSentencePosition& position = _sentence.At(pos);
	if (position.candidates.size() < 2)
	{
		DoBeep(BEEP_TYPE::BEEP_WARNING);
		return S_OK;
	}

	CDIMEArray<CCandidateListItem> candidateList;
	for (const std::wstring& c : position.candidates)
	{
		CCandidateListItem* pItem = candidateList.Append();
		if (pItem == nullptr) return E_OUTOFMEMORY;
		pItem->_ItemString.Set(c.c_str(), c.length());
		pItem->_FindKeyCode.Set(position.code.c_str(), position.code.length());
	}

	HRESULT hr = _CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext);
	if (SUCCEEDED(hr))
	{
		_pUIPresenter->_ClearCandidateList();
		_pUIPresenter->_SetCandidateText(&candidateList, _pCompositionProcessorEngine->GetCandidateListIndexRange(),
			FALSE, _pCompositionProcessorEngine->GetCandidateWindowWidth());
		_pUIPresenter->_SetCandidateSelection(position.chosen, FALSE);
		_candidateMode = CANDIDATE_MODE::CANDIDATE_ORIGINAL;
		_isCandidateWithWildcard = FALSE;
		_sentenceEditPos = (int)pos;
	}
	return hr;
}

//+---------------------------------------------------------------------------
//
// _SentenceCloseReading
//
// Drop the reading / candidate window but keep the sentence.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceCloseReading(TfEditCookie ec, _In_ ITfContext *pContext)
{
	_DeleteCandidateList(TRUE, pContext);
	_sentenceEditPos = -1;
	_sentencePinNext = FALSE;
	return _SentenceRender(ec, pContext, L"");
}

//+---------------------------------------------------------------------------
//
// _HandleSentenceCommit
//
// Write the sentence into the document and end the composition.
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleSentenceCommit(TfEditCookie ec, _In_ ITfContext *pContext)
{
#ifdef DIME_USE_RUST_ENGINE
	CRustSessionHost host(this, ec, pContext);
	return dime_session_handle_sentence_commit(host.Get());
#else
	return _HandleSentenceCommitCpp(ec, pContext);
#endif
}

HRESULT CDIME::_HandleSentenceCommitCpp(TfEditCookie ec, _In_ ITfContext *pContext)
{
	if (_sentence.IsEmpty())
		return S_OK;

	_SentenceLearn();

	std::wstring text = _sentence.Text();
	if (CConfig::GetDoHanConvert() && _pCompositionProcessorEngine)
	{
		CStringRange source, converted;
		source.Set(text.c_str(), text.length());
		if (_pCompositionProcessorEngine->GetSCFromTC(&source, &converted) && converted.Get())
		{
			text.assign(converted.Get(), converted.GetLength());
			delete[] converted.Get();
		}
	}

	_SentenceWrite(ec, pContext, text, text.length(), FALSE);
	_SentenceReset();
	return _HandleComplete(ec, pContext);
}

HRESULT CDIME::_SentenceCommitBeforeOutput(TfEditCookie ec, _In_ ITfContext *pContext)
{
	if (!_IsSentencePending())
		return S_OK;
	_DeleteCandidateList(TRUE, pContext);
	return _HandleSentenceCommit(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleSentenceBackspace
//
//----------------------------------------------------------------------------

#ifdef DIME_COMPILE_ROUTING_HANDLERS
HRESULT CDIME::_HandleSentenceBackspace(TfEditCookie ec, _In_ ITfContext *pContext, UINT code)
{
#ifdef DIME_USE_RUST_ENGINE
	CRustSessionHost host(this, ec, pContext);
	return dime_session_handle_sentence_backspace(host.Get(), code);
#else
	return _HandleSentenceBackspaceCpp(ec, pContext, code);
#endif
}
#endif

HRESULT CDIME::_HandleSentenceBackspaceCpp(TfEditCookie ec, _In_ ITfContext *pContext, UINT code)
{
	BOOL removed = (code == VK_DELETE) ? _sentence.RemoveAtCursor() : _sentence.RemoveBeforeCursor();
	if (!removed)
		return S_OK;

	if (_sentence.IsEmpty())
		return _HandleCancel(ec, pContext);  // nothing left: drop the composition

	_SentenceConvert();
	return _SentenceRender(ec, pContext, L"");
}

//+---------------------------------------------------------------------------
//
// _HandleSentenceArrowKey
//
//----------------------------------------------------------------------------

#ifdef DIME_COMPILE_ROUTING_HANDLERS
HRESULT CDIME::_HandleSentenceArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
#ifdef DIME_USE_RUST_ENGINE
	CRustSessionHost host(this, ec, pContext);
	return dime_session_handle_sentence_arrow_key(host.Get(), (uint32_t)keyFunction);
#else
	return _HandleSentenceArrowKeyCpp(ec, pContext, keyFunction);
#endif
}
#endif

HRESULT CDIME::_HandleSentenceArrowKeyCpp(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
	switch (keyFunction)
	{
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT:        _sentence.MoveLeft(); break;
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT:       _sentence.MoveRight(); break;
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP:    _sentence.Home(); break;
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM: _sentence.End(); break;
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN:        return _SentenceOpenCandidates(ec, pContext);
	default:                                            return S_OK;
	}
	return _SentenceRender(ec, pContext, L"");
}

//+---------------------------------------------------------------------------
//
// _SentenceFinalizeInComposition
//
// The composition is being ended by someone else (focus change, mode
// switch, deactivation): leave the converted sentence in the document
// instead of the raw reading.
//----------------------------------------------------------------------------

void CDIME::_SentenceFinalizeInComposition(TfEditCookie ec)
{
	if (!_IsSentencePending() || _pComposition == nullptr)
		return;

	_SentenceLearn();

	ITfRange* pRange = nullptr;
	if (SUCCEEDED(_pComposition->GetRange(&pRange)) && pRange)
	{
#ifdef DIME_USE_RUST_ENGINE
		std::wstring text;
		if (DimeBuffer* pText = dime_session_sentence_text(_pRustSession))
		{
			size_t len = 0;
			const uint16_t* p = dime_buffer_data(pText, &len);
			if (p) text.assign((const WCHAR*)p, len);
			dime_buffer_free(pText);
		}
#else
		std::wstring text = _sentence.Text();
#endif
		pRange->SetText(ec, 0, text.c_str(), (LONG)text.length());
		pRange->Release();
	}
	_SentenceReset();
}

void CDIME::_SentenceReset()
{
#ifdef DIME_USE_RUST_ENGINE
	dime_session_sentence_reset(_pRustSession);
#endif
	_sentence.Clear();
	_sentenceEditPos = -1;
	_sentencePinNext = FALSE;
	_sentenceDisplay.clear();
	lastReadingString.Set(nullptr, 0);
}
