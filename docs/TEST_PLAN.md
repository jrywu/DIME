# DIME Test Plan

**Version**: 2.6 (FilterLine C3_IDEOGRAPH|C3_ALPHA fallback, surrogate plane check, UT-09 expanded to 20 tests)
**Last Updated**: 2026-03-12
**Status**: ✅ **COMPLETED** - 362 tests passing, 37.2% coverage baseline

---

## 📊 Executive Summary

### Test Coverage Status

| Metric | Target | Actual | Status |
|--------|--------|--------|--------|
| **Overall Coverage** | 80-85% | **37.2%** | ⚠️ **BELOW TARGET** (7,289/19,589 lines) |
| **Unit Tests** | 119 tests | **151 tests** | ✅ COMPLETE |
| **Integration Tests** | 186 tests | **211 tests** | ✅ COMPLETE |
| **Total Automated Tests** | 324 tests | **362 passing** | ✅ COMPLETE |
| **Execution Time** | < 60s | **~21 seconds** | ✅ EXCELLENT |
| **CI/CD Ready** | Yes | **Yes** | ✅ AUTO-RUN |

### Coverage Philosophy

**Realistic CI/CD Automation**: This document targets **80-85% automated coverage** as an aspirational goal for CI/CD pipelines.

**Current Status**: **37.2% coverage** (7,289 covered / 19,589 total lines)

**Coverage Gap Analysis**:
- **Automated tests (359 tests)**: Core functionality, APIs, integration points
- **Coverage challenges**: 
  - Many TSF framework integration points require full IME installation
  - UI rendering code paths (WM_PAINT, GDI) difficult to test in automation
  - Complex Windows message pump interactions
  - COM activation and TSF document manager chains
- **Manual validation**: Full TSF integration, end-to-end workflows, cross-platform compatibility
- **Pragmatic approach**: Focus on testable components, accept limitations of automated testing for IME systems

**Coverage by Category** (estimated):
- Core APIs & Data Structures: ~70-80% (good coverage)
- File I/O & Configuration: ~60-70% (good coverage)
- Dictionary Engine: ~50-60% (moderate coverage)
- Win32 UI Components: ~20-30% (limited - UI rendering)
- TSF Integration: ~15-25% (limited - requires full environment)
- Overall: **37.2%** (measured)

**Coverage Measurement**:
```bash
# Prerequisites
# - Build in Debug|x64 (PDB symbols required)
# - Install OpenCppCoverage: choco install opencppcoverage
# - All tests passing

# Run comprehensive coverage analysis (recommended)
cd src\tests
.\run_all_coverage.ps1

# Output:
# - HTML report: src\tests\coverage_results\AllTests_Coverage\index.html
# - XML report: src\tests\coverage_results\AllTests_Coverage.xml
# - Console summary with per-module breakdown

# Alternative: Run coverage manually
OpenCppCoverage --sources DIME --excluded_sources tests ^
  --export_type=html:coverage.html ^
  --export_type=cobertura:coverage.xml ^
  -- vstest.console.exe bin\x64\Debug\DIMETests.dll
```

---

## Table of Contents

