// CINParserTest.cpp - Unit Tests for CConfig::parseCINFile
// Tests CIN dictionary file parsing, encoding conversion, and escape handling

#include "pch.h"
#include "CppUnitTest.h"
#include "Config.h"
#include "Globals.h"
#include <fstream>
#include <vector>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(CINParserTest)
    {
    private:
        std::wstring testDir;
        std::vector<std::wstring> createdFiles;

        // Helper: Create a UTF-8 CIN file (for customTableMode = FALSE)
        void CreateUTF8CINFile(const std::wstring& filename, const std::string& content)
        {
            std::ofstream file(filename, std::ios::binary);
            Assert::IsTrue(file.is_open(), L"Failed to create UTF-8 test file");
            file.write(content.c_str(), content.size());
            file.close();
            createdFiles.push_back(filename);
        }

        // Helper: Create a UTF-16LE CIN file with BOM (for customTableMode = TRUE)
        void CreateUTF16LECINFile(const std::wstring& filename, const std::wstring& content)
        {
            HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE, L"Failed to create UTF-16LE test file");

            DWORD bytesWritten = 0;

            // Write BOM
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hFile, bom, 2, &bytesWritten, nullptr);

            // Write content
            DWORD contentBytes = (DWORD)(content.length() * sizeof(WCHAR));
            WriteFile(hFile, content.c_str(), contentBytes, &bytesWritten, nullptr);

            CloseHandle(hFile);
            createdFiles.push_back(filename);
        }

        // Helper: Read UTF-16LE output file back as wstring
        std::wstring ReadOutputFile(const std::wstring& filename)
        {
            // Open with C runtime in UTF-16LE mode (matches what parseCINFile writes)
            FILE* fp = nullptr;
            errno_t err = _wfopen_s(&fp, filename.c_str(), L"r, ccs=UTF-16LE");
            if (err != 0 || fp == nullptr)
                return L"";

            std::wstring result;
            WCHAR buf[1024];
            while (fgetws(buf, _countof(buf), fp) != NULL)
            {
                result += buf;
            }
            fclose(fp);
            return result;
        }

    public:
        TEST_METHOD_INITIALIZE(MethodInit)
        {
            wchar_t tempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            testDir = std::wstring(tempPath) + L"DIMETests_CINParser\\";
            CreateDirectoryW(testDir.c_str(), nullptr);
            createdFiles.clear();
        }

        TEST_METHOD_CLEANUP(MethodCleanup)
        {
            for (const auto& file : createdFiles)
            {
                DeleteFileW(file.c_str());
            }
            RemoveDirectoryW(testDir.c_str());
        }

        // UT-07-01: Basic tab-separated key-value parsing
        TEST_METHOD(ParseCIN_SimpleKeyValue_TabSeparated)
        {
            std::wstring inputFile = testDir + L"tab_sep.cin";
            std::wstring outputFile = testDir + L"tab_sep_out.cin";
            createdFiles.push_back(outputFile);

            // key<TAB>value format
            CreateUTF8CINFile(inputFile, "a\t\xe4\xb8\xad\n" "b\t\xe6\x96\x87\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // Outside escape sections, key-value should be written with tab separator
            Assert::IsTrue(output.find(L"a\t") != std::wstring::npos, L"Key 'a' should be in output");
            Assert::IsTrue(output.find(L"b\t") != std::wstring::npos, L"Key 'b' should be in output");
        }

        // UT-07-02: Space-separated key-value parsing
        TEST_METHOD(ParseCIN_SimpleKeyValue_SpaceSeparated)
        {
            std::wstring inputFile = testDir + L"space_sep.cin";
            std::wstring outputFile = testDir + L"space_sep_out.cin";
            createdFiles.push_back(outputFile);

            // key<SPACE>value format
            CreateUTF8CINFile(inputFile, "a \xe4\xb8\xad\n" "b \xe6\x96\x87\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            Assert::IsTrue(output.find(L"a\t") != std::wstring::npos, L"Key 'a' should be in output with tab");
            Assert::IsTrue(output.find(L"b\t") != std::wstring::npos, L"Key 'b' should be in output with tab");
        }

        // UT-07-03: Escape handling inside %chardef section
        TEST_METHOD(ParseCIN_ChardefEscape_QuotesAndBackslashes)
        {
            std::wstring inputFile = testDir + L"escape.cin";
            std::wstring outputFile = testDir + L"escape_out.cin";
            createdFiles.push_back(outputFile);

            // Content with %chardef begin/end and special characters in both keys and values
            std::string content =
                "%chardef begin\n"
                "a\t\xe4\xb8\xad\n"           // a → 中 (no special chars)
                "\"\t\xe6\x96\x87\n"           // " → 文 (key has quote)
                "\\\t\xe6\x96\x87\n"           // \ → 文 (key has backslash)
                "b\t\"\n"                      // b → " (value has quote)
                "c\t\\\n"                      // c → \ (value has backslash)
                "%chardef end\n";

            CreateUTF8CINFile(inputFile, content);

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // Inside escape section, keys/values should be quoted and escaped
            // Key-side escaping
            Assert::IsTrue(output.find(L"\"a\"") != std::wstring::npos, L"Key 'a' should be quoted");
            Assert::IsTrue(output.find(L"\"\\\"\"") != std::wstring::npos, L"Quote key should be escaped as \\\"");
            Assert::IsTrue(output.find(L"\"\\\\\"") != std::wstring::npos, L"Backslash key should be escaped as \\\\");
            // Value-side escaping: b→" should produce "b"\t"\"", c→\ should produce "c"\t"\\"
            // Search for the escaped value patterns in the output
            std::wstring bLine = L"\"b\"\t\"\\\"\"";   // "b"<TAB>"\""
            std::wstring cLine = L"\"c\"\t\"\\\\\"";   // "c"<TAB>"\\"
            Assert::IsTrue(output.find(bLine) != std::wstring::npos, L"Value quote should be escaped as \\\"");
            Assert::IsTrue(output.find(cLine) != std::wstring::npos, L"Value backslash should be escaped as \\\\");
        }

        // UT-07-04: %keyname section also triggers escape mode
        TEST_METHOD(ParseCIN_KeynameSection_Escaped)
        {
            std::wstring inputFile = testDir + L"keyname.cin";
            std::wstring outputFile = testDir + L"keyname_out.cin";
            createdFiles.push_back(outputFile);

            std::string content =
                "%keyname begin\n"
                "a\ttest\n"
                "%keyname end\n";

            CreateUTF8CINFile(inputFile, content);

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // Inside %keyname section, values should be quoted
            Assert::IsTrue(output.find(L"\"a\"") != std::wstring::npos, L"Key should be quoted inside %keyname section");
            Assert::IsTrue(output.find(L"\"test\"") != std::wstring::npos, L"Value should be quoted inside %keyname section");
        }

        // UT-07-05: %encoding directive converted to UTF-16LE
        TEST_METHOD(ParseCIN_EncodingDirective_ConvertedToUTF16LE)
        {
            std::wstring inputFile = testDir + L"encoding.cin";
            std::wstring outputFile = testDir + L"encoding_out.cin";
            createdFiles.push_back(outputFile);

            CreateUTF8CINFile(inputFile, "%encoding UTF-8\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            Assert::IsTrue(output.find(L"%encoding\tUTF-16LE") != std::wstring::npos,
                L"Encoding should be converted to UTF-16LE");
        }

        // UT-07-06: Unparsable lines written as-is outside escape sections
        TEST_METHOD(ParseCIN_UnparsableLines_WrittenAsIs)
        {
            std::wstring inputFile = testDir + L"unparsable.cin";
            std::wstring outputFile = testDir + L"unparsable_out.cin";
            createdFiles.push_back(outputFile);

            // Lines that don't match any key-value pattern (e.g., comments, headers)
            CreateUTF8CINFile(inputFile, "%gen_inp\n" "#comment line\n" "a\tvalue\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            Assert::IsTrue(output.find(L"%gen_inp") != std::wstring::npos,
                L"Single-token line should be written as-is");
        }

        // UT-07-07: Unparsable lines inside escape sections are skipped
        TEST_METHOD(ParseCIN_UnparsableLinesInEscape_Skipped)
        {
            std::wstring inputFile = testDir + L"skip_escape.cin";
            std::wstring outputFile = testDir + L"skip_escape_out.cin";
            createdFiles.push_back(outputFile);

            std::string content =
                "%chardef begin\n"
                "unparsable_no_separator\n"    // single token, no whitespace — truly unparsable
                "a\tvalue\n"
                "%chardef end\n";

            CreateUTF8CINFile(inputFile, content);

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // The unparsable line should NOT appear in output (skipped in escape mode)
            // But %chardef begin/end and the key-value should be present
            Assert::IsTrue(output.find(L"%chardef begin") != std::wstring::npos, L"chardef begin should be present");
            Assert::IsTrue(output.find(L"%chardef end") != std::wstring::npos, L"chardef end should be present");
            Assert::IsTrue(output.find(L"\"a\"") != std::wstring::npos, L"Key 'a' should be escaped and present");
            Assert::IsTrue(output.find(L"unparsable_no_separator") == std::wstring::npos,
                L"Unparsable line inside escape section should be skipped");
        }

        // UT-07-08: customTableMode wraps output with %chardef begin/end
        TEST_METHOD(ParseCIN_CustomTableMode_WrapsChardef)
        {
            std::wstring inputFile = testDir + L"custom.cin";
            std::wstring outputFile = testDir + L"custom_out.cin";
            createdFiles.push_back(outputFile);

            // Create UTF-16LE input (customTableMode reads UTF-16LE)
            CreateUTF16LECINFile(inputFile, L"a\t\x4E2D\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), TRUE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // customTableMode should add %chardef begin at start and %chardef end at end
            Assert::IsTrue(output.find(L"%chardef begin") != std::wstring::npos,
                L"Custom table mode should add %chardef begin");
            Assert::IsTrue(output.find(L"%chardef end") != std::wstring::npos,
                L"Custom table mode should add %chardef end");
        }

        // UT-07-09: Pattern 3 - value with spaces parsed correctly
        TEST_METHOD(ParseCIN_ValueWithSpaces_Pattern3)
        {
            std::wstring inputFile = testDir + L"value_spaces.cin";
            std::wstring outputFile = testDir + L"value_spaces_out.cin";
            createdFiles.push_back(outputFile);

            // Use a non-% key so patterns 1 and 2 fail (they require no extra tokens),
            // forcing pattern 3: %[^ \t\r\n] %[^\t\r\n] → key(no spaces) + value(with spaces)
            // "mykey hello world here" → pattern 2 returns 4 (!=3), pattern 3 captures value with spaces
            CreateUTF8CINFile(inputFile, "mykey hello world here\n");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            Assert::IsTrue(output.find(L"mykey") != std::wstring::npos,
                L"Key should be present in output");
            Assert::IsTrue(output.find(L"hello world here") != std::wstring::npos,
                L"Value with spaces should be preserved via pattern 3");
        }

        // UT-07-10: Empty input file produces empty output
        TEST_METHOD(ParseCIN_EmptyFile_ProducesEmptyOutput)
        {
            std::wstring inputFile = testDir + L"empty.cin";
            std::wstring outputFile = testDir + L"empty_out.cin";
            createdFiles.push_back(outputFile);

            CreateUTF8CINFile(inputFile, "");

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE for empty file");

            std::wstring output = ReadOutputFile(outputFile);
            Assert::IsTrue(output.empty(), L"Empty input should produce empty output");
        }

        // UT-07-11: Already-quoted entries should not be double-escaped
        TEST_METHOD(ParseCIN_AlreadyEscaped_NoDoubleEscape)
        {
            std::wstring inputFile = testDir + L"already_escaped.cin";
            std::wstring outputFile = testDir + L"already_escaped_out.cin";
            // Ensure no stale output from a prior run (can happen under coverage tools)
            DeleteFileW(outputFile.c_str());
            createdFiles.push_back(outputFile);

            // Input CIN file where %chardef entries are already quoted and escaped
            // UTF-8 bytes: ﹨=U+FE68=\xef\xb9\xa8, 中=U+4E2D=\xe4\xb8\xad, 文=U+6587=\xe6\x96\x87
            std::string content =
                "%chardef begin\n"
                "\"=\\\\\"\t\"\xef\xb9\xa8\"\n"    // "=\\"	"﹨" (already quoted, backslash escaped)
                "\"a\"\t\"\xe4\xb8\xad\"\n"         // "a"	"中" (already quoted, no special chars)
                "b\t\xe6\x96\x87\n"                  // b	文 (unquoted — should still be escaped)
                "%chardef end\n";

            CreateUTF8CINFile(inputFile, content);

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);

            // Already-quoted entries should pass through unchanged (no double-escaping)
            Assert::IsTrue(output.find(L"\"=\\\\\"") != std::wstring::npos,
                L"Already-escaped key =\\\\ should not be double-escaped");
            Assert::IsTrue(output.find(L"\"a\"\t\"\x4E2D\"") != std::wstring::npos,
                L"Already-quoted simple entry should pass through unchanged");

            // Unquoted entry should still be escaped normally
            Assert::IsTrue(output.find(L"\"b\"\t\"\x6587\"") != std::wstring::npos,
                L"Unquoted entry 'b' should still be escaped and quoted");
        }

        // UT-07-12: Escape mode toggling - doEscape resets after %chardef end
        TEST_METHOD(ParseCIN_EscapeModeToggles_Correctly)
        {
            std::wstring inputFile = testDir + L"toggle.cin";
            std::wstring outputFile = testDir + L"toggle_out.cin";
            createdFiles.push_back(outputFile);

            std::string content =
                "header\tvalue1\n"             // outside escape: plain output
                "%chardef begin\n"
                "a\tvalue2\n"                  // inside escape: quoted output
                "%chardef end\n"
                "footer\tvalue3\n";            // outside escape again: plain output

            CreateUTF8CINFile(inputFile, content);

            BOOL result = CConfig::parseCINFile(inputFile.c_str(), outputFile.c_str(), FALSE);
            Assert::IsTrue(result, L"parseCINFile should return TRUE");

            std::wstring output = ReadOutputFile(outputFile);
            // Before chardef: plain
            Assert::IsTrue(output.find(L"header\tvalue1") != std::wstring::npos,
                L"Pre-chardef line should be plain");
            // Inside chardef: quoted
            Assert::IsTrue(output.find(L"\"a\"") != std::wstring::npos,
                L"Inside chardef, key should be quoted");
            // After chardef end: plain again
            Assert::IsTrue(output.find(L"footer\tvalue3") != std::wstring::npos,
                L"Post-chardef line should be plain again");
        }
    };
}
