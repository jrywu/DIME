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

---

## Refactor completion (2026-04-28) — incomplete-side-effect fix

### Status of original items

| Item | Status |
|------|--------|
| A. `EnumerateReverseConversionProviders` in `Config.cpp` | Implemented |
| B. `SetReverseConversionSelection` in `Config.cpp` | **Partially implemented** — see below |
| C. `GetReverseConvervsionInfoList` static | Implemented |
| D. `useLegacyUI = true` default | Reverted to `false` (intended end-state — new UI is default) |

### Issue

After the new SettingsWindow shipped, users reported: setting the reverse-lookup IM, then turning it off (selecting "(無)") and saving, the reverse-lookup notify continued to appear during composition. Confirmed in `%APPDATA%\DIME\DayiConfig.ini`:

```text
ReloadReverseConversion = 0
ReverseConversionCLSID = {00000000-0000-0000-0000-000000000000}
ReverseConversionGUIDProfile = {00000000-0000-0000-0000-000000000000}
ReverseConversionDescription = (無)
```

CLSIDs correctly null, but `ReloadReverseConversion = 0` — meaning the IME-side cache `_pITfReverseConversion[mode]` is never released, and `CandidateHandler.cpp::_HandleCandidateWorker` keeps firing `_AsyncReverseConversion` because that gate (line 153) checks the cached pointer, not the CLSID.

### Root cause — incomplete extraction in item B

Item B moved the **model logic** (CLSID / GUIDProfile / Description assignment) from `ConfigDialog.cpp:1513-1529` into `Config.cpp::SetReverseConversionSelection`. But the legacy view also has an associated **side-effect** at `ConfigDialog.cpp:1295`:

```cpp
case CBN_SELCHANGE:
    PropSheet_Changed(GetParent(hDlg), hDlg);
    ret = TRUE;
    _reloadReverseConversion = TRUE;   // view-layer side-effect — NOT moved into model
```

