# Candidate Window Caret Tracking

**Date:** 2026-03-31

---

## 1. How caret tracking works

DIME must know the screen position of the text caret at every stage of the
input lifecycle — before, during, and after composition — to position the
candidate window, notify window, and associated phrase popup.

### 1.1 Before composition: probe (`_ProbeComposition`)

**Files:** `Composition.cpp`, `UIPresenter.cpp`  
**Full details:** [CARET_TRACKING_PROBE.md](CARET_TRACKING_PROBE.md)

Before the user types anything, the CHN/ENG notify window needs a position.
No composition exists yet, so the TSF text layout sink has no range to query.
`_ProbeComposition` solves this by requesting a read-only edit session and
calling `GetSelection` → `GetTextExt` on the selection range (caret position).

**History:** The original probe used `StartComposition` → `EndComposition`
(destructive) to force a composition range for `GetTextExt`. This corrupted
LINE's Chromium TSF text store, breaking the Windows emoji panel. Replaced
with a non-destructive read-only probe (2026-04-01).

The probe feeds its rect into `_LayoutChangeNotification`, which uses the
same data sources as the during-composition path (§1.2). Four issues arose
from selection-range `GetTextExt` returning lower-quality rects:

| Issue | Problem | Fix |
|-------|---------|-----|
| A | Zero-height junk rects (LINE initial focus) | Height > 0 guard |
| B | Notify positioning skipped when candidate allocated but not visible | Expanded guard: `!_pCandidateWnd \|\| !_pCandidateWnd->_IsWindowVisible()` |
| C | Chromium returns 2px-height rect for selection range | Pad with `_rectCompRange` height; DPI-scaled 16px minimum |
| D | Rect from wrong UI element (Excel formula bar, CMD) | `hwndCaret != hwndParent` check skips unreliable rects |

**WPF/UWP fallback — UIAutomation (tier 4):**

When all three sources fail (zero-height `GetTextExt`, zero `GetGUIThreadInfo`,
no Win32 caret), the probe falls through to UIAutomation `TextPattern` as a
last resort.  UIA `ExpandToEnclosingUnit(TextUnit_Line)` provides the correct
**y-position** (which line the caret is on).  The x-position is the text area's
left edge — PSI's UIA provider cannot report caret-level horizontal position.
See [CARET_TRACKING_UIA.md](CARET_TRACKING_UIA.md) for full details.

**Known limitations:**

- **WPF apps (PowerShell ISE):** Notify at correct line, left-aligned to text area. UIA gives correct y but x = left edge (PSI UIA provider limitation).
- **Firefox new tab center search field:** TSF context is the URL bar — notify appears there. Expected behavior.

### 1.2 During composition: TSF text layout sink

**Files:** `TfTextLayoutSink.cpp`, `UIPresenter.cpp`

When a composition is active, `_StartCandidateList` stores the composition
range via `_StartLayout(pContext, ec, pRange)`. The text layout sink monitors
this range — on every layout change, `_GetTextExt()` calls
`ITfContextView::GetTextExt(_pRangeComposition)` to get the composition's
screen rectangle.

```
_StartCandidateList
  → _StartLayout(pContext, ec, pRange)     // stores _pRangeComposition
  → _GetTextExt(&rcTextExt)               // calls pContextView->GetTextExt(_pRangeComposition)
  → _LayoutChangeNotification(&rcTextExt)  // positions candidate at rcTextExt.bottom
```

This is the most accurate method — the app itself reports where the
composition text is on screen.

### 1.3 After composition: `_rectCompRange` fallback

**File:** `UIPresenter.cpp`

After a candidate is committed, the composition ends — the layout sink has
no range to query. But the caret hasn't moved, so the last known position
is still correct.

`_rectCompRange` is saved at the end of every `_LayoutChangeNotification`
call (line 1030). It stores the last valid composition rect. Used in two
situations:

- **Associated phrase popup** — after committing a candidate, the phrase
  candidate list needs a position. `_StartCandidateList(nullptr)` falls
  back to `_rectCompRange` (see §4 for the space hack removal).
- **`_MoveUIWindowsToTextExt`** — when `_GetTextExt` fails, uses
  `_rectCompRange` as fallback.
