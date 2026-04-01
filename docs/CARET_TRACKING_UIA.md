# UIA Caret Tracking for WPF/UWP Apps

**Date:** 2026-04-01
**Status:** Implemented. Correct y-position; x = text area left edge (PSI limitation).

---

## 1. Problem

WPF apps (PowerShell ISE) and some UWP apps don't create Win32 system carets.
The existing notify positioning chain fails for them:

| Source | PowerShell ISE result | Why it fails |
|--------|----------------------|--------------|
| TSF `GetTextExt` (probe) | `top=851 bottom=851` (zero height) | WPF TSF adapter returns degenerate rect |
| `GetGUIThreadInfo` | `rcCaret = {0,0,0,0}` | WPF doesn't create Win32 carets |
| `GetCaretPos` | `{0,0}` | Same reason — no Win32 caret |

After `ClientToScreen({0,0})`, the notify appears at the parent window's client
origin (e.g. 71, 71) — the top-left corner — instead of the actual text caret.

---

## 2. Fix: UIAutomation TextPattern fallback

**UIAutomation (UIA)** is a Windows accessibility framework (since Vista/Win7)
that works across all UI frameworks.  WPF apps natively implement UIA providers.

### Positioning priority chain

```
1. TSF GetTextExt on composition range        ← existing, most accurate
2. TSF GetTextExt on selection range (probe)   ← existing, with hwndCaret guard
3. GetGUIThreadInfo / GetCaretPos              ← existing, seeds _notifyLocation
4. UIA TextPattern → ExpandToLine              ← NEW, for WPF/UWP
5. _notifyLocation cache (last known good)     ← existing fallback
```

UIA is tier 4 — only used when tiers 1–3 all fail (zero-height `GetTextExt`
AND `GetGUIThreadInfo` returns zero caret rect AND no candidate window visible).

---

## 3. Implementation

### 3.1 New files: `UIACaretTracker.h` / `UIACaretTracker.cpp`

Standalone helper class encapsulating all UIA COM logic:

```cpp
class CUIACaretTracker
{
public:
    CUIACaretTracker();
    ~CUIACaretTracker();
    HRESULT Initialize();
    HRESULT GetCaretRect(_Out_ RECT* pRect);
private:
    IUIAutomation* _pAutomation;
};
```

- `Initialize()` calls `CoCreateInstance(CLSID_CUIAutomation)` — ~1ms, cached
  for IME lifetime.
- `GetCaretRect()` returns screen-coordinate rect or E_FAIL.
- No link library needed — UIA is purely COM-based.

### 3.2 `GetCaretRect` algorithm

```
GetFocusedElement → GetCurrentPatternAs(TextPatternId)
  → GetSelection → GetBoundingRectangles
    → if cElements > 0: use directly (collapsed selection rect)
    → if cElements == 0: ExpandToEnclosingUnit(TextUnit_Character), retry
    → if still 0: ExpandToEnclosingUnit(TextUnit_Line), retry
    → if rect looks like client coords (inside element dimensions but
      left < element.left): offset by element's BoundingRectangle origin
```

### 3.3 Client-coordinate detection and correction

PSI's WPF UIA provider returns rects in **client coordinates** relative to
the focused text control, not screen coordinates.  Detected by comparing the
text rect against the focused element's `BoundingRectangle`:

```cpp
RECT rcElement = {0};
pFocused->get_CurrentBoundingRectangle(&rcElement);
LONG elemW = rcElement.right - rcElement.left;
LONG elemH = rcElement.bottom - rcElement.top;
if (pRect->right <= elemW && pRect->bottom <= elemH
    && pRect->left < rcElement.left)
{
    // Client coords — offset by element screen origin
    pRect->left   += rcElement.left;
    pRect->top    += rcElement.top;
    pRect->right  += rcElement.left;
    pRect->bottom += rcElement.top;
}
```

### 3.4 Integration in `CUIPresenter`

**Member:** `CUIACaretTracker _uiaCaretTracker` — initialized in constructor.

**Call site 1: `ShowNotifyText`** — after `GetGUIThreadInfo`/`GetCaretPos` seed:

