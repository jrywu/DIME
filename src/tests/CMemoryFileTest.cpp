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
* UT-09: CMemoryFile Tests
*
* Target Module: File.cpp
* Target Class:  CMemoryFile
* Coverage Target: 100% of new CMemoryFile code paths
*
* Test Categories:
*   UT-09-01: Constructor
*   UT-09-02 through UT-09-05: GetReadBufferPointer basics
*   UT-09-06: Hot-reload (source file modified while running)
*   UT-09-07: Null source buffer
*   UT-09-08: SetupReadBuffer byte-count accuracy
*   UT-09-09 through UT-09-14: FilterLine via buffer inspection
*   UT-09-15: Integration — CTableDictionaryEngine over CMemoryFile
*   UT-09-16: CRLF endings
*   UT-09-17: BMP symbol pass-through
*   UT-09-21 through UT-09-25: Disk cache (BuildCachePath, TryLoadCache, WriteCacheToDisk)
*
* Real-file integration tests are in CMemoryFileIntegrationTest.cpp (IT-MF).
*
* Reference: docs/BIG5_FILTER.md Section 11a
********************************************************************************/

#include "pch.h"
#include "CppUnitTest.h"
#include "../File.h"
#include "../TableDictionaryEngine.h"
#include "../BaseStructure.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(CMemoryFileTests)
    {
    private:
        std::wstring testDir;
        std::vector<std::wstring> createdFiles;
        int fileCounter;

        // Write a UTF-16LE temp file (BOM + content), open it with a new CFile*.
        // Caller owns the returned CFile*; file path is tracked in createdFiles.
        // Returns nullptr on any failure.
        CFile* MakeSourceFile(const std::wstring& content)
        {
            std::wstring filename = testDir + L"src_" + std::to_wstring(++fileCounter) + L".cin";

            HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return nullptr;

            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hFile, bom, 2, &bw, nullptr);
            WriteFile(hFile, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hFile);
            createdFiles.push_back(filename);

            CFile* pFile = new (std::nothrow) CFile();
            if (!pFile) return nullptr;
            if (!pFile->CreateFile(filename.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
            {
                delete pFile;
                return nullptr;
            }
            return pFile;
        }

        // Overwrite an existing file with new UTF-16LE content (BOM + content).
        void RewriteFile(const std::wstring& path, const std::wstring& content)
        {
            HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return;
            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hFile, bom, 2, &bw, nullptr);
            WriteFile(hFile, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hFile);
        }

        // Return true if the filtered output buffer of mem contains the given substring.
        bool BufferContains(CMemoryFile& mem, const std::wstring& text)
        {
            const WCHAR* buf = mem.GetReadBufferPointer();
            if (!buf) return false;
            DWORD_PTR charLen = mem.GetFileSize() / sizeof(WCHAR);
            std::wstring bufStr(buf, charLen);
            return bufStr.find(text) != std::wstring::npos;
        }

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            wchar_t tempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            testDir = std::wstring(tempPath) + L"DIMETests_MemFile\\";
            CreateDirectoryW(testDir.c_str(), nullptr);
            createdFiles.clear();
            fileCounter = 0;
        }

        TEST_METHOD_CLEANUP(Teardown)
        {
            // Delete all files in test directory (including cache files and .tmp)
            WIN32_FIND_DATAW fd;
            std::wstring pattern = testDir + L"*";
            HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        DeleteFileW((testDir + fd.cFileName).c_str());
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
            RemoveDirectoryW(testDir.c_str());
        }

        // UT-09-01: Construct from valid CFile*; resulting object is non-null.
        TEST_METHOD(UT_09_01_Constructor_ValidSource)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc, L"Source CFile must open");

            CMemoryFile* mem = new (std::nothrow) CMemoryFile(pSrc);
            Assert::IsNotNull(mem, L"CMemoryFile must be constructable from a valid CFile*");

            delete mem;
            delete pSrc;
        }

        // UT-09-02: First GetReadBufferPointer() call returns a non-null buffer.
        TEST_METHOD(UT_09_02_GetReadBufferPointer_FirstCall_NonNull)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            const WCHAR* buf = mem.GetReadBufferPointer();
            Assert::IsNotNull(buf, L"First GetReadBufferPointer must return non-null");

            delete pSrc;
        }

        // UT-09-03: fileReloaded is FALSE on the first call (lazy init is not a reload).
        TEST_METHOD(UT_09_03_GetReadBufferPointer_FirstCall_ReloadedFalse)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            BOOL reloaded = TRUE;   // start TRUE to ensure the call sets it to FALSE
            mem.GetReadBufferPointer(&reloaded);

            Assert::IsFalse(reloaded == TRUE,
                L"fileReloaded must be FALSE on initial lazy build (not a source reload)");
            delete pSrc;
        }

        // UT-09-04: Second call with no source change returns the same pointer; no rebuild.
        TEST_METHOD(UT_09_04_GetReadBufferPointer_SecondCall_SamePointer)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            const WCHAR* buf1 = mem.GetReadBufferPointer();
            const WCHAR* buf2 = mem.GetReadBufferPointer();

            Assert::AreEqual(buf1, buf2,
                L"Second call must return same pointer (buffer was not rebuilt)");
            delete pSrc;
        }

        // UT-09-05: Passing nullptr as fileReloaded does not crash.
        TEST_METHOD(UT_09_05_GetReadBufferPointer_NullptrArg_NoCrash)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            const WCHAR* buf = mem.GetReadBufferPointer(nullptr);
            Assert::IsNotNull(buf,
                L"nullptr fileReloaded arg must not crash; buffer must be non-null");
            delete pSrc;
        }

        // UT-09-06: When the source disk file changes, own buffer rebuilds and fileReloaded = TRUE.
        TEST_METHOD(UT_09_06_SourceReload_RebuildBuffer_FileReloadedTrue)
        {
            std::wstring contentA =
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(contentA);
            Assert::IsNotNull(pSrc);
            std::wstring filePath = createdFiles.back();

            CMemoryFile mem(pSrc);

            // Initial call — build buffer, fileReloaded should be FALSE
            BOOL rel1 = TRUE;
            const WCHAR* buf1 = mem.GetReadBufferPointer(&rel1);
            Assert::IsNotNull(buf1);
            Assert::IsFalse(rel1 == TRUE, L"Not reloaded on first call");

            // Sleep >1 s so the filesystem registers a different mtime
            Sleep(1100);

            // Overwrite the source file with different content
            std::wstring contentB =
                L"%gen_inp\n%chardef begin\nb \u5927\n%chardef end\n";
            RewriteFile(filePath, contentB);

            // Second call — source detects change; CMemoryFile must rebuild
            BOOL rel2 = FALSE;
            const WCHAR* buf2 = mem.GetReadBufferPointer(&rel2);

            Assert::IsNotNull(buf2, L"Buffer must be non-null after reload");
            Assert::IsTrue(rel2 == TRUE, L"fileReloaded must be TRUE after source file changed");

            // Confirm the new buffer reflects the updated content
            DWORD_PTR charLen = mem.GetFileSize() / sizeof(WCHAR);
            std::wstring newBuf(buf2, charLen);
            Assert::IsTrue(newBuf.find(L"b ") != std::wstring::npos,
                L"Rebuilt buffer must contain updated content");

            delete pSrc;
        }

        // UT-09-07: When source GetReadBufferPointer() returns null, CMemoryFile also returns null.
        TEST_METHOD(UT_09_07_NullSourceBuffer_ReturnsNull)
        {
            // A default-constructed CFile (no CreateFile called) has no buffer.
            CFile emptyFile;
            CMemoryFile mem(&emptyFile);

            const WCHAR* buf = mem.GetReadBufferPointer();
            Assert::IsNull(buf,
                L"Must return nullptr when the source CFile has no loaded buffer");
        }

        // UT-09-08: _fileSize reflects actual written bytes (smaller than source when lines are filtered).
        TEST_METHOD(UT_09_08_SetupReadBuffer_FileSizeReflectsFiltered)
        {
            // U+0400 (Cyrillic Capital Letter Ie) is not encodable in CP950.
            std::wstring content =
                L"%gen_inp\n"
                L"%chardef begin\n"
                L"a \u4E2D\n"    // CP950 — included
                L"b \u0400\n"   // non-CP950 (Cyrillic) — excluded
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            DWORD_PTR srcSize = pSrc->GetFileSize();

            CMemoryFile mem(pSrc);
            mem.GetReadBufferPointer();   // trigger SetupReadBuffer

            DWORD_PTR filteredSize = mem.GetFileSize();
            Assert::IsTrue(filteredSize < srcSize,
                L"Filtered buffer must be smaller than source (excluded line reduces size)");
            Assert::IsTrue(filteredSize > 0,
                L"Filtered buffer must still contain passing lines");

            delete pSrc;
        }

        // UT-09-09: An empty line INSIDE the chardef section passes through to the output buffer.
        // (Pre-chardef header lines — including empty lines — are now dropped entirely.)
        TEST_METHOD(UT_09_09_FilterLine_EmptyLine_InsideChardef_PassesThrough)
        {
            // Empty line is placed between %chardef begin and the first data entry.
            std::wstring content =
                L"%gen_inp\n%chardef begin\n\na \u4E2D\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            const WCHAR* buf = mem.GetReadBufferPointer();
            Assert::IsNotNull(buf);

            // Buffer contains "%chardef begin\n\na 中\n".
            // The double-\n comes from the chardef-begin line + the empty line.
            Assert::IsTrue(BufferContains(mem, L"\n\n"),
                L"Empty line inside chardef section must pass through (double-\\n present)");

            delete pSrc;
        }

        // UT-09-10: Critical CIN header directives (%sorted, %selkey, %endkey) are kept
        //          even though they appear outside %chardef begin/end.
        //          Non-critical header directives (%gen_inp, %ename, ...) are dropped.
        TEST_METHOD(UT_09_10_FilterLine_CriticalDirectives_PassThrough)
        {
            std::wstring content =
                L"%gen_inp\n"
                L"%sorted 1\n"
                L"%selkey 1234567890\n"
                L"%endkey \n"
                L"%chardef begin\n"
                L"a \u4E2D\n"
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);

            // Critical directives must survive
            Assert::IsTrue(BufferContains(mem, L"%sorted"),
                L"%sorted must pass through (gates radicalIndexMap building)");
            Assert::IsTrue(BufferContains(mem, L"%selkey"),
                L"%selkey must pass through (configures selection keys)");
            Assert::IsTrue(BufferContains(mem, L"%endkey"),
                L"%endkey must pass through (configures end keys)");

            // Non-critical header directive must be dropped
            Assert::IsFalse(BufferContains(mem, L"%gen_inp"),
                L"%gen_inp (non-critical header) must be dropped");

            delete pSrc;
        }

        // UT-09-11: A multi-char value token passes through (filter only rejects single-char non-CP950).
        TEST_METHOD(UT_09_11_FilterLine_MultiCharValue_PassesThrough)
        {
            // "a 中文" — value token is 2 WCHARs (multi-char) → always included regardless of CP950
            std::wstring content =
                L"%gen_inp\n%chardef begin\n"
                L"a \u4E2D\u6587\n"   // a -> 中文 (multi-char value)
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem, L"\u4E2D\u6587"),
                L"Multi-char value must always pass through");

            delete pSrc;
        }

        // UT-09-12: A single-char CP950 value (中, U+4E2D) passes through.
        TEST_METHOD(UT_09_12_FilterLine_SingleCP950Char_PassesThrough)
        {
            std::wstring content =
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem, L"\u4E2D"),
                L"Single-char CP950 value (U+4E2D) must pass through");

            delete pSrc;
        }

        // UT-09-13: A single-char non-CP950 CJK ideograph is excluded by the C3_IDEOGRAPH filter.
        TEST_METHOD(UT_09_13_FilterLine_SingleNonCP950Char_Excluded)
        {
            // U+3400 (CJK Extension A first char) is not in CP950 and has C3_IDEOGRAPH set.
            std::wstring content =
                L"%gen_inp\n%chardef begin\na \u3400\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            Assert::IsFalse(BufferContains(mem, L"\u3400"),
                L"Non-CP950 CJK ideograph (C3_IDEOGRAPH) must be excluded from output");
            // %chardef begin marker is inside the target section and must be present
            Assert::IsTrue(BufferContains(mem, L"%chardef begin"),
                L"%chardef begin marker must be present in filtered buffer");

            delete pSrc;
        }

        // UT-09-14: A whitespace-only line passes through (treated as empty after leading-ws skip).
        TEST_METHOD(UT_09_14_FilterLine_WhitespaceOnlyLine_PassesThrough)
        {
            // "   " — after skipping whitespace, q >= end → treated as empty → passes
            std::wstring content =
                L"%gen_inp\n%chardef begin\n   \na \u4E2D\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem, L"   "),
                L"Whitespace-only line must pass through (treated as empty)");

            delete pSrc;
        }

        // UT-09-15: Integration — CTableDictionaryEngine over CMemoryFile returns only CP950/non-ideograph chars.
        TEST_METHOD(UT_09_15_Integration_TableEngine_OnlyCP950Results)
        {
            // CIN with two single-char entries for key "a":
            //   中  (U+4E2D) — CP950 encodable               → must survive filter
            //   㐀  (U+3400) — CJK Extension A, C3_IDEOGRAPH  → must be filtered out
            std::wstring content =
                L"%gen_inp\n"
                L"%ename FilterTest\n"
                L"%selkey 1234567890\n"
                L"%keyname begin\n"
                L"%keyname end\n"
                L"%chardef begin\n"
                L"a\t\u4E2D\n"   // a -> 中 (CP950) — keep
                L"a\t\u3400\n"   // a -> 㐀 (CJK Extension A, C3_IDEOGRAPH) — filter out
                L"%chardef end\n";

            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source CFile must open");

            CMemoryFile* pMem = new (std::nothrow) CMemoryFile(pSrc);
            Assert::IsNotNull(pMem, L"CMemoryFile must construct");

            LCID locale = MAKELCID(
                MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT);
            CTableDictionaryEngine* engine = new (std::nothrow) CTableDictionaryEngine(
                locale, pMem, DICTIONARY_TYPE::CIN_DICTIONARY);
            Assert::IsNotNull(engine, L"CTableDictionaryEngine must construct");

            engine->ParseConfig(IME_MODE::IME_MODE_ARRAY);

            CStringRange keyCode;
            keyCode.Set(L"a", 1);
            CDIMEArray<CStringRange> results;
            engine->CollectWord(&keyCode, &results);

            bool foundCP950    = false;
            bool foundNonCP950 = false;
            for (UINT i = 0; i < results.Count(); i++)
            {
                const WCHAR* word = results.GetAt(i)->Get();
                DWORD_PTR    len  = results.GetAt(i)->GetLength();
                if (len == 1 && word[0] == L'\u4E2D') foundCP950    = true;
                if (len == 1 && word[0] == L'\u3400') foundNonCP950 = true;
            }

            Assert::IsTrue(foundCP950,
                L"CP950 character (U+4E2D, 中) must appear in CollectWord results");
            Assert::IsFalse(foundNonCP950,
                L"Non-CP950 CJK ideograph (U+3400, C3_IDEOGRAPH) must be absent from CollectWord results");

            delete engine;   // does not own pMem
            delete pMem;     // does not own pSrc
            delete pSrc;
        }

        // UT-09-16: CRLF endings — non-CP950 CJK ideograph must still be filtered.
        TEST_METHOD(UT_09_16_FilterLine_CRLF_NonCP950_Excluded)
        {
            // Source uses Windows CRLF (\r\n) — mirrors real on-disk UTF-16LE CIN files.
            // The CRLF strip in SetupReadBuffer must prevent \r from being counted as
            // part of the value token (which would trigger the multi-char passthrough).
            std::wstring content =
                L"%gen_inp\r\n%chardef begin\r\n"
                L"a \u4E2D\r\n"    // CP950 — should survive
                L"a \u3400\r\n"    // CJK Extension A (C3_IDEOGRAPH) — must be excluded even with \r\n
                L"%chardef end\r\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source file created");
            CMemoryFile mem(pSrc);
            Assert::IsFalse(BufferContains(mem, L"\u3400"), L"Non-CP950 CJK ideograph (C3_IDEOGRAPH) must be filtered with CRLF input");
            Assert::IsTrue(BufferContains(mem, L"\u4E2D"),  L"CP950 char must survive with CRLF input");
            delete pSrc;
        }

        // UT-09-17: BMP symbol characters must pass the filter regardless of CP950 encodability.
        TEST_METHOD(UT_09_17_FilterLine_BMP_Symbol_PassesFilter)
        {
            // C3_SYMBOL characters (arrows, dingbats, math, BMP emoji) must not be filtered.
            // Non-CP950 CJK ideographs (C3_IDEOGRAPH) must still be filtered.
            std::wstring content =
                L"%gen_inp\n%chardef begin\n"
                L"a \u2605\n"    // ★ BLACK STAR — BMP symbol, C3_SYMBOL — must pass
                L"a \u3400\n"    // CJK Extension A (U+3400) — C3_IDEOGRAPH, not CP950 — must be filtered
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source file created");
            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem, L"\u2605"),  L"BMP symbol must pass filter");
            Assert::IsFalse(BufferContains(mem, L"\u3400"), L"Non-CP950 CJK ideograph (C3_IDEOGRAPH) must be filtered");
            delete pSrc;
        }

        // UT-09-18: BMP symbol lacking C3_SYMBOL (Yijing hexagram U+4DC0) must pass through.
        // The fallback filters on C3_IDEOGRAPH: ideographic characters (CJK Extension A,
        // Kangxi Radicals, CJK Radicals Supplement) are filtered; non-ideographic symbols
        // (Yijing hexagrams, superscripts, fractions, APL, rare Katakana, …) pass through.
        TEST_METHOD(UT_09_18_FilterLine_YijingHexagram_PassesFilter)
        {
            std::wstring content =
                L"%gen_inp\n%chardef begin\n"
                L"a \u4DC0\n"   // YIJING HEXAGRAM 1 (U+4DC0) — no CP950, not C3_IDEOGRAPH — must pass
                L"a \u3400\n"   // CJK Extension A (U+3400) — no CP950, C3_IDEOGRAPH — must be filtered
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source file created");
            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem,  L"\u4DC0"), L"Yijing hexagram must pass filter");
            Assert::IsFalse(BufferContains(mem, L"\u3400"), L"CJK Extension A (C3_IDEOGRAPH) must be filtered");
            delete pSrc;
        }

        // UT-09-19: Supplementary-plane emoji (surrogate pairs in SMP U+10000-U+1FFFF)
        // must pass through. CJK Extension B code points (SIP U+20000+) must be filtered.
        // The plane check is used; GetStringTypeW(CT_CTYPE3) is unreliable for surrogates.
        TEST_METHOD(UT_09_19_FilterLine_SurrogatePair_EmojiPass_CJKExtFilter)
        {
            // U+1F4DE TELEPHONE RECEIVER  -> surrogates U+D83D U+DCDE  (SMP -> pass)
            // U+20000 CJK Ext B first char -> surrogates U+D840 U+DC00  (SIP -> filter)
            std::wstring content =
                L"%gen_inp\n%chardef begin\n"
                L"a \xD83D\xDCDE\n"   // U+1F4DE TELEPHONE RECEIVER -- SMP emoji, must pass
                L"a \xD840\xDC00\n"   // U+20000 CJK Ext B -- SIP ideograph, must be filtered
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source file created");
            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem,  L"\xD83D\xDCDE"), L"SMP emoji must pass filter");
            Assert::IsFalse(BufferContains(mem, L"\xD840\xDC00"), L"CJK Ext B (SIP) must be filtered");
            delete pSrc;
        }

        // UT-09-20: Numeric symbols not encodable in CP950 must pass through.
        // Vulgar fractions, superscript digits, circled/enclosed numbers, and Roman
        // numerals are not ideographic (no C3_IDEOGRAPH) and pass unconditionally.
        // Non-CP950 CJK ideographs (C3_IDEOGRAPH) must still be filtered.
        TEST_METHOD(UT_09_20_FilterLine_NumericSymbol_PassesFilter)
        {
            std::wstring content =
                L"%gen_inp\n%chardef begin\n"
                L"a \u2153\n"   // VULGAR FRACTION ONE THIRD (U+2153, No) — not CP950, not C3_IDEOGRAPH — must pass
                L"a \u3400\n"   // CJK Extension A (U+3400) — C3_IDEOGRAPH, not CP950 — must be filtered
                L"%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc, L"Source file created");
            CMemoryFile mem(pSrc);
            Assert::IsTrue(BufferContains(mem,  L"\u2153"), L"Vulgar fraction must pass filter");
            Assert::IsFalse(BufferContains(mem, L"\u3400"), L"Non-CP950 CJK ideograph (C3_IDEOGRAPH) must be filtered");
            delete pSrc;
        }

        // ────────────────────────────────────────────────────────────────
        //  Disk cache tests (UT-09-21 through UT-09-25)
        // ────────────────────────────────────────────────────────────────

        // UT-09-21: After the first filter, a cache file (<name>-Big5.cin) is created on disk.
        TEST_METHOD(UT_09_21_Cache_FileCreated)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            CMemoryFile mem(pSrc);
            const WCHAR* buf = mem.GetReadBufferPointer();
            Assert::IsNotNull(buf, L"Buffer must be valid after filtering");

            // Cache file should be src_N-Big5.cin alongside the source
            std::wstring cachePath = srcPath;
            size_t dot = cachePath.rfind(L'.');
            Assert::IsTrue(dot != std::wstring::npos, L"Source must have extension");
            cachePath.insert(dot, L"-Big5");

            Assert::IsTrue(GetFileAttributesW(cachePath.c_str()) != INVALID_FILE_ATTRIBUTES,
                L"Cache file must exist after first filter");

            delete pSrc;
        }

        // UT-09-22: Cache file's first line contains %src_mtime with the source file's st_mtime.
        TEST_METHOD(UT_09_22_Cache_HeaderMatchesSourceMtime)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            CMemoryFile mem(pSrc);
            mem.GetReadBufferPointer();

            // Get source mtime
            struct _stat srcStat;
            Assert::IsTrue(_wstat(srcPath.c_str(), &srcStat) == 0, L"Source stat must succeed");

            // Read cache file header
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");

            HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE, L"Cache file must be openable");

            DWORD fileSize = ::GetFileSize(hFile, NULL);
            BYTE* raw = new BYTE[fileSize];
            DWORD bytesRead;
            ReadFile(hFile, raw, fileSize, &bytesRead, NULL);
            CloseHandle(hFile);

            // Skip BOM (2 bytes), parse first line
            const WCHAR* wBuf = (const WCHAR*)(raw + sizeof(WCHAR));
            std::wstring header(wBuf, (bytesRead - sizeof(WCHAR)) / sizeof(WCHAR));
            delete[] raw;

            // Parse: "%src_mtime <decimal64>\n..."
            WCHAR expected[64];
            StringCchPrintfW(expected, _countof(expected), L"%%src_mtime %lld\n",
                (__int64)srcStat.st_mtime);
            Assert::IsTrue(header.substr(0, wcslen(expected)) == expected,
                L"Cache header must contain %src_mtime matching source file's st_mtime");

            delete pSrc;
        }

        // UT-09-23: Second CMemoryFile on the same source loads from cache; buffer content matches.
        TEST_METHOD(UT_09_23_Cache_Reused_ContentMatches)
        {
            std::wstring content =
                L"%gen_inp\n%chardef begin\na \u4E2D\nb \u5927\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(content);
            Assert::IsNotNull(pSrc);

            // First CMemoryFile: filters and writes cache
            CMemoryFile mem1(pSrc);
            const WCHAR* buf1 = mem1.GetReadBufferPointer();
            Assert::IsNotNull(buf1);
            DWORD_PTR size1 = mem1.GetFileSize();
            std::wstring bufStr1(buf1, size1 / sizeof(WCHAR));

            // Second CMemoryFile: should load from cache
            CMemoryFile mem2(pSrc);
            const WCHAR* buf2 = mem2.GetReadBufferPointer();
            Assert::IsNotNull(buf2);
            DWORD_PTR size2 = mem2.GetFileSize();
            std::wstring bufStr2(buf2, size2 / sizeof(WCHAR));

            Assert::AreEqual(bufStr1, bufStr2,
                L"Buffer from cache must match buffer from filtering");

            delete pSrc;
        }

        // UT-09-24: When source file changes, cache is invalidated and rebuilt.
        TEST_METHOD(UT_09_24_Cache_Invalidated_OnSourceChange)
        {
            std::wstring contentA =
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n";
            CFile* pSrc = MakeSourceFile(contentA);
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            // First CMemoryFile: filters and writes cache
            {
                CMemoryFile mem(pSrc);
                mem.GetReadBufferPointer();
            }

            // Sleep to ensure filesystem timestamps differ
            Sleep(1100);

            // Rewrite source with different content (U+5927 = 大, also CP950)
            std::wstring contentB =
                L"%gen_inp\n%chardef begin\nb \u5927\n%chardef end\n";
            RewriteFile(srcPath, contentB);

            // New CMemoryFile on the same source CFile — cache should be invalid
            CMemoryFile mem2(pSrc);
            const WCHAR* buf = mem2.GetReadBufferPointer();
            Assert::IsNotNull(buf);

            DWORD_PTR charLen = mem2.GetFileSize() / sizeof(WCHAR);
            std::wstring bufStr(buf, charLen);
            Assert::IsTrue(bufStr.find(L"\u5927") != std::wstring::npos,
                L"After source change, rebuilt buffer must contain new content (大)");
            Assert::IsTrue(bufStr.find(L"\u4E2D") == std::wstring::npos,
                L"After source change, old content (中) must not be present");

            delete pSrc;
        }

        // UT-09-25: Corrupt cache file (invalid header) is ignored; falls back to filtering.
        TEST_METHOD(UT_09_25_Cache_Corrupt_FallsBackToFiltering)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            // First CMemoryFile: filters and writes cache
            {
                CMemoryFile mem(pSrc);
                mem.GetReadBufferPointer();
            }

            // Corrupt the cache file with garbage
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hFile != INVALID_HANDLE_VALUE);
            const char garbage[] = "THIS IS NOT A VALID CACHE FILE";
            DWORD bw;
            WriteFile(hFile, garbage, sizeof(garbage), &bw, nullptr);
            CloseHandle(hFile);

            // New CMemoryFile — corrupt cache should be rejected, falls back to filtering
            CMemoryFile mem2(pSrc);
            const WCHAR* buf = mem2.GetReadBufferPointer();
            Assert::IsNotNull(buf, L"Must still produce a valid buffer despite corrupt cache");
            Assert::IsTrue(BufferContains(mem2, L"\u4E2D"),
                L"Fallback filtering must produce correct results");

            delete pSrc;
        }

        // UT-09-26: Source file without extension — BuildCachePath no-extension branch.
        TEST_METHOD(UT_09_26_Cache_NoExtension_CacheCreated)
        {
            // Create source file without extension
            std::wstring filename = testDir + L"src_noext";
            std::wstring content = L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n";

            HANDLE hf = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hf != INVALID_HANDLE_VALUE);
            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hf, bom, 2, &bw, nullptr);
            WriteFile(hf, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hf);

            CFile* pSrc = new (std::nothrow) CFile();
            Assert::IsNotNull(pSrc);
            Assert::IsTrue(pSrc->CreateFile(filename.c_str(), GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ));

            CMemoryFile mem(pSrc);
            Assert::IsNotNull(mem.GetReadBufferPointer());

            // Cache should be named "src_noext-Big5" (no extension)
            std::wstring cachePath = filename + L"-Big5";
            Assert::IsTrue(GetFileAttributesW(cachePath.c_str()) != INVALID_FILE_ATTRIBUTES,
                L"Cache file must exist for extensionless source");

            delete pSrc;
        }

        // UT-09-27: Cache file with only 1 byte — IsCacheValid rejects (fileSize < sizeof(WCHAR)).
        TEST_METHOD(UT_09_27_Cache_TinyFile_Rejected)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            // First filter to create cache
            { CMemoryFile mem(pSrc); mem.GetReadBufferPointer(); }

            // Replace cache with 1-byte file
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            HANDLE hf = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hf != INVALID_HANDLE_VALUE);
            BYTE oneByte = 0x42;
            DWORD bw;
            WriteFile(hf, &oneByte, 1, &bw, nullptr);
            CloseHandle(hf);

            // Must reject and fall back to filtering
            CMemoryFile mem2(pSrc);
            Assert::IsNotNull(mem2.GetReadBufferPointer());
            Assert::IsTrue(BufferContains(mem2, L"\u4E2D"));

            delete pSrc;
        }

        // UT-09-28: Cache file with only BOM (2 bytes) — IsCacheValid rejects (bytesRead < 4).
        TEST_METHOD(UT_09_28_Cache_BOMOnly_Rejected)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            { CMemoryFile mem(pSrc); mem.GetReadBufferPointer(); }

            // Replace cache with BOM-only file (2 bytes)
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            HANDLE hf = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hf != INVALID_HANDLE_VALUE);
            BYTE bom[2] = { 0xFF, 0xFE };
            DWORD bw;
            WriteFile(hf, bom, sizeof(bom), &bw, nullptr);
            CloseHandle(hf);

            CMemoryFile mem2(pSrc);
            Assert::IsNotNull(mem2.GetReadBufferPointer());
            Assert::IsTrue(BufferContains(mem2, L"\u4E2D"));

            delete pSrc;
        }

        // UT-09-29: Cache file with %src_mtime but no number — IsCacheValid rejects (numLen == 0).
        TEST_METHOD(UT_09_29_Cache_EmptyMtimeValue_Rejected)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            { CMemoryFile mem(pSrc); mem.GetReadBufferPointer(); }

            // Replace cache with valid BOM + "%src_mtime \n" (no number)
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            std::wstring badHeader = L"%src_mtime \nsome data\n";
            HANDLE hf = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hf != INVALID_HANDLE_VALUE);
            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hf, bom, sizeof(bom), &bw, nullptr);
            WriteFile(hf, badHeader.c_str(), (DWORD)(badHeader.size() * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hf);

            CMemoryFile mem2(pSrc);
            Assert::IsNotNull(mem2.GetReadBufferPointer());
            Assert::IsTrue(BufferContains(mem2, L"\u4E2D"));

            delete pSrc;
        }

        // UT-09-30: Cache file with header only, no data after newline — TryLoadCache rejects (dataLen == 0).
        TEST_METHOD(UT_09_30_Cache_HeaderOnly_NoData_Rejected)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            { CMemoryFile mem(pSrc); mem.GetReadBufferPointer(); }

            // Get source mtime to build a valid header
            struct _stat srcStat;
            _wstat(srcPath.c_str(), &srcStat);

            // Replace cache with valid header but no data
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            WCHAR header[64];
            StringCchPrintfW(header, _countof(header), L"%%src_mtime %lld\n", (__int64)srcStat.st_mtime);

            HANDLE hf = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hf != INVALID_HANDLE_VALUE);
            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hf, bom, sizeof(bom), &bw, nullptr);
            WriteFile(hf, header, (DWORD)(wcslen(header) * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hf);

            CMemoryFile mem2(pSrc);
            Assert::IsNotNull(mem2.GetReadBufferPointer());
            Assert::IsTrue(BufferContains(mem2, L"\u4E2D"));

            delete pSrc;
        }

        // UT-09-31: Cache file locked during WriteCacheToDisk — MoveFileExW fails, temp deleted.
        TEST_METHOD(UT_09_31_Cache_LockedDuringWrite_TempDeleted)
        {
            CFile* pSrc = MakeSourceFile(
                L"%gen_inp\n%chardef begin\na \u4E2D\n%chardef end\n");
            Assert::IsNotNull(pSrc);
            std::wstring srcPath = createdFiles.back();

            // First filter — creates cache
            { CMemoryFile mem(pSrc); mem.GetReadBufferPointer(); }

            // Lock the cache file exclusively
            std::wstring cachePath = srcPath;
            cachePath.insert(cachePath.rfind(L'.'), L"-Big5");
            HANDLE hLock = CreateFileW(cachePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                0 /* no sharing */, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(hLock != INVALID_HANDLE_VALUE, L"Must be able to lock cache file");

            // Sleep to ensure different mtime
            Sleep(1100);

            // Rewrite source to force re-filter + WriteCacheToDisk
            RewriteFile(srcPath, L"%gen_inp\n%chardef begin\nb \u5927\n%chardef end\n");

            // This should filter successfully but MoveFileExW should fail (cache locked)
            CMemoryFile mem2(pSrc);
            const WCHAR* buf = mem2.GetReadBufferPointer();
            Assert::IsNotNull(buf, L"Filtering must succeed even when cache is locked");

            // Verify temp file was cleaned up
            std::wstring tmpPath = cachePath + L".tmp";
            Assert::IsTrue(GetFileAttributesW(tmpPath.c_str()) == INVALID_FILE_ATTRIBUTES,
                L"Temp file must be deleted after failed rename");

            CloseHandle(hLock);
            delete pSrc;
        }
    };
}
