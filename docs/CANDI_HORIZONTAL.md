# Horizontal Candidate Window — Implementation Plan (v3)

## Goal

Add an optional horizontal rendering mode for the candidate window.
In horizontal mode, candidate items are laid out left-to-right with **2-row items** (selkey on top, candidate text below), mirroring the vertical layout symmetrically.
An **HScrollBar at the bottom** replaces the VScrollBar on the right for multi-page navigation.
The **page indicator** ("1/7") is drawn inside the scrollbar's middle track area.

---

## Visual Comparison

### Current (vertical)

```
┌──────────────────┬───┐
│ 1 候選一         │ ▲ │
│ 2 候選二         │   │
│ 3 候選三         │   │
│ 4 候選四         │   │
│ 5 候選五         │ ▼ │
│          1/3     │   │
└──────────────────┴───┘

Width  = _cxTitle + VScrollWidth + CANDWND_BORDER_WIDTH * 2
Height = N * cyRow + bottomPadding + CANDWND_BORDER_WIDTH * 2
Scrollbar: right side, full client height (top=0, h=clientH)
```

### New (horizontal, 2-row items)

```
┌─────────────────────────────────┐
│  1       2       3       4     │
│  候選一  候選二  候選三  候選四 │
│ [◄]         1/7           [►]  │
└─────────────────────────────────┘

Width  = _cxTitle + CANDWND_BORDER_WIDTH * 2
Height = contentH + VScrollWidth + CANDWND_BORDER_WIDTH * 2
Scrollbar: bottom, starts at content bottom, fills to client bottom
Page indicator: inside scrollbar track (between ◄ ►)
```

### Symmetry principle

| Aspect | Vertical | Horizontal (mirrored) |
| ------ | -------- | --------------------- |
| Item layout | Selkey LEFT of text, stack top-to-bottom | Selkey ABOVE text, flow left-to-right |
| Content dimension | Height = N × cyRow | Height = itemRows × 2 × cyRow |
| Scrollbar axis | Right side, full height | Bottom, full width |
| Scrollbar added to | Width | Height |
| Page indicator | Below candidates (separate row) | Inside scrollbar track |
| Left margin | `PageCountPosition * cxLine` before selkey | Same `PageCountPosition * cxLine` |

### Key difference from vertical (NOT symmetric)

In vertical mode, the scrollbar is positioned at a fixed offset from the client edge and has a fixed size (`VScrollWidth`). This works because the scrollbar spans the **full** client height — any mismatch from `WS_BORDER` / `CANDWND_BORDER_WIDTH` is absorbed.

In horizontal mode, the scrollbar does NOT span the full client height. It occupies only a bottom strip. Therefore:

- **Scrollbar top** must be anchored to **content bottom** (`_cyRow * _currentPageRows`), NOT computed from `clientH - VScrollWidth`
- **Scrollbar height** must be `clientH - top` (fill to client bottom), NOT fixed at `VScrollWidth`

This way, any mismatch between `CANDWND_BORDER_WIDTH * 2` and the actual `WS_BORDER` system border is absorbed by the scrollbar height (slightly taller or shorter — imperceptible), and there is NEVER a gap between content and scrollbar or below scrollbar.

---

## Bugs encountered and all attempted fixes

### Bug A: Last row clipped (v1 only)

**Cause**: v1 used `fistLineOffset = cyLine / 4` to shift items down, but `_ResizeWindow` didn't include this offset in the window height. Last item bottom exceeded the allocated area.

**Resolution**: v2 uses 2-row items without `fistLineOffset`. Each item occupies exactly `2 × cyRow`. No offset, no clipping.

### Bug B: Gray area below scrollbar

**Previous wrong theory**: `WS_BORDER` / `CANDWND_BORDER_WIDTH` mismatch causes a gap. This was disproven: in vertical mode, the LEFT side of the window (where candidates are drawn, no scrollbar) has NO gray area at the bottom. If the mismatch theory were correct, the left side would also show a gap. It doesn't.

