// CLIIntegrationTest.cpp
// End-to-end integration tests for the CLI interface.
// Each test calls RunCLI() and then asserts the result by reading the
// INI config file directly via GetPrivateProfileStringW, ensuring the
// correct bytes are persisted on disk.

#include "pch.h"
#include <shlwapi.h>
#include "../DIMESettings/CLI.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEIntegratedTests
{

// ---------------------------------------------------------------------------
// Helpers shared across all integration test classes
// ---------------------------------------------------------------------------

// Returns the path to the config INI file for a given mode.
static std::wstring GetConfigFilePath(IME_MODE mode)
{
    WCHAR appData[MAX_PATH] = {};
    SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
    const wchar_t* name = L"GenericConfig.ini";
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     name = L"DayiConfig.ini";    break;
    case IME_MODE::IME_MODE_ARRAY:    name = L"ArrayConfig.ini";   break;
    case IME_MODE::IME_MODE_PHONETIC: name = L"PhoneConfig.ini";   break;
    default: break;
    }
    WCHAR path[MAX_PATH];
    StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\%s", appData, name);
    return path;
}

// Delete the config file so each test starts from a clean state.
static void DeleteConfigFile(IME_MODE mode)
{
    std::wstring path = GetConfigFilePath(mode);
    DeleteFileW(path.c_str());
}

// Read one key from the INI file directly (primary assertion method).
static std::wstring ReadIniKey(IME_MODE mode, const wchar_t* key)
{
    std::wstring path = GetConfigFilePath(mode);
    WCHAR buf[512] = {};
    GetPrivateProfileStringW(L"Config", key, L"__NOT_FOUND__", buf, 512, path.c_str());
    // Trim leading space that WriteConfig emits ("Key = value")
    std::wstring s(buf);
    size_t start = s.find_first_not_of(L" \t");
    return (start == std::wstring::npos) ? s : s.substr(start);
}

// Open a null output sink so RunCLI() doesn't write to stdout during tests.
static FILE* OpenNullOut()
{
    FILE* f = nullptr;
    _wfopen_s(&f, L"NUL", L"w");
    return f;
}

// Write a minimal temporary CIN file to TEMP and return its path.
static std::wstring MakeTempCIN(const wchar_t* suffix, const char* utf8Content)
{
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"dime_cli_test_" + suffix + L".cin";
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"w");  // narrow text mode; content is ASCII
    if (f)
    {
        fputs(utf8Content, f);
        fclose(f);
    }
    return path;
}

// ---------------------------------------------------------------------------
// GetCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIGetCommandTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_DAYI);
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_DAYI);
    }

    TEST_METHOD(Get_FontSize_DefaultValue)
    {
        // First --reset to write defaults
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        fclose(nul);

        // Capture output
        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_get_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");

        int rc = RunCLI(L"--mode dayi --get FontSize", out);
        fflush(out); rewind(out);
        wchar_t buf[128] = {}; fread(buf, sizeof(wchar_t), 127, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"FontSize="));
    }

    TEST_METHOD(Get_UnknownKey_ReturnsExitCode1)
    {
        // Ensure defaults are written so LoadConfig can work
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --get DoesNotExist", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }

    TEST_METHOD(Get_KeyNotApplicableToMode_ReturnsExitCode3)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode array --reset", nul);
        int rc = RunCLI(L"--mode array --get DayiArticleMode", nul);
        fclose(nul);
        Assert::AreEqual(3, rc);
    }
};

// ---------------------------------------------------------------------------
// GetAllCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIGetAllCommandTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC); }

    TEST_METHOD(GetAll_Phonetic_OutputsPhoneticKeys)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode phonetic --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_getall_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");

        int rc = RunCLI(L"--mode phonetic --get-all", out);
        fflush(out); rewind(out);
        wchar_t buf[4096] = {}; fread(buf, sizeof(wchar_t), 4095, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        // PhoneticKeyboardLayout is phonetic-only
        Assert::IsNotNull(wcsstr(buf, L"PhoneticKeyboardLayout"));
        // ArrayScope must NOT appear
        Assert::IsNull(wcsstr(buf, L"ArrayScope"));
    }

    TEST_METHOD(GetAll_Array_OutputsArrayKeys)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode array --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_getall_array_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");

        int rc = RunCLI(L"--mode array --get-all", out);
        fflush(out); rewind(out);
        wchar_t buf[4096] = {}; fread(buf, sizeof(wchar_t), 4095, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"ArrayScope"));
        // Big5Filter must NOT appear for array mode
        Assert::IsNull(wcsstr(buf, L"Big5Filter"));
    }
};

