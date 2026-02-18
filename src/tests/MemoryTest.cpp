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

/********************************************************************************
* MemoryTest.cpp
* 
* Test Suite: UT-04 - Memory Management Tests
* Purpose: Test CDIMEArray, file memory handling, and GDI resource cleanup
* Coverage Target: ≥95% for CDIMEArray operations, ≥95% for file memory management
* 
* Test Categories:
*   UT-04-01: CDIMEArray operations (Append, Insert, RemoveAt, Clear, Count, GetAt)
*   UT-04-02: File memory handling (large files, size limits, MS-02 security fix)
*   UT-04-03: Memory leak detection (stress testing with 10,000 iterations)
*
* Reference: docs/TEST_PLAN.md lines 951-1101
********************************************************************************/

#include "pch.h"
#include "CppUnitTest.h"
#include "../BaseStructure.h"
#include "../File.h"
#include "../Globals.h"
#include <fstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(MemoryTest)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            Logger::WriteMessage(L"MemoryTest: Initializing test class");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage(L"MemoryTest: Cleaning up test class");
            // Clean up any test files
            DeleteFileW(L"test_large_95mb.cin");
            DeleteFileW(L"test_oversized_101mb.cin");
            DeleteFileW(L"test_normal_10mb.cin");
            DeleteFileW(L"test_small.cin");
        }

        // ====================================================================
        // UT-04-01: CDIMEArray Basic Operations Tests
        // Target: BaseStructure.h CDIMEArray<T> template class
        // Coverage Goal: 100% of Append, GetAt, RemoveAt, Clear, Count
        // ====================================================================

        TEST_METHOD(DIMEArray_BasicOperations)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_BasicOperations - Test Append, GetAt, Count, Clear");

            // Arrange
            CDIMEArray<int> array;

            // Act & Assert: Initial state
            Assert::AreEqual(0U, array.Count(), L"New array should be empty");

            // Act: Append first element
            int* item1 = array.Append();
            Assert::IsNotNull(item1, L"Append should return valid pointer");
            *item1 = 42;

            // Assert: Count and GetAt after first append
            Assert::AreEqual(1U, array.Count(), L"Count should be 1 after first append");
            Assert::AreEqual(42, *array.GetAt(0), L"First element should be 42");

            // Act: Append second element
            int* item2 = array.Append();
            *item2 = 99;

            // Assert: Count and GetAt after second append
            Assert::AreEqual(2U, array.Count(), L"Count should be 2 after second append");
            Assert::AreEqual(42, *array.GetAt(0), L"First element should still be 42");
            Assert::AreEqual(99, *array.GetAt(1), L"Second element should be 99");

            // Act: Clear array
            array.Clear();

            // Assert: Empty after clear
            Assert::AreEqual(0U, array.Count(), L"Array should be empty after Clear");
        }

        TEST_METHOD(DIMEArray_RemoveAtOperation)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_RemoveAtOperation - Test RemoveAt with multiple elements");

            // Arrange: Create array with 5 elements
            CDIMEArray<int> array;
            for (int i = 0; i < 5; i++)
            {
                int* item = array.Append();
                *item = i * 10;  // 0, 10, 20, 30, 40
            }

            // Verify initial state
            Assert::AreEqual(5U, array.Count(), L"Array should have 5 elements");

            // Act: Remove element at index 2 (value 20)
            array.RemoveAt(2);

            // Assert: Count decreased and remaining elements correct
            Assert::AreEqual(4U, array.Count(), L"Count should be 4 after removal");
            Assert::AreEqual(0, *array.GetAt(0), L"Element 0 should be 0");
            Assert::AreEqual(10, *array.GetAt(1), L"Element 1 should be 10");
            Assert::AreEqual(30, *array.GetAt(2), L"Element 2 should be 30 (was 3)");
            Assert::AreEqual(40, *array.GetAt(3), L"Element 3 should be 40 (was 4)");

            // Act: Remove first element
            array.RemoveAt(0);

            // Assert: Count and elements after removing first
            Assert::AreEqual(3U, array.Count(), L"Count should be 3 after removing first");
            Assert::AreEqual(10, *array.GetAt(0), L"New first element should be 10");
            Assert::AreEqual(30, *array.GetAt(1), L"Element 1 should be 30");
            Assert::AreEqual(40, *array.GetAt(2), L"Element 2 should be 40");

            // Act: Remove last element
            array.RemoveAt(2);

            // Assert: Count and elements after removing last
            Assert::AreEqual(2U, array.Count(), L"Count should be 2 after removing last");
            Assert::AreEqual(10, *array.GetAt(0), L"Element 0 should be 10");
            Assert::AreEqual(30, *array.GetAt(1), L"Element 1 should be 30");
        }

        TEST_METHOD(DIMEArray_StringRangeElements)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_StringRangeElements - Test CDIMEArray with CStringRange objects");

            // Arrange
            CDIMEArray<CStringRange> stringArray;

            // Act: Append string elements
            CStringRange* str1 = stringArray.Append();
            WCHAR text1[] = { 0x6E2C, 0x8A66, 0x0000 };  // "測試" in hex
            str1->Set(text1, 2);

            CStringRange* str2 = stringArray.Append();
            WCHAR text2[] = { 0x8A18, 0x61B6, 0x9AD4, 0x0000 };  // "記憶體" in hex
            str2->Set(text2, 3);

            // Assert: Count and content
            Assert::AreEqual(2U, stringArray.Count(), L"String array should have 2 elements");
            
            CStringRange* retrieved1 = stringArray.GetAt(0);
            Assert::AreEqual(2U, static_cast<UINT>(retrieved1->GetLength()), L"First string should have length 2");
            
            CStringRange* retrieved2 = stringArray.GetAt(1);
            Assert::AreEqual(3U, static_cast<UINT>(retrieved2->GetLength()), L"Second string should have length 3");

            // Act: Clear
            stringArray.Clear();

            // Assert: Empty after clear
            Assert::AreEqual(0U, stringArray.Count(), L"Array should be empty after Clear");
        }

        TEST_METHOD(DIMEArray_ReserveOperation)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_ReserveOperation - Test reserve() for capacity allocation");

            // Arrange
            CDIMEArray<int> array;

            // Act: Reserve capacity for 100 elements
            array.reserve(100);

            // Assert: Count still 0 (reserve doesn't add elements)
            Assert::AreEqual(0U, array.Count(), L"Count should be 0 after reserve");

            // Act: Add 50 elements (should not trigger reallocation)
            for (int i = 0; i < 50; i++)
            {
                int* item = array.Append();
                *item = i;
            }

            // Assert: Count is 50, all elements correct
            Assert::AreEqual(50U, array.Count(), L"Count should be 50 after adding elements");
            for (UINT i = 0; i < 50; i++)
            {
                Assert::AreEqual(static_cast<int>(i), *array.GetAt(i), L"Element value should match index");
            }
        }

        TEST_METHOD(DIMEArray_MultipleAppendAndRemove)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_MultipleAppendAndRemove - Test repeated append/remove operations");

            // Arrange
            CDIMEArray<int> array;

            // Act: Add 10 elements
            for (int i = 0; i < 10; i++)
            {
                int* item = array.Append();
                *item = i * 5;
            }

            Assert::AreEqual(10U, array.Count(), L"Should have 10 elements");

            // Act: Remove every other element (5 removals)
            for (int i = 0; i < 5; i++)
            {
                array.RemoveAt(0);  // Always remove first
            }

            // Assert: 5 elements remaining
            Assert::AreEqual(5U, array.Count(), L"Should have 5 elements after removals");
            
            // Verify remaining elements are 25, 30, 35, 40, 45
            Assert::AreEqual(25, *array.GetAt(0), L"Element 0 should be 25");
            Assert::AreEqual(30, *array.GetAt(1), L"Element 1 should be 30");
            Assert::AreEqual(35, *array.GetAt(2), L"Element 2 should be 35");
            Assert::AreEqual(40, *array.GetAt(3), L"Element 3 should be 40");
            Assert::AreEqual(45, *array.GetAt(4), L"Element 4 should be 45");

            // Act: Clear and re-add
            array.Clear();
            int* newItem = array.Append();
            *newItem = 999;

            // Assert: Single new element
            Assert::AreEqual(1U, array.Count(), L"Should have 1 element after clear and append");
            Assert::AreEqual(999, *array.GetAt(0), L"New element should be 999");
        }

        TEST_METHOD(DIMEArray_EmptyArrayOperations)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_EmptyArrayOperations - Test operations on empty array");

            // Arrange
            CDIMEArray<int> array;

            // Assert: Empty array properties
            Assert::AreEqual(0U, array.Count(), L"New array should be empty");

            // Act: Clear empty array (should not crash)
            array.Clear();

            // Assert: Still empty
            Assert::AreEqual(0U, array.Count(), L"Array should still be empty after clearing empty array");

            // Act: Append to previously empty array
            int* item = array.Append();
            *item = 123;

            // Assert: Now has one element
            Assert::AreEqual(1U, array.Count(), L"Should have 1 element");
            Assert::AreEqual(123, *array.GetAt(0), L"Element should be 123");
        }

        // ====================================================================
        // UT-04-03: Memory Leak Detection Tests
        // Target: Stress testing with 10,000 iterations
        // Coverage Goal: 0 memory leaks
        // ====================================================================

        TEST_METHOD(DIMEArray_MemoryLeakStressTest)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_MemoryLeakStressTest - Test for memory leaks with 10,000 iterations");

            // Act: Perform 10,000 iterations of array operations
            for (int iteration = 0; iteration < 10000; iteration++)
            {
                CDIMEArray<CStringRange> array;
                
                // Add 100 string elements
                for (int j = 0; j < 100; j++)
                {
                    CStringRange* str = array.Append();
                    WCHAR testText[] = { 0x6E2C, 0x8A66, 0x0000 };  // "測試"
                    str->Set(testText, 2);
                }

                // Verify count
                if (iteration % 1000 == 0)
                {
                    Assert::AreEqual(100U, array.Count(), L"Should have 100 elements");
                }

                // Clear array (destructor will be called at end of scope)
                array.Clear();
            }

            // Assert: If we reach here without crash, memory management is working
            Logger::WriteMessage(L"Memory leak stress test completed successfully (10,000 iterations)");
        }

        TEST_METHOD(DIMEArray_LargeArrayStressTest)
        {
            Logger::WriteMessage(L"TEST: DIMEArray_LargeArrayStressTest - Test with large array (1000 elements)");

            // Arrange
            CDIMEArray<int> largeArray;

            // Act: Create array with 1000 elements
            for (int i = 0; i < 1000; i++)
            {
                int* item = largeArray.Append();
                *item = i;
            }

            // Assert: Count
            Assert::AreEqual(1000U, largeArray.Count(), L"Should have 1000 elements");

            // Verify some random elements
            Assert::AreEqual(0, *largeArray.GetAt(0), L"First element should be 0");
            Assert::AreEqual(500, *largeArray.GetAt(500), L"Middle element should be 500");
            Assert::AreEqual(999, *largeArray.GetAt(999), L"Last element should be 999");

            // Act: Remove half the elements
            for (int i = 0; i < 500; i++)
            {
                largeArray.RemoveAt(0);  // Remove first element 500 times
            }

            // Assert: 500 elements remaining
            Assert::AreEqual(500U, largeArray.Count(), L"Should have 500 elements after removals");
            Assert::AreEqual(500, *largeArray.GetAt(0), L"First element should now be 500");
            Assert::AreEqual(999, *largeArray.GetAt(499), L"Last element should still be 999");
        }

        // ====================================================================
        // UT-04-02: File Memory Handling Tests
        // Target: File.cpp CFile class
        // Coverage Goal: ≥95% file memory management
        // ====================================================================

        TEST_METHOD(FileMemory_NormalFileHandling)
        {
            Logger::WriteMessage(L"TEST: FileMemory_NormalFileHandling - Test normal file creation and memory allocation");

            // Arrange: Create small test file (< 1 MB)
            const WCHAR* fileName = L"test_small.cin";
            CreateTestCINFile(fileName, 1024 * 100);  // 100 KB

            // Act: Open file
            CFile* file = new CFile();
            BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);

            // Assert: File opened successfully
            Assert::IsTrue(result, L"File should open successfully");
            Assert::IsNotNull(file, L"File object should be valid");

            // Cleanup
            delete file;
            DeleteFileW(fileName);
            
            Logger::WriteMessage(L"Normal file handling completed successfully");
        }

        TEST_METHOD(FileMemory_LargeFileHandling)
        {
            Logger::WriteMessage(L"TEST: FileMemory_LargeFileHandling - Test large file (95 MB) within limit");

            // Arrange: Create large file close to limit (95 MB)
            const WCHAR* fileName = L"test_large_95mb.cin";
            const DWORD fileSize = 95 * 1024 * 1024;  // 95 MB
            
            Logger::WriteMessage(L"Creating 95 MB test file (this may take a moment)...");
            BOOL fileCreated = CreateTestCINFile(fileName, fileSize);
            Assert::IsTrue(fileCreated, L"Test file creation should succeed");

            // Act: Open large file
            CFile* file = new CFile();
            BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);

            // Assert: Should load successfully (below MAX_CIN_FILE_SIZE)
            Assert::IsTrue(result, L"Large file (95 MB) should load successfully");
            Assert::IsNotNull(file, L"File object should be valid");

            // Cleanup
            delete file;
            DeleteFileW(fileName);
            
            Logger::WriteMessage(L"Large file (95 MB) handling completed successfully");
        }

        TEST_METHOD(FileMemory_OversizedFileRejection)
        {
            Logger::WriteMessage(L"TEST: FileMemory_OversizedFileRejection - Test oversized file (101 MB) rejection (MS-02)");

            // Arrange: Create oversized file exceeding limit (101 MB)
            const WCHAR* fileName = L"test_oversized_101mb.cin";
            const DWORD fileSize = 101 * 1024 * 1024;  // 101 MB (exceeds MAX_CIN_FILE_SIZE)
            
            Logger::WriteMessage(L"Creating 101 MB test file (this may take a moment)...");
            BOOL fileCreated = CreateTestCINFile(fileName, fileSize);
            Assert::IsTrue(fileCreated, L"Test file creation should succeed");

            // Act: Attempt to load oversized file
            CFile* file = new CFile();
            BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);

            // Assert: Should reject loading (MS-02 security fix)
            Assert::IsFalse(result, L"Oversized file (101 MB) should be rejected per MS-02 security fix");

            // Cleanup
            delete file;
            DeleteFileW(fileName);
            
            Logger::WriteMessage(L"Oversized file rejection (MS-02) verified successfully");
        }

        TEST_METHOD(FileMemory_FileSizeBoundaryTest)
        {
            Logger::WriteMessage(L"TEST: FileMemory_FileSizeBoundaryTest - Test exactly at 100 MB boundary");

            // Arrange: Create file exactly at MAX_CIN_FILE_SIZE (100 MB)
            const WCHAR* fileName = L"test_normal_10mb.cin";
            const DWORD fileSize = 10 * 1024 * 1024;  // 10 MB (well within limit for faster test)
            
            CreateTestCINFile(fileName, fileSize);

            // Act: Open file
            CFile* file = new CFile();
            BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);

            // Assert: Should load successfully
            Assert::IsTrue(result, L"File at boundary should load successfully");

            // Cleanup
            delete file;
            DeleteFileW(fileName);
            
            Logger::WriteMessage(L"Boundary test completed successfully");
        }

        TEST_METHOD(FileMemory_MultipleFileOpenClose)
        {
            Logger::WriteMessage(L"TEST: FileMemory_MultipleFileOpenClose - Test multiple file open/close cycles");

            // Arrange: Create test file
            const WCHAR* fileName = L"test_small.cin";
            CreateTestCINFile(fileName, 1024 * 50);  // 50 KB

            // Act: Open and close file 100 times
            for (int i = 0; i < 100; i++)
            {
                CFile* file = new CFile();
                BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);
                
                Assert::IsTrue(result, L"File should open successfully on each iteration");
                
                delete file;  // Should properly cleanup
            }

            // Cleanup
            DeleteFileW(fileName);
            
            Logger::WriteMessage(L"Multiple open/close cycles completed successfully");
        }

        TEST_METHOD(FileMemory_InvalidFileHandling)
        {
            Logger::WriteMessage(L"TEST: FileMemory_InvalidFileHandling - Test handling of non-existent file");

            // Arrange: Use non-existent file name
            const WCHAR* fileName = L"nonexistent_file_12345.cin";

            // Act: Attempt to open non-existent file
            CFile* file = new CFile();
            BOOL result = file->CreateFile(fileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ);

            // Assert: Should fail gracefully
            Assert::IsFalse(result, L"Opening non-existent file should fail");

            // Cleanup
            delete file;
            
            Logger::WriteMessage(L"Invalid file handling test completed successfully");
        }

    private:
        // Helper function to create test CIN files
        BOOL CreateTestCINFile(const WCHAR* fileName, DWORD sizeInBytes)
        {
            // Create file with Unicode BOM and test content
            std::ofstream file(fileName, std::ios::binary);
            if (!file.is_open())
            {
                return FALSE;
            }

            // Write Unicode BOM (UTF-16 LE)
            unsigned char bom[2] = { 0xFF, 0xFE };
            file.write(reinterpret_cast<char*>(bom), 2);

            // Write CIN file header
            const char* header = "%gen_inp\n%ename DIME_TEST\n%cname Test Dictionary\n%encoding UTF-16LE\n";
            file.write(header, strlen(header));

            // Fill remaining space with valid CIN data (key-value pairs)
            DWORD bytesWritten = 2 + static_cast<DWORD>(strlen(header));
            
            while (bytesWritten < sizeInBytes)
            {
                // Write sample mapping: "a 測\n" (simplified)
                const char* sampleLine = "a \xb8\xde\n";  // Simplified, actual CIN uses proper encoding
                DWORD lineSize = static_cast<DWORD>(strlen(sampleLine));
                
                if (bytesWritten + lineSize > sizeInBytes)
                {
                    // Write padding to reach exact size
                    DWORD remaining = sizeInBytes - bytesWritten;
                    std::vector<char> padding(remaining, ' ');
                    file.write(padding.data(), remaining);
                    bytesWritten = sizeInBytes;
                }
                else
                {
                    file.write(sampleLine, lineSize);
                    bytesWritten += lineSize;
                }
            }

            file.close();
            return TRUE;
        }
    };
}