**TRUE root cause**: `_OnPaint` (line 673) fills `pPaintStruct->rcPaint` with `_brshBkColor`. Then `_DrawList` runs and may call `_ResizeWindow` when `_currentPageRows` grows (via the guard). `_ResizeWindow` calls `MoveWindow` which makes the window **taller**. The newly exposed bottom area is **NOT in the original `pPaintStruct->rcPaint`** — so it gets the default system window background (gray), not `_brshBkColor`.

**Why vertical mode doesn't have this problem**: In vertical mode, `_ResizeWindow` during `_DrawList` only changes WIDTH (when `_cxTitle` grows). Height is fixed at `candidateListPageCnt * _cyRow + bottomPadding + border*2` — it NEVER changes during paint. Any new width area is on the right, behind the scrollbar. In horizontal mode, `_currentPageRows` grows during paint → height changes → new bottom area is visible and unpainted (gray).

**Attempts that failed**:

| # | Attempt | Why it failed |
| - | ------- | ------------- |
| 1 | `HScrollHeight = _cyRow` | Wrong metric — scrollbar height tied to font size instead of system scrollbar metric |
| 2 | `HScrollHeight = SM_CYHSCROLL * 3/2` | Doesn't match `_sizeOfScrollBtn.cy` used in `_AdjustWindowPos`, so override creates mismatch |
| 3 | `top = rcCandRect.bottom - HScrollHeight` with `CANDWND_BORDER_WIDTH*2` in wndHeight | Gray area is from unpainted resize, not border mismatch |
| 4 | `top = _cyRow * rows + bottomPadding` (fixed offset from content) | Same — doesn't address the repaint issue |
| 5 | `height = rcCandRect.bottom - top` (fill to bottom) | Same — the gap is from unpainted area, not scrollbar positioning |
| 6 | `AdjustWindowRectEx` to compute window size from desired client size | Made window even bigger, more unpainted area |
| 7 | `_AdjustWindowPos` with parent client coords instead of screen coords | Different formula causes override mismatch |
| 8 | `_AdjustWindowPos` returns early for horizontal | Correct isolation but doesn't fix the repaint issue |
| 9 | Remove `CANDWND_BORDER_WIDTH*2` from wndHeight entirely | Doesn't address unpainted area from resize during paint |
| 10 | `extraHeight = isMultiPage ? VScrollWidth : _cyRow/2` (no bottomPadding) | Same |
| 11 | Always call `_ResizeWindow()` in `_DrawList` | User forbids — resize should be a guard only |

