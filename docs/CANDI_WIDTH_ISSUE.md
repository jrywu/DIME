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

## Why removing `_ResizeWindow()` from `_SetScrollInfo` alone is insufficient

Removing the synchronous paint fixes one trigger, but a deeper issue remains in `_DrawList`:

1. `_DrawList` recalculates `_cxTitle` at line 817 (correct for new `_wndWidth`)
2. Draws all items using `prc` — which reflects the **old** window size → **clipped**
3. Detects mismatch at line 899 → `_ResizeWindow()` → `MoveWindow(TRUE)` → recursive repaint draws correctly
4. **But then** `_DrawPageIndicator` (new in the page indicator commit) runs on the **outer DC** (still clipped to old size) — its `FillRect` overwrites part of the recursive paint's correct content

Before the page indicator commit, step 4 didn't exist — `_DrawList` was the last drawing call in `_OnPaint`, so the recursive repaint's correct output was preserved. Now `_DrawPageIndicator` on the stale outer DC corrupts it.

## Plan — Minimal changes to `_DrawList`

### Change 1: Remove `_ResizeWindow()` from `_SetScrollInfo` — DONE

### Change 2: Move resize check from after drawing to before drawing in `_DrawList`

**File:** `src/CandidateWindow.cpp`, `_DrawList()`

Same conditional check that was at line 899, moved to after `_cxTitle`/`_cyRow` recalculation (after line 818), with height check added for page indicator. If mismatch → resize and `return` before any drawing.

After line 818 (`_cyRow = cyLine;`), insert:

```cpp
	// Resize before drawing if dimensions changed — avoids drawing with stale prc
	RECT rcWnd = {0, 0, 0, 0};
	GetWindowRect(_GetWnd(), &rcWnd);
	BOOL isMultiPage = (_pVScrollBarWnd && _pVScrollBarWnd->_IsEnabled());
	int expectedBottomPadding = isMultiPage ? _cyRow : _cyRow / 2;
	int expectedHeight = _cyRow * candidateListPageCnt + expectedBottomPadding + CANDWND_BORDER_WIDTH * 2;
	if(_cxTitle != prc->right - prc->left - VScrollWidth
		|| expectedHeight != rcWnd.bottom - rcWnd.top)
	{
		_ResizeWindow();
		return;  // MoveWindow(TRUE) triggers fresh WM_PAINT with correct dimensions
	}
```

Remove the post-draw resize check (current lines 899-906).

This is the same mismatch check, just positioned before the drawing loop. When dimensions match (the common case), no resize — drawing proceeds normally. When they don't match (first paint after switching candidate width), it resizes and returns without drawing anything wrong.

## Files to modify

1. `src/CandidateWindow.cpp`:
   - `_SetScrollInfo()`: remove `_ResizeWindow()` — DONE
   - `_DrawList()`: move resize+height check from post-draw to pre-draw with early return

## Verification

1. Build DIME (Debug, x64)
2. Install and activate the IME
3. Type phrase input that produces 4+ character candidates (e.g. 詞庫 lookup for "中華民國")
4. Verify the candidate window is wide enough — no text clipping on first display
5. Switch between narrow candidates (single chars) and wide candidates (phrases) — verify window resizes correctly each time
6. Verify page indicator still displays correctly for multi-page results
7. Verify single-page results do NOT show page indicator area (correct height)