This `_reloadReverseConversion = TRUE` line is conceptually a model-layer concern: it gates the IME-side runtime cache lifecycle in `CDIME::_LoadConfig` ([DIME.cpp:1053-1086](../src/DIME.cpp#L1053-L1086)). It tells the IME "your `_pITfReverseConversion[mode]` cache is stale; release and (maybe) recreate on next focus". Leaving it in the view meant:

| Path | API used | Sets reload flag? |
|------|----------|-------------------|
| Legacy `ConfigDialog.cpp` combo SELCHANGE | inline `_reloadReverseConversion = TRUE` | yes |
| Legacy `ConfigDialog.cpp` apply | `SetReverseConversionSelection(sel)` | yes (already set on SELCHANGE above) |
| **New `SettingsWindow.cpp`** | `SetReverseConversionSelection(sel)` | **no** — ☠️ regression entry point |
| **CLI `--set ReverseConversion*`** | direct lower-level setters | **no** — same issue |
| `.ini` load via `DictionarySearch` | direct lower-level setters | no (correct: load must NOT self-trigger reload) |

### Solution — finish item B by moving the side-effect into the model

Setting `_reloadReverseConversion = TRUE` belongs inside `SetReverseConversionSelection`. Any UI that mutates the selection through the model API gets correct semantics for free; the legacy view's manual line at line 1295 becomes redundant (harmless duplicate).

#### Change 1 — `Config.cpp::SetReverseConversionSelection`

- Free previous `_reverseConversionDescription` before allocating the new one (plug pre-existing leak).
- At end of the function, set `_reloadReverseConversion = TRUE`.

#### Change 2 — CLI `--set` for any of the three RC fields

[`CLI.cpp::HandleSetSingle`](../src/DIMESettings/CLI.cpp#L387-L422) calls direct setters (`SetReverseConverstionCLSID` / `SetReverseConversionGUIDProfile` / `SetReverseConversionDescription`) rather than `SetReverseConversionSelection`. Each arm must explicitly call `CConfig::SetReloadReverseConversion(TRUE)`. Cannot promote the underlying setters because they're also used by the `.ini` load path which must not self-trigger reload.

#### Change 3 — Defense in depth: gate on CLSID, not just cached pointer

[`CandidateHandler.cpp:153`](../src/CandidateHandler.cpp#L153):

```cpp
// Before:
if (_pITfReverseConversion[(UINT)Global::imeMode])

// After:
if (_pITfReverseConversion[(UINT)Global::imeMode]
    && !IsEqualCLSID(CConfig::GetReverseConverstionCLSID(), CLSID_NULL)
    && !IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL))
```

Aligns the runtime gate with the lifecycle gate in `_LoadConfig`. If any future view ever bypasses the reload flag, the notify still won't fire when the user has selected "(無)".

#### Change 4 — Null-guard in `_AsyncReverseConversionNotification`

[`ReverseConversion.cpp:106`](../src/ReverseConversion.cpp#L106) dereferences `_pITfReverseConversion[mode]` directly. The async edit session can run after `_LoadConfig` releases the provider; defensive null check.

#### Change 5 — Self-heal stale `.ini` state in `_LoadConfig`

For users whose `.ini` already has the broken `ReloadReverseConversion = 0` + null CLSIDs combination, add a consistency check at the top of [`CDIME::_LoadConfig`](../src/DIME.cpp#L1053):

```cpp
BOOL clsidIsNull = IsEqualCLSID(CConfig::GetReverseConverstionCLSID(), CLSID_NULL)
                || IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL);
BOOL providerCached = (_pITfReverseConversion[(UINT)imeMode] != nullptr);
if (clsidIsNull && providerCached)
    CConfig::SetReloadReverseConversion(TRUE);
```

Forces release on the next focus event, regardless of what the `.ini` flag said.

### Files changed (this completion)

| File | Change |
|------|--------|
| `src/Config.cpp` | `SetReverseConversionSelection`: free old description, set reload flag |
| `src/DIMESettings/CLI.cpp` | Three RC `--set` arms each call `SetReloadReverseConversion(TRUE)` |
| `src/CandidateHandler.cpp` | Gate `_AsyncReverseConversion` on CLSID + GUIDProfile, not just cached pointer |
| `src/ReverseConversion.cpp` | Null-guard `_pITfReverseConversion[mode]` before deref |
| `src/DIME.cpp` | `_LoadConfig`: detect stale-provider/null-CLSID inconsistency, force reload |
| `src/UIPresenter.cpp` | `ShowNotifyText` side-by-side: anchor notify to `_candLocation.{x,y}` instead of `_notifyLocation.{x,y}` (see CARET_TRACKING.md §11) |

### Change 6 — notify position covers typing text when assoc + RC both on (Chromium hosts)

After Changes 1-5 made the on/off toggle work end-to-end, a follow-up bug surfaced when associated phrase + reverse-lookup were both enabled in Chromium-based hosts (Outlook web, Teams, Slack, etc.): the reverse-lookup notify appeared at the typing row, covering the composition text. Notepad was unaffected because its candidate window doesn't flip-above in the test scenario.

**Cause:** the side-by-side branch in `ShowNotifyText` ([UIPresenter.cpp:1575](../src/UIPresenter.cpp#L1575)) used `_notifyLocation.{x,y}` for the move target. `_notifyLocation.y` is seeded from `guiInfo->rcCaret.bottom` (caret y) by the immediately preceding code. When the phrase candidate is shown below the typing line, the notify lands at caret y rather than candidate y — i.e. on the typing row.

**Fix:** anchor both x and y to `_candLocation.{x,y}` (the candidate's actual top-left, maintained by `_LayoutChangeNotification`). This matches the equivalent cand-aligned positioning in `_LayoutChangeNotification` ([UIPresenter.cpp:999-1011](../src/UIPresenter.cpp#L999-L1011)), which has been correct since a97d654.

The non-side-by-side branch (no candidate visible) still uses `_notifyLocation` — preserving the assoc-OFF behavior the user reported as already correct.

See [CARET_TRACKING.md §11](CARET_TRACKING.md) for the architectural write-up.

### Optional cleanup (deferred)

- Delete the now-redundant `_reloadReverseConversion = TRUE` at `ConfigDialog.cpp:1295`. Behavior unchanged either way; deletion just removes duplication.

### Out of scope (call-out only)

- `tests/CLIIntegrationTest.cpp:851` writes `ReverseConversionDescription = TestDescription` to the user's real `%APPDATA%\DIME\DayiConfig.ini` (test-isolation defect, unrelated to this bug).
- Variable-name typo: `_reverseConverstionCLSID` (and `Set/GetReverseConverstionCLSID`) should be spelled "Conversion". Cosmetic; renaming would touch the legacy dialog, the new SettingsWindow, the CLI, and the IME — defer until there's another reason to sweep that surface.

### Lesson for future refactors

When extracting model logic from a view, audit the surrounding view for **side-effects associated with the same event** (state flags, event signals, cache invalidations). Moving the assignment without the side-effect leaves the model under-powered and any new view that uses only the new API will silently lose correctness.
