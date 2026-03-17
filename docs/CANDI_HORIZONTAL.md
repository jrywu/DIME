# Horizontal Candidate Window — Implementation Plan

## Goal

Add an optional horizontal rendering mode for the candidate window.
In horizontal mode, candidate items are laid out left-to-right on one or more rows instead of top-to-bottom.
A new **characters-per-line** parameter controls wrapping: when the cumulative character count of items on the current row would exceed the limit, the next item starts on a new row.
An **HScrollBar at the bottom** replaces the VScrollBar on the right for multi-page navigation in horizontal mode.

---

## Visual Comparison

### Current (vertical)

```
┌────────────────┐
│ 1 候選一       │
│ 2 候選二       │
│ 3 候選三       │
│ 4 候選四       │
│ 5 候選五       │
│          1/3   │
└────────────────┘
```

### New (horizontal, maxCharsPerLine = 20)

```
┌──────────────────────────────────┐
│ 1候選一 2候選二 3候選三 4候選四  │
│ 5候選五 6候選六 7候選七          │
│ [◄]                         [►] │  ← HScrollBar (page navigation)
└──────────────────────────────────┘
```

---

## Files to Change

| File | Change |
| ---- | ------ |
| `src/ScrollBarWindow.h` | Declare `_IsHorizontal()` helper |
| `src/ScrollBarWindow.cpp` | Make `_Resize`, `_GetBtnUpRect`, `_GetBtnDnRect`, `_GetScrollArea` orientation-aware |
| `src/CandidateWindow.h` | Add `_isHorizontal`, `_maxCharsPerLine`, `_currentPageRows`; update `_Create`; declare `_BuildHorizontalLayout` and `_CandItemRect` |
| `src/CandidateWindow.cpp` | New layout helper; branch `_ResizeWindow`, `_DrawList`, `_OnLButtonDown`, `_OnMouseMove`, `_DrawPageIndicator` |
| `src/Config.h` | Add `_candidateHorizontal`, `_candidateMaxCharsPerLine` statics + accessors |
| `src/Config.cpp` | Load/save new INI keys; initialize defaults |
| `src/UIPresenter.cpp` | Pass new config values in `MakeCandidateWindow` (`UIPresenter.cpp:1604`) |

No changes required in `CandidateHandler.cpp`, `KeyHandler.cpp`, `CompositionProcessorEngine.h`, or the shadow window class.

---

## Step 1 — Fix `CScrollBarWindow` for horizontal orientation

`CScrollBarWindow` already checks `WS_HSCROLL` at creation time to draw Left/Right arrow buttons (`DFCS_SCROLLLEFT`/`DFCS_SCROLLRIGHT`). However, button *positioning* and `_Resize` are hardcoded vertical.

**Constraint:** When `_IsHorizontal()` returns `false`, every code path must be byte-for-byte identical to the current code. New `if (_IsHorizontal())` branches are purely additive — the `else` branches reproduce the existing logic verbatim.

### `ScrollBarWindow.h`

Add a private inline helper:

```cpp
bool _IsHorizontal() const { return (_GetWnd() && (GetWindowLong(_GetWnd(), GWL_STYLE) & WS_HSCROLL)); }
```

### `ScrollBarWindow.cpp` — `_Resize(int x, int y, int cx, int cy)`

After calling `CBaseWindow::_Resize`, branch on orientation. The `else` branch is the **current code moved verbatim**:

```cpp
RECT rc = {0,0,0,0};
_GetClientRect(&rc);
if (_IsHorizontal()) {
    // Left button on left, Right button on right
    if (_pBtnUp) _pBtnUp->_Resize(rc.left, rc.top, _sizeOfScrollBtn.cx, rc.bottom - rc.top);
    if (_pBtnDn) _pBtnDn->_Resize(rc.right - _sizeOfScrollBtn.cx, rc.top, _sizeOfScrollBtn.cx, rc.bottom - rc.top);
} else {
    // existing vertical logic — not changed
    if (_pBtnUp) _pBtnUp->_Resize(rc.left, rc.top, rc.right - rc.left, _sizeOfScrollBtn.cy);
    if (_pBtnDn) _pBtnDn->_Resize(rc.left, rc.bottom - _sizeOfScrollBtn.cy, rc.right - rc.left, _sizeOfScrollBtn.cy);
}
```

### `ScrollBarWindow.cpp` — `_GetBtnUpRect` / `_GetBtnDnRect` / `_GetScrollArea`

Branch each on `_IsHorizontal()`. The `else` branch in every case is the **current code moved verbatim**:

- **`_GetBtnUpRect`**: horizontal → left strip `(0, 0, _sizeOfScrollBtn.cx, height)`; else → current top strip
- **`_GetBtnDnRect`**: horizontal → right strip `(width - _sizeOfScrollBtn.cx, 0, width, height)`; else → current bottom strip
- **`_GetScrollArea`**: horizontal → middle horizontal strip; else → current middle vertical strip

