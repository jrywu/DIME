// CLIParserTest.cpp
// Unit tests for the CLI parser logic in CLI.cpp.
// These tests are pure in-process logic tests; they do NOT read or write any
// files or INI entries.

#include "pch.h"
#include "../DIMESettings/CLI.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{

// ---------------------------------------------------------------------------
// ParseModeTests
// ---------------------------------------------------------------------------
TEST_CLASS(ParseModeTests)
{
public:
    TEST_METHOD(ParseMode_Dayi)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --list-keys", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)IME_MODE::IME_MODE_DAYI, (int)args.mode);
        Assert::IsTrue(args.hasMode == TRUE);
    }

    TEST_METHOD(ParseMode_Array)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --list-keys", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)IME_MODE::IME_MODE_ARRAY, (int)args.mode);
    }

    TEST_METHOD(ParseMode_Phonetic)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode phonetic --list-keys", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)IME_MODE::IME_MODE_PHONETIC, (int)args.mode);
    }

    TEST_METHOD(ParseMode_Generic)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode generic --list-keys", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)IME_MODE::IME_MODE_GENERIC, (int)args.mode);
    }

    TEST_METHOD(ParseMode_Invalid_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode badmode --list-keys", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }

    TEST_METHOD(ParseMode_Missing_Arg_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }
};

// ---------------------------------------------------------------------------
// ParseArrayTableNameTests
// ---------------------------------------------------------------------------
TEST_CLASS(ParseArrayTableNameTests)
{
public:
    TEST_METHOD(ParseArrayTable_sp)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array sp C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_SP, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_sc)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array sc C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_SC, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_ext_b)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array ext-b C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_EXT_B, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_ext_cd)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array ext-cd C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_EXT_CD, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_ext_efg)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array ext-efg C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_EXT_EFG, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_array40)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array array40 C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_ARRAY40, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_phrase)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array phrase C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_TABLE_PHRASE, (int)args.arrayTable);
    }

    TEST_METHOD(ParseArrayTable_invalid_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array badtable C:\\dummy.cin", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }

    TEST_METHOD(ParseArrayTable_missing_args_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --load-array", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }
};

// ---------------------------------------------------------------------------
// ParseCLIArgsTests
// ---------------------------------------------------------------------------
TEST_CLASS(ParseCLIArgsTests)
{
public:
    TEST_METHOD(ParseArgs_Get_ReadsKeyName)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --get FontSize", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_GET, (int)args.command);
        Assert::AreEqual(0, wcscmp(args.getKey, L"FontSize"));
    }

    TEST_METHOD(ParseArgs_GetAll)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --get-all", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_GET_ALL, (int)args.command);
    }

    TEST_METHOD(ParseArgs_Set_SinglePair)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --set FontSize 14", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_SET, (int)args.command);
        Assert::AreEqual(1, args.setCount);
        Assert::AreEqual(0, wcscmp(args.setPairs[0].key, L"FontSize"));
        Assert::AreEqual(0, wcscmp(args.setPairs[0].value, L"14"));
    }

    TEST_METHOD(ParseArgs_Set_MultiplePairs)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --set FontSize 14 --set ColorMode 2 --set FontItalic 1", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual(3, args.setCount);
        Assert::AreEqual(0, wcscmp(args.setPairs[0].key, L"FontSize"));
        Assert::AreEqual(0, wcscmp(args.setPairs[1].key, L"ColorMode"));
        Assert::AreEqual(0, wcscmp(args.setPairs[2].key, L"FontItalic"));
    }

    TEST_METHOD(ParseArgs_Reset)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --reset", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_RESET, (int)args.command);
    }

    TEST_METHOD(ParseArgs_ListModes_NoModeRequired)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--list-modes", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_LIST_MODES, (int)args.command);
        Assert::IsTrue(args.hasMode == FALSE);
    }

    TEST_METHOD(ParseArgs_ListKeys)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode phonetic --list-keys", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_LIST_KEYS, (int)args.command);
    }

    TEST_METHOD(ParseArgs_JSON_Flag)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --get-all --json", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::IsTrue(args.jsonOutput == TRUE);
    }

    TEST_METHOD(ParseArgs_Silent_Flag)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --reset --silent", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::IsTrue(args.silent == TRUE);
    }

    TEST_METHOD(ParseArgs_ImportCustom)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --import-custom C:\\phrases.txt", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_IMPORT_CUSTOM, (int)args.command);
        Assert::AreEqual(0, wcscmp(args.filePath, L"C:\\phrases.txt"));
    }

    TEST_METHOD(ParseArgs_ExportCustom)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode array --export-custom C:\\backup.txt", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_EXPORT_CUSTOM, (int)args.command);
        Assert::AreEqual(0, wcscmp(args.filePath, L"C:\\backup.txt"));
    }

    TEST_METHOD(ParseArgs_LoadMain)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode generic --load-main C:\\custom.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_LOAD_MAIN, (int)args.command);
        Assert::AreEqual(0, wcscmp(args.filePath, L"C:\\custom.cin"));
    }

    TEST_METHOD(ParseArgs_LoadPhrase)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode generic --load-phrase C:\\phrase.cin", args, nul);
        fclose(nul);
        Assert::IsTrue(ok);
        Assert::AreEqual((int)CLI_LOAD_PHRASE, (int)args.command);
    }

    TEST_METHOD(ParseArgs_UnknownArg_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --unknown-flag", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }

    TEST_METHOD(ParseArgs_Get_MissingKey_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --get", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }

    TEST_METHOD(ParseArgs_Set_MissingValue_ReturnsFalse)
    {
        CLIArgs args{};
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        bool ok = ParseCLIArgs(L"--mode dayi --set FontSize", args, nul);
        fclose(nul);
        Assert::IsFalse(ok);
    }
};

// ---------------------------------------------------------------------------
// KeyApplicableModeTests
// ---------------------------------------------------------------------------
TEST_CLASS(KeyApplicableModeTests)
{
public:
    TEST_METHOD(DayiArticleMode_Applicable_OnlyToDayi)
    {
        const KeyInfo* ki = CLI_FindKey(L"DayiArticleMode");
        Assert::IsNotNull(ki);
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }

    TEST_METHOD(ArrayScope_Applicable_OnlyToArray)
    {
        const KeyInfo* ki = CLI_FindKey(L"ArrayScope");
        Assert::IsNotNull(ki);
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }

    TEST_METHOD(PhoneticKeyboardLayout_Applicable_OnlyToPhonetic)
    {
        const KeyInfo* ki = CLI_FindKey(L"PhoneticKeyboardLayout");
        Assert::IsNotNull(ki);
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }

    TEST_METHOD(SpaceAsFirstCandSelkey_Applicable_OnlyToGeneric)
    {
        const KeyInfo* ki = CLI_FindKey(L"SpaceAsFirstCandSelkey");
        Assert::IsNotNull(ki);
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }

    TEST_METHOD(Big5Filter_NotApplicable_ToArray)
    {
        const KeyInfo* ki = CLI_FindKey(L"Big5Filter");
        Assert::IsNotNull(ki);
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsFalse(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsTrue (CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }

    TEST_METHOD(FontSize_Applicable_ToAllModes)
    {
        const KeyInfo* ki = CLI_FindKey(L"FontSize");
        Assert::IsNotNull(ki);
        Assert::IsTrue(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_DAYI));
        Assert::IsTrue(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_ARRAY));
        Assert::IsTrue(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_PHONETIC));
        Assert::IsTrue(CLI_KeyApplicableToMode(ki, IME_MODE::IME_MODE_GENERIC));
    }
};

