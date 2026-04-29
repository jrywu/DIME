# Issue #127 — Reverse-lookup hint and special-code hint position drift when "Notify Desktop" preference is OFF

- **Repository:** jrywu/DIME
- **Reporter:** alivejoke-spec
- **Title (orig.):** 反查輸入字根 與 特別碼提示，提示視窗位置不如浮動中英切換視窗
- **Severity:** Medium (visual UX defect, no crash, no data loss)
- **Status:** Fixed (probe widening landed; multi-notify extension landed — see "Extension" section)

## Symptom

With the **floating CHN/ENG switching window** preference (`ShowNotifyDesktop`) **OFF**:

- The popup that shows the **reverse-lookup root keys** (反查輸入字根) and the **special-code hint** (特別碼提示) appears **far from the actual caret position** and the position is **unstable / not tracked** as the caret moves.

With `ShowNotifyDesktop` **ON**:

- The same two hint popups appear correctly near the caret, tracking it as it moves — i.e. they behave the same as the CHN/ENG floating notify window.

The reporter notes the floating CHN/ENG notify is too noisy for daily use and they prefer to keep it OFF, but doing so degrades the position of the *other* hint popups. Their request: at minimum, make the reverse-lookup / special-code hints position themselves as accurately as the floating CHN/ENG window does.

## User Theory (verified)

> "Probe composition" is what tracks the caret position. When `ShowNotifyDesktop` is ON, probe composition runs and the popups follow the caret. When `ShowNotifyDesktop` is OFF, probe composition never runs, so the popups lose caret tracking.

**Verified — the theory is correct.** See Root Cause below.

## Root Cause

### The two notification paths use different positioning machinery

DIME has two `NOTIFY_TYPE` values that flow through `CUIPresenter::ShowNotifyText`:

| `NOTIFY_TYPE`    | Used by                                              |
| ---------------- | ---------------------------------------------------- |
| `NOTIFY_CHN_ENG` | `showChnEngNotify` (the floating CHN/ENG window)     |
| `NOTIFY_OTHERS`  | Reverse-lookup hint, special-code hint, full/half shape, etc. |

