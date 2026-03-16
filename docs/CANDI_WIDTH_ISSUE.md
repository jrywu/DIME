# Fix: Candidate window text clipping

## Root Cause

After adding the page indicator (commit `4374fe7`), `_SetScrollInfo` now calls `_ResizeWindow()` (line 1078). `_ResizeWindow()` calls `CBaseWindow::_Resize` → `MoveWindow(hWnd, ..., TRUE)`. Per the Windows API, **`MoveWindow` with `bRepaint=TRUE` internally calls `UpdateWindow`**, which sends `WM_PAINT` **synchronously**.

This synchronous `WM_PAINT` fires in the middle of `_SetCandidateText()` — specifically between `SetPageIndexWithScrollInfo()` (line 622) and `_SetCandStringLength(candWidth)` (line 625). So `_DrawList` runs with the **old `_wndWidth`**, recalculates `_cxTitle` from the old test string, finds it matches the current window width → **no resize at line 899** → items are drawn with old-width dimensions, clipping wider new candidates.

Before the commit, `_SetScrollInfo` did NOT call `_ResizeWindow()`, so no synchronous paint fired before `_SetCandStringLength`. The only paint came from `_InvalidateRect()` (line 630), which runs after `_wndWidth` is already correct.

### Call sequence (after the commit)

```
_SetCandidateText():
  AddCandidateToUI()              ← new items added
  SetPageIndexWithScrollInfo()    ← line 622
    └─ _SetScrollInfo()
         └─ _ResizeWindow()       ← NEW in page indicator commit
              └─ MoveWindow(TRUE)
                   └─ UpdateWindow → synchronous WM_PAINT
                        └─ _DrawList with OLD _wndWidth  ← BUG: text clipped
  _SetCandStringLength(candWidth) ← line 625, _wndWidth updated HERE (too late)
  _InvalidateRect()               ← line 630, triggers second paint (correct)
```

## Plan — Restore original order

Remove `_ResizeWindow()` from `_SetScrollInfo` (restoring pre-commit behavior), and expand the existing resize check in `_DrawList` to also cover height changes needed for the page indicator.

### Change 1: Remove `_ResizeWindow()` from `_SetScrollInfo`

**File:** `src/CandidateWindow.cpp`, line 1078

```cpp
// Before:
    if (_pVScrollBarWnd)
    {
        _pVScrollBarWnd->_SetScrollInfo(&si);
        _ResizeWindow();  // resize height for page indicator before first paint
    }

// After:
    if (_pVScrollBarWnd)
    {
        _pVScrollBarWnd->_SetScrollInfo(&si);
    }
```

This eliminates the synchronous `WM_PAINT` that fires before `_SetCandStringLength`.

### Change 2: Expand resize check in `_DrawList` to also cover height

**File:** `src/CandidateWindow.cpp`, line 899

The existing check only detects width mismatches. Add a height check so the page indicator area is allocated on the first paint:

```cpp
// Before (line 899):
if(_cxTitle != prc->right - prc->left - VScrollWidth) _ResizeWindow();

// After:
BOOL isMultiPage = (_pVScrollBarWnd && _pVScrollBarWnd->_IsEnabled());
int expectedBottomPadding = isMultiPage ? _cyRow : _cyRow / 2;
int expectedHeight = _cyRow * candidateListPageCnt + expectedBottomPadding + CANDWND_BORDER_WIDTH * 2;
RECT rcWnd = {0, 0, 0, 0};
GetWindowRect(_GetWnd(), &rcWnd);
if(_cxTitle != prc->right - prc->left - VScrollWidth
    || expectedHeight != rcWnd.bottom - rcWnd.top)
    _ResizeWindow();
```

This way `_DrawList` triggers `_ResizeWindow()` when the window height doesn't match (e.g. switching between single-page and multi-page), just as it already does for width.

## Files to modify

1. `src/CandidateWindow.cpp`:
   - `_SetScrollInfo()` (line 1078): remove `_ResizeWindow()` call
   - `_DrawList()` (line 899): expand resize condition to include height check

## Verification

1. Build DIME (Debug, x64)
2. Install and activate the IME
3. Type phrase input that produces 4+ character candidates (e.g. 詞庫 lookup for "中華民國")
4. Verify the candidate window is wide enough — no text clipping on first display
5. Switch between narrow candidates (single chars) and wide candidates (phrases) — verify window resizes correctly each time
6. Verify page indicator still displays correctly for multi-page results
7. Verify single-page results do NOT show page indicator area (correct height)
