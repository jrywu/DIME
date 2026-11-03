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

/********************************************************************************
* Rust parity tests (Rust migration, phase 2)
*
* Runs the C++ dictionary stack (CTableDictionaryEngine / CDictionarySearch /
* CMemoryFile) and the Rust port (rust/crates/dime-core via dime_ffi.lib) on
* the same input and asserts identical output:
*   - every UTF-16 table shipped in installer/*.cin
*   - synthetic CIN / TTS sources covering quoting, escapes, CRLF, sections
*
* Only compiled for Win32 / x64 (DIME_RUST_AVAILABLE is defined by the project
* when a matching Rust target exists).
********************************************************************************/

#include "pch.h"

#ifdef DIME_RUST_AVAILABLE

#include "CppUnitTest.h"
#include "../TableDictionaryEngine.h"
#include "../File.h"
#include "../RustTableDictionaryEngine.h"
#include "dime_ffi.h"
#include <algorithm>
#include <set>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        const LCID kLcid = MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT);

        struct Item
        {
            std::wstring item, key;
            int freq = 0;
            bool operator==(const Item& o) const { return item == o.item && key == o.key && freq == o.freq; }
        };
        using Items = std::vector<Item>;

        std::wstring RepoRoot()
        {
            std::wstring p = __FILEW__;            // ...\src\tests\RustParityTest.cpp
            for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
            return p + L"\\";
        }

        Items FromCpp(CDIMEArray<CCandidateListItem>& list)
        {
            Items out;
            for (UINT i = 0; i < list.Count(); i++)
            {
                CCandidateListItem* it = list.GetAt(i);
                out.push_back({ std::wstring(it->_ItemString.Get(), it->_ItemString.GetLength()),
                                std::wstring(it->_FindKeyCode.Get(), it->_FindKeyCode.GetLength()),
                                it->_WordFrequency });
            }
            return out;
        }

        Items FromRust(DimeItems* items)
        {
            Assert::IsNotNull(items, L"Rust returned NULL item list");
            Items out;
            for (size_t i = 0; i < dime_items_count(items); i++)
            {
                size_t n = 0, k = 0;
                const uint16_t* s = dime_items_item(items, i, &n);
                const uint16_t* f = dime_items_find_key(items, i, &k);
                out.push_back({ std::wstring((const wchar_t*)s, n), std::wstring((const wchar_t*)f, k),
                                dime_items_word_frequency(items, i) });
            }
            dime_items_free(items);
            return out;
        }

        void AssertSame(const Items& cpp, const Items& rust, const std::wstring& what)
        {
            if (cpp == rust) return;
            std::wstring msg = what + L": C++ " + std::to_wstring(cpp.size()) + L" items, Rust " + std::to_wstring(rust.size());
            for (size_t i = 0; i < (std::max)(cpp.size(), rust.size()); i++)
            {
                const Item* a = i < cpp.size() ? &cpp[i] : nullptr;
                const Item* b = i < rust.size() ? &rust[i] : nullptr;
                if (!a || !b || !(*a == *b))
                {
                    msg += L"; first diff at #" + std::to_wstring(i) + L": C++=[" + (a ? a->key + L" " + a->item : L"-") +
                           L"] Rust=[" + (b ? b->key + L" " + b->item : L"-") + L"]";
                    break;
                }
            }
            Assert::Fail(msg.c_str());
        }

        // A dictionary loaded once and wrapped by both engines.
        struct Pair
        {
            CFile file;
            CTableDictionaryEngine* cpp = nullptr;
            DimeEngine* rust = nullptr;
            std::wstring name;

            ~Pair()
            {
                delete cpp;
                dime_engine_free(rust);
            }

            bool Open(const std::wstring& path, DICTIONARY_TYPE type)
            {
                name = path.substr(path.find_last_of(L'\\') + 1);
                if (!file.CreateFile(path.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ)) return false;
                const WCHAR* buf = file.GetReadBufferPointer();
                if (!buf) return false;
                cpp = new CTableDictionaryEngine(kLcid, &file, type);
                rust = dime_engine_new(kLcid, (const uint16_t*)buf, file.GetFileSize() / sizeof(WCHAR), (int32_t)type);
                Assert::IsNotNull(rust);
                dime_engine_set_has_cin_phrase_section(rust, Global::hasCINPhraseSection);

                BOOL savedAutoCompose = CConfig::GetAutoCompose();
                cpp->ParseConfig(IME_MODE::IME_MODE_GENERIC);
                CConfig::SetAutoCompose(savedAutoCompose);
                Assert::AreEqual(1, (int)dime_engine_parse_config(rust));
                return true;
            }

            void SetSection(SEARCH_SECTION s)
            {
                cpp->SetSearchSection(s);
                dime_engine_set_search_section(rust, (int32_t)s);
            }

            const WCHAR* Buffer() { return file.GetReadBufferPointer(); }
            size_t Length() { return file.GetFileSize() / sizeof(WCHAR); }

            void CheckConfig()
            {
                WCHAR sel[64] = {}, end[64] = {};
                size_t n = dime_engine_get_selkey(rust, (uint16_t*)sel, 63);
                Assert::AreEqual(std::wstring(cpp->GetSelkey()), std::wstring(sel, n), (name + L" selkey").c_str());
                n = dime_engine_get_endkey(rust, (uint16_t*)end, 63);
                Assert::AreEqual(std::wstring(cpp->GetEndkey()), std::wstring(end, n), (name + L" endkey").c_str());

                _T_RadicalMap* map = cpp->GetRadicalMap();
                Assert::AreEqual(map->size(), dime_engine_radical_count(rust), (name + L" radical count").c_str());
                size_t i = 0;
                for (auto& kv : *map)
                {
                    uint16_t key = 0; const uint16_t* v = nullptr; size_t vlen = 0;
                    Assert::AreEqual(1, (int)dime_engine_radical_at(rust, i++, &key, &v, &vlen));
                    Assert::AreEqual((int)kv.first, (int)key, (name + L" radical key").c_str());
                    Assert::AreEqual(std::wstring(kv.second), std::wstring((const wchar_t*)v, vlen), (name + L" radical value").c_str());
                }
            }

            void CheckCollectWord(const std::wstring& key)
            {
                CStringRange k; k.Set(key.c_str(), key.length());
                CDIMEArray<CCandidateListItem> list;
                cpp->CollectWord(&k, &list);
                AssertSame(FromCpp(list), FromRust(dime_engine_collect_word(rust, (const uint16_t*)key.c_str(), key.length())),
                           name + L" CollectWord(" + key + L")");
            }

            void CheckWildcard(const std::wstring& pattern, Pair* freq)
            {
                CStringRange k; k.Set(pattern.c_str(), pattern.length());
                CDIMEArray<CCandidateListItem> list;
                cpp->CollectWordForWildcard(&k, &list, freq ? freq->cpp : nullptr);
                DimeItems* r = dime_engine_collect_word_for_wildcard(rust, (const uint16_t*)pattern.c_str(), pattern.length(),
                                                                    freq ? freq->rust : nullptr, nullptr, nullptr, 0);
                AssertSame(FromCpp(list), FromRust(r), name + L" CollectWordForWildcard(" + pattern + L")");

                // Sorting: both sides sort their own (identical) lists.
                cpp->SortListItemByFindKeyCode(&list);
                r = dime_engine_collect_word_for_wildcard(rust, (const uint16_t*)pattern.c_str(), pattern.length(),
                                                         freq ? freq->rust : nullptr, nullptr, nullptr, 0);
                dime_engine_sort_by_find_key(rust, r);
                AssertSame(FromCpp(list), FromRust(r), name + L" SortByFindKeyCode(" + pattern + L")");

                if (freq)
                {
                    cpp->SortListItemByWordFrequency(&list);
                    r = dime_engine_collect_word_for_wildcard(rust, (const uint16_t*)pattern.c_str(), pattern.length(), freq->rust, nullptr, nullptr, 0);
                    dime_engine_sort_by_find_key(rust, r);
                    dime_engine_sort_by_word_frequency(rust, r);
                    AssertSame(FromCpp(list), FromRust(r), name + L" SortByWordFrequency(" + pattern + L")");
                }
            }

            void CheckReverse(const std::wstring& value)
            {
                CStringRange s; s.Set(value.c_str(), value.length());
                CDIMEArray<CCandidateListItem> list;
                cpp->CollectWordFromConvertedString(&s, &list);
                AssertSame(FromCpp(list),
                           FromRust(dime_engine_collect_word_from_converted_string(rust, (const uint16_t*)value.c_str(), value.length())),
                           name + L" CollectWordFromConvertedString(" + value + L")");

                std::wstring pattern = value + L"*";
                s.Set(pattern.c_str(), pattern.length());
                CDIMEArray<CCandidateListItem> list2;
                cpp->CollectWordFromConvertedStringForWildcard(&s, &list2);
                AssertSame(FromCpp(list2),
                           FromRust(dime_engine_collect_word_from_converted_string_for_wildcard(
                               rust, (const uint16_t*)pattern.c_str(), pattern.length())),
                           name + L" CollectWordFromConvertedStringForWildcard(" + pattern + L")");
            }
        };

        // Sample (key, value) pairs from the chardef/[Text] lines of a buffer:
        // every `step`-th mapping line, at most `limit` samples.
        std::vector<std::pair<std::wstring, std::wstring>> Sample(const WCHAR* buf, size_t len, size_t limit)
        {
            std::vector<std::wstring> lines;
            bool in = false;
            size_t i = 0;
            while (i < len)
            {
                size_t s = i;
                while (i < len && buf[i] != L'\n') i++;
                std::wstring line(buf + s, i - s);
                i++;
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (_wcsnicmp(line.c_str(), L"%chardef", 8) == 0 || (!line.empty() && line[0] == L'['))
                {
                    in = line.find(L"begin") != std::wstring::npos || _wcsicmp(line.c_str(), L"[Text]") == 0;
                    continue;
                }
                if (in && !line.empty() && line[0] != L'#' && line[0] != L'%') lines.push_back(line);
            }
            std::vector<std::pair<std::wstring, std::wstring>> out;
            size_t step = (std::max)((size_t)1, lines.size() / limit);
            for (size_t j = 0; j < lines.size() && out.size() < limit; j += step)
            {
                const std::wstring& l = lines[j];
                size_t d = l.find_first_of(L"\t =");
                if (d == std::wstring::npos) continue;
                std::wstring k = l.substr(0, d), v = l.substr(l.find_first_not_of(L"\t =", d) == std::wstring::npos ? l.size() : l.find_first_not_of(L"\t =", d));
                auto unq = [](std::wstring s) { return (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') ? s.substr(1, s.size() - 2) : s; };
                out.push_back({ unq(k), unq(v) });
            }
            return out;
        }

        std::wstring TempDir()
        {
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            std::wstring dir = std::wstring(tmp) + L"DIMETests_RustParity\\";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir;
        }

        std::wstring WriteUtf16(const std::wstring& name, const std::wstring& content)
        {
            std::wstring path = TempDir() + name;
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(h != INVALID_HANDLE_VALUE, L"cannot create temp file");
            DWORD w = 0;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(h, bom, 2, &w, nullptr);
            WriteFile(h, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &w, nullptr);
            CloseHandle(h);
            return path;
        }

        void CheckTable(Pair& p, size_t keyLimit, size_t wildcardLimit, Pair* freq)
        {
            p.CheckConfig();
            auto samples = Sample(p.Buffer(), p.Length(), keyLimit);
            std::set<std::wstring> seen;
            size_t wildcards = 0;
            for (auto& kv : samples)
            {
                const std::wstring& key = kv.first;
                if (!seen.insert(key).second) continue;
                p.CheckCollectWord(key);
                std::wstring upper = key;
                CharUpperBuffW(&upper[0], (DWORD)upper.size());
                if (upper != key) p.CheckCollectWord(upper);
                if (wildcards < wildcardLimit && key.size() >= 2)
                {
                    p.CheckWildcard(key.substr(0, key.size() - 1) + L"?", freq);
                    p.CheckWildcard(key.substr(0, 2) + L"*", freq);
                    wildcards++;
                }
            }
            for (size_t i = 0; i < samples.size() && i < 20; i++) p.CheckReverse(samples[i].second);
            p.CheckCollectWord(L"~nonexistent~");
            p.CheckCollectWord(L"");
        }

        void CheckBig5(const std::wstring& srcPath)
        {
            // Work on a temp copy: CMemoryFile writes a "-Big5" cache next to its source.
            std::wstring name = srcPath.substr(srcPath.find_last_of(L'\\') + 1);
            std::wstring copy = TempDir() + L"big5_" + name;
            Assert::IsTrue(CopyFileW(srcPath.c_str(), copy.c_str(), FALSE) != 0);
            {
                CFile src;
                Assert::IsTrue(src.CreateFile(copy.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ) != FALSE);
                const WCHAR* sbuf = src.GetReadBufferPointer();
                Assert::IsNotNull(sbuf);
                CMemoryFile mem(&src);
                const WCHAR* mbuf = mem.GetReadBufferPointer();
                Assert::IsNotNull(mbuf);
                std::wstring cpp(mbuf, mem.GetFileSize() / sizeof(WCHAR));

                DimeBuffer* b = dime_big5_filter(kLcid, (const uint16_t*)sbuf, src.GetFileSize() / sizeof(WCHAR));
                size_t n = 0;
                const uint16_t* r = dime_buffer_data(b, &n);
                std::wstring rust((const wchar_t*)r, n);
                dime_buffer_free(b);
                Assert::IsTrue(cpp == rust, (name + L": Big5 filter output differs (C++ " + std::to_wstring(cpp.size()) +
                                             L" units, Rust " + std::to_wstring(rust.size()) + L")").c_str());
            }
            std::wstring cache = copy.substr(0, copy.find_last_of(L'.')) + L"-Big5.cin";
            DeleteFileW(cache.c_str());
            DeleteFileW(copy.c_str());
        }
    }

    TEST_CLASS(RustParityTest)
    {
    public:
        TEST_METHOD(ShippedTables_SearchAndConfig)
        {
            std::wstring root = RepoRoot() + L"installer\\";
            Pair freq;
            bool haveFreq = freq.Open(root + L"TCFreq.cin", DICTIONARY_TYPE::CIN_DICTIONARY);

            const wchar_t* tables[] = { L"Array.cin", L"Array40.cin", L"Array-Ext-B.cin", L"Array-Ext-CD.cin",
                                        L"Array-Ext-EF.cin", L"Array-Phrase.cin", L"Array-shortcode.cin",
                                        L"Array-special.cin", L"phone.cin", L"TCFreq.cin", L"TCSC.cin" };
            int checked = 0;
            for (const wchar_t* t : tables)
            {
                Pair p;
                if (!p.Open(root + t, DICTIONARY_TYPE::CIN_DICTIONARY))
                {
                    Logger::WriteMessage((std::wstring(L"skip (cannot open): ") + t + L"\n").c_str());
                    continue;
                }
                CheckTable(p, 60, 8, haveFreq ? &freq : nullptr);
                Logger::WriteMessage((std::wstring(L"parity OK: ") + t + L"\n").c_str());
                checked++;
            }
            Assert::IsTrue(checked >= 8, L"expected the shipped installer tables to be present");
        }

        TEST_METHOD(ShippedTables_Big5Filter)
        {
            std::wstring root = RepoRoot() + L"installer\\";
            for (const wchar_t* t : { L"Array.cin", L"Array-Ext-B.cin", L"Array-special.cin", L"phone.cin" })
                CheckBig5(root + t);
        }

        TEST_METHOD(SyntheticCin_EdgeCases)
        {
            std::wstring src =
                L"# comment\r\n"
                L"%gen_inp\r\n"
                L"%ename Edge\r\n"
                L"%selkey\t1234567890abcdef\r\n"   // 16 units: both keep 15 (C++ asserts above 16 in Debug)
                L"%endkey\t,./\r\n"
                L"%autoCompose\t1\r\n"
                L"%keyname begin\r\n"
                L"a\t\u65E5\r\n"
                L"B\t\u6708\r\n"
                L"%keyname end\r\n"
                L"\r\n"
                L"%chardef begin\r\n"
                L"a\t\u4E2D\r\n"
                L"A\t\u592E\r\n"
                L"\"q\"\t\"\u5F15\\\"\u865F\"\r\n"
                L",\t\uFF0C\r\n"
                L"ab\t \u6587 \r\n"
                L"ab\t\u5B57\r\n"
                L"z\t\U0001F600\r\n"
                L"%chardef end\r\n";
            Pair p;
            Assert::IsTrue(p.Open(WriteUtf16(L"edge.cin", src), DICTIONARY_TYPE::CIN_DICTIONARY));
            p.CheckConfig();
            for (const wchar_t* k : { L"a", L"A", L"q", L",", L"ab", L"AB", L"z", L"x", L"" }) p.CheckCollectWord(k);
            for (const wchar_t* k : { L"a*", L"?b", L"*", L"?" }) p.CheckWildcard(k, nullptr);
            for (const wchar_t* v : { L"\u4E2D", L"\u6587", L"\U0001F600" }) p.CheckReverse(v);
        }

        TEST_METHOD(SyntheticCin_Sorted)
        {
            std::wstring src =
                L"%gen_inp\n%sorted\t1\n%selkey\t123\n%chardef begin\n"
                L"a00\t\u5B57\na01\t\u8A5E\nA02\t\u4E00\nb00\t\u6E2C\nb01\t\u8A66\nc00\t\u9A57\nc00\t\u4E8C\n"
                L"%chardef end\n";
            Pair p;
            Assert::IsTrue(p.Open(WriteUtf16(L"sorted.cin", src), DICTIONARY_TYPE::CIN_DICTIONARY));
            p.CheckConfig();
            Assert::AreEqual((size_t)3, dime_engine_radical_index_count(p.rust));
            for (const wchar_t* k : { L"a00", L"a02", L"A01", L"b01", L"c00", L"d00", L"c" }) p.CheckCollectWord(k);
            p.CheckWildcard(L"a*", nullptr);
        }

        TEST_METHOD(SyntheticTts_Sections)
        {
            std::wstring src =
                L"[Config]\r\nFontSize=14\r\nAutoCompose=1\r\n"
                L"[Radical]\r\n\"a\"=\"\u65E5\"\r\n"
                L"[Text]\r\n\"a\"=\"\u4E2D\"\r\n\"b\"=\"\u6587\"\r\n\"a\"=\"\u592E\"\r\n"
                L"[Phrase]\r\n\"\u4E2D\"=\"\u6587\",\"\u570B\"\r\n\"x\"=\"1,2\"\r\n"
                L"[Symbol]\r\n\"=\"=\"\uFF1D\",\"\u2261\"\r\n"
                L"[PhraseFromKeystroke]\r\n\"ab\"=\"\u4E2D\u6587\"\r\n";
            Pair p;
            BOOL savedHasPhrase = Global::hasPhraseSection;
            Assert::IsTrue(p.Open(WriteUtf16(L"tts.txt", src), DICTIONARY_TYPE::TTS_DICTIONARY));
            Global::hasPhraseSection = savedHasPhrase;
            p.CheckConfig();
            Assert::AreEqual((size_t)2, dime_engine_config_entry_count(p.rust));

            p.SetSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
            for (const wchar_t* k : { L"a", L"b", L"c" }) p.CheckCollectWord(k);
            p.CheckReverse(L"\u4E2D");
            p.SetSection(SEARCH_SECTION::SEARCH_SECTION_PHRASE);
            for (const wchar_t* k : { L"\u4E2D", L"x" }) p.CheckCollectWord(k);
            p.CheckReverse(L"\u6587");
            p.SetSection(SEARCH_SECTION::SEARCH_SECTION_SYMBOL);
            p.CheckCollectWord(L"=");
            p.SetSection(SEARCH_SECTION::SEARCH_SECTION_PRHASE_FROM_KEYSTROKE);
            p.CheckCollectWord(L"ab");
        }
    };
}

#endif // DIME_RUST_AVAILABLE
