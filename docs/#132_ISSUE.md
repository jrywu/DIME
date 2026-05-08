# Issue #132 — Array Phrase Input Broken for Codes with Zero Incremental Candidates (v1.3.640)

- **Title (orig.):** v.1.3.640行列輸入法詞庫異常
- **Severity:** High (phrase input silently non-functional for a subset of valid phrase codes)
- **Status:** **RC found 2026-05-08; one-line fix in `src/KeyEventSink.cpp`.**
- **Regression introduced in:** v1.3.640 (works in v1.3.616)

## Symptom

After upgrading to v1.3.640, pressing the array phrase key `'` does nothing when the composition
buffer's suffix has no **short-code** (incremental) candidates. The compositor stays active, no
output is produced, and no error beep is heard. Pressing `←` can still edit the composed keys —
the session is fully live.

Representative failing codes (Array 30, non-BIG5 scope):

| Composed keys | Expected output | Observed in v1.3.640 |
|---------------|-----------------|----------------------|
| `,,,/` + `'` | 燦爛 (or candidate list) | **nothing** |
| `,,,;` + `'` | 燃燒品 (or candidate list) | **nothing** |

Representative passing codes (unchanged):

| Composed keys | Result |
|---------------|--------|
| `,,,` + `'` | works |
| `,,,,` + `'` | works |

The pattern is: codes whose suffix yields ≥ 1 incremental short-code candidates continue to work;
codes whose suffix yields 0 incremental candidates (yet are valid phrase endings in
`Array-Phrase.cin`) silently fail.

## Root Cause

### Background: Array 30 key routing in `IsVirtualKeyNeed`

`IsVirtualKeyNeed` in `src/KeyProcesser.cpp` routes keystrokes based on `candidateMode`.
The array phrase ending key (`VK_OEM_7`, `'`) is handled **only** inside the
`else if (candidateMode == CANDIDATE_MODE::CANDIDATE_INCREMENTAL)` branch
([src/KeyProcesser.cpp:771-782](../src/KeyProcesser.cpp#L771-L782)):

```cpp
if (fComposing)
{
    if (candidateMode == CANDIDATE_MODE::CANDIDATE_NONE) {
        switch (uCode) {
            // VK_LEFT, VK_RIGHT, VK_BACK, VK_RETURN, VK_SPACE …
            // ← VK_OEM_7 is NOT here
        }
    }
    else if (candidateMode == CANDIDATE_MODE::CANDIDATE_INCREMENTAL) {
        switch (uCode) {
            …
            case VK_OEM_7:   // Array phrase ending key '
                if (Global::imeMode == IME_MODE::IME_MODE_ARRAY &&
                    CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
                { … return TRUE; }
        }
    }
}
```

For phrase lookup to fire, `candidateMode` must be `CANDIDATE_INCREMENTAL` when `'` is pressed.

### The regression: over-broad stale-mode reset (Issue #126 fix)

The #126 fix added this guard to `_IsKeyEaten` in `src/KeyEventSink.cpp`
([src/KeyEventSink.cpp:~177-184](../src/KeyEventSink.cpp)):

```cpp
if (_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE && candiCount == 0)
{
    _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
    _isCandidateWithWildcard = FALSE;
}
```

The intent was to clear a race-condition stale state where `CANDIDATE_PHRASE` could be set
before `_pCandidateWnd` was created, causing a null-deref crash on subsequent numpad input.
However, the condition is too broad: it also resets `CANDIDATE_INCREMENTAL` to `CANDIDATE_NONE`
any time the incremental candidate count is 0.

### Step-by-step failure trace for `,,,/` + `'`

| Step | User action | `_candidateMode` | `candiCount` |
|------|-------------|-----------------|--------------|
| 1 | type `,` | INCREMENTAL | > 0 |
| 2 | type `,` | INCREMENTAL | > 0 |
| 3 | type `,` | INCREMENTAL | > 0 |
| 4 | type `/` | INCREMENTAL (unchanged) | **0** — no short-code match for `,,,/` |
| 5 | type `'` — `_IsKeyEaten` called | **reset to NONE** ← regression | 0 |

With `candidateMode == CANDIDATE_NONE`, `IsVirtualKeyNeed` enters the `CANDIDATE_NONE` switch
which has no `VK_OEM_7` case. The `'` key falls through, is not eaten by phrase lookup, and the
composition remains active with no output.

### Why codes with non-zero incremental candidates are unaffected

For `,,,` and `,,,,`, `candiCount > 0` when `'` is pressed (the user reported seeing candidates
*炎炎*, *迷迷糊糊*, *熒熒* for `,,,,`). The stale-reset guard does not fire, `_candidateMode`
stays `CANDIDATE_INCREMENTAL`, and the `VK_OEM_7` case is reached as intended.

## Fix

**File:** `src/KeyEventSink.cpp`

Exclude `CANDIDATE_INCREMENTAL` from the stale-reset guard. `CANDIDATE_INCREMENTAL` with
`candiCount == 0` is a legitimate composition state — the user is still entering a valid phrase
code whose suffix simply has no short-code match yet. The `_pCandidateWnd` null-deref risk
(the original #126 concern) does not apply to `CANDIDATE_INCREMENTAL`: the candidate window is
created before the mode is set to INCREMENTAL and remains valid throughout the session.

### Before

```cpp
if (_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE && candiCount == 0)
```

### After

```cpp
// Do NOT include CANDIDATE_INCREMENTAL: zero incremental candidates is a normal composing
// state (e.g. phrase-code suffix has no short-code match). Resetting it would drop the
// VK_OEM_7 phrase handler that lives only in the INCREMENTAL branch of IsVirtualKeyNeed (#132).
if (_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE &&
    _candidateMode != CANDIDATE_MODE::CANDIDATE_INCREMENTAL &&
    candiCount == 0)
```

## Files Modified

| File | Change |
|------|--------|
| [`src/KeyEventSink.cpp`](../src/KeyEventSink.cpp) | Narrow stale-mode reset to exclude `CANDIDATE_INCREMENTAL` |

## Verification

1. Build Release\|x64.
2. Switch to 行列 (Array 30, non-BIG5 scope).
3. Type `,,,/` then `'` → outputs 燦爛 or shows candidate list. ✓
4. Type `,,,;` then `'` → outputs 燃燒品 or shows candidate list. ✓
5. Type `,,,` then `'` → still works (regression check). ✓
6. Type `,,,,` then `'` → still works (regression check). ✓
7. After a phrase lookup, press numpad digits → no crash (#126 regression check). ✓
