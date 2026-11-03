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

// Sentence mode ("continuous input"): the user types one code per character
// without choosing candidates; each typed character becomes a position that
// keeps its whole candidate list, and the conversion (dime_core::sentence in
// Rust) picks the candidates that read best together.  Positions the user
// fixes by hand are pinned, and on commit CSentenceUserModel learns from them
// (preferred character per code, and the pairs it forms with its neighbours).
//
// This file is free of TSF so it can be unit tested; CDIME drives it from
// SentenceHandler.cpp.

#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct DimeLexicon;
struct DimeUserModel;
class CSentenceBuffer;

// Word list + character frequencies used by the conversion.  Without the Rust
// library (DIME_RUST_AVAILABLE undefined) it is an empty shell and the
// conversion falls back to the first (or pinned) candidate of every position.
class CSentenceLexicon
{
public:
	CSentenceLexicon();
	~CSentenceLexicon();
	CSentenceLexicon(const CSentenceLexicon&) = delete;
	CSentenceLexicon& operator=(const CSentenceLexicon&) = delete;

	static bool IsSupported();

	// Multi-character values of a .cin table become words (e.g. Array-Phrase.cin).
	size_t LoadCinValues(_In_reads_(len) const WCHAR* buf, size_t len);
	// "text<TAB>count" lines (e.g. TCFreq.cin).
	size_t LoadFreqTable(_In_reads_(len) const WCHAR* buf, size_t len);
	bool AddWord(const std::wstring& word, UINT freq = 0);
	size_t WordCount() const;
	size_t CharCount() const;

	DimeLexicon* Handle() const { return _pLexicon; }

private:
	DimeLexicon* _pLexicon;
};

// What the user taught sentence mode.  Persisted as text (UTF-16 with BOM);
// several processes share one file, so a save re-reads the file and adds only
// this process's new data (kept separately as "pending").
class CSentenceUserModel
{
public:
	CSentenceUserModel();
	~CSentenceUserModel();
	CSentenceUserModel(const CSentenceUserModel&) = delete;
	CSentenceUserModel& operator=(const CSentenceUserModel&) = delete;

	size_t Import(_In_reads_(len) const WCHAR* buf, size_t len);
	std::wstring Export() const;
	void Merge(const CSentenceUserModel& other);
	void Clear();
	size_t WordCount() const;
	size_t ChoiceCount() const;
	// Learn from the pinned positions of a sentence about to be committed.
	bool Learn(const CSentenceBuffer& sentence);

	// Add the entries of a file written by SaveFile (missing file: 0).
	size_t LoadFile(_In_z_ LPCWSTR path);
	// Write Export() to path (via a temporary file).  FALSE on failure.
	BOOL SaveFile(_In_z_ LPCWSTR path) const;

	DimeUserModel* Handle() const { return _pModel; }

private:
	DimeUserModel* _pModel;
};

struct CSentencePosition
{
	std::wstring code;                     // keystrokes typed for this character
	std::vector<std::wstring> candidates;  // in table order, never empty
	int pinned = -1;                       // candidate fixed by the user, or -1
	int chosen = 0;                        // candidate picked by the conversion
};

class CSentenceBuffer
{
public:
	// Longest sentence kept in the composition; reaching it commits the sentence.
	static const size_t MaxPositions = 40;

	bool IsEmpty() const { return _positions.empty(); }
	size_t Count() const { return _positions.size(); }
	const CSentencePosition& At(size_t i) const { return _positions[i]; }

	// Cursor: number of positions before the insertion point (0..Count()).
	size_t Cursor() const { return _cursor; }
	void MoveLeft() { if (_cursor > 0) _cursor--; }
	void MoveRight() { if (_cursor < _positions.size()) _cursor++; }
	void Home() { _cursor = 0; }
	void End() { _cursor = _positions.size(); }

	// Insert a position at the cursor and move the cursor past it.  Empty
	// candidate lists are ignored.  pinned outside the list means "not pinned".
	bool Insert(const std::wstring& code, const std::vector<std::wstring>& candidates, int pinned);
	bool RemoveBeforeCursor();
	bool RemoveAtCursor();
	void Clear() { _positions.clear(); _cursor = 0; }

	// The position the candidate window edits: the one under the cursor, or
	// the last one when the cursor is at the end.  Count() when empty.
	size_t EditTarget() const;
	// Fix position pos to candidate index (or unpin with -1).
	bool Pin(size_t pos, int index);

	// Run the conversion and update every position's chosen candidate.
	void Convert(_In_opt_ const CSentenceLexicon* pLexicon, _In_opt_ const CSentenceUserModel* pUser = nullptr);

	// Concatenated chosen candidates.
	std::wstring Text() const;
	// Length (in WCHARs) of the text of the first n positions.
	size_t TextLength(size_t n) const;
	// Text of positions [0, Cursor()) + reading + text of [Cursor(), Count()).
	std::wstring Display(const std::wstring& reading) const;
	// Offset of the caret in Display(reading).
	size_t DisplayCaret(const std::wstring& reading) const { return TextLength(_cursor) + reading.length(); }

private:
	std::vector<CSentencePosition> _positions;
	size_t _cursor = 0;
};
