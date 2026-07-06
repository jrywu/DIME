# Issue #142 - Associated phrase candidate leaks Esc and loses shifted symbols

- **Repository:** jrywu/DIME
- **Area:** Associated phrase candidate (`CANDIDATE_PHRASE`) key routing
- **Severity:** Medium (UX correctness - candidate cleanup leaks host shortcuts; shifted punctuation can be inserted as the base key)
- **Status:** Root cause revised after the first narrow patch failed. The real failure crosses `OnTestKeyDown`, `OnKeyDown`, and shifted-English output handling.

## Problem Statement

This issue tracks two user-visible failures while the associated phrase candidate window is visible.

### 1. Esc closes the phrase candidate but is also delivered to the app

Expected:

- Pressing `Esc` closes only the associated phrase candidate.
- DIME consumes the key.
- The host app does not receive `Esc`.

Observed:

- Pressing `Esc` closes the phrase candidate.
- The host app also receives `Esc`.
- In apps where `Esc` has application meaning, this can trigger an unwanted app action. The reported example is Super.

### 2. Shift+/ while phrase candidate is visible inserts `/` instead of `?`

Expected:

- User holds `Shift` and presses `/`.
- Associated phrase candidate is dismissed.
- The inserted character is `?`.

Observed:

- Associated phrase candidate is dismissed.
- The inserted character is `/`, as if the shifted printable was converted back to the base key.

## RC Analysis

### RC 1 - `OnTestKeyDown` mutates candidate state before `OnKeyDown`

The first attempted fix only adjusted `OnKeyDown`. That failed because the candidate can already be gone before `OnKeyDown` runs.

Current flow for hard candidate-cancel keys such as `Esc`:

1. `OnTestKeyDown` calls `_IsKeyEaten`.
2. `IsVirtualKeyNeed` sees `candidateMode == CANDIDATE_PHRASE` and `uCode == VK_ESCAPE`.
3. It returns `TRUE` and sets:

```cpp
Category = CATEGORY_CANDIDATE;
Function = FUNCTION_CANCEL;
```

4. `OnTestKeyDown` then immediately invokes the edit session for `CATEGORY_CANDIDATE / FUNCTION_CANCEL`:

```cpp
else if (KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE
    && KeystrokeState.Function == KEYSTROKE_FUNCTION::FUNCTION_CANCEL)
{
    _InvokeKeyHandler(...);
}
```

5. That cancels the phrase candidate during the test phase.
6. Later, `OnKeyDown` calls `_IsKeyEaten` again, but the phrase candidate may no longer exist, so the second decision can become "not eaten" or a different category.
7. Result: DIME visually closes the phrase candidate, but the host may still see `Esc`.

The important distinction:

- Generic associated-phrase cancel with a normal printable key intentionally returns `FALSE` so the host can also receive the user's key after DIME dismisses the phrase candidate.
- Hard control-key cancels like `Esc`, `Backspace`, `Delete`, etc. return `TRUE`; they must be handled in `OnKeyDown`, not destructively processed in `OnTestKeyDown`.

So `OnTestKeyDown` should only perform the early phrase-cancel edit session for candidate cancel states when `*pIsEaten == FALSE`.

### RC 2 - The one-line bare Shift fix is not enough

The first attempted fix changed the generic phrase-cancel fallback from:

```cpp
candidateMode == CANDIDATE_PHRASE
&& !(GetCandidateListPhraseModifier() == 0 && uCode == VK_SHIFT)
```

to:

```cpp
candidateMode == CANDIDATE_PHRASE
&& uCode != VK_SHIFT
```

That is still a valid guard: bare `VK_SHIFT` should not cancel an associated phrase candidate. But it does not explain the final `/` vs `?` output by itself.

The actual shifted-symbol loss happens later in the shifted-English handler.

When phrase candidate is visible and `Shift+/` is pressed, `_IsKeyEaten` has a phrase-specific branch:

```cpp
if (_candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE &&
    (Global::ModifiersValue & SHIFT_MASK) != 0 &&
    pwch && *pwch && iswprint(*pwch) && *pwch != L' ' &&
    !(*pCodeOut >= '0' && *pCodeOut <= '9'))
{
    Category = CATEGORY_COMPOSING;
    Function = FUNCTION_SHIFT_ENGLISH_INPUT;
    return TRUE;
}
```

That correctly routes the key to `_HandleCompositionShiftEnglishInput`.

