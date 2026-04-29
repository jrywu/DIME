# Custom Table Validation — Design & Implementation

- **Created:** 2026-03-01 (legacy ConfigDialog — Phases 1-5)
- **Major rewrite:** 2026-04-29 alongside [#130 fix](./#130_ISSUE.md). Legacy property-page implementation in `Config.cpp` is no longer the active surface; the new SettingsWindow (`src/DIMESettings/`) is.
- **Final pass:** 2026-04-29 (sync to actual shipped code — see Appendix B 7.0).
- **Status:** Production. The current architecture is documented below; the historical Phase-1–5 design is summarised in §Appendix A for reference.

## 1. Goals and constraints

The custom table editor lets users hand-edit `<key> <whitespace> <value>` rows. Validation must:

1. Catch malformed lines before the parser writes a corrupt `.cin` file.
2. Provide actionable feedback (visible-line highlighting + auto-jump to first error on Save).
3. **Stay responsive on tens of thousands of lines.** Issue #130 reproduced a UI hang from a 73,278-line user file; that is the hard performance ceiling we design against.
4. Use the **same validation function** for GUI and CLI so `--import-custom`'s pre-flight matches GUI Save's contract exactly.

## 2. Trigger paths and call chains

Three paths share the same primitives — `LoadTextFileAsUtf16` for reads, `ValidateLine` for format checks, and the same UTF-16+BOM `.txt` write + `parseCINFile(customTableMode=TRUE)` persist sequence. Internals in §3 onward; this section answers *when* validation runs and *why*.

### 2.1 GUI 「匯入自建詞庫」 button → 「儲存」

```text
RowAction::ImportCustomTable
  └─ LoadTextFileAsUtf16
  └─ ValidateCustomTableBuffer (in-memory pre-scan)
       └─ SettingsModel::ValidateLine (per line, pEngine = wd->pEngine)
       └─ on errors: count + first-error line recorded, but DO NOT abort
  └─ EM_GETEVENTMASK / EM_SETEVENTMASK(0)              ← mute EN_CHANGE
  └─ SetWindowTextW                                     ← bulk load (always)
  └─ EM_SETEVENTMASK(prev)                              ← restore ENM_CHANGE
  └─ EnableSaveButton(TRUE)                             ← Import = new content vs disk
  └─ ValidateViewport                                   ← FG paint visible (TOM)
  └─ if errCount > 0:
       ├─ ThemedMessageBox (warning, non-blocking — load already succeeded)
       └─ on dismiss: SetFocus(hRE) + EM_SETSEL(line) + EM_SCROLLCARET
                      + ValidateViewport (paint destination)

(user clicks 儲存)
RowAction::SaveCustomTable
  └─ GetWindowTextW
  └─ ValidateCustomTableBuffer (in-memory, O(N) — full pre-persist gate)
       └─ on errors:
            ├─ ThemedMessageBox (with first-N line numbers)
            └─ on dismiss: SetFocus + EM_SETSEL(firstErrorLine) + EM_SCROLLCARET
                           + ValidateViewport — abort write
       └─ on clean: continue
  └─ CreateFileW + WriteFile         (writes <MODE>-Custom.txt as UTF-16+BOM)
  └─ CConfig::parseCINFile(customTableMode=TRUE)
                                     (writes <MODE>-Custom.cin)
  └─ EnableSaveButton(FALSE)         (mark not-dirty until next edit)
```

### 2.2 RichEdit 直接編輯 → 「儲存」

```text
(settings page open — initial load)
SettingsWindow build code
  └─ EM_EXLIMITTEXT(100 MB)                             ← RichEdit default cap is
                                                         ~32-64K chars and silently
                                                         rejects user input above it;
                                                         must raise immediately after
                                                         CreateWindow.
  └─ LoadTextFileAsUtf16
  └─ SetWindowTextW                                     ← ENM_CHANGE not yet set
  └─ EM_SETEVENTMASK(ENM_CHANGE)                        ← enable EN_CHANGE going forward
  └─ ValidateViewport                                   ← FG paint visible (TOM)
                                                         (Save stays disabled — content
                                                          matches disk, not dirty)

(per keystroke / paste / Enter / Space — foreground only)
EN_CHANGE handler  (or WM_USER+1 deferred from subclass on Enter/Space/Paste/large-delete)
  ├─ ValidateViewport                                    ← TOM-based, no caret jump
  ├─ CancelBgScan                                        ← defensive no-op (BG never runs)
  └─ EnableSaveButton(TRUE)                              ← mark dirty

WM_VSCROLL / WM_MOUSEWHEEL (forwarded by RichEdit subclass as WM_USER+3)
  └─ ValidateViewport only (paint newly-visible lines)

(user clicks 儲存)
RowAction::SaveCustomTable
  └─ (same as path 2.1, Save stage — full validate then persist)
```

**Save-button state machine** (simple dirty-bit model — no BG/timer dependency):

| Event | Save button |
| --- | --- |
| Settings page open (initial load matches disk) | disabled |
| Import button → bulk load (with or without errors) | **enabled** (loaded content differs from disk) |
| Any `EN_CHANGE` / `WM_USER+1` (user edited) | **enabled** (dirty) |
| Save click → `ValidateCustomTableBuffer` fails | enabled (still dirty; user must fix) |
| Save click → `ValidateCustomTableBuffer` passes → persist | disabled (no longer dirty) |

**Key design choices:**

- **TOM-based painting only.** `ValidateViewport` and `ValidateCustomTableRE_OneLine` use `ITextDocument::Range` + `ITextFont::SetForeColor` — selection is never touched, so live highlighting on every keystroke causes no caret jumps and no flicker.
- **EN_CHANGE reentrancy guard.** `ValidateViewport` brackets its painting loop with `EM_SETEVENTMASK(0)` / restore. Without this, RichEdit fires EN_CHANGE on some CHARFORMAT-only operations and the handler would recurse into itself, pegging the message pump.
- **Live painting is viewport-only.** Errors outside the visible range are not pre-painted. They surface when the user scrolls (`WM_USER+3` → `ValidateViewport`) or clicks Save (full in-memory validation with auto-jump to first error on dismiss).
- **Save validates synchronously at click time.** `ValidateCustomTableBuffer` is in-memory O(N) — ~500 ms for 73K lines, acceptable for an explicit Save action. On errors, the message box reports first-N line numbers, the caret auto-jumps to the first error after dismiss, write is aborted.
- **Import is non-blocking.** Pre-scan finds errors but the bulk load happens regardless; user sees the content and can fix in place. CLI keeps strict abort-on-error (no UI to inspect).

**RichEdit text limit:** RichEdit50W's default cap is ~32-64K characters. `SetWindowTextW` bypasses the cap (large bulk loads succeed) but the editor then silently rejects user input because the buffer is over-limit. `EM_EXLIMITTEXT(100 MB)` is set immediately after `CreateWindow` to prevent this.

### 2.3 CLI `--import-custom`

CLI keeps the original strict pre-flight model — there's no editor to inspect from a headless invocation, so any error aborts.

```text
RunCLI
  └─ HandleImportCustom
       └─ LoadTextFileAsUtf16                           ← shared with GUI
       └─ SettingsModel::CreateEngine                   ← same factory the GUI uses
       └─ SettingsModel::ValidateLine (per line, pEngine = engine)
            └─ on any failure → fwprintf line numbers, exit 3, no write to disk
       └─ SettingsModel::DestroyEngine
       └─ CreateFileW + WriteFile                       (writes <MODE>-Custom.txt as
                                                         UTF-16+BOM, same as GUI 儲存)
       └─ CConfig::parseCINFile(customTableMode=TRUE)
                                                         (writes <MODE>-Custom.cin)
```

`--no-validate` skips the pre-flight (power users importing master `.cin` files with non-standard sections).

### 2.4 Shared spine

| Stage | Function | GUI Import | RichEdit edit | CLI |
| --- | --- | --- | --- | --- |
| Read | `LoadTextFileAsUtf16` | ✓ | ✓ (initial load) | ✓ |
| Validate (live, viewport) | `ValidateViewport` (TOM, ~visible lines) | ✓ (post-load) | ✓ (per-keystroke + scroll) | — |
| Validate (full) | `ValidateCustomTableBuffer` (in-memory, O(N)) | ✓ (pre-load scan, non-blocking) + ✓ (on Save) | ✓ (on Save) | — |
| Validate (full, CLI) | per-line `ValidateLine` over `LoadTextFileAsUtf16` output | — | — | ✓ (pre-flight, abort on error) |
| Persist `.txt` (UTF-16+BOM) | `CreateFileW` + `WriteFile` | ✓ (on Save) | ✓ (on Save) | ✓ |
| Persist `.cin` | `CConfig::parseCINFile(customTableMode=TRUE)` | ✓ (on Save) | ✓ (on Save) | ✓ |
| Auto-jump to first error | `EM_LINEINDEX` + `EM_SETSEL` + `EM_SCROLLCARET` | ✓ (after warning dismiss) | ✓ (after Save error dismiss) | — |
| `pEngine` lifetime | — | long-lived in `wd` | long-lived in `wd` | constructed for the import, freed after |

The on-disk format and the destination files are identical across all three paths.

## 3. Validation architecture

### 3.1 Single source of truth

```text
SettingsModel::ValidateLine(line, len, mode, maxCodes, pEngine) → LineValidation
  (src/DIMESettings/SettingsController.cpp)
```

Pure function. Called from three callers:

| Caller | File | Purpose |
| --- | --- | --- |
| `ValidateCustomTableRE_OneLine` | SettingsWindow.cpp | Single-line painter — reads via `EM_GETTEXTRANGE`, validates, paints valid + error colours via TOM. Used by `ValidateViewport`. |
| `ValidateCustomTableBuffer` | SettingsWindow.cpp | Per-line iteration over an in-memory `wchar_t*` buffer; no UI side effects. Used by Import pre-scan, Export pre-scan, and Save's pre-persist gate. |
| `HandleImportCustom` (CLI) | DIMESettings/CLI.cpp | Pre-flight loop over `LoadTextFileAsUtf16` output before persist. |

### 3.2 Validation hierarchy

`ValidateLine` runs four levels in sequence; first failure short-circuits.

| Level | Check | Cost | Reports |
| --- | --- | --- | --- |
| **0 — length cap** | `(e - s) > MAX_TABLE_LINE_LENGTH` (`= 1024`, [src/Define.h:173](../src/Define.h#L173)) | O(1) | `LineError::Format` over whole trimmed range |
| **1 — format** | manual scan: exactly two non-whitespace tokens (key + value); a third token after the value is a format error | O(line length) | `LineError::Format` over whole trimmed range |
| **2 — key length** | `key.length() > maxKeyLength` where `maxKeyLength = (PHONETIC) ? MAX_KEY_LENGTH : maxCodes` | O(1) | `LineError::KeyTooLong` over key |
| **3 — char check** | per-char engine validation (or basic-range fallback when `pEngine == nullptr`) | O(key length) | `LineError::InvalidChar` per offending character (multiple errors per line possible) |

Level 1 used to be `std::wregex(L"^([^\\s]+)\\s+(.+)$")`. That regex catastrophically backtracked on a single very-long no-whitespace input and was the proximate hang in #130. The manual scan replaces it. Level 0 is a hard cap added at the same time so no future bug can re-introduce a multi-MB single-line input to the validator. Level 1 was tightened from "key + whitespace + anything" to "key + whitespace + value with no trailing token" so lines like `roc 中華民國 fdfdafds` (3 tokens) are caught.

### 3.3 Character validation rules

| IME mode | Rule |
| --- | --- |
| Phonetic | Printable ASCII `!`–`~` (0x21–0x7E); excludes space, `*`, `?` |
| Dayi / Array / Generic | A–Z accepted via quick path; otherwise `pEngine->ValidateCompositionKeyCharFull(c)`; if `pEngine == nullptr`, fallback range check `[32, 32 + MAX_RADICAL]` |

CLI builds a real engine via `SettingsModel::CreateEngine(args.mode)` so its Level-3 check is identical to the GUI's (no fallback gap).

## 4. Read path — encoding-aware loader

`SettingsModel::LoadTextFileAsUtf16(path, &outLen)` ([src/DIMESettings/SettingsController.cpp](../src/DIMESettings/SettingsController.cpp)):

1. UTF-16LE BOM (`FF FE`) → strip BOM, copy as-is
2. UTF-16BE BOM (`FE FF`) → byte-swap to LE
3. UTF-8 BOM (`EF BB BF`) → skip BOM
4. Strict UTF-8 sniff via `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)` → if it fails, fall back to:
5. `MultiByteToWideChar(CP_ACP, 0, ...)` — CP950/Big5 on zh-TW Windows

Used by:

- GUI initial load on settings page open
- GUI Import button
- CLI `--import-custom`

Before #130 the read paths cast file bytes directly into `WCHAR[]` with no decoding, which aliased Big5/UTF-8 input to ~550K-character single mega-lines. The helper eliminates that class of bug at the read boundary.

## 5. Performance regimes

All live-edit work is bounded by the visible viewport size (typically 20-50 lines), independent of total document size. Save's full validation is the only path that scales with line count, and is gated by an explicit user click.

| Scenario | Validator | Cost |
| --- | --- | --- |
| Live edit (`EN_CHANGE` / `WM_USER+1`) | `ValidateViewport` (TOM, visible lines only) | < 1 ms per fire — typing stays responsive at any document size |
| Scroll (`WM_USER+3`) | `ValidateViewport` | < 1 ms per fire |
| Initial-load (settings page open) | `ValidateViewport` once after `SetWindowTextW` | < 1 ms |
| Import button — pre-load scan | `ValidateCustomTableBuffer` over `LoadTextFileAsUtf16` output | ~500 ms for 73K lines, runs once |
| Save (儲存) | `ValidateCustomTableBuffer` + persist | ~500 ms validation + < 100 ms write |
| CLI `--import-custom` pre-flight | per-line `ValidateLine` over `LoadTextFileAsUtf16` output | ~500 ms for 73K lines |

## 6. Bulk-load handling

`SetWindowTextW` on the RichEdit fires `EN_CHANGE`, which would trigger `ValidateViewport`. For Import (which loads 70K+ lines in one shot) the EN_CHANGE-driven viewport pass is unnecessary work, so the handler is muted around the bulk write:

```text
LoadTextFileAsUtf16        ← decode source
ValidateCustomTableBuffer  ← pre-scan in memory (errors recorded, not blocking)
EM_GETEVENTMASK            ← save current event mask
EM_SETEVENTMASK(0)         ← mute EN_CHANGE
SetWindowTextW             ← bulk write — does NOT trigger live validator
update ctLastLineCount
EM_SETEVENTMASK(prev)      ← restore (ENM_CHANGE re-enabled for user edits)
ValidateViewport           ← single explicit FG paint of the new visible range
```

Initial-load uses the same protection implicitly: `EM_SETEVENTMASK(ENM_CHANGE)` is set *after* the initial `SetWindowTextW`.

## 7. Persist sequence (shared across GUI Save and CLI)

```text
GUI Save (儲存):
  GetWindowTextW
  → ValidateCustomTableBuffer (full O(N) gate; on errors → message box + abort)
  → CreateFileW + WriteFile UTF-16LE BOM + content   ← writes <MODE>-Custom.txt
  → CConfig::parseCINFile(txtPath, cinPath, customTableMode=TRUE, suppressUI=TRUE)
                                          ← reads .txt as ccs=UTF-16LE,
                                            writes %chardef-wrapped <MODE>-Custom.cin
  → EnableSaveButton(FALSE)               ← mark not-dirty until next edit

CLI --import-custom:
  use already-decoded text from LoadTextFileAsUtf16
  → SettingsModel::ValidateLine per line  ← pre-flight (skippable via --no-validate)
                                            (exit 3 on failure, per-line stderr report)
  → CreateFileW + WriteFile UTF-16LE BOM + content   ← writes <MODE>-Custom.txt
  → CConfig::parseCINFile(txtPath, cinPath, customTableMode=TRUE, suppressUI=TRUE)
                                          ← writes <MODE>-Custom.cin
```

`customTableMode=TRUE` is essential: it tells `parseCINFile` to read the `.txt` as `ccs=UTF-16LE` (matching the BOM we just wrote) and to emit an escape-mode `.cin` for the IME runtime. The legacy CLI path that called `parseCINFile(..., FALSE)` against the **original** source file is gone — that path silently truncated Big5 input because it opened the source as `ccs=UTF-8` and bailed on the first non-UTF-8 byte.

## 8. Live-edit triggers

| Event | Source | Action |
| --- | --- | --- |
| `EN_CHANGE` | RichEdit (any edit) | `ValidateViewport` + `EnableSaveButton(TRUE)` |
| `WM_KEYDOWN VK_SPACE` / `VK_RETURN` | RichEdit subclass | Posts `WM_USER+1` (deferred validate after the keystroke is fully applied) |
| `WM_KEYDOWN VK_DELETE` / `VK_BACK` with selection > `CUSTOM_TABLE_LARGE_DELETION_THRESHOLD` (10) | RichEdit subclass | Posts `WM_USER+1` |
| `WM_PASTE` | RichEdit subclass | Posts `WM_USER+1` |
| `WM_USER+1` | parent ContentWndProc | `ValidateViewport` + `EnableSaveButton(TRUE)` (same as `EN_CHANGE`) |
| `WM_VSCROLL` / `WM_MOUSEWHEEL` | RichEdit subclass | Posts `WM_USER+3` (foreground-only viewport revalidate) |
| `WM_USER+3` | parent ContentWndProc | `ValidateViewport` only — paint newly-visible lines, no Save state change |

## 9. Theme support

Validation colours are theme-aware. The active theme comes from `WindowData::isDarkTheme` (set at settings window construction; updated on `WM_THEMECHANGED`). `ValidateCustomTableRE_OneLine` and `PaintErrorRange` select COLORREFs from `CUSTOM_TABLE_LIGHT_*` / `CUSTOM_TABLE_DARK_*` constants ([src/Define.h](../src/Define.h)):

| Constant | Role |
| --- | --- |
| `CUSTOM_TABLE_*_ERROR_FORMAT` | Level 0/1 — entire offending line |
| `CUSTOM_TABLE_*_ERROR_LENGTH` | Level 2 — entire key |
| `CUSTOM_TABLE_*_ERROR_CHAR` | Level 3 — per-character |
| `CUSTOM_TABLE_*_VALID` | reset/OK colour |

## 10. Configuration constants

| Constant | Value | File | Role |
| --- | --- | --- | --- |
| `MAX_TABLE_LINE_LENGTH` | 1024 | Define.h | Level-0 cap; also `parseCINFile` `fgetws` buffer |
| `MAX_KEY_LENGTH` | 64 | Define.h | Phonetic mode key cap |
| RichEdit text limit | 100 MB chars | hard-coded `EM_EXLIMITTEXT` call in `CreateRichEditorControl` | Allows the editor to accept user input on buffers larger than the default ~32-64K cap |
| `CUSTOM_TABLE_VALIDATE_KEYSTROKE_THRESHOLD` | 3 | Define.h | Legacy live-throttle (used by other call sites; the FG viewport pass is cheap enough not to need it) |
| `CUSTOM_TABLE_LARGE_DELETION_THRESHOLD` | 10 | Define.h | Selection size that triggers a deferred WM_USER+1 on Delete/Backspace |

## 11. Code locations

| Symbol | File | Notes |
| --- | --- | --- |
| `SettingsModel::ValidateLine` | [src/DIMESettings/SettingsController.cpp](../src/DIMESettings/SettingsController.cpp) | Single source of truth |
| `SettingsModel::LoadTextFileAsUtf16` | [src/DIMESettings/SettingsController.cpp](../src/DIMESettings/SettingsController.cpp) | Encoding-aware loader |
| `SettingsModel::CreateEngine` / `DestroyEngine` | SettingsController.cpp | Per-mode engine factory shared by GUI and CLI |
| `GetRichEditTextDocument` | SettingsWindow.cpp (file-static) | Gets `ITextDocument` via `EM_GETOLEINTERFACE`; caller releases |
| `PaintRangeColor` | SettingsWindow.cpp (file-static) | TOM-based foreground-colour painter — sets colour without touching selection |
| `PaintErrorRange` | SettingsWindow.cpp (file-static) | Per-error TOM painter (calls `PaintRangeColor` with the appropriate Level palette) |
| `GetViewportLineRange` | SettingsWindow.cpp (file-static) | Computes [firstVis, lastVis] from `EM_GETFIRSTVISIBLELINE` + `EM_CHARFROMPOS` |
| `ValidateCustomTableRE_OneLine` | SettingsWindow.cpp (file-static) | Validate + TOM-paint a single RichEdit line by index |
| `ValidateViewport` | SettingsWindow.cpp (file-static) | Foreground pass — validates visible range only; brackets the loop with `EM_SETEVENTMASK(0)` to prevent CHARFORMAT-fired EN_CHANGE recursion |
| `ValidateCustomTableBuffer` | SettingsWindow.cpp (file-static) | In-memory full-document validator (Import pre-scan, Export pre-scan, Save pre-persist gate) |
| `EnableSaveButton` | SettingsWindow.cpp (file-static) | Centralised Save-button enable/disable |
| Import button handler | SettingsWindow.cpp `RowAction::ImportCustomTable` | Pre-scan + EN_CHANGE-muted bulk load + ValidateViewport + non-blocking warning + auto-jump |
| Save handler | SettingsWindow.cpp `RowAction::SaveCustomTable` | `ValidateCustomTableBuffer` + persist; on errors → message box + auto-jump to first error |
| Export handler | SettingsWindow.cpp `RowAction::ExportCustomTable` | `ValidateCustomTableBuffer` + file dialog + write |
| Initial-load block | SettingsWindow.cpp `CreateRichEditorControl` (~L1840-L1880) | `EM_EXLIMITTEXT(100MB)` + `LoadTextFileAsUtf16` + `SetWindowTextW` + `EM_SETEVENTMASK(ENM_CHANGE)` + `ValidateViewport` |
| `ContentWndProc` `WM_USER+1` | SettingsWindow.cpp | Deferred validate after Enter/Space/Paste/large-delete (same body as EN_CHANGE) |
| `ContentWndProc` `WM_USER+3` | SettingsWindow.cpp | FG-only viewport revalidate after WM_VSCROLL / WM_MOUSEWHEEL |
| RichEdit subclass | SettingsWindow.cpp `CustomTableRE_SubclassProc` | Posts WM_USER+1 on Enter/Space/Paste/large-delete; forwards WM_VSCROLL / WM_MOUSEWHEEL as WM_USER+3; resets selection to (0,0) on WM_SETFOCUS |
| CLI `HandleImportCustom` | [src/DIMESettings/CLI.cpp](../src/DIMESettings/CLI.cpp) | Validate + UTF-16+BOM .txt + parseCINFile(TRUE) |

## 12. Test scenarios (regression matrix)

Repro artefacts under `.claude/txt/` from issue #130 are the canonical fixtures. CLI tests run headless; GUI tests are manual.

| # | Test | Pass criterion |
| --- | --- | --- |
| 1 | CLI: `--import-custom issue130_nowhitespace.txt --mode dayi` (100K-char no-whitespace single line) | Completes in < 2 s, exits 3, prints `Line 1: format error`. **Direct regression test for the catastrophic-backtracking RC** — without the Level-0 + manual-scan fix the call would never return. |
| 2 | CLI: `--import-custom issue130_source.txt --mode dayi` (Big5, 73,278 lines) | Validator decodes Big5 (CP_ACP fallback) → no errors → `DAYI-Custom.txt` is UTF-16+BOM with all lines of correct hanzi → `DAYI-Custom.cin` (escape-mode `%chardef`-wrapped) also written. No silent truncation. |
| 3 | CLI: `--import-custom` on UTF-8 with BOM and UTF-8 without BOM | Both exit 0, both produce identical 73,278-line `.txt` + `.cin`. |
| 4 | CLI: corrupt line (`TOOLONGKEY 詞`, key length 10 > maxCodes=4) | Exits 3, prints offending line number, does NOT write to `DAYI-Custom.txt` or `.cin`. |
| 5 | CLI: same corrupt file with `--no-validate` | Exits 0; writes `.txt` + `.cin` (whatever was decoded). Bypass for power users. |
| 6 | GUI: Settings → 自建詞庫 → 匯入 → pick `issue130_source.txt` | RichEdit fills in seconds with proper Big5 hanzi (no mojibake, no spinner). Save button **enables**. Editor accepts user input (caret moves and characters are inserted). Click Save → `.txt` + `.cin` written, button disables. |
| 7 | GUI: Import `fg_bg_2k.txt` (2,000 valid + 50 `BADBADBADBAD 詞` lines) | Warning message box: "匯入完成，但偵測到 50 個格式錯誤行，第一個錯誤在第 2001 行 ..." On dismiss, caret jumps to line 2001 with the orange "key too long" highlight visible. |
| 8 | GUI: Import `fg_bg_73k_err50k.txt` (line 50,000 has `LONGKEY12345 詞`) | Warning message box reports line 50,000. On dismiss, caret jumps to line 50,000, viewport painted. Save still enabled — clicking Save shows the same error and re-jumps. |
| 9 | GUI: type new line `roc 中華民國 fdfdafds` (3 tokens) | Visible-line repaint shows the line marked red (Level 1 format error — extra token). Click Save → message box reports it, caret jumps to it. |
| 10 | GUI: import the 73K Big5 file, then type characters in the editor | Caret moves normally, characters appear immediately, no flicker, no caret jumps. (Regression test for `EM_EXLIMITTEXT`, TOM painting, and EN_CHANGE-mute guard.) |

## 13. Related work

- **Issue #130** — original symptom report and root-cause analysis: [docs/#130_ISSUE.md](./#130_ISSUE.md). Why the validator hung on a 73K-line user file, with the simulation that pinned it on `std::wregex` catastrophic backtracking + byte-as-WCHAR aliasing.
- **[docs/RICHEDIT_VIEWPORT_VAL.md](RICHEDIT_VIEWPORT_VAL.md)** — focused implementation reference for the live-edit viewport validator (TOM-based painting, trigger wiring, RichEdit text-limit fix, code map).
- **Legacy ConfigDialog property pages** in `Config.cpp` (`CommonPropertyPageWndProc`, `DictionaryPropertyPageWndProc`, `CConfig::ValidateCustomTableLines`) still exist but are no longer the user-facing surface and were not modified by the #130 fix. They retain the older regex-based validator.

---

## Appendix A — Historical Phase 1-5 design (legacy ConfigDialog)

The original validation system landed 2026-03-01–02 in the **legacy** `Config.cpp` property pages. It introduced:

- **Phase 1:** `DialogContext` per-page state to fix the wrong-config-file bug (global `_imeMode` shared across pages).
- **Phase 2:** Smart-validation triggers (Space/Enter/Paste/Delete + structural-change detection).
- **Phase 3:** 3-level error hierarchy (format / key length / per-char).
- **Phase 4:** Light/dark theme support — `CConfig::IsSystemDarkTheme` (uxtheme ordinal 132 + registry fallback), per-theme colour constants, `WM_CTLCOLOR*` handlers, `DwmSetWindowAttribute` title bar dark mode.
- **Phase 5:** Unit tests `UT-CV-01–16` and integration tests `IT-CV-01–14`.

That work shaped the validation contract and colour palette that the new SettingsWindow inherits. The SettingsWindow re-implements the surface (RichEdit, validator, theming) in `src/DIMESettings/`. The Phase 1-5 details are preserved in git history.

## Appendix B — Revision history

| Date | Version | Changes |
| --- | --- | --- |
| 2026-03-01 | 1.0–3.3 | Phase 1-3 in legacy ConfigDialog (context isolation, smart triggers, 3-level errors) |
| 2026-03-02 | 4.0–5.0 | Phase 4 (theme) and Phase 5 (tests) in legacy ConfigDialog |
| 2026-04-29 | 6.0 | Major rewrite for SettingsWindow surface and #130 fix: removed `std::wregex`, added Level-0 cap, `LoadTextFileAsUtf16`, `ValidateCustomTableBuffer`, EN_CHANGE-mute on bulk load, CLI parity through `SettingsModel::ValidateLine` + `CreateEngine` |
| 2026-04-29 | 7.0 | Shipped state: viewport-only live validator with TOM-based painting (`ITextDocument` + `ITextRange::SetForeColor` — no `EM_SETSEL`, no caret jumps, no flicker); `ValidateViewport` brackets its painting loop with `EM_SETEVENTMASK(0)` to prevent EN_CHANGE recursion; `EM_EXLIMITTEXT(100 MB)` set on RichEdit creation so the editor accepts input on large imports; Save button uses simple dirty-bit; Import is non-blocking (loads even with errors, shows warning + auto-jumps); Save and Import auto-jump to first error after message-box dismiss; Level 1 tightened to require exactly two non-whitespace tokens. |
