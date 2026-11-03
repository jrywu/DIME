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

#include "Private.h"
#include "RustTableDictionaryEngine.h"

#ifdef DIME_RUST_AVAILABLE

#include "DictionarySearch.h"
#include "Config.h"
#include "dime_ffi.h"
#include <vector>

namespace
{
	inline const uint16_t* U16(const WCHAR* p) { return reinterpret_cast<const uint16_t*>(p); }
	inline const WCHAR* W(const uint16_t* p) { return reinterpret_cast<const WCHAR*>(p); }
}

CRustTableDictionaryEngine::CRustTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ DICTIONARY_TYPE dictionaryType)
	: CTableDictionaryEngine(locale, pDictionaryFile, dictionaryType)
{
}

CRustTableDictionaryEngine::~CRustTableDictionaryEngine()
{
	dime_engine_free(_pRust);
	_pRust = nullptr;
}

//+---------------------------------------------------------------------------
//
// Sync
//
// Mirrors CDictionarySearch: every search asks the CFile for its buffer,
// which also reloads the file when it changed on disk. A reload (or a new
// buffer pointer) is pushed into the Rust engine.
//
//----------------------------------------------------------------------------

DimeEngine* CRustTableDictionaryEngine::Sync()
{
	if (_pDictionaryFile == nullptr) return nullptr;

	BOOL fileReloaded = FALSE;
	const WCHAR* pBuffer = _pDictionaryFile->GetReadBufferPointer(&fileReloaded);
	if (pBuffer == nullptr) return nullptr;
	size_t length = (size_t)(_pDictionaryFile->GetFileSize() / sizeof(WCHAR));

	if (_pRust == nullptr)
	{
		_pRust = dime_engine_new(_locale, U16(pBuffer), length, (int32_t)_dictionaryType);
		if (_pRust == nullptr) return nullptr;
	}
	else if (fileReloaded || pBuffer != _pSyncedBuffer || length != _syncedLength)
	{
		dime_engine_set_buffer(_pRust, U16(pBuffer), length);
	}
	_pSyncedBuffer = pBuffer;
	_syncedLength = length;

	dime_engine_set_search_section(_pRust, (int32_t)_searchSection);
	dime_engine_set_has_cin_phrase_section(_pRust, Global::hasCINPhraseSection);
	return _pRust;
}

const std::wstring& CRustTableDictionaryEngine::Intern(const WCHAR* p, size_t len)
{
	return *_stringPool.emplace(p ? p : L"", p ? len : 0).first;
}

void CRustTableDictionaryEngine::AppendItems(_In_opt_ DimeItems* items, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
	if (items == nullptr) return;
	size_t count = dime_items_count(items);
	for (size_t i = 0; i < count; i++)
	{
		// Fetch pointer and length first: argument evaluation order is unspecified.
		size_t itemLen = 0, keyLen = 0;
		const WCHAR* pItem = W(dime_items_item(items, i, &itemLen));
		const WCHAR* pKey = W(dime_items_find_key(items, i, &keyLen));
		const std::wstring& item = Intern(pItem, itemLen);
		const std::wstring& key = Intern(pKey, keyLen);

		CCandidateListItem* pLI = pItemList->Append();
		if (pLI)
		{
			pLI->_ItemString.Set(item.c_str(), item.length());
			pLI->_FindKeyCode.Set(key.c_str(), key.length());
			pLI->_WordFrequency = dime_items_word_frequency(items, i);
		}
	}
	dime_items_free(items);
}

//+---------------------------------------------------------------------------
//
// Searches
//
//----------------------------------------------------------------------------

VOID CRustTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
	DimeEngine* pRust = Sync();
	if (pRust == nullptr || pKeyCode == nullptr || pWordStrings == nullptr) return;

	DimeItems* items = dime_engine_collect_word_strings(pRust, U16(pKeyCode->Get()), pKeyCode->GetLength());
	if (items == nullptr) return;
	size_t count = dime_items_count(items);
	for (size_t i = 0; i < count; i++)
	{
		size_t len = 0;
		const WCHAR* p = W(dime_items_item(items, i, &len));
		const std::wstring& s = Intern(p, len);
		CStringRange* pPhrase = pWordStrings->Append();
		if (pPhrase) pPhrase->Set(s.c_str(), s.length());
	}
	dime_items_free(items);
}

VOID CRustTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
	DimeEngine* pRust = Sync();
	if (pRust == nullptr || pKeyCode == nullptr || pItemList == nullptr) return;
	AppendItems(dime_engine_collect_word(pRust, U16(pKeyCode->Get()), pKeyCode->GetLength()), pItemList);
}