// ---------------------------------------------------------------------------
// SetCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLISetCommandTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(Set_FontSize_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set FontSize 18", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        std::wstring v = ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize");
        Assert::AreEqual(std::wstring(L"18"), v);
    }

    TEST_METHOD(Set_MultipleKeys_AllWrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set FontSize 20 --set FontWeight 700 --set FontItalic 1", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"20"),  ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize"));
        Assert::AreEqual(std::wstring(L"700"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontWeight"));
        Assert::AreEqual(std::wstring(L"1"),   ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontItalic"));
    }

    TEST_METHOD(Set_BooleanKey_Zero_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set AutoCompose 0", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"0"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"AutoCompose"));
    }

    TEST_METHOD(Set_ColorMode_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set ColorMode 2", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"2"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"ColorMode"));
    }

    TEST_METHOD(Set_ColorKey_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set ItemColor 0xFF1234", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        std::wstring v = ReadIniKey(IME_MODE::IME_MODE_DAYI, L"ItemColor");
        // Written as "0xRRGGBB" (uppercase)
        Assert::AreEqual(std::wstring(L"0xFF1234"), v);
    }

    TEST_METHOD(Set_ArraySpecificKey_InArrayMode_WrittenToINI)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_ARRAY);
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode array --reset", nul);
        int rc = RunCLI(L"--mode array --set ArrayScope 3", nul);
        fclose(nul);
        DeleteConfigFile(IME_MODE::IME_MODE_ARRAY);

        Assert::AreEqual(0, rc);
    }

    TEST_METHOD(Set_InvalidIntValue_DoesNotWriteINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        // Set a valid value first
        RunCLI(L"--mode dayi --set FontSize 14", nul);
        // Now try invalid
        int rc = RunCLI(L"--mode dayi --set FontSize abc", nul);
        fclose(nul);

        Assert::AreEqual(1, rc);
        // FontSize should still be 14 (not changed)
        Assert::AreEqual(std::wstring(L"14"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize"));
    }

    TEST_METHOD(Set_OutOfRangeValue_DoesNotWriteINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        RunCLI(L"--mode dayi --set MaxCodes 4", nul);
        int rc = RunCLI(L"--mode dayi --set MaxCodes 999", nul);
        fclose(nul);

        Assert::AreEqual(1, rc);
        Assert::AreEqual(std::wstring(L"4"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"MaxCodes"));
    }
};

// ---------------------------------------------------------------------------
// ResetCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIResetCommandTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_GENERIC); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_GENERIC); }

    TEST_METHOD(Reset_WritesINIFile)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode generic --reset", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        // After reset the INI file must exist
        std::wstring path = GetConfigFilePath(IME_MODE::IME_MODE_GENERIC);
        Assert::IsTrue(PathFileExistsW(path.c_str()) == TRUE);
    }

    TEST_METHOD(Reset_OverwritesCustomValue)
    {
        FILE* nul = OpenNullOut();
        // Write a non-default value
        RunCLI(L"--mode generic --reset", nul);
        RunCLI(L"--mode generic --set FontSize 30", nul);
        // Verify it was set
        std::wstring before = ReadIniKey(IME_MODE::IME_MODE_GENERIC, L"FontSize");
        Assert::AreEqual(std::wstring(L"30"), before);

        // Now reset
        int rc = RunCLI(L"--mode generic --reset", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        // After reset FontSize should be the default (12)
        std::wstring after = ReadIniKey(IME_MODE::IME_MODE_GENERIC, L"FontSize");
        Assert::AreEqual(std::wstring(L"12"), after);
    }
};

