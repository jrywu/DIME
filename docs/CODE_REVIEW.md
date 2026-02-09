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

| Severity | Category | Count |
|----------|----------|-------|
| 🔴 Critical | Memory leaks (BSTR/heap) | 6 |
| 🟡 Medium | Robustness / defensive coding | 3 |
| 🟢 Low | Code quality / hardening | 4 |

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

### C-01: BSTR and Heap Leaks in `ReverseConversion.cpp`

**File:** `ReverseConversion.cpp` lines 91-113

**Verified leaks in `_AsyncReverseConversionNotification`:**

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

**Fix:**
```cpp
// Add before each return:
SysFreeString(bstr);
SysFreeString(bstrResult);
delete[] pwch;
```

---

### C-02: BSTR Leak in `DIME.cpp`

**File:** `DIME.cpp` lines 1068-1078

**Verified leak in `_SetupReverseConversion`:**

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

**Fix:** Add `SysFreeString(bstr);` before exiting both branches.

---

### C-03: Missing `goto Exit` Cleanup

**Files:** `ReverseConversion.cpp`, `DIME.cpp`

Functions that bypass the `goto Exit` cleanup pattern:

| File | Function | Line | Leaked Resources |
|------|----------|------|------------------|
| `ReverseConversion.cpp` | `_AsyncReverseConversionNotification` | 91-113 | `bstr`, `bstrResult`, `pwch` |
| `DIME.cpp` | `_SetupReverseConversion` | 1068-1078 | `bstr` |
| `CompositionProcessorEngine.cpp` | `GetCandidateList` | 318 | `pwch` (allocated L303) |
| `CompositionProcessorEngine.cpp` | `GetCandidateList` | 337 | `pwch` (allocated L303) |
| `CompositionProcessorEngine.cpp` | `GetCandidateListFromPhrase` | 561 | `pwch` (allocated L549) |

**Recommendation:** Refactor these functions to use the same `goto Exit` pattern used elsewhere in the codebase.

---

### C-04: `buildKeyStrokesFromPhoneticSyllable` Memory Leak

**File:** `CompositionProcessorEngine.cpp` lines 768-787

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

`CStringRange` is a non-owning string view (destructor is empty). The call site at line 380:
```cpp
noToneKeyStroke = buildKeyStrokesFromPhoneticSyllable(...);
// ❌ noToneKeyStroke goes out of scope, buf leaked
```

**Note:** The call site in `KeyProcesser.cpp` line 62 manually manages ownership via `_keystrokeBuffer` and deletes before reassignment, so that path does not leak.

**Fix:** Either:
- Make `CStringRange` own its memory (breaking change), OR
- Have caller free via `delete[] noToneKeyStroke.Get()` when done, OR
- Refactor to use a static buffer or member variable

---

### C-05: `pwszFontFaceName` Heap Leaks

**File:** `Config.cpp` line 461

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

**File:** `DictionarySearch.cpp` line 481

```cpp
WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
StringCchCopyN(pwszFontFaceName, LF_FACESIZE, ...);
CConfig::SetFontFaceName(pwszFontFaceName);  // Just does StringCchCopy
// ❌ pwszFontFaceName NOT deleted
```

**Impact:** Memory leak each time font name is read from config or .cin file.

**Fix:** Add `delete[] pwszFontFaceName;` after use in both locations.

---

## Medium Severity Issues

### MS-01: `GetFileSize` Return Not Validated

**File:** `File.cpp` line 113

```cpp
_fileSize = ::GetFileSize(_fileHandle, NULL);
// ❌ INVALID_FILE_SIZE (0xFFFFFFFF) not checked before use
```

Later in `SetupReadBuffer()`:
```cpp
const WCHAR* pWideBuffer = new (std::nothrow) WCHAR[_fileSize / sizeof(WCHAR) - 1];
```

If `GetFileSize` fails, this could allocate an enormous buffer or cause arithmetic issues.

**Fix:** Check for `INVALID_FILE_SIZE` before using file size.

---

### MS-02: No Maximum File Size Validation

**Files:** `File.cpp`, `TableDictionaryEngine.cpp`

No validation of `.cin` file size before loading. An extremely large file could cause excessive memory usage.

**Recommendation:** Enforce a reasonable maximum (e.g., 100 MB).

---

### MS-03: Unsigned Installer

The installer binary is unsigned (acknowledged in README). Users cannot verify binary integrity.

**Recommendation:** Publish SHA-256 checksums in release notes.

---

## Low Severity / Hardening

### L-01: Magic Numbers in Drawing Code

Hard-coded pixel values for margins, padding, and font sizes. Consider using DPI-aware calculations.

### L-02: Inconsistent Error Return Codes

Mix of `E_FAIL`, `E_UNEXPECTED`, `HRESULT_FROM_WIN32(...)`, and `S_FALSE`. Consider documenting conventions.

### L-03: No SEH in `DllMain`

Exceptions in `DllMain` cause process termination. Consider wrapping critical cleanup in `__try/__except`.

### L-04: Build Configuration

Release builds already use `/W4` and `/WX` (TreatWarningAsError). Consider enabling `/analyze` for static analysis in CI.

---

## Recommendations

### Immediate (P0)

1. **Fix BSTR/heap leaks in `ReverseConversion.cpp`** — Add proper cleanup for `bstr`, `bstrResult`, and `pwch`
2. **Fix BSTR leak in `DIME.cpp`** — Add `SysFreeString(bstr)` in `_SetupReverseConversion`
3. **Fix early-return leaks in `CompositionProcessorEngine.cpp`** — Add `delete[] pwch;` before early returns at lines 318, 337, 561
4. **Fix `buildKeyStrokesFromPhoneticSyllable` leak** — Refactor ownership or add explicit cleanup in caller
5. **Fix `pwszFontFaceName` leaks** — Add `delete[]` after use in Config.cpp:461 and DictionarySearch.cpp:481
6. **Validate `GetFileSize` return** — Check for `INVALID_FILE_SIZE` before use

### Short-term (P1)

4. Add file size validation for `.cin` loading
5. Consider adding `/analyze` to CI pipeline

### Long-term (P2)

6. Consider `Microsoft::WRL::ComPtr<T>` for new code to prevent future `goto Exit` misses
7. Publish SHA-256 checksums for releases

---

*This report is based on source code analysis. Dynamic testing with [Application Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/application-verifier) is recommended to catch any runtime issues.*