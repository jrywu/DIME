# Candidate Window Scrollbar Redesign

**Status:** Implemented (Phases 1-5 complete, flicker mitigation in progress)
**Date:** 2026-03-25

---

## 1. Previous State (before this work)

The candidate window used a custom-drawn scrollbar (`CScrollBarWindow`) with only two arrow buttons (up/down). No thumb track, no mouse wheel, no direct-jump.

---

## 2. What Was Implemented

### Phase 1 — Thumb rendering

**Files:** `ScrollBarWindow.h`, `ScrollBarWindow.cpp`

- Added `SCROLLBAR_HITTEST` enum: `SB_HIT_NONE`, `SB_HIT_BTN_UP`, `SB_HIT_BTN_DOWN`, `SB_HIT_TRACK_ABOVE`, `SB_HIT_TRACK_BELOW`, `SB_HIT_THUMB`
- Added `_GetTrackRect()` — returns area between up/down buttons
- Added `_GetThumbRect()` — proportional thumb sizing:
  - `thumbH = max(thumbMinH, MulDiv(trackH, nPage, nMax))`
  - `thumbY = MulDiv(trackH - thumbH, nPos, nMax - nPage)`
  - Minimum height from `GetSystemMetricsDpi(SM_CYVTHUMB, dpi)` with floor of 8px
- `_OnPaint()` draws thumb with `FillRect(COLOR_BTNFACE)` + `DrawEdge(EDGE_RAISED)`

### Phase 2 — Track click

**Files:** `ScrollBarWindow.cpp`

- Added `_HitTest(POINT)` method — tests buttons, thumb, track-above, track-below
- `_OnLButtonDown()` rewritten to use `_HitTest`:
  - `SB_HIT_TRACK_ABOVE` → `_ShiftPage(-1, TRUE)`
  - `SB_HIT_TRACK_BELOW` → `_ShiftPage(+1, TRUE)`

### Phase 3 — Thumb dragging

**Files:** `ScrollBarWindow.h`, `ScrollBarWindow.cpp`

- Added drag state: `_isDragging`, `_dragStartY`, `_dragStartPos`
- `_OnLButtonDown(SB_HIT_THUMB)` → starts drag, stores start position
- `_OnMouseMove()` while dragging → `MulDiv(deltaY, posRange, slideRange)` maps pixels to position, calls `_SetCurPos(newPos, SB_THUMBTRACK)`
- `_OnLButtonUp()` while dragging → sends final `SB_THUMBPOSITION`

### Phase 4 — Mouse wheel

**Files:** `CandidateWindow.cpp`

- `WM_MOUSEWHEEL` handler: scrolls by ±1 item per wheel notch
- Guards: only invalidates if `_currentSelection` actually changed (prevents boundary flicker)
- Updates scrollbar via `_ShiftPosition(_currentSelection, FALSE)`

### Phase 5 — SB_THUMBTRACK in _OnVScroll

**Files:** `CandidateWindow.cpp`

- Added `SB_THUMBTRACK` case (identical to `SB_THUMBPOSITION`) for live preview during thumb drag
- `_SetCurPos` sends `WM_VSCROLL` for both `SB_THUMBPOSITION` and `SB_THUMBTRACK`

---

## 3. Flicker Issue — Analysis and Fixes Applied

### Root causes identified

When the user scrolls quickly with a trackpad, each `WM_MOUSEWHEEL` event triggers this repaint chain:

```
WM_MOUSEWHEEL
  → _SetSelectionOffset()
  → _pVScrollBarWnd->_ShiftPosition()
    → _SetCurPos()
      → _Enable()                      ← (1) may call EnableWindow on buttons
        → _pBtnUp->_Enable()           ← (2) triggers system repaint on button HWND
        → _pBtnDn->_Enable()           ← (3) triggers system repaint on button HWND
      → _InvalidateRect()              ← (4) invalidates scrollbar
  → _InvalidateRect()                  ← (5) invalidates candidate window
```

Each of (1)-(5) can cause a separate `WM_PAINT` cycle. Combined with `WM_ERASEBKGND` (system erases to white before each paint), this creates visible white flashes.

### Fixes applied (cumulative)

