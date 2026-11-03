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
* Sentence mode tests: CSentenceBuffer (cursor / editing / display) and the
* conversion through CSentenceLexicon (Rust dime_core::sentence) with the
* shipped TCFreq.cin + Array-Phrase.cin, plus an end-to-end check with the
* Dragonfly Dayi table (dragonfly.cin at the repository root).
********************************************************************************/

#include "pch.h"
#include "CppUnitTest.h"
#include "../SentenceBuffer.h"
#include "../TableDictionaryEngine.h"
#include "../File.h"
#include "../Config.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        std::wstring SentenceRepoRoot()
        {
            std::wstring p = __FILEW__;
            for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
            return p + L"\\";
        }

        std::vector<std::wstring> C(std::initializer_list<const wchar_t*> list)
        {
            std::vector<std::wstring> v;
            for (const wchar_t* s : list) v.emplace_back(s);
            return v;
        }

        size_t LoadInto(CSentenceLexicon& lex, const std::wstring& path, bool freq)
        {
            CFile file;
            if (!file.CreateFile(path.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ)) return 0;
            const WCHAR* p = file.GetReadBufferPointer();
            if (!p) return 0;
            size_t len = (size_t)(file.GetFileSize() / sizeof(WCHAR));
            return freq ? lex.LoadFreqTable(p, len) : lex.LoadCinValues(p, len);
        }
    }

    TEST_CLASS(SentenceBufferTest)
    {
    public:
        TEST_METHOD(InsertMovesCursorAndBuildsDisplay)
        {
            CSentenceBuffer b;
            Assert::IsTrue(b.IsEmpty());
            Assert::IsTrue(b.Insert(L"a", C({ L"\u4E2D", L"\u7D42" }), -1));
            Assert::IsTrue(b.Insert(L"b", C({ L"\u6587" }), -1));
            Assert::IsFalse(b.Insert(L"c", {}, -1), L"empty candidate list is ignored");
            Assert::AreEqual((size_t)2, b.Count());
            Assert::AreEqual((size_t)2, b.Cursor());
            Assert::AreEqual(std::wstring(L"\u4E2D\u6587"), b.Text());
            Assert::AreEqual(std::wstring(L"\u4E2D\u6587ab"), b.Display(L"ab"));
            Assert::AreEqual((size_t)4, b.DisplayCaret(L"ab"));

            b.MoveLeft();
            Assert::AreEqual(std::wstring(L"\u4E2Dab\u6587"), b.Display(L"ab"));
            Assert::AreEqual((size_t)3, b.DisplayCaret(L"ab"));
            b.Home();
            Assert::AreEqual((size_t)0, b.DisplayCaret(L""));
            b.MoveLeft();
            Assert::AreEqual((size_t)0, b.Cursor(), L"cursor stops at the start");
            b.End();
            b.MoveRight();
            Assert::AreEqual((size_t)2, b.Cursor(), L"cursor stops at the end");
        }

        TEST_METHOD(InsertAtCursorAndRemove)
        {
            CSentenceBuffer b;
            b.Insert(L"a", C({ L"\u4E00" }), -1);
            b.Insert(L"c", C({ L"\u4E09" }), -1);
            b.MoveLeft();
            b.Insert(L"b", C({ L"\u4E8C" }), -1);
            Assert::AreEqual(std::wstring(L"\u4E00\u4E8C\u4E09"), b.Text());
            Assert::AreEqual((size_t)2, b.Cursor());

            Assert::IsTrue(b.RemoveAtCursor());
            Assert::AreEqual(std::wstring(L"\u4E00\u4E8C"), b.Text());
            Assert::IsFalse(b.RemoveAtCursor(), L"nothing under the cursor at the end");
            Assert::IsTrue(b.RemoveBeforeCursor());
            Assert::AreEqual(std::wstring(L"\u4E00"), b.Text());
            Assert::AreEqual((size_t)1, b.Cursor());
            b.Home();
            Assert::IsFalse(b.RemoveBeforeCursor());
            b.Clear();
            Assert::IsTrue(b.IsEmpty());
            Assert::AreEqual((size_t)0, b.Cursor());
        }

        TEST_METHOD(EditTargetAndPin)
        {
            CSentenceBuffer b;
            b.Insert(L"a", C({ L"\u7532", L"\u4E59" }), -1);
            b.Insert(L"b", C({ L"\u4E19", L"\u4E01" }), 1);
            Assert::AreEqual(1, b.At(1).pinned);
            Assert::AreEqual(std::wstring(L"\u7532\u4E01"), b.Text(), L"pinned candidate is chosen");
            Assert::AreEqual((size_t)1, b.EditTarget(), L"cursor at end edits the last character");
            b.Home();
            Assert::AreEqual((size_t)0, b.EditTarget());

            Assert::IsTrue(b.Pin(0, 1));
            Assert::AreEqual(std::wstring(L"\u4E59\u4E01"), b.Text());
            Assert::IsFalse(b.Pin(0, 5), L"index out of range");
            Assert::IsFalse(b.Pin(9, 0), L"position out of range");
            Assert::IsTrue(b.Pin(0, -1));
            Assert::AreEqual(-1, b.At(0).pinned);

            b.Insert(L"x", C({ L"\u620A" }), 7);
            Assert::AreEqual(-1, b.At(0).pinned, L"pin outside the list is ignored");
        }

        TEST_METHOD(ConvertWithoutLexiconKeepsFirstOrPinned)
        {
            CSentenceBuffer b;
            b.Insert(L"a", C({ L"\u7532", L"\u4E59" }), -1);
            b.Insert(L"b", C({ L"\u4E19", L"\u4E01" }), 1);
            b.Convert(nullptr);
            Assert::AreEqual(std::wstring(L"\u7532\u4E01"), b.Text());
            b.Clear();
            b.Convert(nullptr);  // empty buffer is fine
        }

        TEST_METHOD(ConvertUsesWordsFromShippedTables)
        {
            if (!CSentenceLexicon::IsSupported())
            {
                Logger::WriteMessage(L"Rust library not linked; skipped.");
                return;
            }
            std::wstring root = SentenceRepoRoot() + L"installer\\";
            CSentenceLexicon lex;
            size_t freq = LoadInto(lex, root + L"TCFreq.cin", true);
            size_t words = LoadInto(lex, root + L"Array-Phrase.cin", false);
            Assert::IsTrue(freq > 5000, L"TCFreq.cin loaded");
            Assert::IsTrue(words > 50000, L"Array-Phrase.cin words loaded");
            Assert::IsTrue(lex.WordCount() == words);

            // Every position has a look-alike decoy ranked first.
            CSentenceBuffer b;
            b.Insert(L"1", C({ L"\u5E97", L"\u96FB" }), -1);
            b.Insert(L"2", C({ L"\u60F1", L"\u8166" }), -1);
            b.Convert(&lex);
            Assert::AreEqual(std::wstring(L"\u96FB\u8166"), b.Text());

            // Pinning the first character breaks the word.
            b.Pin(0, 0);
            b.Convert(&lex);
            Assert::AreEqual(L'\u5E97', b.Text()[0]);

            // A longer sentence made of several words.
            CSentenceBuffer s;
            s.Insert(L"1", C({ L"\u4FC4", L"\u6211" }), -1);
            s.Insert(L"2", C({ L"\u9580", L"\u5011" }), -1);
            s.Insert(L"3", C({ L"\u91D1", L"\u4ECA" }), -1);
            s.Insert(L"4", C({ L"\u592D", L"\u5929" }), -1);
            s.Convert(&lex);
            Assert::AreEqual(std::wstring(L"\u6211\u5011\u4ECA\u5929"), s.Text());
        }

        // The Dayi table users actually type with: dragonfly.cin (Dragonfly, at the
        // repository root).  Plain .cin: "code<space>char" lines in %chardef, in
        // candidate order.  Each character is typed with its first listed code and
        // the conversion must recover the sentence.
        TEST_METHOD(DayiSentencesWithDragonflyTable)
        {
            if (!CSentenceLexicon::IsSupported())
            {
                Logger::WriteMessage(L"Rust library not linked; skipped.");
                return;
            }
            std::wstring path = SentenceRepoRoot() + L"dragonfly.cin";
            HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                Logger::WriteMessage(L"dragonfly.cin not found at the repository root; skipped.");
                return;
            }
            std::string bytes((size_t)GetFileSize(h, nullptr), '\0');
            DWORD read = 0;
            ReadFile(h, &bytes[0], (DWORD)bytes.size(), &read, nullptr);
            CloseHandle(h);
            size_t skip = (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(read - skip), nullptr, 0);
            std::wstring text(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(read - skip), &text[0], wlen);

            // code -> candidates (file order), character -> codes (file order)
            std::map<std::wstring, std::vector<std::wstring>> byCode;
            std::map<std::wstring, std::vector<std::wstring>> byChar;
            bool inChardef = false;
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t eol = text.find(L'\n', pos);
                if (eol == std::wstring::npos) eol = text.size();
                std::wstring line = text.substr(pos, eol - pos);
                pos = eol + 1;
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (line.compare(0, 8, L"%chardef") == 0) { inChardef = line.find(L"begin") != std::wstring::npos; continue; }
                if (!inChardef || line.empty() || line[0] == L'#') continue;
                size_t sep = line.find_first_of(L" \t");
                if (sep == std::wstring::npos) continue;
                size_t val = line.find_first_not_of(L" \t", sep);
                if (val == std::wstring::npos) continue;
                std::wstring code = line.substr(0, sep), value = line.substr(val);
                byCode[code].push_back(value);
                byChar[value].push_back(code);
            }
            Assert::IsTrue(byCode.size() > 10000, L"dragonfly.cin parsed");

            std::wstring root = SentenceRepoRoot() + L"installer\\";
            CSentenceLexicon lex;
            LoadInto(lex, root + L"TCFreq.cin", true);
            LoadInto(lex, root + L"Array-Phrase.cin", false);

            const wchar_t* sentences[] = {
                L"\u8CBC\u6587\u4E00\u51FA\u7ACB\u523B\u5F15\u767C\u5927\u6279\u80A1\u6C11",
                L"\u6211\u5011\u4ECA\u5929\u8981\u53BB\u4E0A\u8AB2",
                L"\u9019\u500B\u554F\u984C\u975E\u5E38\u91CD\u8981",
                L"\u8ACB\u5927\u5BB6\u6CE8\u610F\u4EA4\u901A\u5B89\u5168",
                L"\u96FB\u8166\u79D1\u5B78\u662F\u4E00\u9580\u5B78\u554F",
                L"\u660E\u5929\u7684\u5929\u6C23\u61C9\u8A72\u4E0D\u932F",
                L"\u653F\u5E9C\u7ACB\u523B\u5BA3\u5E03\u65B0\u7684\u9632\u75AB\u63AA\u65BD",
                L"\u4E4B\u524D\u7684\u8A08\u756B\u5DF2\u7D93\u53D6\u6D88\u4E86",
                L"\u4ED6\u7684\u7ACB\u5834\u4E00\u76F4\u6C92\u6709\u6539\u8B8A",
                L"\u5927\u5BB6\u90FD\u5E0C\u671B\u65E9\u65E5\u6062\u5FA9\u6B63\u5E38\u751F\u6D3B",
            };

            size_t total = 0, firstRight = 0, convertedRight = 0, ambiguous = 0;
            for (const wchar_t* sentence : sentences)
            {
                CSentenceBuffer b;
                std::wstring target = sentence;
                bool usable = true;
                for (wchar_t ch : target)
                {
                    auto codes = byChar.find(std::wstring(1, ch));
                    if (codes == byChar.end()) { usable = false; break; }
                    const std::wstring& code = codes->second.front();
                    const std::vector<std::wstring>& list = byCode[code];
                    if (list.size() > 1) ambiguous++;
                    if (list[0] == std::wstring(1, ch)) firstRight++;
                    b.Insert(code, list, -1);
                }
                if (!usable)
                {
                    Logger::WriteMessage((L"skipped (character not in table): " + target + L"\n").c_str());
                    continue;
                }
                b.Convert(&lex);
                std::wstring got = b.Text();
                size_t right = 0;
                for (size_t i = 0; i < target.size() && i < got.size(); i++)
                    if (got[i] == target[i]) right++;
                convertedRight += right;
                total += target.size();
                Logger::WriteMessage(((right == target.size() ? L"OK   " : L"DIFF ") + target + L" -> " + got + L"\n").c_str());
            }
            std::wstring summary = L"chars " + std::to_wstring(total) + L", with homographs " + std::to_wstring(ambiguous) +
                L", first-candidate right " + std::to_wstring(firstRight) + L", converted right " + std::to_wstring(convertedRight) + L"\n";
            Logger::WriteMessage(summary.c_str());
            Assert::IsTrue(total > 0, L"no usable sentence");
            Assert::IsTrue(convertedRight >= firstRight, L"conversion must not be worse than always taking the first candidate");
        }
    };
}
