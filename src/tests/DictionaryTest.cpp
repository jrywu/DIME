#include "pch.h"
#include "CppUnitTest.h"
#include "TableDictionaryEngine.h"
#include "DictionarySearch.h"
#include "File.h"
#include "Config.h"
#include "Globals.h"
#include "BaseStructure.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMETests
{
    TEST_CLASS(DictionaryTest)
    {
    private:
        // Helper to get test data directory
        static std::wstring GetTestDataDir()
        {
            WCHAR modulePath[MAX_PATH];
            // Get the test DLL's location, not the test runner
            HMODULE hModule = NULL;
            GetModuleHandleEx(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)&GetTestDataDir,
                &hModule);
            GetModuleFileName(hModule, modulePath, MAX_PATH);

            std::wstring path = modulePath;
            size_t lastSlash = path.find_last_of(L"\\");
            path = path.substr(0, lastSlash);
            path += L"\\TestData\\";

            // Create directory if it doesn't exist
            CreateDirectory(path.c_str(), NULL);

            // Log the path for debugging
            WCHAR msg[512];
            swprintf_s(msg, 512, L"Test data directory: %s", path.c_str());
            Logger::WriteMessage(msg);

            return path;
        }

        // Helper to create test CIN file
        static std::wstring CreateTestCINFile(const std::wstring& filename, const std::wstring& content)
        {
            std::wstring fullPath = GetTestDataDir() + filename;

            // Delete existing file first
            DeleteFile(fullPath.c_str());

            // Write with BOM for UTF-16LE
            HANDLE hFile = ::CreateFile(fullPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                WCHAR msg[512];
                swprintf_s(msg, 512, L"Failed to create file: %s, error: %d", fullPath.c_str(), GetLastError());
                Logger::WriteMessage(msg);
                return L"";
            }

            // Write UTF-16LE BOM
            WORD bom = 0xFEFF;
            DWORD written;
            WriteFile(hFile, &bom, sizeof(bom), &written, NULL);

            // Write content
            WriteFile(hFile, content.c_str(), (DWORD)(content.length() * sizeof(WCHAR)), &written, NULL);

            CloseHandle(hFile);

            // Verify file was created and has content
            DWORD attrs = GetFileAttributes(fullPath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage("Failed to create test file!");
                return L"";
            }

            return fullPath;
        }

        // Helper to delete test file
        static void DeleteTestFile(const std::wstring& filename)
        {
            std::wstring fullPath = GetTestDataDir() + filename;
            DeleteFile(fullPath.c_str());
        }

        // Helper to check if candidate list contains a specific string
        static BOOL ContainsCandidate(CDIMEArray<CCandidateListItem>* pCandidates, const std::wstring& text)
        {
            for (UINT i = 0; i < pCandidates->Count(); i++)
            {
                CCandidateListItem* pItem = pCandidates->GetAt(i);
                if (pItem && pItem->_ItemString.Get())
                {
                    std::wstring candidateText(pItem->_ItemString.Get(), pItem->_ItemString.GetLength());
                    if (candidateText == text)
                    {
                        return TRUE;
                    }
                }
            }
            return FALSE;
        }

        // Helper to load a dictionary engine
        static CTableDictionaryEngine* LoadDictionaryEngine(const std::wstring& cinFilePath, IME_MODE imeMode = IME_MODE::IME_MODE_ARRAY)
        {
            // Check if file exists first
            DWORD attrs = GetFileAttributes(cinFilePath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
            {
                WCHAR msg[512];
                swprintf_s(msg, 512, L"Test file does not exist: %s", cinFilePath.c_str());
                Logger::WriteMessage(msg);
                return nullptr;
            }

            CFile* pFile = new CFile();
            BOOL result = pFile->CreateFile(
                cinFilePath.c_str(),
                GENERIC_READ,
                OPEN_EXISTING,
                FILE_SHARE_READ
            );

            if (!result)
            {
                WCHAR msg[512];
                DWORD error = GetLastError();
                swprintf_s(msg, 512, L"CFile::CreateFile failed for %s, error: %d", cinFilePath.c_str(), error);
                Logger::WriteMessage(msg);
                delete pFile;
                return nullptr;
            }

            CTableDictionaryEngine* engine = new CTableDictionaryEngine(
                MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT),
                pFile,
                DICTIONARY_TYPE::CIN_DICTIONARY
            );

            // Parse the config to initialize the engine properly
            try
            {
                engine->ParseConfig(imeMode);
            }
            catch (...)
            {
                Logger::WriteMessage("ParseConfig threw an exception!");
                delete engine;
                return nullptr;
            }

            return engine;
        }

    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            CoInitialize(NULL);
            
            // Ensure test data directory exists
            std::wstring testDataDir = GetTestDataDir();
            CreateDirectory(testDataDir.c_str(), NULL);
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            CoUninitialize();
        }

        TEST_METHOD_INITIALIZE(TestSetup)
        {
            // Clean up any leftover test files
            DeleteTestFile(L"test_simple.cin");
            DeleteTestFile(L"test_escape.cin");
            DeleteTestFile(L"test_array.cin");
            DeleteTestFile(L"test_custom.cin");
            DeleteTestFile(L"CUSTOM.txt");
            DeleteTestFile(L"CUSTOM.cin");
            DeleteTestFile(L"source_utf8.cin");
            DeleteTestFile(L"output_utf16.cin");
        }

        // ===== UT-02-01: CIN File Parsing =====

        TEST_METHOD(ParseValidCINFile)
        {
            // Arrange: Create a simple test CIN file
            // Using unicode escapes: \u554A=啊, \u963F=阿, \u611B=愛
            // Note: CIN format requires quotes around keys and values
            std::wstring cinContent =
                L"%gen_inp\n"
                L"%ename Array\n"
                L"%cname \u884C\u5217\n"  // 行列
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\"a\"\t\"\u554A\"\n"  // 啊
                L"\"a\"\t\"\u963F\"\n"  // 阿
                L"\"ai\"\t\"\u611B\"\n"  // 愛
                L"%chardef end\n";

            std::wstring cinPath = CreateTestCINFile(L"test_simple.cin", cinContent);

            // Debug: Check if file was created
            if (cinPath.empty())
            {
                Assert::Fail(L"Failed to create test CIN file");
                return;
            }

            // Check file size
            HANDLE hFile = ::CreateFile(cinPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                WCHAR msg[512];
                swprintf_s(msg, 512, L"Cannot open test file, error: %d", GetLastError());
                Assert::Fail(msg);
                return;
            }
            DWORD fileSize = ::GetFileSize(hFile, NULL);
            CloseHandle(hFile);

            WCHAR sizeMsg[256];
            swprintf_s(sizeMsg, 256, L"Test file size: %d bytes", fileSize);
            Logger::WriteMessage(sizeMsg);

            if (fileSize < 4)
            {
                Assert::Fail(L"Test file is too small, likely not written correctly");
                return;
            }

            // Act: Load and parse the file
            CTableDictionaryEngine* engine = LoadDictionaryEngine(cinPath);

            // Assert: Engine should be created successfully
            Assert::IsNotNull(engine, L"Dictionary engine should be created");
            Assert::AreEqual(
                static_cast<int>(DICTIONARY_TYPE::CIN_DICTIONARY),
                static_cast<int>(engine->GetDictionaryType()),
                L"Dictionary type should be CIN"
            );

            // Test search functionality
            CDIMEArray<CCandidateListItem> candidates;
            CStringRange keyCode;
            keyCode.Set(L"a", 1);
            engine->CollectWord(&keyCode, &candidates);

            WCHAR candidateMsg[256];
            swprintf_s(candidateMsg, 256, L"Found %d candidates for 'a'", candidates.Count());
            Logger::WriteMessage(candidateMsg);

            // Should have at least 2 candidates
            Assert::IsTrue(candidates.Count() >= 2, L"Should have at least 2 candidates for 'a'");

            // Cleanup
            delete engine;
        }

        TEST_METHOD(ParseCINFileWithEscapeCharacters)
        {
            // Arrange: Create CIN file with escape characters
            std::wstring cinContent =
                L"%gen_inp\n"
                L"%ename Test\n"
                L"%cname Test\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\"test\\\\\"\t\"Backslash\"\n"  // test\\ -> backslash
                L"\"quote\\\"\"\t\"Quote\"\n"     // quote\" -> quote
                L"%chardef end\n";
            
            std::wstring cinPath = CreateTestCINFile(L"test_escape.cin", cinContent);
            
            // Act: Load the file
            CTableDictionaryEngine* engine = LoadDictionaryEngine(cinPath);
            
            // Assert: Engine should handle escape characters
            Assert::IsNotNull(engine, L"Dictionary engine should handle escape characters");
            
            // Cleanup
            delete engine;
        }

        // ===== UT-02-02: Radical Search =====

        TEST_METHOD(SearchSingleRadical)
        {
            // Arrange: Create a minimal Array dictionary
            // \u706B=火
            std::wstring cinContent =
                L"%gen_inp\n"
                L"%ename Array\n"
                L"%cname Array30\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\",\"\t\"\u706B\"\n"  // 火
                L"\",,\"\t\"\u708E\"\n"  // 炎
                L"%chardef end\n";
            
            std::wstring cinPath = CreateTestCINFile(L"test_array.cin", cinContent);
            
            // Act: Load dictionary and search
            CTableDictionaryEngine* engine = LoadDictionaryEngine(cinPath);
            Assert::IsNotNull(engine, L"Engine should be created");
            
            CDIMEArray<CCandidateListItem> candidates;
            CStringRange keyCode;
            keyCode.Set(L",", 1);
            engine->CollectWord(&keyCode, &candidates);
            
            // Assert: Should find "fire" radical
            Assert::IsTrue(candidates.Count() > 0, L"Should find candidates for single radical");
            
            delete engine;
        }

        TEST_METHOD(SearchMultipleRadicals)
        {
            // Arrange: Create Array dictionary with multi-radical characters
            // \u706B=火, \u708E=炎, \u7131=焱
            std::wstring cinContent =
                L"%gen_inp\n"
                L"%ename Array\n"
                L"%cname Array30\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\",\"\t\"\u706B\"\n"   // 火
                L"\",,\"\t\"\u708E\"\n"  // 炎
                L"\",,,\"\t\"\u7131\"\n" // 焱
                L"%chardef end\n";
            
            std::wstring cinPath = CreateTestCINFile(L"test_array.cin", cinContent);
            
            // Act: Search for double radicals
            CTableDictionaryEngine* engine = LoadDictionaryEngine(cinPath);
            Assert::IsNotNull(engine, L"Engine should be created");
            
            CDIMEArray<CCandidateListItem> candidates;
            CStringRange keyCode;
            keyCode.Set(L",,", 2);
            engine->CollectWord(&keyCode, &candidates);
            
            // Assert: Should find "flame" character
            Assert::IsTrue(candidates.Count() > 0, L"Should find candidates for double radicals");
            
            // Test triple radicals
            CDIMEArray<CCandidateListItem> candidates2;
            CStringRange keyCode2;
            keyCode2.Set(L",,,", 3);
            engine->CollectWord(&keyCode2, &candidates2);
            
            Assert::IsTrue(candidates2.Count() > 0, L"Should find candidates for triple radicals");
            
            delete engine;
        }

        TEST_METHOD(SearchInvalidRadical)
        {
            // Arrange: Create dictionary
            std::wstring cinContent =
                L"%gen_inp\n"
                L"%ename Array\n"
                L"%cname Array30\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\"a\"\t\"A\"\n"
                L"%chardef end\n";
            
            std::wstring cinPath = CreateTestCINFile(L"test_array.cin", cinContent);
            
            // Act: Search for non-existent radical combination
            CTableDictionaryEngine* engine = LoadDictionaryEngine(cinPath);
            Assert::IsNotNull(engine, L"Engine should be created");
            
            CDIMEArray<CCandidateListItem> candidates;
            CStringRange keyCode;
            keyCode.Set(L"zzzz", 4);
            engine->CollectWord(&keyCode, &candidates);
            
            // Assert: Should return empty results without crashing
            Assert::AreEqual((UINT)0, candidates.Count(), L"Should return empty for invalid radicals");
            
            delete engine;
        }

        // ===== UT-02-03: Custom Table Tests =====

        TEST_METHOD(CustomTablePriority)
        {
            // TODO: This test requires TableDictionaryEngine to support loading both
            // main dictionary and custom table, and CConfig::GetCustomTablePriority()
            // For now, just verify the concept works with a custom table

            // Arrange: Create a custom table
            std::wstring customContent =
                L"%gen_inp\n"
                L"%ename CustomTable\n"
                L"%cname 自訂詞庫\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"\"abc\"\t\"自訂詞彙\"\n"
                L"\"test\"\t\"測試詞\"\n"
                L"%chardef end\n";

            std::wstring cinPath = CreateTestCINFile(L"test_custom.cin", customContent);

            // Act: Load custom table
            CTableDictionaryEngine* customEngine = LoadDictionaryEngine(cinPath);
            Assert::IsNotNull(customEngine, L"Custom engine should be created");

            // Search for custom entry
            CDIMEArray<CCandidateListItem> candidates;
            CStringRange keyCode;
            keyCode.Set(L"abc", 3);
            customEngine->CollectWord(&keyCode, &candidates);

            // Assert: Should find custom entry
            Assert::IsTrue(candidates.Count() > 0, L"Should find custom table entry");

            // Verify the candidate contains our custom text
            if (candidates.Count() > 0)
            {
                CCandidateListItem* pItem = candidates.GetAt(0);
                std::wstring candidateText(pItem->_ItemString.Get(), pItem->_ItemString.GetLength());
                Assert::AreEqual(std::wstring(L"自訂詞彙"), candidateText, L"Should match custom entry");
            }

            delete customEngine;
        }

        TEST_METHOD(CustomTableConversion)
        {
            // Test CConfig::parseCINFile() with customTableMode=TRUE
            // This converts a simple text file (CUSTOM.txt) to CIN format (CUSTOM.cin)
            // for user-defined custom dictionary entries

            Logger::WriteMessage("Testing customTableMode=TRUE (Custom User Table)");

            // Arrange: Prepare custom table in simple text format (UTF-16LE)
            std::wstring customText = 
                L"abc\t測試詞彙\n"
                L"xyz\t自訂字詞\n"
                L"test\t轉換測試\n";

            std::wstring txtPath = GetTestDataDir() + L"CUSTOM.txt";
            std::wstring cinPath = GetTestDataDir() + L"CUSTOM.cin";

            DeleteFile(txtPath.c_str());
            DeleteFile(cinPath.c_str());

            // Create .txt file with UTF-16LE encoding
            HANDLE hFile = CreateFile(txtPath.c_str(), GENERIC_WRITE, 0, NULL, 
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            Assert::AreNotEqual(INVALID_HANDLE_VALUE, hFile, L"Should create .txt file");

            if (hFile != INVALID_HANDLE_VALUE)
            {
                WORD bom = 0xFEFF;
                DWORD written;
                WriteFile(hFile, &bom, sizeof(bom), &written, NULL);
                WriteFile(hFile, customText.c_str(), 
                    (DWORD)(customText.length() * sizeof(WCHAR)), &written, NULL);
                CloseHandle(hFile);
            }

            // Act: Convert with customTableMode=TRUE
            BOOL result = CConfig::parseCINFile(txtPath.c_str(), cinPath.c_str(), TRUE);

            // Assert: Verify conversion succeeded
            Assert::IsTrue(result, L"Custom table conversion should succeed");
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, GetFileAttributes(cinPath.c_str()), 
                L"Output .cin file should exist");

            // Verify line-by-line correspondence
            CFile* pFile = new CFile();
            BOOL fileOpened = pFile->CreateFile(cinPath.c_str(), GENERIC_READ, 
                OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(fileOpened, L"Output file should be readable");

            if (fileOpened)
            {
                const WCHAR* content = pFile->GetReadBufferPointer();
                Assert::IsNotNull(content, L"File content should be readable");
                std::wstring cinContent(content);

                // Verify structure (customTableMode adds %chardef wrapper)
                Assert::IsTrue(cinContent.find(L"%chardef begin") != std::wstring::npos, 
                    L"Should have %chardef begin wrapper");
                Assert::IsTrue(cinContent.find(L"%chardef end") != std::wstring::npos, 
                    L"Should have %chardef end wrapper");

                // Verify ALL source entries are preserved in output
                Assert::IsTrue(cinContent.find(L"abc") != std::wstring::npos && 
                               cinContent.find(L"測試詞彙") != std::wstring::npos, 
                    L"Entry 1: abc → 測試詞彙 should be preserved");
                Assert::IsTrue(cinContent.find(L"xyz") != std::wstring::npos && 
                               cinContent.find(L"自訂字詞") != std::wstring::npos, 
                    L"Entry 2: xyz → 自訂字詞 should be preserved");
                Assert::IsTrue(cinContent.find(L"test") != std::wstring::npos && 
                               cinContent.find(L"轉換測試") != std::wstring::npos, 
                    L"Entry 3: test → 轉換測試 should be preserved");

                delete pFile;
            }

            // Cleanup
            DeleteFile(txtPath.c_str());
            DeleteFile(cinPath.c_str());
        }

        TEST_METHOD(CINFormatConversion)
        {
            // Test CConfig::parseCINFile() with customTableMode=FALSE
            // This converts UTF-8 encoded CIN files to UTF-16LE format
            // for standard dictionary processing

            Logger::WriteMessage("Testing customTableMode=FALSE (CIN Format Conversion UTF-8 → UTF-16LE)");

            // Arrange: Prepare UTF-8 encoded CIN file
            // Note: In CIN parsing mode (customTableMode=FALSE), parseCINFile expects
            // unquoted entries and will add quotes and proper escaping
            std::wstring utf8Content = 
                L"%gen_inp\n"
                L"%ename TestDict\n"
                L"%cname 測試字典\n"
                L"%encoding UTF-8\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"a\t啊\n"
                L"a\t阿\n"
                L"ai\t愛\n"
                L"%chardef end\n";

            std::wstring srcPath = GetTestDataDir() + L"source_utf8.cin";
            std::wstring dstPath = GetTestDataDir() + L"output_utf16.cin";

            DeleteFile(srcPath.c_str());
            DeleteFile(dstPath.c_str());

            // Create UTF-8 file using _wfopen_s with "w, ccs=UTF-8" mode
            // This ensures the file is created in a format readable by "r, ccs=UTF-8"
            FILE* utf8File = nullptr;
            errno_t err = _wfopen_s(&utf8File, srcPath.c_str(), L"w, ccs=UTF-8");
            Assert::AreEqual(0, (int)err, L"Should create UTF-8 source file");
            Assert::IsNotNull(utf8File, L"UTF-8 file handle should be valid");

            if (utf8File)
            {
                fwprintf(utf8File, L"%s", utf8Content.c_str());
                fclose(utf8File);
            }

            // Verify source file exists
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, GetFileAttributes(srcPath.c_str()), 
                L"Source UTF-8 file should exist");

            // Act: Convert with customTableMode=FALSE (CIN parsing mode)
            BOOL result = CConfig::parseCINFile(srcPath.c_str(), dstPath.c_str(), FALSE);

            // Assert: Verify conversion succeeded
            Assert::IsTrue(result, L"CIN format conversion should succeed");
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, GetFileAttributes(dstPath.c_str()), 
                L"Output UTF-16LE file should exist");

            // Verify output file has correct encoding and content
            CFile* pFile = new CFile();
            BOOL fileOpened = pFile->CreateFile(dstPath.c_str(), GENERIC_READ, 
                OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(fileOpened, L"Output file should be readable");

            if (fileOpened)
            {
                const WCHAR* content = pFile->GetReadBufferPointer();
                Assert::IsNotNull(content, L"File content should be readable");
                std::wstring dstContent(content);

                // Verify %encoding is overridden to UTF-16LE (only if source had %encoding line)
                // Note: parseCINFile outputs "%encoding\tUTF-16LE" with tab separator
                Assert::IsTrue(dstContent.find(L"%encoding") != std::wstring::npos && 
                               dstContent.find(L"UTF-16LE") != std::wstring::npos, 
                    L"Output should have %encoding UTF-16LE");
                Assert::IsTrue(dstContent.find(L"UTF-8") == std::wstring::npos, 
                    L"Output should NOT contain UTF-8");

                // Verify entries are preserved with proper quoting
                Assert::IsTrue(dstContent.find(L"\"a\"") != std::wstring::npos && 
                               dstContent.find(L"\"啊\"") != std::wstring::npos, 
                    L"Entry 1: a → 啊 should be preserved with quotes");
                Assert::IsTrue(dstContent.find(L"\"a\"") != std::wstring::npos && 
                               dstContent.find(L"\"阿\"") != std::wstring::npos, 
                    L"Entry 2: a → 阿 should be preserved with quotes");
                Assert::IsTrue(dstContent.find(L"\"ai\"") != std::wstring::npos && 
                               dstContent.find(L"\"愛\"") != std::wstring::npos, 
                    L"Entry 3: ai → 愛 should be preserved with quotes");

                delete pFile;
            }

            // Cleanup
            DeleteFile(srcPath.c_str());
            DeleteFile(dstPath.c_str());
        }
    };
}