- **Probe height padding** — `_rectCompRange` height supplements the
  probe's 2px-height rect from Chromium apps (Issue C above).

`_rectCompRange` is reset on focus change (`ResetCompRange()` in
`OnSetFocus`) so stale heights from a previous app are never used.

### 1.4 Data sources within `_LayoutChangeNotification`

All three lifecycle stages feed into `_LayoutChangeNotification`, which
uses these data sources in priority order:

**a) TSF rect (`lpRect`)** — the rect from `GetTextExt` (composition range
during typing, selection range from probe). Primary source when valid.

**b) `GetCaretPos` / `GetGUIThreadInfo` (Win32 caret)** — always queried
as a secondary signal. Used for narrowing (when TSF rect is wider than the
candidate window, e.g. Firefox) and as firstCall fallback.

**Limitation:** `GetCaretPos` returns the caret of the IME's thread, not
the host app's thread. In cross-process scenarios (SearchHost.exe, elevated
CMD) this returns wrong coordinates. `GetGUIThreadInfo` is more reliable
but WPF apps return `{0,0,0,0}`.

**c) UIAutomation TextPattern (WPF/UWP fallback)** — used in `ShowNotifyText`
and `SHOW_NOTIFY` timer when `GetGUIThreadInfo` returns a zero caret rect and
no candidate window is visible. Gets the caret's line rect via
`ExpandToEnclosingUnit(TextUnit_Line)`, corrects client-to-screen coords using
the focused element's `BoundingRectangle`. See §1.1 and
[CARET_TRACKING_UIA.md](CARET_TRACKING_UIA.md).

**d) `_candLocation` / `_notifyLocation` (cached position)** — last resort.
When the TSF rect is invalid (e.g. `{0,0,1,1}` from SearchHost.exe) and a
prior value exists, the window stays at the last good position. Prevents
jumping to screen origin.

---

## 2. Positioning flow in `_LayoutChangeNotification`

```
_LayoutChangeNotification(lpRect, firstCall):
  1. Get parentWndHandle from ITfContextView::GetWnd() or GetForegroundWindow()
  2. GetCaretPos → ClientToScreen → caretPt
  3. If lpRect is valid (height > 0 or width > 0, not near-origin):
       → Use lpRect for positioning (primary path)
       → Narrow compRect.left to caretPt.x if composition wider than candidate
       → _GetWindowExtent → CalcFitPointAroundTextExtent → _Move
  4. Else if _candLocation is non-zero:
       → Keep last known good position (don't move)
  5. Else if firstCall && parentWndHandle:
       → Build synthetic rect from caretPt → _GetWindowExtent → _Move
```

---

## 3. When each path is used

| Scenario | TSF rect | Path taken |
|----------|----------|------------|
| Normal composition (most apps) | Valid | Primary (step 3) |
| Associated phrase (no composition) | `_GetTextExt` fails | Fallback to `_rectCompRange` (step 3 with saved rect) |
| SearchHost.exe best match shown | `{0,0,1,1}` | `_candLocation` fallback (step 4) |
| First show, TSF fails, no cache | Invalid, `_candLocation` = 0 | `GetCaretPos` fallback (step 5) |
| Subsequent layout change, TSF fails | Invalid, `_candLocation` ≠ 0 | Keep last position (step 4) |

---

## 4. Associated phrase — space hack removal (2026-03-31)

Part of the after-composition caret tracking described in §1.3.

**File:** `CandidateHandler.cpp` — `_HandleCandidateWorker`, lines 202-233

**Problem:** After committing a candidate, if "Make Phrase" is enabled, the old
code inserted a space `L" "` into a temporary composition to provide a range for
`GetTextExt`. This space was visible to the user.

**Fix:** Replaced with direct `_StartCandidateList(nullptr)` call. Three changes
make this work:

1. **`_StartLayout` updates range on same context** (`TfTextLayoutSink.cpp`) —
   previously skipped when same context, leaving stale `_pRangeComposition`. Now
   always updates the range so the null range is properly recorded.

2. **`_StartCandidateList` falls back to `_rectCompRange`** (`UIPresenter.cpp`)
   — when `_GetTextExt` fails (null range), uses `_rectCompRange` saved during
   the just-committed candidate's positioning.