// ---------------------------------------------------------------------------
// ParseColorValueTests
// ---------------------------------------------------------------------------
TEST_CLASS(ParseColorValueTests)
{
public:
    TEST_METHOD(ParseColor_HexWithPrefix)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"0xFF8040", c);
        Assert::IsTrue(ok);
        Assert::AreEqual((DWORD)0xFF8040, (DWORD)c);
    }

    TEST_METHOD(ParseColor_HexUpperCasePrefix)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"0XFF8040", c);
        Assert::IsTrue(ok);
        Assert::AreEqual((DWORD)0xFF8040, (DWORD)c);
    }

    TEST_METHOD(ParseColor_HexNoPrefix)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"FF8040", c);
        Assert::IsTrue(ok);
        Assert::AreEqual((DWORD)0xFF8040, (DWORD)c);
    }

    TEST_METHOD(ParseColor_Black)
    {
        COLORREF c = 1;
        bool ok = CLI_ParseColorValue(L"0x000000", c);
        Assert::IsTrue(ok);
        Assert::AreEqual((DWORD)0x000000u, (DWORD)c);
    }

    TEST_METHOD(ParseColor_White)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"0xFFFFFF", c);
        Assert::IsTrue(ok);
        Assert::AreEqual((DWORD)0xFFFFFF, (DWORD)c);
    }

    TEST_METHOD(ParseColor_InvalidString_ReturnsFalse)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"not-a-color", c);
        Assert::IsFalse(ok);
    }

    TEST_METHOD(ParseColor_EmptyString_ReturnsFalse)
    {
        COLORREF c = 0;
        bool ok = CLI_ParseColorValue(L"", c);
        Assert::IsFalse(ok);
    }
};

