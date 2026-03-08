# DIME IME UI Light/Dark Theme Implementation Plan

## Overview

This document describes the plan to add light/dark theme support to the IME **candidate window** and **notify window**. The settings dialog (`CommonPropertyPageWndProc` in `Config.cpp`) and the `DIMESettings` application already support Windows light/dark theming. This plan extends theming to the runtime IME UI pop-up windows.

A new per-IME setting **「色彩模式」** (Color Mode) will be stored in each IME's `.ini` file and exposed in the Common settings page. It governs which color set the candidate window and notify window use.

---

## 1. Color Mode Enum

**File:** `src/Define.h` — append after the `CANDWND_*` / `NOTIFYWND_*` block.

```cpp
// IME UI color mode – persisted as ColorMode = N in the .ini file
enum class IME_COLOR_MODE : int
{
    IME_COLOR_MODE_SYSTEM = 0,  // 跟隨系統模式 – follow Windows light/dark setting
    IME_COLOR_MODE_LIGHT  = 1,  // 淺色模式     – always use light theme colors
    IME_COLOR_MODE_DARK   = 2,  // 深色模式     – always use dark theme colors
    IME_COLOR_MODE_CUSTOM = 3,  // 自訂         – use the 6 user-customised colors
};
```

---

## 2. Dark Theme Color Constants (`Define.h`)

**File:** `src/Define.h`

Add a dark-theme color set for the candidate window and notify window immediately after the existing light-theme `CANDWND_*` and `NOTIFYWND_*` blocks.

```cpp
//---------------------------------------------------------------------
// Candidate Window – dark theme color set
//---------------------------------------------------------------------
#define CANDWND_DARK_BORDER_COLOR          (RGB(0x55, 0x55, 0x55))
#define CANDWND_DARK_PHRASE_COLOR          (RGB(0x4E, 0xC9, 0xB0))  // teal-green
#define CANDWND_DARK_NUM_COLOR             (RGB(0x85, 0x85, 0x85))
#define CANDWND_DARK_SELECTED_ITEM_COLOR   (RGB(0xFF, 0xFF, 0xFF))
#define CANDWND_DARK_SELECTED_BK_COLOR     (RGB(0x26, 0x5C, 0x8A))  // dark sky-blue
#define CANDWND_DARK_ITEM_COLOR            (RGB(0xDC, 0xDC, 0xDC))
#define CANDWND_DARK_ITEM_BK_COLOR         (RGB(0x1E, 0x1E, 0x1E))  // near-black

//---------------------------------------------------------------------
// Notify Window – dark theme color set
//---------------------------------------------------------------------
#define NOTIFYWND_DARK_BORDER_COLOR        (RGB(0x55, 0x55, 0x55))
#define NOTIFYWND_DARK_TEXT_COLOR          (RGB(0xDC, 0xDC, 0xDC))
#define NOTIFYWND_DARK_TEXT_BK_COLOR       (RGB(0x1E, 0x1E, 0x1E))
```

> **Note:** The existing `CANDWND_*` and `NOTIFYWND_*` constants remain unchanged and serve as the **light theme** defaults.

---

## 3. `CConfig` Changes

### 3.1 Static member (`Config.h`)

In the **private** section of `class CConfig`:

```cpp
static IME_COLOR_MODE _colorMode;      // persisted as ColorMode in .ini
static bool _colorModeKeyFound;        // set by DictionaryParser when key is present
```

In the **public** section:

```cpp
static void SetColorMode(IME_COLOR_MODE mode) { _colorMode = mode; }
static IME_COLOR_MODE GetColorMode() { return _colorMode; }
static void SetColorModeKeyFound(bool found) { _colorModeKeyFound = found; }

// Resolve effective "should use dark colors?" at runtime.
// Call this immediately before showing a candidate or notify window.
static bool GetEffectiveDarkMode();

// Push the correct color set to the presenter. Call instead of the
// individual CConfig::Get*Color() calls before showing a candidate list.
static void ApplyIMEColorSet(CUIPresenter* pPresenter);

// Resolve phrase and background colors respecting the current mode.
static COLORREF GetEffectivePhraseColor();
static COLORREF GetEffectiveItemBGColor();
```

### 3.2 Static definitions (`Config.cpp`)

Near the other static-member initialisations at the top of `Config.cpp`:

```cpp
IME_COLOR_MODE CConfig::_colorMode      = IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM;
bool           CConfig::_colorModeKeyFound = false;
```

### 3.3 `GetEffectiveDarkMode()` (`Config.cpp`)

```cpp
/*static*/
bool CConfig::GetEffectiveDarkMode()
{
    switch (_colorMode)
    {
    case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:   return false;
    case IME_COLOR_MODE::IME_COLOR_MODE_DARK:    return true;
    case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:  return false; // uses _itemColor etc.
    case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
    default:
        return CConfig::IsSystemDarkTheme();
    }
}
```

`IsSystemDarkTheme()` is already `public static` in `Config.h`; no visibility change needed.

### 3.4 `ApplyIMEColorSet()` (`Config.cpp`)

Centralises "pick the right 6 colors and push them to the presenter". Place it near the other color helpers:

```cpp
/*static*/
void CConfig::ApplyIMEColorSet(CUIPresenter* pPresenter)
{
    if (!pPresenter) return;

    if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
    {
        pPresenter->_SetCandidateTextColor(_itemColor, _itemBGColor);
        pPresenter->_SetCandidateNumberColor(_numberColor, _itemBGColor);
        pPresenter->_SetCandidateSelectedTextColor(_selectedColor, _selectedBGColor);
        pPresenter->_SetCandidateFillColor(_itemBGColor);
    }
    else if (GetEffectiveDarkMode())
    {
        pPresenter->_SetCandidateTextColor(CANDWND_DARK_ITEM_COLOR, CANDWND_DARK_ITEM_BK_COLOR);
        pPresenter->_SetCandidateNumberColor(CANDWND_DARK_NUM_COLOR, CANDWND_DARK_ITEM_BK_COLOR);
        pPresenter->_SetCandidateSelectedTextColor(CANDWND_DARK_SELECTED_ITEM_COLOR,
                                                    CANDWND_DARK_SELECTED_BK_COLOR);
        pPresenter->_SetCandidateFillColor(CANDWND_DARK_ITEM_BK_COLOR);
    }
    else
    {
        // Light preset – mirrors the current hard-coded defaults
        pPresenter->_SetCandidateTextColor(CANDWND_ITEM_COLOR, GetSysColor(COLOR_3DHIGHLIGHT));
        pPresenter->_SetCandidateNumberColor(CANDWND_NUM_COLOR, GetSysColor(COLOR_3DHIGHLIGHT));
        pPresenter->_SetCandidateSelectedTextColor(CANDWND_SELECTED_ITEM_COLOR,
                                                    CANDWND_SELECTED_BK_COLOR);
        pPresenter->_SetCandidateFillColor(GetSysColor(COLOR_3DHIGHLIGHT));
    }
}
```

### 3.5 Effective color helpers (`Config.cpp`)

Used in the phrase-mode candidate path (`CandidateHandler.cpp` line ~216):

```cpp
/*static*/
COLORREF CConfig::GetEffectivePhraseColor()
{
    if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM) return _phraseColor;
    return GetEffectiveDarkMode() ? CANDWND_DARK_PHRASE_COLOR : CANDWND_PHRASE_COLOR;
}

/*static*/
COLORREF CConfig::GetEffectiveItemBGColor()
{
    if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM) return _itemBGColor;
    return GetEffectiveDarkMode() ? CANDWND_DARK_ITEM_BK_COLOR : GetSysColor(COLOR_3DHIGHLIGHT);
}
```

---

## 4. INI Persistence

### 4.1 `WriteConfig()` — add one line (`Config.cpp`)

After the `SelectedBGItemColor` line (around line 2317), add:

```cpp
fwprintf_s(fp, L"ColorMode = %d\n", (int)_colorMode);
```

### 4.2 `DictionarySearch::ParseConfig()` (`DictionarySearch.cpp`)

In the `else if` chain that maps INI keys to `CConfig::Set*()` calls (around line 470), add after the existing color entries:

```cpp
else if (CStringRange::Compare(_locale, &keyword,
         &testKey.Set(L"ColorMode", 10)) == CSTR_EQUAL)
{
    CConfig::SetColorMode((IME_COLOR_MODE)wcstol(valueStrings.GetAt(0)->Get(), nullptr, 10));
    CConfig::SetColorModeKeyFound(true);
}
```

### 4.3 Backward-compatibility logic in `LoadConfig()` (`Config.cpp`)

There are two paths to handle:

**Path A — file exists (legacy or updated).** Reset the flag before parsing, then apply the heuristic after. Changes go inside the `if (failed || updated || _configIMEMode!=imeMode)` block:

```cpp
// ---- BEFORE calling ParseConfig ----
_colorMode = IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM; // reset before parse
_colorModeKeyFound = false;                          // detect key absence

iniTableDictionaryEngine->ParseConfig(imeMode); // existing call – unchanged

// ---- AFTER ParseConfig ----
// Backward compatibility: legacy .ini files written before ColorMode existed will
// not have triggered SetColorModeKeyFound(true).
if (!_colorModeKeyFound)
{
    // Detect whether the user ever customised the 6 colors.
    bool colorsAreDefault =
        (_itemColor      == CANDWND_ITEM_COLOR)
     && (_phraseColor    == CANDWND_PHRASE_COLOR)
     && (_numberColor    == CANDWND_NUM_COLOR)
     && (_itemBGColor    == (COLORREF)GetSysColor(COLOR_3DHIGHLIGHT))
     && (_selectedColor  == CANDWND_SELECTED_ITEM_COLOR)
     && (_selectedBGColor == CANDWND_SELECTED_BK_COLOR);

    // (1) Default colors → follow system theme.
    // (2) User-modified colors → honour them as Custom.
    _colorMode = colorsAreDefault
        ? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM
        : IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM;
}
```

**Path B — file does not exist (first run, new `.ini`).** `_colorMode` is a static shared across IME modes. If one IME was loaded before, `_colorMode` retains its previous value. The `else` branch must reset it explicitly before calling `WriteConfig()`, so the newly created file always starts with `ColorMode = 0`:

```cpp
else
{
    // Config file doesn't exist – set IME-specific defaults
    SetIMEModeDefaults(imeMode);
    _colorMode = IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM; // always start new files at SYSTEM
    if (imeMode != IME_MODE::IME_MODE_NONE)
        WriteConfig(imeMode, FALSE);
}
```

> Once `WriteConfig()` has run, the `.ini` always contains `ColorMode = 0`, so subsequent loads take Path A and `_colorModeKeyFound` is set `true` by the parser — the heuristic is skipped cleanly.

---

## 5. Settings Dialog Changes

### 5.1 Dialog resource (`src/DIME.rc` — `IDD_DIALOG_COMMON`)

**No dialog size change.** The 色彩模式 label+combo shares the existing 字型 row (Y ≈ 17–31) by pushing the font controls to the right half of that row.

