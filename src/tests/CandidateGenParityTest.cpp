/********************************************************************************
* CandidateGenParityTest: CCompositionProcessorEngine::GetCandidateList, C++ vs
* the Rust plan (dime_plan_candidate_search via _GetCandidateListRust).
*
* Every dictionary engine slot is filled (or not) with a recording engine that
* logs each search (engine, method, search section, key, frequency engine) and
* returns deterministic items. Two processor engines in the same random state
* run the C++ and the Rust version; the call logs, the resulting candidate list
* (strings, key codes, frequencies, order), the window width, the wildcard flag
* and the active select-key range must match.
********************************************************************************/
#include "pch.h"
#include "TestAppData.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <msctf.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../DIME.h"
#include "../Globals.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"
#include "../TableDictionaryEngine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // Logs searches and returns deterministic items derived from the key.
    class CRecordingDictionaryEngine : public CTableDictionaryEngine
    {
    public:
        CRecordingDictionaryEngine(const wchar_t* name, DICTIONARY_TYPE type, std::vector<std::wstring>* log,
                                   std::deque<std::wstring>* store)
            : CTableDictionaryEngine(MAKELCID(1028, SORT_DEFAULT), nullptr, type), _name(name), _log(log), _store(store)
        {
        }

        using CTableDictionaryEngine::CollectWord;

        VOID CollectWord(_In_ CStringRange* pKeyCode, _Inout_ CDIMEArray<CCandidateListItem>* pItemList) override
        {
            Record(L"Collect", pKeyCode, nullptr, pItemList);
        }

        VOID CollectWordForWildcard(_In_ CStringRange* psrgKeyCode, _Inout_ CDIMEArray<CCandidateListItem>* pItemList,
                                    _In_opt_ CTableDictionaryEngine* freq = nullptr) override
        {
            Record(L"Wildcard", psrgKeyCode, freq, pItemList);
        }

        SEARCH_SECTION Section() const { return _searchSection; }
        const std::wstring& Name() const { return _name; }

    private:
        void Record(const wchar_t* op, CStringRange* key, CTableDictionaryEngine* freq, CDIMEArray<CCandidateListItem>* items)
        {
            std::wstring k = key && key->Get() ? std::wstring(key->Get(), key->GetLength()) : L"<null>";
            std::wstring freqName = freq ? static_cast<CRecordingDictionaryEngine*>(freq)->Name() : L"-";
            _log->push_back(_name + L"." + op + L" section=" + std::to_wstring((int)_searchSection) + L" key=\"" + k +
                            L"\" freq=" + freqName);

            unsigned h = 2166136261u;
            for (wchar_t c : _name + k)
                h = (h ^ c) * 16777619u;
            int count = (int)(h % 4);
            for (int i = 0; i < count; i++)
            {
                unsigned hi = h * 31u + (unsigned)i * 7919u;
                _store->push_back(_name.substr(0, 3) + std::to_wstring(i) + std::wstring((hi >> 8) % 16, L'\x5b57'));
                const std::wstring& item = _store->back();
                std::wstring code = k;
                code.erase(std::remove(code.begin(), code.end(), L'*'), code.end());
                code.erase(std::remove(code.begin(), code.end(), L'?'), code.end());
                code = code.substr(0, (hi >> 4) % (code.size() + 1)) + std::wstring((hi >> 12) % 4, L'q');
                _store->push_back(code);
                const std::wstring& stored = _store->back();
                CCandidateListItem* pLI = items->Append();
                pLI->_ItemString.Set(item.c_str(), item.size());
                pLI->_FindKeyCode.Set(stored.c_str(), stored.size());
                pLI->_WordFrequency = (int)((hi >> 16) % 50);
            }
        }

        std::wstring _name;
        std::vector<std::wstring>* _log;
        std::deque<std::wstring>* _store;
    };

    struct EngineSetup
    {
        IME_MODE mode = IME_MODE::IME_MODE_DAYI;
        bool tableTts = false, big5Tts = false;
        bool big5 = false, custom = false, shortCode = false, phrase = false, extB = false, extCD = false, extE = false, freq = false;
        bool keystrokeSort = false, freqSort = false, phraseRange = false, wildcardFlag = false;
        bool bufferNull = false;
        std::wstring buffer;
        UINT syllable = 0;
        UINT width = 0;
    };
}

// Friend of CCompositionProcessorEngine: one processor engine with recording engines.
class CandidateGenParityAccess
{
public:
    CCompositionProcessorEngine engine{ nullptr };
    std::vector<std::wstring> log;
    std::deque<std::wstring> store;
    std::vector<std::unique_ptr<CRecordingDictionaryEngine>> engines;
    std::wstring buffer;
    static WCHAR s_radical[2];