| Fix | File | What | Why |
|-----|------|------|-----|
| 1 | `CandidateWindow.cpp` | Skip invalidation when selection unchanged at boundary | Eliminates all repaints when wheel events arrive at list start/end |
| 2 | `CandidateWindow.cpp` | Use `_ShiftPosition(pos, FALSE)` instead of `_SetScrollInfo` | `_SetScrollInfo` triggers full recalc + `_Enable` + `_InvalidateRect`; `_ShiftPosition(FALSE)` just updates position |
| 3 | `ScrollBarWindow.cpp` | Guard button `_Enable()` with state-change check | `_pBtnUp->_Enable()` only called when `_IsEnabled() != newState`, preventing `EnableWindow()` system repaint |
| 4 | `ScrollBarWindow.cpp` | `WM_ERASEBKGND → return 1` | Suppresses system background erase on scrollbar |
| 5 | `CandidateWindow.cpp` | `WM_ERASEBKGND → return 1` | Suppresses system background erase on candidate window |
| 6 | `BaseWindow.cpp` | `InvalidateRect(hwnd, NULL, FALSE)` | Changed erase parameter from `TRUE` to `FALSE` in `_InvalidateRect()`. This is the most impactful fix — affects ALL windows (candidate, scrollbar, notify, shadow). With `FALSE`, the system never sends `WM_ERASEBKGND` even to child windows. Safe because all DIME windows fully paint their background in `WM_PAINT`. |

### Flicker chain after fixes

```
WM_MOUSEWHEEL (at boundary)
  → _SetSelectionOffset() — no change
  → (skipped — selection unchanged)                    ← Fix 1
  → return 0

WM_MOUSEWHEEL (mid-list)
  → _SetSelectionOffset() — selection changes
  → _ShiftPosition(pos, FALSE)                         ← Fix 2: no WM_VSCROLL sent
    → _SetCurPos(pos, -1)                              ← dwSB=-1: no WM_VSCROLL
      → _Enable()
        → _pBtnUp->_Enable() — skipped (same state)   ← Fix 3
        → _pBtnDn->_Enable() — skipped (same state)   ← Fix 3
      → _InvalidateRect()                              ← Fix 6: erase=FALSE
  → _InvalidateRect()                                  ← Fix 6: erase=FALSE

  WM_PAINT (scrollbar) — no WM_ERASEBKGND              ← Fix 4+6
  WM_PAINT (candidate) — no WM_ERASEBKGND              ← Fix 5+6
```

**Result:** 2 repaints per wheel event (scrollbar + candidate), zero erase cycles, zero button EnableWindow calls mid-page.

### Deep analysis: scrollbar area flash during rapid scroll

After fixes 1-6, the candidate list itself no longer flickers (double-buffered via `CreateCompatibleDC` + `BitBlt`). However, the **scrollbar area** still flashes occasionally. Root cause analysis:

**The scrollbar is a separate child HWND** (`WS_CHILD`) overlapping the right edge of the candidate window. The paint sequence during a scroll step is:

```
1. Candidate WM_PAINT fires
   → BitBlt double-buffered content to screen
   → This includes the scrollbar region (candidate bg color painted under the scrollbar)   ← FLASH
2. Scrollbar WM_PAINT fires (separate HWND)
   → Paints buttons + track + thumb over the same region
```

Between steps 1 and 2, the user briefly sees the candidate background in the scrollbar area — this is the flash. The two windows paint independently and asynchronously.

**Additional source:** `_DrawList()` at line 962 calls `_ResizeWindow()` mid-paint when `_cxTitle` changes, which triggers `SetWindowPos` on both the candidate and scrollbar windows, causing additional `WM_PAINT` cycles.

### Fixes applied (additional)

