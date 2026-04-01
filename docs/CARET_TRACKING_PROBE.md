# Non-Destructive Probe Composition

**Date:** 2026-04-01  
**Status:** Implemented and verified

---

## 1. Background: why the probe exists

The CHN/ENG mode notification window needs a screen position near the text
caret.  When no composition is active, the TSF text layout sink has no range to
query.  `_ProbeComposition` obtains caret coordinates so the notify appears at
the right place before the user types.

The probe fires from two call sites in `UIPresenter.cpp`:

1. **`SHOW_NOTIFY` timer callback** (line 1088) — when the delayed notify timer
   expires and `_GetContextDocument() == nullptr`
2. **`ShowNotifyText`** (line 1564) — when `delayShow == 0` (immediate show)
   and `_GetContextDocument() == nullptr`

Both are triggered by `showChnEngNotify` → `ShowNotifyText` on focus change or
key events.

---

## 2. The problem: destructive probe broke LINE emoji

The original probe did `StartComposition` → `EndComposition` with no text.
This empty composition cycle corrupted LINE's Chromium-based TSF text store —
subsequent `InsertTextAtSelection` from the Windows emoji panel (Win+.) was
silently dropped.

**Evidence:** With probe enabled, no `OnEndEdit` events after emoji selection.
With probe disabled (`return;` at top of `_ProbeComposition`), emoji worked and
`OnEndEdit` fired for each insertion.

---

## 3. Fix: read-only probe via `GetSelection` + `GetTextExt`

Replaced the destructive `StartComposition`/`EndComposition` with a read-only
edit session that queries the selection range and gets its screen rect.

**File: [Composition.cpp](../src/Composition.cpp)**

### Before (destructive)

```cpp
// RequestEditSession with TF_ES_READWRITE
// → _StartComposition() → StartComposition()
// → _TerminateComposition() → EndComposition()
// → GetTextExt on composition range
```

### After (non-destructive)

```cpp
// RequestEditSession with TF_ES_ASYNCDONTCARE | TF_ES_READWRITE
// → GetSelection(TF_DEFAULT_SELECTION) — gets caret as collapsed range
// → GetTextExt on selection range — no composition created
// → _LayoutChangeNotification if rect is valid
```

**Result:** Emoji works in LINE.  No text store corruption.

---

## 4. Issues from selection-range GetTextExt and their fixes

The non-destructive probe gets rects from `GetTextExt` on a **selection range**
instead of a composition range.  Different apps return different quality rects.
Four issues were encountered and resolved:

### Issue A — Zero-height junk rects on initial focus (LINE)

**Symptom:** LINE returns screen-edge coordinates with zero height on initial
focus (first ~8 probe calls), e.g. `{3839,2075,3840,2075}`.

**Fix:** Guard in `_ProbeCompositionRangeNotification` (line 610):
```cpp
if (SUCCEEDED(hr) && _pUIPresenter && (rcExt.bottom - rcExt.top > 0))
```
Zero-height rects are skipped; notify uses cached `_notifyLocation`.

**Status: FIXED.**

### Issue B — Notify positioning skipped when candidate allocated but not visible

**Symptom:** The notify positioning block in `_LayoutChangeNotification` was
guarded by `else if (!_pCandidateWnd)`.  When the candidate window object
exists but is not shown (probe-only case), both
`_pCandidateWnd->_IsWindowVisible()` (false) and `!_pCandidateWnd` (false)
evaluated false, skipping notify positioning entirely.

**Fix:** Changed guard to (line 1014):
```cpp
else if (!_pCandidateWnd || !_pCandidateWnd->_IsWindowVisible())
```

**Prerequisite:** Issue A's height guard must be active — without it, junk rects
reach this code path and cause the notify to appear at screen edges.

**Status: FIXED.**

### Issue C — Selection range returns 2px-height rect (Chromium)

**Symptom:** `GetTextExt` on a collapsed selection (caret) in Chromium apps
returns the caret geometry (~2px tall), not the text line bounding box.  Only a
composition range triggers Chromium to report the full line extent.