Current font row (for reference):

| Control | X | Y | W | H |
| ------- | - | - | - | - |
| `LTEXT "字型:"` | 24 | 20 | 50 | 8 |
| `IDC_EDIT_FONTNAME` | 128 | 18 | 80 | 12 |
| `IDC_EDIT_FONTPOINT` | 210 | 18 | 16 | 12 |
| `IDC_BUTTON_CHOOSEFONT "▼"` | 230 | 17 | 16 | 14 |

**New font row layout** — replace the four entries above with:

```
LTEXT      "色彩模式:", IDC_STATIC_COLOR_MODE, 10, 20, 40, 8
COMBOBOX   IDC_COMBO_COLOR_MODE, 52, 17, 74, 60,
           CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP
LTEXT      "字型:", IDC_STATIC, 130, 20, 22, 8
EDITTEXT   IDC_EDIT_FONTNAME, 154, 18, 50, 12, ES_AUTOHSCROLL | ES_READONLY | NOT WS_TABSTOP
EDITTEXT   IDC_EDIT_FONTPOINT, 206, 18, 16, 12, ES_AUTOHSCROLL | ES_READONLY | NOT WS_TABSTOP
PUSHBUTTON "▼", IDC_BUTTON_CHOOSEFONT, 224, 17, 16, 14, BS_CENTER
```

Layout reasoning:

- Color-mode label ends at X = 50; combo occupies X = 52–126 (74 DU — wide enough for "跟隨系統模式" + dropdown arrow).
- "字型:" moves from X = 24 to X = 130 (right of the combo); the font-name edit compresses from W = 80 to W = 50 (still readable; the field is `ES_AUTOHSCROLL | ES_READONLY`).
- Size edit and button shift left by 4–6 DU to accommodate the narrower font-name edit. All controls stay within the existing group box (ends at X = 262).
- **No changes to the group box, input-settings group, or dialog height.**

Add the new resource IDs to `src/resource.h`:

```cpp
#define IDC_COMBO_COLOR_MODE    <next_available_id>
#define IDC_STATIC_COLOR_MODE   <next_available_id + 1>
```

> Fine-tune the exact X/W values in Visual Studio's resource editor after the initial edit — the coordinates above are a good starting point but font metrics can shift things by 2–4 DU.

### 5.2 `ParseConfig()` — populate combo and enable/disable color boxes (`Config.cpp`)

In the `initDiag == TRUE` block (around line 1067), after populating the other combos:

```cpp
// Populate 色彩模式 combobox
hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
if (initDiag)
{
    SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"跟隨系統模式"); // 0
    SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"淺色模式");     // 1
    SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"深色模式");     // 2
    SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"自訂");         // 3
}
SendMessage(hwnd, CB_SETCURSEL, (WPARAM)(int)_colorMode, 0);

// Enable the 6 color boxes only when mode is 自訂
BOOL enableColorBoxes = (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
for (size_t ci = 0; ci < _countof(colors); ci++)
    EnableWindow(GetDlgItem(hDlg, colors[ci].id), enableColorBoxes);
```

### 5.3 `WM_COMMAND` — handle `IDC_COMBO_COLOR_MODE` selection changes (`Config.cpp`)

Inside the `WM_COMMAND` / `CBN_SELCHANGE` dispatch, add a new case alongside the other `IDC_COMBO_*` cases:

```cpp
case IDC_COMBO_COLOR_MODE:
    if (HIWORD(wParam) == CBN_SELCHANGE)
    {
        hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
        IME_COLOR_MODE newMode =
            (IME_COLOR_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
        BOOL enableColors = (newMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
        for (size_t ci = 0; ci < _countof(colors); ci++)
            EnableWindow(GetDlgItem(hDlg, colors[ci].id), enableColors);
        PropSheet_Changed(GetParent(hDlg), hDlg);
        ret = TRUE;
    }
    break;
```

### 5.4 `WM_LBUTTONDOWN` — guard color-box clicks (`Config.cpp`)

The existing handler (line ~1376) does a hit-test against each color box's screen rect. A disabled `EnableWindow` prevents `WM_LBUTTONDOWN` from going to the child window, but because the dialog procedure uses manual rect hit-testing, mouse messages routed back to the parent will still match the disabled control's rect. Add a guard at the top of the loop:

```cpp
case WM_LBUTTONDOWN:
    {
        // Only allow colour-box interaction in Custom mode
        HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
        IME_COLOR_MODE curMode =
            (IME_COLOR_MODE)SendMessage(hwndCombo, CB_GETCURSEL, 0, 0);
        if (curMode != IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
            break;
        // ... existing hit-test / ChooseColor loop (unchanged) ...
    }
    break;
```

### 5.5 `PSN_APPLY` — read back `IDC_COMBO_COLOR_MODE` (`Config.cpp`)

After reading the other combo values in the `PSN_APPLY` block, add:

```cpp
hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
_colorMode = (IME_COLOR_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
```

The existing six lines that copy `colors[0..5]` back to `_itemColor` etc. remain **unchanged**. They are always persisted so that user-set colours survive mode switches (switching from 自訂 to 淺色 and back restores the previous custom colours).

### 5.6 `IDC_BUTTON_RESTOREDEFAULT` — reset color mode (`Config.cpp`)

**回復預設** must produce exactly the same UI state as a freshly created `.ini`: color mode = **跟隨系統模式**, all 6 color boxes disabled, and the 6 stored colors reset to their hard-coded light-theme defaults.

```cpp
// New code alongside the existing color reset (colors[0..5] → defaults)
hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
SendMessage(hwnd, CB_SETCURSEL, (WPARAM)(int)IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM, 0);
for (size_t ci = 0; ci < _countof(colors); ci++)
    EnableWindow(GetDlgItem(hDlg, colors[ci].id), FALSE);
```

> **Invariant:** "Default state" is defined as `ColorMode = 0` (跟隨系統模式) with the 6 colors equal to the `CANDWND_*` / `GetSysColor(COLOR_3DHIGHLIGHT)` constants. A newly created `.ini` (Path B in §4.3) writes this same state. The **回復預設** button resets the in-memory config and dialog UI to this same state before calling `WriteConfig()`.

### 5.7 Dark-theme handling for the new label

`IDC_STATIC_COLOR_MODE` is a static text control. The existing `WM_CTLCOLORSTATIC` handler in `CommonPropertyPageWndProc` already handles all statics uniformly when `pCtx->isDarkTheme` is true, so no additional code is needed.

---

## 6. Candidate Window Color Application

### 6.1 Central location

`CUIPresenter::_SetCandidateText()` (`UIPresenter.cpp`) is the single choke-point called from every candidate-display path:

| Caller | Lines (approx) | Mode |
| ------ | -------------- | ---- |
| `KeyHandler.cpp` | ~236 | Normal (incremental) |
| `KeyHandler.cpp` | ~354 | Normal (original) |
| `CandidateHandler.cpp` | ~220 | Phrase mode (make-phrase) |

It always ends with `Show()` + `_InvalidateRect()`, so applying colors at its **top** guarantees they are set before every paint. The only change needed in `KeyHandler.cpp` is **deleting** the 4 now-redundant color-setter lines at both call sites (no new code added).

### 6.2 `_SetCandidateText()` signature change (`UIPresenter.cpp`)

Add a `bool isPhraseMode = false` default parameter:

```cpp
// UIPresenter.h / UIPresenter.cpp
void _SetCandidateText(
    _In_ CDIMEArray<CCandidateListItem>* pCandidateList,
    _In_ CCandidateRange* pIndexRange,
    BOOL isAddFindKeyCode,
    UINT candWidth,
    bool isPhraseMode = false);   // NEW — default keeps all existing callers unchanged
```

At the top of the implementation, before the existing body, add:

```cpp
// Apply the theme-correct color set.
// isPhraseMode substitutes the phrase text color for the normal item text color.
CConfig::ApplyIMEColorSet(this, isPhraseMode);
```

Extend `ApplyIMEColorSet` to accept the flag:

```cpp
/*static*/
void CConfig::ApplyIMEColorSet(CUIPresenter* pPresenter, bool isPhraseMode /*= false*/)
{
    if (!pPresenter) return;

    COLORREF textColor, bgColor, selText, selBg, numColor;

    if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
    {
        textColor = isPhraseMode ? _phraseColor : _itemColor;
        bgColor   = _itemBGColor;
        selText   = _selectedColor;
        selBg     = _selectedBGColor;
        numColor  = _numberColor;
    }
    else if (GetEffectiveDarkMode())
    {
        textColor = isPhraseMode ? CANDWND_DARK_PHRASE_COLOR : CANDWND_DARK_ITEM_COLOR;
        bgColor   = CANDWND_DARK_ITEM_BK_COLOR;
        selText   = CANDWND_DARK_SELECTED_ITEM_COLOR;
        selBg     = CANDWND_DARK_SELECTED_BK_COLOR;
        numColor  = CANDWND_DARK_NUM_COLOR;
    }
    else
    {
        textColor = isPhraseMode ? CANDWND_PHRASE_COLOR : CANDWND_ITEM_COLOR;
        bgColor   = GetSysColor(COLOR_3DHIGHLIGHT);
        selText   = CANDWND_SELECTED_ITEM_COLOR;
        selBg     = CANDWND_SELECTED_BK_COLOR;
        numColor  = CANDWND_NUM_COLOR;
    }

    pPresenter->_SetCandidateTextColor(textColor, bgColor);
    pPresenter->_SetCandidateNumberColor(numColor, bgColor);
    pPresenter->_SetCandidateSelectedTextColor(selText, selBg);
    pPresenter->_SetCandidateFillColor(bgColor);
}
```

### 6.3 `CandidateHandler.cpp` — phrase-mode call site

Pass `isPhraseMode = true` to `_SetCandidateText` and **remove** the 4 preceding color-setter calls that are now redundant:

```cpp
// BEFORE (lines ~214–220):
_pUIPresenter->_SetCandidateTextColor(CConfig::GetPhraseColor(), CConfig::GetItemBGColor());
_pUIPresenter->_SetCandidateSelectedTextColor(CConfig::GetSelectedColor(), CConfig::GetSelectedBGColor());
_pUIPresenter->_SetCandidateNumberColor(CConfig::GetNumberColor(), CConfig::GetItemBGColor());
_pUIPresenter->_SetCandidateFillColor(CConfig::GetItemBGColor());
_pUIPresenter->_SetCandidateText(&candidatePhraseList, ..., FALSE, width);

// AFTER (single call):
_pUIPresenter->_SetCandidateText(&candidatePhraseList, ..., FALSE, width, /*isPhraseMode=*/true);
```

The 4 color calls in `KeyHandler.cpp` are similarly redundant once `_SetCandidateText` handles colors internally — **remove them** from both `KeyHandler.cpp` locations.

### 6.4 Border color

The candidate window border is drawn using the compile-time constant `CANDWND_BORDER_COLOR` in `CandidateWindow.cpp`. To make it theme-aware, fetch the effective border color at paint time:

