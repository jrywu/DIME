# UIA Caret Tracking for WPF/UWP Apps

**Date:** 2026-04-01

---

## 1. Problem

WPF apps (PowerShell ISE) and some UWP apps don't create Win32 system carets.
The current notify positioning chain fails for them:

| Source | PowerShell ISE result | Why it fails |
|--------|----------------------|--------------|
| TSF `GetTextExt` (probe) | `top=934 bottom=934` (zero height) | WPF TSF adapter returns degenerate rect |
| `GetGUIThreadInfo` | `rcCaret = {0,0,0,0}` | WPF doesn't create Win32 carets |
| `GetCaretPos` | `{0,0}` | Same reason — no Win32 caret |

After `ClientToScreen({0,0})`, the notify appears at the parent window's client
origin (e.g. 71, 71) — the top-left corner — instead of the actual text caret.

---

## 2. Proposed fix: UIAutomation TextPattern fallback

**UIAutomation (UIA)** is a Windows accessibility framework (since Vista/Win7)
that works across all UI frameworks.  WPF apps natively implement UIA providers,
so `IUIAutomationTextPattern` returns accurate caret bounding rectangles even
when Win32 caret APIs return zeros.

### Positioning priority chain (after this change)

```
1. TSF GetTextExt on composition range     ← existing, most accurate
2. TSF GetTextExt on selection range (probe) ← existing, with hwndCaret guard
3. GetGUIThreadInfo / GetCaretPos          ← existing, seeds _notifyLocation
4. UIA TextPattern::GetSelection → GetBoundingRectangles  ← NEW
5. _notifyLocation cache (last known good)  ← existing fallback
```

UIA is tier 4 — only used when tiers 1–3 all fail to produce a usable position.

---

## 3. UIA API overview

### 3.1 Initialization

```cpp
#include <UIAutomation.h>

IUIAutomation* pAutomation = nullptr;
CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IUIAutomation, (void**)&pAutomation);
```

`CoCreateInstance` is ~1ms on first call, near-zero after.  The `IUIAutomation`
instance should be created once and cached for the lifetime of the IME.

### 3.2 Getting caret rect

```cpp
IUIAutomationElement* pFocused = nullptr;
pAutomation->GetFocusedElement(&pFocused);

IUIAutomationTextPattern* pTextPattern = nullptr;
pFocused->GetCurrentPatternAs(UIA_TextPatternId,
    IID_IUIAutomationTextPattern, (void**)&pTextPattern);

if (pTextPattern)
{
    IUIAutomationTextRangeArray* pSelection = nullptr;
    pTextPattern->GetSelection(&pSelection);
    if (pSelection)
    {
        IUIAutomationTextRange* pRange = nullptr;
        int length = 0;
        pSelection->get_Length(&length);
        if (length > 0 && SUCCEEDED(pSelection->GetElement(0, &pRange)))
        {
            SAFEARRAY* pRects = nullptr;
            pRange->GetBoundingRectangles(&pRects);
            // pRects contains doubles: [x, y, width, height, ...]
            // First rect = caret/selection start position (screen coords)
            if (pRects && pRects->rgsabound[0].cElements >= 4)
            {
                double* data = nullptr;
                SafeArrayAccessData(pRects, (void**)&data);
                RECT rcCaret;
                rcCaret.left   = (LONG)data[0];
                rcCaret.top    = (LONG)data[1];
                rcCaret.right  = (LONG)(data[0] + data[2]);
                rcCaret.bottom = (LONG)(data[1] + data[3]);
                SafeArrayUnaccessData(pRects);
                // rcCaret is in screen coordinates — use directly
            }
            if (pRects) SafeArrayDestroy(pRects);
            pRange->Release();
        }
        pSelection->Release();
    }
    pTextPattern->Release();
}
pFocused->Release();
```

### 3.3 Key properties

- **Returns screen coordinates** — no `ClientToScreen` needed
- **Rect height** is the actual text line height, not zero
- **Works for WPF, UWP, XAML Islands, and many Electron/Chromium apps**
- `GetFocusedElement` + `GetSelection` takes ~5–50ms depending on app complexity
- `GetBoundingRectangles` on a collapsed selection (caret) may return an empty
  array in some apps — must handle gracefully

---

## 4. Integration plan

### 4.1 New file: `UIACaretTracker.h` / `UIACaretTracker.cpp`