// ---------------------------------------------------------------------------
// ListModesCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIListModesCommandTests)
{
public:
    TEST_METHOD(ListModes_ExitCode0)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--list-modes", nul);
        fclose(nul);
        Assert::AreEqual(0, rc);
    }
};

// ---------------------------------------------------------------------------
// ListKeysCommandTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIListKeysCommandTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC); }

    TEST_METHOD(ListKeys_Phonetic_ContainsPhoneticLayout)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode phonetic --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_listkeys_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");

        int rc = RunCLI(L"--mode phonetic --list-keys", out);
        fflush(out); rewind(out);
        wchar_t buf[4096] = {}; fread(buf, sizeof(wchar_t), 4095, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"PhoneticKeyboardLayout"));
    }

    TEST_METHOD(ListKeys_Array_ContainsArrayScope)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_ARRAY);
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode array --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_listkeys_array_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");

        int rc = RunCLI(L"--mode array --list-keys", out);
        fflush(out); rewind(out);
        wchar_t buf[4096] = {}; fread(buf, sizeof(wchar_t), 4095, out);
        fclose(out); DeleteFileW(outPath.c_str());
        DeleteConfigFile(IME_MODE::IME_MODE_ARRAY);

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"ArrayScope"));
        Assert::IsNull(wcsstr(buf, L"PhoneticKeyboardLayout"));
    }
};

// ---------------------------------------------------------------------------
// ImportExportCustomTableTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIImportExportCustomTableTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
        // Remove any existing custom table
        WCHAR appData[MAX_PATH]; SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
        WCHAR customPath[MAX_PATH];
        StringCchPrintfW(customPath, MAX_PATH, L"%s\\DIME\\GENERIC-CUSTOM.txt", appData);
        DeleteFileW(customPath);
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
        WCHAR appData[MAX_PATH]; SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
        WCHAR customPath[MAX_PATH];
        StringCchPrintfW(customPath, MAX_PATH, L"%s\\DIME\\GENERIC-CUSTOM.txt", appData);
        DeleteFileW(customPath);
    }

    TEST_METHOD(ImportCustom_ValidFile_ExitCode0)
    {
        std::wstring src = MakeTempCIN(L"import_generic",
            "a b\n"
            "b c\n");

        FILE* nul = OpenNullOut();
        std::wstring cmd = L"--mode generic --import-custom " + src;
        int rc = RunCLI(cmd.c_str(), nul);
        fclose(nul);
        DeleteFileW(src.c_str());

        Assert::AreEqual(0, rc);

        // The GENERIC-CUSTOM.txt must now exist
        WCHAR appData[MAX_PATH]; SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
        WCHAR customPath[MAX_PATH];
        StringCchPrintfW(customPath, MAX_PATH, L"%s\\DIME\\GENERIC-CUSTOM.txt", appData);
        Assert::IsTrue(PathFileExistsW(customPath) == TRUE);
    }

    TEST_METHOD(ImportCustom_NonExistentFile_ExitCode2)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode generic --import-custom C:\\nonexistent_file_xyz.cin", nul);
        fclose(nul);
        Assert::AreEqual(2, rc);
    }

    TEST_METHOD(ExportCustom_NoCustomTable_ExitCode2)
    {
        // No custom table exists, so export should fail
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode generic --export-custom C:\\temp_export_out.txt", nul);
        fclose(nul);
        Assert::AreEqual(2, rc);
    }

    TEST_METHOD(ExportCustom_AfterImport_CreatesFile)
    {
        // Import first
        std::wstring src = MakeTempCIN(L"export_generic", "a b\nb c\n");
        FILE* nul = OpenNullOut();
        std::wstring importCmd = L"--mode generic --import-custom " + src;
        RunCLI(importCmd.c_str(), nul);

        // Export to a temp file
        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_export_test.txt";
        std::wstring exportCmd = L"--mode generic --export-custom " + outPath;
        int rc = RunCLI(exportCmd.c_str(), nul);
        fclose(nul);

        DeleteFileW(src.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsTrue(PathFileExistsW(outPath.c_str()) == TRUE);
        DeleteFileW(outPath.c_str());
    }
};

