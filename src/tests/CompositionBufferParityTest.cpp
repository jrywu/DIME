/********************************************************************************
* CompositionBufferParityTest: the keystroke buffer of CCompositionProcessorEngine,
* C++ vs Rust (rust/crates/dime-core/src/composition.rs).
*
* Two engines receive the same random operations -- AddVirtualKey (including the
* phonetic syllable), RemoveVirtualKey, PurgeVirtualKey, GetReadingString (own
* buffer or a given key code), IsEscapeInput, buildKeyStrokesFromPhoneticSyllable
* -- one through the C++ functions, one through the Rust ones. Return values,
* beeps, outputs and the whole buffer state (null vs empty, the stale first unit
* IsEscapeInput reads, the phonetic syllable, the wildcard flag) must match
* after every step.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <msctf.h>
#include <random>
#include <memory>
#include <string>
#include <vector>

#include "../DIME.h"
#include "../Globals.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"
#include "../TableDictionaryEngine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Friend of CCompositionProcessorEngine.
class CompositionBufferParityAccess
{
public:
    CCompositionProcessorEngine cpp{ nullptr };
    CCompositionProcessorEngine rust{ nullptr };
    std::unique_ptr<CTableDictionaryEngine> dict;
    std::vector<std::wstring> values;   // radical strings (stable storage)

    CompositionBufferParityAccess()
    {
        dict.reset(new CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), nullptr, DICTIONARY_TYPE::CIN_DICTIONARY));
        values.reserve(256);
    }

    ~CompositionBufferParityAccess()
    {
        for (CCompositionProcessorEngine* e : { &cpp, &rust })
        {
            for (UINT i = 0; i < IM_SLOTS; i++)
                e->_pTableDictionaryEngine[i] = nullptr;
            e->PurgeVirtualKey();
        }
    }

    void SetRadicals(const std::vector<std::pair<WCHAR, std::wstring>>& radicals, IME_MODE mode)
    {
        dict->GetRadicalMap()->clear();
        values.clear();
        for (const auto& r : radicals)
        {
            values.push_back(r.second);
            (*dict->GetRadicalMap())[r.first] = const_cast<WCHAR*>(values.back().c_str());
        }
        for (CCompositionProcessorEngine* e : { &cpp, &rust })
        {
            for (UINT i = 0; i < IM_SLOTS; i++)
                e->_pTableDictionaryEngine[i] = nullptr;
            e->_pTableDictionaryEngine[(UINT)mode] = dict.get();
            e->_imeMode = mode;
        }
    }

    // The state both implementations must agree on.
    static std::wstring State(CCompositionProcessorEngine& e)
    {
        const WCHAR* p = e._keystrokeBuffer.Get();
        std::wstring s = p ? L"buf=\"" + std::wstring(p, e._keystrokeBuffer.GetLength()) + L"\"" : L"buf=null";
        if (p)
        {
            wchar_t first[16];
            swprintf_s(first, L" first=%04X", (UINT)*p);
            s += first;
        }
        wchar_t rest[64];
        swprintf_s(rest, L" syl=%04X wild=%d", e.phoneticSyllable, e._hasWildcardIncludedInKeystrokeBuffer ? 1 : 0);
        return s + rest;
    }

    std::wstring Compare() { return State(cpp) == State(rust) ? L"" : L"C++ " + State(cpp) + L" | Rust " + State(rust); }

    std::wstring Add(WCHAR wch)
    {
        int bc = -2, br = -2;
        BOOL rc = cpp._AddVirtualKeyCpp(wch, &bc);
        BOOL rr = rust._AddVirtualKeyRust(wch, &br);
        if (rc != rr || bc != br)
        {
            wchar_t m[128];
            swprintf_s(m, L"AddVirtualKey(%04X): C++ %d beep %d, Rust %d beep %d", (UINT)wch, rc, bc, rr, br);
            return m;
        }
        return Compare();
    }

    DWORD_PTR Length() { return cpp._keystrokeBuffer.GetLength(); }

    std::wstring Remove(DWORD_PTR index)
    {
        cpp._RemoveVirtualKeyCpp(index);
        rust._RemoveVirtualKeyRust(index);
        return Compare();
    }

    std::wstring Purge()
    {
        cpp.PurgeVirtualKey();
        rust.PurgeVirtualKey();
        return Compare();
    }

    std::wstring EscapeInput()
    {
        BOOL c = cpp._IsEscapeInputCpp(), r = rust._IsEscapeInputRust();
        return c == r ? L"" : L"IsEscapeInput differs";
    }

    std::wstring Reading(const std::wstring* keyCode, bool withWildcardOut)
    {
        CStringRange kc, rc, rr;
        if (keyCode)
            kc.Set(keyCode->c_str(), keyCode->size());
        BOOL wc = 7, wr = 7;
        cpp._GetReadingStringCpp(&rc, withWildcardOut ? &wc : nullptr, keyCode ? &kc : nullptr);
        rust._GetReadingStringRust(&rr, withWildcardOut ? &wr : nullptr, keyCode ? &kc : nullptr);
        std::wstring sc = rc.Get() ? std::wstring(rc.Get(), rc.GetLength()) : L"<null>";
        std::wstring sr = rr.Get() ? std::wstring(rr.Get(), rr.GetLength()) : L"<null>";
        if (sc != sr || wc != wr)
            return L"GetReadingString: C++ \"" + sc + L"\" (" + std::to_wstring(wc) + L") Rust \"" + sr + L"\" (" + std::to_wstring(wr) + L")";
        return Compare();
    }

    std::wstring KeyStrokes(UINT syllable)
    {
        CStringRange c = cpp._BuildKeyStrokesFromPhoneticSyllableCpp(syllable);
        CStringRange r = rust._BuildKeyStrokesFromPhoneticSyllableRust(syllable);
        std::wstring sc(c.Get(), c.GetLength()), sr(r.Get(), r.GetLength());
        delete[] c.Get();
        delete[] r.Get();
        if (sc != sr)
        {
            wchar_t m[128];
            swprintf_s(m, L"buildKeyStrokes(%04X): C++ \"%s\" Rust \"%s\"", syllable, sc.c_str(), sr.c_str());
            return m;
        }
        return L"";
    }
};

namespace DIMEUnitTests
{
    TEST_CLASS(CompositionBufferParityTest)
    {
    private:
        IME_MODE _savedImeMode = IME_MODE::IME_MODE_NONE;
        ARRAY_SCOPE _savedScope = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
        PHONETIC_KEYBOARD_LAYOUT _savedLayout = PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT;
        UINT _savedMaxCodes = 0;

        static int Rand(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
        static bool Chance(std::mt19937& rng, int percent) { return Rand(rng, 0, 99) < percent; }

        static WCHAR RandomKey(std::mt19937& rng)
        {
            static const wchar_t kSpecial[] = L"=wWhHgG8\\'[]-*?0123456789,./;";
            switch (Rand(rng, 0, 9))
            {
            case 0: return 0;
            case 1:
            case 2:
            case 3: return kSpecial[Rand(rng, 0, ARRAYSIZE(kSpecial) - 2)];
            case 4: return Pick(rng, { 0x00E9, 0x0131, 0x3105, 0x7F, 0x80, 0x1F });
            default: return (WCHAR)Rand(rng, 0x20, 0x7E);
            }
        }

        static WCHAR Pick(std::mt19937& rng, std::initializer_list<WCHAR> items)
        {
            return *(items.begin() + Rand(rng, 0, (int)items.size() - 1));
        }

        static std::vector<std::pair<WCHAR, std::wstring>> RandomRadicals(std::mt19937& rng)
        {
            static const wchar_t* const kValues[] = { L"\x3105", L"\x65e5", L"\x6708\x91d1", L"xy", L"?", L"*", L"\x5341\x5927\x6728" };
            std::vector<std::pair<WCHAR, std::wstring>> r;
            if (Chance(rng, 10))
                return r;   // dictionary not available
            static const wchar_t kKeys[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,./;=abc*?'";
            for (const wchar_t* k = kKeys; *k; k++)
                if (Chance(rng, 70))
                    r.push_back({ *k, kValues[Rand(rng, 0, ARRAYSIZE(kValues) - 1)] });
            return r;
        }

    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            _savedImeMode = Global::imeMode;
            _savedScope = CConfig::GetArrayScope();
            _savedLayout = CConfig::getPhoneticKeyboardLayout();
            _savedMaxCodes = CConfig::GetMaxCodes();
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            Global::imeMode = _savedImeMode;
            CConfig::SetArrayScope(_savedScope);
            CConfig::setPhoneticKeyboardLayout(_savedLayout);
            CConfig::SetMaxCodes(_savedMaxCodes);
        }

        TEST_METHOD(RandomOperations)
        {
            const int kSeeds = 400;
            const int kOps = 300;
            int ops = 0;

            for (int seed = 1; seed <= kSeeds; seed++)
            {
                std::mt19937 rng((unsigned)seed);
                CompositionBufferParityAccess a;

                IME_MODE mode = (IME_MODE)Rand(rng, 0, 3);
                if (Chance(rng, 30))
                    mode = IME_MODE::IME_MODE_PHONETIC;
                Global::imeMode = mode;
                a.SetRadicals(RandomRadicals(rng), mode);
                CConfig::SetArrayScope(Chance(rng, 30) ? ARRAY_SCOPE::ARRAY40_BIG5 : ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
                CConfig::setPhoneticKeyboardLayout(Chance(rng, 50) ? PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT
                                                                   : PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT);
                CConfig::SetMaxCodes((UINT)Rand(rng, 1, 6));

                for (int k = 0; k < kOps; k++, ops++)
                {
                    std::wstring err;
                    int op = Rand(rng, 0, 99);
                    if (op < 50)
                    {
                        // Keep reading strings within MAX_READINGSTRING: the C++
                        // version asserts (Debug) instead of truncating.
                        if (a.Length() < 15)
                            err = a.Add(RandomKey(rng));
                    }
                    else if (op < 65)
                    {
                        if (a.Length() > 0)
                            err = a.Remove((DWORD_PTR)Rand(rng, 0, (int)a.Length() + 1));
                    }
                    else if (op < 70)
                        err = a.Purge();
                    else if (op < 85)
                        err = a.Reading(nullptr, Chance(rng, 70));
                    else if (op < 90)
                    {
                        std::wstring code;
                        for (int n = Rand(rng, 0, 8); n > 0; n--)
                        {
                            WCHAR c = RandomKey(rng);
                            if (c) code += c;
                        }
                        err = a.Reading(&code, Chance(rng, 70));
                    }
                    else if (op < 95)
                        err = a.EscapeInput();
                    else
                    {
                        UINT syl = (UINT)Rand(rng, 0, 0xFFFF) & (Chance(rng, 50) ? 0x7FFF : 0x16E0 | 0x0F00 | 0x5000);
                        err = a.KeyStrokes(syl);
                    }

                    if (!err.empty())
                    {
                        wchar_t head[96];
                        swprintf_s(head, L"seed %d op %d (kind %d, mode %d): ", seed, k, op, (int)mode);
                        Assert::Fail((head + err).c_str());
                    }
                }
            }
            Logger::WriteMessage((std::to_wstring(ops) + L" operations").c_str());
        }
    };
}
