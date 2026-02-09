# DIME Code Review Report

**Date:** February 9, 2026
**Scope:** Memory leaks, GDI/resource leaks, and security vulnerabilities
**Codebase:** DIME Input Method Framework (Windows TSF)
**Architecture:** 100% native C++, no third-party dependencies, TSF provider loaded into every process

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Context](#architecture-context)
3. [Verified Issues — Memory Leaks](#verified-issues--memory-leaks)
4. [Medium Severity Issues](#medium-severity-issues)
5. [Low Severity / Hardening](#low-severity--hardening)
6. [Recommendations](#recommendations)

---

## Executive Summary

DIME is a Windows TSF input method engine written in 100% native C++ with zero external dependencies. As a TSF provider, `DIME.dll` is loaded into **every process** that accepts text input on the system.

| Severity | Category | Count | Resolved |
|----------|----------|-------|----------|
| 🔴 Critical | Memory leaks (BSTR/heap) | 5 | 5 ✅ |
| 🟡 Medium | Robustness / defensive coding | 3 | 3 ✅ |
| 🟢 Low | Code quality / hardening | 4 | 4 ✅ |

### What the Code Already Does Well

- **GDI resource management**: Font handles use `Global::defaultlFontHandle` (global), old objects are restored, `DeleteObject` is called appropriately
- **DC management**: `GetDC`/`ReleaseDC` and `BeginPaint`/`EndPaint` are properly paired
- **File handles**: `CFile` class uses destructor cleanup and `goto errorExit` pattern
- **Registry handles**: `CRegKey` RAII wrapper auto-closes handles
- **String safety**: Already uses `StringCchCopy`, `StringCchCat`, `StringCchPrintf` throughout
- **Thread safety**: Already uses `InterlockedIncrement`/`InterlockedDecrement` for ref counts
- **SafeRelease**: Already exists in `Globals.h`
- **CDIMEArray**: Is a `std::vector<T>` wrapper with `Clear()` method
- **Debug logging**: `debugPrint` is compiled out in release builds

---

## Architecture Context

### Key Design Facts

- `.cin` files are **pure plain text** (tab-delimited key-value pairs) — they cannot contain executable code
- User-loaded `.cin` files are **renamed to fixed filenames** — not derived from file content
- `%APPDATA%\DIME\` is user-writable, but an attacker with same-user access already has equivalent privilege — no escalation possible via `.cin` tampering
- DIME has **no network access, no telemetry, no user-input learning**

---

## Verified Issues — Memory Leaks

### C-01: BSTR and Heap Leaks in `ReverseConversion.cpp` ✅ **RESOLVED**

**File:** `ReverseConversion.cpp` lines 91-113  
**Status:** Fixed - All memory leaks eliminated by implementing `goto Exit` cleanup pattern

**Original leaks in `_AsyncReverseConversionNotification`:**

```cpp
BSTR bstr;
bstr = SysAllocStringLen(_commitString, (UINT)wcslen(_commitString));  // line 95
if (bstr == nullptr) return E_OUTOFMEMORY;
// ...
if (SUCCEEDED(...)) {
    BSTR bstrResult;
    if (SUCCEEDED(reverseConversionList->GetString(0, &bstrResult)) && bstrResult) {
        WCHAR* pwch = new (std::nothrow) WCHAR[SysStringLen(bstrResult)+1];  // line 106
        // ...
        // ❌ bstrResult NOT freed
        // ❌ pwch NOT freed
    }
    reverseConversionList->Release();
    return S_OK;  // ❌ bstr NOT freed
}
return S_FALSE;  // ❌ bstr NOT freed
```

**Impact:** Memory leak on every reverse conversion operation.

**Fix Applied:**
```cpp
// Function refactored with goto Exit cleanup pattern:
HRESULT hr = S_FALSE;
BSTR bstr = nullptr;           // ✅ Initialized to nullptr
BSTR bstrResult = nullptr;     // ✅ Initialized to nullptr
WCHAR* pwch = nullptr;         // ✅ Initialized to nullptr
ITfReverseConversionList* reverseConversionList = nullptr;

// ... allocation and usage ...

Exit:
    if (reverseConversionList)
        reverseConversionList->Release();
    SysFreeString(bstrResult);  // ✅ All BSTRs freed
    SysFreeString(bstr);         // ✅ All BSTRs freed
    delete[] pwch;               // ✅ Heap array freed
    return hr;
```

**Verification:**
- ✅ `bstr` freed in all code paths
- ✅ `bstrResult` freed in all code paths  
- ✅ `pwch` heap allocation freed
- ✅ `reverseConversionList` COM interface released
- ✅ Early return paths replaced with `goto Exit`
- ✅ Null pointer checks added for safe cleanup

---

### C-02: BSTR Leak in `DIME.cpp` ✅ **RESOLVED**

**File:** `DIME.cpp` lines 1068-1078  
**Status:** Fixed - BSTR leak eliminated by adding SysFreeString cleanup

**Original leak in `_SetupReverseConversion`:**

```cpp
BSTR bstr;
bstr = SysAllocStringLen(L"　" , (UINT) 1);
ITfReverseConversionList* reverseConversionList = nullptr;
if(bstr && FAILED(...) || reverseConversionList == nullptr)
{
    _pITfReverseConversion[(UINT)imeMode] = nullptr;
    // ❌ bstr NOT freed
}
else
{
    reverseConversionList->Release();
    // ❌ bstr NOT freed here either
}
```

**Impact:** Memory leak on reverse conversion setup (once per IME mode initialization).

**Fix Applied:**
```cpp
{   //test if the interface can really do reverse conversion
    BSTR bstr = nullptr;  // ✅ Changed from uninitialized
    bstr = SysAllocStringLen(L"　" , (UINT) 1);
    ITfReverseConversionList* reverseConversionList = nullptr;
    if(bstr && FAILED(_pITfReverseConversion[(UINT)imeMode]->DoReverseConversion(bstr, &reverseConversionList)) || reverseConversionList == nullptr)
    {
        _pITfReverseConversion[(UINT)imeMode] = nullptr;
    }
    else
    {
        reverseConversionList->Release();
    }
    SysFreeString(bstr);  // ✅ Cleanup added - bstr freed in all code paths
}
```

**Verification:**
- ✅ `bstr` initialized to nullptr for safe cleanup
- ✅ `SysFreeString(bstr)` called after both if/else branches
- ✅ BSTR freed regardless of success or failure path

---

### C-03: Missing `goto Exit` Cleanup ✅ **RESOLVED**

**Files:** `CompositionProcessorEngine.cpp`  
**Status:** Fixed - All early-return memory leaks eliminated by adding cleanup before returns

**Original leaks - Functions that bypassed cleanup:**

| File | Function | Line | Leaked Resources |
|------|----------|------|------------------|
| `ReverseConversion.cpp` | `_AsyncReverseConversionNotification` | 91-113 | `bstr`, `bstrResult`, `pwch` | ✅ C-01
| `DIME.cpp` | `_SetupReverseConversion` | 1068-1078 | `bstr` | ✅ C-02
| `CompositionProcessorEngine.cpp` | `GetCandidateList` | 319 | `pwch` (allocated L303) | ✅ Fixed
| `CompositionProcessorEngine.cpp` | `GetCandidateList` | 335 | `pwch` (allocated L303) | ✅ Fixed
| `CompositionProcessorEngine.cpp` | `GetCandidateListFromPhrase` | 563 | `pwch` (allocated L549) | ✅ Fixed

**Impact:** Memory leak on every incremental word search and phrase lookup operation where early return conditions were triggered.

**Fix Applied:**

**1. `GetCandidateList` - First early return (line 319):**
```cpp
size_t len = 0;
if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) == S_OK)
{
    wildcardSearch.Set(pwch, len);
}
else
{
    delete[] pwch;  // ✅ Added cleanup before early return
    return;
}
```

**2. `GetCandidateList` - Second early return (line 335):**
```cpp
if (pCandidateList && 0 >= pCandidateList->Count())
{
    delete[] pwch;  // ✅ Added cleanup before early return
    return;
}
```

**3. `GetCandidateListFromPhrase` - Early return (line 563):**
```cpp
// add wildcard char
size_t len = 0;
if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) != S_OK)
{
    delete[] pwch;  // ✅ Added cleanup before early return
    return;
}
```

**Verification:**
- ✅ `pwch` buffer freed before all early returns in `GetCandidateList`
- ✅ `pwch` buffer freed before early return in `GetCandidateListFromPhrase`
- ✅ Normal cleanup paths at lines 362 and 595 remain intact
- ✅ All three allocation sites (L303, L548) now have complete cleanup coverage

---

### C-04: `buildKeyStrokesFromPhoneticSyllable` Memory Leak ✅ **RESOLVED**

**File:** `CompositionProcessorEngine.cpp` lines 768-790  
**Status:** Fixed - Memory leak eliminated by adding explicit cleanup after buffer usage

**Original leak in `buildKeyStrokesFromPhoneticSyllable`:**

```cpp
CStringRange CCompositionProcessorEngine::buildKeyStrokesFromPhoneticSyllable(UINT syllable)
{
    CStringRange csr;
    PWCHAR buf = new (std::nothrow) WCHAR[5];  // ❌ Allocated
    if (buf)
    {
        // ... populate buf ...
        csr.Set(buf, wcslen(buf));  // CStringRange does NOT own memory
    }
    return csr;  // ❌ buf is never freed
}
```

**Problem:** `CStringRange` is a non-owning string view (destructor is empty). The call site at line 382:
```cpp
noToneKeyStroke = buildKeyStrokesFromPhoneticSyllable(...);
// ❌ noToneKeyStroke goes out of scope, buf leaked
```

**Impact:** Memory leak on every phonetic "any tone" wildcard search operation.

**Fix Applied:**

Added explicit cleanup after the last use of `noToneKeyStroke` buffer at line 424-428:

```cpp
//Search cutom table without priority
if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && !customPhrasePriority)
{
    if (phoneticAnyTone)
        _pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&noToneKeyStroke, pCandidateList);
    // ... other searches ...
}

