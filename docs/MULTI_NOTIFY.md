# Multi-Notify — Plan

- **Status:** Planning
- **Driver:** Issue #127 extension. After the per-`NOTIFY_OTHERS` probe widening (already shipped), reverse-lookup and special-code hints both track the caret correctly. The remaining defect: in Array IM the special-code hint **overwrites** the reverse-lookup hint inside the single shared notify window, so the user only ever sees one of the two.
- **Goal:** show co-occurring notifications as separate windows that share a stack anchored to the caret. Each window is an instance of the existing `CNotifyWindow`; only the presenter's ownership and layout grow.

## Context

`CUIPresenter` today owns one `std::unique_ptr<CNotifyWindow>` ([UIPresenter.h:193](../src/UIPresenter.h#L193)). Every `ShowNotifyText` overwrites the single window's text via `_SetString` ([NotifyWindow.cpp:637-652](../src/NotifyWindow.cpp#L637-L652)). When reverse-lookup ([ReverseConversion.cpp:119](../src/ReverseConversion.cpp#L119)) and special-code ([CandidateHandler.cpp:186](../src/CandidateHandler.cpp#L186)) fire from the same candidate-finalize edit session, the second clobbers the first.

The fix is to give each notify source its own `CNotifyWindow` instance, indexed by `NOTIFY_TYPE` itself. The window class needs no changes — only the presenter and a few enum/call-site updates.

### Verified concurrency invariant

A full audit of every `ShowNotifyText` call site (see [docs/#127_ISSUE.md](#127_ISSUE.md) for the inventory) shows:

- The **only** realistic concurrent visible pair is reverse-lookup + special-code (siblings of one Array candidate-finalize step).
- Both share one lifetime: both appear together, neither auto-hides (`timeToHide=0`), both vanish on the next keystroke via `ClearNotify` ([UIPresenter.cpp:1472](../src/UIPresenter.cpp#L1472), called from [KeyEventSink.cpp:385,427](../src/KeyEventSink.cpp#L385)).
- CHN/ENG cannot realistically coexist: every key-event path calls `ClearNotify` before `showChnEngNotify`, and the rare compartment-change path is suppressed by the existing precedence guard at [UIPresenter.cpp:1498-1499](../src/UIPresenter.cpp#L1498-L1499).

**Consequence:** at most two slots visible at once, and they share a lifetime. There is no middle-slot-expires-and-leaves-a-gap scenario, so we do not need to relayout the stack when a slot disappears.

## Design

### Slot key — split `NOTIFY_OTHERS`, use `NOTIFY_TYPE` as the array index

[src/BaseStructure.h:219-225](../src/BaseStructure.h#L219-L225) becomes:

```cpp
enum class NOTIFY_TYPE
{
    NOTIFY_CHN_ENG = 0,        // 中文/英數, 全形/半形, 簡/繁  (mutually exclusive — share slot 0)
    NOTIFY_SINGLEDOUBLEBYTE,
    NOTIFY_BEEP,               // sound — slot reserved but unused
    NOTIFY_REVERSE_LOOKUP,     // 反查根 / wildcard key code
    NOTIFY_SPECIAL_CODE,       // Array 特別碼
    NOTIFY_COUNT               // sentinel
};
```

`NOTIFY_OTHERS` is removed. `NOTIFY_TYPE` continues to be passed by every caller and is now both the semantic tag *and* the array index.

### Owner

```cpp
// CUIPresenter.h replaces _pNotifyWnd
std::array<std::unique_ptr<CNotifyWindow>, (size_t)NOTIFY_TYPE::NOTIFY_COUNT> _pNotifyWnds;
```

A slot's window is created lazily on first `ShowNotifyText` for that type and reused/disposed by the existing single-window code (just per-slot now).

### `CNotifyWindow` changes — minimal

No structural changes. Class already stores `_notifyType` and uses HWND-scoped timers; reusable as-is for N instances. Two trivial additions for the presenter to identify which slot fired a callback (only needed if we keep one shared `_NotifyWndCallback`):

- The existing `GetNotifyType()` ([NotifyWindow.h:80](../src/NotifyWindow.h#L80)) already provides slot identity. The shared callback at [UIPresenter.cpp:1422](../src/UIPresenter.cpp#L1422) takes `pv = this (presenter)`; it can identify the firing window via `(LPARAM)_notifyType` already passed at [NotifyWindow.cpp:187](../src/NotifyWindow.cpp#L187). **No new code in `CNotifyWindow`.**

### Stack layout

Anchor: `_notifyLocation` (caret-bottom-left), set by the existing probe / fallback in `ShowNotifyText`.

**Direction rule** (downward by default; upward when the candidate is above the caret, or when no candidate and the stack overflows the screen bottom):

```cpp
bool growUp = false;
if (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
{
    RECT cr; _pCandidateWnd->_GetWindowRect(&cr);
    growUp = (cr.top < _notifyLocation.y);
}
else
{
    int totalH = _SumVisibleSlotHeights();
    growUp = (_notifyLocation.y + totalH > _ScreenBottom());
}
```

**Position-at-show-time** (no restack-on-hide). When a new slot becomes visible:

```cpp
POINT CUIPresenter::_ComputeStackPosForNewSlot(NOTIFY_TYPE newSlot, bool growUp)
{
    int offset = 0;
    for (size_t i = 0; i < _pNotifyWnds.size(); ++i)
    {
        if (i == (size_t)newSlot) continue;
        auto& w = _pNotifyWnds[i];
        if (w && ::IsWindowVisible(w->_GetWnd()))
            offset += w->_GetHeight() + GAP;
    }
    int h = _pNotifyWnds[(size_t)newSlot]->_GetHeight();
    return { _notifyLocation.x,
             growUp ? _notifyLocation.y - h - offset
                    : _notifyLocation.y + offset };
}
```

`GAP = SHADOW_SPREAD` (existing constant, keeps adjacent shadows from visually merging).

**Caret-motion translate.** `_LayoutChangeNotification` ([UIPresenter.cpp:973](../src/UIPresenter.cpp#L973)) updates `_notifyLocation` then walks visible slots in index order, applying the stack formula with the new anchor. This replaces today's single `_pNotifyWnd->_Move` line; the loop body is the same `_Move` call, repeated over the visible set.

### Candidate-window collision

Existing single-notify code positions the notify beside the candidate when the candidate is visible ([UIPresenter.cpp:984-1009](../src/UIPresenter.cpp#L984-L1009), [UIPresenter.cpp:1544-1549](../src/UIPresenter.cpp#L1544-L1549)). With multiple notifies, the same logic is applied to **the stack as a whole** (treat the visible set as one virtual rect of `max(width)` × `Σ heights + Σ gaps`):

- Decide left-of vs right-of using existing `_notifyLocation.x < stackWidth` test.
- The chosen x is then used for every slot in the loop.

### CHN/ENG precedence

The precedence guard at [UIPresenter.cpp:1498-1499](../src/UIPresenter.cpp#L1498-L1499) stays, generalised to "if `notifyType == NOTIFY_CHN_ENG` and any compositional-hint slot is visible, skip" — preserves today's behaviour with multiple slots.

```cpp
if (notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG && _AnyHintSlotVisible())
    return;
```

`_AnyHintSlotVisible()` is a one-liner that checks `_pNotifyWnds[REVERSE_LOOKUP]` and `_pNotifyWnds[SPECIAL_CODE]`.

### What the plan does **not** add

- No `NOTIFY_SLOT` enum (the type IS the slot).
- No new preferences. Per-slot show/hide is out of scope.
- No restack on slot disappearance (verified: middle-slot-expires cannot happen with the audited concurrency model).
- No `HIDDEN` callback from `CNotifyWindow`.
- No multi-line rendering inside `CNotifyWindow`.
- No registry migration (the `ShowNotifyDesktop` key keeps its current meaning).

## Files to modify

| File | Change |
|---|---|
| [src/BaseStructure.h](../src/BaseStructure.h) | Replace `NOTIFY_OTHERS` with `NOTIFY_REVERSE_LOOKUP` and `NOTIFY_SPECIAL_CODE`; add `NOTIFY_COUNT` sentinel. Keep `NOTIFY_CHN_ENG = 0` as the most-common slot. |
| [src/UIPresenter.h](../src/UIPresenter.h) | Replace `_pNotifyWnd` with `std::array<std::unique_ptr<CNotifyWindow>, NOTIFY_COUNT> _pNotifyWnds`. Add private helpers `_ComputeStackPosForNewSlot`, `_AnyHintSlotVisible`, `_DecideStackDirection`. |
| [src/UIPresenter.cpp](../src/UIPresenter.cpp) | (1) `MakeNotifyWindow` / `SetNotifyText` / `ShowNotify` / `ClearNotify` / `DisposeNotifyWindow` / `ClearAll` / `_SetNotifyTextColor` (line 719) — replace `_pNotifyWnd` access with per-slot indexing. Most become small loops. (2) `ShowNotifyText`: route to `_pNotifyWnds[(size_t)notifyType]`; after creating/sizing the slot's window, call `_ComputeStackPosForNewSlot` and `_Move`, then `_Show`. (3) `_LayoutChangeNotification`: walk visible slots, apply the stack formula with the new anchor. (4) `_NotifyChangeNotification` (`SHOW_NOTIFY` timer case at line 1051): identify firing slot from `lParam` (`(NOTIFY_TYPE)lParam`) and operate on that slot only. (5) Generalise the precedence guard at line 1498. |
| [src/UIPresenter.cpp:1554-1556](../src/UIPresenter.cpp#L1554-L1556) (the `NOTIFY_OTHERS` probe widening from #127) | Update condition to `notifyType == NOTIFY_REVERSE_LOOKUP || notifyType == NOTIFY_SPECIAL_CODE` (or any compositional hint helper). Functionally identical. |
| [src/ReverseConversion.cpp:119](../src/ReverseConversion.cpp#L119) | `NOTIFY_OTHERS` → `NOTIFY_REVERSE_LOOKUP`. |
| [src/CandidateHandler.cpp:173,186](../src/CandidateHandler.cpp#L173) | Line 173 (wildcard key code) → `NOTIFY_REVERSE_LOOKUP` (alternative producer for the same slot). Line 186 (special-code) → `NOTIFY_SPECIAL_CODE`. |
| [src/LanguageBar.cpp](../src/LanguageBar.cpp) | No call-site change. `showChnEngNotify`, `showFullHalfShapeNotify`, simplified/traditional notify all already pass `NOTIFY_CHN_ENG` and continue to share slot 0. |
| [src/DIME.cpp:1119](../src/DIME.cpp#L1119) | Continues to use `NOTIFY_BEEP` (slot reserved; current behaviour unchanged). |

Estimated change size: roughly 70 lines net across the files above. `CNotifyWindow.{h,cpp}` is **not** touched.

## Reused utilities

- `CNotifyWindow` lifetime, rendering, shadow, DPI, light-dismiss — used as-is, one instance per slot.
- `IsWindowVisible(hwnd)` — single source of truth for "is this slot occupying stack space."
- Existing `_LayoutChangeNotification` ([UIPresenter.cpp:973](../src/UIPresenter.cpp#L973)) caret-rect / candidate-collision / clipping logic — adapted from single-window to per-slot loop with the same math.
- Existing `SHADOW_SPREAD` constant — gap between stacked notifies.
- `_NotifyWndCallback` ([UIPresenter.cpp callback param at line 1422](../src/UIPresenter.cpp#L1422)) — shared across all slots; uses `lParam == (LPARAM)_notifyType` to identify firing slot.
- The `NOTIFY_OTHERS`-probe widening from issue #127 stays — just renamed/extended to the two specific compositional-hint types.

## Verification

### Build

`MSBuild src/DIME.sln /t:DIME /p:Configuration=Debug /p:Platform=x64` (VS2026 / v145 toolset).

### Manual repro — Array IM (the headline case)

1. Settings: leave `ShowNotifyDesktop` at any value (no longer matters for this defect).
2. Open Notepad++ / Word / VS Code.
3. Switch to Array IM. Compose and select a candidate that triggers **both** reverse-lookup and special-code hints.
4. **Expected after fix:** two notify boxes, stacked vertically (downward by default), both visible until the next keystroke. Text in each is its own message; neither has been clobbered.
5. **Before fix:** only one notify visible (special-code overwrites reverse-lookup, or vice-versa depending on call order).

### Stack direction

6. In Notepad++ near the bottom of the editor area (force candidate to render *above* the caret): trigger the same Array finalize. Stack should grow **upward** to match the candidate side. Slot 0 closest to caret, others further away.
7. Top of editor, plenty of room below: stack grows downward (default).
8. No candidate visible (rare for this flow, but possible for any slot-1 hint outside Array): stack still anchored to caret, grows downward unless the screen-bottom overflow check trips.

### Caret motion

9. While both notifies are visible, type a character that doesn't dismiss them (this is moot — any keystroke clears all notifies via `ClearNotify`). To test caret-motion translate, alternative: trigger a long-running scenario where `_LayoutChangeNotification` fires (e.g. window resize that moves the caret). Confirm the whole stack translates together; no slot is left behind.

### CHN/ENG precedence

10. With reverse-lookup or special-code visible, click the language bar to toggle CHN/ENG. The CHN/ENG show should be **suppressed** (existing guard, generalised). The compositional-hint slots should remain visible.
11. Press Shift to toggle CHN/ENG via key event. The keystroke triggers `ClearNotify` first (existing behaviour), so all slots vanish, then CHN/ENG appears alone in slot 0. No coexistence.

### Issue #127 regression

12. Set `ShowNotifyDesktop` = OFF. Repeat steps 3–8. The probe widening shipped for #127 must continue to position both slots accurately at the caret. The two slots share `_notifyLocation`, so caret-tracking improvements apply to both.

### LINE emoji / probe canary

13. Focus a LINE conversation, trigger reverse-lookup + special-code (forces both probes), then Win+. → emoji panel → select an emoji. Emoji must insert. (Same canary as #127.)

### Theme / DPI

14. Toggle dark/light theme while a stack is visible: every slot's text colour and fill colour update (loop over `_pNotifyWnds`). Today's single-window theme refresh at [UIPresenter.cpp:719-723](../src/UIPresenter.cpp#L719-L723) becomes a 3-line loop.
15. Move the host window to a different DPI monitor: existing per-window `WM_DPICHANGED` handler ([NotifyWindow.cpp:450-463](../src/NotifyWindow.cpp#L450-L463)) fires per slot. No new code needed.

### Debug trace (optional)

Add a single-line `debugPrint` in `ShowNotifyText` and `ClearNotify` indicating which slot:

- `ShowNotifyText: slot=%d (type=%s) text=%s`
- `ClearNotify: slot=%d disposed`

Eases problem-report triage in `%APPDATA%\DIME\debug.txt`.

## Risks / open items

- **`NOTIFY_OTHERS` removal is breaking inside the codebase.** Any leftover reference compiles-error immediately (`enum class`), so the compiler enforces full migration. Grep before commit.
- **Wire-format reuse of `NOTIFY_TYPE` in `lParam`.** [NotifyWindow.cpp:187](../src/NotifyWindow.cpp#L187) passes `(LPARAM)_notifyType`. Reordering the enum (we add new values) is process-local — no persistence — so safe.
- **Concurrency invariant is documented, not enforced.** A future hint with `timeToHide > 0` or with a different lifetime than reverse-lookup/special-code could create the middle-slot-expires scenario. If that ever lands, revisit: either (a) make all hint slots share lifetime, or (b) introduce restack-on-hide. State the assumption in code-comment form near `_pNotifyWnds` so future maintainers see it.
- **Stack-as-block beside candidate** assumes `max(slotWidth)` doesn't change between layout passes. If a slot's text auto-resizes via `_DrawText`'s width path ([NotifyWindow.cpp:551-554](../src/NotifyWindow.cpp#L551-L554)), the next caret-motion translate corrects the position. Slight visual lag on text-resize, acceptable.
- **Array slot for `NOTIFY_BEEP`** is allocated but never instantiates a window (BEEP doesn't have a popup). 8 wasted bytes per `CUIPresenter` — trivial.

## Sequencing

1. **Refactor commit:** introduce `NOTIFY_REVERSE_LOOKUP` / `NOTIFY_SPECIAL_CODE` / `NOTIFY_COUNT`; convert `_pNotifyWnd` to the array; update every cross-cutting site to per-slot loops; **all current callers continue to use `NOTIFY_CHN_ENG`**, so behaviour is unchanged (single visible slot 0). Build, smoke-test CHN/ENG and full/half toggles in Notepad++ — must look identical to today.
2. **Wiring commit:** point [ReverseConversion.cpp:119](../src/ReverseConversion.cpp#L119) and [CandidateHandler.cpp:173](../src/CandidateHandler.cpp#L173) at `NOTIFY_REVERSE_LOOKUP`, point [CandidateHandler.cpp:186](../src/CandidateHandler.cpp#L186) at `NOTIFY_SPECIAL_CODE`, generalise the CHN/ENG precedence guard, add the stack-direction and stack-position helpers. The Array IM concurrent-pair scenario starts displaying both slots.

Two commits, each individually buildable and bisectable.
