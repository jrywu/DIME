// StringTest.cpp - String Processing Unit Tests (UT-03)
// Tests for BaseStructure.cpp (CStringRange class)
// Coverage Target: >= 90% overall, 100% per operation

#include "pch.h"
#include "CppUnitTest.h"
#include "BaseStructure.h"
#include "Globals.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(StringTest)
    {
    public:
        // UT-03-01: Basic Operations - Set(), Clear(), Get(), GetLength()
        // Target: 100% coverage of Set, Clear, Get, GetLength operations
        TEST_METHOD(StringRange_BasicOperations)
        {
            // Test Set operation with simple ASCII string
            CStringRange str;
            WCHAR testStr[] = { 0x6E2C, 0x8A66, 0x5B57, 0x4E32, 0 }; // Test Unicode via hex escapes
            str.Set(testStr, 4);
            
            // Verify Get() returns correct pointer
            Assert::IsNotNull(str.Get());
            Assert::AreEqual(0, wcscmp(str.Get(), testStr));
            
            // Verify GetLength() returns correct length
            Assert::AreEqual((DWORD_PTR)4, str.GetLength());
            
            // Test Clear operation
            str.Clear();
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            Assert::IsNull(str.Get());
        }

        // UT-03-01: Set operation with various lengths
        // Target: 100% coverage of Set with different parameter combinations
        TEST_METHOD(StringRange_SetVariousLengths)
        {
            CStringRange str;
            
            // Test empty string (length 0)
            str.Set(L"", 0);
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            Assert::IsNotNull(str.Get()); // Pointer to empty string is still valid
            
            // Test single character
            str.Set(L"A", 1);
            Assert::AreEqual((DWORD_PTR)1, str.GetLength());
            Assert::AreEqual(L'A', *str.Get());
            
            // Test longer string
            str.Set(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26);
            Assert::AreEqual((DWORD_PTR)26, str.GetLength());
            Assert::AreEqual(0, wcsncmp(str.Get(), L"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26));
            
            // Test partial string (only first 3 chars)
            str.Set(L"HELLO", 3);
            Assert::AreEqual((DWORD_PTR)3, str.GetLength());
            Assert::AreEqual(0, wcsncmp(str.Get(), L"HEL", 3));
        }

        // UT-03-01: Unicode character support using hex escape sequences
        // Target: 100% Unicode handling coverage
        TEST_METHOD(StringRange_UnicodeSupport)
        {
            CStringRange str;
            
            // Test Chinese characters using hex escapes
            WCHAR chinese[] = { 0x4E2D, 0x6587, 0x8F38, 0x5165, 0x6CD5, 0 };
            str.Set(chinese, 5);
            Assert::AreEqual((DWORD_PTR)5, str.GetLength());
            Assert::AreEqual((WCHAR)0x4E2D, *str.Get());
            
            // Test Japanese hiragana
            WCHAR japanese[] = { 0x3072, 0x3089, 0x304C, 0x306A, 0 };
            str.Set(japanese, 4);
            Assert::AreEqual((DWORD_PTR)4, str.GetLength());
            
            // Test Korean
            WCHAR korean[] = { 0xD55C, 0xAE00, 0 };
            str.Set(korean, 2);
            Assert::AreEqual((DWORD_PTR)2, str.GetLength());
            
            // Test mixed ASCII and Unicode
            WCHAR mixed[] = { L'A', L'B', L'C', 0x4E2D, 0x6587, L'1', L'2', L'3', 0 };
            str.Set(mixed, 8);
            Assert::AreEqual((DWORD_PTR)8, str.GetLength());
        }

        // UT-03-01: Null pointer handling
        // Target: 100% null/empty string handling coverage
        TEST_METHOD(StringRange_NullHandling)
        {
            CStringRange str;
            
            // Test setting null pointer
            str.Set(nullptr, 0);
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            Assert::IsNull(str.Get());
            
            // Test Clear after null
            str.Clear();
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            Assert::IsNull(str.Get());
            
            // Test setting valid string after null
            str.Set(L"Valid", 5);
            Assert::AreEqual((DWORD_PTR)5, str.GetLength());
            Assert::IsNotNull(str.Get());
            
            // Test Clear after valid string
            str.Clear();
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            Assert::IsNull(str.Get());
        }

        // UT-03-01: Compare operation with equal strings
        // Target: 100% coverage of Compare with equal strings
        TEST_METHOD(StringRange_CompareEqualStrings)
        {
            CStringRange str1, str2;
            
            // Test identical ASCII strings
            str1.Set(L"ABC", 3);
            str2.Set(L"ABC", 3);
            int result = CStringRange::Compare(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), &str1, &str2);
            Assert::AreEqual(CSTR_EQUAL, result);
            
            // Test identical Unicode strings using hex escapes
            WCHAR unicode1[] = { 0x6E2C, 0x8A66, 0 };
            WCHAR unicode2[] = { 0x6E2C, 0x8A66, 0 };
            str1.Set(unicode1, 2);
            str2.Set(unicode2, 2);
            result = CStringRange::Compare(MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT), &str1, &str2);
            Assert::AreEqual(CSTR_EQUAL, result);
            
            // Test case-insensitive comparison (NORM_IGNORECASE is used in Compare)
            str1.Set(L"abc", 3);
            str2.Set(L"ABC", 3);
            result = CStringRange::Compare(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), &str1, &str2);
            Assert::AreEqual(CSTR_EQUAL, result); // Should be equal due to NORM_IGNORECASE
        }

        // UT-03-01: Compare operation with different strings
        // Target: 100% coverage of Compare with different strings
        TEST_METHOD(StringRange_CompareDifferentStrings)
        {
            CStringRange str1, str2;
            LCID locale = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
            
            // Test different ASCII strings
            str1.Set(L"ABC", 3);
            str2.Set(L"XYZ", 3);
            int result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
            
            // Test different lengths
            str1.Set(L"ABC", 3);
            str2.Set(L"ABCDEF", 6);
            result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
            
            // Test different Unicode strings using hex escapes
            WCHAR unicode1[] = { 0x4E2D, 0x6587, 0 };
            WCHAR unicode2[] = { 0x65E5, 0x672C, 0 };
            str1.Set(unicode1, 2);
            str2.Set(unicode2, 2);
            result = CStringRange::Compare(MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT), &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
            
            // Test partially matching strings
            str1.Set(L"ABCDEF", 6);
            str2.Set(L"ABCXYZ", 6);
            result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
        }

        // UT-03-01: Compare with empty strings
        // Target: Edge case coverage for Compare
        TEST_METHOD(StringRange_CompareEmptyStrings)
        {
            CStringRange str1, str2;
            LCID locale = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
            
            // Test two empty strings
            str1.Set(L"", 0);
            str2.Set(L"", 0);
            int result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreEqual(CSTR_EQUAL, result);
            
            // Test empty vs non-empty
            str1.Set(L"", 0);
            str2.Set(L"ABC", 3);
            result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
            
            // Test non-empty vs empty
            str1.Set(L"XYZ", 3);
            str2.Set(L"", 0);
            result = CStringRange::Compare(locale, &str1, &str2);
            Assert::AreNotEqual(CSTR_EQUAL, result);
        }

        // UT-03-01: Assignment operator and copy constructor
        // Target: 100% coverage of Set(CStringRange&) and operator=
        TEST_METHOD(StringRange_AssignmentOperations)
        {
            CStringRange str1, str2, str3;

            // Test Set(CStringRange&)
            str1.Set(L"Original", 8);
            str2.Set(str1);
            Assert::AreEqual(str1.GetLength(), str2.GetLength());
            Assert::AreEqual(0, wcscmp(str1.Get(), str2.Get()));

            // Test operator=
            str3 = str1;
            Assert::AreEqual(str1.GetLength(), str3.GetLength());
            Assert::AreEqual(str1.Get(), str3.Get()); // Should point to same buffer

            // Verify shallow copy behavior: pointers are copied, not data
            // After changing str1 to point to a different buffer, str2 still points to original
            const WCHAR* originalPtr = str2.Get();
            str1.Set(L"Modified", 8);
            Assert::AreEqual(originalPtr, str2.Get()); // str2 still points to original buffer
            Assert::AreNotEqual(str1.Get(), str2.Get()); // Now pointing to different buffers
        }

        // UT-03-01: CharNext operation
        // Target: Coverage of CharNext for string iteration
        TEST_METHOD(StringRange_CharNextOperation)
        {
            CStringRange str, next;
            
            // Test CharNext on ASCII string
            str.Set(L"ABC", 3);
            str.CharNext(&next);
            Assert::AreEqual((DWORD_PTR)2, next.GetLength()); // Should advance by 1 character
            Assert::AreEqual(L'B', *next.Get());
            
            // Test CharNext on Unicode string using hex escapes
            WCHAR unicode[] = { 0x4E2D, 0x6587, 0x5B57, 0 };
            str.Set(unicode, 3);
            str.CharNext(&next);
            Assert::AreEqual((DWORD_PTR)2, next.GetLength());
            Assert::AreEqual((WCHAR)0x6587, *next.Get());
            
            // Test CharNext on empty string
            str.Set(L"", 0);
            str.CharNext(&next);
            Assert::AreEqual((DWORD_PTR)0, next.GetLength());
            Assert::IsNull(next.Get());
            
            // Test CharNext on single character
            str.Set(L"A", 1);
            str.CharNext(&next);
            Assert::AreEqual((DWORD_PTR)0, next.GetLength());
        }

        // UT-03-01: Multiple sequential operations
        // Target: Integration testing of combined operations
        TEST_METHOD(StringRange_SequentialOperations)
        {
            CStringRange str;
            
            // Set -> Get -> Clear -> Set again
            str.Set(L"First", 5);
            Assert::AreEqual((DWORD_PTR)5, str.GetLength());
            
            str.Clear();
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
            
            str.Set(L"Second", 6);
            Assert::AreEqual((DWORD_PTR)6, str.GetLength());
            
            // Multiple Sets without Clear
            str.Set(L"ABC", 3);
            Assert::AreEqual((DWORD_PTR)3, str.GetLength());
            
            str.Set(L"DEFGH", 5);
            Assert::AreEqual((DWORD_PTR)5, str.GetLength());
            Assert::AreEqual(0, wcscmp(str.Get(), L"DEFGH"));
        }

        // UT-03-01: Boundary testing for string lengths
        // Target: Edge cases for length handling
        TEST_METHOD(StringRange_BoundaryLengths)
        {
            CStringRange str;
            
            // Test with very long string
            const int longLength = 1000;
            WCHAR longString[1001];
            for (int i = 0; i < longLength; i++)
            {
                longString[i] = L'A' + (i % 26);
            }
            longString[longLength] = L'\0';
            
            str.Set(longString, longLength);
            Assert::AreEqual((DWORD_PTR)longLength, str.GetLength());
            Assert::AreEqual(L'A', *str.Get());
            
            // Test length 1
            str.Set(L"X", 1);
            Assert::AreEqual((DWORD_PTR)1, str.GetLength());
            
            // Test length 0
            str.Set(L"", 0);
            Assert::AreEqual((DWORD_PTR)0, str.GetLength());
        }
    };
}