```cpp
// In CandidateWindow.cpp – wherever CANDWND_BORDER_COLOR is used:
COLORREF borderColor = CConfig::GetEffectiveDarkMode()
    ? CANDWND_DARK_BORDER_COLOR : CANDWND_BORDER_COLOR;
```

---

## 7. Notify Window Color Application

### 7.1 `UIPresenter.cpp` — `ShowNotification` path

In `CUIPresenter::ShowNotification()` (around line 1491), the notify window text color is set to `CConfig::GetItemColor() / GetItemBGColor()` after `MakeNotifyWindow()`. Replace with theme-aware values:

```cpp
// BEFORE:
_SetNotifyTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor());

// AFTER:
if (CConfig::GetEffectiveDarkMode())
    _SetNotifyTextColor(NOTIFYWND_DARK_TEXT_COLOR, NOTIFYWND_DARK_TEXT_BK_COLOR);
else
    _SetNotifyTextColor(NOTIFYWND_TEXT_COLOR, NOTIFYWND_TEXT_BK_COLOR);
```

### 7.2 `NotifyWindow.cpp` — paint-time colors

`CNotifyWindow`'s paint code uses the compile-time constants `NOTIFYWND_BORDER_COLOR`, `NOTIFYWND_TEXT_COLOR`, and `NOTIFYWND_TEXT_BK_COLOR` directly. Replace each at the call site so they are evaluated at paint time (not cached at creation, so a mid-session theme change is reflected on the next repaint):

```cpp
// Border pen
COLORREF borderColor = CConfig::GetEffectiveDarkMode()
    ? NOTIFYWND_DARK_BORDER_COLOR : NOTIFYWND_BORDER_COLOR;
HPEN hPen = CreatePen(PS_SOLID, cx, borderColor);

// Text / background (WM_PAINT)
COLORREF textColor = CConfig::GetEffectiveDarkMode()
    ? NOTIFYWND_DARK_TEXT_COLOR : NOTIFYWND_TEXT_COLOR;
COLORREF bkColor = CConfig::GetEffectiveDarkMode()
    ? NOTIFYWND_DARK_TEXT_BK_COLOR : NOTIFYWND_TEXT_BK_COLOR;
```

> These calls are currently at `NotifyWindow.cpp` lines ~547–548 (SetTextColor/SetBkColor) and ~574 (CreatePen).

---

## 8. Implementation Phases

| Phase | What | Files touched |
|-------|------|---------------|
| **1** | `IME_COLOR_MODE` enum + dark-theme `#define` constants | `Define.h` |
| **2** | `_colorMode` / `_colorModeKeyFound` statics + public accessors + `GetEffectiveDarkMode()` + `ApplyIMEColorSet()` + `GetEffectivePhraseColor()` + `GetEffectiveItemBGColor()` | `Config.h`, `Config.cpp` |
| **3** | Persist `ColorMode` in `WriteConfig()` | `Config.cpp` |
| **4** | Parse `ColorMode` in `DictionarySearch::ParseConfig()`, set `_colorModeKeyFound` | `DictionarySearch.cpp` |
| **5** | Backward-compatibility heuristic in `LoadConfig()` (reset flag before parse, check after) | `Config.cpp` |
| **6** | Dialog resource: expand group box, add `IDC_COMBO_COLOR_MODE` + `IDC_STATIC_COLOR_MODE`, shift controls, expand dialog height | `DIME.rc`, `resource.h` |
| **7** | Wire dialog: `ParseConfig()` init, `WM_COMMAND` CBN_SELCHANGE, `WM_LBUTTONDOWN` guard, `PSN_APPLY` read-back, restore-default reset | `Config.cpp` |
| **8** | Add `isPhraseMode` param to `_SetCandidateText()`; call `ApplyIMEColorSet(this, isPhraseMode)` at top; remove the now-redundant 4 color-setter calls from `KeyHandler.cpp` (×2) and `CandidateHandler.cpp` (×1); pass `isPhraseMode=true` from `CandidateHandler.cpp` | `UIPresenter.h/.cpp`, `CandidateHandler.cpp`, `KeyHandler.cpp` |
| **9** | Replace candidate window border constant with runtime call | `CandidateWindow.cpp` |
| **10** | Replace notify window color constants in show-path and paint | `UIPresenter.cpp`, `NotifyWindow.cpp` |
| **11** | Test all four modes × all four IMEs × light/dark Windows setting | — |

---

## 9. Test Matrix

| ColorMode | Windows theme | Expected candidate window | Expected notify window |
|-----------|--------------|--------------------------|----------------------|
| 跟隨系統 | Light | Light preset | Light preset |
| 跟隨系統 | Dark | Dark preset | Dark preset |
| 淺色模式 | Dark | Light preset | Light preset |
| 深色模式 | Light | Dark preset | Dark preset |
| 自訂 | Either | User's 6 custom colors | Light preset (not customisable) |
| Legacy .ini, default colors | Either | System-follow (§4.3 path 1) | Follows system |
| Legacy .ini, user-modified colors | Either | Custom (§4.3 path 2) | Follows system |
| Restore Default button | — | System-follow, color boxes disabled | Follows system |

---

## 10. Noteworthy Constraints

- **No dialog changes for NotifyWindow colors.** Notify window uses only border + text + background. These are not exposed as user-customisable in the current UI and should not be added. Light and dark presets are sufficient.
- **Color boxes must stay in the dialog resource.** The existing 6 `IDC_COL_*` static windows and their click/paint handling must **not** be removed. They are simply disabled (via `EnableWindow`) when the mode is not 自訂, and the `WM_LBUTTONDOWN` handler is guarded to ignore clicks outside 自訂 mode.
- **Stored custom colors are never discarded.** `PSN_APPLY` always copies `colors[0..5]` to `_itemColor` etc. and writes them to the INI, regardless of the current mode. This ensures switching from 自訂 to another mode and back restores the user's colours.
- **`DIMESettings` compatibility.** `DIMESettings.exe` opens the same `CommonPropertyPageWndProc`, so the combo will appear there automatically with no extra work.
- **Thread safety.** All `CConfig` static members are accessed from the IME thread only (TSF is single-threaded per-process for the IME DLL). No locking is required.
- **Win7 / Win8 compatibility.** `IsSystemDarkTheme()` already falls back to a registry check on older builds. No additional compatibility work is needed for the color-resolution path.

---

## 11. Summary of New Symbols

| Symbol | Kind | Location | Purpose |
|--------|------|----------|---------|
| `IME_COLOR_MODE` | `enum class` | `Define.h` | Color mode options (0–3) |
| `CANDWND_DARK_*` | `#define` | `Define.h` | Dark-theme candidate window colors |
| `NOTIFYWND_DARK_*` | `#define` | `Define.h` | Dark-theme notify window colors |
| `CConfig::_colorMode` | `static` data | `Config.h/.cpp` | Persisted color mode |
| `CConfig::_colorModeKeyFound` | `static` data | `Config.h/.cpp` | Legacy-file detection flag |
| `CConfig::GetColorMode()` / `SetColorMode()` | `static` methods | `Config.h` | Accessor pair |
| `CConfig::SetColorModeKeyFound()` | `static` method | `Config.h` | Called by parser when key is found |
| `CConfig::GetEffectiveDarkMode()` | `static` method | `Config.h/.cpp` | Resolves actual dark/light at runtime |
| `CConfig::ApplyIMEColorSet(pPresenter, isPhraseMode)` | `static` method | `Config.h/.cpp` | Pushes all 4 theme-correct colors to `CUIPresenter`; phrase mode substitutes phrase text color |
| `IDC_COMBO_COLOR_MODE` | resource ID | `resource.h` | Dialog combobox |
| `IDC_STATIC_COLOR_MODE` | resource ID | `resource.h` | Dialog label |
| `ColorMode` | INI key | `.ini` files | Persisted integer (0–3) |

---

## 12. DIMETests – Tests to Add or Modify

This section defines the specific test cases to add or modify in the `DIMETests` project (`src/tests/`) to validate the color-mode feature. All new tests follow the existing naming conventions.

---

### 12.1 ConfigTest.cpp — New Unit Tests

Add the following tests to the `ConfigTest` class. They require no window creation and no DIME.dll, so they run cleanly in a headless CI environment.

#### UT-CM-01 · `GetEffectiveDarkMode` logic for all four modes

```cpp
TEST_METHOD(GetEffectiveDarkMode_LightMode_ReturnsFalse)
{
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
    Assert::IsFalse(CConfig::GetEffectiveDarkMode(),
        L"LIGHT mode must always return false regardless of system theme");
}

TEST_METHOD(GetEffectiveDarkMode_DarkMode_ReturnsTrue)
{
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);
    Assert::IsTrue(CConfig::GetEffectiveDarkMode(),
        L"DARK mode must always return true regardless of system theme");
}

TEST_METHOD(GetEffectiveDarkMode_CustomMode_ReturnsFalse)
{
    // CUSTOM mode uses user colours, effective dark = false (no dark preset applied)
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
    Assert::IsFalse(CConfig::GetEffectiveDarkMode(),
        L"CUSTOM mode must return false (colours come from user, not dark preset)");
}

TEST_METHOD(GetEffectiveDarkMode_SystemMode_MatchesSystemTheme)
{
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM);
    // IsSystemDarkTheme() is the ground truth; just verify GetEffectiveDarkMode agrees
    bool systemDark = CConfig::IsSystemDarkTheme();
    Assert::AreEqual(systemDark, CConfig::GetEffectiveDarkMode(),
        L"SYSTEM mode must mirror IsSystemDarkTheme()");
}
```

#### UT-CM-02 · `ColorMode` INI round-trip for all four values

```cpp
// Parametric helper (call once per mode value)
static void VerifyColorModeRoundTrip(IME_COLOR_MODE mode, IME_MODE imeMode)
{
    CConfig::SetIMEMode(imeMode);
    CConfig::SetColorMode(mode);
    CConfig::WriteConfig(nullptr);   // write to disk

    // Reset in-memory state
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
    CConfig::SetColorModeKeyFound(false);

    // Reload and verify
    CConfig::LoadConfig(imeMode);
    Assert::IsTrue(CConfig::GetColorModeKeyFound(), L"ColorMode key must be present after write");
    Assert::AreEqual(static_cast<int>(mode),
                     static_cast<int>(CConfig::GetColorMode()),
                     L"Reloaded ColorMode must match written value");
}

TEST_METHOD(ColorMode_RoundTrip_System)   { VerifyColorModeRoundTrip(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM,  IME_MODE::IME_MODE_ARRAY);    }
TEST_METHOD(ColorMode_RoundTrip_Light)    { VerifyColorModeRoundTrip(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT,   IME_MODE::IME_MODE_ARRAY);    }
TEST_METHOD(ColorMode_RoundTrip_Dark)     { VerifyColorModeRoundTrip(IME_COLOR_MODE::IME_COLOR_MODE_DARK,    IME_MODE::IME_MODE_ARRAY);    }
TEST_METHOD(ColorMode_RoundTrip_Custom)   { VerifyColorModeRoundTrip(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM,  IME_MODE::IME_MODE_ARRAY);    }
```

