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

#pragma once

#include "TableDictionaryEngine.h"

#ifdef DIME_RUST_AVAILABLE

#include <string>
#include <unordered_set>

struct DimeEngine;
struct DimeItems;

//////////////////////////////////////////////////////////////////////
//
// CRustTableDictionaryEngine
//
// Same interface and behaviour as CTableDictionaryEngine, but the
// dictionary parsing/search runs in the Rust dime-core library
// (rust/crates/dime-ffi). Created by CTableDictionaryEngine::Create()
// when DIME is built with DIME_USE_RUST_ENGINE.
//
// - The dictionary buffer still comes from CFile / CMemoryFile (including
//   reload-on-change and the Big5 filter); it is copied into Rust whenever
//   the file reports a reload.
// - Result strings are interned in this engine, so the CStringRange
//   pointers handed out stay valid for the engine's lifetime (the C++
//   engine hands out pointers into the file buffer).
// - Sorting stays in C++ (inherited): callers sort lists that may mix
//   items from several engines.
//
//////////////////////////////////////////////////////////////////////

class CRustTableDictionaryEngine : public CTableDictionaryEngine
{
public:
	CRustTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ DICTIONARY_TYPE dictionaryType);
	~CRustTableDictionaryEngine() override;

	VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings) override;
	VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList) override;
	VOID CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, _In_opt_ CTableDictionaryEngine* wordFreqTableDictionaryEngine = nullptr) override;
	VOID CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList) override;
	VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList) override;
	VOID ParseConfig(IME_MODE imeMode) override;

private:
	// Makes sure the Rust engine exists and holds the current file buffer.
	DimeEngine* Sync();
	void AppendItems(_In_opt_ DimeItems* items, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
	const std::wstring& Intern(const WCHAR* p, size_t len);

	DimeEngine* _pRust = nullptr;
	const WCHAR* _pSyncedBuffer = nullptr;
	size_t _syncedLength = 0;
	std::unordered_set<std::wstring> _stringPool;   // node-based: element addresses are stable
};

#endif // DIME_RUST_AVAILABLE