**Fix:** Pad rect height using `_rectCompRange` (saved from the last valid
composition).  In `_ProbeCompositionRangeNotification` (lines 614–619):

```cpp
LONG compHeight = _pUIPresenter->GetCompRangeHeight();
if (compHeight > 0 && rectHeight < compHeight)
    rcExt.bottom = rcExt.top + compHeight;
else if (rectHeight < MulDiv(16, CConfig::GetDpiForHwnd(NULL), 96))
    rcExt.bottom = rcExt.top + MulDiv(16, CConfig::GetDpiForHwnd(NULL), 96);
```

**`_rectCompRange` reset on focus change** — `ResetCompRange()` called from
`OnSetFocus` in `ThreadMgrEventSink.cpp` (line 87), so stale heights from a
previous app are never used.

**First-focus behavior:** Before any typing, `_rectCompRange` height is 0 →
DPI-scaled 16px minimum is used → notify slightly imprecise.  After one
composition + commit, `_rectCompRange` captures the correct line height.

**Status: FIXED.**

### Issue D — Probe rect from wrong UI element (CMD) and Excel cell tracking

**CMD symptom:** `GetTextExt` on a selection range returns screen-edge
coordinates while the actual caret is elsewhere.

**CMD fix:** Check whether the system caret window
(`GetGUIThreadInfo.hwndCaret`) differs from the TSF context window
(`pContextView->GetWnd()`).  In `_ProbeCompositionRangeNotification`:

```cpp
GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
if (GetGUIThreadInfo(0, &gti))
{
    if (gti.hwndCaret != 0 && gti.hwndCaret != hwndParent)
    {
        rectIsReliable = FALSE;  // skip layout update
    }
}
```

When skipped, the notify keeps its position from `ShowNotifyText`'s
`GetGUIThreadInfo`.

**Status: FIXED for CMD.**

**Excel investigation:** Excel's TSF text store is the **formula bar**.
Initial testing showed `GetTextExt` returning the formula bar rect
(`top=189 bottom=209 left=231 right=1409`) regardless of cell selection.
However, the hwndCaret guard does NOT catch Excel — `hwndCaret == hwndParent`
(both are the formula bar HWND `0x2F0B4C`).

**What was tried and failed for Excel:**

1. **Distance-based threshold** — compared `GetTextExt` rect against
   `GetGUIThreadInfo` caret position.  Failed because the formula bar is
   close to cells near the top; threshold passed incorrectly and the
   formula bar's `left` coordinate corrupted the x-position.

2. **Save/restore `_notifyLocation` around probe** — saved `_notifyLocation`
   (seeded from `GetGUIThreadInfo`) before the probe, restored it after.
   Failed because `_notifyLocation` is only seeded once (when `< 0`), so
   the restored value was stale — always the first cell's position,
   preventing tracking when the user moved to other cells.

3. **Save/restore + `_Move`** — same as above but also called `_Move` to
   reposition the window after restore.  Same stale-position problem.

**Actual finding:** Further testing revealed the probe's `GetTextExt`
**does return correct cell rects** for Excel (e.g. `top=345 bottom=367
left=282 right=347` — a cell-sized rect, not the formula bar).  The initial
formula bar rects were from a different DLL version or Excel focus state.

The probe's `_LayoutChangeNotification` correctly positions the notify at
the selected cell.  No special handling is needed — the probe works as-is.

**Status: FIXED.  Verified — notify follows cell selection across rows and
columns.**

---

## 5. SHOW_NOTIFY timer: UIA fallback for WPF/UWP apps

**Symptom:** In WPF apps (PowerShell ISE), the probe returns zero-height
rects and `GetGUIThreadInfo` returns `rcCaret = {0,0,0,0}`.  The notify
appeared at the window's client origin.

**Fix:** In the `SHOW_NOTIFY` timer callback, after the probe, check if
`GetGUIThreadInfo` has a zero caret rect (indicating no Win32 caret).  If
so, try UIAutomation `TextPattern` as a fallback, then apply `_Move`:

```cpp
_pTextService->_ProbeComposition(pContext);
GUITHREADINFO gtiCheck = { sizeof(GUITHREADINFO) };
if (_pNotifyWnd && _notifyLocation.x > UI::DEFAULT_WINDOW_X
    && GetGUIThreadInfo(0, &gtiCheck)
    && gtiCheck.rcCaret == {0,0,0,0})
{
    // Try UIA, then apply position
    RECT rcUIA = {0};
    if (SUCCEEDED(_uiaCaretTracker.GetCaretRect(&rcUIA)) && height > 0)
    {
        _notifyLocation.x = rcUIA.left;
        _notifyLocation.y = rcUIA.bottom;
    }
    _pNotifyWnd->_Move(_notifyLocation.x, _notifyLocation.y);
}
ShowNotify(TRUE, 0, (UINT) wParam);
```

For apps with real carets, the probe already positioned the notify
correctly via `_LayoutChangeNotification` — the UIA/`_Move` block is
skipped entirely.

**Status: FIXED.**

---

## 6. WPF apps (PowerShell ISE): UIA fallback

WPF apps don't create Win32 system carets.  Both `GetTextExt` (zero height)
and `GetGUIThreadInfo` (`rcCaret = {0,0,0,0}`) fail.

**Fix:** UIAutomation `TextPattern` fallback.  UIA
`ExpandToEnclosingUnit(TextUnit_Line)` provides the correct y-position
(which line the caret is on).  The x-position is the text area's left
edge — PSI's UIA provider cannot report caret-level horizontal position.

**Result:** Notify on the correct line, left-aligned to text area.

See [CARET_TRACKING_UIA.md](CARET_TRACKING_UIA.md) for full UIA details.

---

## 7. Known limitation: Firefox new tab center search field

Firefox's new tab page center search field is a visual proxy for the URL bar.
Clicking it transfers focus to the URL bar's TSF context.  `GetTextExt`
correctly returns the URL bar position.  The notify appears at the URL bar,
not the center field.  This is expected Firefox behavior — the TSF context IS
the URL bar.

---

## 8. Files changed

| File | Change |
|------|--------|
| `src/Composition.cpp` | Rewrote `_ProbeCompositionRangeNotification`: read-only `GetSelection` + `GetTextExt` instead of `StartComposition`/`EndComposition`; added height padding (Issue C); added hwndCaret sanity check (Issue D) |
| `src/UIPresenter.cpp` | `_LayoutChangeNotification`: expanded notify guard to `!_pCandidateWnd \|\| !_pCandidateWnd->_IsWindowVisible()` (Issue B); added cached position fallback in `SHOW_NOTIFY` timer handler |
| `src/UIPresenter.h` | Added `GetCompRangeHeight()` getter, `ResetCompRange()` method; changed `_In_` → `_In_opt_` for `pRangeComposition` |
| `src/ThreadMgrEventSink.cpp` | Call `ResetCompRange()` in `OnSetFocus` when document manager changes |

---

## 9. Verification matrix

| App | Test | Result |
|-----|------|--------|
| **LINE** | Win+. → emoji panel → select emoji | **PASS** — emoji inserted |
| **LINE** | Type Chinese, select candidates | **PASS** — normal input works |
| **Notepad** | Notify at caret position | **PASS** |
| **Word** | Notify at caret position | **PASS** |
| **Excel** | Notify at selected cell (not formula bar) | **PASS** — probe returns cell rect |
| **CMD** | Notify at caret position | **PASS** — hwndCaret guard |
| **VS Code** | Notify at caret position | **PASS** |
| **Edge** | Notify at text field | **PASS** |
| **Teams** | Notify at caret position | **PASS** |
| **Firefox** (normal page) | Notify at text field | **PASS** |
| **Firefox** (new tab center) | Notify at URL bar | **EXPECTED** — see §7 |
| **PowerShell ISE** | Notify at correct line, x=left edge | **PASS** — UIA fallback; see §6 |