// Cleanup noToneKeyStroke buffer after last use
if (phoneticAnyTone && noToneKeyStroke.Get())
{
    delete[] noToneKeyStroke.Get();  // ✅ Buffer explicitly freed
}

//Search Array unicode extensions
```

**Note:** The call site in `KeyProcesser.cpp` line 62 manually manages ownership via `_keystrokeBuffer` and deletes before reassignment, so that path does not leak and was not modified.

**Verification:**
- ✅ Buffer allocated by `buildKeyStrokesFromPhoneticSyllable` is now freed after last use
- ✅ Cleanup only executes when `phoneticAnyTone` is TRUE (when buffer was allocated)
- ✅ Null pointer check ensures safe cleanup
- ✅ `KeyProcesser.cpp` usage path unaffected (already manages ownership correctly)

---

### C-05: `pwszFontFaceName` Heap Leaks ✅ **RESOLVED**

**Files:** `Config.cpp` line 461, `DictionarySearch.cpp` line 481  
**Status:** Fixed - Memory leaks eliminated by adding cleanup after string copies

**Original leak in `Config.cpp` line 461:**

```cpp
pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
if (pwszFontFaceName)
{
    GetDlgItemText(hDlg, IDC_EDIT_FONTNAME, pwszFontFaceName, LF_FACESIZE);
    if (_pFontFaceName)
        StringCchCopy(_pFontFaceName, LF_FACESIZE, pwszFontFaceName);
    // ❌ pwszFontFaceName NOT deleted
}
```

**Original leak in `DictionarySearch.cpp` line 481:**

```cpp
WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
StringCchCopyN(pwszFontFaceName, LF_FACESIZE, ...);
CConfig::SetFontFaceName(pwszFontFaceName);  // Just does StringCchCopy
// ❌ pwszFontFaceName NOT deleted
```

**Impact:** Memory leak each time font name is read from config dialog or `.cin` file.

**Fix Applied:**

**1. `Config.cpp` (line 467):**
```cpp
pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
if (pwszFontFaceName)
{
    GetDlgItemText(hDlg, IDC_EDIT_FONTNAME, pwszFontFaceName, LF_FACESIZE);
    if (_pFontFaceName)
        StringCchCopy(_pFontFaceName, LF_FACESIZE, pwszFontFaceName);
    delete[] pwszFontFaceName;  // ✅ Cleanup added
}
```

**2. `DictionarySearch.cpp` (line 485):**
```cpp
else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontFaceName", 12)) == CSTR_EQUAL)
{
    WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
    assert(valueStrings.GetAt(0)->GetLength() < LF_FACESIZE-1);
    StringCchCopyN(pwszFontFaceName, LF_FACESIZE, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());
    CConfig::SetFontFaceName(pwszFontFaceName);
    delete[] pwszFontFaceName;  // ✅ Cleanup added
}
```

**Verification:**
- ✅ `pwszFontFaceName` freed after `StringCchCopy` in Config.cpp
- ✅ `pwszFontFaceName` freed after `SetFontFaceName` in DictionarySearch.cpp
- ✅ Both `SetFontFaceName` and `StringCchCopy` only copy the string data, not take ownership
- ✅ Cleanup happens inside the allocation check blocks for safe execution

---

## Medium Severity Issues

### MS-01: `GetFileSize` Return Not Validated ✅ **RESOLVED**

**File:** `File.cpp` line 113  
**Status:** Fixed - Added validation for `INVALID_FILE_SIZE` return value

**Original issue:**

```cpp
_fileSize = ::GetFileSize(_fileHandle, NULL);
// ❌ INVALID_FILE_SIZE (0xFFFFFFFF) not checked before use
```

Later in `SetupReadBuffer()`:
```cpp
const WCHAR* pWideBuffer = new (std::nothrow) WCHAR[_fileSize / sizeof(WCHAR) - 1];
```

If `GetFileSize` fails, this could allocate an enormous buffer or cause arithmetic issues.

**Impact:** Potential buffer over-allocation or arithmetic overflow if file size query fails.

**Fix Applied:**

```cpp
_wstat(pFileName, & _timeStamp);
DWORD fileSize = ::GetFileSize(_fileHandle, NULL);

