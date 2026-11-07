/********************************************************************************
* KeyClassifyParityTest: CCompositionProcessorEngine::IsVirtualKeyNeed, C++ vs
* Rust (dime_classify_key via _IsVirtualKeyNeedRust).
*
* Random engine states (IME mode, keystroke table, keystroke buffer including
* the null-vs-empty distinction, end keys, select keys, page size), config
* (array scope, numeric pad, space as page down, Shift-English digit mode,
* phonetic layout), modifiers, CapsLock and keys are fed to both
* implementations; the return value and the resulting key state must match.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <msctf.h>
#include <random>
#include <map>
#include <memory>
#include <string>

#include "../DIME.h"
#include "../Globals.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"
#include "../TableDictionaryEngine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Friend of CCompositionProcessorEngine: sets the private state both
// implementations read.
class KeyClassifyParityAccess
{
public:
    CCompositionProcessorEngine engine{ nullptr };
    std::unique_ptr<CTableDictionaryEngine> dict;
    std::unique_ptr<CCandidateRange> range;
    bool bufferNull = true;
    std::wstring buffer;
    std::wstring endKeys;
    WCHAR endKeysBuf[MAX_CAND_SELKEY] = {};
    UINT dictSlot = 0;

    KeyClassifyParityAccess()
    {
        dict.reset(new CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), nullptr, DICTIONARY_TYPE::CIN_DICTIONARY));
        static WCHAR radical[] = L"x";
        (*dict->GetRadicalMap())[L'a'] = radical;
    }

    ~KeyClassifyParityAccess()
    {
        for (UINT i = 0; i < IM_SLOTS; i++)
            engine._pTableDictionaryEngine[i] = nullptr;
        engine._pEndkey = nullptr;
        engine._keystrokeBuffer.Set(nullptr, 0);
        engine._pActiveCandidateListIndexRange = &engine._candidateListIndexRange;
    }

    void SetDictionary(IME_MODE mode, bool available)
    {
        for (UINT i = 0; i < IM_SLOTS; i++)
            engine._pTableDictionaryEngine[i] = nullptr;
        engine._imeMode = mode;
        if (available && (UINT)mode < IM_SLOTS)
            engine._pTableDictionaryEngine[(UINT)mode] = dict.get();
    }

    void SetKeystrokeFunctions(const std::vector<KEYSTROKE_FUNCTION>& functions)
    {
        engine._KeystrokeComposition.Clear();
        for (size_t i = 0; i < functions.size(); i++)
        {
            _KEYSTROKE* p = engine._KeystrokeComposition.Append();
            p->Index = (UINT)i;
            p->Function = functions[i];
        }
    }

    void SetBuffer(bool isNull, const std::wstring& text)
    {
        buffer = isNull ? std::wstring() : text;
        bufferNull = isNull;
        if (isNull)
            engine._keystrokeBuffer.Set(nullptr, 0);
        else
            engine._keystrokeBuffer.Set(buffer.c_str(), buffer.size());
    }

    void SetEndKeys(bool isNull, const std::wstring& keys)
    {
        if (isNull)
        {
            engine._pEndkey = nullptr;
            return;
        }
        StringCchCopy(endKeysBuf, ARRAYSIZE(endKeysBuf), keys.c_str());
        engine._pEndkey = endKeysBuf;
    }

    void SetSelkeys(const std::vector<_KEYSTROKE>& keys)
    {
        range.reset(new CCandidateRange());
        for (const _KEYSTROKE& k : keys)
            *range->Append() = k;
        engine._pActiveCandidateListIndexRange = range.get();
    }

    void SetMisc(UINT pageSize, BOOL hasWildcard)
    {
        engine._candidatePageSize = pageSize;
        engine._hasWildcardIncludedInKeystrokeBuffer = hasWildcard;
    }

    BOOL Cpp(UINT code, WCHAR* pwch, BOOL composing, CANDIDATE_MODE mode, BOOL wildcard, UINT count, INT sel, _KEYSTROKE_STATE* ks)
    {
        return engine._IsVirtualKeyNeedCpp(code, pwch, composing, mode, wildcard, count, sel, ks);
    }
    BOOL Rust(UINT code, WCHAR* pwch, BOOL composing, CANDIDATE_MODE mode, UINT count, INT sel, _KEYSTROKE_STATE* ks)
    {
        return engine._IsVirtualKeyNeedRust(code, pwch, composing, mode, count, sel, ks);
    }
};

namespace DIMEUnitTests
{
    TEST_CLASS(KeyClassifyParityTest)
    {
    private:
        IME_MODE _savedImeMode = IME_MODE::IME_MODE_NONE;
        USHORT _savedModifiers = 0;
        ARRAY_SCOPE _savedScope = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
        NUMERIC_PAD _savedNumpad = NUMERIC_PAD::NUMERIC_PAD_NUMERIC;
        BOOL _savedSpacePageDown = FALSE;
        IME_SHIFT_ENGLISH_DIGIT_MODE _savedDigitMode = IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL;
        PHONETIC_KEYBOARD_LAYOUT _savedLayout = PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT;
        BYTE _savedKeyboard[256] = {};

        template <typename T>
        static T Pick(std::mt19937& rng, std::initializer_list<T> items)
        {
            std::uniform_int_distribution<size_t> d(0, items.size() - 1);
            return *(items.begin() + d(rng));
        }

        static int Rand(std::mt19937& rng, int lo, int hi)
        {
            return std::uniform_int_distribution<int>(lo, hi)(rng);
        }

        static bool Chance(std::mt19937& rng, int percent)
        {
            return Rand(rng, 0, 99) < percent;
        }

        // Characters the classifier treats specially, plus non-ASCII ones whose
        // Unicode upper case is ASCII (towupper must stay C-locale).
        static WCHAR RandomChar(std::mt19937& rng)
        {
            switch (Rand(rng, 0, 5))
            {
            case 0: return 0;
            case 1: return Pick<WCHAR>(rng, { L'=', L'w', L'W', L'h', L'H', L'8', L'g', L'G', L'\\', L'\'', L'[', L']',
                                               L'-', L'`', L'*', L'?', L' ', L'0', L'5', L'9', L'_', L'{', L'"' });
            case 2: return Pick<WCHAR>(rng, { 0x00E9, 0x00FF, 0x0131, 0x017F, 0x212A, 0x3000, 0xFF21, 0x7F, 0x80, 0x1F, 0x0A });
            default: return (WCHAR)Rand(rng, 0x20, 0x7E);
            }
        }

        static UINT RandomVk(std::mt19937& rng, WCHAR wch)
        {
            switch (Rand(rng, 0, 4))
            {
            case 0: return Pick<UINT>(rng, { VK_BACK, VK_RETURN, VK_SHIFT, VK_ESCAPE, VK_SPACE, VK_PRIOR, VK_NEXT, VK_END,
                                             VK_HOME, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_INSERT, VK_DELETE, VK_OEM_7,
                                             VK_OEM_MINUS, VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_PLUS, VK_OEM_1, VK_OEM_2,
                                             VK_OEM_3, VK_OEM_COMMA, VK_OEM_PERIOD, VK_MULTIPLY, VK_DIVIDE, VK_ADD });
            case 1: return (UINT)Rand(rng, VK_NUMPAD0, VK_DIVIDE);
            case 2:
                if (wch >= L'a' && wch <= L'z') return (UINT)(wch - 32);
                if ((wch >= L'A' && wch <= L'Z') || (wch >= L'0' && wch <= L'9')) return wch;
                return (UINT)Rand(rng, '0', 'Z');
            case 3: return (UINT)Rand(rng, '0', '9');
            default: return (UINT)Rand(rng, 0, 255);
            }
        }

        static std::wstring RandomBuffer(std::mt19937& rng)
        {
            static const wchar_t* const kBuffers[] = {
                L"", L"a", L"ab", L"abc", L"abcd", L"=", L"=1", L"=12", L"w", L"W", L"w1", L"wq", L"h", L"H", L"8",
                L"h'", L"8[", L"hg", L"HG", L"hg1", L"\\", L"\\a", L"\\ab", L"*", L"a*", L"?b", L"xyzzy",
            };
            return kBuffers[Rand(rng, 0, ARRAYSIZE(kBuffers) - 1)];
        }

        static std::vector<KEYSTROKE_FUNCTION> RandomKeystrokeTable(std::mt19937& rng)
        {
            std::vector<KEYSTROKE_FUNCTION> f;
            if (Chance(rng, 5))
                return f;
            f.assign(MAX_RADICAL + 1, KEYSTROKE_FUNCTION::FUNCTION_NONE);
            int style = Rand(rng, 0, 2);
            for (UINT i = 0; i <= MAX_RADICAL; i++)
            {
                WCHAR c = (WCHAR)(i + 32);
                bool radical = (c >= L'A' && c <= L'Z') || (style > 0 && ((c >= L'0' && c <= L'9') || wcschr(L",./;=", c)));
                if (style == 2 && Chance(rng, 15))
                    radical = !radical;
                if (radical)
                    f[i] = KEYSTROKE_FUNCTION::FUNCTION_INPUT;
            }
            // End keys mark their entries INPUT_AND_CONVERT / CONVERT.
            for (int n = Rand(rng, 0, 4); n > 0; n--)
            {
                UINT i = (UINT)Rand(rng, 0, MAX_RADICAL);
                f[i] = f[i] == KEYSTROKE_FUNCTION::FUNCTION_INPUT ? KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT
                                                                 : KEYSTROKE_FUNCTION::FUNCTION_CONVERT;
            }
            return f;
        }

        static std::vector<_KEYSTROKE> RandomSelkeys(std::mt19937& rng)
        {
            static const wchar_t* const kSets[] = { L"1234567890", L" 1234567890", L"'[]-\\\"{}_", L" '[]-\\", L"asdfghjkl", L"" };
            std::wstring s = kSets[Rand(rng, 0, ARRAYSIZE(kSets) - 1)];
            std::vector<_KEYSTROKE> keys;
            for (size_t i = 0; i < s.size(); i++)
            {
                _KEYSTROKE k;
                k.Printable = s[i];
                k.Index = Chance(rng, 90) ? (UINT)((i + 1) % 10) : (UINT)Rand(rng, 0, 12);
                k.Modifiers = Chance(rng, 70) ? 0 : (UINT)Pick<UINT>(rng, { TF_MOD_SHIFT, TF_MOD_LSHIFT, TF_MOD_CONTROL });
                keys.push_back(k);
            }
            return keys;
        }

    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            _savedImeMode = Global::imeMode;
            _savedModifiers = Global::ModifiersValue;
            _savedScope = CConfig::GetArrayScope();
            _savedNumpad = CConfig::GetNumericPad();
            _savedSpacePageDown = CConfig::GetSpaceAsPageDown();
            _savedDigitMode = CConfig::GetShiftEnglishDigitMode();
            _savedLayout = CConfig::getPhoneticKeyboardLayout();
            GetKeyboardState(_savedKeyboard);
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            Global::imeMode = _savedImeMode;
            Global::ModifiersValue = _savedModifiers;
            CConfig::SetArrayScope(_savedScope);
            CConfig::SetNumericPad(_savedNumpad);
            CConfig::SetSpaceAsPageDown(_savedSpacePageDown);
            CConfig::SetShiftEnglishDigitMode(_savedDigitMode);
            CConfig::setPhoneticKeyboardLayout(_savedLayout);
            SetKeyboardState(_savedKeyboard);
        }

        TEST_METHOD(RandomStates)
        {
            const int kStates = 400;
            const int kKeysPerState = 500;
            std::map<std::pair<int, int>, int> outcomes;
            int eatenCount = 0, total = 0;

            for (int seed = 1; seed <= kStates; seed++)
            {
                std::mt19937 rng((unsigned)seed);
                KeyClassifyParityAccess a;

                IME_MODE mode = Pick(rng, { IME_MODE::IME_MODE_DAYI, IME_MODE::IME_MODE_ARRAY,
                                            IME_MODE::IME_MODE_PHONETIC, IME_MODE::IME_MODE_GENERIC });
                Global::imeMode = mode;
                a.SetDictionary(Chance(rng, 90) ? mode : Pick(rng, { IME_MODE::IME_MODE_DAYI, IME_MODE::IME_MODE_ARRAY }),
                                Chance(rng, 90));
                a.SetKeystrokeFunctions(RandomKeystrokeTable(rng));
                a.SetEndKeys(Chance(rng, 50), Pick<const wchar_t*>(rng, { L"", L" ", L"3467 ", L"'", L"1" }));
                a.SetSelkeys(RandomSelkeys(rng));
                a.SetMisc(Pick<UINT>(rng, { 0, 5, 9, 10 }), Chance(rng, 25));

                CConfig::SetArrayScope(Pick(rng, { ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A, ARRAY_SCOPE::ARRAY40_BIG5,
                                                   ARRAY_SCOPE::ARRAY30_BIG5 }));
                CConfig::SetNumericPad(Pick(rng, { NUMERIC_PAD::NUMERIC_PAD_NUMERIC, NUMERIC_PAD::NUMERIC_PAD_NUMERIC_COMPOSITION,
                                                   NUMERIC_PAD::NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY }));
                CConfig::SetSpaceAsPageDown(Chance(rng, 50));
                CConfig::SetShiftEnglishDigitMode(Pick(rng, { IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL,
                                                              IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_DIGIT,
                                                              IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_ALWAYS_SYMBOL }));
                CConfig::setPhoneticKeyboardLayout(Chance(rng, 50) ? PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT
                                                                   : PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT);

                BYTE kbd[256] = {};
                memcpy(kbd, _savedKeyboard, sizeof(kbd));
                kbd[VK_CAPITAL] = Chance(rng, 50) ? 0x01 : 0x00;
                SetKeyboardState(kbd);

                for (int k = 0; k < kKeysPerState; k++)
                {
                    if (Chance(rng, 20))
                        a.SetBuffer(Chance(rng, 30), RandomBuffer(rng));
                    if (k == 0)
                        a.SetBuffer(Chance(rng, 30), RandomBuffer(rng));

                    Global::ModifiersValue = Pick<USHORT>(rng, { 0, 0, 0, TF_MOD_SHIFT, TF_MOD_LSHIFT | TF_MOD_SHIFT,
                                                                 TF_MOD_RSHIFT | TF_MOD_SHIFT, TF_MOD_RSHIFT, TF_MOD_LSHIFT,
                                                                 TF_MOD_CONTROL, TF_MOD_CONTROL | TF_MOD_SHIFT });
                    WCHAR wch = RandomChar(rng);
                    UINT vk = RandomVk(rng, wch);
                    BOOL composing = Chance(rng, 50);
                    CANDIDATE_MODE cmode = Pick(rng, { CANDIDATE_MODE::CANDIDATE_NONE, CANDIDATE_MODE::CANDIDATE_ORIGINAL,
                                                       CANDIDATE_MODE::CANDIDATE_PHRASE, CANDIDATE_MODE::CANDIDATE_INCREMENTAL });
                    UINT count = (UINT)Rand(rng, 0, 30);
                    INT sel = Rand(rng, -1, 15);
                    bool withState = Chance(rng, 90);

                    _KEYSTROKE_STATE garbage = { (KEYSTROKE_CATEGORY)Rand(rng, 0, 6), (KEYSTROKE_FUNCTION)Rand(rng, 0, 25) };
                    _KEYSTROKE_STATE sc = garbage, sr = garbage;
                    WCHAR wc = wch, wr = wch;
                    BOOL ec = a.Cpp(vk, &wc, composing, cmode, Chance(rng, 50), count, sel, withState ? &sc : nullptr);
                    BOOL er = a.Rust(vk, &wr, composing, cmode, count, sel, withState ? &sr : nullptr);

                    if (ec != er || sc.Category != sr.Category || sc.Function != sr.Function || wc != wr)
                    {
                        wchar_t msg[512];
                        swprintf_s(msg, L"seed %d key %d: mode=%d vk=0x%02X wch=0x%04X mods=0x%X composing=%d cand=%d count=%u sel=%d "
                                        L"state=%d buf=\"%s\"(null=%d) -> C++ %d/%d/%d, Rust %d/%d/%d",
                            seed, k, (int)mode, vk, (UINT)wch, (UINT)Global::ModifiersValue, composing, (int)cmode, count, sel,
                            withState ? 1 : 0, a.buffer.c_str(), a.bufferNull ? 1 : 0,
                            ec, (int)sc.Category, (int)sc.Function, er, (int)sr.Category, (int)sr.Function);
                        Assert::Fail(msg);
                    }
                    if (withState)
                        outcomes[{ (int)sc.Category, (int)sc.Function }]++;
                    eatenCount += ec ? 1 : 0;
                    total++;
                }
            }

            wchar_t summary[128];
            swprintf_s(summary, L"%d keys, %d eaten, %zu distinct (category, function) outcomes", total, eatenCount, outcomes.size());
            Logger::WriteMessage(summary);
            // Sanity: the random inputs must reach most of the classifier.
            Assert::IsTrue(outcomes.size() >= 25, summary);
        }
    };
}
