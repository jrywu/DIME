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
* Rust engine adapter tests (Rust migration, step "wire into DIME.dll")
*
* Compares CTableDictionaryEngine (C++) with CRustTableDictionaryEngine (the
* adapter DIME.dll uses when built with DIME_USE_RUST_ENGINE) through the
* C++ interface the IME calls: same CFile, same calls, identical results,
* identical side effects on CConfig / radical map / selkey.
********************************************************************************/

#include "pch.h"

#ifdef DIME_RUST_AVAILABLE

#include "CppUnitTest.h"
#include "../TableDictionaryEngine.h"
#include "../RustTableDictionaryEngine.h"
#include "../File.h"
#include <algorithm>
#include <memory>
#include <set>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        const LCID kAdapterLcid = MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT);

        std::wstring AdapterRepoRoot()
        {
            std::wstring p = __FILEW__;
            for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
            return p + L"\\";
        }

        std::wstring Dump(CDIMEArray<CCandidateListItem>& list)
        {
            std::wstring out;
            for (UINT i = 0; i < list.Count(); i++)
            {
                CCandidateListItem* it = list.GetAt(i);
                out += std::wstring(it->_FindKeyCode.Get(), it->_FindKeyCode.GetLength()) + L"=" +
                       std::wstring(it->_ItemString.Get(), it->_ItemString.GetLength()) + L"/" +
                       std::to_wstring(it->_WordFrequency) + L"\n";
            }
            return out;
        }

        std::wstring Dump(CDIMEArray<CStringRange>& list)
        {
            std::wstring out;
            for (UINT i = 0; i < list.Count(); i++)
                out += std::wstring(list.GetAt(i)->Get(), list.GetAt(i)->GetLength()) + L"\n";
            return out;
        }

        // Printable form: every non-ASCII / control unit shown as U+XXXX.
        std::wstring Visible(const std::wstring& s)
        {
            std::wstring out;
            for (wchar_t c : s.substr(0, 200))
            {
                if (c >= 0x20 && c < 0x7F) out += c;
                else { wchar_t b[16]; swprintf_s(b, L"<%04X>", (unsigned)c); out += b; }
            }
            return out;
        }

        void Same(const std::wstring& cpp, const std::wstring& rust, const std::wstring& what)
        {
            if (cpp != rust)
                Assert::Fail((what + L": C++ and Rust engines differ | C++: " + Visible(cpp) + L" | Rust: " + Visible(rust)).c_str());
        }

        // One file shared by both engines, like the IME (engine + its CFile).
        struct EnginePair
        {
            std::unique_ptr<CFile> file;
            std::unique_ptr<CTableDictionaryEngine> cpp;
            std::unique_ptr<CTableDictionaryEngine> rust;
            std::wstring name;

            bool Open(const std::wstring& path, DICTIONARY_TYPE type)
            {
                name = path.substr(path.find_last_of(L'\\') + 1);
                file.reset(new CFile());
                if (!file->CreateFile(path.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ)) return false;
                if (!file->GetReadBufferPointer()) return false;
                cpp.reset(new CTableDictionaryEngine(kAdapterLcid, file.get(), type));
                rust.reset(new CRustTableDictionaryEngine(kAdapterLcid, file.get(), type));
                BOOL autoCompose = CConfig::GetAutoCompose();
                BOOL hasPhrase = Global::hasPhraseSection;
                cpp->ParseConfig(IME_MODE::IME_MODE_GENERIC);
                rust->ParseConfig(IME_MODE::IME_MODE_GENERIC);
                CConfig::SetAutoCompose(autoCompose);
                Global::hasPhraseSection = hasPhrase;
                return true;
            }

            void CheckConfig()
            {
                Same(cpp->GetSelkey(), rust->GetSelkey(), name + L" selkey");
                Same(cpp->GetEndkey(), rust->GetEndkey(), name + L" endkey");
                auto dump = [](_T_RadicalMap* m) {
                    std::wstring s;
                    for (auto& kv : *m) s += std::wstring(1, kv.first) + L"=" + kv.second + L"\n";
                    return s;
                };
                Same(dump(cpp->GetRadicalMap()), dump(rust->GetRadicalMap()), name + L" radical map");
            }

            void CheckKey(const std::wstring& key)
            {
                CStringRange k; k.Set(key.c_str(), key.length());
                CDIMEArray<CCandidateListItem> a, b;
                cpp->CollectWord(&k, &a);
                rust->CollectWord(&k, &b);
                Same(Dump(a), Dump(b), name + L" CollectWord(" + key + L")");

                CDIMEArray<CStringRange> sa, sb;
                cpp->CollectWord(&k, &sa);
                rust->CollectWord(&k, &sb);
                Same(Dump(sa), Dump(sb), name + L" CollectWord/strings(" + key + L")");
            }

            void CheckWildcard(const std::wstring& pattern, EnginePair* freq)
            {
                CStringRange k; k.Set(pattern.c_str(), pattern.length());
                CDIMEArray<CCandidateListItem> a, b;
                cpp->CollectWordForWildcard(&k, &a, freq ? freq->cpp.get() : nullptr);
                rust->CollectWordForWildcard(&k, &b, freq ? freq->rust.get() : nullptr);
                Same(Dump(a), Dump(b), name + L" CollectWordForWildcard(" + pattern + L")");

                // Second call into the same list: must de-duplicate against existing items.
                cpp->CollectWordForWildcard(&k, &a, freq ? freq->cpp.get() : nullptr);
                rust->CollectWordForWildcard(&k, &b, freq ? freq->rust.get() : nullptr);
                Same(Dump(a), Dump(b), name + L" CollectWordForWildcard x2(" + pattern + L")");

                cpp->SortListItemByFindKeyCode(&a);
                rust->SortListItemByFindKeyCode(&b);
                cpp->SortListItemByWordFrequency(&a);
                rust->SortListItemByWordFrequency(&b);
                Same(Dump(a), Dump(b), name + L" sorted(" + pattern + L")");
            }

            void CheckReverse(const std::wstring& value)
            {
                CStringRange s; s.Set(value.c_str(), value.length());
                CDIMEArray<CCandidateListItem> a, b;
                cpp->CollectWordFromConvertedString(&s, &a);
                rust->CollectWordFromConvertedString(&s, &b);
                Same(Dump(a), Dump(b), name + L" CollectWordFromConvertedString(" + value + L")");

                std::wstring pattern = value + L"*";
                s.Set(pattern.c_str(), pattern.length());
                CDIMEArray<CCandidateListItem> c, d;
                cpp->CollectWordFromConvertedStringForWildcard(&s, &c);
                rust->CollectWordFromConvertedStringForWildcard(&s, &d);
                Same(Dump(c), Dump(d), name + L" CollectWordFromConvertedStringForWildcard(" + pattern + L")");
            }
        };

        std::vector<std::pair<std::wstring, std::wstring>> SampleMappings(const WCHAR* buf, size_t len, size_t limit)
        {
            std::vector<std::pair<std::wstring, std::wstring>> all;
            bool in = false;
            size_t i = 0;
            while (i < len)
            {
                size_t s = i;
                while (i < len && buf[i] != L'\n') i++;
                std::wstring line(buf + s, i - s);
                i++;
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (_wcsnicmp(line.c_str(), L"%chardef", 8) == 0) { in = line.find(L"begin") != std::wstring::npos; continue; }
                size_t tab = line.find(L'\t');
                if (in && tab != std::wstring::npos && tab > 0) all.push_back({ line.substr(0, tab), line.substr(tab + 1) });
            }
            std::vector<std::pair<std::wstring, std::wstring>> out;
            size_t step = (std::max)((size_t)1, all.size() / limit);
            for (size_t j = 0; j < all.size() && out.size() < limit; j += step) out.push_back(all[j]);
            return out;
        }

        std::wstring AdapterTempFile(const std::wstring& name, const std::wstring& content)
        {
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            std::wstring dir = std::wstring(tmp) + L"DIMETests_RustAdapter\\";
            CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring path = dir + name;
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(h != INVALID_HANDLE_VALUE);
            DWORD w = 0;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(h, bom, 2, &w, nullptr);
            WriteFile(h, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &w, nullptr);
            CloseHandle(h);
            return path;
        }

        // CConfig fields touched by [Config] parsing.
        struct ConfigSnapshot
        {
            UINT fontSize, fontWeight, maxCodes;
            BOOL doBeep, spaceAsPageDown, arrowKeySWPages, big5Filter, doHanConvert, autoCompose;
            COLORREF itemColor;
            int colorMode, numericPad, arrayScope, phoneticLayout;
            std::wstring fontFace;

            static ConfigSnapshot Take()
            {
                ConfigSnapshot s;
                s.fontSize = CConfig::GetFontSize(); s.fontWeight = CConfig::GetFontWeight(); s.maxCodes = CConfig::GetMaxCodes();
                s.doBeep = CConfig::GetDoBeep(); s.spaceAsPageDown = CConfig::GetSpaceAsPageDown();
                s.arrowKeySWPages = CConfig::GetArrowKeySWPages(); s.big5Filter = CConfig::GetBig5Filter();
                s.doHanConvert = CConfig::GetDoHanConvert(); s.autoCompose = CConfig::GetAutoCompose();
                s.itemColor = CConfig::GetItemColor(); s.colorMode = (int)CConfig::GetColorMode();
                s.numericPad = (int)CConfig::GetNumericPad(); s.arrayScope = (int)CConfig::GetArrayScope();
                s.phoneticLayout = (int)CConfig::getPhoneticKeyboardLayout();
                s.fontFace = CConfig::GetFontFaceName() ? CConfig::GetFontFaceName() : L"";
                return s;
            }

            void Restore() const
            {
                CConfig::SetFontSize(fontSize); CConfig::SetFontWeight(fontWeight); CConfig::SetMaxCodes(maxCodes);
                CConfig::SetDoBeep(doBeep); CConfig::SetSpaceAsPageDown(spaceAsPageDown);
                CConfig::SetArrowKeySWPages(arrowKeySWPages); CConfig::SetBig5Filter(big5Filter);
                CConfig::SetDoHanConvert(doHanConvert); CConfig::SetAutoCompose(autoCompose);
                CConfig::SetItemColor(itemColor); CConfig::SetColorMode((IME_COLOR_MODE)colorMode);
                CConfig::SetNumericPad((NUMERIC_PAD)numericPad); CConfig::SetArrayScope((ARRAY_SCOPE)arrayScope);
                CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT)phoneticLayout);
                std::wstring face = fontFace;
                CConfig::SetFontFaceName(&face[0]);
            }

            std::wstring ToString() const
            {
                return L"size=" + std::to_wstring(fontSize) + L" weight=" + std::to_wstring(fontWeight) +
                       L" maxCodes=" + std::to_wstring(maxCodes) + L" beep=" + std::to_wstring(doBeep) +
                       L" spacePD=" + std::to_wstring(spaceAsPageDown) + L" arrow=" + std::to_wstring(arrowKeySWPages) +
                       L" big5=" + std::to_wstring(big5Filter) + L" han=" + std::to_wstring(doHanConvert) +
                       L" auto=" + std::to_wstring(autoCompose) + L" item=" + std::to_wstring(itemColor) +
                       L" color=" + std::to_wstring(colorMode) + L" pad=" + std::to_wstring(numericPad) +
                       L" scope=" + std::to_wstring(arrayScope) + L" phon=" + std::to_wstring(phoneticLayout) +
                       L" face=" + fontFace;
            }
        };
    }

    TEST_CLASS(RustEngineAdapterTest)
    {
    public:
        TEST_METHOD(Factory_DefaultsToCppEngineInTests)
        {
            // C++ engine unless the tests are built with /p:DimeTestRustEngine=true.
            CFile f;
            std::unique_ptr<CTableDictionaryEngine> e(CTableDictionaryEngine::Create(kAdapterLcid, &f, DICTIONARY_TYPE::CIN_DICTIONARY));
            Assert::IsNotNull(e.get());
#ifdef DIME_USE_RUST_ENGINE
            Assert::IsNotNull(dynamic_cast<CRustTableDictionaryEngine*>(e.get()));
#else
            Assert::IsNull(dynamic_cast<CRustTableDictionaryEngine*>(e.get()));
#endif
        }

        TEST_METHOD(ShippedTables_SameResultsThroughCppInterface)
        {
            std::wstring root = AdapterRepoRoot() + L"installer\\";
            EnginePair freq;
            bool haveFreq = freq.Open(root + L"TCFreq.cin", DICTIONARY_TYPE::CIN_DICTIONARY);
            int checked = 0;
            for (const wchar_t* t : { L"Array.cin", L"Array40.cin", L"Array-Ext-CD.cin", L"Array-Phrase.cin",
                                      L"Array-shortcode.cin", L"Array-special.cin", L"phone.cin", L"TCSC.cin" })
            {
                EnginePair p;
                if (!p.Open(root + t, DICTIONARY_TYPE::CIN_DICTIONARY)) continue;
                p.CheckConfig();
                auto samples = SampleMappings(p.file->GetReadBufferPointer(), p.file->GetFileSize() / sizeof(WCHAR), 40);
                std::set<std::wstring> seen;
                size_t wild = 0;
                for (auto& kv : samples)
                {
                    if (!seen.insert(kv.first).second) continue;
                    p.CheckKey(kv.first);
                    if (wild++ < 6 && kv.first.size() >= 2)
                        p.CheckWildcard(kv.first.substr(0, kv.first.size() - 1) + L"?", haveFreq ? &freq : nullptr);
                }
                for (size_t i = 0; i < samples.size() && i < 10; i++) p.CheckReverse(samples[i].second);
                checked++;
            }
            Assert::IsTrue(checked >= 6);
        }

        TEST_METHOD(IniConfig_SameCConfigSideEffects)
        {
            std::wstring ini =
                L"[Config]\r\n"
                L"AutoCompose = 1\r\n"
                L"FontSize=18\r\n"
                L"FontWeight = 700\r\n"
                L"FontFaceName = Microsoft JhengHei\r\n"
                L"MaxCodes=7\r\n"
                L"DoBeep=0\r\n"
                L"SpaceAsPageDown=1\r\n"
                L"ArrowKeySWPages=1\r\n"
                L"Big5Filter=1\r\n"
                L"DoHanConvert=1\r\n"
                L"ItemColor=0x00FF8000\r\n"
                L"ColorMode=2\r\n"
                L"NumericPad=1\r\n"
                L"ArrayScope=3\r\n"
                L"PhoneticKeyboardLayout=1\r\n"
                L"UnknownKey=whatever\r\n";
            std::wstring path = AdapterTempFile(L"config.ini", ini);
            ConfigSnapshot original = ConfigSnapshot::Take();

            auto parseWith = [&](bool useRust) {
                original.Restore();
                CFile f;
                Assert::IsTrue(f.CreateFile(path.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ) != FALSE);
                std::unique_ptr<CTableDictionaryEngine> e(useRust
                    ? new CRustTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), &f, DICTIONARY_TYPE::INI_DICTIONARY)
                    : new CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), &f, DICTIONARY_TYPE::INI_DICTIONARY));
                e->ParseConfig(IME_MODE::IME_MODE_DAYI);
                return ConfigSnapshot::Take();
            };
            ConfigSnapshot cpp = parseWith(false);
            ConfigSnapshot rust = parseWith(true);
            original.Restore();

            Assert::AreEqual(cpp.ToString(), rust.ToString());
            Assert::AreEqual(18u, (unsigned)cpp.fontSize, L"INI was not applied at all");
        }

        TEST_METHOD(FileReload_IsPickedUp)
        {
            std::wstring path = AdapterTempFile(L"reload.cin", L"%chardef begin\r\na\t\u4E2D\r\n%chardef end\r\n");
            CFile f;
            Assert::IsTrue(f.CreateFile(path.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ) != FALSE);
            CRustTableDictionaryEngine e(kAdapterLcid, &f, DICTIONARY_TYPE::CIN_DICTIONARY);
            e.ParseConfig(IME_MODE::IME_MODE_GENERIC);

            std::wstring key = L"a";
            CStringRange k; k.Set(key.c_str(), key.length());
            CDIMEArray<CCandidateListItem> before;
            e.CollectWord(&k, &before);
            Assert::AreEqual((UINT)1, before.Count());

            // Rewrite with a newer timestamp; CFile reloads on the next search.
            Sleep(1100);
            AdapterTempFile(L"reload.cin", L"%chardef begin\r\na\t\u4E2D\r\na\t\u6587\r\n%chardef end\r\n");
            CDIMEArray<CCandidateListItem> after;
            e.CollectWord(&k, &after);
            Assert::AreEqual((UINT)2, after.Count());

            // Strings from the first search remain valid (interned).
            std::wstring first(before.GetAt(0)->_ItemString.Get(), before.GetAt(0)->_ItemString.GetLength());
            Assert::AreEqual(std::wstring(L"\u4E2D"), first, Visible(first).c_str());
        }
    };
}

#endif // DIME_RUST_AVAILABLE