// MS-01: Validate GetFileSize return value
if (fileSize == INVALID_FILE_SIZE)
{
    CloseHandle(_fileHandle);
    _fileHandle = INVALID_HANDLE_VALUE;
    return FALSE;
}

// MS-02: Enforce maximum file size (100 MB) for .cin files
const DWORD MAX_CIN_FILE_SIZE = 100 * 1024 * 1024;  // 100 MB
if (fileSize > MAX_CIN_FILE_SIZE)
{
    CloseHandle(_fileHandle);
    _fileHandle = INVALID_HANDLE_VALUE;
    return FALSE;
}

_fileSize = fileSize;  // ✅ Only assigned after validation
```

**Verification:**
- ✅ `INVALID_FILE_SIZE` checked before using file size
- ✅ File handle properly closed on validation failure
- ✅ `_fileHandle` reset to `INVALID_HANDLE_VALUE` to prevent reuse
- ✅ Function returns `FALSE` to signal error to caller

---

### MS-02: No Maximum File Size Validation ✅ **RESOLVED**

**Files:** `File.cpp`, `TableDictionaryEngine.cpp`  
**Status:** Fixed - Added 100 MB maximum file size limit

**Original issue:**

No validation of `.cin` file size before loading. An extremely large file could cause excessive memory usage or denial of service.

**Impact:** Malicious or corrupted large files could exhaust system memory.

**Fix Applied:**

Added file size validation in `File.cpp` at line 123-129:

```cpp
// MS-02: Enforce maximum file size for .cin files
if (fileSize > Global::MAX_CIN_FILE_SIZE)
{
    CloseHandle(_fileHandle);
    _fileHandle = INVALID_HANDLE_VALUE;
    return FALSE;
}
```

The constant is defined in `Globals.h` lines 212-219:

```cpp
//---------------------------------------------------------------------
// File Security Constraints
//---------------------------------------------------------------------
// Maximum .cin file size enforced during file loading
// Rationale: Typical .cin files are 1-10 MB. Largest known tables with
// 80,000+ entries are ~15 MB. This generous limit prevents memory
// exhaustion attacks while accommodating all legitimate use cases.
constexpr DWORD MAX_CIN_FILE_SIZE = 100 * 1024 * 1024;  // 100 MB
```

**Rationale for 100 MB limit:**
- Typical `.cin` files are 1-10 MB (even large tables with 80,000+ entries)
- 100 MB provides generous headroom for legitimate use cases
- Prevents memory exhaustion attacks
- File rejection happens early in `CreateFile()` before memory allocation
- **Named constant in header** ensures discoverability and maintainability

**Verification:**
- ✅ Maximum file size enforced at file open time
- ✅ Limit is reasonable for legitimate `.cin` files
- ✅ Prevents allocation of excessively large buffers
- ✅ File handle cleaned up properly on rejection
- ✅ Named constant (`Global::MAX_CIN_FILE_SIZE`) defined in `Globals.h` for maintainability
- ✅ Well-documented rationale in header file comments

---

### MS-03: Unsigned Installer ✅ **RESOLVED**

**Issue:** The installer binary is unsigned (acknowledged in README). Users cannot verify binary integrity.

**Status:** Fixed - Automated SHA-256 checksum generation with direct README.md publication

**Impact:** Without checksums, users cannot verify downloaded installers haven't been tampered with.

**Solution Implemented:**

**1. Created automated deployment script** (`installer\deploy-installer.ps1`):
- PowerShell-based deployment automation (replaces batch script)
- Copies build artifacts and builds NSIS installer
- Calculates SHA-256 hash of `DIME-x86armUniversal.exe` using `Get-FileHash`
- **Automatically updates README.md** with checksum between `<!-- CHECKSUM_START -->` and `<!-- CHECKSUM_END -->` markers
- Updates date and hash in readable table format
- Uses native PowerShell cmdlets (no external dependencies)

**2. Updated README.md** with checksum table and verification instructions:
```markdown
3. **（建議）驗證檔案完整性：**
   