| Fix | File | What | Why |
|-----|------|------|-----|
| 7 | `CandidateWindow.cpp` | Double-buffer candidate `WM_PAINT` | `FillRect` + `_DrawList` composed off-screen, then single `BitBlt` to screen. Eliminates visible blank-then-text transition. |
| 8 | `CandidateWindow.cpp` | `WS_CLIPCHILDREN` added to window style | Windows automatically excludes child HWND areas (scrollbar) from the parent's paint region. The candidate BitBlt no longer paints under the scrollbar, eliminating the flash between parent and child paint. |
| 9 | `ScrollBarWindow.cpp` | Buttons always enabled — no `EnableWindow()` at boundaries | Eliminates the last system-triggered repaint. Buttons never gray out; up/down at list boundary wraps circularly instead. |
| 10 | `CandidateWindow.cpp` | Circular navigation via scrollbar buttons | `SB_LINEDOWN` at last item → first item; `SB_LINEUP` at first item → last item. Same for `SB_PAGEDOWN`/`SB_PAGEUP`. Mouse wheel still stops at boundaries (standard UX). |

### Flicker chain after all fixes

```
WM_MOUSEWHEEL (mid-list)
  → _SetSelectionOffset() — selection changes
  → _ShiftPosition(pos, FALSE)
    → _SetCurPos(pos, -1)
      → _Enable() — buttons always enabled, no EnableWindow() (Fix 9)
      → _InvalidateRect() on scrollbar (erase=FALSE, Fix 6)
  → _InvalidateRect() on candidate (erase=FALSE, Fix 6)

  WM_PAINT (candidate):
    → CreateCompatibleDC + BitBlt (Fix 7)
    → Scrollbar area clipped out by WS_CLIPCHILDREN (Fix 8)
    → Single blit — no intermediate state visible

  WM_PAINT (scrollbar):
    → Track bg + thumb + buttons — no erase (Fix 4+6)
    → Separate HWND — no overlap with candidate paint (Fix 8)

Button click at boundary (e.g. down arrow on last item):
  → SB_LINEDOWN → _SetSelectionOffset(+1) returns FALSE
  → Wrap: _SetSelection(0, FALSE) — jump to first item (Fix 10)
  → No EnableWindow() call — button stays enabled (Fix 9)
```

**Result:** Zero `EnableWindow()` calls during any scrolling. The candidate and scrollbar paint independently with no overlapping screen regions. Zero erase cycles. Double-buffered candidate content.

### Navigation behavior summary

Scrollbar/mouse inputs are **positional** (stop at boundaries). Keyboard inputs are **sequential** (wrap circularly).

| Input | Mid-list | At boundary | Circular? |
|-------|----------|-------------|-----------|
| **Keyboard arrow keys** | Move by ±1 item | Wraps | Yes |
| **Keyboard Page Up/Down** | Move by page | Wraps | Yes |
| **Mouse wheel** | Scrolls by ±1 item | Stops | No |
| **Up/Down button click** | Pages up/down | Stops | No |
| **Up/Down button hold** | Auto-repeat page | Stops | No |
| **Thumb drag** | Tracks position | Clamped | No |
| **Track click** | Pages up/down | Stops | No |

---

## 4. Scrollbar Sizing Analysis and Redesign

### Current sizing formula

All dimensions derive from `SM_CXVSCROLL * 3/2` (1.5x the system scrollbar width). This single value controls:

| Element | Formula | 96 DPI | 120 DPI | 144 DPI |
|---------|---------|--------|---------|---------|
| Scrollbar width | `SM_CXVSCROLL * 3/2` | 25px | 31px | 37px |
| Button height | `SM_CYVSCROLL * 3/2` | 25px | 31px | 37px |
| Button width | Same as scrollbar width | 25px | 31px | 37px |
| Candidate row height | `fontSize * dpi/72 * 5/4` | ~20px | ~25px | ~30px |

**Problem:** The button height (25-37px) exceeds the candidate row height (20-30px) at most DPIs. Two buttons consume 50-74px of vertical space, which is 2.5-3.7 candidate rows worth. The 1.5x multiplier was intended for touch targets but is visually oversized for an IME candidate window.

### Visual balance issues