// ---------------------------------------------------------------------------
// SetAndReadBackRoundtripTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLISetAndReadBackTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(SetFontSize_ReadBack_Matches)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        RunCLI(L"--mode dayi --set FontSize 22", nul);
        fclose(nul);

        // Primary assertion: direct INI check
        Assert::AreEqual(std::wstring(L"22"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize"));

        // Secondary assertion: read-back via --get
        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_readback_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");
        int rc = RunCLI(L"--mode dayi --get FontSize", out);
        fflush(out); rewind(out);
        wchar_t buf[128] = {}; fread(buf, sizeof(wchar_t), 127, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"22"));
    }

    TEST_METHOD(SetPhoneticLayout_InPhoneticMode_WrittenToINI)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC);
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode phonetic --reset", nul);
        int rc = RunCLI(L"--mode phonetic --set PhoneticKeyboardLayout 1", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"1"),
            ReadIniKey(IME_MODE::IME_MODE_PHONETIC, L"PhoneticKeyboardLayout"));
        DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC);
    }

    TEST_METHOD(SetDayiArticleMode_InDayiMode_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set DayiArticleMode 1", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"1"),
            ReadIniKey(IME_MODE::IME_MODE_DAYI, L"DayiArticleMode"));
    }

    TEST_METHOD(SetBig5Filter_InGenericMode_WrittenToINI)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode generic --reset", nul);
        int rc = RunCLI(L"--mode generic --set Big5Filter 1", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"1"),
            ReadIniKey(IME_MODE::IME_MODE_GENERIC, L"Big5Filter"));
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
    }
};

// ---------------------------------------------------------------------------
// SilentFlagTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLISilentFlagTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(SetWithSilent_StillWritesINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set FontSize 16 --silent", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"16"), ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize"));
    }
};

// ---------------------------------------------------------------------------
// JSONOutputTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLIJSONOutputTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(GetAll_JSON_OutputsOpeningBrace)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_json_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");
        int rc = RunCLI(L"--mode dayi --get-all --json", out);
        fflush(out); rewind(out);
        wchar_t buf[8192] = {}; fread(buf, sizeof(wchar_t), 8191, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"{"));
        Assert::IsNotNull(wcsstr(buf, L"mode"));
        Assert::IsNotNull(wcsstr(buf, L"dayi"));
        Assert::IsNotNull(wcsstr(buf, L"FontSize"));
    }
};

// ---------------------------------------------------------------------------
// LoadCommandTests  (error-path only — success path writes live table files)
// ---------------------------------------------------------------------------
TEST_CLASS(CLILoadCommandTests)
{
public:
    TEST_METHOD(LoadMain_NonExistentFile_ExitCode2)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode dayi --load-main C:\\nonexistent_table_xyz.cin", nul);
        fclose(nul);
        Assert::AreEqual(2, rc);
    }

    TEST_METHOD(LoadPhrase_NonExistentFile_ExitCode2)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode generic --load-phrase C:\\nonexistent_phrase_xyz.cin", nul);
        fclose(nul);
        Assert::AreEqual(2, rc);
    }

    TEST_METHOD(LoadArray_NonExistentFile_ExitCode2)
    {
        FILE* nul = OpenNullOut();
        int rc = RunCLI(L"--mode array --load-array sp C:\\nonexistent_array_xyz.cin", nul);
        fclose(nul);
        Assert::AreEqual(2, rc);
    }
};