`WM_VSCROLL` notifications are kept as-is — `CandidateWindow::_OnVScroll` already handles `SB_PAGEDOWN`/`SB_PAGEUP` which is sufficient for page flipping.

---

## Step 2 — New members in `CCandidateWindow`

### `CandidateWindow.h` — private members (alongside `_cyRow`, `_cxTitle`)

```cpp
BOOL _isHorizontal;        // TRUE = horizontal layout
UINT _maxCharsPerLine;     // 0 = unlimited; >0 = char wrap threshold
int  _currentPageRows;     // rows used by current page (set during layout)
```

### `CandidateWindow.h` — private struct + helper method

```cpp
struct _CandItemRect {
    UINT listIndex;
    RECT rc;          // client-relative bounding rect
};
void _BuildHorizontalLayout(_In_ HDC dc, _In_ UINT startIndex, _In_ int itemCount,
                             _In_ int cxLine, _In_ int cyLine,
                             _Out_ std::vector<_CandItemRect>& out,
                             _Out_ int& totalWidth, _Out_ int& totalRows);
```

### `CandidateWindow.h` — updated `_Create` signature

Add two optional parameters; defaults keep all existing callers unchanged:

```cpp
BOOL _Create(_In_ UINT wndWidth, _In_ UINT fontSize,
             _In_opt_ HWND parentWndHandle,
             _In_ BOOL isHorizontal    = FALSE,
             _In_ UINT maxCharsPerLine = 0);
```

### `CandidateWindow.cpp` — constructor

Initialize: `_isHorizontal = FALSE; _maxCharsPerLine = 0; _currentPageRows = 1;`

### `CandidateWindow.cpp` — `_Create`

Store the two new params. When creating the scrollbar window in horizontal mode, pass `WS_CHILD | WS_HSCROLL` (instead of `WS_CHILD`) so it draws Left/Right arrows.

---

## Step 3 — `_BuildHorizontalLayout` algorithm

```
col_x = 0, row = 0, charsSoFar = 0
for each item i in [startIndex, startIndex + itemCount):
    itemChars = 1 (index char) + item text length
    itemPixels = GetTextExtentPoint32(index char + space + text) + gap (1 x cxLine)
    if maxCharsPerLine > 0 AND charsSoFar + itemChars > maxCharsPerLine AND col_x > 0:
        row++, col_x = 0, charsSoFar = 0
    rc = { col_x, row*cyLine, col_x + itemPixels, (row+1)*cyLine }
    push { listIndex = startIndex+i, rc }
    col_x += itemPixels
    charsSoFar += itemChars
totalRows = row + 1
totalWidth = widest row in pixels
```

---

## Step 4 — `_ResizeWindow`

Branch on `_isHorizontal`:

**Horizontal:**

1. Get DC, select font, call `_BuildHorizontalLayout` for the current page.
2. `windowWidth  = totalWidth + CANDWND_BORDER_WIDTH*2`
3. `HScrollBarHeight = GetSystemMetrics(SM_CYHSCROLL) * 3/2`
4. `windowHeight = totalRows * _cyRow + (isMultiPage ? HScrollBarHeight : _cyRow/2) + CANDWND_BORDER_WIDTH*2`
5. `CBaseWindow::_Resize(_x, _y, windowWidth, windowHeight)`
6. Position scrollbar at bottom: `_pVScrollBarWnd->_Resize(0, totalRows * _cyRow, windowWidth, HScrollBarHeight)`

**Vertical:** existing logic unchanged.

---

## Step 5 — `_DrawList`

After the common font/metric setup and resize-guard check, branch on `_isHorizontal`:

**Horizontal (new):**

- Call `_BuildHorizontalLayout` to get item rects.
- For each `_CandItemRect`: draw index char in left `PageCountPosition * cxLine` pixels, draw candidate text in the remainder.
- Apply selected/unselected colors exactly as the vertical path.
- Set `_currentPageRows` from the helper output.
- Fill trailing empty cells on the last row with `FillRect(_brshBkColor)`.
- No `VScrollWidth` subtraction (HScrollBar is at bottom, not right).

**Vertical (`_isHorizontal == FALSE`):** existing loop is **untouched** — wrap it in `else { /* current code verbatim */ }`.

---

## Step 6 — Hit-testing

### `_OnLButtonDown`

**Horizontal (new):** call `_BuildHorizontalLayout` to get rects; `PtInRect` each one. No scrollbar area subtraction needed (HScrollBar is below item area, not overlapping).

**Vertical (`_isHorizontal == FALSE`):** existing code is **untouched** — wrap it in `else { /* current code verbatim */ }`.

### `_OnMouseMove`