3. **`_StartCandidateList` skips `MakeCandidateWindow` if window exists**
   (`UIPresenter.cpp`) — prevents `E_FAIL` → `_EndCandidateList` destroying an
   existing window.

| File | Change |
|------|--------|
| `CandidateHandler.cpp` | Space hack → direct `_StartCandidateList(nullptr)` call |
| `TfTextLayoutSink.cpp` | `_StartLayout`: update `_pRangeComposition` even on same context |
| `UIPresenter.cpp` | `_StartCandidateList`: skip `MakeCandidateWindow` if exists; else branch uses `_rectCompRange` |
| `UIPresenter.h` | `_In_` → `_In_opt_` for `pRangeComposition` parameter |

### Regression and re-fix (2026-04-28, during #127 verification)

Items 2 and 3 above were **silently reverted** by commit `f898dd6` (*"Suppress
CHN/ENG notify popup for non-editable windows on focus change"*). The
regression went unnoticed for ~3 weeks — phrase candidates were technically
visible for one paint cycle (because `_SetCandidateText`→`Show` runs *before*
the destructive `_StartCandidateList` repositioning call) and then destroyed,
producing a hard-to-spot blink.

While verifying [#127_ISSUE.md](#127_ISSUE.md) with debug tracing, the pattern
was identified and both guards restored. A third bug surfaced once the guards
were back: `_GetTextExt` was returning `S_OK` (instead of failure) when
`_pRangeComposition == nullptr`, leaving the caller's local rect at
`{0,0,0,0}` and positioning the phrase window at screen origin. a97d654's
commit message claimed *"_GetTextExt fails cleanly"* — but the function had
always returned success in this case. Fixed by returning `E_FAIL` when
`_pRangeComposition == nullptr` (also plugs a pre-existing `pContextView`
leak on the failure path).

| File | Change (this branch) |
|------|----------------------|
| `UIPresenter.cpp` | Restored `if (_pCandidateWnd) hr = S_OK;` guard around `MakeCandidateWindow` and `else _LayoutChangeNotification(&_rectCompRange, TRUE)` fallback |
| `TfTextLayoutSink.cpp` | `_GetTextExt` returns `E_FAIL` when `_pRangeComposition == nullptr` (was returning `S_OK` with unpopulated rect) + `pContextView` leak fix |

See [#127_ISSUE.md "Co-fixed regressions found during verification"](#127_ISSUE.md) for the full debug-log evidence.

---

## 5. SearchHost.exe fix (2026-03-31)

**File:** `UIPresenter.cpp` — `_LayoutChangeNotification`, lines 931-948

Windows 11 Explorer search box (SearchHost.exe) returns `{0,0,1,1}` from `GetTextExt` when a "best match" result appears. This passed the old validity check (`0 >= 0`) and positioned the candidate at screen origin.

**Fix:**
- Tightened validity check: `bottom - top > 0` (strict) + reject `left <= 1 && top <= 1 && right <= 1 && bottom <= 1`
- Added `_candLocation` fallback: when rect is invalid and `_candLocation` has a prior value, keep last position

---

## 6. Candidate too low when flipped above (fixed 2026-03-31)

When the composition is near the bottom of the screen, the candidate window flips above. The candidate was positioned too low — partially covering the composition text.

**Root cause:** The validity check in `_LayoutChangeNotification` (line 931) used `>= 0` which accepted zero-area rects. Combined with the `_candLocation` fallback and SearchHost `{0,0,1,1}` rejection changes, certain edge cases caused `CalcFitPointAroundTextExtent` to receive slightly different input, resulting in incorrect flip-above positioning.

**Fix:** Tightened validity check to `> 0` (strict positive area) ensures only genuinely valid rects reach the primary positioning path. Invalid rects fall through to `_candLocation` or `GetCaretPos` fallback, which position correctly.

**Note:** After this fix, the candidate position is correct when flipped above. However, the **notify window** appears visually higher than the candidate (not bottom-aligned). This is a separate pre-existing issue — see section 7.

---

## 7. Pre-existing: candidate/notify misaligned when flipped above (2026-03-31)

### Single root cause

Both the "candidate too low" and "notify too high" symptoms have one root cause: **the candidate window resizes from h=260 to h=272 AFTER positioning.** The 12px height increase causes the bottom edge to shift down, breaking alignment with the composition top and with the notify.

### Evidence from debug.txt

Enabled `debugPrint` in `UIPresenter.cpp`, `NotifyWindow.cpp`, `CandidateWindow.cpp` (already on).

#### Scenario A: Type 明 (multiple candidates, flip above)

```
line 112: _ResizeWindow() h=260 pos=(-32768,-32768)     ← initial size before positioning
line 120: _Move() y=489                                  ← positioned: 749 - 260 = 489, bottom at 749 ✓
line 134: _Move(notify) y=719                            ← bottom-aligned: 489 + 260 - 30 = 719, bottom at 749 ✓
line 166: _ResizeWindow() h=272 pos=(145,489)            ← height changes 260→272, y stays 489
                                                          → bottom shifts to 489+272=761, 12px below 749
```

#### Scenario B: Type 天 (direct hit, associated phrase, flip above)

```
line 649: _Move(cand) y=489                              ← from _candLocation fallback
line 697: _Move(notify) y=719                            ← bottom-aligned: 489 + 260 - 30 = 719
line 705: _ResizeWindow() h=272 pos=(145,489)            ← same resize 260→272 after WM_PAINT
                                                          → candidate bottom 761, notify bottom 749, 12px gap
```

Same in both scenarios: positioning uses h=260, then `_DrawList` → `_ResizeWindow` changes to h=272.

### Why height changes

`_cyRow` is measured by `GetTextExtentPoint32` in two places:

| Where | DC source | `_cyRow` | Total height (10 rows) |
|-------|-----------|----------|----------------------|
| `WM_CREATE` (line 407) | `GetDC(wndHandle)` | produces h=260 | `_cyRow/3 + _cyRow*10 + _padding + 2` = 260 |
| `_DrawList` (line 932) | DC from `BeginPaint` | produces h=272 | same formula = 272 |

`GetDC` and `BeginPaint` return DCs with different font metric results (1.2px/row × 10 rows = 12px total).

### Fix

The two `_cyRow` calculations use the same code (`GetTextExtentPoint32` with same font and text) but different DCs:

- `WM_CREATE`: `GetDC(wndHandle)` — window DC
- `_DrawList`: DC from `BeginPaint` — paint DC

These DCs produce different `candSize.cy` values (off by ~1.2px/row). The fix is to make `WM_CREATE` produce the same result as `_DrawList`.

**Actual root cause (confirmed by debug.txt):** The height difference is NOT from different DCs. Both produce the same `_cyRow`. The difference is `_padding`:

- `WM_CREATE`: `_padding = 0` (constructor default, line 70)
- `_DrawList` (line 947): `_padding = cyLine / 2`

`_ResizeWindow` formula: `totalH = _cyRow/3 + _cyRow*10 + _padding + 2`

- First call (h=260): `_padding = 0` → `_cyRow/3 + _cyRow*10 + 0 + 2 = 260`
- After `_DrawList` (h=272): `_padding = _cyRow/2` → `_cyRow/3 + _cyRow*10 + _cyRow/2 + 2 = 272`

The 12px = `_cyRow/2` = bottom padding, never set in `WM_CREATE`.

**Fix:** In `WM_CREATE`, after computing `_cyRow`, also set `_padding = _cyRow / 2` to match `_DrawList` (line 947: `_padding = cyLine / 2`). **Implemented 2026-03-31.**

---

## 8. Shadow gap between candidate/notify and composition text

### Problem

The candidate and notify windows have a glow shadow (`SHADOW_SPREAD = 14px`, visible spread ~7px). When positioned directly at `prcTextExtent->bottom` (below) or `prcTextExtent->top` (above), the shadow overlaps the composition text, making the candidate/notify appear visually too close.

### Current positioning in `CalcFitPointAroundTextExtent` (`BaseWindow.cpp`)

```cpp
// Below composition: candidate top touches composition bottom
OffsetRect(&rcTargetWindow[0], baseX, prcTextExtent->bottom);

// Above composition: candidate bottom touches composition top
OffsetRect(&rcTargetWindow[1], baseX, prcTextExtent->top - windowHeight);
```

### Fix

Add `SHADOW_SPREAD / 2` (7px) gap between the candidate/notify and the composition:

- **Below (not flipped):** offset Y by `prcTextExtent->bottom + SHADOW_SPREAD / 2`
- **Above (flipped):** offset Y by `prcTextExtent->top - windowHeight - SHADOW_SPREAD / 2`

**File:** `BaseWindow.cpp` — `CalcFitPointAroundTextExtent` (lines 500, 508)

```cpp
// Below: add half shadow gap
OffsetRect(&rcTargetWindow[0], baseX, prcTextExtent->bottom + SHADOW_SPREAD / 2);

// Above: add half shadow gap
OffsetRect(&rcTargetWindow[1], baseX, prcTextExtent->top - windowHeight - SHADOW_SPREAD / 2);
```

Both directions need `SHADOW_SPREAD / 2` to prevent overlap in apps with inaccurate `prcTextExtent`. The visual gap asymmetry between above and below varies by app — Firefox reports `top` too high (large gap above), Notepad reports `top` too low (tight above). This is a `prcTextExtent` accuracy issue across apps, not fixable in the positioning code. The `SHADOW_SPREAD/2` gap is correct for both directions as a minimum safe margin.

`SHADOW_SPREAD` is currently defined in `ShadowWindow.h` (line 41). Move it to `Define.h` so `BaseWindow.cpp` can access it without including `ShadowWindow.h`.

### Impact

This affects ALL windows that use `CalcFitPointAroundTextExtent` — candidate, notify, and any future popup. The 7px gap prevents shadow overlap with the composition in both directions.

---

## 9. Orphaned reverse lookup notify after associated phrase dismiss

### Problem

When reverse lookup notify + associated phrase candidate are both visible, dismissing the candidate (click outside, drag host window, Esc) leaves the notify orphaned at its original screen position. The notify has no timer (`timeToHide=0`) and `_EndCandidateList` intentionally does NOT call `ClearNotify` (to preserve Array special code notify).

### Why `_EndCandidateList` preserves the notify

Array special code notify (`CandidateHandler.cpp` line 180) is shown as `NOTIFY_OTHERS` with `timeToHide=0` during normal candidate commit (`_candidateMode = CANDIDATE_ORIGINAL`). The user needs to read the special code after the candidate is dismissed. If `_EndCandidateList` cleared the notify, Array special code would vanish immediately — breaking the feature.

### When each notify type is shown

| Notify | When | `_candidateMode` at dismiss | Should persist after `_EndCandidateList`? |
|--------|------|---------------------------|----------------------------------------|
| Array special code | Normal candidate commit (line 180) | `CANDIDATE_ORIGINAL` | Yes — user needs to read it |
| Reverse lookup | Normal candidate commit (line 155) | Transitions to `CANDIDATE_PHRASE` | No — associated phrase replaces context |
| Reverse lookup | Associated phrase dismiss (Esc/click) | `CANDIDATE_PHRASE` | No — user has moved on |

### What was tried

**Approach: `ClearNotify` in `OnSetFocus` (`ThreadMgrEventSink.cpp`)**

Added `if (_pUIPresenter) _pUIPresenter->ClearNotify()` at the beginning of `OnSetFocus`. Debug trace confirmed:

- `OnSetFocus` fires on title bar click (verified)
- `ClearNotify` runs, `_Show(FALSE, 0, 0)` hides the window, destructor runs, shadow deleted
- `InvalidateRect(NULL, &rcNotify, TRUE)` called to force repaint

**Result: notify still visually persists despite being destroyed.** Debug log proves the HWND is destroyed (`~CNotifyWindow()` runs, `_DeleteShadowWnd()` runs), yet the user sees both notify text and shadow on screen.

### Reverted

All changes reverted — `ClearNotify` removed from `OnSetFocus`, `ClearNotify` function restored to original.

### Further investigation needed

The debug log proves the notify IS destroyed, yet it remains visible. Possible explanations:

1. **Multiple IME instances across processes** — `debug.txt` is shared by all processes. The destroyed notify belongs to process A, but the visible one belongs to process B's IME instance that we're not tracking. The host app may have multiple text controls or child processes with separate IME contexts.

2. **The visible artifact is not the notify HWND** — it could be a rendering artifact from the candidate window's shadow, or a stale DWM surface that hasn't been repainted.

3. **The notify is recreated after our destroy** — `_NotifyChangeNotification(SHOW_NOTIFY)` can recreate the notify via a delayed timer callback. If it fires after `OnSetFocus`, the notify reappears.

**Next step:** Use Spy++ or similar HWND inspector to identify the visible window — what HWND it is, which process owns it, and whether it matches the one we destroyed.

### Debug trace: title bar click with MakePhrase=ON

Both associated phrase candidate + reverse lookup notify visible. User clicks title bar.

```
Line 72: _DeleteCandidateList()              ← normal candidate commit (CANDIDATE_ORIGINAL)
Line 74: _TerminateComposition()             ← _HandleComplete ends composition
Line 76: _RemoveDummyCompositionForComposing()
         (associated phrase + notify shown — UIPresenter debug OFF, not logged)
Line 78: _DeleteCandidateList()              ← title bar click: TSF terminates probe composition
Line 80: _TerminateComposition()
Line 82: OnSetFocus() _isChinese=1           ← TSF reports focus change AFTER candidate dismissed
Line 84: leaving OnSetFocus()                ← no ClearNotify called, notify stays orphaned
```

Key finding: **`OnSetFocus` fires on title bar click** — confirmed by debug trace. It fires AFTER the candidate is already dismissed. The notify is not cleared because `OnSetFocus` doesn't call `ClearNotify`.

---

## 11. Side-by-side notify covers typing text when assoc + RC both on (2026-04-28)

### §11 Symptom

With both **associated phrase** and **reverse-lookup IM** enabled, after committing a candidate the reverse-lookup notify appeared at the typing row, **covering the composition text**. Visible in Chromium-based hosts (Outlook web, Teams, Slack, the screenshot reporter); Notepad happened to be unaffected because its candidate window did not flip in the same scenario.

When **assoc was OFF**, the same RC notify positioned correctly — strong hint that the bug was in the side-by-side path (which only fires when a candidate window is visible).

### §11 Root cause

`ShowNotifyText` ([UIPresenter.cpp:1575](../src/UIPresenter.cpp#L1575)) — the side-by-side branch when `_pCandidateWnd && _IsWindowVisible()`:

```cpp
// Before:
if (_notifyLocation.x < (int) _pNotifyWnd->_GetWidth())
    _pNotifyWnd->_Move(_notifyLocation.x + _pCandidateWnd->_GetWidth() + SHADOW_SPREAD/2, _notifyLocation.y);
else
    _pNotifyWnd->_Move(_notifyLocation.x - _pNotifyWnd->_GetWidth() - SHADOW_SPREAD/2, _notifyLocation.y);
```

`_notifyLocation.y` was just seeded a few lines above (lines 1552-1567) from `guiInfo->rcCaret.bottom` — i.e. **caret y**, not candidate y. With `refreshLocation = (notifyType == NOTIFY_OTHERS)` from #127, this re-seeds on every reverse-lookup notify show.

Result:

- The x-offset was correctly pushing notify to the side of the candidate.
- The y was the **caret bottom** — the typing line.

When the phrase candidate was below the line (the normal case after make-phrase): notify on typing row → covers text.
When the phrase candidate was flipped above the line: notify still on typing row → loose floating between candidate and text.

The cand-aligned positioning code in `_LayoutChangeNotification` lines 999-1011 has been correct since a97d654 — but `_LayoutChangeNotification` does not always run for async reverse-lookup notify shows (the path goes through `ShowNotifyText` directly, which seeds `_notifyLocation` from caret and does its own `_Move`).

### §11 Fix

Anchor both x and y to `_candLocation.{x,y}` (the candidate's actual top-left, maintained by `_LayoutChangeNotification` lines 967-968), matching the cand-aligned logic in lines 999-1011:

```cpp
// After:
if (_candLocation.x < (int) _pNotifyWnd->_GetWidth())
    _pNotifyWnd->_Move(_candLocation.x + _pCandidateWnd->_GetWidth() + SHADOW_SPREAD/2, _candLocation.y);
else
    _pNotifyWnd->_Move(_candLocation.x - _pNotifyWnd->_GetWidth() - SHADOW_SPREAD/2, _candLocation.y);
```

The non-side-by-side branch (no candidate visible) is unchanged and still uses `_notifyLocation` — preserving the assoc-OFF behavior that was already correct.

### Why Notepad was masked

In Notepad, the candidate window for the test case happened to land below the line (no flip), and the y mismatch between caret-bottom and candidate-top was small enough (or the notify was narrow enough vertically) that the visual overlap was not obvious. Chromium hosts produced larger candidate-vs-caret y deltas and made the bug visible.

### Related work

This is the side-by-side y-anchor counterpart of [REVERSE_CONV_REFACTOR.md "Change 6"](REVERSE_CONV_REFACTOR.md). The notify-positioning misalignment was a pre-existing latent bug that became visible only after #127 enabled `refreshLocation` for `NOTIFY_OTHERS`, which made the caret-y seed authoritative on every reverse-lookup show.

---

## 10. Verification test matrix

### Different UI frameworks

| App | Framework | Notify position | Candidate position | Emoji (Win+.) | Notes |
|-----|-----------|-----------------|-------------------|---------------|-------|
| Notepad | Win32 edit control | OK | OK | OK | Baseline reference |
| CMD | Console host | OK | OK | OK | hwndCaret guard (Issue D) |
| Windows Terminal | XAML | OK | OK | OK | Modern console, different from CMD |
| PowerShell ISE | WPF | y OK, x=left edge | OK | OK | UIA fallback; see §1.1 |
| Visual Studio 2022 | WPF editor | OK | OK | OK | Multi-caret / IntelliSense |
| Firefox | Gecko | OK | OK | OK | New tab center: notify at URL bar (expected) |
| File Explorer rename (F2) | Win32 lightweight | OK | OK | OK | Minimal edit control |

### Office apps

| App | Input surface | Notify position | Candidate position | Emoji (Win+.) | Notes |
|-----|---------------|-----------------|-------------------|---------------|-------|
| Word | Win32/COM | OK | OK | OK | |
| Excel | Cell + formula bar | OK | OK | OK | Probe returns correct cell rect |
| PowerPoint | Canvas text box | OK | OK | OK | |
| Outlook | HTML compose | OK | OK | OK | Different editor from Word |
| OneNote | Free-form canvas | OK | OK | OK | |

### System surfaces

| App | Type | Notify position | Candidate position | Emoji (Win+.) | Notes |
|-----|------|-----------------|-------------------|---------------|-------|
| Win+R (Run dialog) | Minimal edit | OK | OK | OK | |
| Start Menu / Search | UWP search box | OK | OK | OK | SearchHost.exe {0,0,1,1} rect |
| Task Manager search | UWP | OK | OK | OK | |
| Registry Editor (regedit) | Value edit dialog | OK | OK | OK | |
| mstsc (Remote Desktop) | RDP client | OK | OK | OK | IME runs on remote machine, not local |

### Third-party (different toolkits)

| App | Framework | Notify position | Candidate position | Emoji (Win+.) | Notes |
|-----|-----------|-----------------|-------------------|---------------|-------|
| VS Code | Electron/Chromium | OK | OK | OK | |
| Edge | Chromium | OK | OK | OK | |
| LINE | Chromium | OK | OK | OK | Primary emoji fix target |
| Teams | Electron | OK | OK | OK | |
| Discord | Electron | OK | OK | OK | Unique composition handling |
| IntelliJ / Android Studio | Java/Swing | OK | OK | OK | Completely different IME integration |
| Sublime Text | Custom renderer | OK | OK | OK | |
| LibreOffice Writer | Independent IME | OK | OK | OK | |

### Stress / edge cases

| App | Scenario | Notify position | Candidate position | Emoji (Win+.) | Notes |
|-----|----------|-----------------|-------------------|---------------|-------|
| Elevated (Run as Admin) app | Privilege boundary | OK | OK | OK | IME injection across UIPI |
| WSL terminal | Linux subsystem | OK | OK | OK | Input through WSL layer |
| Game chat (e.g. Steam overlay) | DirectX overlay | | | | |