// ---------------------------------------------------------------------------
// FindKeyTests
// ---------------------------------------------------------------------------
TEST_CLASS(FindKeyTests)
{
public:
    TEST_METHOD(FindKey_ExistingKey_ReturnsNonNull)
    {
        const KeyInfo* ki = CLI_FindKey(L"AutoCompose");
        Assert::IsNotNull(ki);
        Assert::AreEqual(0, wcscmp(ki->name, L"AutoCompose"));
        Assert::AreEqual((int)BOOL_T, (int)ki->type);
    }

    TEST_METHOD(FindKey_NonExistentKey_ReturnsNull)
    {
        const KeyInfo* ki = CLI_FindKey(L"NonExistentKey");
        Assert::IsNull(ki);
    }

    TEST_METHOD(FindKey_CaseSensitive_WrongCase_ReturnsNull)
    {
        const KeyInfo* ki = CLI_FindKey(L"autocompose");
        Assert::IsNull(ki);
    }

    TEST_METHOD(FindKey_IntKey_HasCorrectRange)
    {
        const KeyInfo* ki = CLI_FindKey(L"FontSize");
        Assert::IsNotNull(ki);
        Assert::AreEqual((int)INT_T, (int)ki->type);
        Assert::AreEqual(8,  ki->minVal);
        Assert::AreEqual(72, ki->maxVal);
    }

    TEST_METHOD(FindKey_ColorKey_HasCorrectType)
    {
        const KeyInfo* ki = CLI_FindKey(L"ItemColor");
        Assert::IsNotNull(ki);
        Assert::AreEqual((int)COLOR_T, (int)ki->type);
    }

    TEST_METHOD(FindKey_StringKey_HasCorrectType)
    {
        const KeyInfo* ki = CLI_FindKey(L"FontFaceName");
        Assert::IsNotNull(ki);
        Assert::AreEqual((int)STRING_T, (int)ki->type);
    }

    TEST_METHOD(FindKey_CLSIDKey_HasCorrectType)
    {
        const KeyInfo* ki = CLI_FindKey(L"ReverseConversionCLSID");
        Assert::IsNotNull(ki);
        Assert::AreEqual((int)CLSID_T, (int)ki->type);
    }

    TEST_METHOD(AllKeys_HaveUniqueNames)
    {
        int n = CLI_GetKeyCount();
        const KeyInfo* reg = CLI_GetKeyRegistry();
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                Assert::AreNotEqual(0, wcscmp(reg[i].name, reg[j].name));
    }
};

// ---------------------------------------------------------------------------
// RunCLI_ListModesTests  (no mode needed, no file I/O)
// ---------------------------------------------------------------------------
TEST_CLASS(RunCLI_ListModesUnitTest)
{
public:
    TEST_METHOD(ListModes_Plain_OutputsFourModes)
    {
        // Write to a temp file, then verify content
        WCHAR tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        wcscat_s(tmp, L"cli_listmodes_test.txt");

        FILE* f = nullptr;
        _wfopen_s(&f, tmp, L"w+, ccs=UTF-16LE");
        Assert::IsNotNull(f);

        int rc = RunCLI(L"--list-modes", f);
        fflush(f); rewind(f);

        // Read back (skip the UTF-16LE BOM at position 0)
        wchar_t buf[256] = {};
        fread(buf, sizeof(wchar_t), 255, f);
        fclose(f);
        DeleteFileW(tmp);

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"dayi"));
        Assert::IsNotNull(wcsstr(buf, L"array"));
        Assert::IsNotNull(wcsstr(buf, L"phonetic"));
        Assert::IsNotNull(wcsstr(buf, L"generic"));
    }

    TEST_METHOD(ListModes_JSON_OutputsArray)
    {
        WCHAR tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        wcscat_s(tmp, L"cli_listmodes_json_test.txt");

        FILE* f = nullptr;
        _wfopen_s(&f, tmp, L"w+, ccs=UTF-16LE");
        Assert::IsNotNull(f);

        int rc = RunCLI(L"--list-modes --json", f);
        fflush(f); rewind(f);

        wchar_t buf[256] = {};
        fread(buf, sizeof(wchar_t), 255, f);
        fclose(f);
        DeleteFileW(tmp);

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"["));
        Assert::IsNotNull(wcsstr(buf, L"dayi"));
    }
};

// ---------------------------------------------------------------------------
// RunCLI_ExitCodeUnitTests  (error paths that don't need file I/O)
// ---------------------------------------------------------------------------
TEST_CLASS(RunCLI_ExitCodeUnitTests)
{
public:
    TEST_METHOD(NoCommand_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--mode dayi", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(NoMode_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--get FontSize", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(UnknownKey_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--mode dayi --get NoSuchKey", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(KeyNotApplicableToMode_ReturnsExitCode3)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        // DayiArticleMode is only valid for dayi mode
        int rc = RunCLI(L"--mode array --get DayiArticleMode", nul);
        fclose(nul);
        Assert::AreEqual(3, rc);
    }

    TEST_METHOD(SetKeyNotApplicableToMode_ReturnsExitCode3)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        // ArrayScope is only valid for array mode
        int rc = RunCLI(L"--mode dayi --set ArrayScope 2", nul);
        fclose(nul);
        Assert::AreEqual(3, rc);
    }

    TEST_METHOD(SetInvalidIntValue_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--mode dayi --set FontSize notanumber", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(SetOutOfRangeIntValue_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--mode dayi --set FontSize 999", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(SetUnknownKey_ReturnsExitCode1)
    {
        FILE* nul = nullptr; _wfopen_s(&nul, L"NUL", L"w");
        int rc = RunCLI(L"--mode dayi --set NoSuchKey 42", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }
};

} // namespace DIMEUnitTests