Call sites of `NOTIFY_OTHERS` for the two hints in question:
- Reverse-lookup root keys: [src/ReverseConversion.cpp:118](../src/ReverseConversion.cpp#L118)
- Special-code hint:        [src/CandidateHandler.cpp:167](../src/CandidateHandler.cpp#L167), [src/CandidateHandler.cpp:180](../src/CandidateHandler.cpp#L180)

### `_ProbeComposition` is gated to `NOTIFY_CHN_ENG` only

`_ProbeComposition` is the read-only TSF probe that, via `GetSelection` + `ITfContextView::GetTextExt`, obtains the precise caret rect and pushes it through `_LayoutChangeNotification` into `_notifyLocation` (see [docs/CARET_TRACKING_PROBE.md](CARET_TRACKING_PROBE.md)). It is the mechanism that gives the CHN/ENG notify its accurate, caret-tracking position.

The probe fires from **two and only two** call sites in `CUIPresenter`, and **both are gated to `NOTIFY_CHN_ENG`**:

[src/UIPresenter.cpp:1051-1075](../src/UIPresenter.cpp#L1051-L1075) — `SHOW_NOTIFY` timer (delayed-show path):
```cpp
case NOTIFY_WND::SHOW_NOTIFY:
    if((NOTIFY_TYPE)lParam == NOTIFY_TYPE::NOTIFY_CHN_ENG && !_pTextService->_IsComposing())
    {
        if(_pTextService && _GetContextDocument() == nullptr)
        {
            ...
            ShowNotify(TRUE, 0, (UINT) wParam);
            _pTextService->_ProbeComposition(pContext);   // <-- PROBE
        }
    }
    else if((NOTIFY_TYPE)lParam == NOTIFY_TYPE::NOTIFY_OTHERS || ...)
        ShowNotify(TRUE, 0, (UINT) wParam);              // <-- NO PROBE
    break;
```

[src/UIPresenter.cpp:1554-1556](../src/UIPresenter.cpp#L1554-L1556) — immediate-show path (`delayShow == 0`):
```cpp
//means TextLayoutSink is not working. We need to ProbeComposition to start layout
if(_pTextService && delayShow == 0 && _GetContextDocument() == nullptr
   && notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG )
    _pTextService->_ProbeComposition(pContext);
```

So whenever a hint comes in as `NOTIFY_OTHERS`, **no probe runs**, and `_LayoutChangeNotification` is never called for that show. The notify window keeps whatever `_notifyLocation` was last seeded by:

[src/UIPresenter.cpp:1511-1537](../src/UIPresenter.cpp#L1511-L1537):
```cpp
GetGUIThreadInfo(NULL, guiInfo);
if(guiInfo && parentWndHandle) {
    pt->x = guiInfo->rcCaret.left;
    pt->y = guiInfo->rcCaret.bottom;
    ClientToScreen(parentWndHandle, pt);
    if(_notifyLocation.x < 0) _notifyLocation.x = pt->x;   // <-- only seeds if uninitialised
    if(_notifyLocation.y < 0) _notifyLocation.y = pt->y;
} else if(parentWndHandle) {
    POINT caretPt;
    GetCaretPos(&caretPt);
    ...
    if (_notifyLocation.x < 0) _notifyLocation.x = caretPt.x;
    if (_notifyLocation.y < 0) _notifyLocation.y = caretPt.y;
}
```

Two problems compound:

1. **`GetGUIThreadInfo` / `GetCaretPos` are unreliable in modern TSF apps** — Chromium-based apps, UWP/XAML, WPF, and many Win32 apps don't maintain a Win32 system caret. The fallback returns `{0,0,0,0}` or stale data, so the popup lands at the window origin or far from the real caret. (The probe path was specifically introduced to bypass this — see [docs/CARET_TRACKING_PROBE.md](CARET_TRACKING_PROBE.md).)
2. **`_notifyLocation` is only seeded when `< 0`** — i.e. only on first use. If a `NOTIFY_OTHERS` ever fires after a stale `_notifyLocation` from earlier, the new hint inherits the stale position with **no update mechanism** at all.

### Why turning `ShowNotifyDesktop` ON masks the bug

`showChnEngNotify` is gated on the preference:

[src/LanguageBar.cpp:1158-1163](../src/LanguageBar.cpp#L1158-L1163):
```cpp
void CDIME::showChnEngNotify(BOOL isChinese, UINT delayShow)
{
    CStringRange notify;
    if (CConfig::GetShowNotifyDesktop() && _pUIPresenter)
        _pUIPresenter->ShowNotifyText(..., NOTIFY_TYPE::NOTIFY_CHN_ENG);
}
```

When the preference is ON, every CHN/ENG notify call runs the probe, which:
- Calls `_LayoutChangeNotification` → updates `_notifyLocation` precisely.
- Activates TextLayoutSink (`_GetContextDocument() != nullptr` thereafter), so subsequent layout changes flow through the sink and keep `_notifyLocation` fresh.

A subsequent `NOTIFY_OTHERS` (reverse-lookup / special-code hint) then **inherits** that fresh `_notifyLocation` and appears in the right place. It is not because `NOTIFY_OTHERS` itself learned to track the caret — it is piggy-backing on the probe that ran for the CHN/ENG notify.

Turn the preference OFF and `NOTIFY_OTHERS` is on its own, with only the unreliable `GetGUIThreadInfo`/`GetCaretPos` fallback.

### Verification of the theory — explicit answer

> **Q: When the `ShowNotifyDesktop` pref is OFF, is `_ProbeComposition` still running and tracking the caret?**
>
> **A: No.** With the pref OFF, `showChnEngNotify` is short-circuited at [LanguageBar.cpp:1162](../src/LanguageBar.cpp#L1162) and never calls `ShowNotifyText` with `NOTIFY_CHN_ENG`. The two probe call sites at [UIPresenter.cpp:1054](../src/UIPresenter.cpp#L1054) and [UIPresenter.cpp:1555](../src/UIPresenter.cpp#L1555) both require `notifyType == NOTIFY_CHN_ENG` (and the `SHOW_NOTIFY` timer additionally requires it as the lParam tag). Reverse-lookup and special-code hints come in as `NOTIFY_OTHERS`, so the probe is never invoked for them. `_notifyLocation` therefore falls back to `GetGUIThreadInfo` / `GetCaretPos`, which fail or return stale values in most modern TSF hosts — exactly matching the reporter's symptom.

The user's theory is confirmed.

## Proposed Fix

### Strategy

Make `_ProbeComposition` available to `NOTIFY_OTHERS` as well, so that reverse-lookup and special-code hints get the same caret-tracking treatment that CHN/ENG enjoys. Probe is **read-only** since the [CARET_TRACKING_PROBE.md](CARET_TRACKING_PROBE.md) rewrite (`GetSelection` + `GetTextExt`, no `StartComposition`/`EndComposition`), so calling it for additional notify types carries no risk of the LINE/Chromium emoji-input regression that the original destructive probe caused.

### Change A — extend the probe gate in the immediate-show path

[src/UIPresenter.cpp:1554-1556](../src/UIPresenter.cpp#L1554-L1556):

```cpp
// Before:
if(_pTextService && delayShow == 0 && _GetContextDocument() == nullptr
   && notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG )
    _pTextService->_ProbeComposition(pContext);

// After:
if(_pTextService && delayShow == 0 && _GetContextDocument() == nullptr
   && (notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG
       || notifyType == NOTIFY_TYPE::NOTIFY_OTHERS) )
    _pTextService->_ProbeComposition(pContext);
```

This causes reverse-lookup and special-code hints (both come in with `delayShow == 0`, see [ReverseConversion.cpp:118](../src/ReverseConversion.cpp#L118), [CandidateHandler.cpp:167,180](../src/CandidateHandler.cpp#L167)) to trigger the probe when TextLayoutSink is not yet active. The probe then calls `_LayoutChangeNotification`, which updates `_notifyLocation` to the precise caret rect *and* activates TextLayoutSink so subsequent changes are tracked.

### Change B — extend the probe gate in the SHOW_NOTIFY timer path

[src/UIPresenter.cpp:1051-1075](../src/UIPresenter.cpp#L1051-L1075):

Restructure so the probe block is reachable for both notify types when the document context is null. Sketch:

```cpp
case NOTIFY_WND::SHOW_NOTIFY:
{
    NOTIFY_TYPE nt = (NOTIFY_TYPE)lParam;
    BOOL needProbe = (nt == NOTIFY_TYPE::NOTIFY_CHN_ENG && !_pTextService->_IsComposing())
                     || nt == NOTIFY_TYPE::NOTIFY_OTHERS;
    if (needProbe && _pTextService && _GetContextDocument() == nullptr)
    {
        ITfContext* pContext = nullptr;
        ITfDocumentMgr* pDocumentMgr = nullptr;
        ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
        if (pThreadMgr
            && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr
            && SUCCEEDED(pDocumentMgr->GetTop(&pContext)) && pContext)
        {
            ShowNotify(TRUE, 0, (UINT)wParam);
            _pTextService->_ProbeComposition(pContext);
            pContext->Release();
            pDocumentMgr->Release();
            break;
        }
        if (pDocumentMgr) pDocumentMgr->Release();
    }
    // existing fallback
    ShowNotify(TRUE, 0, (UINT)wParam);
    break;
}
```

(Note: confirm AddRef/Release ordering against the existing block; the snippet above corrects a missing `Release` that the current code also has — out of scope unless we want to fold it in.)

### Change C — refresh `_notifyLocation` on every NOTIFY_OTHERS show

[src/UIPresenter.cpp:1511-1537](../src/UIPresenter.cpp#L1511-L1537) currently seeds `_notifyLocation` only when its components are `< 0`. For `NOTIFY_OTHERS` the location should be **refreshed each time** so a stale location from an earlier hint does not stick. Two viable options — pick one (recommend Option 1):

**Option 1 (preferred, smallest change):** Drop the `< 0` guard for `NOTIFY_OTHERS`. The probe added in Change A will overwrite with the precise rect when available; in the fallback case we still get a fresh (even if imperfect) `GetGUIThreadInfo`/`GetCaretPos` value rather than reusing stale state.

```cpp
if (notifyType == NOTIFY_TYPE::NOTIFY_OTHERS || _notifyLocation.x < 0) _notifyLocation.x = pt->x;
if (notifyType == NOTIFY_TYPE::NOTIFY_OTHERS || _notifyLocation.y < 0) _notifyLocation.y = pt->y;
```
(applied in both the `guiInfo` branch and the `GetCaretPos` branch.)

**Option 2:** Reset `_notifyLocation = {-1,-1}` at the top of `ShowNotifyText` for `NOTIFY_OTHERS` so the existing `< 0` guards re-seed naturally. Slightly cleaner but a wider behavioural change — also affects the candidate-side-by-side positioning at lines 1544-1549, so verify nothing there relies on the persisted value.

### Change D — verify (no code change expected)

Confirm `_LayoutChangeNotification` ([UIPresenter.cpp around the `!_pCandidateWnd || !_pCandidateWnd->_IsWindowVisible()` guard at line 1014](../src/UIPresenter.cpp#L1014)) already handles the "notify-only, no candidate window" case correctly. Per [CARET_TRACKING_PROBE.md §4 Issue B](CARET_TRACKING_PROBE.md), it does — that guard was widened specifically for this purpose. So once the probe fires for `NOTIFY_OTHERS`, the existing layout-change pipeline takes over without further plumbing.

### Why this is safe

- `_ProbeComposition` is the **non-destructive** read-only variant since the rewrite documented in [CARET_TRACKING_PROBE.md](CARET_TRACKING_PROBE.md). It uses `GetSelection(TF_DEFAULT_SELECTION)` + `GetTextExt`, never mutates the text store, and was specifically validated against LINE / Chromium / Edge / Firefox / Word / Excel / VS Code / Teams / PowerShell ISE / CMD.
- The four caret-tracking issue fixes (A–D in the probe doc — zero-height rect guard, candidate-not-visible guard, height padding, hwndCaret sanity) all live inside `_ProbeCompositionRangeNotification` and `_LayoutChangeNotification`, so they apply automatically when the probe is invoked from the `NOTIFY_OTHERS` path.
- This change does not alter the gating of the CHN/ENG notify itself — `ShowNotifyDesktop` continues to control whether the user sees "中文 / 英數" popups. It only decouples the *probe* from that preference.

## Files to modify

- [src/UIPresenter.cpp](../src/UIPresenter.cpp)
  - Line 1054 / 1051-1075: extend `SHOW_NOTIFY` timer probe gate to include `NOTIFY_OTHERS`.
  - Line 1555: extend immediate-show probe gate to include `NOTIFY_OTHERS`.
  - Lines 1525-1535: refresh `_notifyLocation` for `NOTIFY_OTHERS` (Option 1) instead of seeding only when `< 0`.

No header changes, no new methods, no preference changes.

## Verification

### Manual repro (must reproduce **before** patch and stop reproducing **after**)

Preconditions: build DIME with the patch, register the new DLL, restart the host app between toggles.

1. Open DIME settings → set **Show Notify Desktop** = OFF.
2. Open Notepad++ (a TSF host with a real caret).
3. Switch to Array / Phonetic mode that exposes 反查 (reverse lookup) and 特別碼.
4. Trigger the **reverse-lookup root keys** popup (e.g. type a character that has reverse-lookup configured). Observe popup position.
   - **Before patch:** popup at window origin or far from caret, does not move when caret moves between lines.
   - **After patch:** popup near caret, follows caret across lines/cells.
5. Trigger the **special-code hint** popup (CandidateHandler 特別碼提示 path). Observe.
   - **Before patch:** same drift.
   - **After patch:** near caret.
6. Repeat the test in the apps from the existing probe verification matrix ([CARET_TRACKING_PROBE.md §9](CARET_TRACKING_PROBE.md)): Word, Excel, Edge, VS Code, Teams, Firefox, CMD. Confirm hint position is correct in each.

### Regression checks

1. Toggle `ShowNotifyDesktop` = ON, repeat steps 4–6. Confirm CHN/ENG notify still works correctly (no regression in the existing path).
2. **LINE Win+. emoji panel** (the canary for destructive-probe regressions): focus a LINE conversation, trigger a reverse-lookup or special-code hint (forces the probe via the new path), then Win+. and select an emoji. Emoji must insert. If it does not, the probe gate widening has somehow re-enabled a destructive path — investigate.
3. Notify-side-by-side with candidate window: open a candidate list, then trigger a hint. Confirm the side-by-side positioning at [UIPresenter.cpp:1544-1549](../src/UIPresenter.cpp#L1544-L1549) still places the notify next to (not on top of) the candidate window.

### Debug trace (optional)

Enable `DEBUG_PRINT` and watch `%APPDATA%\DIME\debug.txt` for:
- `CDIME::_ProbeComposition() pContext = ...` lines firing for reverse-lookup / special-code triggers (should appear after patch, should NOT appear before patch when `ShowNotifyDesktop` is OFF).
- `current caret position from GetGUIThreadInfo` should yield to the probe's `_LayoutChangeNotification` rect when the probe succeeds.

## Implementation status (2026-04-28)

The probe-gate widening described above has been implemented in the working tree.
While testing the patch, debug tracing revealed two **pre-existing** regressions
in the associated-phrase candidate path that were unrelated to #127 itself but
surfaced under the same retest matrix. Both are fixed in this branch.

### Implemented for #127

- **[UIPresenter.cpp `_NotifyChangeNotification`](../src/UIPresenter.cpp#L1051-L1075)** — `SHOW_NOTIFY` timer probe gate restructured around a `needProbe` boolean so `NOTIFY_OTHERS` reaches the probe path when `_GetContextDocument() == nullptr`. CHN/ENG retains the additional `!_IsComposing()` predicate; `NOTIFY_OTHERS` does not.
- **[UIPresenter.cpp `ShowNotifyText`](../src/UIPresenter.cpp#L1520-L1545)** — added `BOOL refreshLocation = (notifyType == NOTIFY_TYPE::NOTIFY_OTHERS);` and changed both `_notifyLocation` seed lines (`GetGUIThreadInfo` and `GetCaretPos` branches) to `refreshLocation || _notifyLocation.x < 0` (Option 1 from the original strategy).
- **[UIPresenter.cpp `ShowNotifyText` immediate-show path](../src/UIPresenter.cpp#L1562-L1567)** — extended the probe gate from `notifyType == NOTIFY_CHN_ENG` to `(NOTIFY_CHN_ENG || NOTIFY_OTHERS)`.

No header changes, no preference changes, no method additions — exactly as scoped.

### Co-fixed regressions found during verification

While verifying #127 with `DEBUG_PRINT` enabled, the trace showed phrase
associative candidates **flashing then vanishing** after every commit, in a
pattern unrelated to the probe gate. Investigation traced it to a **silent
revert** of two earlier guards from commit `a97d654` (2026-04-01, *"Fix caret
tracking, associated phrase anchor, and shadow gap"*). The reverter was
`f898dd6` (*"Suppress CHN/ENG notify popup for non-editable windows on focus
change"*); a75b180 (#126) did not touch the affected block.

The bug was always present between f898dd6 and now, but the #127 retest matrix
exercised it more aggressively (committing from the first-tier candidate list
into a phrase list multiple times in a row), which is why it surfaced now.

#### Regression A — phrase candidate window destroyed immediately after show

**Where:** [`UIPresenter.cpp::_StartCandidateList`](../src/UIPresenter.cpp#L537-L590)

**Symptom in debug log:**

```text
_StartCandidateList()              ← second call, for phrase associative list
_StartLayout()
BeginUIElement(), _uiElementId=N, hresult=0
_EndCandidateList()                ← fires synchronously inside Start
~CCandidateWindow()                ← phrase window destroyed
_StartCandidateList(), hresult = -2147467259  (E_FAIL)
```

**Root cause:** `MakeCandidateWindow` ([UIPresenter.cpp:1604-1608](../src/UIPresenter.cpp#L1604-L1608)) returns `E_FAIL` when `_pCandidateWnd != nullptr`. After committing a candidate the make-phrase path in `_HandleCandidateWorker` ([CandidateHandler.cpp:215-233](../src/CandidateHandler.cpp#L215-L233)) does `_ClearCandidateList` (does **not** dispose `_pCandidateWnd`) → `_SetCandidateText` (repopulates the live window) → `_StartCandidateList(nullptr)` for repositioning. The repositioning Start hits `MakeCandidateWindow`'s "already exists" rejection, gets `E_FAIL`, runs the Exit branch's `_EndCandidateList()`, and tears down the window we just populated. Net effect: phrase candidates render for one paint cycle then disappear.

**Fix:** Restored a97d654's guard:

```cpp
BeginUIElement();
if (_pCandidateWnd)
{
    // Candidate window already exists (e.g. reusing for phrase candidates after
    // commit — _ClearCandidateList + _SetCandidateText repopulated it). Skip
    // creation; MakeCandidateWindow returns E_FAIL when _pCandidateWnd != nullptr,
    // which would tear down the just-populated window via the Exit branch.
    hr = S_OK;
}
else
{
    hr = MakeCandidateWindow(pContextDocument, wndWidth);
    if (FAILED(hr)) goto Exit;
}
```

#### Regression B — phrase window jumped to screen origin (0, 7)

**Where:** [`TfTextLayoutSink.cpp::_GetTextExt`](../src/TfTextLayoutSink.cpp#L252-L283)

**Symptom in debug log (after Regression A was fixed):**

```text
_StartCandidateList()
_StartLayout()                     ← updates _pRangeComposition to nullptr
BeginUIElement() hresult=0
_GetTextExt()                       ← returns S_OK with rect unmodified
_MoveUIWindowToTextExt(), top=0, bottom=0, right=0, left=0
_LayoutChangeNotification(top=0, bottom=0, right=0, left=0)
_Move() x=0, y=7
```

**Root cause:** When `_pRangeComposition == nullptr` (phrase show, no composition to measure), the original code path was:

```cpp
HRESULT hr = S_OK;
...
if (_pRangeComposition == nullptr || FAILED(hr = pContextView->GetTextExt(...)))
{
    return hr;   // returns S_OK without populating lpRect
}
```

The caller's `if (SUCCEEDED(_GetTextExt(&rcTextExt)))` branch fires with `rcTextExt` left at its `{0,0,0,0}` initialization, which then gets passed to `_LayoutChangeNotification` and positions the candidate at screen origin. a97d654's commit message says *"_GetTextExt fails cleanly"* — but the function never actually returned a failure code in this case; it returned success with an unpopulated rect.

The same bug also affected the [`_MoveUIWindowToTextExt`](../src/UIPresenter.cpp#L883-L896) caller, which has the correct `else _LayoutChangeNotification(&_rectCompRange, true)` fallback already wired up — but the fallback was unreachable because `_GetTextExt` falsely reported success.

**Fix:** Made `_GetTextExt` return `E_FAIL` when `_pRangeComposition == nullptr`. Also plugged a pre-existing `pContextView` leak on the failure path:

```cpp
if (_pRangeComposition == nullptr)
{
    pContextView->Release();
    return E_FAIL;
}
if (FAILED(hr = pContextView->GetTextExt(...)))
{
    pContextView->Release();
    return hr;
}
```

After this, both callers' `_rectCompRange` fallbacks fire correctly — the phrase window positions next to the just-committed character using the last saved composition rect.

### Combined effect

| Path | Before | After |
| --- | --- | --- |
| `NOTIFY_OTHERS` with ShowNotifyDesktop=OFF | No probe; stale `_notifyLocation` | Probe runs; rect from `GetTextExt` |
| `NOTIFY_OTHERS` re-show | `_notifyLocation` only re-seeded if `< 0` | Refreshed every show |
| Phrase candidate window after commit | E_FAIL → destroyed | Stays alive; `MakeCandidateWindow` skipped |
| Phrase candidate position | (0, 7) (screen origin) | Anchored at `_rectCompRange` (caret-adjacent) |

### Files changed (final)

| File                      | Change                                                                            |
| ------------------------- | --------------------------------------------------------------------------------- |
| `src/UIPresenter.cpp`     | #127 probe-gate widening (3 sites) + restored a97d654 phrase-window guard         |
| `src/TfTextLayoutSink.cpp`| `_GetTextExt` returns `E_FAIL` when `_pRangeComposition == nullptr` + leak fix    |

### Verification (post-fix)

With `DEBUG_PRINT` re-enabled in `UIPresenter.cpp`, `CandidateWindow.cpp`, `CandidateHandler.cpp`, `KeyHandler.cpp`, `KeyProcesser.cpp`, `Composition.cpp`, `TfTextLayoutSink.cpp`:

1. Type "dj" → 明 candidate → space to commit → phrase associative list (64 items) **stays visible at the correct position next to the committed character**. Repeat with "ev" → 天 and "ant" → 你.
2. Reverse-lookup root keys popup with `ShowNotifyDesktop=OFF` tracks the caret across lines (the original #127 ask).
3. Special-code hint popup with `ShowNotifyDesktop=OFF` tracks the caret.
4. LINE Win+. emoji panel still inserts emoji (probe is still read-only — non-destructive).
5. `ShowNotifyDesktop=ON` retest: CHN/ENG notify behaves identically to before — no regression in the existing path.

`DEBUG_PRINT` toggled back off in all 7 files after verification.

## Notes / follow-ups (out of scope for this fix)

- The orphaned `pContext`/`pDocumentMgr` references in the existing `SHOW_NOTIFY` timer block ([UIPresenter.cpp:1056-1071](../src/UIPresenter.cpp#L1056-L1071)) are not released on the success path. Worth tightening when restructuring for Change B.
- `NOTIFY_OTHERS` is also used by `showFullHalfShapeNotify` ([LanguageBar.cpp:1165-1170](../src/LanguageBar.cpp#L1165-L1170)) and other small notifications. These will *also* benefit from probe-based positioning after the patch — verify they still look right (they should look *better*).
- A longer-term cleanup would decouple "should we show the CHN/ENG notify?" from "should we run probe to seed `_notifyLocation`?" — they are conceptually independent and should not share a preference at all.

---

## Extension — multi-notify (reverse-lookup + special-code coexist)

After the probe widening shipped, a follow-up defect surfaced in Array IM: when both reverse-lookup (反查) and special-code (特別碼) hints fire from the same candidate-finalize edit session, the second `ShowNotifyText` call **overwrote** the first inside the single shared notify window. The user only ever saw one of the two.

Full design rationale, concurrency-invariant audit, and call-site inventory in [docs/MULTI_NOTIFY.md](MULTI_NOTIFY.md). Implementation summary follows.

### Approach

Give each notify source its own `CNotifyWindow` instance, indexed by `NOTIFY_TYPE` itself. The window class is unchanged; only the presenter's ownership and layout grow. Co-existing slots (today: only reverse-lookup + special-code) form a stack anchored to the candidate window's left edge.

### Slot key — split `NOTIFY_OTHERS`, use `NOTIFY_TYPE` as the array index

[src/BaseStructure.h](../src/BaseStructure.h):

```cpp
enum class NOTIFY_TYPE
{
    NOTIFY_CHN_ENG = 0,        // 中文/英數, 全形/半形, 簡/繁 — share slot 0
    NOTIFY_SINGLEDOUBLEBYTE,
    NOTIFY_BEEP,               // sound only — slot reserved, no window
    NOTIFY_REVERSE_LOOKUP,     // 反查根 / wildcard key code
    NOTIFY_SPECIAL_CODE,       // Array 特別碼
    NOTIFY_COUNT               // sentinel — array size
};
```

`NOTIFY_OTHERS` removed. Each `NOTIFY_TYPE` value is now both a semantic tag *and* an array index into the slot table.

### Owner

[src/UIPresenter.h](../src/UIPresenter.h):

```cpp
std::array<std::unique_ptr<CNotifyWindow>, (size_t)NOTIFY_TYPE::NOTIFY_COUNT> _pNotifyWnds;
```

Replaces the single `_pNotifyWnd`. Slots are lazily instantiated by `MakeNotifyWindow(notifyType)` and indexed by `(size_t)notifyType` everywhere. `DisposeNotifyWindow(NOTIFY_COUNT)` (sentinel) wipes all slots; per-type variant wipes one.

### Stack layout — `_RestackVisibleSlots`

Single source of truth for slot positioning. Two rules:

**X rule:**

- Candidate visible → each slot's **right edge** aligned to candidate's left edge minus `SHADOW_SPREAD/2`. Per-slot x = `candLeft - slot.width`.
- No candidate → all slots share `_notifyLocation.x` (caret-anchored, left-aligned).

**Y rule (candidate visible):**

- `growUp = false` (candidate below caret) → first visible slot's **top** at candidate's top; stack grows downward.
- `growUp = true`  (candidate above caret) → first visible slot's **bottom** at candidate's bottom; stack grows upward.

`growUp` is decided by `_DecideStackGrowUp`: candidate visible → mirror its side of the caret; no candidate → flip up only on screen-bottom overflow.

### Right-edge alignment guarantees no overlap

By computing `slotX = candLeft - slot.width` per slot, a slot can never extend past `candLeft` regardless of width. Earlier attempts (left-edge alignment with max-width clamp) were correct in principle but visually displaced narrower slots, which the user disliked. The right-edge rule produces the natural "all slots flush against the candidate" appearance.

### Concurrency invariant (verified — see [MULTI_NOTIFY.md §Verified concurrency invariant](MULTI_NOTIFY.md))

- Only realistic concurrent visible pair: reverse-lookup + special-code, siblings of one Array candidate-finalize step.
- Both share one lifetime: appear together (sync + async within the same edit session), neither auto-hides (`timeToHide=0`), both wiped together by next-keystroke `ClearNotify`.
- CHN/ENG cannot realistically coexist: every key-event path calls `ClearNotify` before `showChnEngNotify`; the rare compartment-change path is suppressed by the generalised precedence guard `_AnyHintSlotVisible()`.

**Consequence:** at most two slots visible at once with shared lifetime — no middle-slot-expires-and-leaves-a-gap scenario. No restack on slot disappearance is needed.

### CHN/ENG precedence — generalised

Old guard ([UIPresenter.cpp:1498-1499](../src/UIPresenter.cpp#L1498-L1499)) blocked `NOTIFY_CHN_ENG` from clobbering an active `NOTIFY_OTHERS`. New guard `_AnyHintSlotVisible()` blocks CHN/ENG when any compositional-hint slot (reverse-lookup or special-code) is visible. Same intent, per-slot semantics.

### Files changed

| File | Change |
| --- | --- |
| [src/BaseStructure.h](../src/BaseStructure.h) | Replace `NOTIFY_OTHERS` with `NOTIFY_REVERSE_LOOKUP` and `NOTIFY_SPECIAL_CODE`; add `NOTIFY_COUNT` sentinel. |
| [src/UIPresenter.h](../src/UIPresenter.h) | `_pNotifyWnd` → `std::array<unique_ptr<CNotifyWindow>, NOTIFY_COUNT> _pNotifyWnds`. Add `_AnyHintSlotVisible`, `_DecideStackGrowUp`, `_ComputeStackPosForNewSlot`, `_RestackVisibleSlots`. Slot-aware overloads of `MakeNotifyWindow` / `SetNotifyText` / `ShowNotify` / `DisposeNotifyWindow`. Saved as UTF-8 with BOM. |
| [src/UIPresenter.cpp](../src/UIPresenter.cpp) | Per-slot indexing throughout; cross-cutting ops (`_SetNotifyTextColor`, `IsNotifyShown`, `ToShowUIWindows`, `ClearAll`) loop the array. `_LayoutChangeNotification` finds first-visible slot as anchor for max-width / candidate-collision math, then translates whole visible stack via `_RestackVisibleSlots`. `ShowNotifyText` does per-slot replace, generalised precedence guard, then `_RestackVisibleSlots`. SHOW_NOTIFY timer routes correct slot via `(NOTIFY_TYPE)lParam`. Probe-widening updated to enumerate the new hint types. |
| [src/ReverseConversion.cpp:119](../src/ReverseConversion.cpp#L119) | `NOTIFY_OTHERS` → `NOTIFY_REVERSE_LOOKUP`. |
| [src/CandidateHandler.cpp:173,186](../src/CandidateHandler.cpp#L173) | Line 173 (wildcard key code) → `NOTIFY_REVERSE_LOOKUP`. Line 186 (Array special-code) → `NOTIFY_SPECIAL_CODE`. |
| [src/tests/NotificationWindowIntegrationTest.cpp](../src/tests/NotificationWindowIntegrationTest.cpp), [src/tests/UIPresenterIntegrationTest.cpp](../src/tests/UIPresenterIntegrationTest.cpp) | Test references `NOTIFY_OTHERS` → `NOTIFY_REVERSE_LOOKUP`. |

### Behaviour matrix

| Scenario | Before extension | After extension |
| --- | --- | --- |
| Reverse-lookup only | One notify near caret | One notify near caret (unchanged) |
| Special-code only | One notify near caret | One notify near caret (unchanged) |
| Both fire (Array finalize) | Second clobbers first; one visible | Both visible, stacked; right edges aligned at candidate's left |
| Both visible, candidate above caret (no room below) | n/a | Stack grows upward from candidate's bottom; first slot's bottom at candidate.bottom |
| Both visible, candidate below caret | n/a | Stack grows downward from candidate's top; first slot's top at candidate.top |
| CHN/ENG fires while a hint slot is visible | Existing guard blocked it | Generalised guard still blocks it |
| Next keystroke after both shown | `ClearNotify` wipes the single window | `ClearNotify` disposes all slots; layout sink ended once |

### Extension verification

1. Array IM: type a syllable that triggers both reverse-lookup and special-code → two notify boxes visible, right edges flush against candidate's left edge, stacked vertically (lower index closer to candidate). Confirmed via screenshots.
2. Caret near top of edit area (candidate below caret) → stack grows downward from candidate top. Confirmed.
3. Caret near bottom of edit area (candidate above caret) → stack grows upward from candidate bottom; first slot's bottom flush with candidate's bottom. Confirmed via screenshot.
4. CHN/ENG via Shift while hints visible → keystroke clears hints first, then CHN/ENG appears alone in slot 0. Existing precedence path.
5. CHN/ENG via language-bar click while hints visible → `_AnyHintSlotVisible()` suppresses the CHN/ENG show. Hints remain.
6. LINE Win+. emoji panel after triggering hints → emoji inserts. Probe is still read-only.
7. Build verified Debug x64 + Debug Win32 clean (Release blocked by pre-existing `BuildInfo.cmd` infrastructure issue, unrelated).

---

## Follow-up — Firefox notify position in Firefox (2026-04-29 → 2026-04-30)

### Problem

After the multi-notify extension landed, reverse-lookup (slot 3) and NOTIFY_BEEP
appeared at wrong screen positions in Firefox (URL bar and web search fields).
Specifically:

- With associative candidate **OFF**: both hit near screen `(0,0)` / Firefox window origin.
- With both reverse-lookup **and** special-code slots **ON**: slots appeared at correct
  positions individually but were **X-misaligned** relative to each other.

### Root causes (three layers)

**Layer 1 — `refreshLocation` overwrote cached `_notifyLocation` with zero**

Firefox returns `rcCaret={0,0,0,0}` from `GetGUIThreadInfo` (no Win32 system caret).
After `refreshLocation` was added for hint slots (REVERSE_LOOKUP, SPECIAL_CODE, BEEP),
every hint show unconditionally overwrote `_notifyLocation` with the result, discarding
the valid value that had been seeded by the probe during active composition. The probe
then also failed post-commit (`TS_E_NOLAYOUT`), so the overwrite was never corrected.

**Layer 2 — `validCaretPos` check was after `ClientToScreen`**

First attempted fix: check `validCaretPos = (pt->x != 0 || pt->y != 0)` after
`ClientToScreen`. This was wrong: Firefox's `rcCaret={0,0,0,0}` client-space
coordinates are transformed by `ClientToScreen` to the **screen position of the
Firefox window's client origin** (e.g. `(850, 150)`) — non-zero, so the check passed
and `_notifyLocation` was still overwritten with the browser window's top-left corner.
Result: both slots appeared at the top-left of the Firefox page, not near the caret.

#### Layer 3 — initialization guard also wrote browser window origin

Even with the `validCaretPos` check before `ClientToScreen`, the initialization guard
`_notifyLocation.x < 0` still fired on first use and wrote `pt->x` (now the Firefox
window origin after `ClientToScreen`). `_notifyLocation` was then set to the window
origin on first hint, and both slots went to the top-left corner of the page.

#### Layer 4 — slot X misalignment when two slots are shown

The no-candidate branch called `_ComputeStackPosForNewSlot` + `slot->_Move` for the
new slot only. If `_notifyLocation.x` changed between the first and second
`ShowNotifyText` calls (e.g. probe fired in-between and called `_LayoutChangeNotification`),
the two slots ended up with different X values. Replacing with `_RestackVisibleSlots`
repositions all visible slots from the same anchor on every add.

### Debug trace (debug.txt)

```text
current caret position from GetGUIThreadInfo, x = 0, y = 0   ← Firefox zero caret
CUIPresenter::ShowNotify(slot=3)
CDIME::_ProbeComposition() pContext = bb715190
CDIME::_ProbeCompositionRangeNotification() GetTextExt hr=0x80040206 top=0 bottom=0 left=0 right=0
```

`0x80040206` = `TS_E_NOLAYOUT`. TSF layout is not recalculated in Firefox after
composition commit. `GetTextExt` fails, so `_LayoutChangeNotification` is never called.

### Fix (final, landing 2026-04-30)

**A — check raw `rcCaret` BEFORE `ClientToScreen`**

```cpp
// validCaretPos must be determined from the raw rcCaret fields, not from pt after
// ClientToScreen — Firefox's {0,0,0,0} maps to the browser window screen origin.
BOOL validCaretPos = (guiInfo->rcCaret.left   != 0 || guiInfo->rcCaret.top    != 0
                   || guiInfo->rcCaret.right  != 0 || guiInfo->rcCaret.bottom != 0);
pt->x = guiInfo->rcCaret.left;
pt->y = guiInfo->rcCaret.bottom;
ClientToScreen(parentWndHandle, pt);
```

**B — fully gate `_notifyLocation` updates on `validCaretPos` (no init-guard exception)**

```cpp
if (validCaretPos) {
    if (refreshLocation || _notifyLocation.x < 0) _notifyLocation.x = pt->x;
    if (refreshLocation || _notifyLocation.y < 0) _notifyLocation.y = pt->y;
}
```

When `validCaretPos=FALSE`, `_notifyLocation` is never written from this path — not
even on first use. The probe's `_LayoutChangeNotification` is the only writer when
Firefox is the host. The same guard is applied to the `GetCaretPos` branch.

**C — `_candLocation` fallback for uninitialised `_notifyLocation`**

```cpp
// If _notifyLocation was never seeded (no valid caret and no prior probe),
// fall back to the last known candidate window position as approximation.
if (_notifyLocation.x < 0 && _candLocation.x >= 0)
    _notifyLocation = _candLocation;
```

**D — use `_RestackVisibleSlots` in the no-candidate branch**

Replaced `_ComputeStackPosForNewSlot` + `slot->_Move` (positions new slot only) with
`_RestackVisibleSlots` (repositions all visible slots), ensuring consistent X alignment
regardless of when `_notifyLocation` changes relative to each `ShowNotifyText` call.

### Verified

Firefox URL bar and Yahoo/Google web search boxes (Firefox), associative candidate ON and OFF:

- Reverse-lookup and special-code notifies appear near the committed character. ✓
- Both slots X-aligned when shown together. ✓
- NOTIFY_BEEP unaffected. ✓

`DEBUG_PRINT` disabled in `UIPresenter.cpp` and `Composition.cpp` after verification.

---

### Design history (decisions taken and rejected)

- **Multi-line single window** considered and rejected: ~180 lines of changes inside `CNotifyWindow` (rendering, sweep timer, prefix table, multi-line measurement). Concentrates risk in the rendering window.
- **Parallel `NOTIFY_SLOT` enum** considered and rejected: dual axis (type + slot) was redundant since `NOTIFY_TYPE` already carries semantic meaning. Splitting `NOTIFY_OTHERS` into its real producers cleans up an existing wart.
- **Left-edge alignment with max-width clamp** implemented and reverted at user request: produced visually-displaced narrower slots. Right-edge alignment is the user's preferred final design.
- **Per-slot expiry + restack-on-hide** considered and rejected: verified concurrency invariant rules out middle-slot-expires; both hint slots share lifetime via `ClearNotify`. Saves a `HIDDEN` callback and restack pass.
- **Per-slot show/hide preferences** considered and rejected as scope creep. The original report did not ask for finer-grained preference control; left as a future enhancement if requested.