<!-- CHECKSUM_START -->

   **最新版本校驗和 (更新日期: yyyy-mm-dd):**
   
   | 檔案 | SHA-256 校驗和 |
   |------|----------------|
   | DIME-x86armUniversal.exe | `<automatically updated>` |
   | DIME-x86armUniversal.zip | `<automatically updated>` |

   <!-- CHECKSUM_END -->
   
解壓縮後建議用如下Powershell指令，驗證 SHA-256 校驗和以確保檔案完整性：
```powershell
Get-FileHash DIME-x86armUniversal.exe -Algorithm SHA256
```
```

**Automated Release Process:**
1. Run `deploy-installer.ps1` to build installer and auto-update README.md with checksum
2. Commit updated README.md with new checksum
3. Create GitHub release with installer
4. **Checksum is immediately visible on GitHub README page** (no separate file needed)

**Verification:**
- ✅ Fully automated checksum generation and publication
- ✅ README.md serves as single source of truth for checksums
- ✅ Uses PowerShell `Get-FileHash` cmdlet (clean, reliable)
- ✅ Checksum visible on GitHub README without downloading any files
- ✅ PowerShell instructions provided for user verification
- ✅ Eliminates manual copy-paste errors in release process
- ✅ Addresses MS-03 security recommendation without requiring expensive code signing certificate

---

## Low Severity / Hardening

### L-01: Magic Numbers in Drawing Code ✅ **RESOLVED**

**Status:** Fixed - Created dedicated UI constants header and refactored UI code to use named constants

Hard-coded pixel values for margins, padding, and font sizes scattered throughout UI code. These values work correctly (DIME already has proper high-DPI support on 2K/4K displays), but have been centralized as named constants for improved code maintainability.

**Solution Implemented:**

**1. Created `UIConstants.h`** with centralized UI layout constants following the same pattern used for `Global::MAX_CIN_FILE_SIZE` in MS-02:

```cpp
// UIConstants.h
namespace UI {
    // Candidate Window Layout
    constexpr int CANDIDATE_ROW_WIDTH           = 30;
    constexpr int CANDIDATE_BORDER_WIDTH        = 1;
    constexpr int CANDIDATE_ITEM_PADDING        = 2;
    constexpr int CANDIDATE_MARGIN              = 4;