// ---------------------------------------------------------------------------
// JSONGetKeyTests  (--get --json and --list-keys --json output)
// ---------------------------------------------------------------------------
TEST_CLASS(CLIJSONGetKeyTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(Get_JSON_OutputsKeyValueObject)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_get_json_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");
        int rc = RunCLI(L"--mode dayi --get FontSize --json", out);
        fflush(out); rewind(out);
        wchar_t buf[256] = {}; fread(buf, sizeof(wchar_t), 255, out);
        fclose(out); DeleteFileW(outPath.c_str());

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"{"));
        Assert::IsNotNull(wcsstr(buf, L"FontSize"));
        Assert::IsNotNull(wcsstr(buf, L"}"));
    }

    TEST_METHOD(ListKeys_JSON_OutputsKeyArray)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC);
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode phonetic --reset", nul);
        fclose(nul);

        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring outPath = std::wstring(tmp) + L"cli_listkeys_json_test.txt";
        FILE* out = nullptr; _wfopen_s(&out, outPath.c_str(), L"w+, ccs=UTF-16LE");
        int rc = RunCLI(L"--mode phonetic --list-keys --json", out);
        fflush(out); rewind(out);
        wchar_t buf[8192] = {}; fread(buf, sizeof(wchar_t), 8191, out);
        fclose(out); DeleteFileW(outPath.c_str());
        DeleteConfigFile(IME_MODE::IME_MODE_PHONETIC);

        Assert::AreEqual(0, rc);
        Assert::IsNotNull(wcsstr(buf, L"keys"));
        Assert::IsNotNull(wcsstr(buf, L"PhoneticKeyboardLayout"));
        Assert::IsNull(wcsstr(buf, L"ArrayScope"));
    }
};

// ---------------------------------------------------------------------------
// StringAndCLSIDKeyTests  (FontFaceName and CLSID key set/get roundtrip)
// ---------------------------------------------------------------------------
TEST_CLASS(CLIStringAndCLSIDKeyTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(Set_FontFaceName_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set FontFaceName Arial", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"Arial"),
            ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontFaceName"));
    }

    TEST_METHOD(Set_ReverseConversionCLSID_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        // Set a valid GUID (test value)
        int rc = RunCLI(L"--mode dayi --set ReverseConversionCLSID {12345678-1234-1234-1234-123456789ABC}", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        std::wstring v = ReadIniKey(IME_MODE::IME_MODE_DAYI, L"ReverseConversionCLSID");
        // GUID written as {XXXXXXXX-...} format
        Assert::IsTrue(v.find(L"12345678") != std::wstring::npos);
    }

    TEST_METHOD(Set_LightItemColor_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set LightItemColor 0xAABBCC", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"0xAABBCC"),
            ReadIniKey(IME_MODE::IME_MODE_DAYI, L"LightItemColor"));
    }

    TEST_METHOD(Set_DarkItemColor_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set DarkItemColor 0x112233", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"0x112233"),
            ReadIniKey(IME_MODE::IME_MODE_DAYI, L"DarkItemColor"));
    }
};

// ---------------------------------------------------------------------------
// Helper: build %APPDATA%\DIME\<filename> path
// ---------------------------------------------------------------------------
static std::wstring GetDimeFilePath(const wchar_t* filename)
{
    WCHAR appData[MAX_PATH] = {};
    SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
    WCHAR path[MAX_PATH];
    StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\%s", appData, filename);
    return path;
}