```
Current (1.5x):                 Proposed (1.0x):
┌────────────────┬─────────┐    ┌────────────────┬──────┐
│ 1 令           │  ▲ 25px │    │ 1 令           │▲17px│
│ 2 證           │         │    │ 2 證           │     │
│ 3 治           │ track   │    │ 3 治           │track│
│ 4 月           │ ┌─────┐ │    │ 4 月           │┌──┐ │
│ 5 快           │ │thumb│ │    │ 5 快           ││  │ │
│ 6 哲           │ └─────┘ │    │ 6 哲           │└──┘ │
│ 7 道           │         │    │ 7 道           │     │
│ 8 春           │ track   │    │ 8 春           │track│
│ 9 珠           │         │    │ 9 珠           │     │
│ 0 志           │  ▼ 25px │    │ 0 志           │▼17px│
│       4/8      │ ← 25px→ │    │       4/8      │←17→ │
└────────────────┴─────────┘    └────────────────┴──────┘
     Scrollbar too wide              Better proportion
```

### Proposed change

Remove the `* 3/2` multiplier — use `SM_CXVSCROLL` directly (1.0x). This is the standard system scrollbar size that Windows users expect. At 96 DPI this is 17px, which is:
- Narrower than a candidate row (20px) — visually balanced
- Still a comfortable click/touch target (same size as every other scrollbar on the system)
- Already DPI-scaled by the system via `GetSystemMetricsForDpi`

**Files to change:**
- `ScrollBarWindow.cpp` constructor: `_sizeOfScrollBtn.cx/cy` — remove `* 3/2`
- `CandidateWindow.cpp` `_GetWidth()` and `_ResizeWindow()`: remove `* 3/2` from VScrollWidth
- `ScrollBarWindow.cpp` `_AdjustWindowPos()`: remove `* 3/2`

---

## 5. Testing Plan

### Unit Tests (new `ScrollBarTest.cpp` or in `DpiScalingTest.cpp`)

| Test | Description |
|------|-------------|
| ThumbHeight_SinglePage_EqualsTrack | When nMax == nPage, thumb fills entire track |
| ThumbHeight_TwoPages_HalfTrack | When nMax == 2*nPage, thumb is ~50% of track |
| ThumbHeight_ManyPages_MinHeight | When nMax >> nPage, thumb clamps to min height |
| ThumbPosition_AtStart_IsTop | When nPos == 0, thumb is at track top |
| ThumbPosition_AtEnd_IsBottom | When nPos == nMax-nPage, thumb is at track bottom |
| ThumbPosition_Middle_CentersThumb | When nPos == (nMax-nPage)/2, thumb is centered |
| DragDelta_MapsToPosition | Verify deltaY → deltaPos formula |
| HitTest_AboveThumb_ReturnsTrackAbove | Point above thumb rect returns correct zone |
| HitTest_OnThumb_ReturnsThumb | Point inside thumb rect returns HIT_THUMB |
| HitTest_BelowThumb_ReturnsTrackBelow | Point below thumb rect returns correct zone |

### Integration Tests (in `CandidateWindowIntegrationTest.cpp`)

| Test | Description |
|------|-------------|
| ScrollBar_ThumbVisible_WhenMultiPage | Thumb rendered when candidates > page size |
| ScrollBar_ThumbHidden_WhenSinglePage | No thumb when all candidates fit in one page |
| ScrollBar_WheelDown_AdvancesSelection | WM_MOUSEWHEEL with negative delta moves forward |
| ScrollBar_WheelUp_RetreatSelection | WM_MOUSEWHEEL with positive delta moves backward |
| ScrollBar_WheelAtBoundary_NoChange | WM_MOUSEWHEEL at list end doesn't change selection |
| ScrollBar_TrackClick_PagesDown | Click below thumb triggers page-down |

---

## 5. Phase 2 — Windows 11 Style Thin Scrollbar (planned)

### Motivation

The current scrollbar uses classic `DrawFrameControl` 3D buttons and `COLOR_BTNFACE` thumb — visually Win95-era. Windows 11 introduced thin overlay scrollbars that auto-hide when idle. There is no Win32 API to enable this look — it must be custom-painted.

### System setting

Windows 11 exposes "Always show scrollbars" in Settings > Accessibility > Visual effects, stored as:

```
HKEY_CURRENT_USER\Control Panel\Accessibility
  DynamicScrollbars (DWORD)
    1 = auto-hide (default)
    0 = always show
```

When the key is missing (Win7/8/10), default to auto-hide (always show = FALSE).

