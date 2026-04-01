# Candidate Window Technical Reference

**Date:** 2026-03-29

---

## 1. Visual Design

### 1.1 Rounded corners and glow shadow

**Goal:** candidate window with rounded corners (5px arc radius, matching VS Code's popup style) and a VS Code-style all-around soft shadow, Win7–Win11.

---

#### 1.1.1 Rules that govern the design

**Rule 1 — `SetWindowRgn` and `WS_EX_LAYERED` cannot coexist on the same window**

`SetWindowRgn` clips the window to a shape. Win11 DWM silently ignores `SetWindowRgn` on any window that has `WS_EX_LAYERED`. The candidate window must never carry `WS_EX_LAYERED` while `SetWindowRgn` is in use.

**Rule 2 — `SetLayeredWindowAttributes` silently adds `WS_EX_LAYERED` on Win8+**

Calling `SetLayeredWindowAttributes` on a window that lacks `WS_EX_LAYERED` silently adds it on Windows 8 and later — even when alpha = 0. The old fade-in animation in `_Move` called `SetLayeredWindowAttributes(255*(5/100))` = alpha 0 on every move, adding `WS_EX_LAYERED` to the candidate each time. This was the root cause of all failed attempts (5a–5e).

**Rule 3 — `CS_DROPSHADOW` is NOT suppressed by `SetWindowRgn`**

`CS_DROPSHADOW` in the window class causes DWM to render a shadow outside the window region, always on the bottom-right side only — regardless of any `SetWindowRgn` applied to the window. It cannot be suppressed by the region. Since `CShadowWindow` provides all-around shadow, `CS_DROPSHADOW` must be **removed from the popup window class** to avoid asymmetric dark edges on the right and bottom.

---

#### 1.1.2 Design decisions

| Concern | Decision | Reason |
|---------|----------|--------|
| Rounded corners | `SetWindowRgn` on all popup windows | Only reliable method on Win7–Win11; DWM corner API is no-op for IME |
| Shadow | `CShadowWindow` — separate `WS_EX_LAYERED` popup, per-pixel alpha SDF | All-around even gradient; avoids Rule 1 conflict on candidate |
| `CS_DROPSHADOW` | **Removed** from popup window class | Not suppressed by `SetWindowRgn`; causes right/bottom-only dark edges (Rule 3) |
| Scrollbar corners | Shorten scrollbar vertically by arc radius at top and bottom | Scrollbar never overlaps corner areas; no clipping needed |
| Fade-in animation | **Removed** | Calls `SetLayeredWindowAttributes` → adds `WS_EX_LAYERED` → breaks `SetWindowRgn` (Rule 2) |
| Notify window border | `RoundRect()` matching region radius | `Rectangle()` is clipped by `SetWindowRgn` at corners, leaving cut-out gaps |

---

#### 1.1.3 Drawing procedure

##### Rounded corners

The candidate is `WS_POPUP | WS_CLIPCHILDREN` with `WS_EX_TOPMOST | WS_EX_NOACTIVATE` and **no** `WS_EX_LAYERED`.

1. `radius = MulDiv(_cornerRadiusBase, dpi, 96)` — DPI-scaled. `_cornerRadiusBase = 10` → ellipse diameter 10px → **5px arc radius** at 96 DPI. `CreateRoundRectRgn` takes ellipse width/height, so the visible arc radius is always `_cornerRadiusBase / 2`.
2. `SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, w+1, h+1, radius, radius), TRUE)` — clips rendering and hit-testing to the rounded shape; pixels outside are transparent.
   - Called in `CBaseWindow::_Resize` on every resize.
   - Re-applied in `CCandidateWindow::_Show(TRUE)` after `CBaseWindow::_Show(TRUE)` — Win11 DWM discards `SetWindowRgn` on hidden windows; re-applying on the same message-pump turn (before the next DWM frame) ensures it takes effect.
3. Scrollbar: `y = BORDER + arcRadius`, `height = parentH - BORDER*2 - arcRadius*2`. Never overlaps corner areas. No `SetWindowRgn` needed on the scrollbar child.
4. Notify window border: `RoundRect(dc, 0, 0, w, h, radius, radius)` with matching radius — draws a border that follows the rounded region instead of cutting off at corners.

##### Glow shadow

`CShadowWindow` is a `WS_EX_LAYERED` popup sitting immediately below the candidate in z-order, extending `SHADOW_SPREAD` (14px) beyond the candidate on all four sides.

```
+--------[shadow window]--------+
|  14px  +--[candidate]--+  14px|
|        |               |      |
|        +---------------+      |
+--------------------------------+
```

**Per-pixel alpha — `_InitShadow`:**

1. Allocate a 32-bit BGRA DIB the size of the shadow window; zero all pixels.
2. For each pixel, compute its signed distance from the candidate's rounded-rect boundary (SDF):

```text
r  = arcRadius / 2     (arc radius of candidate corners)
qx = |ox - half_w| - (half_w - r)       // ox, oy relative to candidate top-left
qy = |oy - half_h| - (half_h - r)
sdf = sqrt(max(qx,0)^2 + max(qy,0)^2) + min(max(qx,qy), 0) - r
```

- `sdf <= 0`: inside candidate rounded rect → alpha = 0
- `sdf > 0`: outside → `f = 1 - min(sdf / SHADOW_SPREAD, 1.0)`, `alpha = SHADOW_MAX_ALPHA × f²`

3. `UpdateLayeredWindow` uploads the DIB. Must be called while the shadow window is **visible** — DWM discards `UpdateLayeredWindow` on hidden windows.

**Show order (must not be reordered):**

```
CShadowWindow::_Show(TRUE):
  1. _AdjustWindowPos()       — position and size shadow around owner
  2. CBaseWindow::_Show(TRUE) — shadow enters DWM composition tree
  3. _InitShadow()            — UpdateLayeredWindow on now-visible window
```

On owner resize: steps 1 + 3 (bitmap must match new size).
On owner move only: step 1 only (bitmap is position-independent).

---

### 1.2 Rounded selection highlight

The selected candidate row uses a rounded rectangle highlight instead of a sharp full-width background stripe.

Drawing sequence for the selected row:

1. Fill the entire row with normal background color (`_crBkColor`)
2. Draw a `RoundRect` with symmetric 3px horizontal / 1px vertical inset, filled with `_crSelectedBkColor`, 4px corner radius (all DPI-scaled)
3. Draw text over the highlight with `SetBkMode(TRANSPARENT)`

The highlight is **symmetrically centered** — the same 3px `hInset` is applied on both left and right sides. When the scrollbar is hidden (single-page), the highlight extends into the scrollbar area for visual balance.

Non-selected rows use the standard `ExtTextOut(ETO_OPAQUE)` for maximum performance.

### 1.3 Page indicator (removed)

The text page indicator ("4/8") has been removed. The scrollbar's thin idle line already encodes page position visually — its vertical position and height convey both current page and total page count without consuming bottom padding. Bottom padding is `cyLine / 2` in all cases.

### 1.4 Scrollbar visibility

The scrollbar is only shown when the candidate list has multiple pages. Single-page candidate lists show no scrollbar — `WM_SHOWWINDOW` checks `_IsEnabled()` before showing, and `_OnScrollActivity()` returns early if not enabled. This prevents an empty scrollbar track from appearing on hover when there is nothing to scroll.

When multi-page, the scrollbar appearance follows the Windows 11 "Always show scrollbars" accessibility setting (`DynamicScrollbars` registry key), matching Explorer's behavior exactly:

| Setting | Mouse position | Scrollbar state |
|---------|---------------|-----------------|
| Always show ON | any | Full (triangles + pill), always visible |
| Always show OFF | outside scrollbar column | Thin line (2px) only |
| Always show OFF | inside scrollbar column | Full (triangles + pill) |
| Always show OFF | mouse leaves scrollbar column | Full → thin line after 2s delay |
| Always show OFF | mouse wheel / trackpad scroll | Thin line (no expansion) |

### 1.5 Border behavior

`_DrawBorder` is **never called** on any supported Windows version. `CShadowWindow` is a top-level `WS_EX_LAYERED` popup, which works on Windows 7 and later, and its per-pixel alpha SDF gradient provides clear visual separation on all versions. No additional 1px border is needed or drawn.

### 1.5.1 Windows 10 border bugs (fixed 2026-03-30)

`_DrawBorder` used `Rectangle()` which was clipped by `SetWindowRgn` at corners; replaced with `RoundRect()`, then removed entirely since `CShadowWindow` provides sufficient visual separation on all versions. See [CANDI_SCROLL_BAR.md](CANDI_SCROLL_BAR.md) RC20–RC21 for details.

---

### 1.6 Visual design summary

| Element | Description |
|---------|-------------|
| Window corners | Rounded 5px arc radius via `SetWindowRgn(CreateRoundRectRgn(..., 10, 10))` on all versions (Win7–Win11) |
| Shadow | `CShadowWindow` per-pixel alpha SDF gradient; `CS_DROPSHADOW` removed from window class |
| Border | None — `CShadowWindow` provides visual separation on all versions (Win7+); `_DrawBorder` is never called |
| Selection highlight | `RoundRect` with 4px radius, symmetric scrollbar-width margin on both sides |
| Page indicator | Removed — scroll position shown by thin idle scrollbar line |
| Non-selected rows | Standard `ExtTextOut(ETO_OPAQUE)` stripes |
| Single-page scrollbar | Hidden — no empty track shown |
| Window class | Popup class: no `CS_IME`; drop-shadow flag removed (handled by `CShadowWindow`) |
| Extended style | `WS_EX_TOPMOST \| WS_EX_NOACTIVATE` (no `WS_EX_LAYERED`, no `WS_EX_TOOLWINDOW`) |

### 1.7 Notify / candidate window spacing

In reverse-lookup mode the notify window appears adjacent to the candidate window. Both windows carry a `CShadowWindow` that extends `SHADOW_SPREAD` (14px) in all directions. Without explicit spacing the two shadow bitmaps overlap, causing a visible dark band between the windows.

**Rule:** the gap between the outer edges of the two content windows is `SHADOW_SPREAD / 2` (7px). The two shadow bitmaps overlap slightly in the middle — the combined alpha blends softly rather than doubling visibly.

```text
        SHADOW_SPREAD          SHADOW_SPREAD
        <-14px->               <-14px->
+-------[notify shadow]--+----++----+--[candidate shadow]----+
|  14px +--[notify]--+   | 7px||7px |  +--[candidate]+  14px |
|       |            |   |    ||    |  |             |        |
|       +------------+   |    ||    |  +-------------+        |
+------------------------+----++----+------------------------+
                              ^
                         7px gap - shadows overlap softly
```

**Placement logic (`UIPresenter.cpp`):**

| Condition                                               | Notify x position                                                                    |
|---------------------------------------------------------|--------------------------------------------------------------------------------------|
| `candPt.x < notifyWidth` (not enough room to the left)  | `candPt.x + candidateWidth + SHADOW_SPREAD / 2` — notify to the right of candidate  |
| Otherwise                                               | `candPt.x - notifyWidth - SHADOW_SPREAD / 2` — notify to the left of candidate      |

This calculation is applied at **two sites**:

1. **Initial placement** (~line 984): sets `_notifyLocation.x` when notify first appears alongside candidate.
2. **Re-placement** (~line 1542): calls `_pNotifyWnd->_Move()` when the candidate window is already visible and notify needs to reposition (e.g. on candidate list update).

Both sites use identical logic so notify never drifts relative to the candidate across updates.

---

## 2. Window Layout

The candidate window is a popup window (`WS_POPUP | WS_CLIPCHILDREN`) with three visual layers stacked by z-order. On Windows 11, all popup windows have rounded corners via `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUNDSMALL)` applied in `CBaseWindow::_Create`.

| Layer | Window Class | Style | Purpose |
|-------|-------------|-------|---------|
| Shadow | `CShadowWindow` | `WS_EX_LAYERED`, popup | Drop shadow behind candidate window |
| Candidate | `CCandidateWindow` | `WS_EX_LAYERED | WS_EX_TOPMOST`, popup | Candidate list |
| Scrollbar | `CScrollBarWindow` | `WS_EX_LAYERED | WS_EX_TOPMOST`, child of candidate | Thin scrollbar overlay on right edge |

### Overall layout diagram

```
+--border(1px)---------------------------------------------+
|                                                    |  ^   |
|  +-margin-+-num-+----candidate text----+  +------+ | btn  |
|  | (gap)  | 1   | 令                  |  |  /\  | | 14px |
|  |        +-----+----  selected bg  ---+  +------+ |  v   |
|  |        | 2   | 證                  |  |      | |      |
|  |        +-----+---------------------+  |thumb | |      |
|  |        | 3   | 治                  |  | pill | |      |
|  |        +-----+---------------------+  |      | | track|
|  |        | 4   | 月                  |  |      | |      |
|  |        +-----+---------------------+  +------+ |      |
|  |        | 5   | 快                  |  |      | |      |
|  |        +-----+---------------------+  |      | |      |
|  |        | ...                        |  |      | |      |
|  |        +-----+---------------------+  +------+ |  ^   |
|  |        | 0   | 志                  |  |  \/  | | btn  |
|  |        +-----+---------------------+  +------+ | 14px |
|  |           (bottom padding cyLine/2) |  |  sb  | |  v   |
+--border(1px)---------------------------------------------+

|<-1*cx->|<1*cx>|<--- content area ---->|  |<-sb->|
|        |      |                       |  | 6-16 |
|        |      |                       |  | px   |
|<------------ _cxTitle --------------->|
|<------------------- total width -------------------->|
```

- `cx` = `cxLine` (one CJK character width measured from the configured font)
- `sb` = scrollbar width (6px idle / 16px hover, DPI-scaled)
- Border = 1px on all sides (`CANDWND_BORDER_WIDTH`)

### Row layout

Each candidate row is divided into three horizontal zones. All measurements derive from font metrics using the configured IME font at the current monitor DPI.

`cxLine` is the wider of `tmAveCharWidth` and the average character width measured from a test string of `_wndWidth` CJK characters (typically '國'). `cyLine = fontHeight * 5/4`. `leftPadding = cyLine / 2`.

Row layout (left to right):

| Zone | Left edge | Right edge | Content |
|------|-----------|------------|---------|
| Left margin | 0 | `leftPadding` (`cyLine / 2`) | Empty (background color) |
| Number column | `leftPadding` | `leftPadding + cxLine` | Selection key (1-9, 0) |
| Candidate text | `leftPadding + cxLine` | `windowRight - scrollbarWidth` | Candidate string, selection-highlighted |
| Scrollbar | `windowRight - scrollbarWidth` | `windowRight` | Thin scrollbar (child HWND) |

### Padding on four sides

All four paddings derive from `cyLine`:

| Side | Variable | Formula | Set in |
|------|----------|---------|--------|
| Top | `topPadding` / `fistLineOffset` | `cyLine / 3` | `_ResizeWindow` (line 230) / `_DrawList` (line 929) |
| Bottom | `_padding` | `cyLine / 2` | `WM_CREATE` (line 408), `_DrawList` (line 934) |
| Left | `leftPadding` | `cyLine / 2` | `_DrawList` (line 930) |
| Right | (none — scrollbar fills) | scrollbar width | `_ResizeWindow` (line 229) |

**Critical:** `_padding` must be set in BOTH `WM_CREATE` and `_DrawList`. If `WM_CREATE` omits it (as it did before the 2026-03-31 fix), the initial `_ResizeWindow` produces a shorter height, and the first `_DrawList` triggers a resize that shifts the window downward when flipped above the composition.

### Row height and vertical spacing

| Element | Formula | Description |
|---------|---------|-------------|
| `cyLine` (row height) | `fontHeight * 5/4` | Font height measured via `GetTextExtentPoint32`, multiplied by 1.25 for line spacing |
| `fistLineOffset` (top padding) | `cyLine / 3` | Blank space above the first candidate row |
| `_padding` (bottom padding) | `cyLine / 2` | Visual balance below last row; page indicator area |
| `cyOffset` (text baseline) | `fontHeight / 8 + fistLineOffset` | Vertical offset to center text within each row stripe |

Vertical spacing diagram:

```
  border (1px)
  +-----------------------------------------+  ---
  | fistLineOffset (cyLine/3)               |   ^  top padding
  +-----------------------------------------+   |
  | row 0: [num] [candidate text]  cyLine   |   |
  +-----------------------------------------+   |
  | row 1: [num] [candidate text]  cyLine   |   | row area
  +-----------------------------------------+   |
  | ...                                     |   |
  +-----------------------------------------+   |
  | row N: [num] [candidate text]  cyLine   |   v
  +-----------------------------------------+  ---
  | _padding (cyLine / 2)                   |   ^  bottom padding
  |   page indicator "4/8" drawn here       |   v
  +-----------------------------------------+  ---
  border (1px)
```

Each row stripe occupies exactly `cyLine` pixels vertically. The `ExtTextOut` call with `ETO_OPAQUE` fills the row background and draws text in one operation, so each row is a solid colored stripe with no gap between rows.

### Content width calculation

During `WM_CREATE` and each `_DrawList` call, the content width is measured:

1. Build a test string of `_wndWidth` copies of '國' (a full-width CJK character)
2. Measure via `GetTextExtentPoint32` → `candSize.cx`
3. `_cxTitle = candSize.cx + (leftPadding + cxLine)` — total content width including left margin and number column
4. If any candidate text is wider than `_wndWidth` characters, `_cxTitle` expands to fit and `_ResizeWindow()` is called mid-paint

### Window dimensions

| Element | Formula | Description |
|---------|---------|-------------|
| Content width (`_cxTitle`) | `candSize.cx + (leftPadding + cxLine)` | CJK text area + left margin + number column |
| Scrollbar width | 6px idle / 16px hover (DPI-scaled) | Thin Win11-style overlay |
| Total width | `_cxTitle + scrollbarWidth + 2 * CANDWND_BORDER_WIDTH` | Border is 1px per side |
| Top padding | `_cyRow / 3` | `fistLineOffset` |
| Row area height | `_cyRow * candidateListPageCnt` | Number of candidates per page × row height |
| Bottom padding (`_padding`) | `_cyRow / 2` | Visual balance below last row; page indicator area |
| Total height | `topPadding + rowAreaHeight + _padding + 2 * CANDWND_BORDER_WIDTH` | Set in `_ResizeWindow` |

### Size calculation flow

`_ResizeWindow()` computes the total window size:

```
totalW = _cxTitle + VScrollWidth + CANDWND_BORDER_WIDTH * 2
totalH = _cyRow/3 + _cyRow * pageCnt + _padding + CANDWND_BORDER_WIDTH * 2
```

`_ResizeWindow` is called from:
- `WM_CREATE` → `_Create` → `_ResizeWindow()` — initial size with `_cyRow` and `_padding` from `WM_CREATE` measurement
- `_DrawList` (line 1069) — when `_cxTitle` changes during paint (wider candidate text)
- `_Show(TRUE)` (line 304) — when window has zero size

Both `WM_CREATE` and `_DrawList` must set `_cyRow` and `_padding` consistently, or the initial `_ResizeWindow` produces a different height than `_DrawList`'s `_ResizeWindow`, causing position drift when the candidate is flipped above the composition.

> **Note:** The page indicator ("4/8") is drawn in the bottom padding area. The scrollbar's thin idle line also communicates scroll position visually.

### DPI scaling

All dimensions are implicitly DPI-correct because they derive from font measurement (`GetTextExtentPoint32` on the actual DPI-scaled font). The scrollbar width is explicitly DPI-scaled via `MulDiv(baseWidth, dpi, 96)`.

---

## 3. Scrollbar Design

The scrollbar is a Windows 11-style thin overlay rendered as a child HWND of the candidate window.

### Visual appearance

| Element | Light mode | Dark mode |
|---------|-----------|-----------|
| Track background | Transparent (candidate background shows through) | Transparent |
| Thumb | `RGB(128,128,128)` rounded pill | `RGB(180,180,180)` rounded pill |
| Arrow buttons | `RGB(100,100,100)` filled triangle | `RGB(160,160,160)` filled triangle |

The thumb is drawn as a `RoundRect` with corner radius equal to half the thumb width, giving it a pill shape. The arrow triangles are filled polygons centered in the button areas at the top and bottom, spanning ~60% of the button width (Win11 Explorer style).

### Scrollbar states

| State     | Trigger                                                                       | Appearance                            |
|-----------|-------------------------------------------------------------------------------|---------------------------------------|
| Hidden    | Single page (no scroll needed)                                                | Not shown                             |
| Thin line | Multi-page; Always show OFF; mouse outside scrollbar column                   | 2px centered line, no buttons         |
| Full      | Multi-page; Always show ON — or — Always show OFF + mouse in scrollbar column | 16px wide with triangles + pill thumb |

The transition is driven by `CCandidateWindow::_OnMouseMove` / `WM_MOUSELEAVE` calling `_SetCandidateMouseIn(BOOL)` on the scrollbar. The hit-test uses only the scrollbar column (rightmost `_GetHoverWidth()` px of the candidate client rect), not the full candidate area. Collapse from full to thin is delayed 2 seconds (`SCROLLBAR_COLLAPSE_DELAY_MS`) via `SCROLLBAR_COLLAPSE_TIMER_ID`; if the mouse re-enters the scrollbar column before the timer fires, the timer is cancelled and the scrollbar stays full. `CConfig::GetAlwaysShowScrollbars()` is read live on every paint (not cached) so the setting takes effect immediately without restart.

### Scrollbar geometry

```
  Thin line (6px)   Full / hovered (16px)
  +------+          +----------------+
  |      |          |      /\        | 14px   Up button (equilateral triangle)
  |      |          |     /  \       |
  +------+          +----------------+
  |  |   |          |                |
  |  |   |          |                |         Track (candidate bg color)
  |  |   |          |   +--------+   |
  |  |   |          |   |  pill  |   |         Thumb (pill, proportional height)
  |  |   |          |   +--------+   |
  |  |   |          |                |
  |      |          |                |         Track
  +------+          +----------------+
                    |     \  /       | 14px   Down button (equilateral triangle)
                    |      \/        |
                    +----------------+
```

**Thin line** — 2px wide centered `RoundRect`, full scrollbar height (no button zones), no triangles. The `SetLayeredWindowAttributes` alpha stays at 200/255 (78% opaque).

**Full** — triangles are equilateral, `aw = max(2, btnW/3)` (half-base), `ah = max(2px, aw × 87%)` (√3/2 ratio). Apex at horizontal center `acx = clientWidth/2`.

| Zone        | Height                       | Content                                                                                    |
|-------------|------------------------------|--------------------------------------------------------------------------------------------|
| Up button   | 14px at 96 DPI (DPI-scaled)  | Equilateral upward triangle, centered; gives gap above the thumb track                     |
| Track       | Remaining space              | Candidate bg color; thumb positioned proportionally within track (between the two buttons) |
| Down button | 14px at 96 DPI (DPI-scaled)  | Equilateral downward triangle, centered; gives gap below the thumb track                   |

### Thumb sizing

The thumb height is proportional to the visible page relative to the total candidate count:

- `thumbH = trackHeight * pageSize / totalCount`
- Clamped to a minimum of 12px (DPI-scaled) for clickability
- Clamped to a maximum of trackHeight

### Thumb position

- `thumbY = (trackHeight - thumbH) * scrollPos / (totalCount - pageSize)`
- Position 0 places the thumb at the top of the track
- Position (totalCount - pageSize) places it at the bottom

### Thumb horizontal alignment

The pill thumb must share the same visual center as the triangle apex. Both derive from `acx = clientWidth / 2`.

**Triangle** — `Polygon` with apex at `(acx, ...)` and base vertices at `(acx ± aw, ...)`.

**Pill** — `RoundRect(left, top, right, bottom, radius, radius)` where GDI fills `x = left .. right-1` (right-exclusive). The topmost arc pixel is `floor((left + right - 1) / 2)`. For it to land on `acx`:

```text
(left + right - 1) = 2*acx + 1  →  left + right = 2*acx + 2
```

Using pill half-width `pw = max(2, aw - 1)` (one step narrower than the triangle so the pill sits visually inside the triangle base):

```text
left   = acx - pw + 1
right  = acx + pw + 1
radius = 2 * pw          ← full-width corner ellipse → capsule shape
```

This always produces a half-integer arc center `(left + right - 1)/2 = acx + 0.5`, so GDI renders a **single** topmost pixel at `x = acx` for all DPI values, matching the triangle apex exactly.

### Gap between triangle and thumb

The button area height (`SCROLLBAR_BTN_HEIGHT`, 14px at 96 DPI) defines the gap between each arrow triangle and the nearest edge of the thumb travel range:

```text
  +----------+
  |   /\     |  <- triangle drawn centered in the 14px up-button zone
  |  /  \    |
  +----------+  <- top of track starts here
  |          |
  | +------+ |  <- thumb top (can reach here at most)
```

The larger the button height, the more comfortable the click target around the triangle and the more visible separation between the triangle and the scrolling thumb.

### Visibility behavior

Visibility and appearance match Windows 11 Explorer's scrollbar behavior, driven by `CConfig::GetAlwaysShowScrollbars()` (read live each paint) and mouse tracking via `_SetCandidateMouseIn(BOOL)`:

**Always show OFF (default — `DynamicScrollbars=1` or key missing):**

| Mouse position                | Scrollbar state                         | Width |
|-------------------------------|-----------------------------------------|-------|
| Outside scrollbar column      | Thin line (2px centered, no buttons)    | 6px   |
| Inside scrollbar column       | Full scrollbar (triangles + pill thumb) | 16px  |
| Leaves scrollbar column       | Full → thin after 2s delay              | —     |
| Mouse wheel / trackpad scroll | Thin line (no expansion)                | 6px   |
| Single page                   | Hidden                                  | —     |

The scrollbar column is the rightmost `_GetHoverWidth()` (16px at 96 DPI) of the candidate client rect. Mouse entry is detected by a `PtInRect` hit-test in `CCandidateWindow::_OnMouseMove`; mouse leave is detected via `TrackMouseEvent(TME_LEAVE)`. `WM_MOUSELEAVE` starts a 2s `SCROLLBAR_COLLAPSE_TIMER_ID` timer. When the timer fires, `_SetCandidateMouseIn(FALSE)` collapses the scrollbar. If the mouse re-enters the column before the timer fires, `_EndTimer` cancels it and the scrollbar stays full.

**Always show ON (`DynamicScrollbars=0`):**

| Mouse position | Scrollbar state                         | Width |
|----------------|-----------------------------------------|-------|
| Any            | Full scrollbar (triangles + pill thumb) | 16px  |
| Single page    | Hidden                                  | —     |

`WS_EX_LAYERED` with `SetLayeredWindowAttributes(alpha=200)` keeps both states at 78% opacity. No fade timer runs.

---

## 4. Rendering Pipeline

Both the candidate window and scrollbar use double-buffered painting to eliminate flicker during rapid scrolling.

### Candidate window paint

1. `BeginPaint` obtains the device context
2. Create off-screen bitmap via `CreateCompatibleDC` + `CreateCompatibleBitmap`
3. Paint into the back buffer: background fill, candidate text, selection highlight
4. `BitBlt` the complete frame to screen in a single operation
5. Draw border via `_DrawBorder` (directly to screen, 1px frame)

The `WS_CLIPCHILDREN` style ensures the scrollbar child area is automatically excluded from the candidate window's paint region, preventing double-painting of the overlapping area.

### Scrollbar paint

1. `BeginPaint` obtains the device context
2. Create off-screen bitmap (same `CreateCompatibleDC` pattern)
3. Paint into back buffer: track background, arrow triangles, pill thumb
4. `BitBlt` to screen

### Flicker prevention

| Technique | Scope | Purpose |
|-----------|-------|---------|
| `WM_ERASEBKGND` returns 1 | Candidate + scrollbar | Suppresses system background erase |
| `InvalidateRect(hwnd, NULL, FALSE)` | All DIME windows | The `FALSE` parameter prevents erase flag in `PAINTSTRUCT` |
| Double-buffered `BitBlt` | Candidate + scrollbar | No intermediate visual states during paint |
| `WS_CLIPCHILDREN` | Candidate window | Scrollbar area excluded from parent paint |
| Boundary guard on wheel | Candidate window | No invalidation when selection unchanged at list start/end |

---

## 5. Keyboard Interaction

Keyboard input is handled by the IME's `KeyEventSink`, not by the candidate window itself. The candidate window receives selection changes via its public API.

| Key | Action | At boundary |
|-----|--------|-------------|
| Up arrow | Move selection up by one item | Wraps to last item |
| Down arrow | Move selection down by one item | Wraps to first item |
| Page Up | Move to same position on previous page | Wraps to last page |
| Page Down | Move to same position on next page | Wraps to first page |
| Home | Move to first candidate | — |
| End | Move to last candidate | — |
| Number keys (1-9, 0) | Select candidate by index on current page | — |
| Enter / Space | Commit selected candidate | — |
| Escape | Dismiss candidate window | — |

Keyboard navigation wraps circularly. This is the expected behavior for sequential IME candidate navigation — users press Page Down repeatedly to cycle through all pages without needing to go back to the top manually.

---

## 6. Mouse Interaction

### Candidate list area

| Action | Behavior |
|--------|----------|
| Click on candidate item | Select and commit that candidate |
| Click outside candidate area | Dismiss candidate window |

### Scrollbar interactions

| Action | Behavior | At boundary |
|--------|----------|-------------|
| Click up arrow button | Page up | Stops (no wrap) |
| Click down arrow button | Page down | Stops (no wrap) |
| Hold arrow button | Auto-repeat: 400ms initial delay, then 50ms repeat interval, page per tick | Stops at first/last page |
| Click track above thumb | Page up | Stops |
| Click track below thumb | Page down | Stops |
| Drag thumb | Live position tracking; maps vertical pixel delta to scroll position | Clamped to range |
| Release thumb | Snaps to final position | — |

### Mouse wheel

| Action | Behavior | At boundary |
|--------|----------|-------------|
| Wheel up | Move selection up by one item | Stops (no wrap) |
| Wheel down | Move selection down by one item | Stops (no wrap) |

### Boundary behavior design rationale

Scrollbar and mouse wheel inputs are **positional** — they stop at the top and bottom of the list. The thumb position provides visual feedback that the user has reached the end.

Keyboard inputs are **sequential** — they wrap circularly. IME users expect to press Page Down repeatedly to cycle through all candidate pages without needing to scroll back.

| Input type | Circular? | Rationale |
|------------|-----------|-----------|
| Keyboard (arrow keys, Page Up/Down) | Yes | Sequential navigation; cycle through all candidates |
| Mouse wheel | No | Positional; standard scroll behavior |
| Scrollbar buttons | No | Part of scrollbar; positional |
| Track click | No | Positional |
| Thumb drag | No (clamped) | Positional |

### Scrollbar visibility on hover

Mouse proximity to the scrollbar area triggers it to appear (in auto-hide mode) and expand to the hover width. Moving the mouse away causes it to shrink and eventually fade out after the inactivity timeout.

---

## 7. Hit Testing

The scrollbar defines five hit zones, tested in order:

| Zone | Region | Action on click |
|------|--------|----------------|
| `SB_HIT_BTN_UP` | Top button area | Page up |
| `SB_HIT_BTN_DOWN` | Bottom button area | Page down |
| `SB_HIT_THUMB` | Thumb pill rect | Begin drag |
| `SB_HIT_TRACK_ABOVE` | Track between up button and thumb | Page up |
| `SB_HIT_TRACK_BELOW` | Track between thumb and down button | Page down |

---

## 8. System Settings Integration

### Accessibility: Always show scrollbars

Read from `HKCU\Control Panel\Accessibility\DynamicScrollbars` via `CConfig::GetAlwaysShowScrollbars()`. Default is auto-hide (always show = FALSE) when the key is missing.

`GetAlwaysShowScrollbars()` is called live on every paint — it is never cached — so toggling the system setting takes effect at the next paint without restarting DIME. This matches the `GetEffectiveDarkMode()` pattern used for dark/light mode.

Mouse tracking uses `TrackMouseEvent(TME_LEAVE)` registered from `CCandidateWindow::_OnMouseMove`. When the mouse leaves the candidate window's bounding rect, Windows delivers `WM_MOUSELEAVE` to the candidate, which calls `_SetCandidateMouseIn(FALSE)` to collapse the scrollbar back to the thin line. Moving the mouse from the candidate content area onto the scrollbar child does **not** fire `WM_MOUSELEAVE` on the candidate (child HWNDs are within the parent's bounding rect), so the full scrollbar remains visible while interacting with it.