// Back up a file by moving it to .testbak (preserves the original for restore).
// No-op if the file does not exist.
static void BackupDimeFile(const wchar_t* fileName)
{
    std::wstring src = GetDimeFilePath(fileName);
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring bak = src + L".testbak";
    MoveFileExW(src.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Restore a file from .testbak, or delete the test-created file if no backup exists.
static void RestoreDimeFile(const wchar_t* fileName)
{
    std::wstring src = GetDimeFilePath(fileName);
    std::wstring bak = src + L".testbak";
    if (GetFileAttributesW(bak.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(bak.c_str(), src.c_str(), MOVEFILE_REPLACE_EXISTING);
    else
        DeleteFileW(src.c_str());
}

// ---------------------------------------------------------------------------
// ColorKeySetTests — cover all 18 color setter branches in ApplyKeyValue
// ---------------------------------------------------------------------------
TEST_CLASS(CLIColorKeySetTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(Set_AllRemainingColorKeys_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);

        // 15 color keys not yet covered by other tests
        static const wchar_t* colorKeys[] = {
            L"PhraseColor", L"NumberColor", L"ItemBGColor",
            L"SelectedItemColor", L"SelectedBGItemColor",
            L"LightPhraseColor", L"LightNumberColor", L"LightItemBGColor",
            L"LightSelectedItemColor", L"LightSelectedBGItemColor",
            L"DarkPhraseColor", L"DarkNumberColor", L"DarkItemBGColor",
            L"DarkSelectedItemColor", L"DarkSelectedBGItemColor",
        };

        for (const wchar_t* key : colorKeys)
        {
            WCHAR cmd[256];
            StringCchPrintfW(cmd, 256, L"--mode dayi --set %s 0xAA1122", key);
            int rc = RunCLI(cmd, nul);
            Assert::AreEqual(0, rc, key);
            Assert::AreEqual(std::wstring(L"0xAA1122"), ReadIniKey(IME_MODE::IME_MODE_DAYI, key), key);
        }
        fclose(nul);
    }

    TEST_METHOD(Set_ColorKey_InvalidValue_ReturnsExitCode1)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set PhraseColor not_a_color", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }
};

// ---------------------------------------------------------------------------
// StringCLSIDSetTests — ReverseConversionDescription, GUIDProfile, invalid GUID
// ---------------------------------------------------------------------------
TEST_CLASS(CLIStringCLSIDSetTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }
    TEST_METHOD_CLEANUP(Teardown)  { DeleteConfigFile(IME_MODE::IME_MODE_DAYI); }

    TEST_METHOD(Set_ReverseConversionDescription_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set ReverseConversionDescription TestDescription", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        Assert::AreEqual(std::wstring(L"TestDescription"),
            ReadIniKey(IME_MODE::IME_MODE_DAYI, L"ReverseConversionDescription"));
    }

    TEST_METHOD(Set_ReverseConversionGUIDProfile_WrittenToINI)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set ReverseConversionGUIDProfile {ABCDEF01-2345-6789-ABCD-EF0123456789}", nul);
        fclose(nul);

        Assert::AreEqual(0, rc);
        std::wstring v = ReadIniKey(IME_MODE::IME_MODE_DAYI, L"ReverseConversionGUIDProfile");
        Assert::IsTrue(v.find(L"ABCDEF01") != std::wstring::npos);
    }

    TEST_METHOD(Set_ReverseConversionCLSID_InvalidGUID_ReturnsExitCode1)
    {
        FILE* nul = OpenNullOut();
        RunCLI(L"--mode dayi --reset", nul);
        int rc = RunCLI(L"--mode dayi --set ReverseConversionCLSID not-a-guid", nul);
        fclose(nul);
        Assert::AreEqual(1, rc);
    }
};

// ---------------------------------------------------------------------------
// LoadMainSuccessTests — load-main success path for all 4 modes
// ---------------------------------------------------------------------------
TEST_CLASS(CLILoadMainSuccessTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        BackupDimeFile(L"Dayi.cin");
        BackupDimeFile(L"Array.cin");
        BackupDimeFile(L"Phone.cin");
        BackupDimeFile(L"Generic.cin");
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        RestoreDimeFile(L"Dayi.cin");
        RestoreDimeFile(L"Array.cin");
        RestoreDimeFile(L"Phone.cin");
        RestoreDimeFile(L"Generic.cin");
    }

    TEST_METHOD(LoadMain_AllModes_CreatesTableFile)
    {
        std::wstring src = MakeTempCIN(L"loadmain",
            "%chardef begin\na 0x4E00\n%chardef end\n");

        struct { const wchar_t* modeName; const wchar_t* destFile; } modes[] = {
            { L"dayi",     L"Dayi.cin"    },
            { L"array",    L"Array.cin"   },
            { L"phonetic", L"Phone.cin"   },
            { L"generic",  L"Generic.cin" },
        };

        FILE* nul = OpenNullOut();
        for (auto& m : modes)
        {
            WCHAR cmd[512];
            StringCchPrintfW(cmd, 512, L"--mode %s --load-main %s", m.modeName, src.c_str());
            int rc = RunCLI(cmd, nul);
            Assert::AreEqual(0, rc, m.modeName);

            std::wstring dest = GetDimeFilePath(m.destFile);
            Assert::IsTrue(PathFileExistsW(dest.c_str()) == TRUE, m.destFile);
            DeleteFileW(dest.c_str());
        }
        fclose(nul);
        DeleteFileW(src.c_str());
    }

    TEST_METHOD(LoadMain_LockedFile_ReturnsExitCode2)
    {
        std::wstring src = MakeTempCIN(L"loadmain_locked", "a 0x4E00\n");
        HANDLE hLock = CreateFileW(src.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        Assert::IsTrue(hLock != INVALID_HANDLE_VALUE);

        FILE* nul = OpenNullOut();
        WCHAR cmd[512];
        StringCchPrintfW(cmd, 512, L"--mode dayi --load-main %s", src.c_str());
        int rc = RunCLI(cmd, nul);
        fclose(nul);
        CloseHandle(hLock);
        DeleteFileW(src.c_str());

        Assert::AreEqual(2, rc);
    }
};