Reading pattern (same as existing `AppsUseLightTheme` in `Config.cpp:281`):

```cpp
static BOOL GetAlwaysShowScrollbars() {
    DWORD dynamicSB = 1; // default: auto-hide
    DWORD cb = sizeof(dynamicSB);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Control Panel\\Accessibility",
        L"DynamicScrollbars",
        RRF_RT_REG_DWORD, nullptr, &dynamicSB, &cb);
    return (dynamicSB == 0); // 0 = always show; 1 or missing = auto-hide
}
```

### Visual design

| Element | Current (classic) | New (Win11-style) |
|---------|-------------------|-------------------|
| Track | `COLOR_SCROLLBAR` fill (gray) | Transparent (parent shows through) |
| Thumb | `COLOR_BTNFACE` + `DrawEdge(EDGE_RAISED)` | Rounded-rect pill, semi-transparent gray |
| Buttons | `DrawFrameControl(DFC_SCROLL)` 17px square | Removed — no arrow buttons |
| Width | `SM_CXVSCROLL` (17px at 96 DPI) | 6px idle, 10px on hover (DPI-scaled) |
| Visibility | Always visible | Auto-hide after 1.5s inactivity |

### Thumb appearance

- Rounded rectangle pill shape
- Light mode: `RGB(128,128,128)` at ~60% opacity
- Dark mode: `RGB(180,180,180)` at ~50% opacity
- On hover: expand to 10px width, increase opacity to ~80%
- Proportional height (same `MulDiv` formula as current)

### Auto-hide behavior

**When `DynamicScrollbars = 1` or missing (auto-hide):**
- Scrollbar appears on scroll action or mouse enter near scrollbar edge
- Fades out after ~1.5s of inactivity
- Uses `WS_EX_LAYERED` + `SetLayeredWindowAttributes` for alpha fade
- Reappears instantly on next scroll/hover

**When `DynamicScrollbars = 0` (always show):**
- Scrollbar always visible in expanded (10px) state
- No fade animation

### Hover expansion

- Idle: 6px wide (DPI-scaled), only thumb visible
- Mouse enters scrollbar area: expand to 10px, show track highlight
- Mouse leaves: shrink back to 6px after short delay
- Track `WM_MOUSEMOVE` / `WM_MOUSELEAVE` for hover detection

### Implementation steps

1. **Read system setting** — Add `CConfig::GetAlwaysShowScrollbars()` via `RegGetValueW`
2. **Remove arrow buttons** — Delete `CScrollButtonWindow` creation/painting; track click handles paging
3. **Thin rendering** — Replace `DrawFrameControl`/`DrawEdge` with `FillRoundRect` pill thumb
4. **Auto-hide timer** — `_lastActivityTick` + fade timer, `SetLayeredWindowAttributes` for alpha
5. **Hover expansion** — Resize scrollbar width on `WM_MOUSEMOVE`/`WM_MOUSELEAVE`
6. **CandidateWindow integration** — Update width calculations for thin scrollbar
7. **`WM_SETTINGCHANGE`** — Re-read registry when user toggles accessibility setting

### Files to modify

| File | Changes |
|------|---------|
| `Config.h` / `Config.cpp` | Add `GetAlwaysShowScrollbars()` static method |
| `ScrollBarWindow.h` | Remove button members, add auto-hide/hover/alpha state |
| `ScrollBarWindow.cpp` | Thin pill rendering, auto-hide timer, hover expansion, remove buttons |
| `CandidateWindow.cpp` | Thin scrollbar width, show/hide triggers, `WM_SETTINGCHANGE` |

### Compatibility

| Windows Version | Behavior |
|----------------|----------|
| Windows 7 | No registry key → default auto-hide; `WS_EX_LAYERED` on child not supported → fallback to always show |
| Windows 8.1+ | Auto-hide with alpha fade |
| Windows 10+ | Full thin scrollbar with auto-hide |
| Windows 11 | Matches system "Always show scrollbars" setting |

---

## 6. Compatibility (current implementation)

All Phase 1 changes use standard Win32 APIs available since Windows 2000+. No new DLLs or runtime loading required. DPI scaling preserved via existing `GetDpiForHwnd` / `GetSystemMetricsDpi` infrastructure.

