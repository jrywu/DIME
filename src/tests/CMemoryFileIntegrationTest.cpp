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
* IT-MF: CMemoryFile Integration Tests
*
* Target Module: File.cpp / Target Class: CMemoryFile
* Namespace:     DIMEIntegratedTests
*
* CIN files:  "keycode"<TAB>"character"   (quoted, tab separator)
* TTS files:  "key"="character"           (quoted, = separator)
* Both formats are supported by FilterLine.
*
* IT-MF-01: CP950 filter reduces Array.cin from Unicode range to Big5 range.
*   Array.cin has ~32 000 CIN entries including CJK Extension characters outside
*   Big5/CP950. After filtering, only Big5-encodable entries survive (~15 000).
*
* IT-MF-02: CP950 filter reduces the Windows built-in Dayi TTS table.
*   TableTextServiceDaYi.txt is the system Dayi input table installed under
*   %ProgramW6432%\Windows NT\TableTextService\. It uses = as the separator
*   ("key"="char") and contains non-Big5 CJK Extension entries that must be
*   filtered out when running in Big5 scope.
*
* IT-MF-03: Disk cache — Array-Big5.cin on disk matches in-memory line count.
*   After CMemoryFile filters Array.cin, the cache file Array-Big5.cin is written
*   to %APPDATA%\DIME\. This test reads the cache file back from disk as a plain
*   CFile and asserts the data line count matches the in-memory filtered buffer.
*   Then it creates a second CMemoryFile from the same source — which loads from
*   cache — and asserts the line count also matches.
*
* IT-MF-04: Cache invalidation — touching Array.cin triggers cache rebuild.
*   Uses _wutime to advance the source Array.cin mtime by 2 seconds, then
*   creates a new CMemoryFile. The cache must be rebuilt with the new %src_mtime
*   value matching the touched source timestamp.  The original mtime is restored
*   after the test.
*
* IT-MF-05: Disk cache — TableTextServiceDaYi-Big5.txt on disk matches
*   in-memory line count.  Same as IT-MF-03 but with the DAYI TTS table
*   (%APPDATA%\DIME\TableTextServiceDaYi.txt) as the source.
*
* IT-MF-06: Cache invalidation — touching TableTextServiceDaYi.txt triggers
*   cache rebuild.  Same as IT-MF-04 but with the DAYI TTS table.
*
* All IT-MF tests are SKIPPED when the required files are not present
* (e.g. CI runners without DIME installed).
*
* Reference: docs/BIG5_FILTER.md
********************************************************************************/