**Horizontal (new):** `PtInRect` each item rect for `IDC_HAND`; `IDC_ARROW` otherwise. The existing `PtInRect(&rcWindow, pt)` guard (lines 707–718) already shows/hides the HScrollBar correctly — no additional changes.

**Vertical (`_isHorizontal == FALSE`):** existing code is **untouched** — wrap it in `else { /* current code verbatim */ }`.

---

## Step 7 — `_DrawPageIndicator`

Replace `candidateListPageCnt * cyLine` with `_currentPageRows * cyLine` for the indicator top edge.

In horizontal mode with multiple pages, the HScrollBar provides sufficient navigation feedback. Skip drawing the `X/Y` text indicator when `_isHorizontal && isMultiPage`. Otherwise draw it as before (single-page horizontal, or vertical).

---

## Step 8 — HScrollBar show/hide (matches current VScrollBar behavior)

The HScrollBar must behave exactly like the current VScrollBar: **hidden by default, shown only when the cursor is inside the candidate window rect**.

Because `_pVScrollBarWnd` is reused for both orientations, all three existing show/hide code paths apply automatically to horizontal mode with **zero code changes**:

| Location | Existing code | Effect in horizontal mode |
| -------- | ------------- | ------------------------- |
| `CCandidateWindow::_Show` (line 304) | `_pVScrollBarWnd->_Show(FALSE)` when `ALWAYS_SHOW_SCROLL` not defined | HScrollBar starts hidden when candidate window appears |
| `_OnMouseMove` (lines 707–718) | `_Show(_IsEnabled())` if `PtInRect(&rcWindow, pt)`, else `_Show(FALSE)` | HScrollBar shown when cursor enters candidate rect, hidden when cursor leaves |
| `_AddString` (line 1037) | `_pVScrollBarWnd->_Show(FALSE)` | HScrollBar hidden when candidate list is rebuilt |

No changes to `_Show`, `_OnMouseMove`, or `_AddString` are needed for the scrollbar show/hide behavior.

---

## Step 9 — Config (`Config.h` / `Config.cpp`)

Add alongside existing candidate settings:

| Key | Type | Default | INI name |
| --- | ---- | ------- | -------- |
| `_candidateHorizontal` | `BOOL` | `FALSE` | `CandidateHorizontal` |
| `_candidateMaxCharsPerLine` | `UINT` | `0` | `CandidateMaxCharsPerLine` |

```cpp
static BOOL  GetCandidateHorizontal()            { return _candidateHorizontal; }
static void  SetCandidateHorizontal(BOOL v)      { _candidateHorizontal = v; }
static UINT  GetCandidateMaxCharsPerLine()       { return _candidateMaxCharsPerLine; }
static void  SetCandidateMaxCharsPerLine(UINT v) { _candidateMaxCharsPerLine = v; }
```

Persist to the same INI section as `CandidateWindowWidth` for each IME mode.

---

## Step 10 — `UIPresenter::MakeCandidateWindow` (`UIPresenter.cpp:1604`)

```cpp
_pCandidateWnd->_Create(wndWidth, CConfig::GetFontSize(), parentWndHandle,
                        CConfig::GetCandidateHorizontal(),
                        CConfig::GetCandidateMaxCharsPerLine());
```

---

## Edge Cases

| Case | Handling |
| ---- | -------- |
| `maxCharsPerLine = 0` | All items on one row; wrap never fires |
| Single item longer than limit | Placed on its own row; never split |
| Only 1 page | HScrollBar hidden (same enable/disable logic as VScrollBar) |
| Window wider than monitor | `_Move` clips to monitor work area as before |
| Vertical mode | All new paths guarded by `if (_isHorizontal)`; vertical path untouched |
| Toggle at runtime | Destroy + recreate candidate window (same as font-size change) |

---

## Step 11 — DIMESettings GUI controls

Add UI controls for the two new settings in the existing candidate property page.

**Files:** `src/DIME.rc`, `src/resource.h`, `src/ConfigDialog.cpp`

All 4 IME modes share a **single dialog template** `IDD_DIALOG_COMMON` (ID 111). The candidate controls live in the "候選視窗設定" groupbox. Adding controls here automatically applies to all modes — no per-mode duplication needed.

A **combo box** is used for layout direction (rather than a checkbox) because it labels both states explicitly and is extensible if more layout modes are added later.

### New resource IDs (`resource.h`)

Last defined ID is `IDC_STATIC_CHARSET_SCOPE = 1171`.

```cpp
#define IDC_COMBO_CANDIDATE_LAYOUT           1172
#define IDC_STATIC_CANDIDATE_LAYOUT          1173
#define IDC_EDIT_CANDIDATE_MAXCHARSPERLINE   1174
#define IDC_STATIC_CANDIDATE_MAXCHARSPERLINE 1175
```