    // Scrollbar Layout
    constexpr int SCROLLBAR_WIDTH               = 16;
    constexpr int SCROLLBAR_BUTTON_HEIGHT       = 16;
    constexpr int SCROLLBAR_THUMB_MIN_HEIGHT    = 8;

    // Font Size Limits
    constexpr int DEFAULT_FONT_SIZE             = 12;
    constexpr int MIN_FONT_SIZE                 = 8;
    constexpr int MAX_FONT_SIZE                 = 72;

    // Shadow Window Constants
    constexpr int SHADOW_WIDTH                  = 5;
    constexpr int SHADOW_ALPHA_LEVELS           = 5;
    
    // Animation Constants
    constexpr int ANIMATION_STEP_TIME_MS        = 8;
    constexpr int ANIMATION_ALPHA_START         = 5;
    
    // Window Positioning
    constexpr int DEFAULT_WINDOW_X              = -32768;
    constexpr int DEFAULT_WINDOW_Y              = -32768;
}
```

**2. Refactored UI code** to use the constants (example from `CandidateWindow.cpp`):

```cpp
// Before: Magic numbers
_cyRow = CANDWND_ROW_WIDTH;
_x = -32768;
_y = -32768;
SetLayeredWindowAttributes(_GetWnd(), 0, (255 * 5) / 100, LWA_ALPHA);

