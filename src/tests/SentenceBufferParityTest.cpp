/********************************************************************************
* SentenceBufferParityTest: CSentenceBuffer (C++) vs SentenceBuffer
* (rust/crates/dime-core/src/sentence_buffer.rs through dime_sentence_buffer_*).
*
* Random operation sequences -- insert (with valid and invalid pins, and no
* candidates), cursor moves, removals, pin / unpin, conversion with the shipped
* TCFreq.cin / Array-Phrase.cin lexicon and a learning user model, clear -- run
* on both. After every step the whole state must match: positions (code,
* candidates, pinned, chosen), cursor, edit target, text, text lengths, display
* and caret for a reading. Learning is compared through the exported user models.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../SentenceBuffer.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
    std::wstring RepoRootForSentenceParity()
    {
        WCHAR path[MAX_PATH] = {};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
        std::wstring p(path);
        size_t at = p.find(L"\\src\\bin\\");
        return at == std::wstring::npos ? L"" : p.substr(0, at + 1);
    }

    size_t LoadTable(CSentenceLexicon& lex, const std::wstring& path, bool freq)
    {
        std::ifstream f(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.size() < 2) return 0;
        const WCHAR* p = reinterpret_cast<const WCHAR*>(bytes.data());
        size_t len = bytes.size() / sizeof(WCHAR);
        if (*p == 0xFEFF) { p++; len--; }
        return freq ? lex.LoadFreqTable(p, len) : lex.LoadCinValues(p, len);
    }

    std::wstring FromBuffer(DimeBuffer* b)
    {
        size_t len = 0;
        const uint16_t* p = dime_buffer_data(b, &len);
        std::wstring s(p ? reinterpret_cast<const WCHAR*>(p) : L"", p ? len : 0);
        dime_buffer_free(b);
        return s;
    }

    std::wstring DescribeCpp(const CSentenceBuffer& b, const std::wstring& reading)
    {
        std::wstring s = L"count=" + std::to_wstring(b.Count()) + L" cursor=" + std::to_wstring(b.Cursor()) +
            L" target=" + std::to_wstring(b.EditTarget()) + L"\n";
        for (size_t i = 0; i < b.Count(); i++)
        {
            const CSentencePosition& p = b.At(i);
            s += L" [" + p.code + L"]";
            for (const std::wstring& c : p.candidates) s += L" " + c;
            s += L" pin=" + std::to_wstring(p.pinned) + L" chosen=" + std::to_wstring(p.chosen) + L"\n";
        }
        s += L"text=" + b.Text() + L" display=" + b.Display(reading) + L" caret=" + std::to_wstring(b.DisplayCaret(reading));
        for (size_t n = 0; n <= b.Count() + 1; n++) s += L" len" + std::to_wstring(n) + L"=" + std::to_wstring(b.TextLength(n));
        return s;
    }

    std::wstring DescribeRust(DimeSentenceBuffer* b, const std::wstring& reading)
    {
        size_t count = dime_sentence_buffer_count(b);
        std::wstring s = L"count=" + std::to_wstring(count) + L" cursor=" + std::to_wstring(dime_sentence_buffer_cursor(b)) +
            L" target=" + std::to_wstring(dime_sentence_buffer_edit_target(b)) + L"\n";
        for (size_t i = 0; i < count; i++)
        {
            size_t len = 0;
            const uint16_t* code = dime_sentence_buffer_position_code(b, i, &len);
            s += L" [" + std::wstring(reinterpret_cast<const WCHAR*>(code), len) + L"]";
            for (size_t k = 0; k < dime_sentence_buffer_candidate_count(b, i); k++)
            {
                const uint16_t* c = dime_sentence_buffer_candidate(b, i, k, &len);
                s += L" " + std::wstring(reinterpret_cast<const WCHAR*>(c), len);
            }
            s += L" pin=" + std::to_wstring(dime_sentence_buffer_pinned(b, i)) +
                L" chosen=" + std::to_wstring(dime_sentence_buffer_chosen(b, i)) + L"\n";
        }
        const uint16_t* r = reinterpret_cast<const uint16_t*>(reading.c_str());
        s += L"text=" + FromBuffer(dime_sentence_buffer_text(b)) +
            L" display=" + FromBuffer(dime_sentence_buffer_display(b, r, reading.size())) +
            L" caret=" + std::to_wstring(dime_sentence_buffer_display_caret(b, r, reading.size()));
        for (size_t n = 0; n <= count + 1; n++) s += L" len" + std::to_wstring(n) + L"=" + std::to_wstring(dime_sentence_buffer_text_length(b, n));
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SentenceBufferParityTest)
    {
    public:
        TEST_METHOD(RandomOperations)
        {
            if (!CSentenceLexicon::IsSupported())
            {
                Logger::WriteMessage(L"Rust library not linked; skipped.");
                return;
            }
            std::wstring root = RepoRootForSentenceParity() + L"installer\\";
            CSentenceLexicon lex;
            Assert::IsTrue(LoadTable(lex, root + L"TCFreq.cin", true) > 1000, L"TCFreq.cin");
            Assert::IsTrue(LoadTable(lex, root + L"Array-Phrase.cin", false) > 1000, L"Array-Phrase.cin");

            // Characters that form words in the lexicon, with look-alike decoys.
            static const wchar_t* const kGroups[][3] = {
                { L"電", L"店", L"甸" }, { L"腦", L"惱", L"瑙" }, { L"我", L"俄", L"鵝" },
                { L"們", L"門", L"問" }, { L"今", L"金", L"介" }, { L"天", L"夭", L"夫" },
                { L"中", L"忠", L"鍾" }, { L"國", L"果", L"過" }, { L"人", L"入", L"八" },
                { L"大", L"太", L"犬" }, { L"學", L"覺", L"攖" }, { L"生", L"牲", L"甥" },
            };
            static const wchar_t* const kReadings[] = { L"", L"a", L"人", L"ab", L"=1" };

            int ops = 0;
            for (int seed = 1; seed <= 300; seed++)
            {
                std::mt19937 rng((unsigned)seed);
                auto rand = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

                CSentenceBuffer cpp;
                std::unique_ptr<DimeSentenceBuffer, decltype(&dime_sentence_buffer_free)> rust(dime_sentence_buffer_new(), dime_sentence_buffer_free);
                CSentenceUserModel userCpp, userRust;   // learn targets, compared by export
                CSentenceUserModel userConvert;         // shared model used by conversions

                for (int k = 0; k < 120; k++, ops++)
                {
                    int op = rand(0, 99);
                    std::wstring what;
                    if (op < 30)
                    {
                        std::vector<std::wstring> cands;
                        int n = rand(0, 10) == 0 ? 0 : rand(1, 3);
                        int g = rand(0, ARRAYSIZE(kGroups) - 1);
                        for (int i = 0; i < n; i++) cands.push_back(kGroups[g][i]);
                        if (rand(0, 5) == 0) cands.push_back(L"大學");   // a multi-character candidate
                        std::wstring code = std::wstring(1, (wchar_t)(L'a' + g)) + (rand(0, 1) ? L"b" : L"");
                        int pinned = rand(-2, 4);
                        std::vector<const uint16_t*> ptrs;
                        std::vector<size_t> lens;
                        for (const std::wstring& c : cands) { ptrs.push_back(reinterpret_cast<const uint16_t*>(c.c_str())); lens.push_back(c.size()); }
                        bool rc = cpp.Insert(code, cands, pinned);
                        int rr = dime_sentence_buffer_insert(rust.get(), reinterpret_cast<const uint16_t*>(code.c_str()), code.size(),
                            ptrs.data(), lens.data(), cands.size(), pinned);
                        Assert::AreEqual(rc ? 1 : 0, rr, L"Insert result");
                        what = L"insert";
                    }
                    else if (op < 50)
                    {
                        uint32_t m = (uint32_t)rand(0, 3);
                        switch (m) { case 0: cpp.MoveLeft(); break; case 1: cpp.MoveRight(); break; case 2: cpp.Home(); break; default: cpp.End(); break; }
                        dime_sentence_buffer_move(rust.get(), m);
                        what = L"move";
                    }
                    else if (op < 58)
                    {
                        Assert::AreEqual(cpp.RemoveBeforeCursor() ? 1 : 0, dime_sentence_buffer_remove_before_cursor(rust.get()), L"RemoveBeforeCursor");
                        what = L"remove before";
                    }
                    else if (op < 64)
                    {
                        Assert::AreEqual(cpp.RemoveAtCursor() ? 1 : 0, dime_sentence_buffer_remove_at_cursor(rust.get()), L"RemoveAtCursor");
                        what = L"remove at";
                    }
                    else if (op < 76)
                    {
                        size_t pos = (size_t)rand(0, (int)cpp.Count() + 1);
                        int index = rand(-2, 3);
                        Assert::AreEqual(cpp.Pin(pos, index) ? 1 : 0, dime_sentence_buffer_pin(rust.get(), pos, index), L"Pin");
                        what = L"pin";
                    }
                    else if (op < 92)
                    {
                        bool withUser = rand(0, 1) == 1;
                        cpp.Convert(&lex, withUser ? &userConvert : nullptr);
                        dime_sentence_buffer_convert(rust.get(), lex.Handle(), withUser ? userConvert.Handle() : nullptr);
                        what = L"convert";
                    }
                    else if (op < 98)
                    {
                        bool lc = userCpp.Learn(cpp);
                        int lr = dime_sentence_buffer_learn(rust.get(), userRust.Handle());
                        Assert::AreEqual(lc ? 1 : 0, lr, L"Learn result");
                        Assert::IsTrue(userCpp.Export() == userRust.Export(), L"learned user models differ");
                        userConvert.Learn(cpp);
                        what = L"learn";
                    }
                    else
                    {
                        cpp.Clear();
                        dime_sentence_buffer_clear(rust.get());
                        what = L"clear";
                    }

                    std::wstring reading = kReadings[rand(0, ARRAYSIZE(kReadings) - 1)];
                    std::wstring dc = DescribeCpp(cpp, reading), dr = DescribeRust(rust.get(), reading);
                    if (dc != dr)
                    {
                        Logger::WriteMessage((L"--- C++\n" + dc + L"\n--- Rust\n" + dr + L"\n").c_str());
                        Assert::Fail((L"seed " + std::to_wstring(seed) + L" step " + std::to_wstring(k) + L" (" + what + L")").c_str());
                    }
                }
            }
            Logger::WriteMessage((std::to_wstring(ops) + L" operations").c_str());
        }
    };
}