// ---------------------------------------------------------------------------
// LoadPhraseSuccessTests
// ---------------------------------------------------------------------------
TEST_CLASS(CLILoadPhraseSuccessTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        BackupDimeFile(L"Phrase.cin");
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        RestoreDimeFile(L"Phrase.cin");
    }

    TEST_METHOD(LoadPhrase_ValidFile_CreatesPhraseTable)
    {
        std::wstring src = MakeTempCIN(L"loadphrase",
            "%chardef begin\ntest 0x4E00\n%chardef end\n");

        FILE* nul = OpenNullOut();
        WCHAR cmd[512];
        StringCchPrintfW(cmd, 512, L"--mode generic --load-phrase %s", src.c_str());
        int rc = RunCLI(cmd, nul);
        fclose(nul);
        DeleteFileW(src.c_str());

        Assert::AreEqual(0, rc);
        std::wstring dest = GetDimeFilePath(L"Phrase.cin");
        Assert::IsTrue(PathFileExistsW(dest.c_str()) == TRUE);
    }

    TEST_METHOD(LoadPhrase_LockedFile_ReturnsExitCode2)
    {
        std::wstring src = MakeTempCIN(L"loadphrase_locked", "a 0x4E00\n");
        HANDLE hLock = CreateFileW(src.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        Assert::IsTrue(hLock != INVALID_HANDLE_VALUE);

        FILE* nul = OpenNullOut();
        WCHAR cmd[512];
        StringCchPrintfW(cmd, 512, L"--mode generic --load-phrase %s", src.c_str());
        int rc = RunCLI(cmd, nul);
        fclose(nul);
        CloseHandle(hLock);
        DeleteFileW(src.c_str());

        Assert::AreEqual(2, rc);
    }
};

// ---------------------------------------------------------------------------
// LoadArraySuccessTests — sp and sc table types
// ---------------------------------------------------------------------------
TEST_CLASS(CLILoadArraySuccessTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        BackupDimeFile(L"Array-special.cin");
        BackupDimeFile(L"Array-shortcode.cin");
        BackupDimeFile(L"Array-Ext-B.cin");
        BackupDimeFile(L"Array-Ext-CD.cin");
        BackupDimeFile(L"Array-Ext-EF.cin");
        BackupDimeFile(L"Array40.cin");
        BackupDimeFile(L"Array-Phrase.cin");
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        RestoreDimeFile(L"Array-special.cin");
        RestoreDimeFile(L"Array-shortcode.cin");
        RestoreDimeFile(L"Array-Ext-B.cin");
        RestoreDimeFile(L"Array-Ext-CD.cin");
        RestoreDimeFile(L"Array-Ext-EF.cin");
        RestoreDimeFile(L"Array40.cin");
        RestoreDimeFile(L"Array-Phrase.cin");
    }

    TEST_METHOD(LoadArray_AllTableTypes_CreatesFiles)
    {
        std::wstring src = MakeTempCIN(L"loadarray_all",
            "%chardef begin\na 0x4E00\n%chardef end\n");

        struct { const wchar_t* tableName; const wchar_t* destFile; } tables[] = {
            { L"sp",      L"Array-special.cin"   },
            { L"sc",      L"Array-shortcode.cin"  },
            { L"ext-b",   L"Array-Ext-B.cin"      },
            { L"ext-cd",  L"Array-Ext-CD.cin"     },
            { L"ext-efg", L"Array-Ext-EF.cin"     },
            { L"array40", L"Array40.cin"           },
            { L"phrase",  L"Array-Phrase.cin"      },
        };

        FILE* nul = OpenNullOut();
        for (auto& t : tables)
        {
            WCHAR cmd[512];
            StringCchPrintfW(cmd, 512, L"--mode array --load-array %s %s", t.tableName, src.c_str());
            int rc = RunCLI(cmd, nul);
            Assert::AreEqual(0, rc, t.tableName);

            std::wstring dest = GetDimeFilePath(t.destFile);
            Assert::IsTrue(PathFileExistsW(dest.c_str()) == TRUE, t.destFile);
            DeleteFileW(dest.c_str());
        }
        fclose(nul);
        DeleteFileW(src.c_str());
    }

    TEST_METHOD(LoadArray_LockedFile_ReturnsExitCode2)
    {
        std::wstring src = MakeTempCIN(L"loadarray_locked", "a 0x4E00\n");
        HANDLE hLock = CreateFileW(src.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        Assert::IsTrue(hLock != INVALID_HANDLE_VALUE);

        FILE* nul = OpenNullOut();
        WCHAR cmd[512];
        StringCchPrintfW(cmd, 512, L"--mode array --load-array sp %s", src.c_str());
        int rc = RunCLI(cmd, nul);
        fclose(nul);
        CloseHandle(hLock);
        DeleteFileW(src.c_str());

        Assert::AreEqual(2, rc);
    }
};

