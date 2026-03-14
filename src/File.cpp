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
//#define DEBUG_PRINT
#include "Globals.h"
#include "Private.h"
#include "File.h"
#include "BaseStructure.h"
#ifdef _DEBUG
#include <ShlObj.h>   // SHGetSpecialFolderPathW — not pulled in without DEBUG_PRINT
#endif

//---------------------------------------------------------------------
//
// ctor
//
//---------------------------------------------------------------------

CFile::CFile(UINT codePage)
{
    _codePage = codePage;
    _fileHandle = nullptr;
    _pReadBuffer = nullptr;
    _fileSize = 0;
    _filePosPointer = 0;
    _pFileName = nullptr;
    memset(&_timeStamp, 0, sizeof(_timeStamp));

}

//---------------------------------------------------------------------
//
// dtor
//
//---------------------------------------------------------------------

CFile::~CFile()
{
    if (_pReadBuffer)
    {
        delete [] _pReadBuffer;
        _pReadBuffer = nullptr;
    }
    if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }
    if (_pFileName)
    {
        delete [] _pFileName;
        _pFileName = nullptr;
    }
}

//---------------------------------------------------------------------
//
// CreateFile
//
//---------------------------------------------------------------------

BOOL CFile::CreateFile(_In_ PCWSTR pFileName, DWORD desiredAccess,
DWORD creationDisposition,
DWORD sharedMode, _Inout_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD flagsAndAttributes, _Inout_opt_ HANDLE templateFileHandle)
{
	debugPrint(L" CFile::CreateFile() filename = %s", pFileName);
    size_t fullPathLen = wcslen(pFileName);
    if (!_pFileName)
    {
        _pFileName = new (std::nothrow) WCHAR[ fullPathLen + 1 ];
    }
    if (!_pFileName)
    {
        return FALSE;
    }

    StringCchCopyN(_pFileName, fullPathLen + 1, pFileName, fullPathLen);

	_fileHandle = ::CreateFile(_pFileName, desiredAccess, sharedMode,
        lpSecurityAttributes, creationDisposition, flagsAndAttributes, templateFileHandle);

    if (_fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

	_wstat(pFileName, & _timeStamp);
    DWORD fileSize = ::GetFileSize(_fileHandle, NULL);
    
    // MS-01: Validate GetFileSize return value
    if (fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(_fileHandle);
        _fileHandle = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    
    // MS-02: Enforce maximum file size for .cin files
    if (fileSize > Global::MAX_CIN_FILE_SIZE)
    {
        CloseHandle(_fileHandle);
        _fileHandle = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    
    _fileSize = fileSize;

    return TRUE;
}

//---------------------------------------------------------------------
//
// SetupReadBuffer
//
//---------------------------------------------------------------------

BOOL CFile::SetupReadBuffer()
{
	
	debugPrint(L" CFile::CreateFile()");
   
	BOOL ret = TRUE;
	const WCHAR* pWideBuffer = nullptr;
	size_t wcharCount = 0;

	DWORD dwNumberOfByteRead = 0;
	if(_fileSize < sizeof(WCHAR) ) 
	{
		ret = FALSE;
		goto errorExit;
	}
	// Safe arithmetic: calculate buffer size safely
	wcharCount = (_fileSize - sizeof(WCHAR)) / sizeof(WCHAR);
	if (wcharCount == 0)
	{
		ret = FALSE;
		goto errorExit;
	}
	
	// Allocate buffer for unicode string (without BOM)
	pWideBuffer = new (std::nothrow) WCHAR[wcharCount];
	if (!pWideBuffer)
	{
		ret = FALSE;
		goto errorExit;
	}

	// Skip unicode byte-order signature and check result
	if (SetFilePointer(_fileHandle, sizeof(WCHAR), NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		delete [] pWideBuffer;
		ret = FALSE;
		goto errorExit;
	}

	if (!ReadFile(_fileHandle, (LPVOID)pWideBuffer, (DWORD)(_fileSize - sizeof(WCHAR)), &dwNumberOfByteRead, NULL) 
		|| !IsTextUnicode(pWideBuffer, dwNumberOfByteRead, NULL))
	{
		delete [] pWideBuffer;
		ret = FALSE;
		goto errorExit;
	}
	

	_fileSize -= sizeof(WCHAR);
	delete [] _pReadBuffer;
	_pReadBuffer = pWideBuffer;

errorExit:
	if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }

    return ret;
}

_Ret_maybenull_
const WCHAR* CFile::GetReadBufferPointer(_Inout_opt_ BOOL *fileReloaded)
{
	debugPrint(L" CFile::GetReadBufferPointer()");
	if(fileReloaded) *fileReloaded = FALSE;
	if (!_pReadBuffer)
	{
		if (!SetupReadBuffer())
		{
			return nullptr;
		}
	}
	else
	{
		struct _stat timeStamp;
		if (_wstat(_pFileName, &timeStamp) || //error for retrieving timestamp
			(long(timeStamp.st_mtime>>32) != long(_timeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
			(long(timeStamp.st_mtime) != long(_timeStamp.st_mtime)) )			// then load the config. skip otherwise
		{
			debugPrint(L"the file is updated and need to reload");
			if (_fileHandle)
			{
				CloseHandle(_fileHandle);
				_fileHandle = nullptr;
			}
			if (CreateFile(_pFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				delete [] _pReadBuffer;
				_pReadBuffer = nullptr;
				if (!SetupReadBuffer())
				{
					return nullptr;
				}
				if(fileReloaded) *fileReloaded = TRUE;
			}
		}
	}
	return _pReadBuffer;
}

BOOL CFile::IsFileUpdated()
{
	debugPrint(L" CFile::IsFileUpdated()");
	struct _stat timeStamp;
	if (_wstat(_pFileName, &timeStamp) || //error for retrieving timestamp
		(long(timeStamp.st_mtime>>32) != long(_timeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
		(long(timeStamp.st_mtime) != long(_timeStamp.st_mtime)) )			// then load the config. skip otherwise
		return TRUE;
	else
		return FALSE;
}

//---------------------------------------------------------------------
//
// CMemoryFile ctor
//
// Stores the source CFile* and immediately builds the filtered buffer by
// calling GetReadBufferPointer().  Eager initialisation ensures that any
// debug dumps (DebugDumpBuffer in _DEBUG builds) fire right at
// construction rather than being deferred until the first dictionary lookup.
//
//---------------------------------------------------------------------

CMemoryFile::CMemoryFile(CFile* pSrcFile)
    : CFile(), _pSrcFile(pSrcFile)
{
    // Eagerly build the filtered buffer so that DebugDumpBuffer fires at
    // construction time rather than being delayed until the first dictionary
    // lookup.  Without this, the buffer (and the debug dump) only appear when
    // the user first presses a key in the IME — potentially minutes later.
    GetReadBufferPointer();
}

//---------------------------------------------------------------------
//
// CMemoryFile::GetReadBufferPointer
//
// Polls the source CFile* for changes.  If the source has reloaded
// (its on-disk file was modified), discards the own buffer and rebuilds
// via SetupReadBuffer(), then propagates fileReloaded = TRUE.
//
//---------------------------------------------------------------------

_Ret_maybenull_
const WCHAR* CMemoryFile::GetReadBufferPointer(BOOL* fileReloaded)
{
    if (fileReloaded) *fileReloaded = FALSE;

    BOOL srcReloaded = FALSE;
    _pSrcFile->GetReadBufferPointer(&srcReloaded);

    if (!_pReadBuffer || srcReloaded)
    {
        delete[] _pReadBuffer;
        _pReadBuffer = nullptr;
        _fileSize    = 0;
        if (!SetupReadBuffer()) return nullptr;
        if (fileReloaded && srcReloaded) *fileReloaded = TRUE;
    }
    return _pReadBuffer;
}

//---------------------------------------------------------------------
//
// CMemoryFile::SetupReadBuffer
//
// Single-pass selective copy.  Reads the source buffer line by line
// and applies FilterLine() only within the section that contains
// key→character mappings.  Lines OUTSIDE that section are REMOVED.
//
//   CIN files  → only lines between  %chardef begin  …  %chardef end
//               (case-insensitive).  All other directives, %keyname,
//               etc. are dropped.
//
//   TTS files  → only lines inside  [Text]  section.  [Phrase],
//               [Radical], [CodingLayout], etc. are dropped entirely.
//
// The purpose is to produce a buffer containing ONLY the filterable
// key→character mapping entries, so the Big5 engine built on top of
// this buffer sees exactly those entries.  Phrase lookup and radical
// maps are served by the separate, unfiltered main engine.
//
//---------------------------------------------------------------------

BOOL CMemoryFile::SetupReadBuffer()
{
    const WCHAR* src = _pSrcFile->GetReadBufferPointer();
    if (!src) return FALSE;
    DWORD_PTR charLen = _pSrcFile->GetFileSize() / sizeof(WCHAR);
    if (charLen == 0) return FALSE;

    // Worst-case allocation (same as source); actual used length may be smaller.
    WCHAR* buf = new (std::nothrow) WCHAR[charLen];
    if (!buf) return FALSE;

    // Single-pass selective copy: include only lines that are INSIDE the
    // section that contains key→character mappings AND pass FilterLine().
    // Everything outside the target section is dropped, with three exceptions
    // for CIN header directives that ParseConfig needs (see below).
    //
    //  CIN files  (%gen_inp format, '%-directive' headers)
    //      Kept region: between  %chardef begin  and  %chardef end.
    //      Also kept (outside chardef): %sorted, %selkey, %endkey directives.
    //        %sorted 1   → gates _sortedCIN and radicalIndexMap building (perf)
    //        %selkey     → configures candidate selection keys on Big5 engine
    //        %endkey     → configures end keys on Big5 engine
    //      All other header lines (%ename, %gen_inp, %keyname block, …) dropped.
    //
    //  TTS files  ([Section] / "key"="char" format)
    //      Kept region: inside  [Text]  section only.  The [Text] header IS
    //      kept; any other [Section] header drops the filter region and is itself
    //      dropped.  No extra header sections are needed (ParseConfig sets
    //      _searchMode=SEARCH_NONE for [Text] and ignores [Config]/[Radical]).
    //
    // File type is inferred lazily from the first non-empty line.
    const WCHAR* rp  = src;
    const WCHAR* end = src + charLen;
    WCHAR*       wp  = buf;

    enum class FileType { Unknown, CIN, TTS } fileType = FileType::Unknown;
    bool inFilterSection = false;

    while (rp < end)
    {
        const WCHAR* lineStart = rp;
        while (rp < end && *rp != L'\n') ++rp;
        DWORD_PTR lineLen = (DWORD_PTR)(rp - lineStart);
        if (lineLen > 0 && lineStart[lineLen - 1] == L'\r') --lineLen;  // strip CR from CRLF files
        bool hadNewline = (rp < end);
        if (hadNewline) ++rp;   // step past '\n'

        // ── First non-whitespace character of this line ───────────────────
        const WCHAR* p = lineStart;
        while (p < lineStart + lineLen && (*p == L' ' || *p == L'\t')) ++p;
        DWORD_PTR rem = (DWORD_PTR)((lineStart + lineLen) - p);

        // ── Infer file type on first recognisable line ────────────────────
        if (fileType == FileType::Unknown && rem > 0)
        {
            if      (*p == L'%') fileType = FileType::CIN;
            else if (*p == L'[') fileType = FileType::TTS;
        }

        // ── Update section-tracking state ─────────────────────────────────
        if (fileType == FileType::CIN && rem > 0 && *p == L'%')
        {
            // Look for  %chardef begin  /  %chardef end  (case-insensitive)
            const WCHAR* d  = p + 1;          // skip '%'
            DWORD_PTR    dr = rem - 1;
            if (dr >= 7 && _wcsnicmp(d, L"chardef", 7) == 0)
            {
                d  += 7; dr -= 7;
                while (dr > 0 && (*d == L' ' || *d == L'\t')) { ++d; --dr; }
                if      (dr >= 5 && _wcsnicmp(d, L"begin", 5) == 0) inFilterSection = true;
                else if (dr >= 3 && _wcsnicmp(d, L"end",   3) == 0) inFilterSection = false;
            }
        }
        else if (fileType == FileType::TTS && rem > 0 && *p == L'[')
        {
            // Any [Section] header: enter filter region only for [Text]
            const WCHAR* s  = p + 1;           // skip '['
            DWORD_PTR    sr = rem - 1;
            inFilterSection = (sr >= 5 && _wcsnicmp(s, L"Text]", 5) == 0);
        }

        // ── Selective copy ────────────────────────────────────────────────
        // Keep lines that are inside the target section AND pass FilterLine.
        // Lines outside the section are dropped, EXCEPT for a small set of
        // CIN header directives that ParseConfig needs:
        //   %sorted  → gates _sortedCIN / radicalIndexMap building (performance)
        //   %selkey  → configures selection keys on the Big5 engine
        //   %endkey  → configures end keys on the Big5 engine
        // For TTS files no extra header lines are needed.
        bool isCriticalCINDirective = false;
        if (fileType == FileType::CIN && !inFilterSection && rem > 0 && *p == L'%')
        {
            const WCHAR* d  = p + 1;
            DWORD_PTR    dr = rem - 1;
            isCriticalCINDirective =
                (dr >= 6 && _wcsnicmp(d, L"sorted", 6) == 0) ||
                (dr >= 6 && _wcsnicmp(d, L"selkey", 6) == 0) ||
                (dr >= 6 && _wcsnicmp(d, L"endkey", 6) == 0);
        }
        bool keep = (inFilterSection || isCriticalCINDirective) && (FilterLine(lineStart, lineLen) != FALSE);

        if (keep)
        {
            wmemcpy(wp, lineStart, lineLen);
            wp += lineLen;
            if (hadNewline) *wp++ = L'\n';
        }
    }

    _pReadBuffer = buf;
    _fileSize    = (DWORD_PTR)(wp - buf) * sizeof(WCHAR);
#ifdef _DEBUG
    DebugDumpBuffer();
#endif
    return TRUE;
}

//---------------------------------------------------------------------
//
// CMemoryFile::FilterLine
//
// Pure predicate - returns TRUE if the line should be included in the
// output buffer, FALSE if it should be excluded.
//
// Empty lines and header directives (%...) always pass through.
// Multi-char value tokens always pass through (incl. surrogate-pair emoji).
// Single-char value tokens that are Unicode symbols/emoji always pass through.
// Only single-char value tokens that are NOT CP950-encodable and NOT symbols are rejected.
//
//---------------------------------------------------------------------

BOOL CMemoryFile::FilterLine(const WCHAR* lineStart, DWORD_PTR lineLen)
{
    const WCHAR* end = lineStart + lineLen;

    // ── 1. Skip leading whitespace ────────────────────────────────────────
    const WCHAR* q = lineStart;
    while (q < end && (*q == L' ' || *q == L'\t')) ++q;

    // ── 2. Empty lines and directives always pass through ─────────────────
    if (q >= end || *q == L'%') return TRUE;

    // ── 3. Skip keycode token (optionally double-quoted) ──────────────────
    // Supported separators: space, tab, or '=' (TTS format uses "key"="char").
    // keycode and character may or may not be wrapped in double-quotes.
    if (*q == L'"')
    {
        ++q;                                              // skip opening quote
        while (q < end && *q != L'"') ++q;               // scan to closing quote
        if (q < end) ++q;                                // step past closing quote
    }
    else
    {
        while (q < end && *q != L' ' && *q != L'\t' && *q != L'=') ++q;
    }

    // ── 4. Skip separator (one or more spaces, tabs, or '=' signs) ────────
    while (q < end && (*q == L' ' || *q == L'\t' || *q == L'=')) ++q;

    // ── 5. Extract character value (optionally double-quoted) ─────────────
    const WCHAR* valStart;
    const WCHAR* valEnd;
    if (q < end && *q == L'"')
    {
        ++q;                                              // skip opening quote
        valStart = q;
        while (q < end && *q != L'"') ++q;               // scan to closing quote
        valEnd = q;
    }
    else
    {
        valStart = q;
        while (q < end && *q != L' ' && *q != L'\t') ++q;
        valEnd = q;
    }

    DWORD_PTR valLen = (DWORD_PTR)(valEnd - valStart);
    if (valLen == 0) return TRUE;    // no character — pass through

    // ── 6. Classify and filter the extracted character ────────────────────

    // Surrogate pair: exactly 2 wchar_t = one supplementary-plane code point.
    // Never in CP950.  Compute the actual code point and use a plane check:
    //   SMP  U+10000–U+1FFFF  emoji, musical notation, enclosed alphanumerics → pass
    //   SIP  U+20000–U+2FFFF  CJK Extension B/C/D/E/F/I, Compat Ideographs Supp → filter
    //   TIP  U+30000+          CJK Extension G/H and above → filter
    // GetStringTypeW(CT_CTYPE3) is unreliable for supplementary code points across
    // Windows versions and must not be used here.
    if (valLen == 2 &&
        (valStart[0] & 0xFC00) == 0xD800 &&   // high surrogate: U+D800–U+DBFF (top 6 bits = 110110)
        (valStart[1] & 0xFC00) == 0xDC00)      // low  surrogate: U+DC00–U+DFFF (top 6 bits = 110111)
    {
        // Decode to a scalar code point using the Unicode surrogate formula:
        //   cp = 0x10000 + (H − 0xD800) × 0x400 + (L − 0xDC00)
        // Subtracting 0xD800 / 0xDC00 is equivalent to masking the low 10 bits
        // (the top 6 bits of each surrogate unit are fixed and carry no payload).
        // 0x03FF = 0000 0011 1111 1111  — extracts the 10-bit payload of each unit.
        UINT32 cp = 0x10000u
            + (((UINT32)(valStart[0] & 0x03FFu)) << 10)  // high 10 bits of offset (bits 10–19)
            |  ((UINT32)(valStart[1] & 0x03FFu));         // low  10 bits of offset (bits  0– 9)
        return cp < 0x20000u;  // SMP (U+10000–U+1FFFF) passes; SIP+ (CJK supplements) filtered
    }

    // Multi-char compound value: always pass
    if (valLen != 1) return TRUE;

    WCHAR ch = valStart[0];

    // BMP symbols and emoji (C3_SYMBOL): always pass regardless of CP950
    WORD ct3 = 0;
    GetStringTypeW(CT_CTYPE3, &ch, 1, &ct3);
    if (ct3 & C3_SYMBOL) return TRUE;

    // CP950 round-trip check.
    // WC_NO_BEST_FIT_CHARS is unreliable for DBCS code pages; Windows may silently
    // best-fit-map non-CP950 chars and leave usedDefault=FALSE.  The round-trip is
    // definitive: a character that cannot be encoded in CP950 and recovered losslessly
    // is not a Big5/CP950 character — but it may still be a symbol (see below).
    char  mb[4] = {};
    int   r  = WideCharToMultiByte(950, 0, &ch, 1, mb, sizeof(mb), nullptr, nullptr);
    if (r > 0)
    {
        WCHAR rt = 0;
        int   r2 = MultiByteToWideChar(950, 0, mb, r, &rt, 1);
        if (r2 == 1 && rt == ch) return TRUE;
    }

    // CP950 encoding failed or was lossy.  Filter characters that Windows marks
    // as ideographic (C3_IDEOGRAPH) or alphabetic (C3_ALPHA):
    //   C3_IDEOGRAPH — all non-CP950 CJK: Unified Ideographs, Extension A,
    //     Compatibility Ideographs, CJK Radicals Supplement (U+2E80–U+2EFF),
    //     Kangxi Radicals (U+2F00–U+2FD5), etc.
    //   C3_ALPHA     — all non-CP950 alphabetic scripts: Cyrillic, Georgian,
    //     Hebrew, Arabic, etc. (letters that are not symbols).
    // Everything else (non-CP950 symbols, superscripts, vulgar fractions, small
    // Roman numerals, circled/enclosed numbers, Yijing hexagrams, APL symbols, …)
    // has neither flag and passes through unconditionally.
    // ct3 was already fetched above — no extra Windows API call needed.
    return !(ct3 & (C3_IDEOGRAPH | C3_ALPHA));
}

//---------------------------------------------------------------------
//
// CMemoryFile::DebugDumpBuffer  (DEBUG builds only)
//
// Writes the filtered in-memory buffer as a UTF-16LE file to
// %APPDATA%\DIME\dbg_<source-filename> so that the result of the
// CP950 filter can be inspected directly on disk.
// Called automatically by SetupReadBuffer() each time the buffer is
// (re)built.  The output file is silently overwritten on each reload.
//
//---------------------------------------------------------------------

#ifdef _DEBUG
void CMemoryFile::DebugDumpBuffer() const
{
    if (!_pReadBuffer || _fileSize == 0) return;

    // Resolve %APPDATA%
    WCHAR appData[MAX_PATH] = {};
    if (!SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, FALSE))
        return;

    // Only dump production DIME dictionary files (in %APPDATA%\DIME\).
    // Unit-test temp files live in %TEMP%\..., not in %APPDATA%\DIME\, and
    // must not produce noise dumps in the user's DIME data folder.
    const WCHAR* srcPath = _pSrcFile ? _pSrcFile->GetFileName() : nullptr;
    if (!srcPath) return;

    WCHAR dimeDir[MAX_PATH];
    if (FAILED(StringCchPrintfW(dimeDir, MAX_PATH, L"%s\\DIME\\", appData)))
        return;
    size_t dimeDirLen = wcslen(dimeDir);
    if (wcsncmp(srcPath, dimeDir, dimeDirLen) != 0)
        return;   // not a file inside %APPDATA%\DIME\ — skip dump

    // Extract just the filename part of the source path  (e.g. "Array.cin")
    const WCHAR* lastSep  = wcsrchr(srcPath, L'\\');
    const WCHAR* baseName = lastSep ? lastSep + 1 : srcPath;

    // Build output path: %APPDATA%\DIME\dbg_<basename>
    WCHAR outPath[MAX_PATH];
    if (FAILED(StringCchPrintfW(outPath, MAX_PATH, L"%s\\DIME\\dbg_%s", appData, baseName)))
        return;

    HANDLE hFile = ::CreateFileW(outPath, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    // UTF-16LE BOM then the filtered character data
    DWORD written;
    BYTE  bom[2] = { 0xFF, 0xFE };
    WriteFile(hFile, bom, sizeof(bom), &written, nullptr);
    WriteFile(hFile, _pReadBuffer, static_cast<DWORD>(_fileSize), &written, nullptr);
    CloseHandle(hFile);
}
#endif // _DEBUG