    ~CandidateGenParityAccess()
    {
        CCompositionProcessorEngine& e = engine;
        for (UINT i = 0; i < IM_SLOTS; i++)
        {
            e._pTableDictionaryEngine[i] = nullptr;
            e._pBig5TableDictionaryEngine[i] = nullptr;
            e._pCustomTableDictionaryEngine[i] = nullptr;
        }
        e._pArrayShortCodeTableDictionaryEngine = nullptr;
        e._pArrayPhraseTableDictionaryEngine = nullptr;
        e._pArrayExtBTableDictionaryEngine = nullptr;
        e._pArrayExtCDTableDictionaryEngine = nullptr;
        e._pArrayExtETableDictionaryEngine = nullptr;
        e._pTCFreqTableDictionaryEngine = nullptr;
        e._keystrokeBuffer.Set(nullptr, 0);
    }

    CRecordingDictionaryEngine* Make(const wchar_t* name, bool tts = false)
    {
        engines.emplace_back(new CRecordingDictionaryEngine(name, tts ? DICTIONARY_TYPE::TTS_DICTIONARY : DICTIONARY_TYPE::CIN_DICTIONARY, &log, &store));
        return engines.back().get();
    }

    void Setup(const EngineSetup& s)
    {
        CCompositionProcessorEngine& e = engine;
        UINT m = (UINT)s.mode;
        CRecordingDictionaryEngine* table = Make(L"table", s.tableTts);
        (*table->GetRadicalMap())[L'A'] = s_radical;
        e._pTableDictionaryEngine[m] = table;
        e._pBig5TableDictionaryEngine[m] = s.big5 ? Make(L"big5", s.big5Tts) : nullptr;
        e._pCustomTableDictionaryEngine[m] = s.custom ? Make(L"custom") : nullptr;
        e._pArrayShortCodeTableDictionaryEngine = s.shortCode ? Make(L"short") : nullptr;
        e._pArrayPhraseTableDictionaryEngine = s.phrase ? Make(L"phrase") : nullptr;
        e._pArrayExtBTableDictionaryEngine = s.extB ? Make(L"extB") : nullptr;
        e._pArrayExtCDTableDictionaryEngine = s.extCD ? Make(L"extCD") : nullptr;
        e._pArrayExtETableDictionaryEngine = s.extE ? Make(L"extE") : nullptr;
        e._pTCFreqTableDictionaryEngine = s.freq ? Make(L"freq") : nullptr;
        e._isKeystrokeSort = s.keystrokeSort;
        e._isWildCardWordFreqSort = s.freqSort;
        e._pActiveCandidateListIndexRange = s.phraseRange ? &e._phraseCandidateListIndexRange : &e._candidateListIndexRange;
        e._hasWildcardIncludedInKeystrokeBuffer = s.wildcardFlag;
        e.phoneticSyllable = s.syllable;
        e._candidateWndWidth = s.width;
        buffer = s.buffer;
        if (s.bufferNull)
            e._keystrokeBuffer.Set(nullptr, 0);
        else
            e._keystrokeBuffer.Set(buffer.c_str(), buffer.size());
    }

    void Run(bool rust, BOOL incremental, BOOL wildcard, BOOL phraseEnding, CDIMEArray<CCandidateListItem>* list)
    {
        if (rust)
            engine._GetCandidateListRust(list, incremental, wildcard, phraseEnding);
        else
            engine._GetCandidateListCpp(list, incremental, wildcard, phraseEnding);
    }

    // Everything observable after the call.
    std::wstring Outcome(CDIMEArray<CCandidateListItem>& list)
    {
        std::wstring s;
        for (const std::wstring& l : log)
            s += l + L"\n";
        for (UINT i = 0; i < list.Count(); i++)
        {
            CCandidateListItem* p = list.GetAt(i);
            s += L"item \"" + std::wstring(p->_ItemString.Get() ? p->_ItemString.Get() : L"", p->_ItemString.GetLength()) +
                 L"\" code \"" + std::wstring(p->_FindKeyCode.Get() ? p->_FindKeyCode.Get() : L"", p->_FindKeyCode.GetLength()) +
                 L"\" freq " + std::to_wstring(p->_WordFrequency) + L"\n";
        }
        s += L"width " + std::to_wstring(engine._candidateWndWidth);
        s += L" wildcard " + std::to_wstring(engine._hasWildcardIncludedInKeystrokeBuffer);
        s += engine._pActiveCandidateListIndexRange == &engine._candidateListIndexRange ? L" range=main" : L" range=phrase";
        for (auto& e : engines)
            s += L" " + e->Name() + L":" + std::to_wstring((int)e->Section());
        s += L" freqTable=" + std::to_wstring(engine._pTCFreqTableDictionaryEngine != nullptr);
        return s;
    }
};

WCHAR CandidateGenParityAccess::s_radical[2] = L"x";