#### UT-CM-03 · Backward-compat heuristic — legacy INI without `ColorMode` key

```cpp
TEST_METHOD(LoadConfig_NoColorModeKey_DefaultColors_SetsSystemMode)
{
    // Arrange: write an INI that has no ColorMode line and default color values
    // (use a temp file path and inject via WritePrivateProfileString)
    // After load, ColorModeKeyFound must be false and ColorMode must be SYSTEM
    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    std::wstring path = GetConfigFilePath(IME_MODE::IME_MODE_ARRAY);

    // Overwrite with a minimal INI lacking ColorMode, keeping all color values at defaults
    WritePrivateProfileString(L"Config", L"ColorMode", nullptr, path.c_str()); // delete key

    CConfig::SetColorModeKeyFound(true);  // pre-set to ensure LoadConfig resets it
    CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);

    Assert::IsFalse(CConfig::GetColorModeKeyFound(),
        L"Key must be absent for a legacy file");
    Assert::AreEqual(static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM),
                     static_cast<int>(CConfig::GetColorMode()),
                     L"Default colors -> SYSTEM mode");
}

TEST_METHOD(LoadConfig_NoColorModeKey_ModifiedColors_SetsCustomMode)
{
    // Arrange: write an INI without ColorMode but with a non-default ItemColor
    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    std::wstring path = GetConfigFilePath(IME_MODE::IME_MODE_ARRAY);

    WritePrivateProfileString(L"Config", L"ColorMode", nullptr, path.c_str());
    // Write a non-default ItemColor so backward-compat heuristic triggers CUSTOM
    WritePrivateProfileString(L"Config", L"ItemColor", L"16711680", path.c_str()); // red

    CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);

    Assert::IsFalse(CConfig::GetColorModeKeyFound(), L"Key still absent");
    Assert::AreEqual(static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM),
                     static_cast<int>(CConfig::GetColorMode()),
                     L"Non-default colors -> CUSTOM mode");
}
```

#### UT-CM-04 · `ThemeColorConstants` values (extend existing test)

The existing test `ThemeColorConstants_LightAndDark_AreDifferent` verifies that light and dark
presets differ. Extend it or add siblings to verify each dark constant is in a "dark" luminance
range and each light constant is in a "bright" luminance range:

```cpp
TEST_METHOD(DarkColorConstants_AreInDarkRange)
{
    // All CANDWND_DARK_* background constants must be darker than 0x80 in all channels
    auto isDark = [](COLORREF c) {
        return GetRValue(c) < 0x80 && GetGValue(c) < 0x80 && GetBValue(c) < 0x80;
    };
    Assert::IsTrue(isDark(CANDWND_DARK_ITEM_BG_COLOR),     L"Item BG should be dark");
    Assert::IsTrue(isDark(CANDWND_DARK_SELECTED_COLOR),    L"Selected BG should be dark");
    Assert::IsTrue(isDark(NOTIFYWND_DARK_TEXT_BK_COLOR),   L"Notify BG should be dark");
}

TEST_METHOD(DarkColorConstants_TextColors_AreInLightRange)
{
    // Text drawn on a dark background must be light for readability
    auto isLight = [](COLORREF c) {
        return GetRValue(c) > 0x80 || GetGValue(c) > 0x80 || GetBValue(c) > 0x80;
    };
    Assert::IsTrue(isLight(CANDWND_DARK_ITEM_COLOR),       L"Item text should be light");
    Assert::IsTrue(isLight(CANDWND_DARK_PHRASE_COLOR),     L"Phrase text should be light");
    Assert::IsTrue(isLight(NOTIFYWND_DARK_TEXT_COLOR),     L"Notify text should be light");
}
```

---

### 12.2 SettingsDialogIntegrationTest.cpp — New Integration Tests

Add after the last `IT07_*` test (currently `IT07_15`). These tests load `DIME.dll`, open the
settings page, and exercise the new combo box.

#### IT-CM-01 · Combo initialises with saved mode value

```cpp
TEST_METHOD(IT_CM_01_ColorModeCombo_InitialisesWithSavedValue)
{
    // Arrange: persist DARK mode to INI, then open dialog
    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);
    CConfig::WriteConfig(nullptr);

    HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
    if (!hDimeDll) { Logger::WriteMessage(L"SKIP: DIME.dll not found\n"); return; }

    HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON),
                                  NULL, CConfig::CommonPropertyPageWndProc, 0);
    if (!hDlg) { FreeLibrary(hDimeDll); Logger::WriteMessage(L"SKIP: dialog not created\n"); return; }

    // Reload config into dialog controls
    CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
    SendMessage(hDlg, WM_INITDIALOG, 0, 0);

    // Act: read combo selection
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
    Assert::IsNotNull(hCombo, L"Combo must exist");
    int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);

    // Assert: selection index must match IME_COLOR_MODE_DARK (2)
    Assert::AreEqual(static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK), sel,
        L"Combo selection must reflect the saved DARK mode");

    DestroyWindow(hDlg);
    FreeLibrary(hDimeDll);
}
```

#### IT-CM-02 · Color boxes enabled only in CUSTOM mode

```cpp
TEST_METHOD(IT_CM_02_ColorBoxes_DisabledUnlessCustomMode)
{
    HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
    if (!hDimeDll) { Logger::WriteMessage(L"SKIP\n"); return; }

    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
    CConfig::WriteConfig(nullptr);

    HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON),
                                  NULL, CConfig::CommonPropertyPageWndProc, 0);
    if (!hDlg) { FreeLibrary(hDimeDll); return; }

    SendMessage(hDlg, WM_INITDIALOG, 0, 0);

    // In LIGHT mode all 6 color boxes must be disabled
    const int colorBoxIds[] = {
        IDC_COLOR_CANDIDATE_TEXT, IDC_COLOR_PHRASE_TEXT,
        IDC_COLOR_NUMBER_TEXT,    IDC_COLOR_FILL,
        IDC_COLOR_SELECTED_TEXT,  IDC_COLOR_SELECTED_FILL
    };
    for (int id : colorBoxIds)
    {
        HWND hBox = GetDlgItem(hDlg, id);
        if (hBox)
            Assert::IsFalse(IsWindowEnabled(hBox) != 0,
                L"Color box must be disabled in LIGHT mode");
    }

    // Switch combo to CUSTOM and verify boxes become enabled
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
    if (hCombo)
    {
        SendMessage(hCombo, CB_SETCURSEL,
            static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM), 0);
        // Simulate CBN_SELCHANGE notification
        SendMessage(hDlg, WM_COMMAND,
            MAKEWPARAM(IDC_COMBO_COLOR_MODE, CBN_SELCHANGE),
            (LPARAM)hCombo);

        for (int id : colorBoxIds)
        {
            HWND hBox = GetDlgItem(hDlg, id);
            if (hBox)
                Assert::IsTrue(IsWindowEnabled(hBox) != 0,
                    L"Color box must be enabled in CUSTOM mode");
        }
    }

    DestroyWindow(hDlg);
    FreeLibrary(hDimeDll);
}
```

#### IT-CM-03 · PSN\_APPLY persists the selected mode

```cpp
TEST_METHOD(IT_CM_03_PSN_Apply_PersistsColorMode)
{
    HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
    if (!hDimeDll) { Logger::WriteMessage(L"SKIP\n"); return; }

    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM);
    CConfig::WriteConfig(nullptr);

    HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON),
                                  NULL, CConfig::CommonPropertyPageWndProc, 0);
    if (!hDlg) { FreeLibrary(hDimeDll); return; }
    SendMessage(hDlg, WM_INITDIALOG, 0, 0);

    // Select DARK mode in combo
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
    if (hCombo)
    {
        SendMessage(hCombo, CB_SETCURSEL,
            static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK), 0);
        SendMessage(hDlg, WM_COMMAND,
            MAKEWPARAM(IDC_COMBO_COLOR_MODE, CBN_SELCHANGE), (LPARAM)hCombo);
    }

    // Simulate PSN_APPLY
    PSHNOTIFY psn = {};
    psn.hdr.code    = PSN_APPLY;
    psn.hdr.hwndFrom = hDlg;
    psn.hdr.idFrom   = 0;
    SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psn);

    // Reload from disk and verify
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM); // reset
    CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);

    Assert::AreEqual(static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK),
                     static_cast<int>(CConfig::GetColorMode()),
                     L"DARK mode must be persisted after PSN_APPLY");

    DestroyWindow(hDlg);
    FreeLibrary(hDimeDll);
}
```

#### IT-CM-04 · Restore Default resets combo to SYSTEM and disables color boxes

```cpp
TEST_METHOD(IT_CM_04_RestoreDefault_ResetsComboToSystem)
{
    HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
    if (!hDimeDll) { Logger::WriteMessage(L"SKIP\n"); return; }

    CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
    CConfig::WriteConfig(nullptr);

    HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON),
                                  NULL, CConfig::CommonPropertyPageWndProc, 0);
    if (!hDlg) { FreeLibrary(hDimeDll); return; }
    SendMessage(hDlg, WM_INITDIALOG, 0, 0);

    // Click Restore Default
    HWND hBtn = GetDlgItem(hDlg, IDC_BUTTON_RESTORE_DEFAULT);
    if (hBtn)
        SendMessage(hDlg, WM_COMMAND,
            MAKEWPARAM(IDC_BUTTON_RESTORE_DEFAULT, BN_CLICKED), (LPARAM)hBtn);

    // Verify combo is now SYSTEM (index 0)
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
    if (hCombo)
    {
        int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
        Assert::AreEqual(
            static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM), sel,
            L"After Restore Default, combo must show SYSTEM (index 0)");
    }

    // Verify all 6 color boxes are disabled
    const int colorBoxIds[] = {
        IDC_COLOR_CANDIDATE_TEXT, IDC_COLOR_PHRASE_TEXT,
        IDC_COLOR_NUMBER_TEXT,    IDC_COLOR_FILL,
        IDC_COLOR_SELECTED_TEXT,  IDC_COLOR_SELECTED_FILL
    };
    for (int id : colorBoxIds)
    {
        HWND hBox = GetDlgItem(hDlg, id);
        if (hBox)
            Assert::IsFalse(IsWindowEnabled(hBox) != 0,
                L"Color boxes must be disabled after Restore Default");
    }

    DestroyWindow(hDlg);
    FreeLibrary(hDimeDll);
}
```

---

### 12.3 CandidateWindowIntegrationTest.cpp — New Color Tests

Add after the last `IT03_*` test. These tests verify that `CCandidateWindow` stores and returns
the correct colors after `ApplyIMEColorSet` has been called via `_SetCandidateText`.