```cpp
if (guiInfo->rcCaret == {0,0,0,0} && no candidate window visible)
{
    RECT rcUIA;
    if (SUCCEEDED(_uiaCaretTracker.GetCaretRect(&rcUIA)) && height > 0)
    {
        _notifyLocation.x = rcUIA.left;
        _notifyLocation.y = rcUIA.bottom;
    }
}
```

**Call site 2: `SHOW_NOTIFY` timer** — same condition, after probe.

### 3.5 Build changes

- Added `UIACaretTracker.h` / `UIACaretTracker.cpp` to `DIME.vcxproj`
- No additional library — UIA is COM-based (`CoCreateInstance`)

---

## 4. What was tried and why

### 4.1 Collapsed selection `GetBoundingRectangles` — empty

PSI returns `cElements=0` for a collapsed selection (zero-width caret).
WPF's UIA TextPattern cannot produce a bounding rect for a zero-length range.

### 4.2 `ExpandToEnclosingUnit(TextUnit_Character)` — also empty

PSI returns `cElements=0` for character-level expansion too.
Only `TextUnit_Line` produces a rect.

### 4.3 `ExpandToEnclosingUnit(TextUnit_Line)` — correct y, wrong x

Returns the **entire line's** bounding rect.  `left` is the line's left margin
(always the same), not the caret's horizontal position.  `top`/`bottom`
correctly reflect which line the caret is on.

### 4.4 Destructive probe (`StartComposition` → `SetText(L" ")` → `GetTextExt`)

Attempted inserting a space into a temporary composition to give `GetTextExt`
something to measure.  **Failed** — `GetTextExt` is called synchronously via
`_MoveUIWindowsToTextExt`, but WPF renders asynchronously.  The rect comes back
as junk (`top=851, bottom=851, left=1439, right=1440` — screen edge) because
WPF hasn't processed the text insertion yet.

During real typing this works because the layout sink **callback** fires after
WPF renders — the async notification path provides the correct rect.  The
synchronous probe path cannot benefit from this.

Also attempted empty composition (no `SetText`) — same junk result.

---

## 5. PSI result: correct y, x = text area left edge

| Before UIA | After UIA |
|-----------|-----------|
| Notify at window origin (71, 71) | Notify at text area left edge, correct line |

The notify now appears on the **correct line** but at the left edge of the text
area instead of at the caret's horizontal position.  This is a PSI WPF UIA
provider limitation — no API returns the caret x-position:

- `GetBoundingRectangles` on collapsed selection → empty
- `ExpandToEnclosingUnit(Character)` → empty
- `ExpandToEnclosingUnit(Line)` → correct y, x = line left margin
- `GetTextExt` (destructive probe) → junk (async rendering)
- `GetGUIThreadInfo` → `{0,0,0,0}` (no Win32 caret)

### Apps where UIA helps

| App | Framework | Before | After |
|-----|-----------|--------|-------|
| PowerShell ISE | WPF | Window origin (71, 71) | Correct line, left-aligned |

### Apps NOT affected (tiers 1–3 already work)

| App | Why UIA won't run |
|-----|-------------------|
| Notepad | `GetTextExt` + `GetCaretPos` both work |
| Word/Excel | hwndCaret guard + `GetGUIThreadInfo` work |
| Edge/Chrome/VS Code | Probe `GetTextExt` on selection works |
| LINE | Probe works (with height padding) |
| CMD | `GetGUIThreadInfo` works (hwndCaret guard skips bad probe) |

---

## 6. Guarding against UIA overriding good positions

UIA must not override `_notifyLocation` when it's already been set to a correct
value by candidate window positioning or a successful probe.  Two conditions
gate UIA:

1. **`GetGUIThreadInfo` returns zero caret** — only WPF/UWP apps.  Apps with
   real Win32 carets skip UIA entirely.

2. **No candidate window visible** — if the candidate window is on screen,
   `_LayoutChangeNotification` already set correct `_notifyLocation`.  The
   reverse lookup notify should use that position, not UIA.

These guards prevent the bug where UIA's `x=87` (left edge) overwrote the
correct `x=252` (from candidate positioning) for the reverse lookup notify.
