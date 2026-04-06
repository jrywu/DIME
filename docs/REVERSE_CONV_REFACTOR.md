# Model/View separation for reverse conversion code

## Context

Reverse conversion logic is scattered across files. Model logic should be in `Config.cpp`, view logic stays in `ConfigDialog.cpp`.

## What to move

### A. Enumeration: `DIMESettings.cpp` → `Config.cpp`

Move the enumeration block from `DIMESettings.cpp` (lines 478-491) into `Config.cpp` as a new static method `EnumerateReverseConversionProviders(LANGID)`. `DIMESettings.cpp` calls the new method instead.

### B. Selection save: `ConfigDialog.cpp` → `Config.cpp`

Extract model logic from `ConfigDialog.cpp` (lines 1513-1529) into `Config.cpp` as `SetReverseConversionSelection(UINT sel)`. `ConfigDialog.cpp` keeps `GetDlgItem`/`CB_GETCURSEL` UI code and calls the new method.

### C. Make `GetReverseConvervsionInfoList()` static

Currently non-static but returns static member. Add `static`.

### D. Switch default to legacy UI

In `DIMESettings.cpp`, set `useLegacyUI = true` so `DIMESettings.exe` launches the old PropertySheet dialog by default. This allows verifying the refactor works with the existing UI before integrating with the new UI.

## Files

| File | Change |
|------|--------|
| `src/Config.h` | Add 2 method declarations, make getter static |
| `src/Config.cpp` | Add 2 methods (moved code) |
| `src/ConfigDialog.cpp` | Replace inline save with method call |
| `src/DIMESettings/DIMESettings.cpp` | Replace inline enumeration with method call, set `useLegacyUI = true` |

## Tests

No test modifications needed. Existing tests (`CLIIntegrationTest.cpp`) only test CLI set/get of `ReverseConversionCLSID`/`Description`/`GUIDProfile` via INI — unaffected by this refactor.

## Verification

1. Build DIME.sln (Win32) — 0 errors
2. Run `DIMESettings.exe` — opens legacy dialog, verify 反查輸入字根 combo populated and saves correctly