### Dialog resource (`DIME.rc`)

Insert inside the "候選視窗設定" groupbox in `IDD_DIALOG_COMMON`, below the existing `IDC_EDIT_MAXWIDTH` / beep controls:

- `LTEXT "候選排列方式:"` → `IDC_STATIC_CANDIDATE_LAYOUT`
- `COMBOBOX` → `IDC_COMBO_CANDIDATE_LAYOUT` (CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP)
  - Item 0: `"直排"` (Vertical, stored as `FALSE`)
  - Item 1: `"橫排"` (Horizontal, stored as `TRUE`)
- `LTEXT "每行字數上限 (0=不限):"` → `IDC_STATIC_CANDIDATE_MAXCHARSPERLINE`
- `EDITTEXT` → `IDC_EDIT_CANDIDATE_MAXCHARSPERLINE` (ES_NUMBER | WS_TABSTOP)
  (disabled / greyed out when layout is Vertical)

### `WM_INITDIALOG` / `ParseConfig()` — populate and load controls

```cpp
// Populate combo once (WM_INITDIALOG)
SendDlgItemMessage(hDlg, IDC_COMBO_CANDIDATE_LAYOUT, CB_ADDSTRING, 0, (LPARAM)L"Vertical");
SendDlgItemMessage(hDlg, IDC_COMBO_CANDIDATE_LAYOUT, CB_ADDSTRING, 0, (LPARAM)L"Horizontal");

// Load current value (ParseConfig)
SendDlgItemMessage(hDlg, IDC_COMBO_CANDIDATE_LAYOUT, CB_SETCURSEL,
    CConfig::GetCandidateHorizontal() ? 1 : 0, 0);

WCHAR num[8] = {};
_snwprintf_s(num, _TRUNCATE, L"%d", CConfig::GetCandidateMaxCharsPerLine());
SetDlgItemTextW(hDlg, IDC_EDIT_CANDIDATE_MAXCHARSPERLINE, num);
EnableWindow(GetDlgItem(hDlg, IDC_EDIT_CANDIDATE_MAXCHARSPERLINE),
    CConfig::GetCandidateHorizontal());
```

### `WM_COMMAND` handler — mark page dirty and enable/disable edit

```cpp
case IDC_COMBO_CANDIDATE_LAYOUT:
    if (HIWORD(wParam) == CBN_SELCHANGE) {
        BOOL isHoriz = (SendDlgItemMessage(hDlg, IDC_COMBO_CANDIDATE_LAYOUT,
                            CB_GETCURSEL, 0, 0) == 1);
        EnableWindow(GetDlgItem(hDlg, IDC_EDIT_CANDIDATE_MAXCHARSPERLINE), isHoriz);
        PropSheet_Changed(GetParent(hDlg), hDlg);
        ret = TRUE;
    }
    break;
case IDC_EDIT_CANDIDATE_MAXCHARSPERLINE:
    if (HIWORD(wParam) == EN_CHANGE) {
        PropSheet_Changed(GetParent(hDlg), hDlg);
        ret = TRUE;
    }
    break;
```

### `PSN_APPLY` handler — save from controls

```cpp
CConfig::SetCandidateHorizontal(
    SendDlgItemMessage(hDlg, IDC_COMBO_CANDIDATE_LAYOUT, CB_GETCURSEL, 0, 0) == 1);

WCHAR num[8] = {};
GetDlgItemText(hDlg, IDC_EDIT_CANDIDATE_MAXCHARSPERLINE, num, _countof(num));
CConfig::SetCandidateMaxCharsPerLine((UINT)_wtol(num));
```

(`WriteConfig()` / INI persistence is already covered in Step 9.)

---

## Step 12 — CLI `--set` keys

**File:** `src/DIMESettings/CLI.cpp`

### Add to `g_keyRegistry[]`

```cpp
{ L"CandidateHorizontal",    BOOL_T,  0,   1,   CLI_MASK_ALL },
{ L"CandidateMaxCharsPerLine", INT_T, 0, 999,   CLI_MASK_ALL },
```

### Add to `GetKeyValue()`

```cpp
else if (wcscmp(n, L"CandidateHorizontal") == 0)      v = CConfig::GetCandidateHorizontal();
else if (wcscmp(n, L"CandidateMaxCharsPerLine") == 0)  v = (int)CConfig::GetCandidateMaxCharsPerLine();
```

### Add to `ApplyKeyValue()`

```cpp
else if (wcscmp(n, L"CandidateHorizontal") == 0)      CConfig::SetCandidateHorizontal((BOOL)v);
else if (wcscmp(n, L"CandidateMaxCharsPerLine") == 0)  CConfig::SetCandidateMaxCharsPerLine((UINT)v);
```

---

## Out of Scope

- Animated fade-in (`NO_ANIMATION` defined; animation path unaffected).