But `_HandleCompositionShiftEnglishInput` treats non-digit symbols as legacy Shift-English output:

- CapsLock off: clear Shift and recompute the base symbol.
- CapsLock on: keep Shift and recompute the shifted symbol.

So even if `wch` arrives as `?`, the handler can deliberately convert it back to `/` when CapsLock is off.

That behavior may be intentional for normal Shift-English mode, but it is wrong for the associated-phrase dismissal case. In phrase dismissal, the user is not asking DIME to reinterpret Shift-English policy; they are continuing text input with the exact shifted printable they typed.

## Proposed Solution

### Fix 1 - Do not perform eaten candidate cancels inside `OnTestKeyDown`

In `OnTestKeyDown`, keep the early edit-session path for pass-through phrase cancellation only:

```cpp
else if (!*pIsEaten
    && KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE
    && KeystrokeState.Function == KEYSTROKE_FUNCTION::FUNCTION_CANCEL)
{
    _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
}
```

This preserves the intended pass-through behavior for normal keys that dismiss associated phrase candidates, while preventing hard eaten cancels like `Esc` from destroying state before `OnKeyDown` handles them.

Also keep `OnKeyDown` from rewriting phrase-mode `Esc` candidate cancel into composing cancel:

```cpp
if (code == VK_ESCAPE &&
    !(_candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE &&
      KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE &&
      KeystrokeState.Function == KEYSTROKE_FUNCTION::FUNCTION_CANCEL))
{
    KeystrokeState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
}
```

### Fix 2 - Bare Shift must not cancel associated phrase

Keep the generic phrase-cancel fallback from treating bare Shift as a cancellation key:

```cpp
if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE
    && uCode != VK_SHIFT)
{
    Category = CATEGORY_CANDIDATE;
    Function = FUNCTION_CANCEL;
    return FALSE;
}
```

This prevents a standalone `Shift` keydown from destroying the phrase candidate or modifier intent.

### Fix 3 - Preserve shifted printable symbols when dismissing phrase candidates

In `_HandleCompositionShiftEnglishInput`, capture phrase state before finalizing / canceling the candidate:

```cpp
const BOOL wasPhraseCandidate = (_candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE);
```

Then preserve `wch` for non-digit symbols only when the handler was entered from phrase candidate dismissal:

```cpp
else if (!wasPhraseCandidate)
{
    // Existing legacy symbol behavior:
    // CapsLock OFF -> base char, CapsLock ON -> shifted char.
}
```

Because `outputChar` is initialized as `wch`, skipping the legacy symbol recompute under `wasPhraseCandidate == TRUE` keeps `Shift+/` as `?`.

Do not change digit-row behavior. `Shift+1` must continue to participate in phrase selection and shifted-digit preferences.

## Regression Tests / Verification

Automated coverage to keep:

- `Issue142_PhraseEscape_IsEatenAsCandidateCancel` - pins the lower-level `Esc` routing as eaten candidate cancel.
- `Issue142_PhraseShiftOnly_DoesNotCancelCandidate` - pins that bare `VK_SHIFT` is not phrase cancel.

Manual verification still required because the failed behavior depends on the full TSF `OnTestKeyDown` -> `OnKeyDown` sequence and host key delivery:

| Scenario | Expected |
|---|---|
| Associated phrase visible, press `Esc` | Phrase candidate closes; app does not receive `Esc`. |
| Associated phrase visible, press bare `Shift` | Phrase candidate remains; no cancel, no commit. |
| Associated phrase visible, press `Shift+/` | Phrase candidate closes; `?` is inserted. |
| Associated phrase visible, press `/` | Phrase candidate closes; `/` follows existing pass-through / input behavior. |
| Associated phrase visible, press `Shift+1` | Phrase selection via shifted digit remains unchanged. |

## Files Expected To Change

| File | Purpose |
|---|---|
| `src/KeyEventSink.cpp` | Prevent destructive hard-cancel handling during `OnTestKeyDown`; preserve phrase Esc candidate-cancel category in `OnKeyDown`. |
| `src/KeyProcesser.cpp` | Do not treat bare `VK_SHIFT` as generic phrase-cancel input. |
| `src/KeyHandler.cpp` | Preserve shifted printable symbols when shifted-English input is entered from phrase candidate dismissal. |
| `src/tests/TSFCoreLogicIntegrationTest.cpp` | Add focused regression coverage for phrase Esc and bare Shift routing. |