// After: Named constants
#include "UIConstants.h"
_cyRow = UI::CANDIDATE_ROW_WIDTH;
_x = UI::DEFAULT_WINDOW_X;
_y = UI::DEFAULT_WINDOW_Y;
SetLayeredWindowAttributes(_GetWnd(), 0, (255 * UI::ANIMATION_ALPHA_START) / 100, LWA_ALPHA);
```

**Rationale:**
- Dedicated header keeps GUI constants separate from framework constants (`Globals.h`)
- Only included by GUI-related files (smaller compilation impact)
- All baseline values documented as being DPI-scaled at runtime
- Maintains existing high-DPI functionality while improving maintainability

**Verification:**
- ✅ UI constants centralized in dedicated header file (`UIConstants.h`)
- ✅ **All UI files refactored** to use named constants:
  - ✅ `CandidateWindow.cpp` - 5 replacements (row width, window position, animation alpha)
  - ✅ `NotifyWindow.cpp` - 6 replacements (font size, window position, animation alpha values)
  - ✅ `UIPresenter.cpp` - 3 replacements (off-screen initialization coordinates)
  - ✅ `ShadowWindow.cpp` - 1 replacement (shadow alpha number constant)
- ✅ Clear separation of concerns (GUI vs. framework)
- ✅ Well-documented with DPI scaling notes
- ✅ Follows same naming pattern as MS-02 fix
- ✅ No functional changes - purely code quality improvement
- ✅ Compiles without errors
- ✅ BaseWindow.cpp and ButtonWindow.cpp reviewed - no magic numbers found

**Note:** This addresses code maintainability only. DIME's existing high-DPI support remains unchanged and continues to work correctly on 2K/4K displays. All UI files have been systematically refactored to use centralized constants.

### L-02: Inconsistent Error Return Codes ✅ **RESOLVED**

**Status:** Fixed - Documented comprehensive HRESULT usage conventions in `Globals.h`

**Original issue:**

Mix of `E_FAIL`, `E_UNEXPECTED`, `HRESULT_FROM_WIN32(...)`, and `S_FALSE` used throughout codebase without documented conventions. While the code functionally works, lack of conventions makes maintenance and code review more difficult.

**Impact:** Low - No functional issues, but inconsistent error handling conventions can lead to confusion for new contributors.

**Solution Implemented:**

Added comprehensive HRESULT usage documentation in `Globals.h` (lines 221-280) explaining when to use each standard Windows error code:

**Documented conventions:**
- **S_OK (0x00000000)** - Operation succeeded completely
- **S_FALSE (0x00000001)** - Success but no data (e.g., search with no results)
- **E_OUTOFMEMORY (0x8007000E)** - Memory allocation failed
- **E_INVALIDARG (0x80070057)** - Invalid parameter (input validation)
- **E_UNEXPECTED (0x8000FFFF)** - Catastrophic failure (should never happen)
- **E_FAIL (0x80004005)** - Generic failure (use sparingly)
- **HRESULT_FROM_WIN32(GetLastError())** - Convert Win32 errors

**Added usage patterns:**
```cpp
// Input validation:
if (param == nullptr) return E_INVALIDARG;
if (!ptr) return E_OUTOFMEMORY;

// Error propagation:
HRESULT hr = SomeFunction();
if (FAILED(hr)) return hr;

// Cleanup with goto Exit:
HRESULT hr = E_FAIL;
// ... code ...
Exit:
    // cleanup
    return hr;