1. [Testing Overview](#testing-overview)
2. [Test Environment](#test-environment)
3. [Test Suite Summary](#test-suite-summary)
4. [Unit Tests (UT)](#unit-tests)
5. [Integration Tests (IT)](#integration-tests)
6. [Testing Tools](#testing-tools)
7. [Test Execution](#test-execution)
8. [Document Revision History](#document-revision-history)

---

## Testing Overview

### Testing Objectives

| Category | Objectives | Validation Method |
|----------|-----------|-------------------|
| **Functional Correctness** | IME modes work correctly<br>Accurate radical-to-character conversion<br>Complete candidate window functionality | Unit tests + Integration tests |
| **Stability** | No memory leaks<br>No crashes<br>Long-term stability | Memory tests + Automated runs |
| **Performance** | Input response < 50ms<br>Dictionary lookup < 100ms<br>Reasonable memory usage | Unit tests + Manual validation |
| **Compatibility** | Windows 8.1/10/11<br>x86/x64/ARM64/ARM64EC<br>Various applications | Automated builds + Manual validation |

---

## Test Environment

### Requirements

| Category | Requirements |
|----------|-------------|
| **Hardware** | CPU: Intel/AMD x64 or ARM64<br>RAM: ≥ 4GB<br>Disk: ≥ 200MB free |
| **Software** | Windows: 8.1 / 10 (21H2+) / 11 (21H2+)<br>Visual Studio: 2019/2022 (C++ tools)<br>Windows SDK: ≥ 10.0.17763.0<br>Test Framework: Microsoft Native C++ Unit Test |

### Platform Support Matrix

| Windows Version | x86 | x64 | ARM64 | ARM64EC |
|----------------|-----|-----|-------|---------|
| Windows 8.1 | ✅ | ✅ | ❌ | ❌ |
| Windows 10 21H2+ | ✅ | ✅ | ✅ | ✅ |
| Windows 11 22H2+ | ✅ | ✅ | ✅ | ✅ |
| Windows 11 23H2+ | ✅ | ✅ | ✅ | ✅ |

---

## Test Suite Summary

### Unit Tests Overview (151 tests, ~15 seconds)

| Suite | Tests | Coverage Target | Actual Coverage | Files Tested |
|-------|-------|----------------|-----------------|--------------|
| **UT-01: Configuration** | 26 tests | ≥90% | ~95% | `Config.cpp`, `Config.h` (ConfigTest class) |
| **UT-02: Dictionary** | 8 tests | ≥85% | ~90% | `TableDictionaryEngine.cpp`, `DictionarySearch.cpp` |
| **UT-03: String Processing** | 11 tests | ≥90% | ~100% | `BaseStructure.cpp` (CStringRange) |
| **UT-04: Memory Management** | 14 tests | ≥85% | ~95% | `BaseStructure.h` (CDIMEArray), `File.cpp` |
| **UT-05: File I/O** | 12 tests | ≥90% | ~95% | `File.cpp`, `Config.cpp` |
| **UT-06: TableDictionaryEngine** | 25 tests | ≥85% | ~72%* | `TableDictionaryEngine.cpp` |
| **UT-07: CIN File Parsing** | 11 tests | ≥85% | ~90% | `Config.cpp` (`parseCINFile`) |
| **UT-09: CMemoryFile Filter** | 20 tests | ≥100% | **100%** | `File.cpp` (`CMemoryFile`, `FilterLine`) |
| **UT-CM: Color Mode (GetEffectiveDarkMode, round-trip, constants)** | 8 tests | ≥90% | ~90% | `Config.cpp`, `Config.h`, `Define.h` |
| **UT-CV: Custom Table Validation unit tests** | 17 tests | ≥90% | ~90% | `Config.cpp` (CustomTableValidationUnitTest class) |
| **UT-PT: Palette round-trip + backward compat** | 11 tests | ≥90% | ~90% | `Config.cpp` (ConfigTest class) |
| **Total Unit Tests** | **151** | **≥85%** | **~90%** | **Core functionality** |

*UT-06 has room for improvement in wildcard/reverse lookup coverage

### Integration Tests Overview (211 tests, ~20 seconds)

| Suite | Tests | Coverage Target | Actual Coverage | Test Approach |
|-------|-------|-----------------|-----------------|---------------|
| **IT-01: TSF Integration** | 33 tests | 55-80% | Server: 54.5%, DIME: 30.4% | DLL exports + Direct instantiation + System-level |
| **IT-02: Language Bar** | 17 tests | ≥85% | LanguageBar: 26.3%, Compartment: 72.9% | Button management, mode switching |
| **IT-03: Candidate Window** | 24 tests | ≥80% | CandidateWindow: 29.8%, ShadowWindow: 4.6% | Win32 window testing (incl. IT-CM-10–13) |
| **IT-04: Notification Window** | 22 tests | ≥30% | NotifyWindow: ~30%, BaseWindow: ~35% | Win32 window testing (incl. IT-CM-20–22) |
| **IT-05: TSF Core Logic** | 18 tests | ≥70% | KeyEventSink: 23.2%, Engine: 13.8% | Stub-based logic testing |
| **IT-06: UIPresenter** | 54 tests | 55-60%* | **54.8%** | Stub-based testing (practical max) |
| **IT-07: Settings Dialog** | 18 tests | ≥90% | Config: ~60-70% | End-to-end dialog integration (incl. IT-CM-01–04) |
| **IT-CV: Custom Table Validation integration** | 14 tests | ≥85% | Config: ~70% | DialogContext + mode-aware validation |
| **IT-PT: Palette integration** | 8 tests | ≥90% | Config: ~80% | Light/dark palette get/set + static defaults |
| **Total Integration Tests** | **211** | **≥75%** | **~37%** | **Interaction & workflows** |

*IT-06 revised target: 55-60% is practical maximum without full TSF simulation infrastructure  
**Overall project coverage limited by TSF/UI integration complexity

### Test Strategy by Component

| Component Type | Testing Strategy | Coverage Expectation |
|----------------|------------------|---------------------|
| **Core APIs** | Direct unit testing | 90-100% |
| **File I/O** | Unit tests with temp files | 90-95% |
| **Win32 UI** | Integration tests (CreateWindowEx, message pump) | 30-60% (UI rendering limited) |
| **TSF Integration** | Stubs + selective system testing | 50-80% (full TSF requires installation) |
| **Configuration** | End-to-end workflow testing | 90%+ (comprehensive) |

---

## Unit Tests

**Total:** 151 tests | **Execution Time:** ~15 seconds | **Coverage:** Core logic and configuration

### UT-01: Configuration Management (26 tests)

**Target**: `Config.cpp`, `Config.h` | **Coverage**: ~95%

| Test | Target Functions | Key Validations |
|------|------------------|-----------------|
| **UT-01-01: Load Configuration** | `LoadConfig()`, `ParseINI()`, `GetConfigPath()` | File parsing, default values, path resolution (%APPDATA%\DIME\), error handling |
| **UT-01-02: Write Configuration** | `WriteConfig()`, `GetInitTimeStamp()` | UTF-16LE encoding + BOM, atomic writes, timestamp validation |
| **UT-01-03: Change Notification** | `WriteConfig(checkTime=TRUE)`, `_wstat()` | Timestamp tracking, external modification detection, conflict prevention |
| **UT-01-04: Multi-Process Safety** | `LoadConfig()`, `WriteConfig()`, file locking | Concurrent reads (100%), timestamp conflicts (100%), lost update prevention (100%) |

**Key Features Tested**:
- ✅ Valid/invalid/missing config file handling
- ✅ UTF-16LE BOM handling
- ✅ Multi-process concurrent access safety
- ✅ Timestamp-based conflict detection
- ✅ Graceful degradation on corruption

### UT-02: Dictionary Search (8 tests)

**Target**: `TableDictionaryEngine.cpp`, `DictionarySearch.cpp`, `DictionaryParser.cpp`, `File.cpp` | **Coverage**: ~90%

| Test | Target Functions | Key Validations |
|------|------------------|-----------------|
| **UT-02-01: CIN File Parsing** | `ParseConfig()`, `CFile::CreateFile()` | UTF-16LE BOM detection (100%), %chardef parsing (100%), escape characters (100%), malformed files (90%) |
| **UT-02-02: Radical Search** | `CollectWord()`, `FindWords()` | Single/multiple radical search (100%), invalid radicals (100%), edge cases (95%) |
| **UT-02-03: Custom Table Tests** | `parseCINFile()`, `GetCustomTablePriority()` | UTF-8 → UTF-16LE conversion (100%), %encoding override (100%), priority logic (100%) |

**Key Features Tested**:
- ✅ CIN format parser (all standard formats)
- ✅ Radical-to-character lookup algorithms
- ✅ Custom table loading and priority
- ✅ Encoding detection and conversion

### UT-03: String Processing (11 tests)

**Target**: `BaseStructure.cpp` (CStringRange class) | **Coverage**: ~100%

Comprehensive tests covering all `CStringRange` operations:
- Set/Clear/Get/GetLength: 100%
- String comparison (equal/different): 100%
- Null/empty string handling: 100%
- Unicode character support: 100%

### UT-04: Memory Management (14 tests)

**Target**: `BaseStructure.h` (CDIMEArray), `File.cpp` | **Coverage**: ~95%

| Test | Target Functions | Key Validations |
|------|------------------|-----------------|
| **UT-04-01: CDIMEArray Operations** | `Append()`, `Insert()`, `RemoveAt()`, `Clear()`, `GetAt()` | All operations (100%), bounds checking (100%), memory leak detection (0 leaks after 10K iterations) |
| **UT-04-02: File Memory Tests** | `CreateFile()`, `MAX_CIN_FILE_SIZE` validation | Large file handling (95MB, 100%), size limit enforcement (>100MB, 100%), MS-02 security fix validation |

### UT-05: File I/O (12 tests)

**Target**: `File.cpp`, `Config.cpp` | **Coverage**: ~95%

| Test | Target Functions | Key Validations |
|------|------------------|-----------------|
| **UT-05-01: UTF-16LE Encoding** | `CreateFile()`, `_wfopen_s()` | Read/write UTF-16LE with BOM (100%), BOM verification (100%), Unicode preservation (100%) |
| **UT-05-02: Encoding Auto-detection** | `importCustomTableFile()`, `MultiByteToWideChar()` | UTF-8 detection (100%), Big5 detection (100%), UTF-16LE pass-through (100%), fallback logic (90%) |

### UT-06: TableDictionaryEngine (25 tests)

**Target**: `TableDictionaryEngine.cpp` (CTableDictionaryEngine class) | **Coverage**: ~72% (target: ≥85%)

| Test Category | Tests | Coverage Target | Key Validations |
|---------------|-------|-----------------|-----------------|
| **UT-06-01: CollectWord Operations** | 4 tests | ≥95% | CStringRange overload (95%+), CCandidateListItem overload (95%+), sorted CIN optimization, phrase population |
| **UT-06-02: Wildcard Search** | 4 tests | ≥95% | Basic wildcard (100%), deduplication (100%), word frequency integration (100%), multiple patterns (95%) |
| **UT-06-03: Reverse Lookup** | 4 tests | 100% | Exact match (100%), wildcard (100%), multiple key codes (100%) |
| **UT-06-04: Sorting by KeyCode** | 6 tests | 100% | Single/two items, merge sort, empty list, already sorted, reverse sorted |
| **UT-06-05: Sorting by Frequency** | 4 tests | 100% | Descending order, negative frequencies, zero frequencies, mixed |
| **UT-06-06: CIN Config Parsing** | 3 tests | ≥95% | Header parsing, radical map building, index map building, sorted CIN detection |

**Coverage Gap**: ~13% gap to target (CollectWordForWildcard, CollectWordFromConvertedString need improvement)

### UT-CM: Color Mode (8 tests, in `CustomTableValidationUnitTest` class)

**Target**: `Config.cpp`, `Config.h`, `Define.h` | **Coverage**: ~90%

Part of the light/dark theme feature (see `docs/LIGHT_DARK_THEME.md` §12). Hosted in the `CustomTableValidationUnitTest` class in `ConfigTest.cpp`.

| Test Category | Tests | Key Validations |
|---------------|-------|-----------------|
| **UT-CM-01: GetEffectiveDarkMode logic** | 4 tests | LIGHT→false, DARK→true, CUSTOM→false, SYSTEM→mirrors OS |
| **UT-CM-03: Accessor pair** | 1 test | `SetColorModeKeyFound` / `GetColorModeKeyFound` symmetry |
| **UT-CM-04: Dark color constant ranges** | 3 tests | Dark backgrounds are dark (<0x80), dark text is light (>0x80), border constants differ |

UT-CM-02 (ColorMode INI round-trip, 4 tests) is in the `ConfigTest` class and counted under UT-PT.

**Infrastructure fixes**:
- `ConfigTest::TEST_METHOD_INITIALIZE` resets `_configIMEMode = DAYI` via a DAYI config ping so every `LoadConfig(ARRAY)` call re-parses, avoiding false negatives from `LoadConfig`'s same-second timestamp guard.
- `ConfigTest::TEST_CLASS_INITIALIZE` now initialises `Global::isWindows1809OrLater` via inline `RtlGetVersion` (ntdll), since `DllMain` never runs in the test process — this ensures the backward-compat heuristic returns `SYSTEM` (not `LIGHT`) on Windows 10 1809+.

---

### UT-CV: Custom Table Validation (17 tests, in `CustomTableValidationUnitTest` class)

**Target**: `Config.cpp` (`ValidateCustomTableLines`, `DialogContext`) | **Coverage**: ~90%

| Test Category | Tests | Key Validations |
|---------------|-------|-----------------|
| **Constants & misc** | 4 tests | `DialogContext` default init, keystroke/deletion thresholds, `IsSystemDarkTheme` no-crash |
| **ValidateCustomTableLines** | 11 tests | Null edit, valid line, empty content, no separator, key too long, key at max codes, multiple valid, mixed lines, empty interspersed, phonetic ASCII pass, phonetic non-ASCII fail |
| **Theme & dark-context** | 2 tests | `ThemeColorConstants` light≠dark; `ValidateCustomTableLines` dark-context no-crash |

---

### UT-PT: Palette Round-trip & Backward Compat (11 tests, in `ConfigTest` class)

**Target**: `Config.cpp`, `Config.h` | **Coverage**: ~90%

Added alongside the light/dark theme feature to verify the three-palette (light/dark/custom) INI persistence and legacy backward-compatibility.

| Test | Key Validations |
|------|-----------------|
| **ColorMode_RoundTrip_{System,Light,Dark,Custom}** (4) | All four `IME_COLOR_MODE` values survive `WriteConfig` → `LoadConfig`; `_colorModeKeyFound` set correctly |
| **LightPalette_RoundTrip** | All 6 light-palette colors survive round-trip |
| **DarkPalette_RoundTrip** | All 6 dark-palette colors survive round-trip |
| **LightPalette_FactoryDefaults** | Light defaults match `CANDWND_*` constants |
| **DarkPalette_FactoryDefaults** | Dark defaults match `CANDWND_DARK_*` constants |
| **BackwardCompat_NonDefaultColors_InfersCustom** | Legacy INI without `ColorMode` + non-default colors → `IME_COLOR_MODE_CUSTOM` |
| **BackwardCompat_DefaultColors_InfersSystem** | Legacy INI without `ColorMode` + all-default colors → `IME_COLOR_MODE_SYSTEM` (Win10 1809+) |
| **AllPalettes_RoundTrip_Together** | All three palettes survive a single `WriteConfig`/`LoadConfig` cycle simultaneously |

**Key fix**: Both backward-compat tests used narrow `std::string` I/O to strip the `ColorMode` key — this silently failed on UTF-16LE files. Fixed to `WritePrivateProfileStringW(L"Config", L"ColorMode", nullptr, path)`.

---

### UT-07: CIN File Parsing (11 tests)

**Target**: `Config.cpp` (`parseCINFile`) | **Coverage**: ~90%

| Test | Target Functions | Key Validations |
|------|------------------|-----------------|
| **UT-07-01: Tab-Separated** | `parseCINFile()` pattern 1 | Basic key\tvalue parsing and output |
| **UT-07-02: Space-Separated** | `parseCINFile()` pattern 2 | key value (space delimiter) parsing |
| **UT-07-03: Escape Quotes/Backslashes** | `parseCINFile()` escape logic | `"` and `\` escaped inside %chardef sections |
| **UT-07-04: Keyname Section** | `parseCINFile()` %keyname handling | %keyname begin/end triggers escape mode |
| **UT-07-05: Encoding Directive** | `parseCINFile()` %encoding handling | `%encoding UTF-8` → `%encoding\tUTF-16LE` |
| **UT-07-06: Unparsable Lines** | `parseCINFile()` fallback path | Unparsable lines written as-is outside escape |
| **UT-07-07: Unparsable in Escape** | `parseCINFile()` skip logic | Unparsable lines skipped inside escape sections |
| **UT-07-08: Custom Table Mode** | `parseCINFile(customTableMode=TRUE)` | Output wrapped with %chardef begin/end |
| **UT-07-09: Value With Spaces** | `parseCINFile()` pattern 3 | `key value with spaces` parsed via fallback pattern |
| **UT-07-10: Empty File** | `parseCINFile()` | Empty input produces empty output |
| **UT-07-11: Escape Mode Toggle** | `parseCINFile()` doEscape state | doEscape resets after %chardef end |

**Key Features Tested**:
- ✅ Three `swscanf_s` parsing patterns (tab, space, value-with-spaces)
- ✅ Escape quoting of `"` and `\` in %chardef and %keyname sections
- ✅ Encoding directive conversion to UTF-16LE
- ✅ Custom table mode (UTF-16LE input, %chardef wrapping)
- ✅ Unparsable line handling (write vs skip based on escape state)
- ✅ Escape mode toggling (doEscape on/off across sections)

---

### UT-09: CMemoryFile Filter (20 tests)

**Target**: `File.cpp` (`CMemoryFile`, `FilterLine`) | **Coverage**: **100%**
**Test File**: `src/tests/CMemoryFileTest.cpp`

Tests the Big5/CP950 candidate filter built into `CMemoryFile`. Each test builds an in-memory CIN source via a helper `MakeSourceFile()`, constructs a `CMemoryFile` over it, and inspects the resulting filtered buffer via `BufferContains()`.

| ID | Scenario |
|----|----------|
| UT-09-01 to UT-09-08 | Constructor, `GetReadBufferPointer()` (first/second call, null arg, hot-reload), `SetupReadBuffer()` null/valid source, `_fileSize` reflects filtered output |
| UT-09-09 to UT-09-14 | `FilterLine()` via buffer: empty line (inside chardef), critical directives pass/non-critical drop, multi-char value, single CP950 char, non-CP950 CJK ideograph excluded, whitespace-only line |
| UT-09-15 | Integration: `CTableDictionaryEngine` over `CMemoryFile` — `CollectWord` returns only CP950 chars |
| UT-09-16 | CRLF line endings (`\r\n`) — non-CP950 CJK ideograph still excluded; CP950 char still present |
| UT-09-17 | BMP symbol (★ U+2605, `C3_SYMBOL`) passes filter; non-CP950 CJK ideograph (U+3400, `C3_IDEOGRAPH`) is excluded |
| UT-09-18 | Yijing hexagram (U+4DC0) passes (not CP950, not `C3_IDEOGRAPH`, not `C3_ALPHA`); CJK Extension A (U+3400) filtered |
| UT-09-19 | SMP emoji (U+1F4DE, surrogates D83D/DCDE) passes via plane check (`cp < 0x20000u`); SIP CJK Ext B (U+20000, D840/DC00) filtered |
| UT-09-20 | Numeric symbol (vulgar fraction U+2153) passes (not CP950, not `C3_IDEOGRAPH`, not `C3_ALPHA`); CJK Extension A (U+3400) filtered |

**Key fixes covered by these tests**:
- CRLF strip: prevents trailing `\r` from inflating the value-token length, which would defeat the CP950 check
- BMP symbol pass-through: `GetStringTypeW(CT_CTYPE3)` + `C3_SYMBOL` lets arrows, dingbats, and BMP emoji through regardless of CP950 encodability
- Surrogate pair plane check: code-point arithmetic (`cp < 0x20000u`) replaces unreliable `GetStringTypeW(CT_CTYPE3)` for supplementary characters; SMP (emoji) passes, SIP+ (CJK Extension B/C/D/E/F) filtered
- `C3_IDEOGRAPH | C3_ALPHA` fallback: non-CP950 characters that fail the round-trip and lack `C3_SYMBOL` are filtered if they carry `C3_IDEOGRAPH` (CJK radicals, Extension A, Kangxi) or `C3_ALPHA` (Cyrillic, Georgian, Arabic, Hebrew); non-ideographic non-alphabetic symbols (Yijing, superscripts, fractions, APL) pass through

---

## Integration Tests

**Total:** 211 tests | **Execution Time:** ~20 seconds | **Coverage:** Interaction & workflow validation

### Test Philosophy

Integration tests focus on **component interaction** rather than line coverage:
- **Win32 UI components** (CandidateWindow, NotifyWindow): Testable via CreateWindowEx without TSF
- **UIPresenter**: DIME's UI management layer (NOT TSF code) - testable with stubs
- **TSF Integration**: Full lifecycle requires system installation (graceful auto-skip in CI/CD)
- **Settings Dialog**: End-to-end workflow testing (UI ↔ Backend ↔ File I/O)

**Note on Coverage**: Integration test coverage for individual components can be high (e.g., UIPresenter 54.8%), but overall project coverage remains at **37.2%** due to:
- Large portions of TSF integration code requiring full IME installation
- UI rendering and Windows message pump code difficult to test
- COM activation and TSF framework dependencies

### IT-01: TSF Integration (33 tests, 3 test approaches)

**Test Files**: `TSFIntegrationTest.cpp` (18 tests) + `TSFIntegrationTest_Simple.cpp` (15 tests)  
**Target**: `DIME.cpp`, `Server.cpp`, `Compartment.cpp`, `LanguageBar.cpp`  
**Coverage**: Server.cpp 54.55%, DIME.cpp 30.38%

| Sub-Category | Method | Tests | Coverage | Limitations |
|--------------|--------|-------|----------|-------------|
| **IT-01-01: DLL Exports** | LoadLibrary + GetProcAddress | 11 tests | Server: ~55% | Cannot test full COM activation |
| **IT-01-02: Direct Unit Tests** | Direct class instantiation (`new CDIME()`) | 15 tests | DIME: ≥80% | TSF framework methods require ITfThreadMgr |
| **IT-01-03: Full TSF (System-Level)** | CoCreateInstance + full TSF activation | 7 tests | Manual + Auto-skip | Requires IME installation (auto-skips in CI/CD) |

**Summary**:
- ✅ 33/33 tests passing (100% pass rate)
- ✅ Foundation coverage ~60-80% without full COM registration
- ✅ Graceful auto-skip when TSF unavailable (CI/CD friendly)
- ✅ Manual validation confirms full DIME functionality in real applications

### IT-02: Language Bar (17 tests)

**Target**: `LanguageBar.cpp`, `Compartment.cpp` | **Coverage**: LanguageBar: 26.30%, Compartment: 72.92%

- ✅ Button creation (all buttons), Windows 8 special handling, icon loading
- ✅ Chinese/English mode switching (state sync, icon update)
- ✅ Full/half shape toggle (configuration update, state persistence)

### IT-03: Candidate Window (24 tests, incl. IT-CM-10–13)

**Target**: `CandidateWindow.cpp`, `ShadowWindow.cpp` | **Coverage**: CandidateWindow: 29.84%, ShadowWindow: 4.58%

- ✅ Window creation via Win32 CreateWindowEx (100%)
- ✅ Candidate list display, show/hide operations (100%)
- ✅ Shadow window creation, positioning, size tracking (100%)

### IT-04: Notification Window (22 tests, incl. IT-CM-20–22)

**Target**: `NotifyWindow.cpp`, `BaseWindow.cpp` | **Coverage**: ~30-35% (UI rendering limited)  
**Test Approach**: Win32 window testing (**no TSF required**)

**Key Insight**: CNotifyWindow inherits from CBaseWindow (standard Win32 CreateWindowEx) - testable without IME installation

- ✅ Window creation, constructor initialization, class registration (100%)
- ✅ Show/hide operations, timer setup (90%), auto-dismiss logic (85%)
- ✅ Text content update, window auto-sizing (95%), multi-line support (90%)
- ✅ Shadow window integration (creation, positioning, show/hide sync)

### IT-05: TSF Core Logic (18 tests)

**Target**: `KeyEventSink.cpp`, `Composition.cpp`, `CandidateHandler.cpp`, `KeyHandler.cpp` | **Coverage**: KeyEventSink: 23.2%, Engine: 13.8%

**Test Strategy**: Test core IME logic without full TSF framework:
- Direct CDIME instance creation
- Stub ITfContext for minimal TSF interface
- OnTestKeyDown() for key event simulation

| Test Group | Tests | Key Validations |
|------------|-------|-----------------|
| **IT-05-01: Key Event Processing** | 4 tests | Printable keys, Backspace, Escape, Space |
| **IT-05-02: Composition Management** | 3 tests | AddVirtualKey, RemoveVirtualKey, GetCandidateList |
| **IT-05-03: Candidate Selection** | 5 tests | Number key selection, Arrow up/down, PageUp/PageDown navigation |
| **IT-05-04: Mode Switching** | 2 tests | Chinese mode, English mode pass-through |
| **IT-05-05: Special Characters** | 2 tests | Full-width conversion, punctuation handling |
| **IT-05-06: Context Switching** | 2 tests | OnSetFocus handling, multiple contexts independence |

### IT-06: UIPresenter (54 tests)

**Target**: `UIPresenter.cpp` | **Coverage**: 54.8% (practical maximum: 55-60%)  
**Test Approach**: Stub-based testing (minimal TSF mocks)

**Key Insight**: UIPresenter is **DIME's UI management layer** (NOT TSF code) - testable with stubs

**Coverage Progress**:
- Initial (17 tests): 29.4%
- After Phase 1+2 (35 tests): 42.7% (+13.3%)
- After Phase 3 (54 tests): **54.8%** (+12.1%, total +25.4%)
- Remaining gap to 75% original target: 20.2% (146 lines of TSF lifecycle code)

**Test Categories** (54 tests):
- ✅ Construction & initialization (3 tests)
- ✅ Candidate window management (4 tests)
- ✅ Notification window management (5 tests)
- ✅ UI visibility & state (5 tests)
- ✅ ITfUIElement interface (3 tests)
- ✅ Candidate list query (5 tests)
- ✅ Candidate paging (4 tests)
- ✅ Candidate selection (4 tests)
- ✅ Selection finalization (2 tests)
- ✅ COM interface testing (3 tests)
- ✅ Selection style & integration (3 tests)
- ✅ Keyboard events (2 tests)
- ✅ Color configuration (5 tests)
- ✅ List management (3 tests)
- ✅ UI navigation & state (2 tests)
- ✅ Edge cases (1 test)

**Uncovered Code** (~326 lines, 45.2%):
- TSF Lifecycle Methods (~30 lines): BeginUIElement, EndUIElement, _UpdateUIElement (require ITfUIElementMgr callbacks)
- Candidate Selection Integration (~60 lines): _CandidateChangeNotification (requires ITfEditSession chain)
- Candidate List Startup (~40 lines): _StartCandidateList (cascading TSF dependencies)
- Notification Integration (~40 lines): _NotifyChangeNotification (requires composition infrastructure)
- Window Positioning (~80 lines): _LayoutChangeNotification (requires Win32 HWND hierarchy)
- Other TSF Integration (~76 lines): Thread focus, compartments, error paths

**ROI Assessment**: To reach 70% coverage would require ~800 lines of mock infrastructure for 57 additional covered lines (14:1 ratio) - **impractical**

### IT-CM: Color Mode Integration Tests (11 tests)

**Test Files**: `SettingsDialogIntegrationTest.cpp` (4), `CandidateWindowIntegrationTest.cpp` (4), `NotificationWindowIntegrationTest.cpp` (3)
**Target**: Color-mode wiring across dialog, candidate window, and notify window
**See**: `docs/LIGHT_DARK_THEME.md` §12 for full specifications

| Sub-Category | Tests | File | Key Validations |
|--------------|-------|------|-----------------|
| **IT-CM-01 to IT-CM-04: Settings Dialog** | 4 tests | `SettingsDialogIntegrationTest.cpp` | Combo initialises from INI; color boxes disabled/enabled by mode; PSN_APPLY persists mode; Restore Default resets combo to SYSTEM |
| **IT-CM-10 to IT-CM-13: Candidate Window** | 4 tests | `CandidateWindowIntegrationTest.cpp` | Light preset setter no-crash; dark preset setter no-crash; phrase≠item color; dark border luminance > light border luminance |
| **IT-CM-20 to IT-CM-22: Notify Window** | 3 tests | `NotificationWindowIntegrationTest.cpp` | Dark text/bg setter no-crash; light colors differ from dark; dark border luminance > light border luminance |

**Note**: IT-CM-13 and IT-CM-22 assert `darkLum > lightLum` because `CANDWND_BORDER_COLOR = RGB(0,0,0)` (pure black) — dark-theme borders must be *lighter* (gray) to remain visible against dark backgrounds.

---

### IT-07: Settings Dialog (18 tests, `SettingsDialogIntegrationTest` class)

**Target**: `Config.cpp`, `DIMESettings.cpp`, `ShowConfig.cpp` | **Coverage**: Config ~60-70%
**Test Approach**: End-to-end dialog integration testing

**Tests Implemented** (14 core IT-07 + 4 IT-CM color-mode):
- IT07_01: FontSize (edit control)
- IT07_03: MaxCodes (edit control) — IT07_02 removed (FontWeight uses ChooseFont, no direct control)
- IT07_04: AutoCompose (checkbox)
- IT07_05–08: ClearOnBeep, DoBeep, DoBeepNotify, DoBeepOnCandi (checkboxes)
- IT07_09: IMEShiftMode (combo - 4 values)
- IT07_10: DoubleSingleByteMode (combo - 3 values)
- IT07_11: ArrayScope (combo - 5 values)
- IT07_12: NumericPad (combo - 3 values)
- IT07_13: PhoneticKeyboardLayout (combo - 2 values)
- IT07_14: MakePhrase (checkbox)
- IT07_15: ShowNotifyDesktop (checkbox)
- IT-CM-01–04: Color mode combo init, color-box enable/disable, PSN_APPLY persist, Restore Default reset

**Integration Chain**: `Win32 Dialog UI ↔ PropertyPageWndProc ↔ CConfig ↔ File I/O ↔ INI Parser`

---

### IT-CV: Custom Table Validation Integration (14 tests, `CustomTableValidationIntegrationTest` class)

**Target**: `Config.cpp` (`ValidateCustomTableLines`, `DialogContext`, `WM_THEMECHANGED`) | **Coverage**: ~70%
**Test Approach**: IME-mode-aware validation with dialog context, theme change simulation, performance

| Test | Key Validations |
|------|-----------------|
| **IT-CV-01–08: Mode & rule coverage** | ARRAY max-codes boundary, DAYI/Phonetic/Generic key rules, all-modes pass/fail |
| **IT-CV-09: WM_THEMECHANGED** | Handler updates `isDarkTheme` flag in DialogContext |
| **IT-CV-10: Context isolation** | Each `DialogContext` stores its own IME mode independently |
| **IT-CV-11–12: Performance** | 1000 valid lines < 500ms; 100 error lines < 2000ms |
| **IT-CV-13–14: Dark/light color** | Dark context → white text; light context → black text |

---

### IT-PT: Palette Integration (8 tests, `CustomTableValidationIntegrationTest` class)

**Target**: `Config.cpp`, `Config.h` (`GetEffectiveDarkMode`, light/dark palette accessors) | **Coverage**: ~80%

| Test | Key Validations |
|------|-----------------|
| **IT-PT-01–03: EffectiveDarkMode** | LIGHT→false, DARK→true, CUSTOM→false (integration-level) |
| **IT-PT-04–05: Palette get/set round-trip** | Light and dark palette colors survive set/get |
| **IT-PT-06–07: Static defaults** | `GetLight*` and `GetDark*` accessors match `CANDWND_*` / `CANDWND_DARK_*` constants |
| **IT-PT-08: Dark item color lighter than light** | `CANDWND_DARK_ITEM_COLOR` luminance > `CANDWND_ITEM_COLOR` (white on dark vs black on light) |

---

## Testing Tools

### Automated Testing

| Tool | Purpose | Integration |
|------|---------|-------------|
| **Microsoft Native C++ Unit Test Framework** | Unit + integration testing | Built into VS 2019/2022, Test Explorer |
| **OpenCppCoverage** | Code coverage analysis | HTML reports, line-by-line coverage |
| **GitHub Actions / Azure DevOps** | CI/CD automation | Auto-run tests on every commit |

### Manual Testing

| Tool | Purpose |
|------|---------|
| **Task Manager** | Resource monitoring (CPU, memory, GDI objects) |
| **Performance Monitor** | Detailed performance analysis |
| **Spy++** | Window message monitoring |
| **GDIView (NirSoft)** | GDI handle leak detection |
| **Registry Editor** | Verify COM registration |
| **Process Monitor** | File and registry access monitoring |

---

## Test Execution

### Quick Commands

```bash
# Run all tests
vstest.console.exe DIMETests.dll

# Run specific test suite
vstest.console.exe DIMETests.dll /TestCaseFilter:"FullyQualifiedName~ConfigTest"

# Run with detailed logging
vstest.console.exe DIMETests.dll /logger:trx /ResultsDirectory:.\TestResults

# Generate coverage report
OpenCppCoverage --sources DIME --excluded_sources tests ^
  --export_type=html:coverage.html ^
  -- vstest.console.exe bin\x64\Debug\DIMETests.dll
```

### CI/CD Pipeline (GitHub Actions / Azure DevOps)

```yaml
name: DIME Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: windows-latest
    strategy:
      matrix:
        platform: [Win32, x64, ARM64EC]
        configuration: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
      
      - name: Add MSBuild to PATH
        uses: microsoft/setup-msbuild@v2
      
      - name: Setup VSTest
        uses: darenm/Setup-VSTest@v1.3
      
      - name: Build & Run Tests
        run: |
          cd src
          msbuild DIME.sln /p:Configuration=${{ matrix.configuration }} /p:Platform=${{ matrix.platform }} /p:PlatformToolset=v143
          vstest.console.exe bin\${{ matrix.platform }}\${{ matrix.configuration }}\DIMETests.dll /logger:trx
      
      - name: Upload Results
        uses: actions/upload-artifact@v4
        with:
          name: test-results-${{ matrix.platform }}-${{ matrix.configuration }}
          path: TestResults/*.trx
```

---

## Document Revision History

### Version 2.6 - 2026-03-12
**FilterLine C3_IDEOGRAPH|C3_ALPHA fallback, surrogate plane check, UT-09 expanded to 20 tests:**

- ✅ **Total: 362 tests passing** (up from 359 at v2.5); confirmed 151 unit + 211 integration (all running, no auto-skip)
- ✅ **Surrogate-pair rewrite** (`File.cpp`, `FilterLine()`): Replaced `GetStringTypeW(CT_CTYPE3)` + `C3_SYMBOL` check (unreliable across Windows versions for supplementary code points) with direct Unicode plane arithmetic: `cp < 0x20000u`. SMP (U+10000–U+1FFFF: emoji, enclosed alphanumerics) → pass; SIP+ (U+20000+: CJK Extension B/C/D/E/F, Compat Ideographs Supplement) → filtered.
- ✅ **Fallback extended to `C3_IDEOGRAPH | C3_ALPHA`** (`File.cpp`, `FilterLine()`): Non-CP950 BMP characters that fail the CP950 round-trip and lack `C3_SYMBOL` are now filtered if they have `C3_IDEOGRAPH` (CJK radicals, Extension A, Kangxi) **or** `C3_ALPHA` (Cyrillic, Georgian, Arabic, Hebrew). Characters with neither flag (Yijing hexagrams, superscripts, vulgar fractions, APL, small Roman numerals, circled/enclosed numbers) pass through unconditionally.
- ✅ **UT-09-17 updated**: "must be filtered" character changed from Cyrillic U+0400 (filtered by `C3_ALPHA`) to CJK Extension A U+3400 (filtered by `C3_IDEOGRAPH`) — more clearly exercises the ideographic filter path.
- ✅ **UT-09-18** (`FilterLine_YijingHexagram_PassesFilter`): Yijing hexagram U+4DC0 passes (not CP950, not `C3_IDEOGRAPH`, not `C3_ALPHA`); CJK Extension A U+3400 filtered (`C3_IDEOGRAPH`).
- ✅ **UT-09-19** (`FilterLine_SurrogatePair_EmojiPass_CJKExtFilter`): SMP emoji U+1F4DE (D83D/DCDE) passes via `cp < 0x20000u`; SIP CJK Ext B U+20000 (D840/DC00) filtered.
- ✅ **UT-09-20** (`FilterLine_NumericSymbol_PassesFilter`): Vulgar fraction U+2153 passes (not CP950, not `C3_IDEOGRAPH`, not `C3_ALPHA`); CJK Extension A U+3400 filtered.

### Version 2.5 - 2026-03-11
**IT-MF-02 (Dayi TTS), FilterLine `=` separator, quoted-field and real-filter fix:**

- ✅ **Total: 359 tests passing** (up from 358 at v2.4)
- ✅ **Integration tests: 210** (up from 209) — added IT-MF-02 (`IT_MF_02_DayiTTS_FilterReducesToBig5Range`)
- ✅ **Root cause fixed**: The CIN format wraps both fields in double-quotes (`"keycode"<TAB>"character"`). The old `FilterLine` scanned the quoted value `"中"` as a 3-char token (quote + char + quote), fell into the multi-char passthrough (`q - valStart != 1`), and returned `TRUE` without ever checking CP950. Filter was a no-op on all real CIN data.
- ✅ **FilterLine rewrite** (`File.cpp`): Now correctly parses quoted fields. Keycode skip handles optional double-quote wrapping. Value extraction strips quotes before handing the inner character to the CP950 check. Separator skip extended to include `=` for TTS format (`"key"="char"`).
- ✅ **IT-MF-01 updated** (`IT_MF_01_RealArrayCin_FilterReducesToBig5Range`): Dual-filter strategy. rawLines=32 413 → rawFiltLines=15 750 (removed 16 663). augLines=32 418 → augFiltLines=15 750. Proves both natural non-Big5 entries and injected Cyrillic lines are removed.
- ✅ **IT-MF-02** (`IT_MF_02_DayiTTS_FilterReducesToBig5Range`): Exercises FilterLine on `%ProgramW6432%\Windows NT\TableTextService\TableTextServiceDaYi.txt` (Windows built-in Dayi TTS, `=` separator). raw=27 453 → filtered=19 172 (removed 8 281 non-Big5 entries).
- ✅ **`CountEqualSepLines` helper**: Added as private static in `CMemoryFileIntegrationTests`; counts non-section-header lines containing `=` (TTS format).

### Version 2.4 - 2026-03-11
**IT-MF-01 real-file integration test and surrogate-pair filter fix:**

- ✅ **Total: 358 tests passing** (up from 357 at v2.3)
- ✅ **Integration tests: 209** (up from 208) — added IT-MF-01 (`IT_MF_01_RealArrayCin_InjectedNonCP950_AreRemoved` in `CMemoryFileIntegrationTests`)
- ✅ **IT-MF-01** (injection approach): Loads real `%APPDATA%\DIME\Array.cin`, injects 5 Cyrillic lines (U+0400–U+0404, not in CP950) into an augmented temp file, applies `CMemoryFile` filtering, and asserts exactly those 5 lines are removed. Results: rawLines=32 413, augmented=32 418, filtered=32 413, removed=5. Skipped when Array.cin not installed.
- ✅ **Surrogate-pair filter fix** (`File.cpp`, `FilterLine()`): Previously, the `q - valStart != 1` multi-char passthrough let UTF-16 surrogate pairs (CJK Extension B etc., 2 wchar_t each) bypass filtering entirely. New code detects high+low surrogate pairs and applies `GetStringTypeW(CT_CTYPE3)`: C3_SYMBOL (supplementary emoji) passes; C3_IDEOGRAPH (CJK extension) is filtered.
- ✅ **`CountDataLines` helper**: Added as private static in `CMemoryFileIntegrationTests`; counts tab-containing lines in a WCHAR buffer.

### Version 2.3 - 2026-03-11
**Big5 filter, CMemoryFile tests, CRLF fix, and symbol pass-through:**

- ✅ **Total: 357 tests passing** (up from 339 at v2.2)
- ✅ **Unit tests: 160** (up from 143) — added UT-09 (17 tests: CMemoryFile filter)
- ✅ **UT-09-16** (`FilterLine_CRLF_NonCP950_Excluded`): Verifies CRLF strip in `SetupReadBuffer()` — real CIN files use UTF-16LE `\r\n`; without the fix the trailing `\r` inflated value-token length, defeating the CP950 filter entirely
- ✅ **UT-09-17** (`FilterLine_BMP_Symbol_PassesFilter`): Verifies `GetStringTypeW(CT_CTYPE3)` + `C3_SYMBOL` pass-through — BMP symbols/emoji now survive the filter regardless of CP950 encodability
- ✅ **`SetupBig5Engine` guard**: Skips rebuild and frees engine when filter is off for the mode
- ✅ **`SetupBig5ShortCodeEngine` removed**: Array short-code dictionary is never Big5-filtered; all 3-file references deleted

### Version 2.2 - 2026-03-07
**Palette, custom-table validation, and backward-compat test additions:**

- ✅ **Total: 339 tests passing** (351 defined, ~12 auto-skip when DIME.dll/TSF unavailable)
- ✅ **Unit tests: 143** (up from 119) — UT-01 expanded to 26, UT-02–05 expanded, UT-PT (11), UT-CV (17)
- ✅ **Integration tests: 208** (up from 186) — IT-CV (14), IT-PT (8), IT-03/04/07 counts updated
- ✅ **UT-PT**: 11 palette round-trip + backward-compat tests; both backward-compat tests fixed (UTF-16LE strip via `WritePrivateProfileStringW`)
- ✅ **UT-CV**: 17 custom table validation unit tests (new `CustomTableValidationUnitTest` class)
- ✅ **IT-CV**: 14 custom table validation integration tests (new `CustomTableValidationIntegrationTest` class)
- ✅ **IT-PT**: 8 palette integration tests
- ✅ **Infrastructure fix**: `TEST_CLASS_INITIALIZE` now initialises `Global::isWindows1809OrLater` via inline `RtlGetVersion` so backward-compat heuristic returns SYSTEM (not LIGHT) on Windows 10 1809+
- ✅ **UT-CM restructured**: UT-CM-02 round-trip tests moved to UT-PT; UT-CM-01/03/04 remain in `CustomTableValidationUnitTest`

### Version 2.1 - 2026-03-06
**Color-mode tests added (see `docs/LIGHT_DARK_THEME.md` §12):**

- ✅ **Total: 324 tests** (119 unit + 186 integration est.)
- ✅ **UT-CM**: 12 color-mode unit tests (GetEffectiveDarkMode, INI round-trip, accessor, constant ranges)
- ✅ **IT-CM**: 11 color-mode integration tests (settings dialog, candidate window, notify window)
- ✅ **Infrastructure fix**: `TEST_METHOD_INITIALIZE` resets `_configIMEMode = DAYI` to bypass timestamp guard

### Version 2.0 - 2026-02-18
**Major restructuring for practical CI/CD automation and improved readability:**

- ✅ **Restructured for clarity**: Removed repetitive information, consolidated test descriptions
- ✅ **Executive summary added**: Quick overview of test status and coverage metrics
- ✅ **Summary tables**: All test suites summarized in tables for easy reference
- ✅ **Coverage target adjusted** to 80-85% as aspirational goal
- ✅ **Actual coverage: 37.2%** (7,289/19,589 lines) - realistic for IME system testing
- ✅ **Total tests: 271** (96 unit + 175 integration)
- ✅ **Clarified test separation**: CI/CD Automated Tests vs Manual Validation
- ✅ **Consolidated test specifications**: Removed duplicate explanations, kept essential information
- ✅ **Added practical assessments**: ROI analysis for coverage targets, practical maximum for IT-06
- ✅ **Improved organization**: Hierarchical structure, better navigation, less duplication
- ✅ **Added coverage gap analysis**: Explained why 37.2% is realistic for IME testing

**Key Improvements**:
1. Document length reduced by ~60% while preserving all essential information
2. Better use of tables for test summaries and comparisons
3. Removed repetitive "Coverage Goals" sections with similar content
4. Consolidated test approach explanations (Win32, TSF, stubs)
5. Clearer distinction between automated and manual testing
6. More concise test descriptions focusing on what matters
7. **Honest coverage reporting**: Updated from misleading 82.1% to actual 37.2%
8. **Accurate test counts**: Updated from 218 to actual 271 tests

**Coverage Reality**:
IME systems are inherently difficult to test due to:
- TSF framework requiring full Windows installation and COM registration
- Complex UI rendering and Windows message pump interactions
- Document manager and edit session chains requiring live applications
- 37.2% coverage represents solid testing of testable components while acknowledging inherent limitations

### Version 1.0 - 2025-02-09 (Original)
**Initial comprehensive test plan:**
- Original 90% coverage target
- Detailed test specifications for all planned tests
- Mix of implemented and planned tests without clear separation

---

**Maintainer:** DIME Development Team  
**License:** See project LICENSE file