### Dark mode

The scrollbar reads `CConfig::GetEffectiveDarkMode()` to choose thumb and arrow colors. The candidate window uses `_brshBkColor` for its background, which is set by the IME's color configuration (system/light/dark/custom modes).

### DPI

Scrollbar dimensions are scaled via `MulDiv(baseValue, monitorDpi, 96)`. The monitor DPI is obtained from `CConfig::GetDpiForHwnd()`, which uses `GetDpiForMonitor` on Windows 8.1+ with fallback to `GetDeviceCaps(LOGPIXELSY)` on Windows 7.

---

## 9. Source Files

| File | Role |
|------|------|
| `CandidateWindow.h` / `.cpp` | Candidate list window: layout, painting, selection, keyboard/mouse dispatch |
| `ScrollBarWindow.h` / `.cpp` | Thin scrollbar: thumb geometry, hit testing, drag, auto-hide, fade, arrow buttons |
| `BaseWindow.h` / `.cpp` | Base class for all DIME windows: HWND management, invalidation, capture |
| `ShadowWindow.h` / `.cpp` | Drop shadow behind candidate window |
| `UIPresenter.cpp` | Creates and manages candidate/notify windows; bridges TSF and UI |
| `Config.h` / `.cpp` | `GetAlwaysShowScrollbars()`, `GetEffectiveDarkMode()`, `GetDpiForHwnd()`, font creation |