```

**Rationale:**
- Documentation-only approach (no code changes required)
- Uses standard Windows HRESULT values (no custom codes)
- Provides guidelines for new code without breaking existing code
- Educational for developers unfamiliar with COM error handling
- Low-risk, high-value documentation improvement

**Verification:**
- ✅ Comprehensive conventions documented in `Globals.h`
- ✅ Covers all commonly used HRESULT values in DIME
- ✅ Includes practical usage examples and patterns
- ✅ Explains when to use each error code
- ✅ Documents error propagation and cleanup patterns
- ✅ No code changes required - purely documentation
- ✅ Establishes conventions for future development

**Note:** These conventions apply to new code. Existing code uses these HRESULT values correctly but may not follow these conventions consistently. No retroactive refactoring is planned or needed.

---

### L-03: No SEH in `DllMain` ✅ **RESOLVED**

**Status:** Fixed - Added Structured Exception Handling to protect critical DLL initialization and cleanup

**Original issue:**

`DllMain` had no exception handling. If an exception occurs during `DLL_PROCESS_ATTACH` or `DLL_PROCESS_DETACH`, the process would terminate immediately, which is particularly problematic since DIME.dll is loaded into every text-accepting process on the system.

**Impact:** Medium-Low - Unhandled exceptions in `DllMain` cause immediate process termination. Given DIME's wide deployment (loaded into every app), any crash would affect the entire system.

**Solution Implemented:**

Added `__try/__except` blocks around critical sections in `DllMain.cpp`:

**1. Protected `DLL_PROCESS_ATTACH` (initialization):**
```cpp
case DLL_PROCESS_ATTACH:
    __try
    {
        debugPrint(L"DllMain() DLL_PROCESS_ATTACH");
        Global::dllInstanceHandle = hInstance;
        
        if (!InitializeCriticalSectionAndSpinCount(&Global::CS, 0))
            return FALSE;
            
        if (!Global::RegisterWindowClass())
            return FALSE;
            
        // Version detection, DLL loading, resource loading...
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // L-03: SEH protection - fail gracefully on exception
        debugPrint(L"DllMain() Exception caught during DLL_PROCESS_ATTACH: 0x%08X", 
                   GetExceptionCode());
        return FALSE;
    }
    break;
```

**2. Protected `DLL_PROCESS_DETACH` (cleanup):**
```cpp
case DLL_PROCESS_DETACH:
    __try
    {
        debugPrint(L"DllMain() DLL_PROCESS_DETACH");
        
        if (Global::hShcore)
        {
            FreeLibrary(Global::hShcore);
            Global::hShcore = nullptr;
        }
        
        DeleteCriticalSection(&Global::CS);
        
        #ifdef _DEBUG
            _CrtDumpMemoryLeaks();
        #endif
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // L-03: SEH protection - log exception but allow graceful termination
        debugPrint(L"DllMain() Exception caught during DLL_PROCESS_DETACH: 0x%08X", 
                   GetExceptionCode());
    }
    break;