---

## 7. Files Modified (Phase 1 — complete)

| File | Changes |
|------|---------|
| `ScrollBarWindow.h` | `SCROLLBAR_HITTEST` enum, `_GetTrackRect`, `_GetThumbRect`, `_HitTest`, drag state members |
| `ScrollBarWindow.cpp` | Thumb drawing, hit testing, drag logic, track click, `WM_ERASEBKGND`, button enable guard, double-buffer, always-enabled buttons, 1.0x width |
| `CandidateWindow.cpp` | `WM_MOUSEWHEEL`, `WM_ERASEBKGND`, `WS_CLIPCHILDREN`, `SB_THUMBTRACK`, double-buffer, boundary guard, circular navigation, 1.0x scrollbar width |
| `BaseWindow.cpp` | `InvalidateRect` erase parameter changed from `TRUE` to `FALSE` |

---

## 8. Visual Design RC Log (RC11–RC25)

### Bug fixes and implementation history

| # | Issue | Status | File | Fix |
|---|-------|--------|------|-----|
| RC11 | Shadow alpha=0 at corner cutout pixels | Done | `ShadowWindow.cpp` | Replaced `PtInRegion`+`dx/dy` with SDF formula |
| RC12 | Animation `SetLayeredWindowAttributes(alpha=0)` in `_Move` adds `WS_EX_LAYERED` | Done | `CandidateWindow.cpp` | Removed `SetLayeredWindowAttributes` from `_Move` and `_OnTimerID` |
| — | Scrollbar overlapping rounded corners | Done | `CandidateWindow.cpp` | Scrollbar shortened vertically by arc radius top and bottom |
| RC13 | `CS_DROPSHADOW` causes right/bottom dark edges (not suppressed by `SetWindowRgn`) | Done | `BaseWindow.cpp` | Removed `CS_DROPSHADOW` from `_InitPopupWindowClass` |
| RC14 | Notify window `_DrawBorder` uses `Rectangle()` — corners cut out by `SetWindowRgn` | Done | `NotifyWindow.cpp` | Changed `Rectangle()` → `RoundRect()`; skipped on Win11 (DWM provides border) |
| RC15 | Notify `_Show(TRUE)` never re-applies `SetWindowRgn` — Win11 discards region on hide, so corners become rectangular on every re-show | Done | `NotifyWindow.cpp` | Re-apply `SetWindowRgn` after `CBaseWindow::_Show(TRUE)`, same as candidate; removed dead `#define NO_ANIMATION` / `#ifndef NO_ANIMATION` guard code |
| RC16 | Notify and candidate shadow bitmaps overlapped when notify is shown to the left of candidate (reverse-lookup mode) | Done | `UIPresenter.cpp` | Added `SHADOW_SPREAD / 2` (7px) gap at both placement sites — `_notifyLocation.x` calculation and `_pNotifyWnd->_Move()` call |
| RC17 | Scrollbar arrow triangles too narrow and sharp — `aw = width/3` ≈ 4px for 10px button; Win11 Explorer uses ~60% of button width | Done | `ScrollBarWindow.cpp` | Changed triangle geometry: `aw = max(3px, btnW × 60%)`, `ah = max(2px, aw × 87%)`; centered on full button rect, removed inset-adjusted variables |
| RC18 | Scrollbar arrow triangles too flat (isosceles); gap between triangle and thumb too small; triangle wider than scrollbar causing clipping/misalignment | Done | `ScrollBarWindow.cpp`, `ScrollBarWindow.h` | `ah = aw × 87/100` equilateral; `aw = (btnW - 2×inset) / 2` aligns triangle with thumb horizontally; `SCROLLBAR_BTN_HEIGHT` 10 → 22px for larger gap |
| RC19 | Pill thumb horizontally misaligned with triangle apex; `RoundRect` two-pixel top artifact | Done | `ScrollBarWindow.cpp`, `ScrollBarWindow.h` | Thumb uses `pw = aw-1`; `left = acx-pw+1`, `right = acx+pw+1`, `radius = 2*pw`; GDI half-integer rule gives single topmost pixel at `acx`; `SCROLLBAR_BTN_HEIGHT` 22 → 14px; removed `SCROLLBAR_THUMB_INSET` constant |
| RC19 | Page indicator removed | Done | `CandidateWindow.cpp` | Commented out `_DrawPageIndicator` call; bottom padding is always `cyLine/2` |
| RC20 | Win10: `CandidateWindow::_DrawBorder` used `Rectangle()` — corners cut by `SetWindowRgn` | Done | `CandidateWindow.cpp` | Replaced `Rectangle()` with `RoundRect()` using DPI-scaled radius matching `SetWindowRgn` |
| RC21 | Win10: custom 1px border unnecessary — `CShadowWindow` provides separation on all versions (Win7+) | Done | `CandidateWindow.cpp`, `NotifyWindow.cpp` | Removed `_DrawBorder` call entirely from both `WM_PAINT` handlers |
| RC22 | Legacy DPI-unaware apps: candidate window invisible (0×0 size) | Done | `CandidateWindow.cpp`, `CandidateWindow.h` | `_Show(TRUE)` checks `GetClientRect`; calls `_ResizeWindow()` if 0×0 and `_pIndexRange` set; `_ResizeWindow()` moved to public |
| RC23 | Legacy DPI-unaware apps: candidate at wrong position (DPI coordinate mismatch) | Done | `BaseWindow.cpp` | Skip `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` override if parent window is `DPI_AWARENESS_CONTEXT_UNAWARE`; APIs loaded dynamically for Win7/8 safety |
| RC24 | Legacy DPI-unaware apps: scrollbar `WS_EX_LAYERED` on child rejected (error 183) | Done | `ScrollBarWindow.cpp` | Retry `CreateWindowEx` without `WS_EX_LAYERED` on failure; scrollbar works fully opaque |
| RC25 | Scrollbar creation failure deleted shadow window | Done | `CandidateWindow.cpp` | Removed `_DeleteShadowWnd()` from scrollbar failure path; shadow and scrollbar creation now independent |
| RC26 | Scrollbar magenta background on non-layered (legacy) windows | Done | `ScrollBarWindow.cpp`, `ScrollBarWindow.h`, `CandidateWindow.cpp` | Non-layered bg via `WM_CTLCOLORSCROLLBAR`; selection highlight `RoundRect` drawn on top of all scrollbar elements via `_SetSelectionHighlight` |

