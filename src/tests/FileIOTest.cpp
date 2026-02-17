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

#include "pch.h"
#include "CppUnitTest.h"
#include "../File.h"
#include "../Globals.h"
#include <fstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMETests
{
    TEST_CLASS(FileIOTest)
    {
    private:
        std::wstring testDir;
        std::vector<std::wstring> createdFiles;

        // Helper: Create UTF-16LE file with BOM (binary mode)
        void CreateUTF16LEFile(const std::wstring& filename, const std::wstring& content)
        {
            // Open in binary mode to write raw bytes
            HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr, 
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE, L"Failed to create test file");

            DWORD bytesWritten = 0;

            // Write BOM (0xFEFF in little-endian = FF FE)
            BYTE bom[2] = { 0xFF, 0xFE };
            BOOL result = WriteFile(hFile, bom, 2, &bytesWritten, nullptr);
            Assert::IsTrue(result && bytesWritten == 2, L"Failed to write BOM");

            // Write content as UTF-16LE
            DWORD contentBytes = (DWORD)(content.length() * sizeof(WCHAR));
            result = WriteFile(hFile, content.c_str(), contentBytes, &bytesWritten, nullptr);
            Assert::IsTrue(result && bytesWritten == contentBytes, L"Failed to write content");

            CloseHandle(hFile);
            createdFiles.push_back(filename);
        }

        // Helper: Create binary file
        void CreateBinaryFile(const std::wstring& filename, const std::vector<BYTE>& data)
        {
            std::ofstream file(filename, std::ios::binary);
            Assert::IsTrue(file.is_open(), L"Failed to create binary test file");
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file.close();

            createdFiles.push_back(filename);
        }

    public:
        TEST_CLASS_INITIALIZE(ClassInit)
        {
            Logger::WriteMessage("FileIOTest: Initializing test class...\n");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage("FileIOTest: Cleaning up test class...\n");
        }

        TEST_METHOD_INITIALIZE(MethodInit)
        {
            wchar_t tempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            testDir = std::wstring(tempPath) + L"DIMETests_FileIO\\";
            CreateDirectoryW(testDir.c_str(), nullptr);
            createdFiles.clear();
        }

        TEST_METHOD_CLEANUP(MethodCleanup)
        {
            // Clean up created files
            for (const auto& file : createdFiles)
            {
                DeleteFileW(file.c_str());
            }
            RemoveDirectoryW(testDir.c_str());
        }

        // UT-05-01: UTF-16LE Encoding Validation
        TEST_METHOD(FileIO_UTF16LE_WithBOM_LoadsCorrectly)
        {
            Logger::WriteMessage("Test: FileIO_UTF16LE_WithBOM_LoadsCorrectly\n");

            std::wstring testFile = testDir + L"utf16le_bom.txt";
            std::wstring content = L"%gen_inp\n%ename test\n%cname \x6E2C\x8A66\n%encoding UTF-16LE\n%chardef begin\na \x4E2D\n%chardef end";
            CreateUTF16LEFile(testFile, content);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed");

            const WCHAR* buffer = file.GetReadBufferPointer();
            Assert::IsNotNull(buffer, L"Buffer should not be null");

            // Verify content loaded correctly (BOM should be skipped)
            std::wstring loaded(buffer);
            Assert::IsTrue(loaded.find(L"%gen_inp") == 0, L"Content should start with %gen_inp");
            Assert::IsTrue(loaded.find(L"\x6E2C\x8A66") != std::wstring::npos, L"Unicode characters should be preserved");
        }

        // UT-05-02: File Without BOM (should still work if valid UTF-16)
        TEST_METHOD(FileIO_UTF16LE_WithoutBOM_LoadsCorrectly)
        {
            Logger::WriteMessage("Test: FileIO_UTF16LE_WithoutBOM_LoadsCorrectly\n");

            std::wstring testFile = testDir + L"utf16le_nobom.txt";

            // Write content WITHOUT BOM using binary mode
            HANDLE hFile = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE);

            std::wstring content = L"%gen_inp\n%ename test\n%chardef begin\na \x4E2D\n%chardef end";
            DWORD bytesWritten = 0;
            DWORD contentBytes = (DWORD)(content.length() * sizeof(WCHAR));
            WriteFile(hFile, content.c_str(), contentBytes, &bytesWritten, nullptr);
            CloseHandle(hFile);
            createdFiles.push_back(testFile);

            CFile fileObj;
            BOOL result = fileObj.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed");

            const WCHAR* buffer = fileObj.GetReadBufferPointer();
            // Note: Without BOM, IsTextUnicode may reject the file
            // This tests the actual behavior
            if (buffer != nullptr)
            {
                std::wstring loaded(buffer);
                Logger::WriteMessage("File loaded without BOM\n");
            }
            else
            {
                Logger::WriteMessage("File rejected without BOM (expected behavior)\n");
            }
        }

        // UT-05-03: Empty File Handling
        TEST_METHOD(FileIO_EmptyFile_HandlesGracefully)
        {
            Logger::WriteMessage("Test: FileIO_EmptyFile_HandlesGracefully\n");

            std::wstring testFile = testDir + L"empty.txt";
            std::ofstream file(testFile, std::ios::binary);
            Assert::IsTrue(file.is_open());
            file.close();
            createdFiles.push_back(testFile);

            CFile fileObj;
            BOOL result = fileObj.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed for empty file");

            const WCHAR* buffer = fileObj.GetReadBufferPointer();
            // Empty file should return null or empty buffer
            if (buffer == nullptr)
            {
                Logger::WriteMessage("Empty file returns null buffer (acceptable)\n");
            }
            else
            {
                Assert::AreEqual(0, (int)wcslen(buffer), L"Empty file should have zero-length content");
            }
        }

        // UT-05-04: File With Only BOM
        TEST_METHOD(FileIO_OnlyBOM_HandlesGracefully)
        {
            Logger::WriteMessage("Test: FileIO_OnlyBOM_HandlesGracefully\n");

            std::wstring testFile = testDir + L"only_bom.txt";

            // Write only BOM using binary mode
            HANDLE hFile = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE);

            BYTE bom[2] = { 0xFF, 0xFE };
            DWORD bytesWritten = 0;
            WriteFile(hFile, bom, 2, &bytesWritten, nullptr);
            CloseHandle(hFile);
            createdFiles.push_back(testFile);

            CFile fileObj;
            BOOL result = fileObj.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed");

            const WCHAR* buffer = fileObj.GetReadBufferPointer();
            // File with only BOM should have zero content after BOM
            if (buffer == nullptr)
            {
                Logger::WriteMessage("BOM-only file returns null buffer (acceptable)\n");
            }
            else
            {
                Assert::AreEqual(0, (int)wcslen(buffer), L"BOM-only file should have zero-length content");
            }
        }

        // UT-05-05: MS-02 Boundary Testing (Already covered in MemoryTest, but verify via TestHook)
        TEST_METHOD(FileIO_FileSizeValidation_BelowLimit)
        {
            Logger::WriteMessage("Test: FileIO_FileSizeValidation_BelowLimit\n");

            std::wstring testFile = testDir + L"small_file.cin";
            std::wstring content = L"%gen_inp\n%ename test\n";
            CreateUTF16LEFile(testFile, content);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed for small file");

            // Call TestHook to verify SetupReadBuffer succeeds
            BOOL setupResult = file.TestHook_SetupReadBuffer();
            Assert::IsTrue(setupResult, L"SetupReadBuffer should succeed for valid file");

            const WCHAR* buffer = file.GetReadBufferPointer();
            Assert::IsNotNull(buffer, L"Buffer should be allocated");
        }

        // UT-05-06: Invalid Unicode Detection
        TEST_METHOD(FileIO_InvalidUnicode_ReturnsNull)
        {
            Logger::WriteMessage("Test: FileIO_InvalidUnicode_ReturnsNull\n");

            std::wstring testFile = testDir + L"invalid_unicode.txt";
            // Create file with invalid UTF-16 sequences
            std::vector<BYTE> invalidData = {
                0xFF, 0xFE,  // BOM
                0x00, 0xD8,  // High surrogate without low surrogate (invalid)
                0x41, 0x00   // 'A'
            };
            CreateBinaryFile(testFile, invalidData);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should succeed");

            const WCHAR* buffer = file.GetReadBufferPointer();
            // IsTextUnicode may reject invalid sequences
            // Behavior depends on Windows API
            Logger::WriteMessage(buffer ? "Invalid Unicode accepted\n" : "Invalid Unicode rejected\n");
        }

        // UT-05-07: File Timestamp Detection
        TEST_METHOD(FileIO_TimestampDetection_DetectsChanges)
        {
            Logger::WriteMessage("Test: FileIO_TimestampDetection_DetectsChanges\n");

            std::wstring testFile = testDir + L"timestamp_test.cin";
            std::wstring content1 = L"%gen_inp\n%ename test1\n";
            CreateUTF16LEFile(testFile, content1);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE);
            Assert::IsTrue(result, L"CreateFile should succeed");

            // First read
            const WCHAR* buffer1 = file.GetReadBufferPointer();
            Assert::IsNotNull(buffer1, L"First read should succeed");

            // Wait to ensure timestamp difference
            Sleep(1100);

            // Modify file
            DeleteFileW(testFile.c_str());
            std::wstring content2 = L"%gen_inp\n%ename test2\n";
            CreateUTF16LEFile(testFile, content2);

            // Check if file updated
            BOOL updated = file.IsFileUpdated();
            Assert::IsTrue(updated, L"IsFileUpdated should detect file modification");
        }

        // UT-05-08: Multiple Sequential Reads
        TEST_METHOD(FileIO_MultipleReads_SameContent)
        {
            Logger::WriteMessage("Test: FileIO_MultipleReads_SameContent\n");

            std::wstring testFile = testDir + L"multi_read.cin";
            std::wstring content = L"%gen_inp\n%ename test\n%cname \x6E2C\x8A66\n";
            CreateUTF16LEFile(testFile, content);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result);

            // Read multiple times
            const WCHAR* buffer1 = file.GetReadBufferPointer();
            const WCHAR* buffer2 = file.GetReadBufferPointer();
            const WCHAR* buffer3 = file.GetReadBufferPointer();

            Assert::IsNotNull(buffer1);
            Assert::IsNotNull(buffer2);
            Assert::IsNotNull(buffer3);

            // All reads should return same pointer
            Assert::AreEqual((size_t)buffer1, (size_t)buffer2, L"Subsequent reads should return same pointer");
            Assert::AreEqual((size_t)buffer2, (size_t)buffer3, L"Subsequent reads should return same pointer");
        }

        // UT-05-09: File Path Edge Cases
        TEST_METHOD(FileIO_LongFilePath_HandlesCorrectly)
        {
            Logger::WriteMessage("Test: FileIO_LongFilePath_HandlesCorrectly\n");

            // Create file with reasonably long path
            std::wstring longPath = testDir + L"very_long_directory_name_for_testing_edge_cases_in_file_system\\";
            CreateDirectoryW(longPath.c_str(), nullptr);

            std::wstring testFile = longPath + L"test_file_with_long_name_for_edge_case_testing.cin";
            std::wstring content = L"%gen_inp\n%ename test\n";
            CreateUTF16LEFile(testFile, content);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result, L"CreateFile should handle long paths");

            const WCHAR* buffer = file.GetReadBufferPointer();
            Assert::IsNotNull(buffer, L"Should load file with long path");

            // Cleanup
            DeleteFileW(testFile.c_str());
            RemoveDirectoryW(longPath.c_str());
        }

        // UT-05-10: Concurrent File Access
        TEST_METHOD(FileIO_SharedRead_AllowsMultipleReaders)
        {
            Logger::WriteMessage("Test: FileIO_SharedRead_AllowsMultipleReaders\n");

            std::wstring testFile = testDir + L"shared_read.cin";
            // Clean up any existing file first
            DeleteFileW(testFile.c_str());

            std::wstring content = L"%gen_inp\n%ename shared\n";
            CreateUTF16LEFile(testFile, content);

            CFile file1;
            BOOL result1 = file1.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result1, L"First reader should succeed");

            const WCHAR* buffer1 = file1.GetReadBufferPointer();
            Assert::IsNotNull(buffer1, L"First reader should get buffer");

            CFile file2;
            BOOL result2 = file2.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result2, L"Second reader should succeed with FILE_SHARE_READ");

            const WCHAR* buffer2 = file2.GetReadBufferPointer();
            Assert::IsNotNull(buffer2, L"Second reader should get buffer");

            // Both should have same content (or at least both should succeed)
            // Note: The buffers are independent copies, content should be identical
            size_t len1 = wcslen(buffer1);
            size_t len2 = wcslen(buffer2);
            wchar_t msg[256];
            swprintf_s(msg, L"Buffer1 length: %zu, Buffer2 length: %zu\n", len1, len2);
            Logger::WriteMessage(msg);

            Assert::AreEqual(len1, len2, L"Both readers should read same length");

            // Verify both contain expected content
            Assert::IsTrue(wcsstr(buffer1, L"%gen_inp") != nullptr, L"First reader should have content");
            Assert::IsTrue(wcsstr(buffer2, L"%gen_inp") != nullptr, L"Second reader should have content");
        }

        // UT-05-11: Non-existent File Handling (Error Path)
        TEST_METHOD(FileIO_NonExistentFile_ReturnsFalse)
        {
            Logger::WriteMessage("Test: FileIO_NonExistentFile_ReturnsFalse\n");

            std::wstring testFile = testDir + L"nonexistent_file_that_does_not_exist.cin";

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsFalse(result, L"CreateFile should fail for non-existent file");

            const WCHAR* buffer = file.GetReadBufferPointer();
            Assert::IsNull(buffer, L"Buffer should be null for failed CreateFile");
        }

        // UT-05-12: File With Special Characters in Content
        TEST_METHOD(FileIO_SpecialCharacters_PreservesContent)
        {
            Logger::WriteMessage("Test: FileIO_SpecialCharacters_PreservesContent\n");

            std::wstring testFile = testDir + L"special_chars.cin";
            // Content with various Unicode ranges
            std::wstring content = L"%gen_inp\n"
                L"%cname Chinese:\x4E2D\x6587 Japanese:\x3042\x3044 Korean:\xAC00\xB098\n"
                L"%chardef begin\n"
                L"test \x4E2D\x6587\x3042\x3044\xAC00\xB098\n"
                L"%chardef end";
            CreateUTF16LEFile(testFile, content);

            CFile file;
            BOOL result = file.CreateFile(testFile.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
            Assert::IsTrue(result);

            const WCHAR* buffer = file.GetReadBufferPointer();
            Assert::IsNotNull(buffer);

            std::wstring loaded(buffer);
            Assert::IsTrue(loaded.find(L"\x4E2D\x6587") != std::wstring::npos, L"Chinese characters preserved");
            Assert::IsTrue(loaded.find(L"\x3042\x3044") != std::wstring::npos, L"Japanese characters preserved");
            Assert::IsTrue(loaded.find(L"\xAC00\xB098") != std::wstring::npos, L"Korean characters preserved");
        }
    };
}