```

**Rationale:**
- **Process-wide impact**: DIME is loaded into every text-accepting process (browsers, Office, Notepad, etc.)
- **Initialization safety**: If `RegisterWindowClass()` or critical section initialization throws, fail gracefully with FALSE
- **Cleanup safety**: During `DLL_PROCESS_DETACH`, process is already terminating - log exception but don't prevent process exit
- **Minimal overhead**: SEH has negligible performance impact in non-exception cases
- **Windows standard practice**: Microsoft recommends SEH in `DllMain` for production DLLs

**Exception handling behavior:**
- **On ATTACH exception**: Returns FALSE to indicate initialization failed - DLL won't be loaded into that process
- **On DETACH exception**: Logs exception code for debugging but allows process to continue terminating normally
- **Thread attach/detach**: No SEH needed - these are simpler operations with no critical resource management

**Verification:**
- ✅ SEH wraps all critical initialization in `DLL_PROCESS_ATTACH`
- ✅ SEH wraps all cleanup code in `DLL_PROCESS_DETACH`
- ✅ Exception codes logged via `GetExceptionCode()` for diagnostics
- ✅ Proper return FALSE on initialization failure
- ✅ Graceful handling during cleanup (process terminating anyway)
- ✅ No SEH overhead during normal operation
- ✅ Compiles without errors
- ✅ Follows Microsoft best practices for `DllMain` exception handling

**Note:** This protection is defensive programming. DIME's existing code is already stable, but SEH provides an additional safety net given the DLL's system-wide deployment scope.

### L-04: Build Configuration ✅ **RESOLVED**

**Status:** Fixed - Code Analysis (`/analyze`) enabled for all build configurations

**Original issue:**

Release builds already use `/W4` (Level 4 warnings) and `/WX` (TreatWarningAsError), but static code analysis was not enabled.

**Impact:** Low - No functional issues, but static analysis can catch potential bugs at compile time.

**Solution Implemented:**

Enabled Code Analysis for **all 8 build configurations** in `DIME.vcxproj` by adding `<RunCodeAnalysis>true</RunCodeAnalysis>`:

**Debug Configurations:**
- ✅ Debug|Win32 (line 244)
- ✅ Debug|x64 (line 250)
- ✅ Debug|ARM64 (line 256)
- ✅ Debug|ARM64EC (line 263)

**Release Configurations:**
- ✅ Release|Win32 (line 269)
- ✅ Release|x64 (line 275)
- ✅ Release|ARM64 (line 281)
- ✅ Release|ARM64EC (line 288)

**What Code Analysis Does:**
- Runs Microsoft C/C++ Code Analysis (`/analyze` compiler flag)
- Checks for C++ Core Guidelines violations
- Detects potential bugs through static analysis at compile-time
- Enforces security best practices (buffer overruns, null pointer dereferences, etc.)
- Zero runtime overhead (compile-time only)

**CI/CD Integration:**
- Code Analysis now runs on every build
- Errors and warnings will appear in build output
- Can be integrated into CI pipelines for automated quality checks
- Particularly valuable for Release builds used in production

**Rationale:**
- Proactive bug detection before runtime
- Complements existing `/W4 /WX` strict warning enforcement
- Industry best practice for production C++ code
- Minimal impact on build time for incremental builds
- Catches issues that compiler warnings might miss

**Verification:**
- ✅ `<RunCodeAnalysis>true</RunCodeAnalysis>` added to all 8 configurations
- ✅ Enabled for both Debug (development) and Release (production) builds
- ✅ Configured via Visual Studio project settings
- ✅ No additional dependencies required (built into MSVC)

**Note:** Code Analysis may increase initial full-build time, but provides significant value through compile-time bug detection. For faster development iteration, developers can temporarily disable it for Debug builds if needed, but it should remain enabled for Release/CI builds.

---

## Recommendations

### Immediate (P0)

1. ✅ ~~**Fix BSTR/heap leaks in `ReverseConversion.cpp`**~~ — **RESOLVED:** Proper cleanup added for `bstr`, `bstrResult`, and `pwch` using `goto Exit` pattern
2. ✅ ~~**Fix BSTR leak in `DIME.cpp`**~~ — **RESOLVED:** Added `SysFreeString(bstr)` in `_SetupReverseConversion`
3. ✅ ~~**Fix early-return leaks in `CompositionProcessorEngine.cpp`**~~ — **RESOLVED:** Added `delete[] pwch` before all early returns
4. ✅ ~~**Fix `buildKeyStrokesFromPhoneticSyllable` leak**~~ — **RESOLVED:** Added explicit `delete[]` cleanup after buffer usage
5. ✅ ~~**Fix `pwszFontFaceName` leaks**~~ — **RESOLVED:** Added `delete[]` after use in Config.cpp and DictionarySearch.cpp
6. ✅ ~~**Validate `GetFileSize` return**~~ — **RESOLVED:** Added `INVALID_FILE_SIZE` check and 100 MB file size limit

### Short-term (P1)

1. ✅ ~~**Add file size validation for `.cin` loading**~~ — **RESOLVED:** Included with MS-01/MS-02 fix
2. ✅ ~~**Add `/analyze` to CI pipeline**~~ — **RESOLVED:** Code Analysis enabled for all 8 build configurations (L-04)

### Long-term (P2)

1. **`Microsoft::WRL::ComPtr<T>` - NOT RECOMMENDED for existing code**  
   - DIME's COM pointer management is based on **Microsoft's official TSF sample code**
   - The `goto Exit` cleanup pattern is the **established Windows/TSF idiom** used throughout Microsoft's own samples
   - All memory leaks have been fixed - the existing code now works correctly
   - Refactoring would be **high-risk, high-effort, zero functional benefit**
   - The current pattern is widely understood by Windows developers familiar with TSF
   - **Recommendation:** Keep existing TSF COM code as-is. Only consider `ComPtr<T>` for **new non-TSF COM code** if any is added in the future.

2. ✅ ~~**Publish SHA-256 checksums for releases**~~ — **RESOLVED:** Automated checksum generation implemented with `deploy-installer.ps1` (MS-03)

---

*This report is based on source code analysis. Dynamic testing with [Application Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/application-verifier) is recommended to catch any runtime issues.*