### Bug fix details

#### RC20–RC21 — Windows 10 border bugs (2026-03-30)

Both the candidate and notify windows showed an ugly border on Windows 10 while looking correct on Windows 11.

**RC20:** `CCandidateWindow::_DrawBorder` called `Rectangle()` to draw the 1px outline. Because `SetWindowRgn` clips the window to a rounded shape, the straight edges of `Rectangle()` are clipped at the corners, leaving visible notches. `CNotifyWindow::_DrawBorder` already used `RoundRect()` correctly — the candidate window was an oversight.

Fix: replaced `Rectangle()` with `RoundRect()` using `MulDiv(_cornerRadiusBase, dpi, USER_DEFAULT_SCREEN_DPI)`:

```cpp
// was:
Rectangle(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);

// fixed:
UINT dpi = CConfig::GetDpiForHwnd(wndHandle);
int radius = MulDiv(_cornerRadiusBase, dpi, USER_DEFAULT_SCREEN_DPI);
RoundRect(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom, radius, radius);
```

**RC21:** After fixing RC20, the border remained visually jarring regardless of color. `CShadowWindow` is a top-level `WS_EX_LAYERED` popup supported from Windows 7 onwards — its per-pixel alpha SDF gradient provides clear visual separation on every supported version. No Windows version needs a custom 1px border.

Fix: removed `_DrawBorder` call entirely from both WM_PAINT handlers. The functions remain in the source but are no longer invoked.

#### RC22–RC25 — Legacy (DPI-unaware) app compatibility (2026-03-30)

Old Win32 apps (e.g. Windows XP-era Notepad) are DPI-unaware. Three bugs caused the candidate window to malfunction in these apps.