Encapsulate all UIA logic in a standalone helper class:

```cpp
class CUIACaretTracker
{
public:
    CUIACaretTracker();
    ~CUIACaretTracker();

    // Initialize COM interface (call once, e.g. from CUIPresenter constructor)
    HRESULT Initialize();

    // Get caret screen rect from UIA.  Returns S_OK if rect is valid.
    // Returns E_FAIL if UIA is unavailable, element has no TextPattern,
    // or GetBoundingRectangles returns empty.
    HRESULT GetCaretRect(_Out_ RECT* pRect);

private:
    IUIAutomation* _pAutomation;
};
```

### 4.2 Lifetime management

- `Initialize()` in `CUIPresenter::CUIPresenter()` constructor.
- `_pAutomation->Release()` in `CUIPresenter::~CUIPresenter()` destructor.
- `IUIAutomation` is apartment-threaded.  DIME's IME runs on the app's STA
  thread, so no threading issues.

### 4.3 Call site: `ShowNotifyText` in `UIPresenter.cpp`

After the existing `GetGUIThreadInfo` / `GetCaretPos` block (lines 1533–1558),
add a UIA fallback:

```cpp
// Existing code seeds _notifyLocation from GetGUIThreadInfo/GetCaretPos.
// If that returned the window origin (indicating no Win32 caret),
// try UIA as a fallback.
if (_notifyLocation.x < 0 || isWindowOrigin(_notifyLocation, parentWndHandle))
{
    RECT rcUIA = {0};
    if (_pUIACaretTracker && SUCCEEDED(_pUIACaretTracker->GetCaretRect(&rcUIA))
        && (rcUIA.bottom - rcUIA.top > 0))
    {
        _notifyLocation.x = rcUIA.left;
        _notifyLocation.y = rcUIA.bottom;  // position below text line
        debugPrint(L"UIA caret: x=%d y=%d", _notifyLocation.x, _notifyLocation.y);
    }
}
```

**`isWindowOrigin` helper:** returns TRUE if the point equals
`ClientToScreen(hwnd, {0,0})` — indicating `GetGUIThreadInfo` returned zeros.

### 4.4 Call site: `SHOW_NOTIFY` timer in `_NotifyChangeNotification`

Same fallback after the probe, before `_Move`:

```cpp
_pTextService->_ProbeComposition(pContext);
// If probe didn't update position and cached position looks like window origin,
// try UIA before showing.
if (_pNotifyWnd && _notifyLocation.x > UI::DEFAULT_WINDOW_X)
{
    // Check if _notifyLocation is just the window origin (no real caret data)
    // If so, try UIA
    RECT rcUIA = {0};
    if (_pUIACaretTracker && SUCCEEDED(_pUIACaretTracker->GetCaretRect(&rcUIA))
        && (rcUIA.bottom - rcUIA.top > 0))
    {
        _notifyLocation.x = rcUIA.left;
        _notifyLocation.y = rcUIA.bottom;
    }
    _pNotifyWnd->_Move(_notifyLocation.x, _notifyLocation.y);
}
ShowNotify(TRUE, 0, (UINT) wParam);
```

---

## 5. Build changes

### 5.1 Link library

Add to `DIME.vcxproj` `<AdditionalDependencies>`:

```
UIAutomationClient.lib
```

This is a system library — no redistributable needed.

### 5.2 Source files

Add to `DIME.vcxproj`:

```xml
<ClInclude Include="UIACaretTracker.h" />
<ClCompile Include="UIACaretTracker.cpp" />
```

### 5.3 Precompiled header

`UIACaretTracker.cpp` uses standard includes (`UIAutomation.h`, `windows.h`).
No PCH conflicts expected.  Set `<PrecompiledHeader>NotUsing</PrecompiledHeader>`
for all 8 configurations (Win32/x64/ARM64/ARM64EC × Debug/Release) if needed.

---

## 6. Performance considerations

| Operation | Cost | When |
|-----------|------|------|
| `CoCreateInstance(CLSID_CUIAutomation)` | ~1ms | Once per IME lifetime |
| `GetFocusedElement` | 1–10ms | Each notify show (only when tiers 1–3 fail) |
| `GetSelection` + `GetBoundingRectangles` | 5–50ms | Same |

The UIA path only runs when:
1. TSF `GetTextExt` returns zero-height or is rejected by hwndCaret guard, AND
2. `GetGUIThreadInfo` returns a zero caret rect

