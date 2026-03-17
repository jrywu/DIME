# Fix: Candidate window text clipping + font settings not applied

## Issue 1 — Width clipping with certain fonts

### Symptom

After the page indicator commit (`4374fe7`), candidate text is clipped with 新細明體 (PMingLiU) but not with 微軟正黑體 (JhengHei).

### Root Cause: Corrupted test character in source file

Both `WM_CREATE` and `_DrawList` measure a test string of `_wndWidth` repeated characters to compute `_cxTitle` (the content column width). The test character in the source was **U+FFFD (REPLACEMENT CHARACTER `�`)** — a corrupted byte, not a real CJK character.

U+FFFD renders at different widths depending on the font:

| Font | U+FFFD width | Actual CJK width (e.g. '美') |
| --- | --- | --- |
| PMingLiU/新細明體 | 12px (narrow) | 28px |
| JhengHei/微軟正黑體 | 28px (full-width) | 28px |

Debug log proof (`_wndWidth=8`, PMingLiU font):

```
WM_CREATE: candSize.cx=96, perChar=12              ← U+FFFD = 12px/char
_DrawList item0: text=[美國太空總署] itemSize.cx=168  ← actual = 28px/char
textArea=96                                          ← 96 < 168 → CLIPPED
```

The window is sized for 12px characters but the actual content renders at 28px — text clipped.

This bug was latent before the page indicator commit. The commit exposed it by adding `_ResizeWindow()` to `_SetScrollInfo()`, which triggered premature paints that made the sizing error more visible.

### Fix

**`src/CandidateWindow.cpp`**: Replace the corrupted test character `L"�e"` with `L"國"` in both test string loops (WM_CREATE and `_DrawList`).

Also remove `_ResizeWindow()` from `_SetScrollInfo()` (was added by the page indicator commit; causes premature `WM_PAINT` with stale `_wndWidth`).

No other changes to `_OnPaint`, `_DrawList`, or `_DrawPageIndicator` needed — the page indicator commit's structure is correct once the test character is fixed.

## Issue 2 — Font change not applied immediately

### Symptom

After changing the font in settings (Ctrl+\) and closing the dialog, the new font is not applied until the user switches to another IME and back.

### Root Cause: `WriteConfig` stomps `_initTimeStamp`

`WriteConfig()` updates `_initTimeStamp` after writing the INI file:

```cpp
fclose(fp);
_wstat(_pwszINIFileName, &initTimeStamp);        // reads post-write mtime
_initTimeStamp.st_mtime = initTimeStamp.st_mtime; // stomps the cache
```

When `_LoadConfig()` runs after the dialog closes, `LoadConfig` compares the file's mtime against `_initTimeStamp` — but they already match (WriteConfig set them equal). So `LoadConfig` sees `updated = 0`, skips the reload, and `SetDefaultTextFont()` is never called. The old font handle persists.

### Fix

1. **`src/Config.cpp`** — `WriteConfig()`: Remove the `_wstat`/`_initTimeStamp` update (lines 442-443). Only `LoadConfig` should update the timestamp cache.

2. **`src/ShowConfig.cpp`** — `CDIME::Show()`: Add `_LoadConfig()` call after the PropertySheet dialog closes, so config (including font) is reloaded immediately.

## Files modified

1. **`src/CandidateWindow.cpp`**:
   - Test character: `L"�e"` → `L"國"` (2 occurrences)
   - `_SetScrollInfo()`: `_ResizeWindow()` removed
2. **`src/Config.cpp`**: `WriteConfig()` — removed `_initTimeStamp` update
3. **`src/ShowConfig.cpp`**: Added `_LoadConfig()` + `ClearNotify()` after dialog close
4. **`src/tests/UIPresenterIntegrationTest.cpp`**: Fixed 4 corrupted U+FFFD test strings
5. **`src/tests/CandidateWindowIntegrationTest.cpp`**: Added regression test

## Regression test

**`IT03_06_CandidateWindow_TestCharWidth_MatchesRealCJK`** in `CandidateWindowIntegrationTest.cpp`:

Creates a candidate window, selects `Global::defaultlFontHandle` into a DC, then compares `GetTextExtentPoint32` for the test character '國' (used in `_DrawList`) against a real CJK candidate character '美'. Asserts that per-character widths are equal — catches the exact bug where a corrupted test character (U+FFFD) measured 12px while real CJK characters measured 28px.

**`UT_01_06_WriteConfig_DoesNot_StompTimestamp`** in `ConfigTest.cpp`:

Calls `WriteConfig()` twice with different font settings, then `LoadConfig()`. Asserts that `LoadConfig` returns TRUE (detected the second write). Before the fix, `WriteConfig` updated `_initTimeStamp` to match the file's mtime, making `LoadConfig` see `updated = 0` and skip the reload.