**RC22 — Candidate window invisible (0×0 size):**
The new design removed `WS_BORDER` (replaced with `WS_POPUP | WS_CLIPCHILDREN`). Without `WS_BORDER`, a window with zero client area has truly zero physical size and receives no `WM_PAINT`. The only call to `_ResizeWindow()` was inside `_DrawList()` during `WM_PAINT`, so it never fired.

Fix: `CCandidateWindow::_Show(TRUE)` checks `GetClientRect`. If 0×0 and `_pIndexRange` is set, calls `_ResizeWindow()` before showing. `_ResizeWindow()` moved from private to public.

**RC23 — Candidate window at wrong screen position:**
`CBaseWindow::_Create` temporarily switched the thread to `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` during `CreateWindowEx`. This placed the IME window in physical-pixel coordinate space, while caret positions from DPI-unaware apps are in virtualized (scaled) coordinates. At 175% scaling, the candidate appeared at roughly `(x/1.75, y/1.75)`.

Fix: check parent window's DPI awareness via `GetWindowDpiAwarenessContext` + `AreDpiAwarenessContextsEqual` (loaded dynamically). If parent is `DPI_AWARENESS_CONTEXT_UNAWARE`, skip the DPI override. APIs loaded via `GetProcAddress` for Win7/8 safety.

**RC24 — Scrollbar creation failed with `WS_EX_LAYERED` on child:**
Some legacy apps reject `CreateWindowEx` with `WS_EX_LAYERED` on child windows (error 183 / `ERROR_ALREADY_EXISTS`).

Fix: retry without `WS_EX_LAYERED` on failure. Scrollbar works fully without it — always opaque (no fade/auto-hide), same as Win7 fallback.

**RC25 — Scrollbar failure deleted the shadow:**
`_CreateVScrollWindow` called `_DeleteShadowWnd()` on failure, destroying the already-created shadow window.

Fix: removed `_DeleteShadowWnd()` from the scrollbar failure path. Shadow and scrollbar creation are now independent.

#### RC26 — Scrollbar magenta background on non-layered windows (2026-03-30)

On legacy Win32 apps where `WS_EX_LAYERED` fails on child windows (RC24), the scrollbar falls back to a non-layered window. Two problems resulted:

**Magenta background:** `_OnPaint` unconditionally filled the background with `SCROLLBAR_COLORKEY` (`RGB(255, 0, 255)`). On layered windows this becomes transparent via `LWA_COLORKEY`; on non-layered windows it paints as solid magenta.

Fix: check `WS_EX_LAYERED` in `_OnPaint`. Layered windows continue using the magenta color key. Non-layered windows send `WM_CTLCOLORSCROLLBAR` to the parent candidate window, which returns `_brshBkColor` (tracks current theme). The parent-owned brush is not deleted by the scrollbar.

**Selection highlight hidden behind scrollbar:** The candidate's `WS_CLIPCHILDREN` prevents it from painting in the scrollbar's area. On layered windows the color key transparency reveals the parent content; on non-layered windows the scrollbar's opaque background covers the selection highlight.

Fix: added `_SetSelectionHighlight(RECT, cornerRadius, color)` to `CScrollBarWindow`. The candidate calls this from `_DrawList` with the full `RoundRect` coordinates (in scrollbar client space — left extends past 0). The scrollbar draws the same `RoundRect` as the **last step** in `_OnPaint`, on top of all scrollbar elements (background, arrows, thumb), so the highlight visually extends over the scrollbar with its original shape.

### Files modified (visual design work)

| File | Changes |
|------|---------|
| `BaseWindow.h` / `.cpp` | `_InitPopupWindowClass` (no `CS_IME`, drop-shadow flag removed); `SetWindowRgn` for rounded corners; `_cornerRadiusBase` member |
| `Globals.cpp` | Candidate + Notify windows registered via `_InitPopupWindowClass` |
| `CandidateWindow.cpp` | `WS_EX_NOACTIVATE` (no `WS_EX_LAYERED`/`WS_EX_TOOLWINDOW`); `NO_WINDOW_SHADOW`; rounded selection highlight; page indicator removed |
| `NotifyWindow.cpp` | Same extended style changes; `NO_WINDOW_SHADOW` |
| `ShadowWindow.h` | `_cornerRadiusBase = 10` for shadow SDF corner masking |
