// KeyFlowTest.cpp - golden key-flow tests (Rust migration phase 2a)
//
// Each scenario types a key script into the real CDIME (see KeyFlowHarness.h) and
// compares the per-key trace with src/tests/keyflow/<name>.txt.
//
// Re-record after an intended behaviour change:
//   set DIME_KEYFLOW_RECORD=1 and run the tests; review the diff of src/tests/keyflow.

#include "pch.h"
#include "CppUnitTest.h"
#include "KeyFlowHarness.h"

#include <fstream>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        std::wstring RepoRoot()
        {
            std::wstring p = __FILEW__;            // ...\src\tests\KeyFlowTest.cpp
            for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
            return p + L"\\";
        }

        std::wstring Installer(const wchar_t* name) { return L"@" + RepoRoot() + L"installer\\" + name; }

        std::string ToUtf8(const std::wstring& s)
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
            std::string out(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
            return out;
        }

        std::wstring FromUtf8(const std::string& s)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring out(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
            return out;
        }

        // Golden file: "# script: ..." header, then one line per key.
        void CheckGolden(const std::wstring& name, const KeyFlowOptions& options, const std::wstring& script)
        {
            std::vector<std::wstring> actual = RunKeyFlow(options, script);
            std::wstring path = RepoRoot() + L"src\\tests\\keyflow\\" + name + L".txt";

            wchar_t record[8] = {};
            if (GetEnvironmentVariableW(L"DIME_KEYFLOW_RECORD", record, 8) && record[0] == L'1')
            {
                CreateDirectoryW((RepoRoot() + L"src\\tests\\keyflow").c_str(), nullptr);
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                f << ToUtf8(L"# script: " + script) << "\n";
                for (const auto& l : actual) f << ToUtf8(l) << "\n";
                Logger::WriteMessage((L"recorded " + path + L"\n").c_str());
                return;
            }

            std::ifstream f(path, std::ios::binary);
            Assert::IsTrue(f.good(), (L"missing golden file " + path + L" (run with DIME_KEYFLOW_RECORD=1)").c_str());
            std::vector<std::wstring> expected;
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("# ", 0) == 0) continue;
                expected.push_back(FromUtf8(line));
            }
            for (size_t i = 0; i < (std::max)(expected.size(), actual.size()); i++)
            {
                std::wstring e = i < expected.size() ? expected[i] : L"<none>";
                std::wstring a = i < actual.size() ? actual[i] : L"<none>";
                if (e != a)
                    Assert::Fail((name + L" step " + std::to_wstring(i) + L"\n expected: " + e + L"\n   actual: " + a).c_str());
            }
        }

        KeyFlowOptions ArrayOptions(std::vector<std::wstring> config = {})
        {
            KeyFlowOptions o;
            o.mode = IME_MODE::IME_MODE_ARRAY;
            o.appDataFiles = {
                { L"Array.cin", Installer(L"Array.cin") },
                { L"Array40.cin", Installer(L"Array40.cin") },
                { L"Array-special.cin", Installer(L"Array-special.cin") },
                { L"Array-shortcode.cin", Installer(L"Array-shortcode.cin") },
                { L"Array-Phrase.cin", Installer(L"Array-Phrase.cin") },
                { L"Array-Ext-B.cin", Installer(L"Array-Ext-B.cin") },
                { L"Array-Ext-CD.cin", Installer(L"Array-Ext-CD.cin") },
                { L"Array-Ext-EF.cin", Installer(L"Array-Ext-EF.cin") },
            };
            o.config = std::move(config);
            return o;
        }

        KeyFlowOptions PhoneticOptions(std::vector<std::wstring> config = {})
        {
            KeyFlowOptions o;
            o.mode = IME_MODE::IME_MODE_PHONETIC;
            o.appDataFiles = { { L"Phone.cin", Installer(L"phone.cin") } };
            o.config = std::move(config);
            return o;
        }

        // A small Dayi-like table (the real Dayi table is not redistributable).
        const wchar_t* kDayiCin =
            L"%gen_inp\n%ename\tDayi\n%cname\t\x5927\x6613\n%selkey\t1234567890\n"
            L"%keyname begin\n"
            L"a\t\x4EBA\nb\t\x99AC\nc\t\x91D1\nd\t\x8A00\ne\t\x5C71\nf\t\x571F\ng\t\x738B\n"
            L",\t\x529B\n.\t\x821F\n/\t\x77F3\n;\t\x866B\n=\t\x7B26\n"
            L"%keyname end\n"
            L"%chardef begin\n"
            L"a\t\x4EBA\na\t\x5165\na\t\x516B\n"                 // a: 人 入 八
            L"ab\t\x5F9E\n"                                       // ab: 從 (only one)
            L"c\t\x91D1\n"                                        // c: 金
            L"d\t\x8A00\nd\t\x8A5E\n"                             // d: 言 詞
            L"ad\t\x4FE1\n"                                       // ad: 信
            L"dd\t\x9019\ndd\t\x8AF8\n"                           // dd: 這 諸
            L"e\t\x5C71\ne\t\x51FA\n"                             // e: 山 出
            L"ef\t\x5730\n"                                       // ef: 地
            L"g\t\x738B\ng\t\x570B\ng\t\x4E3B\ng\t\x7389\ng\t\x5168\ng\t\x91D1\ng\t\x7406\ng\t\x73FE\ng\t\x7403\ng\t\x73ED\ng\t\x73E0\ng\t\x7434\n" // g: 12 candidates
            L"=1\t\xFF0C\n=1\t\x3002\n=2\t\x300C\n=2\t\x300D\n"  // symbols
            L"%chardef end\n";

        // Associated phrases: key = committed character, value = continuation.
        const wchar_t* kPhraseCin =
            L"%chardef begin\n"
            L"\x4EBA\t\x6C11\n\x4EBA\t\x5011\n\x4EBA\t\x751F\n"   // 人: 民 們 生
            L"\x5730\t\x65B9\n"                                   // 地: 方
            L"%chardef end\n";

        // Windows built-in table format (TableTextServiceDaYi.txt): used for associated
        // phrases when no Phrase.cin is present ([Phrase] section, exact key lookup).
        const wchar_t* kTtsPhrase =
            L"[Phrase]\n"
            L"\"\x4EBA\"=\"\x6C11\",\"\x5011\",\"\x751F\"\n"       // 人: 民 們 生
            L"\"\x5730\"=\"\x65B9\",\"\x7403\"\n";                  // 地: 方 球

        // A small custom (Generic) table.
        const wchar_t* kGenericCin =
            L"%gen_inp\n%ename\tTest\n%cname\t\x6E2C\x8A66\n%selkey\t1234567890\n%endkey\t;\n"
            L"%keyname begin\n"
            L"a\tA\nb\tB\nc\tC\nd\tD\n;\t;\n"
            L"%keyname end\n"
            L"%chardef begin\n"
            L"a\t\x4E00\na\t\x4E8C\na\t\x4E09\na\t\x56DB\na\t\x4E94\na\t\x516D\na\t\x4E03\na\t\x516B\na\t\x4E5D\na\t\x5341\na\t\x767E\na\t\x5343\n" // a: 一..十 百 千
            L"ab\t\x6771\nab\t\x897F\n"                           // ab: 東 西
            L"abc\t\x5357\n"                                      // abc: 南
            L"abcd\t\x5317\n"                                     // abcd: 北
            L"b\t\x5927\nb\t\x5C0F\n"                             // b: 大 小
            L"c\t\x4E0A\n"                                        // c: 上
            L"%chardef end\n";

        KeyFlowOptions DayiOptions(std::vector<std::wstring> config = {})
        {
            KeyFlowOptions o;
            o.mode = IME_MODE::IME_MODE_DAYI;
            o.appDataFiles = { { L"Dayi.cin", kDayiCin }, { L"TableTextServiceDaYi.txt", kTtsPhrase } };
            o.config = std::move(config);
            return o;
        }

        KeyFlowOptions GenericOptions(std::vector<std::wstring> config = {})
        {
            KeyFlowOptions o;
            o.mode = IME_MODE::IME_MODE_GENERIC;
            o.appDataFiles = { { L"Generic.cin", kGenericCin }, { L"TableTextServiceDaYi.txt", kTtsPhrase } };
            o.config = std::move(config);
            return o;
        }
    }

    // Keys that the IME passes to the application do not change the document here:
    // the trace shows what the IME itself did ("passed" = the app would handle the key).
    TEST_CLASS(KeyFlowTest)
    {
    public:
        // ---- Array (installer tables) ------------------------------------------
        TEST_METHOD(Array_TypeAndCommit)      { CheckGolden(L"array_type_and_commit", ArrayOptions(), L"t a Space 1 Back"); }
        TEST_METHOD(Array_SelectByNumber)     { CheckGolden(L"array_select_by_number", ArrayOptions(), L"a Space 2 a Space 1"); }
        TEST_METHOD(Array_BackspaceEscape)    { CheckGolden(L"array_backspace_escape", ArrayOptions(), L"t a Back Back Back t a Esc Esc"); }
        TEST_METHOD(Array_CandidateNavigation){ CheckGolden(L"array_candidate_navigation", ArrayOptions(), L"a Space Down Down Up PgDn PgUp Right Left Enter"); }
        TEST_METHOD(Array_PhraseEnding)       { CheckGolden(L"array_phrase_ending", ArrayOptions(), L"t a ' r g ' Space"); }
        TEST_METHOD(Array_Wildcard)           { CheckGolden(L"array_wildcard", ArrayOptions(), L"t Shift+/ Space 1 t Shift+8 Space Esc"); }
        TEST_METHOD(Array_SymbolMode)         { CheckGolden(L"array_symbol_mode", ArrayOptions(), L"w 1 Space 1 w 2 Esc"); }
        TEST_METHOD(Array_ShiftEnglish)       { CheckGolden(L"array_shift_english", ArrayOptions(), L"Shift+a Shift+b t Shift+c Shift+1 Shift+/"); }
        TEST_METHOD(Array_MaxCodes)           { CheckGolden(L"array_max_codes", ArrayOptions(), L"a a a a a a Esc"); }
        TEST_METHOD(Array_MaxCodesNoClear)    { CheckGolden(L"array_max_codes_no_clear", ArrayOptions({ L"ClearOnBeep = 0" }), L"a a a a a a Esc"); }
        TEST_METHOD(Array_ForceSpecialCode)   { CheckGolden(L"array_force_special_code", ArrayOptions({ L"ArrayForceSP = 1" }), L"t a Space y d Space"); }
        TEST_METHOD(Array40)                  { CheckGolden(L"array40", ArrayOptions({ L"ArrayScope = 4" }), L"t a Space a Space 2"); }
        TEST_METHOD(Array_Punctuation)        { CheckGolden(L"array_punctuation", ArrayOptions(), L", . / ; Space Enter"); }

        // ---- Phonetic (installer table) ----------------------------------------
        TEST_METHOD(Phonetic_Basic)           { CheckGolden(L"phonetic_basic", PhoneticOptions(), L"s u 3 1 c l 3 1"); }
        TEST_METHOD(Phonetic_SpaceTone)       { CheckGolden(L"phonetic_space_tone", PhoneticOptions(), L"5 j / Space Space Down Enter"); }
        TEST_METHOD(Phonetic_EditReading)     { CheckGolden(L"phonetic_edit_reading", PhoneticOptions(), L"j i Back Back j i 3 Esc j i 4 Back"); }
        TEST_METHOD(Phonetic_Replace)         { CheckGolden(L"phonetic_replace", PhoneticOptions(), L"s j u 3"); }
        TEST_METHOD(Phonetic_Eten)            { CheckGolden(L"phonetic_eten", PhoneticOptions({ L"PhoneticKeyboardLayout = 1" }), L"n e 3 1"); }

        // ---- Dayi (synthetic table) --------------------------------------------
        TEST_METHOD(Dayi_SpaceSelectsFirst)   { CheckGolden(L"dayi_space_selects_first", DayiOptions(), L"a Space d Space 2 a b Space"); }
        TEST_METHOD(Dayi_UniqueCandidate)     { CheckGolden(L"dayi_unique_candidate", DayiOptions(), L"c Space a d Space"); }
        TEST_METHOD(Dayi_AssociatedPhrase)    { CheckGolden(L"dayi_associated_phrase", DayiOptions(), L"a Space Space 2 a Space Space Esc e f Space a Space Space Down Enter a Space Space Shift+x a Space Space a"); }
        TEST_METHOD(Dayi_PhraseCin)
        {
            // A user Phrase.cin (README format). Currently yields no associated phrases: the
            // lookup appends '*' unless a TTS [Phrase] section was parsed, and CIN CollectWord
            // does not treat '*' as a wildcard. Recorded as-is; see docs/RUST_MIGRATION_PLAN.md.
            KeyFlowOptions o = DayiOptions();
            o.appDataFiles = { { L"Dayi.cin", kDayiCin }, { L"Phrase.cin", kPhraseCin } };
            CheckGolden(L"dayi_phrase_cin", o, L"a Space Space 2");
        }
        TEST_METHOD(Dayi_NoPhrase)            { CheckGolden(L"dayi_no_phrase", DayiOptions({ L"MakePhrase = 0" }), L"a Space Space e f Space"); }
        TEST_METHOD(Dayi_PagingTwelve)        { CheckGolden(L"dayi_paging_twelve", DayiOptions(), L"g Space PgDn PgUp Down Down 3"); }
        TEST_METHOD(Dayi_SpaceAsPageDown)     { CheckGolden(L"dayi_space_as_page_down", DayiOptions({ L"SpaceAsPageDown = 1", L"SpaceAsFirstCandSelkey = 0" }), L"g Space Space Space 1"); }
        TEST_METHOD(Dayi_SelkeyNumeric)       { CheckGolden(L"dayi_selkey_numeric", DayiOptions({ L"DayiSelkeyMode = 1" }), L"a Space 2 g Space 0"); }
        TEST_METHOD(Dayi_SelkeyDayi)          { CheckGolden(L"dayi_selkey_dayi", DayiOptions({ L"DayiSelkeyMode = 2" }), L"a Space [ g Space Shift+' g Space \\"); }
        TEST_METHOD(Dayi_AddressChars)        { CheckGolden(L"dayi_address_chars", DayiOptions(), L"' [ ] - \\ `"); }
        TEST_METHOD(Dayi_ArticleMode)         { CheckGolden(L"dayi_article_mode", DayiOptions({ L"DayiArticleMode = 1" }), L"' [ ] - \\"); }
        TEST_METHOD(Dayi_SymbolPrompt)        { CheckGolden(L"dayi_symbol_prompt", DayiOptions(), L"= 1 Space 2 = 2 Space 1"); }
        TEST_METHOD(Dayi_AutoCompose)         { CheckGolden(L"dayi_auto_compose", DayiOptions({ L"AutoCompose = 1" }), L"a d Back g 3"); }
        TEST_METHOD(Dayi_FullShapeShift)      { CheckGolden(L"dayi_shift_english_caps", DayiOptions(), L"Shift+a Shift+1"); }
        TEST_METHOD(Dayi_Sentence)
        {
            KeyFlowOptions o = DayiOptions({ L"SentenceMode = 1" });
            o.appDataFiles.push_back({ L"TCFreq.cin", Installer(L"TCFreq.cin") });
            o.appDataFiles.push_back({ L"Array-Phrase.cin", Installer(L"Array-Phrase.cin") });
            CheckGolden(L"dayi_sentence", o, L"a Space d Space a d Space Left Left Down 2 Right Left Down Esc Enter");
        }
        TEST_METHOD(Dayi_SentenceEdit)
        {
            KeyFlowOptions o = DayiOptions({ L"SentenceMode = 1" });
            o.appDataFiles.push_back({ L"TCFreq.cin", Installer(L"TCFreq.cin") });
            CheckGolden(L"dayi_sentence_edit", o, L"a Space e Space Back d Space Del Left Left Del Esc a Space , Esc");
        }

        // ---- Generic (synthetic table) -----------------------------------------
        TEST_METHOD(Generic_EndKey)           { CheckGolden(L"generic_end_key", GenericOptions(), L"a b ; b ; 2"); }
        TEST_METHOD(Generic_SpaceFirst)       { CheckGolden(L"generic_space_first", GenericOptions({ L"SpaceAsFirstCandSelkey = 1" }), L"a Space Space b Space Space"); }
        TEST_METHOD(Generic_MaxCodes)         { CheckGolden(L"generic_max_codes", GenericOptions({ L"MaxCodes = 2" }), L"a b c Space a b c d"); }
        TEST_METHOD(Generic_AutoComposeWildcard){ CheckGolden(L"generic_auto_compose_wildcard", GenericOptions({ L"AutoCompose = 1" }), L"a Shift+8 Space Esc a Shift+/ Space 1"); }
        TEST_METHOD(Generic_ArrowKeySWPages)  { CheckGolden(L"generic_arrow_pages", GenericOptions({ L"ArrowKeySWPages = 1" }), L"a Space Right Right Left 1"); }
        TEST_METHOD(Generic_NoArrowPages)     { CheckGolden(L"generic_no_arrow_pages", GenericOptions({ L"ArrowKeySWPages = 0" }), L"a Space Right Right Left 1"); }
    };
}