#include "pch.h"
#include "CppUnitTest.h"
#include "../File.h"
#include <sys/utime.h>   // _wutime, _utimbuf — used by IT-MF-04 to touch source timestamp

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEIntegratedTests
{
    TEST_CLASS(CMemoryFileIntegrationTests)
    {
    private:
        // Count data lines in a WCHAR buffer.
        // A data line is one keycode->character mapping entry:
        //   CIN format: key<TAB>char  (tab separator)
        //   TTS format: "key"="char"  (= separator)
        // Lines containing a tab or '=' are counted as data lines.
        static size_t CountDataLines(const WCHAR* buf, DWORD_PTR byteLen)
        {
            if (!buf || byteLen == 0) return 0;
            const WCHAR* end = buf + byteLen / sizeof(WCHAR);
            const WCHAR* rp  = buf;
            size_t count = 0;
            while (rp < end)
            {
                bool isData = false;
                while (rp < end && *rp != L'\n')
                {
                    if (*rp == L'\t' || *rp == L'=') isData = true;
                    ++rp;
                }
                if (isData) ++count;
                if (rp < end) ++rp;
            }
            return count;
        }

        // Write a UTF-16LE file (with BOM) from a wstring.  Returns TRUE on success.
        static bool WriteUTF16File(PCWSTR path, const std::wstring& content)
        {
            HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf == INVALID_HANDLE_VALUE) return false;
            DWORD bw;
            BYTE bom[2] = { 0xFF, 0xFE };
            WriteFile(hf, bom, sizeof(bom), &bw, nullptr);
            WriteFile(hf, content.c_str(), (DWORD)(content.size() * sizeof(WCHAR)), &bw, nullptr);
            CloseHandle(hf);
            return true;
        }

        // Count lines whose extracted value is exactly one non-ASCII (CJK) character.
        // Uses the same field-extraction logic as FilterLine:
        //   - Both CIN format ("keycode"<TAB>"char") and TTS format ("key"="char") supported.
        //   - Section headers ([Section]), directives (%...), and empty lines are excluded.
        //   - Multi-char values (phrases) and ASCII values are NOT counted.
        //
        // sectionFilter: when non-null, count only entries within the named TTS section
        //   (e.g. L"Text" counts only lines inside [Text]…[NextSection]).
        //   When null, all sections are counted.
        static size_t CountSingleCJKEntries(const WCHAR* buf, DWORD_PTR byteLen,
                                             const WCHAR* sectionFilter = nullptr)
        {
            if (!buf || byteLen == 0) return 0;
            const WCHAR* end = buf + byteLen / sizeof(WCHAR);
            const WCHAR* rp  = buf;
            size_t count = 0;

            // Section tracking (used when sectionFilter != nullptr)
            bool inTarget = (sectionFilter == nullptr);  // no filter → always in target

            while (rp < end)
            {
                // Determine line extent
                const WCHAR* lineStart = rp;
                while (rp < end && *rp != L'\n') ++rp;
                DWORD_PTR lineLen = (DWORD_PTR)(rp - lineStart);
                if (rp < end) ++rp;   // step past \n

                // Strip trailing \r (CRLF files)
                if (lineLen > 0 && lineStart[lineLen - 1] == L'\r') --lineLen;

                const WCHAR* ls  = lineStart;
                const WCHAR* le  = lineStart + lineLen;

                // Skip leading whitespace
                const WCHAR* q = ls;
                while (q < le && (*q == L' ' || *q == L'\t')) ++q;
                DWORD_PTR rem = (DWORD_PTR)(le - q);

                // ── Update TTS section state ───────────────────────────────────
                if (sectionFilter != nullptr && rem > 0 && *q == L'[')
                {
                    const WCHAR* s    = q + 1;
                    DWORD_PTR    sr   = rem - 1;
                    size_t       slen = wcslen(sectionFilter);
                    inTarget = (sr >= slen + 1 &&
                                _wcsnicmp(s, sectionFilter, slen) == 0 &&
                                s[slen] == L']');
                    continue;   // section headers are never data entries
                }

                if (!inTarget) continue;

                // Skip empty lines, directives, and section headers
                if (q >= le || *q == L'%' || *q == L'[') continue;

                // Skip keycode token (quoted or unquoted)
                if (*q == L'"')
                {
                    ++q;
                    while (q < le && *q != L'"') ++q;
                    if (q < le) ++q;
                }
                else
                {
                    while (q < le && *q != L' ' && *q != L'\t' && *q != L'=') ++q;
                }

                // Skip separator (spaces, tabs, or '=')
                while (q < le && (*q == L' ' || *q == L'\t' || *q == L'=')) ++q;

                // Extract value (quoted or unquoted)
                const WCHAR* valStart;
                const WCHAR* valEnd;
                if (q < le && *q == L'"')
                {
                    ++q;
                    valStart = q;
                    while (q < le && *q != L'"') ++q;
                    valEnd = q;
                }
                else
                {
                    valStart = q;
                    while (q < le && *q != L' ' && *q != L'\t') ++q;
                    valEnd = q;
                }

                // Count only single non-ASCII characters (CJK range)
                if ((DWORD_PTR)(valEnd - valStart) == 1 && (WCHAR)valStart[0] > 0x007F)
                    ++count;
            }
            return count;
        }

    public:

        // IT-MF-01: CP950 filter reduces Array.cin from Unicode range to Big5 range.
        //
        // Array.cin contains ~32 000 keymapping entries spanning Big5 characters AND
        // CJK Extension characters (Ext-B, Ext-C, etc.) that are outside CP950/Big5.
        // After filtering, only Big5-encodable entries survive (~15 000 lines).
        //
        // The dual-filter comparison proves that additional non-CP950 (CJK Extension A)
        // entries injected after the natural filter are also removed — without the
        // artificial "injected entries are the ONLY removed entries" assumption.
        //
        // Skipped when %APPDATA%\DIME\Array.cin is not installed.
        TEST_METHOD(IT_MF_01_RealArrayCin_FilterReducesToBig5Range)
        {
            // ── locate Array.cin ──────────────────────────────────────────────────
            WCHAR appData[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE);

            WCHAR cinPath[MAX_PATH] = {};
            StringCchPrintfW(cinPath, MAX_PATH, L"%s\\DIME\\Array.cin", appData);

            if (GetFileAttributesW(cinPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage(
                    "IT-MF-01 SKIPPED: Array.cin not found at %APPDATA%\\DIME\\Array.cin");
                return;
            }

            // ── (a) load raw Array.cin ────────────────────────────────────────────
            CFile* pRaw = new (std::nothrow) CFile();
            Assert::IsNotNull(pRaw, L"CFile allocation must succeed");
            Assert::IsTrue(
                pRaw->CreateFile(cinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"Array.cin must open for reading");

            const WCHAR* rawBuf = pRaw->GetReadBufferPointer();
            Assert::IsNotNull(rawBuf, L"Raw buffer must load");

            DWORD_PTR rawBytes = pRaw->GetFileSize();
            size_t rawLines = CountDataLines(rawBuf, rawBytes);

            // Save raw content for augmented-file construction below
            std::wstring content(rawBuf, rawBytes / sizeof(WCHAR));

            // ── (b) filter raw Array.cin via CMemoryFile ──────────────────────────
            CMemoryFile memRaw(pRaw);
            const WCHAR* rawFiltBuf = memRaw.GetReadBufferPointer();
            Assert::IsNotNull(rawFiltBuf, L"Filtered raw buffer must be non-null");
            size_t rawFiltLines = CountDataLines(rawFiltBuf, memRaw.GetFileSize());

            delete pRaw;

            // ── (c) build augmented content: raw + 5 CJK Extension A lines ──────────
            // U+3400..U+3404 are CJK Extension A characters with C3_IDEOGRAPH set —
            // not encodable in CP950, so the Big5 filter must remove them.
            // Use unquoted bare format (also handled by FilterLine).
            const std::wstring injection =
                L"z\t\u3400\n"   // 㐀 — CJK Extension A U+3400 — C3_IDEOGRAPH, not CP950
                L"z\t\u3401\n"   // 㐁 — CJK Extension A U+3401 — C3_IDEOGRAPH, not CP950
                L"z\t\u3402\n"   // 㐂 — CJK Extension A U+3402 — C3_IDEOGRAPH, not CP950
                L"z\t\u3403\n"   // 㐃 — CJK Extension A U+3403 — C3_IDEOGRAPH, not CP950
                L"z\t\u3404\n";  // 㐄 — CJK Extension A U+3404 — C3_IDEOGRAPH, not CP950

            const std::wstring marker = L"%chardef end";
            size_t markPos = content.find(marker);
            if (markPos != std::wstring::npos)
                content.insert(markPos, injection);
            else
                content += injection;

            // ── write augmented file to %TEMP% ────────────────────────────────────
            WCHAR tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);
            WCHAR augPath[MAX_PATH] = {};
            StringCchPrintfW(augPath, MAX_PATH, L"%s\\IT_MF_01_aug.cin", tempDir);

            Assert::IsTrue(WriteUTF16File(augPath, content),
                L"Augmented temp file create must succeed");

            // ── (d) filter augmented file via CMemoryFile ─────────────────────────
            CFile* pAug = new (std::nothrow) CFile();
            Assert::IsNotNull(pAug, L"Augmented CFile allocation must succeed");
            Assert::IsTrue(
                pAug->CreateFile(augPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"Augmented file must open for reading");

            size_t augLines = CountDataLines(pAug->GetReadBufferPointer(), pAug->GetFileSize());

            CMemoryFile memAug(pAug);
            const WCHAR* augFiltBuf = memAug.GetReadBufferPointer();
            Assert::IsNotNull(augFiltBuf, L"Filtered augmented buffer must be non-null");
            size_t augFiltLines = CountDataLines(augFiltBuf, memAug.GetFileSize());

            delete pAug;
            DeleteFileW(augPath);

            // ── log ───────────────────────────────────────────────────────────────
            WCHAR msg[512] = {};
            StringCchPrintfW(msg, 512,
                L"IT-MF-01: rawLines=%llu  rawFiltLines=%llu  removed=%llu"
                L"  augLines=%llu  augFiltLines=%llu",
                (unsigned long long)rawLines,
                (unsigned long long)rawFiltLines,
                (unsigned long long)(rawLines >= rawFiltLines ? rawLines - rawFiltLines : 0),
                (unsigned long long)augLines,
                (unsigned long long)augFiltLines);
            Logger::WriteMessage(msg);

            // ── assertions ────────────────────────────────────────────────────────
            Assert::IsTrue(rawLines > 0,
                L"Array.cin must have data entries");

            // Filter must reduce Array.cin from Unicode range to Big5 range
            Assert::IsTrue(rawFiltLines < rawLines,
                L"Filter must remove non-Big5 CJK Extension entries from Array.cin");

            // All standard Big5 characters must be preserved
            Assert::IsTrue(rawFiltLines >= 13053,
                L"At least 13 053 Big5 mapping lines must survive filtering");

            // Result must be in Big5 range (like Phone.cin ~15 000 lines)
            Assert::IsTrue(rawFiltLines <= 16000,
                L"Filtered line count must be in Big5 range (<= 16 000)");

            // Augmented file (raw + 5 CJK Extension A entries) must produce the same filtered count:
            // the 5 injected non-CP950 C3_IDEOGRAPH entries are also removed by the filter.
            Assert::AreEqual(rawFiltLines, augFiltLines,
                L"5 injected CJK Extension A entries must also be filtered (augFiltLines == rawFiltLines)");
        }

        // IT-MF-02: CP950 filter reduces the Windows built-in Dayi TTS table.
        //
        // TableTextServiceDaYi.txt is the system Dayi input table installed under
        // %ProgramW6432%\Windows NT\TableTextService\. It uses '=' as the separator
        // with both fields quoted: "key"="character".
        //
        // The file contains CJK Extension characters outside Big5/CP950 that must be
        // filtered out when DIME operates in Big5 scope.
        //
        // Path mirrors SetupCompositionProcessorEngine::SetupDictionaryFile():
        //   GetEnvironmentVariable("ProgramW6432") + \Windows NT\TableTextService\TableTextServiceDaYi.txt
        //
        // SetupReadBuffer applies FilterLine only within the [Text] section.
        // This test counts single non-ASCII CJK entries exclusively in [Text]
        // (via CountSingleCJKEntries with sectionFilter=L"Text") to measure that
        // section in isolation — phrase and radical entries are excluded from the tally.
        //
        // Assertions ([Text] single-CJK entries only):
        //   rawLines  > 0           — [Text] section has single-CJK entries
        //   filtLines < rawLines    — filter removed non-CP950 entries from [Text]
        //   filtLines >= 13 053     — standard Big5 character count preserved
        //   filtLines <= 16 000     — result is in Big5 range (not Unicode-wide)
        //
        // Skipped when the TTS file is not present.
        TEST_METHOD(IT_MF_02_DayiTTS_FilterReducesToBig5Range)
        {
            // ── locate TableTextServiceDaYi.txt (same logic as SetupDictionaryFile) ─
            WCHAR progFiles[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"ProgramW6432", progFiles, MAX_PATH) == 0)
                GetEnvironmentVariableW(L"ProgramFiles", progFiles, MAX_PATH);

            WCHAR ttsPath[MAX_PATH] = {};
            StringCchPrintfW(ttsPath, MAX_PATH,
                L"%s\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt", progFiles);

            if (GetFileAttributesW(ttsPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage("IT-MF-02 SKIPPED: TableTextServiceDaYi.txt not found");
                return;
            }

            // ── load raw TTS file ─────────────────────────────────────────────────
            CFile* pRaw = new (std::nothrow) CFile();
            Assert::IsNotNull(pRaw, L"CFile allocation must succeed");
            Assert::IsTrue(
                pRaw->CreateFile(ttsPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"TableTextServiceDaYi.txt must open for reading");

            const WCHAR* rawBuf = pRaw->GetReadBufferPointer();
            Assert::IsNotNull(rawBuf, L"Raw TTS buffer must load");
            size_t rawLines = CountSingleCJKEntries(rawBuf, pRaw->GetFileSize(), L"Text");

            // ── apply CP950 filter via CMemoryFile ────────────────────────────────
            CMemoryFile mem(pRaw);
            const WCHAR* filtBuf = mem.GetReadBufferPointer();
            Assert::IsNotNull(filtBuf, L"Filtered TTS buffer must be non-null");
            size_t filtLines = CountSingleCJKEntries(filtBuf, mem.GetFileSize(), L"Text");

            delete pRaw;

            WCHAR msg[256] = {};
            StringCchPrintfW(msg, 256,
                L"IT-MF-02 DayiTTS [Text] section: rawCJK=%llu  filtCJK=%llu  removed=%llu",
                (unsigned long long)rawLines,
                (unsigned long long)filtLines,
                (unsigned long long)(rawLines > filtLines ? rawLines - filtLines : 0));
            Logger::WriteMessage(msg);

            Assert::IsTrue(rawLines > 0,
                L"[Text] section must have single-CJK entries");
            Assert::IsTrue(filtLines < rawLines,
                L"Filter must remove non-CP950 entries from Dayi TTS [Text] section");
            Assert::IsTrue(filtLines >= 13053,
                L"At least 13 053 Big5 entries must survive filtering");
            Assert::IsTrue(filtLines <= 16000,
                L"Filtered [Text] CJK entry count must be in Big5 range (<= 16 000)");
        }

        // IT-MF-03: Disk cache — Array-Big5.cin on disk matches in-memory line count.
        //
        // (a) Filter Array.cin via CMemoryFile and count data lines in the in-memory buffer.
        // (b) Read the resulting Array-Big5.cin cache file from disk as a plain CFile,
        //     skip the %src_mtime header line, and count data lines — must match (a).
        // (c) Create a second CMemoryFile from the same source (loads from cache).
        //     Count data lines — must also match (a).
        //
        // Skipped when %APPDATA%\DIME\Array.cin is not installed.
        TEST_METHOD(IT_MF_03_CacheFile_LineCountMatchesMemory)
        {
            // ── locate Array.cin ────────────────────────────────────────────────
            WCHAR appData[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE);

            WCHAR cinPath[MAX_PATH] = {};
            StringCchPrintfW(cinPath, MAX_PATH, L"%s\\DIME\\Array.cin", appData);

            if (GetFileAttributesW(cinPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage(
                    "IT-MF-03 SKIPPED: Array.cin not found at %APPDATA%\\DIME\\Array.cin");
                return;
            }

            WCHAR cachePath[MAX_PATH] = {};
            StringCchPrintfW(cachePath, MAX_PATH, L"%s\\DIME\\Array-Big5.cin", appData);

            // ── (a) filter Array.cin → in-memory buffer ─────────────────────────
            CFile* pSrc = new (std::nothrow) CFile();
            Assert::IsNotNull(pSrc);
            Assert::IsTrue(
                pSrc->CreateFile(cinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"Array.cin must open");

            CMemoryFile mem1(pSrc);
            const WCHAR* memBuf = mem1.GetReadBufferPointer();
            Assert::IsNotNull(memBuf, L"Filtered buffer must be non-null");
            size_t memLines = CountDataLines(memBuf, mem1.GetFileSize());

            // ── (b) read cache file from disk and count lines ───────────────────
            Assert::IsTrue(GetFileAttributesW(cachePath) != INVALID_FILE_ATTRIBUTES,
                L"Array-Big5.cin cache file must exist after filtering");

            CFile* pCache = new (std::nothrow) CFile();
            Assert::IsNotNull(pCache);
            Assert::IsTrue(
                pCache->CreateFile(cachePath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"Array-Big5.cin must open for reading");

            const WCHAR* cacheBuf = pCache->GetReadBufferPointer();
            Assert::IsNotNull(cacheBuf, L"Cache buffer must load");
            DWORD_PTR cacheBytes = pCache->GetFileSize();

            // Skip the %src_mtime header line before counting
            const WCHAR* cacheEnd = cacheBuf + cacheBytes / sizeof(WCHAR);
            const WCHAR* dataStart = cacheBuf;
            while (dataStart < cacheEnd && *dataStart != L'\n') ++dataStart;
            if (dataStart < cacheEnd) ++dataStart;  // step past '\n'
            DWORD_PTR dataBytes = (DWORD_PTR)(cacheEnd - dataStart) * sizeof(WCHAR);

            size_t cacheLines = CountDataLines(dataStart, dataBytes);
            delete pCache;

            // ── (c) create second CMemoryFile (loads from cache) ────────────────
            CMemoryFile mem2(pSrc);
            const WCHAR* mem2Buf = mem2.GetReadBufferPointer();
            Assert::IsNotNull(mem2Buf, L"Second CMemoryFile buffer must be non-null");
            size_t mem2Lines = CountDataLines(mem2Buf, mem2.GetFileSize());

            delete pSrc;

            // ── log ─────────────────────────────────────────────────────────────
            WCHAR msg[256] = {};
            StringCchPrintfW(msg, 256,
                L"IT-MF-03: memLines=%llu  cacheLines=%llu  mem2Lines=%llu",
                (unsigned long long)memLines,
                (unsigned long long)cacheLines,
                (unsigned long long)mem2Lines);
            Logger::WriteMessage(msg);

            // ── assertions ──────────────────────────────────────────────────────
            Assert::IsTrue(memLines > 0,
                L"Filtered buffer must have data lines");
            Assert::AreEqual(memLines, cacheLines,
                L"Cache file on disk must have same data line count as in-memory buffer");
            Assert::AreEqual(memLines, mem2Lines,
                L"Second CMemoryFile (from cache) must have same data line count");
        }

        // IT-MF-04: Cache invalidation — touching Array.cin triggers cache rebuild
        //           with updated %src_mtime.
        //
        // (a) Filter Array.cin to ensure cache exists with current mtime.
        // (b) Read %src_mtime from the cache file.
        // (c) Touch Array.cin: advance mtime by 2 seconds via _wutime.
        // (d) Create new CMemoryFile — cache is stale, must re-filter and rewrite.
        // (e) Read %src_mtime from the rebuilt cache — must match the new source mtime.
        // (f) Restore Array.cin's original mtime so the test is non-destructive.
        //
        // Skipped when %APPDATA%\DIME\Array.cin is not installed.
        TEST_METHOD(IT_MF_04_CacheInvalidated_OnSourceMtimeChange)
        {
            // ── locate Array.cin ────────────────────────────────────────────────
            WCHAR appData[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE);

            WCHAR cinPath[MAX_PATH] = {};
            StringCchPrintfW(cinPath, MAX_PATH, L"%s\\DIME\\Array.cin", appData);

            if (GetFileAttributesW(cinPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage(
                    "IT-MF-04 SKIPPED: Array.cin not found at %APPDATA%\\DIME\\Array.cin");
                return;
            }

            WCHAR cachePath[MAX_PATH] = {};
            StringCchPrintfW(cachePath, MAX_PATH, L"%s\\DIME\\Array-Big5.cin", appData);

            // ── (a) ensure cache exists ─────────────────────────────────────────
            {
                CFile* pSrc = new (std::nothrow) CFile();
                Assert::IsNotNull(pSrc);
                Assert::IsTrue(
                    pSrc->CreateFile(cinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ));
                CMemoryFile mem(pSrc);
                Assert::IsNotNull(mem.GetReadBufferPointer(),
                    L"Initial filter must succeed");
                delete pSrc;
            }
            Assert::IsTrue(GetFileAttributesW(cachePath) != INVALID_FILE_ATTRIBUTES,
                L"Cache file must exist after initial filter");

            // ── helper lambda: read %src_mtime from cache file ──────────────────
            auto readCacheMtime = [&]() -> __time64_t
            {
                HANDLE hf = ::CreateFileW(cachePath, GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf == INVALID_HANDLE_VALUE) return 0;
                // Read header (BOM + up to 128 WCHARs)
                BYTE headerBuf[260] = {};
                DWORD br = 0;
                ReadFile(hf, headerBuf, sizeof(headerBuf), &br, NULL);
                CloseHandle(hf);
                if (br < 4) return 0;
                // Skip BOM, parse "%src_mtime <decimal64>"
                const WCHAR* w = (const WCHAR*)(headerBuf + 2);
                DWORD_PTR wLen = (br - 2) / sizeof(WCHAR);
                const WCHAR prefix[] = L"%src_mtime ";
                size_t pLen = wcslen(prefix);
                if (wLen < pLen || wcsncmp(w, prefix, pLen) != 0) return 0;
                const WCHAR* numStart = w + pLen;
                WCHAR numStr[32] = {};
                const WCHAR* numEnd = numStart;
                while (numEnd < w + wLen && *numEnd != L'\n' && *numEnd != L'\r') ++numEnd;
                size_t nLen = (size_t)(numEnd - numStart);
                if (nLen == 0 || nLen >= _countof(numStr)) return 0;
                wcsncpy_s(numStr, _countof(numStr), numStart, nLen);
                return _wcstoi64(numStr, nullptr, 10);
            };

            // ── (b) read original %src_mtime from cache ─────────────────────────
            __time64_t origCacheMtime = readCacheMtime();
            Assert::IsTrue(origCacheMtime != 0,
                L"Original cache must have a valid %src_mtime");

            // ── save original source mtime for restoration ──────────────────────
            struct _stat origStat;
            Assert::IsTrue(_wstat(cinPath, &origStat) == 0,
                L"Must be able to stat Array.cin");

            // ── (c) touch Array.cin: advance mtime by 2 seconds ─────────────────
            struct _utimbuf newTimes;
            newTimes.actime  = origStat.st_atime;
            newTimes.modtime = origStat.st_mtime + 2;
            Assert::IsTrue(_wutime(cinPath, &newTimes) == 0,
                L"_wutime must succeed to advance source mtime");

            // ── (d) create new CMemoryFile — cache should be invalidated ────────
            {
                CFile* pSrc = new (std::nothrow) CFile();
                Assert::IsNotNull(pSrc);
                Assert::IsTrue(
                    pSrc->CreateFile(cinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ));
                CMemoryFile mem(pSrc);
                Assert::IsNotNull(mem.GetReadBufferPointer(),
                    L"Re-filter after touch must succeed");
                delete pSrc;
            }

            // ── (e) read new %src_mtime from rebuilt cache ──────────────────────
            __time64_t newCacheMtime = readCacheMtime();

            // ── (f) restore original source mtime ───────────────────────────────
            struct _utimbuf restoreTimes;
            restoreTimes.actime  = origStat.st_atime;
            restoreTimes.modtime = origStat.st_mtime;
            _wutime(cinPath, &restoreTimes);

            // Also rebuild cache with restored mtime so we leave it consistent
            {
                CFile* pSrc = new (std::nothrow) CFile();
                if (pSrc && pSrc->CreateFile(cinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
                {
                    CMemoryFile mem(pSrc);
                    mem.GetReadBufferPointer();
                }
                delete pSrc;
            }

            // ── log ─────────────────────────────────────────────────────────────
            WCHAR msg[256] = {};
            StringCchPrintfW(msg, 256,
                L"IT-MF-04: origCacheMtime=%lld  newCacheMtime=%lld  expectedNew=%lld",
                (__int64)origCacheMtime,
                (__int64)newCacheMtime,
                (__int64)(origStat.st_mtime + 2));
            Logger::WriteMessage(msg);

            // ── assertions ──────────────────────────────────────────────────────
            Assert::IsTrue(newCacheMtime != origCacheMtime,
                L"Cache %src_mtime must change after source mtime was advanced");
            Assert::AreEqual((__int64)(origStat.st_mtime + 2), (__int64)newCacheMtime,
                L"New cache %src_mtime must match touched source mtime (original + 2)");
        }

        // IT-MF-05: Disk cache — TableTextServiceDaYi-Big5.txt on disk matches
        //           in-memory line count.  Same structure as IT-MF-03 but uses
        //           the DAYI TTS table (%APPDATA%\DIME\TableTextServiceDaYi.txt).
        //
        // Skipped when %APPDATA%\DIME\TableTextServiceDaYi.txt is not installed.
        TEST_METHOD(IT_MF_05_DayiTTS_CacheFile_LineCountMatchesMemory)
        {
            // ── locate TableTextServiceDaYi.txt ─────────────────────────────────
            WCHAR appData[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE);

            WCHAR ttsPath[MAX_PATH] = {};
            StringCchPrintfW(ttsPath, MAX_PATH, L"%s\\DIME\\TableTextServiceDaYi.txt", appData);

            if (GetFileAttributesW(ttsPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage(
                    "IT-MF-05 SKIPPED: TableTextServiceDaYi.txt not found at %APPDATA%\\DIME\\");
                return;
            }

            WCHAR cachePath[MAX_PATH] = {};
            StringCchPrintfW(cachePath, MAX_PATH, L"%s\\DIME\\TableTextServiceDaYi-Big5.txt", appData);

            // ── (a) filter TTS → in-memory buffer ───────────────────────────────
            CFile* pSrc = new (std::nothrow) CFile();
            Assert::IsNotNull(pSrc);
            Assert::IsTrue(
                pSrc->CreateFile(ttsPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"TableTextServiceDaYi.txt must open");

            CMemoryFile mem1(pSrc);
            const WCHAR* memBuf = mem1.GetReadBufferPointer();
            Assert::IsNotNull(memBuf, L"Filtered buffer must be non-null");
            size_t memLines = CountDataLines(memBuf, mem1.GetFileSize());

            // ── (b) read cache file from disk and count lines ───────────────────
            Assert::IsTrue(GetFileAttributesW(cachePath) != INVALID_FILE_ATTRIBUTES,
                L"TableTextServiceDaYi-Big5.txt cache file must exist after filtering");

            CFile* pCache = new (std::nothrow) CFile();
            Assert::IsNotNull(pCache);
            Assert::IsTrue(
                pCache->CreateFile(cachePath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ),
                L"TableTextServiceDaYi-Big5.txt must open for reading");

            const WCHAR* cacheBuf = pCache->GetReadBufferPointer();
            Assert::IsNotNull(cacheBuf, L"Cache buffer must load");
            DWORD_PTR cacheBytes = pCache->GetFileSize();

            // Skip the %src_mtime header line before counting
            const WCHAR* cacheEnd = cacheBuf + cacheBytes / sizeof(WCHAR);
            const WCHAR* dataStart = cacheBuf;
            while (dataStart < cacheEnd && *dataStart != L'\n') ++dataStart;
            if (dataStart < cacheEnd) ++dataStart;  // step past '\n'
            DWORD_PTR dataBytes = (DWORD_PTR)(cacheEnd - dataStart) * sizeof(WCHAR);

            size_t cacheLines = CountDataLines(dataStart, dataBytes);
            delete pCache;

            // ── (c) create second CMemoryFile (loads from cache) ────────────────
            CMemoryFile mem2(pSrc);
            const WCHAR* mem2Buf = mem2.GetReadBufferPointer();
            Assert::IsNotNull(mem2Buf, L"Second CMemoryFile buffer must be non-null");
            size_t mem2Lines = CountDataLines(mem2Buf, mem2.GetFileSize());

            delete pSrc;

            // ── log ─────────────────────────────────────────────────────────────
            WCHAR msg[256] = {};
            StringCchPrintfW(msg, 256,
                L"IT-MF-05: memLines=%llu  cacheLines=%llu  mem2Lines=%llu",
                (unsigned long long)memLines,
                (unsigned long long)cacheLines,
                (unsigned long long)mem2Lines);
            Logger::WriteMessage(msg);

            // ── assertions ──────────────────────────────────────────────────────
            Assert::IsTrue(memLines > 0,
                L"Filtered buffer must have data lines");
            Assert::AreEqual(memLines, cacheLines,
                L"Cache file on disk must have same data line count as in-memory buffer");
            Assert::AreEqual(memLines, mem2Lines,
                L"Second CMemoryFile (from cache) must have same data line count");
        }

        // IT-MF-06: Cache invalidation — touching TableTextServiceDaYi.txt triggers
        //           cache rebuild with updated %src_mtime.
        //           Same structure as IT-MF-04 but uses the DAYI TTS table.
        //
        // Skipped when %APPDATA%\DIME\TableTextServiceDaYi.txt is not installed.
        TEST_METHOD(IT_MF_06_DayiTTS_CacheInvalidated_OnSourceMtimeChange)
        {
            // ── locate TableTextServiceDaYi.txt ─────────────────────────────────
            WCHAR appData[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE);

            WCHAR ttsPath[MAX_PATH] = {};
            StringCchPrintfW(ttsPath, MAX_PATH, L"%s\\DIME\\TableTextServiceDaYi.txt", appData);

            if (GetFileAttributesW(ttsPath) == INVALID_FILE_ATTRIBUTES)
            {
                Logger::WriteMessage(
                    "IT-MF-06 SKIPPED: TableTextServiceDaYi.txt not found at %APPDATA%\\DIME\\");
                return;
            }

            WCHAR cachePath[MAX_PATH] = {};
            StringCchPrintfW(cachePath, MAX_PATH, L"%s\\DIME\\TableTextServiceDaYi-Big5.txt", appData);

            // ── (a) ensure cache exists ─────────────────────────────────────────
            {
                CFile* pSrc = new (std::nothrow) CFile();
                Assert::IsNotNull(pSrc);
                Assert::IsTrue(
                    pSrc->CreateFile(ttsPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ));
                CMemoryFile mem(pSrc);
                Assert::IsNotNull(mem.GetReadBufferPointer(),
                    L"Initial filter must succeed");
                delete pSrc;
            }
            Assert::IsTrue(GetFileAttributesW(cachePath) != INVALID_FILE_ATTRIBUTES,
                L"Cache file must exist after initial filter");

            // ── helper lambda: read %src_mtime from cache file ──────────────────
            auto readCacheMtime = [&]() -> __time64_t
            {
                HANDLE hf = ::CreateFileW(cachePath, GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf == INVALID_HANDLE_VALUE) return 0;
                // Read header (BOM + up to 128 WCHARs)
                BYTE headerBuf[260] = {};
                DWORD br = 0;
                ReadFile(hf, headerBuf, sizeof(headerBuf), &br, NULL);
                CloseHandle(hf);
                if (br < 4) return 0;
                // Skip BOM, parse "%src_mtime <decimal64>"
                const WCHAR* w = (const WCHAR*)(headerBuf + 2);
                DWORD_PTR wLen = (br - 2) / sizeof(WCHAR);
                const WCHAR prefix[] = L"%src_mtime ";
                size_t pLen = wcslen(prefix);
                if (wLen < pLen || wcsncmp(w, prefix, pLen) != 0) return 0;
                const WCHAR* numStart = w + pLen;
                WCHAR numStr[32] = {};
                const WCHAR* numEnd = numStart;
                while (numEnd < w + wLen && *numEnd != L'\n' && *numEnd != L'\r') ++numEnd;
                size_t nLen = (size_t)(numEnd - numStart);
                if (nLen == 0 || nLen >= _countof(numStr)) return 0;
                wcsncpy_s(numStr, _countof(numStr), numStart, nLen);
                return _wcstoi64(numStr, nullptr, 10);
            };

            // ── (b) read original %src_mtime from cache ─────────────────────────
            __time64_t origCacheMtime = readCacheMtime();
            Assert::IsTrue(origCacheMtime != 0,
                L"Original cache must have a valid %src_mtime");

            // ── save original source mtime for restoration ──────────────────────
            struct _stat origStat;
            Assert::IsTrue(_wstat(ttsPath, &origStat) == 0,
                L"Must be able to stat TableTextServiceDaYi.txt");

            // ── (c) touch source: advance mtime by 2 seconds ────────────────────
            struct _utimbuf newTimes;
            newTimes.actime  = origStat.st_atime;
            newTimes.modtime = origStat.st_mtime + 2;
            Assert::IsTrue(_wutime(ttsPath, &newTimes) == 0,
                L"_wutime must succeed to advance source mtime");

            // ── (d) create new CMemoryFile — cache should be invalidated ────────
            {
                CFile* pSrc = new (std::nothrow) CFile();
                Assert::IsNotNull(pSrc);
                Assert::IsTrue(
                    pSrc->CreateFile(ttsPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ));
                CMemoryFile mem(pSrc);
                Assert::IsNotNull(mem.GetReadBufferPointer(),
                    L"Re-filter after touch must succeed");
                delete pSrc;
            }

            // ── (e) read new %src_mtime from rebuilt cache ──────────────────────
            __time64_t newCacheMtime = readCacheMtime();

            // ── (f) restore original source mtime ───────────────────────────────
            struct _utimbuf restoreTimes;
            restoreTimes.actime  = origStat.st_atime;
            restoreTimes.modtime = origStat.st_mtime;
            _wutime(ttsPath, &restoreTimes);

            // Also rebuild cache with restored mtime so we leave it consistent
            {
                CFile* pSrc = new (std::nothrow) CFile();
                if (pSrc && pSrc->CreateFile(ttsPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
                {
                    CMemoryFile mem(pSrc);
                    mem.GetReadBufferPointer();
                }
                delete pSrc;
            }

            // ── log ─────────────────────────────────────────────────────────────
            WCHAR msg[256] = {};
            StringCchPrintfW(msg, 256,
                L"IT-MF-06: origCacheMtime=%lld  newCacheMtime=%lld  expectedNew=%lld",
                (__int64)origCacheMtime,
                (__int64)newCacheMtime,
                (__int64)(origStat.st_mtime + 2));
            Logger::WriteMessage(msg);

            // ── assertions ──────────────────────────────────────────────────────
            Assert::IsTrue(newCacheMtime != origCacheMtime,
                L"Cache %src_mtime must change after source mtime was advanced");
            Assert::AreEqual((__int64)(origStat.st_mtime + 2), (__int64)newCacheMtime,
                L"New cache %src_mtime must match touched source mtime (original + 2)");
        }
    };
}