**Correct fix**: After `_ResizeWindow` is called from the guard in `_DrawList`, call `_InvalidateRect()` to schedule a full repaint. The second paint fills the newly exposed area correctly because dimensions are now stable (guard won't trigger resize again — no infinite loop).

```cpp
int top = _cyRow * _currentPageRows;     // anchored to content bottom
int height = rcCandRect.bottom - top;    // fills to client bottom — no gap possible
```

### Bug C: Wrong scrollbar height

**Cause**: `_AdjustWindowPos` in `CScrollBarWindow` is called from `WM_WINDOWPOSCHANGED` and `WM_WINDOWPOSCHANGING` (lines 516/552 of `CandidateWindow.cpp`) every time the candidate window moves or resizes. It uses `_sizeOfScrollBtn.cy` for the scrollbar height, which differs from whatever `_ResizeWindow` computed. This override shrinks the scrollbar.

**Resolution**: `_AdjustWindowPos` returns early for horizontal mode (already implemented). The scrollbar position is managed exclusively by `_ResizeWindow`. For `WS_CHILD` windows, the child moves with the parent automatically — `_AdjustWindowPos` is unnecessary.

### Bug D: Left margin too tight

**Cause**: Vertical mode uses `PageCountPosition * cxLine` (= 1 char width) as left margin before the selkey number (line 1067). Horizontal `_BuildHorizontalLayout` starts items at `col * columnWidth` with zero left margin.

**Fix**: Add `PageCountPosition * cxLine` offset to all item rects in `_BuildHorizontalLayout`, and include it in `totalWidth`.

### Bug E: `_ResizeWindow` not called when height changes

**Cause**: The resize guard at the end of `_DrawList` horizontal branch only checked `_cxTitle` (width) changes. When `_currentPageRows` (height) changed but `_cxTitle` didn't, `_ResizeWindow` was never called — scrollbar stayed at stale position from `_Create`.

**Fix**: Guard checks BOTH `_cxTitle` and `_currentPageRows` changes:
```cpp
if (_cxTitle != prevCxTitle || _currentPageRows != prevPageRows) _ResizeWindow();
```

### Bug F: Columns too wide / wrong items per row

**Cause**: `_BuildHorizontalLayout` scanned ALL candidates (thousands) for the widest text. A 4-character phrase made every column 4-chars wide even when current page had single-char items. Also `itemsPerRow = (_maxCharsPerLine * cxLine) / columnWidth` used inflated columnWidth, resulting in only 2 items per row.

**Fix**:
1. Scan only CURRENT PAGE items for max text width (not all candidates). `_cxTitle` "only grows" in caller for stability.
2. Treat `_maxCharsPerLine` as max items per row (not character count). Default 20 = all 10 items fit on one row.

### Bug G: Window size flickers when flipping pages

**Cause**: `_cxTitle` and `_currentPageRows` were recalculated per page, causing the window to resize on every page flip.

**Fix**: Both values only grow, never shrink. Reset in `_ClearList` when a new candidate list is built. This matches vertical mode where `_cxTitle` only grows.

---

## Current implementation status

### DONE (working correctly)

- ScrollBarWindow orientation support (`_IsHorizontal`, button positioning, `_AdjustWindowPos` returns early)
- 2-row item layout in `_BuildHorizontalLayout` (uniform columns, current-page max width)
- 2-row item drawing in `_DrawList` (selkey top row, text bottom row)
- Page indicator drawn inside scrollbar track in `_DrawPageIndicator`
- Hit-testing for 2-row items in `_OnLButtonDown`
- Arrow key swap (Left/Right for candidate selection, Up/Down for pages)
- Config settings (`CandidateHorizontal`, `CandidateMaxCharsPerLine`)
- DIMESettings GUI controls in 外觀設定 groupbox
- CLI keys for both settings
- `_cxTitle` / `_currentPageRows` only grow, reset on `_ClearList`
- Resize guard checks both width and height changes

### TODO

#### Bug A fix: (resolved by v2 2-row items)

#### Bug B + H + I fix: FIXED width and height — no resize during paint

**Key insight**: In vertical mode, height NEVER changes during paint. It's always `candidateListPageCnt * _cyRow + bottomPadding + border*2`. Only width changes (when `_cxTitle` grows for extra-wide items). This means `_ResizeWindow` during `_DrawList` only grows the window horizontally — new area is behind the scrollbar (invisible). No gray area possible.

For horizontal, WIDTH should never change during paint (symmetric with vertical's fixed height).

**Bug I (latest): Column width uses `candSizeRef.cx` = `_wndWidth` × CJK chars**

The current code does:
```cpp
columnWidth = candSizeRef.cx + cxLine / 2;
```
Where `candSizeRef.cx` is the pixel width of `_wndWidth` (e.g., 4) Chinese characters — the ENTIRE vertical content width. With 10 items per row: `totalWidth = margin + 10 × (4 chars + gap) = 46 character-widths`. This makes the window absurdly wide.

In vertical mode, `_cxTitle = candSize.cx + StringPosition * cxLine` is the width of ONE row (ONE item: margin + selkey + text). That's ~6 character-widths for `_wndWidth=4`. Reasonable.

For horizontal, each COLUMN shows ONE item. Column width should be per-ITEM, not per-ROW:
- Each column needs: text width (typically 1-3 CJK chars) + gap
- Most CJK IME candidates are 1-2 characters
- Use `2 * cxLine + cxLine / 2` (2 CJK chars + half-char gap) as fixed column width

**Corrected implementation**:

```cpp
// Column width: 2 CJK character widths + half-char gap (fits most candidates)
// Items wider than 2 chars overflow visually — same as vertical clips to _wndWidth
int columnWidth = 2 * cxLine + cxLine / 2;

// Items per row — constant
int itemsPerRow = itemCount;  // = candidateListPageCnt
if (_maxCharsPerLine > 0)
    itemsPerRow = max(1, min((int)_maxCharsPerLine, itemCount));

// Fixed dimensions from page capacity
int totalItemRows = (itemCount - 1) / itemsPerRow + 1;
totalRows = totalItemRows * 2;

int leftMargin = 1 * cxLine;
totalWidth = leftMargin + itemsPerRow * columnWidth;
```

With `_maxCharsPerLine=20` and 10 items: `totalWidth = cxLine + 10 * 2.5 * cxLine = 26 * cxLine ≈ 400px`. Reasonable.

**Scrollbar positioning**: `top = _cyRow * _currentPageRows`, `height = rcCandRect.bottom - top`.

**Guard**: dimensions are constant after first paint → `_ResizeWindow` fires once only → no gray area.

#### Bug C fix: (resolved — `_AdjustWindowPos` returns early)
#### Bug D fix: (resolved — left margin added)
#### Bug E fix: (resolved — guard checks both dimensions)
#### Bug F fix: (resolved — `_maxCharsPerLine` as max items per row)
#### Bug G fix: (resolved — only grow, reset on `_ClearList`)

---

## Files to modify

| File | Change |
| ---- | ------ |
| `src/CandidateWindow.cpp` | `_BuildHorizontalLayout`: fix column width to `2 * cxLine + cxLine/2` (per-item, not per-row). Remove `candSizeRef` measurement (not needed). |

---

## Config, GUI, CLI (DONE)

- `Config.h`/`Config.cpp`: `_candidateHorizontal` (default FALSE), `_candidateMaxCharsPerLine` (default 20)
- `UIPresenter.cpp`: passes config to `_Create`
- `DIME.rc`/`resource.h`/`ConfigDialog.cpp`: combo + edit in 外觀設定 groupbox
- `CLI.cpp`: `CandidateHorizontal` and `CandidateMaxCharsPerLine` keys

---

## Arrow Key Behavior (DONE)

| Key | Vertical mode | Horizontal mode |
| --- | ------------- | --------------- |
| ↑ Up | Move to previous candidate | Page up (when `ArrowKeySWPages`) |
| ↓ Down | Move to next candidate | Page down (when `ArrowKeySWPages`) |
| ← Left | Page up (when `ArrowKeySWPages`) | Move to previous candidate |
| → Right | Page down (when `ArrowKeySWPages`) | Move to next candidate |

---

## Edge Cases

| Case | Handling |
| ---- | -------- |
| `maxCharsPerLine = 0` | All items on one row; no wrapping |
| `maxCharsPerLine = 20` (default) | Up to 20 items per row; with page size 10, all fit on one row |
| Only 1 page | Scrollbar hidden; VScrollWidth area acts as bottom padding |
| Page flip | `_cxTitle` and `_currentPageRows` only grow; reset on `_ClearList` |
| Vertical mode | All paths guarded by `if (_isHorizontal)`; unchanged |

---

## Verification

1. Build x64 Debug
2. Switch to horizontal mode in settings
3. Type a character producing multi-page candidates
4. Verify: no gray gap below scrollbar, scrollbar fills from content bottom to window bottom
5. Verify: left margin matches vertical mode's left margin
6. Verify: page indicator "1/7" appears inside scrollbar track
7. Flip pages — window size stable (no flicker)
8. Test single-page candidates — scrollbar hidden, bottom padding present
9. Test vertical mode — unchanged behavior
