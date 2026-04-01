# Candidate Window Caret Tracking

**Date:** 2026-03-31

---

## 1. How caret tracking works

The candidate window needs a screen position to anchor itself. DIME uses three mechanisms, tried in priority order:

### 1.1 Primary: TSF `GetTextExt` (composition range)

**Files:** `TfTextLayoutSink.cpp`, `UIPresenter.cpp`

When a composition is active, `_StartCandidateList` stores the composition range via `_StartLayout(pContextDocument, ec, pRangeComposition)`. The range is used by `_GetTextExt()` which calls `ITfContextView::GetTextExt(_pRangeComposition)` to get the composition's screen rectangle.

```
_StartCandidateList
  → _StartLayout(pContext, ec, pRange)     // stores _pRangeComposition
  → _GetTextExt(&rcTextExt)               // calls pContextView->GetTextExt(_pRangeComposition)
  → _LayoutChangeNotification(&rcTextExt)  // positions candidate at rcTextExt.bottom
```

This is the most accurate method — the app itself reports where the composition text is on screen.

### 1.2 Saved composition rect: `_rectCompRange`

**File:** `UIPresenter.cpp`

`_rectCompRange` is saved at the end of every `_LayoutChangeNotification` call (line 1021). It stores the last valid composition rect. Used as fallback when `_GetTextExt` fails (e.g. phrase candidates after commit — the composition is gone but the caret hasn't moved).

Used in two places:
- `_MoveUIWindowsToTextExt` (line 878) — when `_GetTextExt` fails
- `_StartCandidateList` else branch (line 570) — when starting phrase candidates without composition

### 1.3 Fallback: `GetCaretPos` (Win32 caret)

**File:** `UIPresenter.cpp` — `_LayoutChangeNotification`, lines 919-923

Always called as a secondary signal. `GetCaretPos` returns the caret position of the calling thread's window, then `ClientToScreen` converts to screen coordinates.

```cpp
GetCaretPos(&caretPt);
ClientToScreen(parentWndHandle, &caretPt);
```

Used in two situations:
- **Narrowing:** When the composition rect is wider than the candidate window (e.g. Firefox returns the entire text field), `compRect.left` is narrowed to `caretPt.x` (line 936)
- **firstCall fallback:** When `GetTextExt` returns an invalid rect, `_candLocation` has no prior value, and it's the first call, a synthetic rect is built from `caretPt` (lines 949-961)

**Limitation:** `GetCaretPos` returns the caret of the IME's thread, not the host app's thread. In cross-process scenarios (e.g. SearchHost.exe, elevated CMD), this returns wrong coordinates.

### 1.4 Last resort: `_candLocation` (cached position)

**File:** `UIPresenter.cpp` — `_LayoutChangeNotification`, lines 944-948

When the TSF rect is invalid (e.g. `{0,0,1,1}` from SearchHost.exe) and `_candLocation` has a prior value, the candidate stays at the last successfully computed position. This prevents jumping to screen origin.

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

**File:** `CandidateHandler.cpp` — `_HandleCandidateWorker`, lines 202-233

### Problem

After committing a candidate, if "Make Phrase" is enabled, the old code inserted a space `L" "` into a temporary composition to provide a range for `GetTextExt`. This space was visible to the user.

### Fix

Replaced with direct `_StartCandidateList(nullptr)` call. Three changes make this work:

**1. `_StartLayout` updates range on same context (`TfTextLayoutSink.cpp`)**

Previously, `_StartLayout` skipped entirely when `_pContextDocument == pContextDocument` (same context), leaving a stale `_pRangeComposition`. Now it always updates the range — releases old, stores new — so the null range is properly recorded.

**2. `_StartCandidateList` falls back to `_rectCompRange` (`UIPresenter.cpp`)**

When `_GetTextExt` fails (null range), the else branch passes `_rectCompRange` to `_LayoutChangeNotification`. This rect was saved during the just-committed normal candidate's positioning — the caret hasn't moved, so the position is correct.

**3. `_StartCandidateList` skips `MakeCandidateWindow` if window exists (`UIPresenter.cpp`)**

`MakeCandidateWindow` returns `E_FAIL` when `_pCandidateWnd` is non-null (window already exists). This caused `_EndCandidateList` to destroy the window. Now `_StartCandidateList` checks `_pCandidateWnd` first and skips creation if it already exists.

### Files changed

| File | Change |
|------|--------|
| `CandidateHandler.cpp` | Space hack → direct `_StartCandidateList(nullptr)` call |
| `TfTextLayoutSink.cpp` | `_StartLayout`: update `_pRangeComposition` even on same context |
| `UIPresenter.cpp` | `_StartCandidateList`: skip `MakeCandidateWindow` if exists; else branch uses `_rectCompRange` |
| `UIPresenter.h` | `_In_` → `_In_opt_` for `pRangeComposition` parameter |

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
