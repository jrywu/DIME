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

	// The flattened arrays dime_sentence_convert / dime_user_model_learn take.
	struct SentenceArrays
	{
		std::vector<const uint16_t*> codes;
		std::vector<size_t> codeLens;
		std::vector<size_t> starts;
		std::vector<const uint16_t*> cands;
		std::vector<size_t> candLens;
		std::vector<int32_t> pinned;
		std::vector<int32_t> chosen;

		explicit SentenceArrays(const CSentenceBuffer& b)
		{
			starts.push_back(0);
			for (size_t i = 0; i < b.Count(); i++)
			{
				const CSentencePosition& position = b.At(i);
				codes.push_back(U16(position.code.c_str()));
				codeLens.push_back(position.code.length());
				for (const std::wstring& c : position.candidates)
				{
					cands.push_back(U16(c.c_str()));
					candLens.push_back(c.length());
				}
				starts.push_back(cands.size());
				pinned.push_back(position.pinned);
				chosen.push_back(position.chosen);
			}
		}
	};
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
// CSentenceUserModel
//////////////////////////////////////////////////////////////////////

CSentenceUserModel::CSentenceUserModel() : _pModel(nullptr)
{
#ifdef DIME_RUST_AVAILABLE
	_pModel = dime_user_model_new();
#endif
}

CSentenceUserModel::~CSentenceUserModel()
{
#ifdef DIME_RUST_AVAILABLE
	dime_user_model_free(_pModel);
#endif
	_pModel = nullptr;
}

size_t CSentenceUserModel::Import(_In_reads_(len) const WCHAR* buf, size_t len)
{
#ifdef DIME_RUST_AVAILABLE
	if (_pModel && buf) return dime_user_model_import(_pModel, U16(buf), len);
#else
	buf; len;
#endif
	return 0;
}

std::wstring CSentenceUserModel::Export() const
{
	std::wstring text;
#ifdef DIME_RUST_AVAILABLE
	DimeBuffer* pBuffer = dime_user_model_export(_pModel);
	if (pBuffer)
	{
		size_t len = 0;
		const uint16_t* p = dime_buffer_data(pBuffer, &len);
		if (p) text.assign(reinterpret_cast<const WCHAR*>(p), len);
		dime_buffer_free(pBuffer);
	}
#endif
	return text;
}

void CSentenceUserModel::Merge(const CSentenceUserModel& other)
{
#ifdef DIME_RUST_AVAILABLE
	dime_user_model_merge(_pModel, other._pModel);
#else
	other;
#endif
}

void CSentenceUserModel::Clear()
{
#ifdef DIME_RUST_AVAILABLE
	dime_user_model_clear(_pModel);
#endif
}

size_t CSentenceUserModel::WordCount() const
{
#ifdef DIME_RUST_AVAILABLE
	return dime_user_model_word_count(_pModel);
#else
	return 0;
#endif
}

size_t CSentenceUserModel::ChoiceCount() const
{
#ifdef DIME_RUST_AVAILABLE
	return dime_user_model_choice_count(_pModel);
#else
	return 0;
#endif
}

bool CSentenceUserModel::Learn(const CSentenceBuffer& sentence)
{
#ifdef DIME_RUST_AVAILABLE
	if (_pModel == nullptr || sentence.IsEmpty()) return false;
	SentenceArrays a(sentence);
	return dime_user_model_learn(_pModel, sentence.Count(), a.codes.data(), a.codeLens.data(), a.starts.data(),
		a.cands.data(), a.candLens.data(), a.pinned.data(), a.chosen.data()) != 0;
#else
	sentence;
	return false;
#endif
}

size_t CSentenceUserModel::LoadFile(_In_z_ LPCWSTR path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return 0;

	size_t imported = 0;
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024)
	{
		std::vector<WCHAR> data((size_t)size.QuadPart / sizeof(WCHAR) + 1, 0);
		DWORD read = 0;
		if (ReadFile(h, data.data(), (DWORD)size.QuadPart, &read, nullptr))
		{
			size_t len = read / sizeof(WCHAR);
			const WCHAR* p = data.data();
			if (len && *p == 0xFEFF) { p++; len--; }
			imported = Import(p, len);
		}
	}
	CloseHandle(h);
	return imported;
}

BOOL CSentenceUserModel::SaveFile(_In_z_ LPCWSTR path) const
{
	std::wstring text = Export();
	if (text.empty()) return FALSE;

	std::wstring temp = std::wstring(path) + L".tmp";
	HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return FALSE;

	const WCHAR bom = 0xFEFF;
	DWORD written = 0;
	DWORD bytes = (DWORD)(text.length() * sizeof(WCHAR));
	BOOL ok = WriteFile(h, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom) &&
		WriteFile(h, text.data(), bytes, &written, nullptr) && written == bytes;
	CloseHandle(h);

	if (ok)
		ok = MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	if (!ok)
		DeleteFileW(temp.c_str());
	return ok;
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

void CSentenceBuffer::Convert(_In_opt_ const CSentenceLexicon* pLexicon, _In_opt_ const CSentenceUserModel* pUser)
{
	if (_positions.empty()) return;

#ifdef DIME_RUST_AVAILABLE
	SentenceArrays a(*this);
	if (dime_sentence_convert(pLexicon ? pLexicon->Handle() : nullptr, pUser ? pUser->Handle() : nullptr,
		_positions.size(), a.codes.data(), a.codeLens.data(), a.starts.data(),
		a.cands.data(), a.candLens.data(), a.pinned.data(), a.chosen.data()))
	{
		for (size_t i = 0; i < _positions.size(); i++)
		{
			int k = a.chosen[i];
			_positions[i].chosen = (k >= 0 && (size_t)k < _positions[i].candidates.size()) ? k : 0;
		}
		return;
	}
#else
	pLexicon; pUser;
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