---

## 10. Compatibility

| Windows Version | Candidate Window | Scrollbar |
|----------------|-----------------|-----------|
| Windows 7 | Full functionality | Thin pill thumb and arrow triangles render correctly (standard GDI: `RoundRect`, `Polygon`). Always visible — `WS_EX_LAYERED` on child windows is silently ignored by Win7, so fade/auto-hide has no effect. The window is fully opaque at all times. DPI from `GetDeviceCaps`. |
| Windows 8+ | Full functionality | `WS_EX_LAYERED` on child windows supported. Auto-hide with alpha fade works. |
| Windows 8.1+ | Full functionality | Per-monitor DPI via `GetDpiForMonitor`. |
| Windows 10+ | Full functionality | Full thin scrollbar with auto-hide and per-monitor DPI via `GetSystemMetricsForDpi`. |
| Windows 11 | Full functionality | Respects system "Always show scrollbars" accessibility setting (`DynamicScrollbars` registry key). |

### API compatibility details

| Feature | API | Minimum Windows | Fallback on older Windows |
|---------|-----|-----------------|--------------------------|
| Pill thumb | `RoundRect` (GDI) | Windows 2000 | N/A — always available |
| Arrow triangles | `Polygon` (GDI) | Windows 2000 | N/A — always available |
| Double-buffer | `CreateCompatibleDC` + `BitBlt` | Windows 2000 | N/A — always available |
| Alpha fade | `WS_EX_LAYERED` on child + `SetLayeredWindowAttributes` | Windows 8 | Scrollbar always fully visible (no fade) |
| Auto-hide | Fade timer + layered alpha | Windows 8 | Always visible |
| Per-monitor DPI | `GetDpiForMonitor` (Shcore.dll) | Windows 8.1 | `GetDeviceCaps(LOGPIXELSY)` — system DPI |
| DPI-scaled metrics | `GetSystemMetricsForDpi` (User32.dll) | Windows 10 1607 | `GetSystemMetrics` — system DPI |
| Always-show setting | `DynamicScrollbars` registry key | Windows 11 | Key missing → default auto-hide (always visible on Win7 regardless) |