VOID CRustTableDictionaryEngine::CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, _In_opt_ CTableDictionaryEngine* wordFreqTableDictionaryEngine)
{
	DimeEngine* pFreq = nullptr;
	if (wordFreqTableDictionaryEngine)
	{
		CRustTableDictionaryEngine* pRustFreq = dynamic_cast<CRustTableDictionaryEngine*>(wordFreqTableDictionaryEngine);
		if (pRustFreq == nullptr)
		{
			// Mixed engines: fall back to the C++ implementation.
			CTableDictionaryEngine::CollectWordForWildcard(psrgKeyCode, pItemList, wordFreqTableDictionaryEngine);
			return;
		}
		pFreq = pRustFreq->Sync();
	}

	DimeEngine* pRust = Sync();
	if (pRust == nullptr || psrgKeyCode == nullptr || pItemList == nullptr) return;

	// The C++ engine de-duplicates against everything already in the list
	// (callers accumulate results from several engines), so pass those along.
	std::vector<const uint16_t*> existing;
	std::vector<size_t> existingLens;
	existing.reserve(pItemList->Count());
	existingLens.reserve(pItemList->Count());
	for (UINT i = 0; i < pItemList->Count(); i++)
	{
		existing.push_back(U16(pItemList->GetAt(i)->_ItemString.Get()));
		existingLens.push_back(pItemList->GetAt(i)->_ItemString.GetLength());
	}
	AppendItems(dime_engine_collect_word_for_wildcard(pRust, U16(psrgKeyCode->Get()), psrgKeyCode->GetLength(), pFreq,
		existing.data(), existingLens.data(), existing.size()), pItemList);
}

VOID CRustTableDictionaryEngine::CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
	DimeEngine* pRust = Sync();
	if (pRust == nullptr || pString == nullptr || pItemList == nullptr) return;
	AppendItems(dime_engine_collect_word_from_converted_string(pRust, U16(pString->Get()), pString->GetLength()), pItemList);
}

VOID CRustTableDictionaryEngine::CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
	DimeEngine* pRust = Sync();
	if (pRust == nullptr || pString == nullptr || pItemList == nullptr) return;
	AppendItems(dime_engine_collect_word_from_converted_string_for_wildcard(pRust, U16(pString->Get()), pString->GetLength()), pItemList);
}

//+---------------------------------------------------------------------------
//
// ParseConfig
//
// Applies everything the C++ ParseConfig writes: radical map, selkey/endkey,
// CConfig (%autoCompose and [Config] entries) and Global::hasPhraseSection.
//
//----------------------------------------------------------------------------

VOID CRustTableDictionaryEngine::ParseConfig(IME_MODE imeMode)
{
	UNREFERENCED_PARAMETER(imeMode);

	if (_dictionaryType != DICTIONARY_TYPE::INI_DICTIONARY && _pRadicalMap->size())
	{
		for (auto& item : *_pRadicalMap) delete[] item.second;
		_pRadicalMap->clear();
	}

	DimeEngine* pRust = Sync();
	if (pRust == nullptr || !dime_engine_parse_config(pRust)) return;

	// Radical map (%keyname / [Radical])
	size_t radicalCount = dime_engine_radical_count(pRust);
	for (size_t i = 0; i < radicalCount; i++)
	{
		uint16_t key = 0;
		const uint16_t* value = nullptr;
		size_t valueLen = 0;
		if (!dime_engine_radical_at(pRust, i, &key, &value, &valueLen)) continue;

		PWCHAR radical = new (std::nothrow) WCHAR[16];   // same capacity as the C++ engine
		if (radical == nullptr) continue;
		StringCchCopyN(radical, 16, W(value), valueLen);

		auto it = _pRadicalMap->find((WCHAR)key);
		if (it != _pRadicalMap->end())
		{
			delete[] it->second;
			it->second = radical;
		}
		else
		{
			(*_pRadicalMap)[(WCHAR)key] = radical;
		}
	}

	// %selkey / %endkey (truncated to MAX_CAND_SELKEY - 1 like StringCchCopyN)
	WCHAR keys[MAX_CAND_SELKEY] = {};
	size_t n = (std::min)(dime_engine_get_selkey(pRust, reinterpret_cast<uint16_t*>(keys), MAX_CAND_SELKEY - 1), (size_t)MAX_CAND_SELKEY - 1);
	keys[n] = L'\0';
	StringCchCopy(_pSelkey, MAX_CAND_SELKEY, keys);
	n = (std::min)(dime_engine_get_endkey(pRust, reinterpret_cast<uint16_t*>(keys), MAX_CAND_SELKEY - 1), (size_t)MAX_CAND_SELKEY - 1);
	keys[n] = L'\0';
	StringCchCopy(_pEndkey, MAX_CAND_SELKEY, keys);

	_sortedCIN = dime_engine_is_sorted_cin(pRust);

	if (dime_engine_has_phrase_section(pRust)) Global::hasPhraseSection = TRUE;

	int32_t autoCompose = dime_engine_auto_compose(pRust);
	if (autoCompose >= 0) CConfig::SetAutoCompose(autoCompose != 0);

	// [Config] entries -> CConfig. Copy into NUL-terminated strings because
	// ApplyIniConfigEntry parses numbers with _wtoi/wcstoul.
	size_t entryCount = dime_engine_config_entry_count(pRust);
	for (size_t i = 0; i < entryCount; i++)
	{
		const uint16_t *k = nullptr, *v = nullptr;
		size_t kLen = 0, vLen = 0;
		if (!dime_engine_config_entry_at(pRust, i, &k, &kLen, &v, &vLen)) continue;
		std::wstring key(W(k), kLen), value(W(v), vLen);
		CStringRange keyRange, valueRange;
		keyRange.Set(key.c_str(), key.length());
		valueRange.Set(value.c_str(), value.length());
		ApplyIniConfigEntry(_locale, &keyRange, &valueRange);
	}
}

#endif // DIME_RUST_AVAILABLE
