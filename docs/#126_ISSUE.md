# Issue #126 — Host application crashes when typing numpad digits after a committed Chinese composition

- **Repository:** jrywu/DIME
- **Reporter:** SimonChung (additional confirmations from WuPai500, carlos06tw)
- **Affected version:** 1.3.616
- **Severity:** High (host-application crash, possible data loss)
- **Status:** Fixed

## Symptom

In Chinese input mode (Phonetic, Dayi, Array — any IME mode that triggers the *associated phrase* popup):

1. User composes and commits a Chinese character.
2. **Immediately** after commit (while the associated-phrase popup is being shown), user types a digit using the **right-side numeric keypad**.
3. The host application (Notepad++, VS Code, Word, RealVNC Viewer, etc.) crashes (`閃退`).

Reproduction string from the issue: `我當年18歲時`.

Pressing the **top-row** digit keys does not crash — only the numpad. Pressing **space first** (which dismisses the associated-phrase popup), then numpad digits, also does not crash.

## Root Cause

Two cooperating defects, both visible only when the associated-phrase candidate window is showing:

### Defect 1 — `_pCandidateWnd` null dereference

`CUIPresenter::_SetCandidateSelectionInPage` ([UIPresenter.h:130](../src/UIPresenter.h#L130)) was an unchecked pointer dereference:

```cpp
BOOL _SetCandidateSelectionInPage(int nPos) { return _pCandidateWnd->_SetSelectionInPage(nPos); }
```

It is called from `CDIME::_HandleCandidateSelectByNumber` ([CandidateHandler.cpp:287](../src/CandidateHandler.cpp#L287)) to confirm a candidate by selkey. If `_pCandidateWnd == nullptr` at that moment, the IME (which is loaded in-process inside the host) crashes with an access violation, taking the host with it.

### Defect 2 — Stale `_candidateMode = CANDIDATE_PHRASE` while `_pCandidateWnd == nullptr`

Empirical trace from `%APPDATA%\DIME\debug.txt` showed:

```text
IsVirtualKeyNeed() uCode=0x61 wch=0x0031 fComposing=0 candidateMode=2 candiCount=0 ...
 CDIME::OnKeyDown() eating the key
CDIME::_HandleCandidateSelectByNumber() iSelectAsNumber = 0
[crash]
```

`candidateMode = 2 = CANDIDATE_PHRASE` while `candiCount = 0` means the IME thinks the phrase popup is active, but `_pCandidateWnd` (the actual candidate window) has either not yet been created or has been torn down without the mode flag being cleared. Make-phrase code in [CandidateHandler.cpp](../src/CandidateHandler.cpp) sets `_candidateMode = CANDIDATE_PHRASE` *before* `_StartCandidateList` finishes constructing the window — a brief window in which the state is inconsistent.

### Defect 3 — `IsRange` ignores the `NumericPad` user preference

In `CCandidateRange::IsRange` ([BaseStructure.cpp:285-291](../src/BaseStructure.cpp#L285-L291)) numpad keys (`VK_NUMPAD0..VK_NUMPAD9`) are unconditionally accepted as candidate-by-index selectors:

```cpp
else if ((VK_NUMPAD0 <= vKey) && (vKey <= VK_NUMPAD9))
{
    if ((vKey - VK_NUMPAD0) == _CandidateListIndexRange.GetAt(i)->Index)
        return TRUE;
}
```

This violates the documented `NumericPad` preference. Users who set `NumericPad = NUMERIC_PAD_MUMERIC` (the default — *"numpad always types digits"*) still had numpad keys hijacked as candidate selectors whenever a candidate window was showing. Top-row digits also reach selkey selection, but via the `Printable` match path which reflects the keyboard layout — the user's report of "top-row OK, numpad bad" is consistent with the user expecting numpad to be a digit per their pref.

### Why "space first then numpad" did not crash

`VK_SPACE` in `CANDIDATE_PHRASE` mode hits a dedicated case in `IsVirtualKeyNeed` that returns `INVOKE_COMPOSITION_EDIT_SESSION + FUNCTION_CANCEL`. That path goes through `_HandleCancel` → `_DeleteCandidateList`, which sets `_candidateMode = CANDIDATE_NONE` and tears down state safely (and tolerates `_pCandidateWnd == nullptr`). After that, numpad keys arrive with `candidateMode = NONE` and flow to the host as plain digits.

Numpad keys go through `IsKeystrokeRange` → `FUNCTION_SELECT_BY_NUMBER` → `_HandleCandidateSelectByNumber` → `_SetCandidateSelectionInPage` → null deref.

## Fix

Three independent, layered changes:

### 1. Reset stale `_candidateMode` in `_IsKeyEaten`

[src/KeyEventSink.cpp](../src/KeyEventSink.cpp) — when the IME claims a candidate mode is active but `GetCount()` reports zero candidates in the UI, the state is stale; reset to `CANDIDATE_NONE` before passing the key to `IsVirtualKeyNeed`. This is the primary fix and ensures the key flows to the host normally rather than being eaten as a phantom selkey.

```cpp
if (_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE && candiCount == 0)
{
    _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
    _isCandidateWithWildcard = FALSE;
}
```

### 2. Respect the `NumericPad` preference in `IsKeystrokeRange`

[src/KeyProcesser.cpp](../src/KeyProcesser.cpp) — short-circuit numpad keys when the user has selected `NUMERIC_PAD_MUMERIC`. Numpad never acts as a candidate selkey under that preference, regardless of whether a candidate window is showing.

```cpp
if (uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE &&
    CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC)
{
    return FALSE;
}
```

### 3. Defensive null-guard on `_SetCandidateSelectionInPage`

[src/UIPresenter.h:130](../src/UIPresenter.h#L130) — convert the unchecked `_pCandidateWnd->_SetSelectionInPage` call into a null-guarded ternary so any future state-desync regression cannot crash the host.

```cpp
BOOL _SetCandidateSelectionInPage(int nPos) {
    return _pCandidateWnd ? _pCandidateWnd->_SetSelectionInPage(nPos) : FALSE;
}
```

### Behavior matrix after the fix

| State                    | Pref                | Key     | Result                                          |
| ------------------------ | ------------------- | ------- | ----------------------------------------------- |
| Live phrase popup        | `NUMERIC` (default) | numpad  | Not eaten — host receives digit                 |
| Live phrase popup        | `NUMERIC` (default) | top-row | Selects candidate (Printable match — unchanged) |
| Live phrase popup        | `RADICAL`           | numpad  | Selects candidate (unchanged)                   |
| Stale phrase mode (race) | any                 | numpad  | State reset to NONE; host receives digit        |
| No candidate mode        | any                 | numpad  | Host receives digit (always worked)             |

## Verification

Manual repro of `我當年18歲時`:

1. In any IME mode with associated-phrase enabled, default `NumericPad = NUMERIC`.
2. Open Notepad++ / VS Code / Word.
3. Compose and commit `我`, `當`, `年` so the associated-phrase popup is on screen.
4. Press numpad `1`, then numpad `8`.
5. Continue with `歲時`.

Expected: full string `我當年18歲時` typed, host application stable, phrase popup dismissed when the digit is typed (because `_candidateMode` resets to `NONE`).

Repeat with top-row digits to confirm no regression in candidate-selection behavior.

## Files changed

- [src/KeyEventSink.cpp](../src/KeyEventSink.cpp) — stale-state reset in `_IsKeyEaten`.
- [src/KeyProcesser.cpp](../src/KeyProcesser.cpp) — `NumericPad=NUMERIC` short-circuit in `IsKeystrokeRange`.
- [src/UIPresenter.h](../src/UIPresenter.h) — null-guard on `_SetCandidateSelectionInPage`.

## Code review

This section walks through each change, explaining what it does, why it is necessary, and what it does **not** do.

### 1. `src/KeyEventSink.cpp` — stale-state reset in `CDIME::_IsKeyEaten`

```cpp
UINT candiCount = 0;
INT candiSelection = -1;
if (_pUIPresenter)
{
    _pUIPresenter->GetCount(&candiCount);
    candiSelection = _pUIPresenter->_GetCandidateSelection();
}

// Issue #126: stale _candidateMode without an actual candidate window.
// Make-phrase sets _candidateMode = PHRASE before _StartCandidateList
// completes, leaving a window where candidate UI is not yet alive but
// _candidateMode says PHRASE. A numpad key that hits IsKeystrokeRange
// would then be eaten as SELECT_BY_NUMBER and crash on null _pCandidateWnd.
// If the candidate UI is not actually present, treat as no candidate mode.
if (_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE && candiCount == 0)
{
    debugPrint(L"_IsKeyEaten: stale _candidateMode=%d with candiCount=0, resetting to NONE",
        (int)_candidateMode);
    _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
    _isCandidateWithWildcard = FALSE;
}
```

**What it does.** Just after the IME computes `candiCount` from the UI presenter, the code checks for a state desync: any non-`NONE` `_candidateMode` while the candidate window reports zero candidates. When detected, the IME's mode flags are forced back to `NONE` *before* `IsVirtualKeyNeed` is called.

**Why it is necessary.** This is the primary fix. The crash trace showed `candidateMode=2 (PHRASE)` while `candiCount=0`. Without this reset, `IsVirtualKeyNeed` enters its phrase-candidate handling block, `IsKeystrokeRange` accepts a numpad VK as a selkey-by-index, the key is eaten as `FUNCTION_SELECT_BY_NUMBER`, and `_HandleCandidateSelectByNumber` ultimately null-derefs `_pCandidateWnd`. By recognising the desync early and clearing the flags, every downstream branch sees a consistent "no candidate" state.

**Why this location.** `_IsKeyEaten` is the single funnel through which both `OnTestKeyDown` and `OnKeyDown` route their keystroke decisions. Resetting here means *one* reset covers test and dispatch paths.

**What it does not do.** It does not fix the root cause (the producer that left `_candidateMode = PHRASE` while `_pCandidateWnd == nullptr`); see the Notes section. It also does not modify the actual TSF text store — purely an in-memory state correction.

**`debugPrint`.** Kept in the source as a no-op (it expands to a stub when `DEBUG_PRINT` is undefined). Useful for diagnosing any future state-desync regression without requiring a code change.

---

### 2. `src/KeyProcesser.cpp` — respect `NumericPad` preference in `CCompositionProcessorEngine::IsKeystrokeRange`

```cpp
pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;

// Respect NumericPad preference: when numpad is configured to always type
// digits (NUMERIC) it must not act as a candidate selkey, even with a
// phrase / candidate window showing. (Issue #126)
if (uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE &&
    CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_MUMERIC)
{
    return FALSE;
}

if (_pActiveCandidateListIndexRange->IsRange(uCode, *pwch, Global::ModifiersValue, candidateMode))
{
    ...
}
```

**What it does.** Short-circuits `IsKeystrokeRange` to return `FALSE` when (a) the virtual-key code is in the numpad range `VK_NUMPAD0..VK_DIVIDE`, and (b) the user's `NumericPad` preference is `NUMERIC_PAD_MUMERIC` (numpad-always-types-digits — the default).

**Why it is necessary.** Even with the stale-state reset in change 1, the bug would still appear when there *are* real phrase candidates: `CCandidateRange::IsRange` ([BaseStructure.cpp:285-291](../src/BaseStructure.cpp#L285-L291)) unconditionally maps `VK_NUMPAD0..VK_NUMPAD9` to a candidate index regardless of `NumericPad`, so numpad keys would still be eaten as selkeys. That violates the documented preference and surprises users who configured numpad as a digit pad. The guard ensures `NUMERIC` always means "type digits", everywhere.

**Why this location.** `IsKeystrokeRange` is the single entry into selkey selection from `IsVirtualKeyNeed` (called for both `CANDIDATE_ORIGINAL` and `CANDIDATE_PHRASE`). Adding the check here covers both candidate flavours without touching the lower-level `CCandidateRange::IsRange` (which is also used by `GetIndex` and other callers that should retain the existing numpad-by-index semantics).

**Symmetry with existing code.** The `(uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE)` range and the `CConfig::GetNumericPad() == NUMERIC_PAD_MUMERIC` test mirror the bypass already present three places in `IsVirtualKeyNeed` and `IsVirtualKeyKeystrokeComposition`. This change extends the existing convention to the candidate-selkey path that previously missed it.

**What it does not do.** It does not affect `NUMERIC_PAD_MUMERIC_COMPOSITION_ONLY` or `NUMERIC_PAD_RADICAL` modes — both retain their current numpad-as-selkey behaviour, since the guard only triggers when the pref is `NUMERIC`. It also does not affect top-row digits (which match selkeys via `Printable` rather than the numpad-index branch in `IsRange`).

---

### 3. `src/UIPresenter.h:130` — null-guard on `_SetCandidateSelectionInPage`

```cpp
// Before:
BOOL _SetCandidateSelectionInPage(int nPos) { return _pCandidateWnd->_SetSelectionInPage(nPos); }

// After:
BOOL _SetCandidateSelectionInPage(int nPos) { return _pCandidateWnd ? _pCandidateWnd->_SetSelectionInPage(nPos) : FALSE; }
```

**What it does.** Adds a null check on `_pCandidateWnd` before dereferencing. Returns `FALSE` (no selection made) when the candidate window does not exist.

**Why it is necessary.** This is the actual line where the host crashed. With changes 1 and 2 in place, the call site in `_HandleCandidateSelectByNumber` should never be reached with `_pCandidateWnd == nullptr`. The guard is **defense in depth**: it converts an in-process IME crash (which takes the host with it) into a benign `FALSE` return that callers already treat as "selection skipped" (see `CandidateHandler.cpp:287-291`).

**Why this location.** `_SetCandidateSelectionInPage` is an inline accessor in the header that has always assumed `_pCandidateWnd` is non-null. The same pattern (`if (_pCandidateWnd == nullptr) return ...`) is already used by sibling helpers in `UIPresenter.cpp` (e.g. lines 376, 398, 414, 772, 798, 837); this change brings the inline accessor in line with that existing convention.

**What it does not do.** It does not silently mask state-desync bugs in production — callers see `FALSE` and skip the selection (which is the right behaviour when no UI exists). With the upstream fixes in place, the guard is effectively dead code on the happy path; it only fires if a future regression reintroduces a desync.

---

### Layered defence summary

The three changes form three layers of protection against the same family of failure (`_candidateMode != NONE` while `_pCandidateWnd == nullptr`):

1. **`_IsKeyEaten`** detects the desync earliest and prevents the key from being eaten in the first place — host receives the digit, IME state self-heals.
2. **`IsKeystrokeRange`** prevents numpad keys from being misclassified as selkeys when the user's pref says they are digits, eliminating the most common path into the selkey handler.
3. **`_SetCandidateSelectionInPage`** is the last-resort null-guard at the actual crash site.

Any one of the three would prevent the host crash in the reported scenario; together they also fix two latent correctness bugs (NumericPad pref violation, and an unguarded inline pointer dereference).

## Notes

- The state desync between `_candidateMode = PHRASE` and `_pCandidateWnd == nullptr` is the underlying defect; the fix recovers from it but does not eliminate it. A follow-up could move the `_candidateMode` assignment in [CandidateHandler.cpp](../src/CandidateHandler.cpp) (the make-phrase block) to **after** `_StartCandidateList` returns, so the mode flag is only set once the window actually exists.
- `OnTestKeyDown` in this codebase calls `_InvokeKeyHandler` (which issues `RequestEditSession`), which is not strictly conformant to TSF (`OnTestKeyDown` should be a pure test). It is not the cause of #126 but is a latent risk worth a follow-up.
