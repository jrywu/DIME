# DIME Settings — Child Controls Float Above Section Title When Scrolled

## Context

In the new DIME settings UI (`DIMESettings`), the section title (e.g. "大易輸入法") is repainted on top of the scrolled card list to act as a fixed header — see [SettingsWindow.cpp:1334-1348](../src/DIMESettings/SettingsWindow.cpp#L1334-L1348) and the "Section title cover" note in [NEW_DIME_SETTIGNS.md:301](NEW_DIME_SETTIGNS.md#L301).

Cards themselves are owner-drawn, so the cover band hides them correctly. But each card row that uses a real Win32 child control — `ComboBox`, `Edit` (e.g. `組字區最大長度`), `RichEdit`, owner-draw swatch statics — is a *child window*. Win32 child windows always paint on top of the parent's background, regardless of what the parent's `WM_PAINT` drew there.

Symptom: when the page is scrolled, child controls whose row has scrolled into the title-cover band remain visible above the section title — combos and edit fields appear to "float" beside or over the title text.

Intended outcome: as a control's row scrolls into the title-cover band, the control disappears cleanly and reappears once the row is fully below the title again.

## Root Cause

`SettingsWindow::RepositionControlsForScroll()` originally only called `SetWindowPos` to move each child to `(origX, origY - scrollPos)` — no visibility gating. So once `origY - scrollPos < titleBottom`, the child window painted on top of the section title that the parent's `WM_PAINT` had drawn underneath it.

A second contributing factor: `ComboBox` is created with `CBS_DROPDOWNLIST` and the *full* dropdown-open height (`g_geo.comboDropH ≈ 200`) as its window height, but only the top `g_geo.ctrlHeight (24)` is painted when the combo is closed. This made any "fully hidden" threshold based on `origH` insensitive — the combo had to scroll ~200 px before it was treated as fully under the title.

## Approach — Snap-Hide Whenever Top Crosses the Title Cover Band

Tried first: per-control `SetWindowRgn` to clip the upper portion of each control as it scrolls into the title band. In practice this looked worse than hiding — settings rows are short (~24 px tall), so a partially-clipped combo or edit reads as a floating box rather than a graceful reveal, and a clipped `CBS_DROPDOWNLIST` combo exposes its dead Y range below the closed area.

Final behaviour: snap-hide. If `newY = origY - scrollPos < titleBottom`, the control is `SW_HIDE`-d; otherwise its region is cleared and it's `SW_SHOW`-n. The threshold is `titleBottom = ScaleForDpi(g_geo.topMargin, dpi)`, which works for normal Level 0 / Level 1, narrow Level 0 (hamburger band ≤ topMargin), and narrow Level 1 (breadcrumb is outside `hContentArea`, so the cover within content area still starts at 0).

## Changes Made

### 1. `ControlHandle` gained a `visH` field

[SettingsWindow.h:102-109](../src/DIMESettings/SettingsWindow.h#L102-L109):

```cpp
struct ControlHandle {
    SettingsControlId id;
    HWND hWnd;
    int origX, origY;  // original position (before scroll offset)
    int origW, origH;  // window size (combobox origH = dropdown-open height)
    int visH;          // visible/painted height (for combo == closed control height)
};
```

`visH` is reserved for cases where the window height differs from the painted/visible area — currently only `ComboBox`. It is no longer used by `RepositionControlsForScroll` (snap-hide makes it unnecessary), but is kept for any future feature that needs to know how tall the painted region is.

### 2. `AddControl` accepts an optional `visH`

[SettingsWindow.h:147](../src/DIMESettings/SettingsWindow.h#L147), [SettingsWindow.cpp:1505-1511](../src/DIMESettings/SettingsWindow.cpp#L1505-L1511):

```cpp
void SettingsWindow::AddControl(WindowData* wd, SettingsControlId id, HWND h,
                                int x, int y, int w, int ht, int visH /* = -1 */)
{
    WindowData::ControlHandle ch = {};
    ch.id = id; ch.hWnd = h; ch.origX = x; ch.origY = y; ch.origW = w; ch.origH = ht;
    ch.visH = (visH > 0) ? visH : ht;
    wd->controlHandles.push_back(ch);
    if (h) SetWindowSubclass(h, ChildWheelSubclassProc, 1, 0);
}
```

### 3. ComboBox creation passes `ctrlHeight` as `visH`

[SettingsWindow.cpp:1543](../src/DIMESettings/SettingsWindow.cpp#L1543):

```cpp
AddControl(wd, lr.id, h, ctrlRight - comboW, cy, comboW,
           ScaleForDpi(g_geo.comboDropH, dpi),
           ScaleForDpi(g_geo.ctrlHeight, dpi));   // visH = closed-control height
```

The lambda `addCtrl` in `RebuildContentArea` ([SettingsWindow.cpp:2196-2206](../src/DIMESettings/SettingsWindow.cpp#L2196-L2206)) was also updated to set `ch.visH = oh` (treat the window height as the visible height for paths that don't go through `AddControl`).

### 4. `RepositionControlsForScroll` snap-hides under the title

[SettingsWindow.cpp:620-642](../src/DIMESettings/SettingsWindow.cpp#L620-L642):

```cpp
void SettingsWindow::RepositionControlsForScroll(WindowData* wd)
{
    if (!wd->hContentArea) return;
    UINT dpi = CConfig::GetDpiForHwnd(wd->hContentArea);
    int titleBottom = ScaleForDpi(g_geo.topMargin, dpi);
    for (auto& ch : wd->controlHandles) {
        if (!(ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0)))
            continue;
        int newY = ch.origY - wd->scrollPos;
        SetWindowPos(ch.hWnd, nullptr, ch.origX, newY,
            ch.origW, ch.origH, SWP_NOZORDER | SWP_NOACTIVATE);
        // Snap-hide any control whose top enters the title cover band.
        // Settings rows are short (~24 px) — partial clipping looks like a
        // floating control above the title rather than a graceful reveal.
        if (newY < titleBottom) {
            if (IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
        } else {
            SetWindowRgn(ch.hWnd, nullptr, TRUE);
            if (!IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_SHOW);
        }
    }
}
```

`SetWindowRgn(hWnd, nullptr, TRUE)` on the show path clears any region that earlier (interim partial-clip) versions of this code may have left behind, and is harmless if no region was set.

## Why Not Partial Clipping (`SetWindowRgn`)?

Tried, rejected. For the controls in this UI:

- **ComboBox** has `origH ≈ 200` (dropdown height) but only paints into `Y 0..ctrlHeight(24)`. A region clipping the top exposed the dead Y range below the closed control, which appeared as a floating empty rectangle.
- **Edit / Toggle / Button** are ~24 px tall. Clipping a few pixels off the top produced a tiny visible sliver above the title that read as broken UI rather than as smooth progress.

Snap-hide is what the user wants and matches Win11 settings behaviour where controls disappear cleanly under fixed headers.

## Why Not a Single Parent Clip Region?

Setting a clip region on the content-area parent does not affect child windows — Win32 child clipping is per-window via `SetWindowRgn`, or the parent must use `WS_CLIPCHILDREN` plus child `WS_CLIPSIBLINGS`, neither of which clips against an arbitrary "below title" rectangle.

## Files Modified

- [src/DIMESettings/SettingsWindow.h](../src/DIMESettings/SettingsWindow.h) — `ControlHandle::visH` field; `AddControl` `visH` parameter (default `-1`).
- [src/DIMESettings/SettingsWindow.cpp](../src/DIMESettings/SettingsWindow.cpp) — `AddControl` body; `RowType::ComboBox` `AddControl` call passes `ctrlHeight` as `visH`; `addCtrl` lambda sets `ch.visH`; `RepositionControlsForScroll` rewritten to snap-hide.

## Reused Functions / Utilities

- `CConfig::GetDpiForHwnd(HWND)` — already used elsewhere in scroll handlers.
- `ScaleForDpi(value, dpi)` — DPI scaling helper used throughout.
- `g_geo.topMargin` ([SettingsGeometry.h:42](../src/DIMESettings/SettingsGeometry.h#L42)) — already defined as "space for section title".
- `g_geo.ctrlHeight` ([SettingsGeometry.h:98](../src/DIMESettings/SettingsGeometry.h#L98)) — closed combo / edit height.
- Win32 `ShowWindow`, `SetWindowRgn`, `IsWindowVisible` — no new dependencies.

## Build Notes

`DIMESettings.vcxproj` declares `<PlatformToolset>v145</PlatformToolset>`, which is unavailable on the dev machine; build with `/p:PlatformToolset=v143` from the solution:

```sh
MSBuild src/DIME.sln /t:DIMESettings /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143
```

Output: `src/Release/DIMESettings.exe`. The link step requires the running settings window to be closed first.

## Verification

1. Build `DIMESettings.exe` per the command above.
2. Launch `src/Release/DIMESettings.exe`, select **大易輸入法**, ensure window is tall enough that scrolling is required.
3. Scroll down so the `字集查詢範圍` and `九宮格數字鍵盤` rows pass under the title.
   - Expected: their combo controls disappear the moment their top crosses the title-cover edge — no floating combos beside "大易輸入法".
4. Continue scrolling so `組字區最大長度` (Edit) passes under the title.
   - Expected: the `4` edit field disappears cleanly with the row.
5. Scroll back up; controls reappear with full geometry, dropdowns still work.
6. Repeat for `中英切換熱鍵` / `全半形輸入模式` (ComboBox), `自定碼表` page buttons, and Level 1 `自建詞庫` RichEdit.
7. Resize window narrow enough that the sidebar collapses (`< 740px`); re-test scrolling on Level 0 and Level 1.
8. Switch to dark mode — hide/show behaviour unchanged.
9. DPI test: 100% / 125% / 150% / 200% — threshold scales via `ScaleForDpi(topMargin, dpi)`.
10. Existing integration tests under [src/tests/SettingsDialogIntegrationTest.cpp](../src/tests/SettingsDialogIntegrationTest.cpp) should still pass.