namespace DIMEUnitTests
{
    TEST_CLASS(CandidateGenParityTest)
    {
    private:
        IME_MODE _savedImeMode = IME_MODE::IME_MODE_NONE;
        ARRAY_SCOPE _savedScope = ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A;
        BOOL _savedBig5 = FALSE, _savedPriority = FALSE, _savedQuote = FALSE;
        std::wstring _tempDir;

        static int Rand(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
        static bool Chance(std::mt19937& rng, int percent) { return Rand(rng, 0, 99) < percent; }

        static std::wstring RandomBuffer(std::mt19937& rng)
        {
            static const wchar_t* const kBuffers[] = {
                L"", L"a", L"ab", L"abc", L"abcd", L"*", L"?", L"**", L"?*", L"a*", L"*a", L"a?c", L"=", L"=1", L"=12",
                L"=123", L"w1", L"W", L"h'", L"8[", L"hg1", L"\\", L"\\a", L"\\ab", L"\\a*", L"xyzzy", L"u,4",
            };
            return kBuffers[Rand(rng, 0, ARRAYSIZE(kBuffers) - 1)];
        }

    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            _savedImeMode = Global::imeMode;
            _savedScope = CConfig::GetArrayScope();
            _savedBig5 = CConfig::GetBig5Filter();
            _savedPriority = CConfig::getCustomTablePriority();
            _savedQuote = CConfig::GetArraySingleQuoteCustomPhrase();
            // SetupTCFreqTable may run (frequency sort without a table): keep it
            // away from the real %APPDATA%\DIME.
            WCHAR tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            _tempDir = std::wstring(tmp) + L"DIME_CandidateGenParity";
            CreateDirectoryW(_tempDir.c_str(), nullptr);
            StringCchCopyW(Global::testAppDataOverride, MAX_PATH, _tempDir.c_str());
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            Global::imeMode = _savedImeMode;
            CConfig::SetArrayScope(_savedScope);
            CConfig::SetBig5Filter(_savedBig5);
            CConfig::setCustomTablePriority(_savedPriority);
            CConfig::SetArraySingleQuoteCustomPhrase(_savedQuote);
            DIMETestAppData::Restore();
        }

        TEST_METHOD(RandomStates)
        {
            const int kCases = 30000;
            int nonEmpty = 0;
            for (int seed = 1; seed <= kCases; seed++)
            {
                std::mt19937 rng((unsigned)seed);
                EngineSetup s;
                s.mode = (IME_MODE)Rand(rng, 0, 3);
                s.tableTts = Chance(rng, 30);
                s.big5Tts = Chance(rng, 30);
                s.big5 = Chance(rng, 50);
                s.custom = Chance(rng, 60);
                s.shortCode = Chance(rng, 50);
                s.phrase = Chance(rng, 60);
                s.extB = Chance(rng, 60);
                s.extCD = Chance(rng, 60);
                s.extE = Chance(rng, 60);
                s.freq = Chance(rng, 40);
                s.keystrokeSort = Chance(rng, 40);
                s.freqSort = Chance(rng, 60);
                s.phraseRange = Chance(rng, 30);
                s.wildcardFlag = Chance(rng, 50);
                s.bufferNull = Chance(rng, 5);
                s.buffer = RandomBuffer(rng);
                s.syllable = (UINT)Rand(rng, 0, 0x7FFF);
                if (Chance(rng, 30))
                    s.syllable = (s.syllable & ~0x7000u) | 0x5000u;   // any tone
                s.width = (UINT)Rand(rng, 0, 20);

                Global::imeMode = s.mode;
                CConfig::SetArrayScope((ARRAY_SCOPE)Rand(rng, 0, 5));
                CConfig::SetBig5Filter(Chance(rng, 50));
                CConfig::setCustomTablePriority(Chance(rng, 50));
                CConfig::SetArraySingleQuoteCustomPhrase(Chance(rng, 50));
                BOOL incremental = Chance(rng, 40), wildcard = Chance(rng, 40), phraseEnding = Chance(rng, 30);

                CandidateGenParityAccess cpp, rust;
                cpp.Setup(s);
                rust.Setup(s);
                CDIMEArray<CCandidateListItem> listCpp, listRust;
                cpp.Run(false, incremental, wildcard, phraseEnding, &listCpp);
                rust.Run(true, incremental, wildcard, phraseEnding, &listRust);

                std::wstring oc = cpp.Outcome(listCpp), orr = rust.Outcome(listRust);
                if (oc != orr)
                {
                    wchar_t head[160];
                    swprintf_s(head, L"seed %d mode=%d buf=\"%s\"%s inc=%d wild=%d ending=%d scope=%d\n", seed, (int)s.mode,
                               s.buffer.c_str(), s.bufferNull ? L"(null)" : L"", incremental, wildcard, phraseEnding,
                               (int)CConfig::GetArrayScope());
                    Logger::WriteMessage((std::wstring(head) + L"--- C++\n" + oc + L"\n--- Rust\n" + orr).c_str());
                    Assert::Fail(head);
                }
                if (listCpp.Count())
                    nonEmpty++;
            }
            Logger::WriteMessage((std::to_wstring(kCases) + L" cases, " + std::to_wstring(nonEmpty) + L" with candidates").c_str());
        }
    };
}
