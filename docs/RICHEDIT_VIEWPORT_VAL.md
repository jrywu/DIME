# RichEdit viewport validator — Implementation reference

- **Status:** Shipped 2026-04-29 (issue #130 Phase 3).
- **Authoritative design:** [docs/CUSTOM_TABLE_VALIDATION.md](CUSTOM_TABLE_VALIDATION.md). This doc is the focused implementation reference for the live-edit pieces — what runs where, on what triggers, with what cost. Use it as a code map.

## Design summary

Live error highlighting in the custom-table RichEdit is **foreground viewport-only** — `EN_CHANGE` / scroll / Enter-Space-Paste paint just the visible 20-50 lines via the Text Object Model (TOM). Errors outside the visible range are caught at Save time by an in-memory full-document validator that pops a message box and auto-jumps the caret to the first error.

There is **no background scan**. The mechanism never made it past prototype: every approach we tried (per-line `EM_GETTEXTRANGE`, per-line TOM `Range()`) was O(N) per call and therefore O(N²) over the chunked timer ticks for a 73K-line buffer, which starved the message pump and made the editor unresponsive. Save's full in-memory pass is fast (~500 ms for 73K lines) and bounded by an explicit user click, so the missing live coverage of off-screen lines is acceptable.

## Why TOM (`ITextDocument` / `ITextRange`) instead of `EM_SETSEL` + `EM_SETCHARFORMAT`

The selection-driven painting flow had three failure modes that TOM eliminates in one shot:

1. **Caret jumps.** `EM_SETSEL` per painted line moves the caret. Save/restore around the batch via `EM_HIDESELECTION` + `WM_SETREDRAW(FALSE)` doesn't reliably restore — RichEdit-50 ignores `EM_EXSETSEL` while redraw is suppressed, so the caret ends up on the last painted line.
2. **Line-by-line repaint flicker.** Each `EM_SETCHARFORMAT(SCF_SELECTION)` triggers a paint; 30-50 visible lines on every keystroke = visible churn.
3. **EN_CHANGE recursion risk.** Some CHARFORMAT operations cause RichEdit to fire `EN_CHANGE` even though only formatting changed. Without an explicit mute, the EN_CHANGE handler re-enters itself.

`ITextDocument::Range(cpMin, cpMax)` returns an `ITextRange` independent of the user selection. `ITextFont::SetForeColor` sets the colour without touching the selection. No caret movement, no flicker, no need for redraw suppression. (TOM is still O(N) per `Range()` call internally, which is why a full-document scan is unviable — but for the visible 20-50 lines it's O(small constant), perfectly fine.)

## Implementation

### State (none)

The viewport validator is stateless across calls. No fields are added to `WindowData` for it. The only related fields in `WindowData` (`ctLastLineCount`, `ctLastEditedLine`, `ctKeystrokeCount`) come from the legacy smart-throttle and are still used by other paths.

### Helpers

All in [src/DIMESettings/SettingsWindow.cpp](../src/DIMESettings/SettingsWindow.cpp), file-static:

| Function | Purpose |
| --- | --- |
| `GetRichEditTextDocument(hRE) → ITextDocument*` | Calls `SendMessage(EM_GETOLEINTERFACE)` + `QueryInterface(__uuidof(ITextDocument))`. Caller releases. Returns `nullptr` if the control doesn't support TOM (very old RichEdit versions); callers fall back to skipping live highlighting. |
| `PaintRangeColor(pDoc, cpMin, cpMax, color)` | TOM-based foreground-colour painter. `pDoc->Range(...)` → `ITextRange::GetFont` → `ITextFont::SetForeColor`. Selection untouched. |
| `PaintErrorRange(pDoc, lineStart, v, isDark)` | Per-error wrapper around `PaintRangeColor` — picks the appropriate Level palette (`*_ERROR_FORMAT` / `*_ERROR_LENGTH` / `*_ERROR_CHAR`). |
| `GetViewportLineRange(hRE, &firstVis, &lastVis)` | `EM_GETFIRSTVISIBLELINE` + `EM_CHARFROMPOS` at the bottom-right corner → `EM_LINEFROMCHAR` for `lastVis`. Bounded by `EM_GETLINECOUNT`. |
| `ValidateCustomTableRE_OneLine(hRE, pDoc, li, mode, maxCodes, isDark, pEng)` | Read line via `EM_GETTEXTRANGE`, call `SettingsModel::ValidateLine`, paint valid colour over the whole line first, then `PaintErrorRange` for any errors. |
| `ValidateViewport(hRE, wd)` | Acquires `ITextDocument` once, brackets the painting loop with `EM_SETEVENTMASK(0)` + restore (prevents EN_CHANGE recursion if RichEdit fires it on CHARFORMAT-only changes), iterates `[firstVis..lastVis]` calling `ValidateCustomTableRE_OneLine`. |

### Trigger wiring (in `ContentWndProc`)

| Trigger | Handler body |
| --- | --- |
| `EN_CHANGE` on `CTRL_CUSTOM_TABLE_EDITOR` | `ValidateViewport(hRE, wd)` + `EnableSaveButton(TRUE)` (mark dirty) + update `ctLastLineCount` |
| `WM_USER+1` (deferred from RichEdit subclass after Enter/Space/Paste/large-delete) | Same as `EN_CHANGE` |
| `WM_USER+3` (forwarded by RichEdit subclass on `WM_VSCROLL` / `WM_MOUSEWHEEL`) | `ValidateViewport(hRE, wd)` only — no Save state change (scroll doesn't dirty) |
| Initial-load (in `CreateRichEditorControl`) | Once after `SetWindowTextW` + `EM_SETEVENTMASK(ENM_CHANGE)` |
| Import button (in `RowAction::ImportCustomTable`) | Once after the EN_CHANGE-muted bulk `SetWindowTextW` |

### RichEdit text limit

`SetWindowTextW` bypasses the editor's text limit (large bulk loads succeed) but the editor then silently rejects user input because the buffer is over the default cap (~32-64K chars). Immediately after `CreateWindow`:

```cpp
SendMessageW(hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100 * 1024 * 1024));
```

100 MB is far beyond any realistic custom table.

### Bulk-load handling

`SetWindowTextW` fires `EN_CHANGE`. For Import (which loads 70K+ lines at once) the EN_CHANGE-driven viewport pass is unnecessary work, so the call is sandwiched between `EM_GETEVENTMASK(0)` / `EM_SETEVENTMASK(0)` / restore:

```cpp
LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);
SetWindowTextW(hRE, text);
// update ctLastLineCount, etc.
SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
ValidateViewport(hRE, wd);   // single explicit FG paint
```

Initial-load uses the same protection implicitly: `EM_SETEVENTMASK(ENM_CHANGE)` is set *after* the initial `SetWindowTextW`.

## Save-time validation (the off-screen safety net)

For errors outside the visible viewport, the user is informed at Save time:

```text
RowAction::SaveCustomTable
  └─ GetWindowTextW
  └─ ValidateCustomTableBuffer (in-memory full-document, ~500 ms for 73K lines)
       └─ on errors:
            ├─ ThemedMessageBox with first-N line numbers
            └─ on dismiss:
                 ├─ SetFocus(hRE)         ← MUST come first; the subclass's
                 │                          WM_SETFOCUS handler resets selection
                 │                          to (0,0), so positioning the caret
                 │                          before SetFocus would clobber it.
                 ├─ EM_LINEINDEX(firstErrorLine - 1) → cp
                 ├─ EM_SETSEL(cp, cp) + EM_SCROLLCARET
                 └─ ValidateViewport       ← paint the destination line + neighbours
       └─ on clean: persist (.txt UTF-16+BOM + parseCINFile customTableMode=TRUE)
                  + EnableSaveButton(FALSE)
```

Import button uses the same auto-jump pattern when its non-blocking pre-scan finds errors (loads anyway, then warns + jumps).

## Performance

| Scenario | Cost |
| --- | --- |
| `EN_CHANGE` / `WM_USER+1` / `WM_USER+3` | < 1 ms (TOM operations on 20-50 visible lines, character offsets near the visible window) |
| Initial-load `ValidateViewport` after `SetWindowTextW` | < 1 ms |
| Save full validation | ~500 ms in-memory for 73K lines |
| Import pre-scan + bulk load + `ValidateViewport` | ~500 ms scan + ~200 ms `SetWindowTextW` + < 1 ms paint |

Typing remains responsive at any document size because the viewport pass cost doesn't depend on total line count.

## Verification

Reuse the `.claude/txt/` fixtures from issue #130. CLI tests are in [CUSTOM_TABLE_VALIDATION.md §12](CUSTOM_TABLE_VALIDATION.md#12-test-scenarios-regression-matrix); GUI scenarios:

| # | Procedure | Expected |
| --- | --- | --- |
| G1 | Import `issue130_source.txt` (Big5, 73,278 lines) | RichEdit fills, Save enables, editor accepts user input. No spinner, no caret jumps, no flicker. Click Save → write completes, Save disables. |
| G2 | After G1, type characters in the editor | Caret stays put, characters appear immediately, visible-range colours update on each keystroke. |
| G3 | After G1, scroll | Newly-visible lines paint via `WM_USER+3`. |
| G4 | Type `roc 中華民國 fdfdafds` (3 tokens) | Visible-line repaint marks the line red (Level 1 — extra token after value). Click Save → message box reports it, caret auto-jumps to it. |
| G5 | Import `fg_bg_2k.txt` (2,000 valid + 50 `BADBADBADBAD 詞` lines) | Warning message box "匯入完成，但偵測到 50 個格式錯誤行..." On dismiss, caret jumps to line 2001 with orange highlight. |
| G6 | Import `fg_bg_73k_err50k.txt` (line 50,000 has `LONGKEY12345 詞`) | Warning reports line 50,000. On dismiss, caret jumps there. Save remains enabled — re-clicking Save shows the same error and re-jumps. |

## Automated test coverage

`G1-G6` above are **manual** GUI scenarios. To get end-to-end coverage that runs in CI, the validation pieces split into three layers:

### Layer 1 — pure-function unit tests (already covered, can extend)

These exercise the validation primitives in isolation. No window, no message pump, runs in milliseconds. **Existing home:** `src/tests/SettingsControllerTest.cpp` (CppUnitTest framework, project `DIMETests.vcxproj`).

| Existing tests | Files | Notes |
| --- | --- | --- |
| 21 `UT_SM_*` tests on `SettingsModel` (sidebar hit-test, snapshot round-trip, IME-mode parsing, etc.) | `SettingsControllerTest.cpp` | Don't currently exercise `ValidateLine` / `ValidateCustomTableBuffer` / `LoadTextFileAsUtf16`. |
| `CINParserTest.cpp` (`parseCINFile`) | `src/tests/` | Covers persist-side; doesn't cover validator. |
| `DictionaryTest.cpp` | `src/tests/` | Has `parseCINFile customTableMode` test; doesn't cover validator. |

**Gap to fill (suggested new tests in `SettingsControllerTest.cpp`):**

| Test name | What | G-test it pins down |
| --- | --- | --- |
| `UT_VL_01_ValidateLine_TwoTokens_Pass` | `roc 中華民國` → valid | G4 inverse |
| `UT_VL_02_ValidateLine_ThreeTokens_FormatError` | `roc 中華民國 fdfdafds` → `LineError::Format` | G4 |
| `UT_VL_03_ValidateLine_KeyTooLong` | `LONGKEY12345 詞`, `maxCodes=4` → `LineError::KeyTooLong` | G6 |
| `UT_VL_04_ValidateLine_LengthCap` | 2000-char single line → `LineError::Format` (Level 0 cap) | #130 RC regression |
| `UT_VL_05_ValidateLine_NoSeparator` | `fff` → `LineError::Format` | (not in G but documented) |
| `UT_VL_06_ValidateLine_PerCharInvalid_Phonetic` | non-ASCII key in PHONETIC mode → `LineError::InvalidChar` | per-char rule |
| `UT_LT_01_LoadTextFile_Utf16BOM` | UTF-16+BOM input | encoding ladder |
| `UT_LT_02_LoadTextFile_Utf8BOM` | UTF-8+BOM input | encoding ladder |
| `UT_LT_03_LoadTextFile_Utf8NoBOM` | strict UTF-8 sniff path | encoding ladder |
| `UT_LT_04_LoadTextFile_CP_ACP_Big5` | feed bytes that fail UTF-8 sniff → fall through to CP_ACP, validate hanzi survive | G1 (Big5 import) |
| `UT_VB_01_ValidateCustomTableBuffer_FirstError` | `fg_bg_2k.txt` content → returns 50 errors, `firstErrorLine == 2001` | G5 |
| `UT_VB_02_ValidateCustomTableBuffer_DeepError` | `fg_bg_73k_err50k.txt` content → returns 1, `firstErrorLine == 50000` | G6 |

These can be added incrementally without new test infrastructure — the helpers are already callable from the test project.

### Layer 2 — CLI E2E tests (already covered, can extend)

`src/tests/CLIIntegrationTest.cpp` runs the actual `--import-custom` code path in-process and asserts on the resulting `<MODE>-Custom.txt` / `.cin`. CLI tests T1-T5 in [CUSTOM_TABLE_VALIDATION.md §12](CUSTOM_TABLE_VALIDATION.md#12-test-scenarios-regression-matrix) already pass. **Suggested additions:**

| Test name | What | G-test it pins down |
| --- | --- | --- |
| `IT_CLI_BIG5_FullRoundTrip` | Pre-stage `issue130_source.txt` (Big5) → `--import-custom --mode dayi` → assert `DAYI-Custom.txt` UTF-16+BOM, line count 73,278, first 5 hanzi match `猙猙獰獰` etc.; `DAYI-Custom.cin` has `%chardef begin`/`end` wrapper | G1 (encoding correctness) |
| `IT_CLI_FG_BG_2K_AbortsClean` | `--import-custom fg_bg_2k.txt` → exits 3, prints `Line 2001: key too long error`, `DAYI-Custom.txt` not written | strict-mode CLI counterpart of G5 |
| `IT_CLI_FG_BG_73K_Err50K_AbortsClean` | `--import-custom fg_bg_73k_err50k.txt` → exits 3, line 50,000 reported | strict-mode CLI counterpart of G6 |
| `IT_CLI_NoValidate_BypassesError` | Same corrupt file with `--no-validate` → exits 0, content persisted as-is | bypass switch |

### Layer 3 — In-process RichEdit GUI tests (new infrastructure)

The truly RichEdit-specific behaviours (caret stability under TOM painting, EM_EXLIMITTEXT acceptance, EN_CHANGE-mute reentrancy guard, auto-jump after Save error) cannot be observed without a real RichEdit window. They CAN be observed without the full Settings UI — host a bare `RICHEDIT50W` in the test process and drive it directly.

**Proposed new file:** `src/tests/RichEditViewportTest.cpp` (added to `DIMETests.vcxproj`). Skeleton:

```cpp
TEST_CLASS(RichEditViewportTest)
{
    static HWND s_hRE;
    TEST_CLASS_INITIALIZE(Setup) {
        LoadLibraryW(L"Msftedit.dll");
        s_hRE = CreateWindowExW(0, L"RICHEDIT50W", L"",
            WS_POPUP | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 800, 600, GetDesktopWindow(), nullptr, nullptr, nullptr);
        SendMessageW(s_hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100*1024*1024));
    }
    TEST_CLASS_CLEANUP(Teardown) { DestroyWindow(s_hRE); }

    // ... per-test body sets content via SetWindowTextW, exercises
    //     ValidateViewport / PaintRangeColor / EM_LINEINDEX+EM_SETSEL,
    //     then asserts via EM_EXGETSEL, GetWindowText length, etc.
};
```

| Test name | Procedure | Pass criterion | G-test it pins down |
| --- | --- | --- | --- |
| `IT_RE_01_LargeImportAcceptsInput` | `SetWindowTextW` with 1.5 MB content; `EM_REPLACESEL(L"X")` to insert at end | `GetWindowTextLengthW` increased by 1 | G1 (`EM_EXLIMITTEXT` raised) |
| `IT_RE_02_LargeImportNoLimitRaisesInputBlocked` | Same but skip `EM_EXLIMITTEXT`; expect `EM_REPLACESEL` to silently fail | length unchanged | regression test |
| `IT_RE_03_TOMPaintDoesNotMoveSelection` | Set content; `EM_SETSEL(50, 50)`; call `PaintRangeColor(pDoc, 0, 100, RGB(255,0,0))`; check `EM_EXGETSEL` | selection still `(50, 50)` | G2 |
| `IT_RE_04_ValidateViewport_PaintsErrors` | Set 5-line content with one invalid line; call `ValidateViewport`; `EM_GETSEL` to query `cfGet.crTextColor` on each line | only the invalid line has error colour | G4 |
| `IT_RE_05_AutoJumpAfterSaveError` | Set content with error at line 50; simulate Save handler's auto-jump (`EM_LINEINDEX(49)` → `EM_SETSEL` → `EM_SCROLLCARET`); query `EM_GETFIRSTVISIBLELINE` | first visible line = 50 (or near; depends on line height) | G5/G6 auto-jump |
| `IT_RE_06_ENCHANGEMuteSurvivesPaint` | Hook EN_CHANGE notifications via parent window; set content; call `ValidateViewport`; verify zero EN_CHANGE messages reach the parent during the call | no recursion | EN_CHANGE-mute guard |

The static helpers (`PaintRangeColor`, `ValidateViewport`, etc.) are file-static in `SettingsWindow.cpp`. To call them from tests, expose via a test-only header `src/DIMESettings/SettingsWindow_TestHooks.h` that declares them and is included only when `DIME_UNIT_TESTING` is defined (the same gate `CLI.cpp` already uses for its test shims).

### Layer 4 — Full-process automation (out of scope)

Testing the full GUI workflow including modal `ThemedMessageBox` dismissal would require either UI Automation API or a child-window hook into the message box. That's substantial infrastructure for marginal additional coverage — Layers 1-3 already pin down all the validation logic, encoding, RichEdit behaviour, and Save-error auto-jump path. The message-box rendering itself is a separate concern.

### Recommended execution order

1. **Layer 1** unit tests (~2 hours of work, high coverage value) — pins down `ValidateLine` levels, encoding ladder, full-buffer validator. Catches regressions on every PR.
2. **Layer 2** CLI tests (~1 hour) — confirms end-to-end CLI persists are right.
3. **Layer 3** RichEdit-host tests (~half day to set up + per-test 30 min) — needed to lock down TOM painting and `EM_EXLIMITTEXT` semantics.

After layers 1-3 land, G1-G6 in §Verification become regression tests rather than acceptance tests — manual smoke only on releases.

## Code locations (quick map)

| Symbol | Line range (approx) |
| --- | --- |
| `GetRichEditTextDocument`, `PaintRangeColor`, `PaintErrorRange` | SettingsWindow.cpp ~L470-L530 |
| `GetViewportLineRange`, `ValidateCustomTableRE_OneLine`, `ValidateViewport` | SettingsWindow.cpp ~L540-L605 |
| `ValidateCustomTableBuffer` (in-memory full-document) | SettingsWindow.cpp ~L385-L420 |
| `EnableSaveButton` | SettingsWindow.cpp ~L608-L615 |
| Initial-load (`EM_EXLIMITTEXT(100 MB)`, `LoadTextFileAsUtf16`, `SetWindowTextW`, `ValidateViewport`) | SettingsWindow.cpp `CreateRichEditorControl` ~L1840-L1880 |
| Import button (pre-scan + bulk load + `ValidateViewport` + auto-jump warning) | SettingsWindow.cpp `RowAction::ImportCustomTable` ~L3780-L3850 |
| Save handler (full validation + auto-jump + persist) | SettingsWindow.cpp `RowAction::SaveCustomTable` ~L3619-L3690 |
| Export handler (`ValidateCustomTableBuffer` pre-scan + write) | SettingsWindow.cpp `RowAction::ExportCustomTable` |
| `EN_CHANGE` handler | SettingsWindow.cpp `ContentWndProc` ~L3860-L3875 |
| `WM_USER+1` handler | SettingsWindow.cpp `ContentWndProc` ~L3970-L3980 |
| `WM_USER+3` handler | SettingsWindow.cpp `ContentWndProc` ~L3984-L3988 |
| RichEdit subclass | SettingsWindow.cpp `CustomTableRE_SubclassProc` ~L686-L725 |
| `SettingsModel::ValidateLine` | [src/DIMESettings/SettingsController.cpp](../src/DIMESettings/SettingsController.cpp) |
| `SettingsModel::LoadTextFileAsUtf16` | [src/DIMESettings/SettingsController.cpp](../src/DIMESettings/SettingsController.cpp) |