> **Note:** `CCandidateWindow` does not expose its colors through a public getter. Tests verify
> behavior indirectly by checking that the window object is created without crashing and that
> color-aware paint does not fault. Direct color verification requires adding thin `_GetItemColor()`
> / `_GetSelectedBgColor()` accessors to `CCandidateWindow` (guarded with `#ifdef DIME_UNIT_TESTING`).

#### IT-CM-10 · Light-mode colors set on candidate window

```cpp
TEST_METHOD(IT_CM_10_CandidateWindow_LightMode_ColorsApplied)
{
    Logger::WriteMessage("Test: IT_CM_10 - Light mode colors applied\n");

    // Arrange
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
    CCandidateRange indexRange;
    CCandidateWindow* pWnd = new (std::nothrow) CCandidateWindow(
        TestCandidateCallback, nullptr, &indexRange, FALSE);
    Assert::IsNotNull(pWnd);

    // Act: set colors via public setter (light preset)
    pWnd->_SetTextColor(CANDWND_ITEM_COLOR, CANDWND_SELECTED_COLOR);
    pWnd->_SetFillColor(CANDWND_FILL_COLOR);

    // Assert: colors must not be zero (default unset state)
    Assert::AreNotEqual((COLORREF)0, pWnd->_GetItemColor(),
        L"Item color must be set to light preset");

    delete pWnd;
}
```

#### IT-CM-11 · Dark-mode colors set on candidate window

```cpp
TEST_METHOD(IT_CM_11_CandidateWindow_DarkMode_ColorsApplied)
{
    Logger::WriteMessage("Test: IT_CM_11 - Dark mode colors applied\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);
    CCandidateRange indexRange;
    CCandidateWindow* pWnd = new (std::nothrow) CCandidateWindow(
        TestCandidateCallback, nullptr, &indexRange, FALSE);
    Assert::IsNotNull(pWnd);

    pWnd->_SetTextColor(CANDWND_DARK_ITEM_COLOR, CANDWND_DARK_SELECTED_COLOR);
    pWnd->_SetFillColor(CANDWND_DARK_ITEM_BG_COLOR);

    Assert::AreEqual(CANDWND_DARK_ITEM_COLOR, pWnd->_GetItemColor(),
        L"Item color must be set to dark preset");
    Assert::AreNotEqual(CANDWND_ITEM_COLOR, pWnd->_GetItemColor(),
        L"Dark item color must differ from light item color");

    delete pWnd;
}
```

#### IT-CM-12 · Phrase mode uses phrase color, not item color

```cpp
TEST_METHOD(IT_CM_12_CandidateWindow_PhraseMode_UsesPhraseColor)
{
    Logger::WriteMessage("Test: IT_CM_12 - Phrase mode uses phrase color\n");

    // For light mode the phrase color is CANDWND_PHRASE_COLOR (green),
    // which differs from CANDWND_ITEM_COLOR (black).
    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
    CCandidateRange indexRange;
    CCandidateWindow* pWnd = new (std::nothrow) CCandidateWindow(
        TestCandidateCallback, nullptr, &indexRange, FALSE);
    Assert::IsNotNull(pWnd);

    // Simulate what ApplyIMEColorSet does for phrase mode
    pWnd->_SetTextColor(CANDWND_PHRASE_COLOR, CANDWND_SELECTED_COLOR);

    Assert::AreEqual(CANDWND_PHRASE_COLOR, pWnd->_GetItemColor(),
        L"Phrase mode must use phrase color (green), not item color (black)");
    Assert::AreNotEqual(CANDWND_ITEM_COLOR, pWnd->_GetItemColor(),
        L"Phrase color must differ from normal item color");

    delete pWnd;
}
```

#### IT-CM-13 · Border color reflects effective dark mode

```cpp
TEST_METHOD(IT_CM_13_CandidateWindow_DarkMode_BorderColorIsDark)
{
    Logger::WriteMessage("Test: IT_CM_13 - Dark border color\n");

    // Verify constants differ (paint code chooses based on GetEffectiveDarkMode)
    Assert::AreNotEqual(CANDWND_DARK_BORDER_COLOR, CANDWND_BORDER_COLOR,
        L"Dark and light border colors must differ");
    // Verify dark border is actually darker than light border
    Assert::IsTrue(
        GetRValue(CANDWND_DARK_BORDER_COLOR) < GetRValue(CANDWND_BORDER_COLOR) ||
        GetGValue(CANDWND_DARK_BORDER_COLOR) < GetGValue(CANDWND_BORDER_COLOR) ||
        GetBValue(CANDWND_DARK_BORDER_COLOR) < GetBValue(CANDWND_BORDER_COLOR),
        L"Dark border color must be darker than light border color");
}
```

---

### 12.4 NotificationWindowIntegrationTest.cpp — Modified Tests

Add new tests to the `NotificationWindowIntegrationTest` class that verify dark-theme color
selection. Follow the existing pattern: create a `CNotifyWindow`, call `ShowNotification` or
set colors directly, and verify the stored values.

#### IT-CM-20 · Notify window uses dark colors in DARK mode

```cpp
TEST_METHOD(IT_CM_20_NotifyWindow_DarkMode_UsesPresetColors)
{
    Logger::WriteMessage("Test: IT_CM_20 - Notify window dark colors\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);

    CNotifyWindow* pWnd = new (std::nothrow) CNotifyWindow(nullptr, 0, 0);
    Assert::IsNotNull(pWnd);

    // Simulate what ShowNotification does for dark mode
    pWnd->_SetTextColor(NOTIFYWND_DARK_TEXT_COLOR);
    pWnd->_SetTextBkColor(NOTIFYWND_DARK_TEXT_BK_COLOR);

    Assert::AreEqual(NOTIFYWND_DARK_TEXT_COLOR,    pWnd->_GetTextColor(),
        L"Text color must be dark preset");
    Assert::AreEqual(NOTIFYWND_DARK_TEXT_BK_COLOR, pWnd->_GetTextBkColor(),
        L"Background must be dark preset");

    delete pWnd;
}
```

#### IT-CM-21 · Notify window uses light colors in LIGHT mode

```cpp
TEST_METHOD(IT_CM_21_NotifyWindow_LightMode_UsesLightColors)
{
    Logger::WriteMessage("Test: IT_CM_21 - Notify window light colors\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);

    CNotifyWindow* pWnd = new (std::nothrow) CNotifyWindow(nullptr, 0, 0);
    Assert::IsNotNull(pWnd);

    pWnd->_SetTextColor(NOTIFYWND_TEXT_COLOR);
    pWnd->_SetTextBkColor(GetSysColor(COLOR_3DHIGHLIGHT));

    // Verify the light colors differ from the dark ones
    Assert::AreNotEqual(NOTIFYWND_DARK_TEXT_COLOR,    pWnd->_GetTextColor(),
        L"Light text color must differ from dark preset");
    Assert::AreNotEqual(NOTIFYWND_DARK_TEXT_BK_COLOR, pWnd->_GetTextBkColor(),
        L"Light background must differ from dark preset");

    delete pWnd;
}
```

#### IT-CM-22 · Notify-window border constant verification

```cpp
TEST_METHOD(IT_CM_22_NotifyWindow_BorderConstants_Differ)
{
    Logger::WriteMessage("Test: IT_CM_22 - Notify border constants\n");

    Assert::AreNotEqual(NOTIFYWND_DARK_BORDER_COLOR, NOTIFYWND_BORDER_COLOR,
        L"Dark and light notify border colors must differ");
}
```

---

### 12.5 UIPresenterIntegrationTest.cpp — New Color-Dispatch Tests

Add to the `UIPresenterIntegrationTest` class (IT-06 group). These tests exercise
`_SetCandidateText` and verify that `ApplyIMEColorSet` has dispatched the expected colors.

#### IT-CM-30 · `_SetCandidateText` applies light colors for normal candidates

```cpp
TEST_METHOD(IT_CM_30_SetCandidateText_LightMode_SetsLightColors)
{
    Logger::WriteMessage("Test: IT_CM_30\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);

    // Create a minimal UIPresenter and call _SetCandidateText with an empty list.
    // Verify the presenter's candidate window (if created) has the light preset colors.
    // If window creation fails in CI, just verify no crash.
    StubUIPresenter presenter;
    CCandidateRange range;
    CPtrArray<CStringRange> list;

    // Should not crash; colors applied internally
    presenter._SetCandidateText(&list, &range, FALSE, 200, /*isPhraseMode=*/false);
    Assert::IsTrue(true, L"_SetCandidateText must not crash in light mode");
}
```

#### IT-CM-31 · `_SetCandidateText` with `isPhraseMode=true` dispatches phrase color

```cpp
TEST_METHOD(IT_CM_31_SetCandidateText_PhraseMode_DispatchesPhraseColor)
{
    Logger::WriteMessage("Test: IT_CM_31\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);

    StubUIPresenter presenter;
    CCandidateRange range;
    CPtrArray<CStringRange> list;

    // isPhraseMode = true — must pick CANDWND_PHRASE_COLOR, not CANDWND_ITEM_COLOR
    presenter._SetCandidateText(&list, &range, FALSE, 200, /*isPhraseMode=*/true);

    // If the presenter exposes a color getter under DIME_UNIT_TESTING, assert:
    // Assert::AreEqual(CANDWND_PHRASE_COLOR, presenter._GetLastItemColor());
    Assert::IsTrue(true, L"Phrase mode dispatch must not crash");
}
```

#### IT-CM-32 · `_SetCandidateText` in DARK mode dispatches dark colors

```cpp
TEST_METHOD(IT_CM_32_SetCandidateText_DarkMode_SetsDarkColors)
{
    Logger::WriteMessage("Test: IT_CM_32\n");

    CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);

    StubUIPresenter presenter;
    CCandidateRange range;
    CPtrArray<CStringRange> list;

    presenter._SetCandidateText(&list, &range, FALSE, 200, /*isPhraseMode=*/false);
    Assert::IsTrue(true, L"_SetCandidateText must not crash in dark mode");
}
```

---

### 12.6 Test Infrastructure Notes

| Concern | Guidance |
|---------|----------|
| **Accessor guards** | Methods like `_GetItemColor()` on `CCandidateWindow` and `_GetLastItemColor()` on `CUIPresenter` should be added inside `#ifdef DIME_UNIT_TESTING` blocks so they are compiled only by the test project. |
| **State isolation** | Each test that changes `CConfig::_colorMode` must restore it in a teardown helper or use `TEST_METHOD_CLEANUP` to call `CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM)` so subsequent tests are not affected. |
| **Dialog tests require DIME.dll** | IT-CM-01 through IT-CM-04 load `DIME.dll` at runtime. They silently skip if the DLL is absent (CI environment without a built DLL). The same pattern is used by existing IT07\_\* tests. |
| **No new test projects** | All tests above slot into the existing `DIMETests` project. No new `.vcxproj` or solution entries are required. |
| **Test ID registry** | Next free unit-test method name prefix in `ConfigTest`: `UT-CM-0x`. Next free integration-test prefix in `SettingsDialogIntegrationTest`: `IT_CM_0x`. Next free in `CandidateWindowIntegrationTest`: `IT_CM_1x`. Next free in `NotificationWindowIntegrationTest`: `IT_CM_2x`. Next free in `UIPresenterIntegrationTest`: `IT_CM_3x`. |