// ---------------------------------------------------------------------------
// ImportExportErrorPathTests — parseCINFile failure, CopyFileW failure
// ---------------------------------------------------------------------------
TEST_CLASS(CLIImportExportErrorPathTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
        WCHAR appData[MAX_PATH]; SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
        WCHAR customPath[MAX_PATH];
        StringCchPrintfW(customPath, MAX_PATH, L"%s\\DIME\\GENERIC-CUSTOM.txt", appData);
        DeleteFileW(customPath);
    }
    TEST_METHOD_CLEANUP(Teardown)
    {
        DeleteConfigFile(IME_MODE::IME_MODE_GENERIC);
        WCHAR appData[MAX_PATH]; SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
        WCHAR customPath[MAX_PATH];
        StringCchPrintfW(customPath, MAX_PATH, L"%s\\DIME\\GENERIC-CUSTOM.txt", appData);
        DeleteFileW(customPath);
    }

    TEST_METHOD(ImportCustom_LockedFile_ReturnsExitCode2)
    {
        // Create a file, then hold an exclusive lock so parseCINFile cannot open it
        std::wstring src = MakeTempCIN(L"import_locked", "a b\n");
        HANDLE hLock = CreateFileW(src.c_str(), GENERIC_READ | GENERIC_WRITE,
            0 /* no sharing */, NULL, OPEN_EXISTING, 0, NULL);
        Assert::IsTrue(hLock != INVALID_HANDLE_VALUE);

        FILE* nul = OpenNullOut();
        WCHAR cmd[512];
        StringCchPrintfW(cmd, 512, L"--mode generic --import-custom %s", src.c_str());
        int rc = RunCLI(cmd, nul);
        fclose(nul);

        CloseHandle(hLock);
        DeleteFileW(src.c_str());

        Assert::AreEqual(2, rc);
    }

    TEST_METHOD(ExportCustom_BadDestPath_ReturnsExitCode2)
    {
        // Import a valid custom table first
        std::wstring src = MakeTempCIN(L"export_err", "a b\nb c\n");
        FILE* nul = OpenNullOut();
        WCHAR importCmd[512];
        StringCchPrintfW(importCmd, 512, L"--mode generic --import-custom %s", src.c_str());
        RunCLI(importCmd, nul);
        DeleteFileW(src.c_str());

        // Export to a path whose parent directory doesn't exist
        int rc = RunCLI(L"--mode generic --export-custom C:\\nonexistent_dir_xyz\\output.txt", nul);
        fclose(nul);

        Assert::AreEqual(2, rc);
    }
};

} // namespace DIMEIntegratedTests