All rendering uses standard Win32 GDI APIs. No external dependencies or runtime DLL loading required for the UI layer.

### Legacy (DPI-unaware) app compatibility (fixed 2026-03-30)

Old Win32 apps (e.g. Windows XP-era Notepad) are DPI-unaware. Four bugs were fixed:

- **0×0 window:** `_Show(TRUE)` now calls `_ResizeWindow()` if the window has zero size, since the removal of `WS_BORDER` means zero client area = zero physical size = no `WM_PAINT`.
- **Wrong position:** Skip `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` override when the parent window is DPI-unaware, so the IME inherits the app's virtualized coordinate space.
- **Scrollbar creation failure:** Retry `CreateWindowEx` without `WS_EX_LAYERED` on failure; removed `_DeleteShadowWnd()` from the scrollbar failure path so shadow and scrollbar are independent.
- **Scrollbar magenta background:** On non-layered scrollbar windows (where `WS_EX_LAYERED` failed), the magenta color key painted as solid magenta. Fixed by using the parent's background brush via `WM_CTLCOLORSCROLLBAR` for non-layered windows. The selection highlight is drawn on top of all scrollbar elements via `_SetSelectionHighlight` so it visually extends over the scrollbar with its original `RoundRect` shape.

See [CANDI_SCROLL_BAR.md](CANDI_SCROLL_BAR.md) RC22–RC26 for details.

### Caret tracking fixes (2026-03-31)

Three caret tracking improvements were made. See [CARET_TRACKING.md](CARET_TRACKING.md) for full details.

- **Associated phrase space hack removed:** The space character `L" "` inserted into the composition buffer for phrase candidate positioning has been eliminated. Phrase candidates now position using `_rectCompRange` (the last saved composition rect). Required changes to `_StartLayout` (update range on same context), `_StartCandidateList` (skip `MakeCandidateWindow` if exists; fall back to `_rectCompRange`), and `CandidateHandler.cpp` (direct `_StartCandidateList(nullptr)` call).
- **SearchHost.exe jump fix:** Windows 11 Explorer search box returns bogus `{0,0,1,1}` rects, causing the candidate to jump to screen origin. Fixed by tightening the validity check and adding `_candLocation` fallback.

### Notify/candidate bottom alignment when flipped above (pre-existing, under investigation)

When the candidate flips above the composition (not enough room below), the notify visually appears higher than the candidate despite the math being correct. Debug data confirms `candPt.y + candH = notifyY + notifyH` (mathematically bottom-aligned). `_pNotifyWnd->_GetHeight()` may not match the actual rendered height of the notify window.