---

### 12.7 Implementation Status (2026-03-06)

All tests in §§12.1–12.4 are implemented and passing. §12.5 (UIPresenter IT-CM-30 to IT-CM-32) and the backward-compatibility tests in §12.1 UT-CM-03 are **not yet implemented** (stubs only).

#### Deviations from plan

| Section | Deviation |
|---------|-----------|
| **UT-CM-02 placement** | Tests were initially added to `CustomTableValidationUnitTest` (wrong class). `LoadConfig` has a timestamp + mode guard (`_configIMEMode != imeMode`) that silently skips re-parsing when called in the same second as `WriteConfig`. Moving the four round-trip tests to `ConfigTest` exposed a second issue: `_configIMEMode` was sticky across tests. Fixed by adding a DAYI config "ping" to `ConfigTest::TEST_METHOD_INITIALIZE` — write a minimal DAYI config, load it (sets `_configIMEMode = DAYI`), delete it — so every subsequent `LoadConfig(ARRAY)` call always re-parses due to the mode mismatch. |
| **UT-CM-03 (backward-compat)** | The two `LoadConfig_NoColorModeKey_*` tests described in the plan were **not implemented**. Only the accessor-pair test `ColorModeKeyFound_SetAndGet_RoundTrips` is present. |
| **IT-CM-13 / IT-CM-22 luminance direction** | The plan implied dark borders should be *darker* than light borders. In practice `CANDWND_BORDER_COLOR = RGB(0,0,0)` (pure black) and `CANDWND_DARK_BORDER_COLOR = RGB(80,80,80)` (gray). Dark-theme borders must be *lighter* to remain visible against dark backgrounds. The assertion was corrected to `darkLum > lightLum`. |
| **IT-CM-30 to IT-CM-32 (UIPresenter)** | These tests depend on `StubUIPresenter` and `_SetCandidateText` accepting an `isPhraseMode` parameter that is not yet wired. They are **not implemented**. |

#### Actual test methods added

| ID | File | Class | Count |
|----|------|-------|-------|
| UT-CM-01 | `ConfigTest.cpp` | `CustomTableValidationUnitTest` | 4 |
| UT-CM-02 | `ConfigTest.cpp` | `ConfigTest` | 4 |
| UT-CM-03 (accessor only) | `ConfigTest.cpp` | `CustomTableValidationUnitTest` | 1 |
| UT-CM-04 | `ConfigTest.cpp` | `CustomTableValidationUnitTest` | 3 |
| IT-CM-01 to IT-CM-04 | `SettingsDialogIntegrationTest.cpp` | `SettingsDialogIntegrationTest` | 4 |
| IT-CM-10 to IT-CM-13 | `CandidateWindowIntegrationTest.cpp` | `CandidateWindowIntegrationTest` | 4 |
| IT-CM-20 to IT-CM-22 | `NotificationWindowIntegrationTest.cpp` | `NotificationWindowIntegrationTest` | 3 |
| **Total** | | | **23** |

---

## Phase 13 — Per-Theme Color Customization

### 13.1 Rationale

The Phase 10 dialog disables all six color boxes when the user picks **Light** or **Dark** mode and enables them only for **Custom** mode. This forces users who want a slightly different shade of dark background to use the **Custom** mode — losing the semantic "follow the OS" behavior of **Dark** and the guaranteed light rendering of **Light**.

**New goal**: keep the color boxes *enabled* for **Light** and **Dark** (and **Custom**), and persist per-theme palettes to the INI so each mode remembers its own customizations. **System** mode remains purely auto and color boxes stay disabled.

---

### 13.2 Color Mode Enum — No Change

`IME_COLOR_MODE` in `Define.h` is unchanged:

| Value | Int | Meaning after Phase 13 |
|-------|-----|------------------------|
| `IME_COLOR_MODE_SYSTEM` | 0 | Auto; uses light or dark palette based on OS. Color boxes disabled in dialog. |
| `IME_COLOR_MODE_LIGHT` | 1 | Always light. User can customize the light palette. |
| `IME_COLOR_MODE_DARK` | 2 | Always dark. User can customize the dark palette. |
| `IME_COLOR_MODE_CUSTOM` | 3 | Retained for backward compatibility (legacy single palette). |

#### SYSTEM mode — OS version behavior and combo visibility

`CConfig::IsSystemDarkTheme()` already handles all Windows versions correctly at the rendering level:

| Windows version | Detection method | `IsSystemDarkTheme()` result |
| --------------- | ---------------- | ---------------------------- |
| Build ≥ 17763 (Win10 1809 / Win11) | `ShouldAppsUseDarkMode()` ordinal 132 in `uxtheme.dll` | Follows actual OS light/dark setting |
| Build < 17763 (Win7 / Win8 / pre-1809 Win10) | Registry fallback: `AppsUseLightTheme` key absent → default `1` → `false` | Always **light** |

However, showing the SYSTEM item in the combo on pre-1809 Windows is **meaningless from the user's perspective**: they can pick it, but it behaves identically to LIGHT — they cannot use it to switch to dark at all. It is misleading UI.

**Decision: remove the SYSTEM combo item at runtime on pre-1809 Windows (build < 17763).** See §13.9 for the implementation detail (combo item data pattern). Internally, `GetEffectiveDarkMode()` and `IsSystemDarkTheme()` are unchanged — the rendering path already returns light on old Windows. Only the dialog combo is affected.

If an old INI has `ColorMode = 0` (SYSTEM) and the user is on pre-1809 Windows, treat it as LIGHT when populating the combo (select the LIGHT item). On `PSN_APPLY`, save as `IME_COLOR_MODE_LIGHT`. The user loses nothing — the behavior was already identical to light.

---

### 13.3 CConfig — New Per-Theme Palette Fields

Add 12 new static fields (6 light + 6 dark) alongside the existing 6 CUSTOM fields:

```cpp
// Config.h  (new static members — same section as existing _itemColor etc.)
static COLORREF _lightItemColor;        // default: CANDWND_ITEM_COLOR
static COLORREF _lightPhraseColor;      // default: CANDWND_PHRASE_COLOR
static COLORREF _lightNumberColor;      // default: CANDWND_NUM_COLOR
static COLORREF _lightItemBGColor;      // default: GetSysColor(COLOR_3DHIGHLIGHT)
static COLORREF _lightSelectedColor;    // default: CANDWND_SELECTED_ITEM_COLOR
static COLORREF _lightSelectedBGColor;  // default: CANDWND_SELECTED_BK_COLOR

static COLORREF _darkItemColor;         // default: CANDWND_DARK_ITEM_COLOR
static COLORREF _darkPhraseColor;       // default: CANDWND_DARK_PHRASE_COLOR
static COLORREF _darkNumberColor;       // default: CANDWND_DARK_NUM_COLOR
static COLORREF _darkItemBGColor;       // default: CANDWND_DARK_ITEM_BG_COLOR
static COLORREF _darkSelectedColor;     // default: CANDWND_DARK_SELECTED_COLOR
static COLORREF _darkSelectedBGColor;   // default: CANDWND_DARK_SELECTED_BG_COLOR
```

Add getters/setters using the same macro pattern as the existing color accessors. Dark defaults come from `Define.h` constants; light defaults match the compile-time light defaults already used in `SetColorInPresenter`.

---

### 13.4 INI Key Scheme

Three parallel sets of keys are stored in the INI under the `[Settings]` section:

| Purpose | INI key | CConfig field | Default |
|---------|---------|---------------|---------|
| Light — item text | `LightItemColor` | `_lightItemColor` | `CANDWND_ITEM_COLOR` |
| Light — phrase text | `LightPhraseColor` | `_lightPhraseColor` | `CANDWND_PHRASE_COLOR` |
| Light — number text | `LightNumberColor` | `_lightNumberColor` | `CANDWND_NUM_COLOR` |
| Light — item bg | `LightItemBGColor` | `_lightItemBGColor` | `GetSysColor(COLOR_3DHIGHLIGHT)` |
| Light — selected text | `LightSelectedItemColor` | `_lightSelectedColor` | `CANDWND_SELECTED_ITEM_COLOR` |
| Light — selected bg | `LightSelectedBGItemColor` | `_lightSelectedBGColor` | `CANDWND_SELECTED_BK_COLOR` |
| Dark — item text | `DarkItemColor` | `_darkItemColor` | `CANDWND_DARK_ITEM_COLOR` |
| Dark — phrase text | `DarkPhraseColor` | `_darkPhraseColor` | `CANDWND_DARK_PHRASE_COLOR` |
| Dark — number text | `DarkNumberColor` | `_darkNumberColor` | `CANDWND_DARK_NUM_COLOR` |
| Dark — item bg | `DarkItemBGColor` | `_darkItemBGColor` | `CANDWND_DARK_ITEM_BG_COLOR` |
| Dark — selected text | `DarkSelectedItemColor` | `_darkSelectedColor` | `CANDWND_DARK_SELECTED_COLOR` |
| Dark — selected bg | `DarkSelectedBGItemColor` | `_darkSelectedBGColor` | `CANDWND_DARK_SELECTED_BG_COLOR` |
| Custom (unchanged) | `ItemColor` | `_itemColor` | `CANDWND_ITEM_COLOR` |
| Custom (unchanged) | `PhraseColor` | `_phraseColor` | `CANDWND_PHRASE_COLOR` |
| Custom (unchanged) | `NumberColor` | `_numberColor` | `CANDWND_NUM_COLOR` |
| Custom (unchanged) | `ItemBGColor` | `_itemBGColor` | `GetSysColor(COLOR_3DHIGHLIGHT)` |
| Custom (unchanged) | `SelectedItemColor` | `_selectedColor` | `CANDWND_SELECTED_ITEM_COLOR` |
| Custom (unchanged) | `SelectedBGItemColor` | `_selectedBGColor` | `CANDWND_SELECTED_BK_COLOR` |

All 18 keys are written on every `WriteConfig` call (unconditional, same as the current 6). Keys absent in an older INI file fall back to the listed defaults when parsed.

---

### 13.5 `WriteConfig` Changes (`Config.cpp`)

After the existing `ColorMode = %d` write, append the 12 new keys:

```cpp
// Already written (unchanged):
fwprintf_s(fp, L"ColorMode = %d\n", static_cast<int>(_colorMode));
fwprintf_s(fp, L"ItemColor = 0x%06X\n",           _itemColor);
fwprintf_s(fp, L"PhraseColor = 0x%06X\n",         _phraseColor);
fwprintf_s(fp, L"NumberColor = 0x%06X\n",          _numberColor);
fwprintf_s(fp, L"ItemBGColor = 0x%06X\n",          _itemBGColor);
fwprintf_s(fp, L"SelectedItemColor = 0x%06X\n",   _selectedColor);
fwprintf_s(fp, L"SelectedBGItemColor = 0x%06X\n", _selectedBGColor);

// New — light palette:
fwprintf_s(fp, L"LightItemColor = 0x%06X\n",           _lightItemColor);
fwprintf_s(fp, L"LightPhraseColor = 0x%06X\n",         _lightPhraseColor);
fwprintf_s(fp, L"LightNumberColor = 0x%06X\n",          _lightNumberColor);
fwprintf_s(fp, L"LightItemBGColor = 0x%06X\n",          _lightItemBGColor);
fwprintf_s(fp, L"LightSelectedItemColor = 0x%06X\n",   _lightSelectedColor);
fwprintf_s(fp, L"LightSelectedBGItemColor = 0x%06X\n", _lightSelectedBGColor);

// New — dark palette:
fwprintf_s(fp, L"DarkItemColor = 0x%06X\n",           _darkItemColor);
fwprintf_s(fp, L"DarkPhraseColor = 0x%06X\n",         _darkPhraseColor);
fwprintf_s(fp, L"DarkNumberColor = 0x%06X\n",          _darkNumberColor);
fwprintf_s(fp, L"DarkItemBGColor = 0x%06X\n",          _darkItemBGColor);
fwprintf_s(fp, L"DarkSelectedItemColor = 0x%06X\n",   _darkSelectedColor);
fwprintf_s(fp, L"DarkSelectedBGItemColor = 0x%06X\n", _darkSelectedBGColor);
```

---

### 13.6 `LoadConfig` / `ParseConfig` Changes

`TableDictionaryEngine::ParseConfig` (INI dictionary engine) already handles `ItemColor` etc. The 12 new keys follow the same pattern. In `ParseConfig`, add 12 new `else if` cases for each key:

```text
LightItemColor           → CConfig::_lightItemColor
LightPhraseColor         → CConfig::_lightPhraseColor
LightNumberColor         → CConfig::_lightNumberColor
LightItemBGColor         → CConfig::_lightItemBGColor
LightSelectedItemColor   → CConfig::_lightSelectedColor
LightSelectedBGItemColor → CConfig::_lightSelectedBGColor
DarkItemColor            → CConfig::_darkItemColor
... (same pattern for remaining 6 dark keys)
```

In `LoadConfig`, the `_colorModeKeyFound` sentinel flag pattern is already set. No structural changes needed; the new keys parse like any other optional color key.

---

### 13.7 `SetColorInPresenter` Changes (`Config.cpp`)

Replace the hardcoded constant branches with the new palette fields:

```cpp
// Before (Phase 10):
else if (dark)
{
    pPresenter->_SetCandidateTextColor(CANDWND_DARK_ITEM_COLOR, CANDWND_DARK_ITEM_BG_COLOR);
    // ...
}
else  // light
{
    COLORREF bgColor = GetSysColor(COLOR_3DHIGHLIGHT);
    pPresenter->_SetCandidateTextColor(CANDWND_ITEM_COLOR, bgColor);
    // ...
}

// After (Phase 13):
else if (dark)
{
    COLORREF textColor = isPhraseMode ? _darkPhraseColor : _darkItemColor;
    pPresenter->_SetCandidateTextColor(textColor, _darkItemBGColor);
    pPresenter->_SetCandidateNumberColor(_darkNumberColor, _darkItemBGColor);
    pPresenter->_SetCandidateSelectedTextColor(_darkSelectedColor, _darkSelectedBGColor);
    pPresenter->_SetCandidateFillColor(_darkItemBGColor);
}
else  // light
{
    COLORREF textColor = isPhraseMode ? _lightPhraseColor : _lightItemColor;
    pPresenter->_SetCandidateTextColor(textColor, _lightItemBGColor);
    pPresenter->_SetCandidateNumberColor(_lightNumberColor, _lightItemBGColor);
    pPresenter->_SetCandidateSelectedTextColor(_lightSelectedColor, _lightSelectedBGColor);
    pPresenter->_SetCandidateFillColor(_lightItemBGColor);
}
```

The `CUSTOM` branch (first `if`) is unchanged.

---

### 13.8 Notify Window — `SetColorInNotifyWindow` (analogous function)

The same pattern applies for the notify window color dispatch (wherever `NOTIFYWND_*` constants are currently hardcoded). Replace with `_lightXxx` / `_darkXxx` fields. If notify window uses a subset of the 6 colors (text color + bg + border), only those fields are used; add matching `_lightNotifyXxx` / `_darkNotifyXxx` fields if the notify window uses a distinct palette, OR reuse the candidate palette fields if they are shared.

> **Decision point for implementation**: Check whether `CConfig::SetColorInNotifyWindow` (or equivalent) uses the same 6 fields or separate ones. If separate notify-window-specific fields exist, add `LightNotifyTextColor` etc. INI keys. If shared, no extra fields are needed.

---

### 13.9 Settings Dialog — `CommonPropertyPageWndProc` Changes

#### Dialog-local state

Introduce a local struct holding all 3 palettes for the duration of the dialog session:

```cpp
struct DialogPalette {
    COLORREF colors[6]; // [BG, FR, NUM, PHRASE, SEL_BG, SEL_FR]
};
// Stored in PropSheetThemeData or as static locals:
DialogPalette g_lightPalette, g_darkPalette, g_customPalette;
```

Map indices to controls:

| Index | Control | CConfig getter (light) | CConfig getter (dark) | CConfig getter (custom) |
|-------|---------|------------------------|-----------------------|------------------------|
| 0 | `IDC_COL_BG` | `GetLightItemBGColor()` | `GetDarkItemBGColor()` | `GetItemBGColor()` |
| 1 | `IDC_COL_FR` | `GetLightItemColor()` | `GetDarkItemColor()` | `GetItemColor()` |
| 2 | `IDC_COL_NU` | `GetLightNumberColor()` | `GetDarkNumberColor()` | `GetNumberColor()` |
| 3 | `IDC_COL_PHRASE` | `GetLightPhraseColor()` | `GetDarkPhraseColor()` | `GetPhraseColor()` |
| 4 | `IDC_COL_SEBG` | `GetLightSelectedBGColor()` | `GetDarkSelectedBGColor()` | `GetSelectedBGColor()` |
| 5 | `IDC_COL_SEFR` | `GetLightSelectedColor()` | `GetDarkSelectedColor()` | `GetSelectedColor()` |

#### `WM_INITDIALOG`

1. Load all 3 palettes from `CConfig` into `g_lightPalette`, `g_darkPalette`, `g_customPalette`.
2. Populate the combo using `ComboBox_SetItemData` so that each item carries its `IME_COLOR_MODE` enum value. This avoids fragile index arithmetic when items are conditionally removed:

   ```cpp
   // Always add LIGHT, DARK, CUSTOM
   // Add SYSTEM only when the OS supports dark mode detection
   HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
   bool systemSupported = (OsBuildNumber() >= 17763);
   if (systemSupported) {
       int i = ComboBox_AddString(hCombo, L"跟隨系統");   // Follow System
       ComboBox_SetItemData(hCombo, i, (LPARAM)IME_COLOR_MODE_SYSTEM);
   }
   int i = ComboBox_AddString(hCombo, L"淺色");           // Light
   ComboBox_SetItemData(hCombo, i, (LPARAM)IME_COLOR_MODE_LIGHT);
   i = ComboBox_AddString(hCombo, L"深色");               // Dark
   ComboBox_SetItemData(hCombo, i, (LPARAM)IME_COLOR_MODE_DARK);
   i = ComboBox_AddString(hCombo, L"自訂");               // Custom
   ComboBox_SetItemData(hCombo, i, (LPARAM)IME_COLOR_MODE_CUSTOM);
   ```

3. Select the saved mode. If `_colorMode == SYSTEM` but SYSTEM was not added (old Windows), select LIGHT instead:

   ```cpp
   IME_COLOR_MODE savedMode = CConfig::GetColorMode();
   if (savedMode == IME_COLOR_MODE_SYSTEM && !systemSupported)
       savedMode = IME_COLOR_MODE_LIGHT;
   // Scan item data to find the matching item index
   SelectComboByData(hCombo, (LPARAM)savedMode);
   ```

4. Call `UpdateColorBoxesForMode(hDlg, savedMode)`.

#### Helper: `UpdateColorBoxesForMode(HWND hDlg, IME_COLOR_MODE mode)`

```text
BOOL enable = (mode != IME_COLOR_MODE_SYSTEM);
Enable all 6 IDC_COL_* boxes based on `enable`.
If enable:
    Get the palette for `mode` (light / dark / custom).
    For each box: set its stored COLORREF and InvalidateRect.
```

#### Helper: `GetComboSelectedMode(HWND hCombo) → IME_COLOR_MODE`

```cpp
int sel = ComboBox_GetCurSel(hCombo);
return static_cast<IME_COLOR_MODE>(ComboBox_GetItemData(hCombo, sel));
```

All code that previously read the combo index as an integer and cast to `IME_COLOR_MODE` must use this helper instead.

#### `CBN_SELCHANGE` (combo `IDC_COMBO_COLOR_MODE`)

1. `newMode = GetComboSelectedMode(hCombo)`.
2. Call `UpdateColorBoxesForMode(hDlg, newMode)`.

#### Color box `WM_LBUTTONDOWN` / `ChooseColor`

When the user clicks a color box and selects a new color:

1. Determine current mode from combo.
2. Update the corresponding slot in the mode-appropriate palette (`g_lightPalette`, `g_darkPalette`, or `g_customPalette`).
3. Repaint the box.

This is unchanged from the existing custom-color logic except the target palette array changes based on mode.

#### `PSN_APPLY`

1. Read `newMode = GetComboSelectedMode(hCombo)`. This is already `IME_COLOR_MODE_LIGHT` if the user is on old Windows (SYSTEM item was never in the combo), so no extra gating needed.
2. Write all 3 palettes to `CConfig`:
   - `SetLightItemBGColor(g_lightPalette.colors[0])` … (6 calls)
   - `SetDarkItemBGColor(g_darkPalette.colors[0])` … (6 calls)
   - `SetItemBGColor(g_customPalette.colors[0])` … (6 calls, unchanged)
3. `CConfig::SetColorMode(newMode)`.
4. Call `CConfig::WriteConfig(imeMode, FALSE)` as before.

Writing all three palettes regardless of which was modified simplifies the logic and ensures the INI always has a complete snapshot.

#### `IDC_BUTTON_RESTOREDEFAULT`

"Restore Default" is a **full factory reset** of all colour settings — not just the currently selected mode. Resetting only the active palette would silently leave customisations in the other palettes, which is surprising and hard to undo.

