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

#include "SentenceBuffer.h"

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#endif

namespace
{
#ifdef DIME_RUST_AVAILABLE
	static_assert(sizeof(WCHAR) == sizeof(uint16_t), "WCHAR must be UTF-16");
	inline const uint16_t* U16(const WCHAR* p) { return reinterpret_cast<const uint16_t*>(p); }
#endif
}

//////////////////////////////////////////////////////////////////////
// CSentenceLexicon
//////////////////////////////////////////////////////////////////////

CSentenceLexicon::CSentenceLexicon() : _pLexicon(nullptr)
{
#ifdef DIME_RUST_AVAILABLE
	_pLexicon = dime_lexicon_new();
#endif
}

CSentenceLexicon::~CSentenceLexicon()
{
#ifdef DIME_RUST_AVAILABLE
	dime_lexicon_free(_pLexicon);
#endif
	_pLexicon = nullptr;
}

bool CSentenceLexicon::IsSupported()
{
#ifdef DIME_RUST_AVAILABLE
	return true;
#else
	return false;
#endif
}

size_t CSentenceLexicon::LoadCinValues(_In_reads_(len) const WCHAR* buf, size_t len)
{
#ifdef DIME_RUST_AVAILABLE
	if (_pLexicon && buf) return dime_lexicon_load_cin_values(_pLexicon, U16(buf), len);
#else
	buf; len;
#endif
	return 0;
}

size_t CSentenceLexicon::LoadFreqTable(_In_reads_(len) const WCHAR* buf, size_t len)
{
#ifdef DIME_RUST_AVAILABLE
	if (_pLexicon && buf) return dime_lexicon_load_freq_table(_pLexicon, U16(buf), len);
#else
	buf; len;
#endif
	return 0;
}

bool CSentenceLexicon::AddWord(const std::wstring& word, UINT freq)
{
#ifdef DIME_RUST_AVAILABLE
	if (_pLexicon) return dime_lexicon_add_word(_pLexicon, U16(word.c_str()), word.length(), freq) != 0;
#else
	word; freq;
#endif
	return false;
}

size_t CSentenceLexicon::WordCount() const
{
#ifdef DIME_RUST_AVAILABLE
	return dime_lexicon_word_count(_pLexicon);
#else
	return 0;
#endif
}

size_t CSentenceLexicon::CharCount() const
{
#ifdef DIME_RUST_AVAILABLE
	return dime_lexicon_char_count(_pLexicon);
#else
	return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// CSentenceBuffer
//////////////////////////////////////////////////////////////////////

bool CSentenceBuffer::Insert(const std::wstring& code, const std::vector<std::wstring>& candidates, int pinned)
{
	if (candidates.empty()) return false;

	CSentencePosition position;
	position.code = code;
	position.candidates = candidates;
	position.pinned = (pinned >= 0 && (size_t)pinned < candidates.size()) ? pinned : -1;
	position.chosen = position.pinned >= 0 ? position.pinned : 0;

	_positions.insert(_positions.begin() + _cursor, position);
	_cursor++;
	return true;
}

bool CSentenceBuffer::RemoveBeforeCursor()
{
	if (_cursor == 0) return false;
	_positions.erase(_positions.begin() + (_cursor - 1));
	_cursor--;
	return true;
}

bool CSentenceBuffer::RemoveAtCursor()
{
	if (_cursor >= _positions.size()) return false;
	_positions.erase(_positions.begin() + _cursor);
	return true;
}

size_t CSentenceBuffer::EditTarget() const
{
	if (_positions.empty()) return 0;
	return _cursor < _positions.size() ? _cursor : _positions.size() - 1;
}

bool CSentenceBuffer::Pin(size_t pos, int index)
{
	if (pos >= _positions.size()) return false;
	CSentencePosition& position = _positions[pos];
	if (index >= (int)position.candidates.size()) return false;
	position.pinned = index < 0 ? -1 : index;
	if (position.pinned >= 0) position.chosen = position.pinned;
	return true;
}

void CSentenceBuffer::Convert(_In_opt_ const CSentenceLexicon* pLexicon)
{
	if (_positions.empty()) return;

#ifdef DIME_RUST_AVAILABLE
	std::vector<size_t> starts;
	std::vector<const uint16_t*> cands;
	std::vector<size_t> lens;
	std::vector<int32_t> pinned;
	std::vector<int32_t> chosen(_positions.size(), 0);
	starts.reserve(_positions.size() + 1);
	pinned.reserve(_positions.size());

	starts.push_back(0);
	for (const CSentencePosition& position : _positions)
	{
		for (const std::wstring& c : position.candidates)
		{
			cands.push_back(U16(c.c_str()));
			lens.push_back(c.length());
		}
		starts.push_back(cands.size());
		pinned.push_back(position.pinned);
	}

	if (dime_sentence_convert(pLexicon ? pLexicon->Handle() : nullptr, _positions.size(), starts.data(),
		cands.data(), lens.data(), pinned.data(), chosen.data()))
	{
		for (size_t i = 0; i < _positions.size(); i++)
		{
			int k = chosen[i];
			_positions[i].chosen = (k >= 0 && (size_t)k < _positions[i].candidates.size()) ? k : 0;
		}
		return;
	}
#else
	pLexicon;
#endif

	for (CSentencePosition& position : _positions)
		position.chosen = position.pinned >= 0 ? position.pinned : 0;
}

std::wstring CSentenceBuffer::Text() const
{
	std::wstring text;
	for (const CSentencePosition& position : _positions)
		text += position.candidates[position.chosen];
	return text;
}

size_t CSentenceBuffer::TextLength(size_t n) const
{
	size_t length = 0;
	for (size_t i = 0; i < n && i < _positions.size(); i++)
		length += _positions[i].candidates[_positions[i].chosen].length();
	return length;
}

std::wstring CSentenceBuffer::Display(const std::wstring& reading) const
{
	std::wstring text = Text();
	text.insert(TextLength(_cursor), reading);
	return text;
}