This is rare — primarily WPF and some UWP apps.  The 5–50ms cost is acceptable
for the delayed notify timer (already 500ms+ delay).

---

## 7. Apps expected to benefit

| App | Framework | Current behavior | After UIA |
|-----|-----------|-----------------|-----------|
| PowerShell ISE | WPF | Notify at window origin (71, 71) | Notify at caret |
| Visual Studio 2022 editor | WPF | Untested — may have same issue | Should work |
| Windows Terminal | XAML/UWP | Untested | Should work if TextPattern exposed |
| Settings app | UWP/XAML | Untested | Should work |

### Apps NOT affected (tiers 1–3 already work)

| App | Why UIA won't run |
|-----|-------------------|
| Notepad | GetTextExt + GetCaretPos both work |
| Word/Excel | hwndCaret guard + GetGUIThreadInfo work |
| Edge/Chrome/VS Code | Probe GetTextExt on selection works |
| LINE | Probe works (with height padding) |
| CMD | GetGUIThreadInfo works (hwndCaret guard skips bad probe) |

---

## 8. Detecting "no real caret" — the trigger condition

The UIA fallback should only fire when existing sources failed.  Detection:

```cpp
// After GetGUIThreadInfo seeds _notifyLocation:
BOOL hasRealCaret = TRUE;
if (gti.rcCaret.left == 0 && gti.rcCaret.top == 0
    && gti.rcCaret.right == 0 && gti.rcCaret.bottom == 0)
{
    hasRealCaret = FALSE;  // No Win32 caret — WPF/UWP app
}
```

Combined with probe failure (zero-height `GetTextExt`), `!hasRealCaret` triggers
the UIA path.  Store `hasRealCaret` as a member or pass it through the call chain.

---

## 9. Error handling

UIA can fail at multiple points.  Every failure = silent fallback to tier 5
(`_notifyLocation` cache).  No user-visible errors.

| Failure | Handling |
|---------|----------|
| `CoCreateInstance` fails | `_pAutomation = nullptr`, all `GetCaretRect` return E_FAIL |
| `GetFocusedElement` returns NULL | Return E_FAIL |
| Element has no `TextPattern` | Return E_FAIL (app doesn't support text UIA) |
| `GetSelection` empty | Return E_FAIL |
| `GetBoundingRectangles` empty | Return E_FAIL (collapsed caret, no rect) |
| UIA call hangs (unresponsive app) | **Risk** — consider timeout via async call |

### 9.1 Timeout protection

`GetFocusedElement` can hang if the target app is unresponsive.  Mitigation
options:

1. **`CoSetProxyBlanket` with timeout** — not available for UIA inproc
2. **Call from a worker thread with `WaitForSingleObject` timeout** — adds
   complexity but guarantees the IME thread never blocks
3. **Accept the risk** — the UIA path only fires on the delayed timer callback
   (not keystroke-critical), and the timer already tolerates 500ms+ delay.
   If the app is hung, the user isn't typing anyway.

**Recommendation:** Start with option 3 (accept the risk).  If field reports
show hangs, add a worker thread with 100ms timeout.

---

## 10. Testing plan

### 10.1 PowerShell ISE (primary target)

1. Open PSI, focus script pane
2. Toggle CHN/ENG → notify should appear near caret (not window origin)
3. Move caret to different lines → notify should track
4. Switch to console pane → notify should follow

### 10.2 Regression — apps that already work

1. **Notepad** — notify at caret (UIA path should NOT fire)
2. **Excel** — notify at cell (hwndCaret guard, UIA should NOT fire)
3. **Edge** — notify at text field (probe works, UIA should NOT fire)
4. **LINE** — emoji still works, notify at caret
5. **CMD** — notify at caret (GetGUIThreadInfo works)

### 10.3 Debug verification

Add `debugPrint` in `GetCaretRect`:
```
UIA: GetFocusedElement hr=0x%08X
UIA: TextPattern hr=0x%08X
UIA: GetBoundingRectangles count=%d rect=(%d,%d,%d,%d)
```

Verify in `debug.txt` that UIA path fires ONLY for PSI, not for Notepad/Excel.

### 10.4 New apps to test with UIA

- Visual Studio 2022 (WPF editor)
- Windows Terminal (XAML)
- Settings app text fields (UWP)