1. Reset **all three** dialog-local palettes to compile-time constants:
   - `g_lightPalette` ← `CANDWND_ITEM_COLOR`, `CANDWND_PHRASE_COLOR`, `CANDWND_NUM_COLOR`, `GetSysColor(COLOR_3DHIGHLIGHT)`, `CANDWND_SELECTED_ITEM_COLOR`, `CANDWND_SELECTED_BK_COLOR`
   - `g_darkPalette` ← `CANDWND_DARK_ITEM_COLOR`, `CANDWND_DARK_PHRASE_COLOR`, `CANDWND_DARK_NUM_COLOR`, `CANDWND_DARK_ITEM_BG_COLOR`, `CANDWND_DARK_SELECTED_COLOR`, `CANDWND_DARK_SELECTED_BG_COLOR`
   - `g_customPalette` ← same as light defaults (matches original Phase 10 behaviour)
2. Reset combo to SYSTEM (on Win10 1809+ / Win11) or LIGHT (on pre-1809 Windows where SYSTEM is not in the combo).
3. Call `UpdateColorBoxesForMode(hDlg, IME_COLOR_MODE_SYSTEM / LIGHT)` — colour boxes will be disabled (SYSTEM) or show factory light colours (LIGHT on old Windows).

---

### 13.10 First-Run / No INI File Defaults

#### Timing: when does `LoadConfig` / first `WriteConfig` run?

`LoadConfig` is called at the top of `WM_INITDIALOG` in the settings dialog, immediately before `ParseConfig(hDlg, imeMode, TRUE)`:

```text
User opens settings dialog
  └─ WM_INITDIALOG
       ├─ LoadConfig(imeMode)           ← reads INI; if absent, calls WriteConfig to create it
       └─ ParseConfig(hDlg, mode, TRUE) ← populates controls, sets _colorDisplayedMode
```

The first `WriteConfig` therefore fires **the moment the user opens the settings dialog for the first time** — not during IME startup. By the time `ParseConfig(TRUE)` runs, all `CConfig` fields are correctly set.

#### Static field values before the first `LoadConfig`

All 18 color fields keep their C++ static-initialiser values (factory constants) until `LoadConfig` reads or creates the INI:

```cpp
COLORREF CConfig::_lightItemColor       = CANDWND_ITEM_COLOR;
COLORREF CConfig::_lightItemBGColor     = GetSysColor(COLOR_3DHIGHLIGHT);
// ... (6 light + 6 dark fields, all at compile-time defaults)
```

> **Note on `GetSysColor(COLOR_3DHIGHLIGHT)`**: evaluated at static-init time, returns the system default — matches the existing `_itemBGColor` initialisation.

`_colorMode` is set explicitly in `LoadConfig`'s no-INI branch before calling `WriteConfig`: `SYSTEM` on Win10 1809+ / Win11, `LIGHT` on older Windows.

`_colorDisplayedMode` (file-scope static, dialog-only) is initialised to `LIGHT` as a safe fallback. In practice it is always overwritten by `ParseConfig(TRUE)` before any code reads it — `LoadConfig` runs first and sets `_colorMode` correctly, then `ParseConfig(TRUE)` copies it to `_colorDisplayedMode`.

| Scenario | `_colorMode` | `_colorDisplayedMode` after dialog opens | INI written |
| -------- | ------------ | ---------------------------------------- | ----------- |
| No INI, Win10 1809+ / Win11 | `SYSTEM` | `SYSTEM` (snapped from combo) | `ColorMode = 0` + all 18 keys |
| No INI, pre-1809 Windows | `LIGHT` | `LIGHT` | `ColorMode = 1` + all 18 keys |

---

### 13.11 Backward Compatibility

#### Cases with a `ColorMode` key present

| Old INI state | Behavior after Phase 13 |
| ------------- | ----------------------- |
| `ColorMode = 0` (SYSTEM), no `LightXxx`/`DarkXxx` keys | Light/dark palettes default to hardcoded constants. ✓ |
| `ColorMode = 1` (LIGHT), no `LightXxx` keys | Light palette defaults to hardcoded constants. ✓ |
| `ColorMode = 2` (DARK), no `DarkXxx` keys | Dark palette defaults to hardcoded constants. ✓ |
| `ColorMode = 3` (CUSTOM) + old `ItemColor` etc. | Custom palette (`_itemColor` etc.) loads from INI. Light/dark palettes default to constants. ✓ |

#### Cases without a `ColorMode` key (Phase 10 backward-compat branch, updated)

When `_colorModeKeyFound == false` after `ParseConfig`, the existing 6 color keys (`ItemColor` etc.) have already been parsed into `_itemColor`, `_phraseColor`, etc. (the CUSTOM palette fields). The inferral logic then runs:

```cpp
if (!_colorModeKeyFound)
{
    // Old DIME had the 6 ItemColor/PhraseColor/... keys but no ColorMode key.
    // If colors are still at factory defaults → no customisation ever happened
    // → migrate to SYSTEM (or LIGHT on pre-1809 Windows where SYSTEM is hidden).
    // If any color differs from the default → user had customised them
    // → migrate to CUSTOM; _itemColor etc. are already loaded and CUSTOM reads
    //   them directly, so no copying is needed.
    bool isDefault =
        _itemColor       == CANDWND_ITEM_COLOR &&
        _selectedColor   == CANDWND_SELECTED_ITEM_COLOR &&
        _selectedBGColor == CANDWND_SELECTED_BK_COLOR &&
        _phraseColor     == CANDWND_PHRASE_COLOR &&
        _numberColor     == CANDWND_NUM_COLOR &&
        _itemBGColor     == GetSysColor(COLOR_3DHIGHLIGHT);
    _colorMode = isDefault
        ? (IsWindows1809OrLater()
            ? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM
            : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT)
        : IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM;
}
```

| Old INI (no `ColorMode` key) | Inferred mode | Color palette used |
| ---------------------------- | ------------- | ------------------ |
| Keys absent, or all at defaults, Win10 1809+ | `SYSTEM` | Light/dark palettes use hardcoded constants |
| Keys absent, or all at defaults, pre-1809 | `LIGHT` | Light palette uses hardcoded constants |
| Keys present, any non-default | `CUSTOM` | `_itemColor` etc. already loaded from INI; CUSTOM mode reads them directly — no copying needed |

**Why CUSTOM, not LIGHT:** The legacy `ItemColor` / `PhraseColor` etc. fields are exactly the fields `IME_COLOR_MODE_CUSTOM` reads at runtime. The user opens settings, sees CUSTOM selected, and their old colors appear in the color boxes immediately.

#### Cases with `ColorMode = 0` (SYSTEM) on pre-1809 Windows

Even if a user saved `ColorMode = 0` on a newer machine and then ran on an old machine (or the INI was copied), the dialog must not crash or show a missing combo item. The `WM_INITDIALOG` logic (§13.9) detects `build < 17763`, skips adding the SYSTEM item, and falls back to selecting LIGHT when the saved value is SYSTEM. On `PSN_APPLY`, `ColorMode = 1` (LIGHT) is written, silently migrating the INI. Next time the user opens the dialog on old Windows, LIGHT is selected as expected. If they later move back to Win10 1809+ or Win11, SYSTEM is available again in the combo.

---

### 13.11 Files to Change

| File | Change |
|------|--------|
| `src/Config.h` | Add 12 static fields + getters/setters |
| `src/Config.cpp` | Init 12 statics; update `WriteConfig` (+12 keys); update `SetColorInPresenter` (remove constants, use fields) |
| `src/DictionarySearch.cpp` or `TableDictionaryEngine.cpp` | Add 12 new key parse cases in `ParseConfig` |
| `src/UIPresenter.cpp` | Update notify-window color dispatch (if separate from `SetColorInPresenter`) |
| `src/Config.cpp` (dialog handler) | Rewrite `UpdateColorBoxes` helper; update `WM_INITDIALOG`, `CBN_SELCHANGE`, `PSN_APPLY`, `IDC_BUTTON_RESTOREDEFAULT` |
| `src/Define.h` | No change (constants remain as defaults) |

---

### 13.12 Test Plan for Phase 13

#### Unit Tests — `UT-PT` (Per-Theme palette)

| ID | Method name | Validates |
|----|-------------|-----------|
| UT-PT-01 | `LightPalette_DefaultsMatchConstants` | All 6 `_light*` fields equal the `CANDWND_*` light constants at initialisation |
| UT-PT-02 | `DarkPalette_DefaultsMatchDarkConstants` | All 6 `_dark*` fields equal the `CANDWND_DARK_*` constants at initialisation |
| UT-PT-03 | `LightPalette_RoundTrip_WriteRead` | Write `LightItemColor=0xFF0000` → `LoadConfig` → `GetLightItemColor() == 0xFF0000` |
| UT-PT-04 | `DarkPalette_RoundTrip_WriteRead` | Same for dark palette |
| UT-PT-05 | `LightPalette_AbsentKeys_FallBackToDefaults` | INI without `LightXxx` keys → fields stay at defaults |
| UT-PT-06 | `DarkPalette_AbsentKeys_FallBackToDefaults` | Same for dark |
| UT-PT-07 | `CustomPalette_Unchanged_BackwardCompat` | Old `ItemColor` key still parses into `_itemColor` correctly |

#### Integration Tests — `IT-PT` (Settings Dialog)

| ID | Method name | Validates |
|----|-------------|-----------|
| IT-PT-01 | `SettingsDlg_LightMode_ColorBoxesEnabled` | Selecting LIGHT in combo → all 6 boxes `IsWindowEnabled() == TRUE` |
| IT-PT-02 | `SettingsDlg_DarkMode_ColorBoxesEnabled` | Selecting DARK in combo → all 6 boxes enabled |
| IT-PT-03 | `SettingsDlg_SystemMode_ColorBoxesDisabled` | Selecting SYSTEM → all 6 boxes `IsWindowEnabled() == FALSE` |
| IT-PT-04 | `SettingsDlg_SwitchModes_BoxesReflectPalette` | Switch DARK→LIGHT → color boxes update to light palette colors |
| IT-PT-05 | `SettingsDlg_Apply_PersistsLightPalette` | Change a light color, Apply → `LoadConfig` → `GetLightItemColor()` returns new value |
| IT-PT-06 | `SettingsDlg_Apply_PersistsDarkPalette` | Same for dark palette |
| IT-PT-07 | `SettingsDlg_RestoreDefault_ResetsPaletteToConstants` | DARK mode restore → `_darkItemColor == CANDWND_DARK_ITEM_COLOR` |
| IT-PT-08 | `SettingsDlg_CustomMode_UnchangedBehavior` | CUSTOM still enables boxes and saves `ItemColor` key (backward compat) |
| UT-PT-08 | `OsBuildNumber_Below17763_SystemItemAbsentFromCombo` | When `OsBuildNumber()` < 17763, combo has no SYSTEM item; `GetComboSelectedMode` never returns `IME_COLOR_MODE_SYSTEM` |
| UT-PT-09 | `OsBuildNumber_Below17763_SavedSystemMode_FallsBackToLight` | `_colorMode = SYSTEM` loaded from INI on old Windows → dialog selects LIGHT, Apply saves `ColorMode = 1` |
