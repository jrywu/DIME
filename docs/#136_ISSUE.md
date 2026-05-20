# Issue #136 — Plain Numpad digit commits an associated phrase, bypassing the Shift gate

- **Repository:** jrywu/DIME
- **Reporter:** gontera (Gontera Yeh)
- **Title (orig.):** DIME 1.3.644 自建輸入法聯想字詞的行為
- **Affected build:** 1.3.644 (current)
- **Severity:** Medium (UX / correctness — Numpad-as-radical IMEs collide with the associated-phrase selector when a phrase candidate is showing)
- **Status:** **Investigating.** Root cause located by code reading; reproduction conditions confirmed by reporter; no code change shipped yet.

## Symptom (reporter)

> 印象中，聯想字詞是必須按 "shift+數字鍵" 的方式送詞。但在「DIME自建輸入法」中，則是只按數字鍵就送詞了（不必加按 shift）。建議還是維持按 shift 的一貫處理方式比較好，如此在當數字鍵做為輸入法的字根鍵時（如：行列40、行列10、大易……等輸入法），才不致與聯想字詞相衝而造成不便。

After confirming with the reporter, the precise conditions are:

- **Keyboard area:** **Numpad** number keys (not the main-row digits). Main-row Shift+digit already correctly commits the phrase, and main-row plain digit already correctly does NOT commit a phrase.
- **DIME NumericPad preference:** 「**僅用數字鍵盤輸入字根**」 = `NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY` (value `2`). In this mode, only the Numpad can produce composition radicals; the main row can't.
- **IME mode:** observed in DIME自建 (`IME_MODE_GENERIC`), but the bug is **mode-independent** — Array, Dayi, Phonetic share the same phrase selkey range and the same selection logic. The reporter sees it under GENERIC because their custom table uses Numpad as radical keys.

The user expects: when an associated-phrase candidate is showing and they press a plain Numpad digit (no Shift), the phrase should NOT commit; it should either cancel the phrase and use the Numpad digit as the next composition radical, or pass through to the host. Shift+Numpad digit should still commit the phrase, matching main-row Shift+digit.

## Root cause

`CCandidateRange::IsRange` and `CCandidateRange::GetIndex` at [BaseStructure.cpp:271-320](../src/BaseStructure.cpp#L271-L320) have a Numpad fallback that matches by `Index`, ignoring both the stored `Modifiers` field and the `NumericPad` preference:

```cpp
BOOL CCandidateRange::IsRange(UINT vKey, WCHAR Printable, UINT Modifiers, CANDIDATE_MODE candidateMode)
{
    if (candidateMode != CANDIDATE_MODE::CANDIDATE_NONE)
    {
        for (UINT i = 0; i < _CandidateListIndexRange.Count(); i++)
        {
            if (Printable == _CandidateListIndexRange.GetAt(i)->Printable)
            {
                if (i == 0 && vKey == VK_SPACE && CConfig::GetSpaceAsPageDown()) return FALSE;
                else                                                              return TRUE;
            }
            else if ((VK_NUMPAD0 <= vKey) && (vKey <= VK_NUMPAD9))
            {
                if ((vKey - VK_NUMPAD0) == _CandidateListIndexRange.GetAt(i)->Index)
                    return TRUE;                            // <-- bug path
            }
        }
    }
    return FALSE;
}
```

The phrase index range is populated in [SetupCompositionProcesseorEngine.cpp:1661-1684](../src/SetupCompositionProcesseorEngine.cpp#L1661-L1684) as:

| Entry | `Printable` | `Index` | `Modifiers` |
|---|---|---|---|
| 0 | `'!'` | `1` | `TF_MOD_SHIFT` |
| 1 | `'@'` | `2` | `TF_MOD_SHIFT` |
| … | … | … | `TF_MOD_SHIFT` |
| 8 | `'('` | `9` | `TF_MOD_SHIFT` |
| 9 | `')'` | `0` | `TF_MOD_SHIFT` |

The design intent is unambiguous: phrase selkeys are the shifted-digit row, and every entry stores `TF_MOD_SHIFT` as the required modifier. But the match function never reads `Modifiers`, and the Numpad branch matches purely on `(vKey - VK_NUMPAD0) == Index`. Numpad-1's `vKey - VK_NUMPAD0 == 1`, which matches phrase entry 0's `Index == 1`, returns TRUE, and the key is dispatched as `FUNCTION_SELECT_BY_NUMBER` → `_HandlePhraseSelectByNumber` commits the phrase.

### Why main-row digits are unaffected

Main-row `'1'` produces `wch = '1'`. Stored phrase Printable is `'!'`. `'1' ≠ '!'`, so the Printable branch misses. Main-row VK is `'1'` (`0x31`), not in `VK_NUMPAD0..VK_NUMPAD9`, so the Numpad branch also misses. Returns FALSE → key flows to the *"cancel associated phrase with any key except shift"* branch at [KeyProcesser.cpp:631-640](../src/KeyProcesser.cpp#L631-L640), phrase is dismissed, key is passed back to the host or to a fresh composition. This matches the reporter's observation that main row works correctly.

### Why the NumericPad pref doesn't save it

The `NumericPad` preference is honoured in four places in `KeyProcesser.cpp` ([519](../src/KeyProcesser.cpp#L519), [549](../src/KeyProcesser.cpp#L549), [825](../src/KeyProcesser.cpp#L825), [910-911](../src/KeyProcesser.cpp#L910-L911), [928](../src/KeyProcesser.cpp#L928)) — escape-input gating, Dayi address-char gating, wildcard suppression, and the radical-map check. **None of them gate `IsKeystrokeRange` / `IsRange` / `GetIndex`.** So under `NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY`, where the user has explicitly said "Numpad is for radical input", a Numpad digit pressed during a phrase candidate still hits the selkey-match path and commits a phrase — exactly the collision the reporter described.

The bug also fires under `NUMERIC_PAD_NUMERIC` (Numpad → digit symbols passed to host): plain Numpad still commits the phrase, even though the user has asked for Numpad to act as a system digit-pad and never trigger composition. Under `NUMERIC_PAD_NUMERIC_COMPOSITION` (Numpad usable for both radicals and digits) the same race occurs.

## Reproduction

1. DIME自建 (`IME_MODE_GENERIC`) with a CIN/TTS table whose phrase section produces associated phrases AND whose radical keystrokes include Numpad digits. Either the reporter's own custom GENERIC table, or, as a proxy: load Dayi.cin / Array.cin into GENERIC. Set NumericPad pref to **「僅用數字鍵盤輸入字根」** (value `2`).
2. NumLock **ON**.
3. Type a character that produces an associated-phrase candidate (e.g. an Array shape that has a `[Phrase]` entry, or a Dayi character whose key has phrase entries).
4. With the phrase candidate visible, press a Numpad digit (e.g. `Numpad 1`).
5. **Observed:** the first phrase commits. The user's intended next radical (Numpad 1) is consumed by the phrase selector and never reaches composition.
6. **Expected:** plain Numpad digit dismisses the phrase candidate, then begins a new composition with the Numpad digit as radical. Only Shift+Numpad digit commits a phrase.

Sanity checks (matching the reporter's confirmation that main-row already works):

7. Same setup, press main-row `1`. **Observed:** phrase dismissed, no commit. **Expected:** same.
8. Same setup, press main-row `Shift+1`. **Observed:** first phrase commits. **Expected:** same (intended path).
9. Same setup, press Numpad with NumLock **OFF** (so `vKey` is `VK_END` / `VK_DOWN` / etc., not `VK_NUMPAD0..9`). Numpad branch in `IsRange` doesn't fire; behavior depends on the specific navigation key.

## Proposed fix — pref-scoped: only under 「僅用數字鍵盤輸入字根」

Per user direction, scope the fix to the single NumericPad value where the collision actually matters. Under `NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY` the user has explicitly reserved Numpad for radical input, so a Numpad digit pressed during a phrase candidate is unambiguously *radical-intent*, not phrase-select-intent. Under the other two prefs (`NUMERIC` and `NUMERIC_COMPOSITION`), the current "Numpad selects candidate directly" behaviour is preserved exactly as today — many users rely on Numpad for fast candidate selection and the doc should not surprise them.

One surgical edit to `CCandidateRange` in [BaseStructure.cpp](../src/BaseStructure.cpp), plus threading the live modifier value into `GetIndex`.

### (1) Pref-gated modifier check in the VK_NUMPAD branch

In `IsRange` and `GetIndex`, before returning a Numpad-by-`Index` match, require that the entry's `Modifiers` are satisfied — **but only when the pref is `NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY`**:

```cpp
else if ((VK_NUMPAD0 <= vKey) && (vKey <= VK_NUMPAD9))
{
    if ((vKey - VK_NUMPAD0) == _CandidateListIndexRange.GetAt(i)->Index)
    {
        // #136: When user has chosen "Numpad for radicals only", Numpad keystrokes
        // are radical-intent — phrase selection must require Shift+Numpad, matching
        // the main-row Shift+digit convention. Other prefs preserve today's behaviour
        // (Numpad directly selects, no Shift needed).
        if (CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY)
        {
            UINT required = _CandidateListIndexRange.GetAt(i)->Modifiers;
            if ((Modifiers & required) != required)
                continue;       // modifier mismatch under this pref — skip entry
        }
        return TRUE;
    }
}
```

- Main-candidate range entries have `Modifiers = 0` → gate is a no-op even under `COMPOSITION_ONLY`. Main candidates selectable by Numpad as today.
- Phrase range entries have `Modifiers = TF_MOD_SHIFT` → under `COMPOSITION_ONLY`, plain Numpad no longer matches; Shift+Numpad still does. Phrase cancels and the plain Numpad keystroke flows to the next composition as a radical.
- Under `NUMERIC` and `NUMERIC_COMPOSITION` the gate is bypassed entirely — current behaviour preserved bit-for-bit, including the (pre-existing) "plain Numpad selects phrase" path that the reporter is no longer affected by.

The Printable branch is left untouched. Main-row Shift+digit (`wch='!'`) already matches phrase Printable `'!'` correctly today; no need to add a modifier gate there because the Printable equality itself encodes the Shift requirement.

### (2) `GetIndex` signature: add a `modifiers` parameter

Today `GetIndex` is `(UINT vKey, WCHAR Printable, CANDIDATE_MODE)`. Add a `UINT modifiers` arg and thread it from the two call sites:

- [CandidateHandler.cpp:283](../src/CandidateHandler.cpp#L283) (`_HandleCandidateSelectByNumber`)
- [CandidateHandler.cpp:361](../src/CandidateHandler.cpp#L361) (`_HandlePhraseSelectByNumber`)

Both callers can pass `Global::ModifiersValue` (the same value already used by `IsRange` callers and the Shift-English carve-out in [KeyEventSink.cpp:222-225](../src/KeyEventSink.cpp#L222-L225)).

## Risk and regression matrix

| Scenario | Before | After |
|---|---|---|
| Main row plain digit, `CANDIDATE_ORIGINAL` (Array/Phonetic/Generic) | Selects (Printable match, no modifier) | Unchanged |
| Main row Shift+digit, `CANDIDATE_PHRASE` | Selects via Printable `'!'` | Unchanged |
| Main row plain digit, `CANDIDATE_PHRASE` | Doesn't select (`'1' ≠ '!'`) → cancels phrase | Unchanged |
| Numpad plain digit, `CANDIDATE_ORIGINAL`, pref = `NUMERIC` | Selects via Index | Unchanged — gate disabled under this pref |
| Numpad plain digit, `CANDIDATE_ORIGINAL`, pref = `COMPOSITION` | Selects via Index | Unchanged — gate disabled under this pref |
| Numpad plain digit, `CANDIDATE_ORIGINAL`, pref = `COMPOSITION_ONLY` | Selects via Index | Unchanged — main-row entry has `Modifiers=0`, gate passes |
| Numpad plain digit, `CANDIDATE_PHRASE`, pref = `NUMERIC` | Selects phrase via Index | Unchanged — gate disabled under this pref. Plain Numpad still commits phrase. |
| Numpad plain digit, `CANDIDATE_PHRASE`, pref = `COMPOSITION` | Selects phrase via Index | Unchanged — gate disabled under this pref. Plain Numpad still commits phrase. |
| Numpad plain digit, `CANDIDATE_PHRASE`, pref = `COMPOSITION_ONLY` (reporter's setting) | Selects phrase (bug) | **No longer selects.** Phrase cancels, plain Numpad becomes the next composition radical. |
| Numpad Shift+digit, `CANDIDATE_PHRASE`, pref = `COMPOSITION_ONLY` | Selects via Index | Unchanged — modifier gate satisfied |
| `CConfig::GetSpaceAsPageDown()` early-return for `vKey == VK_SPACE` | Honoured | Honoured (early return is in the Printable branch, untouched) |
| `CConfig::GetSpaceAsFirstCaniSelkey()` (main range only, `Modifiers=0`) | Honoured | Honoured (gate is a no-op even when enabled) |

Note: the bug *technically* still exists under `NUMERIC` and `NUMERIC_COMPOSITION` — plain Numpad continues to commit a phrase there. We are deliberately leaving that behaviour intact because (a) it has shipped for years without complaint, (b) users on those prefs may rely on Numpad as a fast selector, and (c) the reporter's specific collision only matters when Numpad is the user's *radical* keyboard, which is exactly what `COMPOSITION_ONLY` declares.

## Tests to add to `src/tests/StringTest.cpp`

Existing tests under `CCandidateRange tests — IsRange() and GetIndex()` ([StringTest.cpp:438](../src/tests/StringTest.cpp#L438)) include:

- `GetIndex_ValidKey_ReturnsCorrectIndex`
- `GetIndex_InvalidKey_ReturnsNeg1`
- `GetIndex_NoneMode_ReturnsNeg1`
- `GetIndex_NumpadKey_ReturnsCorrectIndex`

New cases needed (all use a `CCandidateRange` populated with phrase entries — `Printable='!'`, `Index=1`, `Modifiers=TF_MOD_SHIFT` — and `candidateMode = CANDIDATE_PHRASE`):

Each phrase-mode test sets `NumericPad = NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY` unless noted (gate only enforced under that pref):

- `GetIndex_PhraseMode_CompositionOnly_NumpadNoShift_ReturnsNeg1` — `vKey=VK_NUMPAD1`, `modifiers=0`, pref=`COMPOSITION_ONLY` ⇒ expect `-1`.
- `GetIndex_PhraseMode_CompositionOnly_NumpadWithShift_ReturnsIndex0` — `vKey=VK_NUMPAD1`, `modifiers=TF_MOD_SHIFT`, pref=`COMPOSITION_ONLY` ⇒ expect `0`.
- `GetIndex_PhraseMode_Numeric_NumpadNoShift_ReturnsIndex0` — `vKey=VK_NUMPAD1`, `modifiers=0`, pref=`NUMERIC` ⇒ expect `0` (gate disabled; pre-existing behaviour preserved).
- `GetIndex_PhraseMode_Composition_NumpadNoShift_ReturnsIndex0` — same, pref=`NUMERIC_COMPOSITION` ⇒ expect `0` (gate disabled).
- `GetIndex_PhraseMode_MainRowShifted_ReturnsIndex0` — `vKey='1'`, `Printable='!'`, `modifiers=TF_MOD_SHIFT`, any pref ⇒ expect `0` (Printable branch, untouched).
- `GetIndex_PhraseMode_MainRowPlainDigit_ReturnsNeg1` — `vKey='1'`, `Printable='1'`, `modifiers=0`, any pref ⇒ expect `-1` (already true today, pin it).
- `GetIndex_OriginalMode_CompositionOnly_NumpadNoShift_ReturnsIndex` — main-candidate range (`Modifiers=0`), `vKey=VK_NUMPAD1`, pref=`COMPOSITION_ONLY` ⇒ expect index `0` (gate is a no-op for entries with no required modifier).

## Why we're not shipping a code fix in this commit

Per project guideline §7 (`CLAUDE.md`): if the failure hypothesis isn't proven by trace, gather evidence before editing. The analysis above is from code reading + reporter confirmation of the trigger conditions; it has not been verified by `DEBUG_PRINT` against a live repro. Next concrete steps below.

## Next steps (in order)

1. Enable `DEBUG_PRINT` in `BaseStructure.cpp` and `KeyProcesser.cpp` long enough to capture one repro of the bug path: log `(vKey, Printable, Modifiers, candidateMode)` on every `IsRange` / `GetIndex` call.
2. Reproduce locally with NumericPad pref = `NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY` and Numpad NumLock ON. Expect a trace line showing `vKey=VK_NUMPAD1, Printable='1', Modifiers=0, candiMode=CANDIDATE_PHRASE` immediately followed by `FUNCTION_SELECT_BY_NUMBER` dispatch.
3. Implement the modifier-gate edit in `IsRange` / `GetIndex` and the `NumericPad == NUMERIC` Numpad-suppression edit. Thread `Global::ModifiersValue` (or equivalent) into `GetIndex`.
4. Add the six unit tests in `src/tests/StringTest.cpp`.
5. Visually verify:
   - **Reporter's setup** (GENERIC with Numpad-radical custom table, pref = `COMPOSITION_ONLY`): plain Numpad digit cancels phrase and becomes next radical. Shift+Numpad commits phrase.
   - **Array / Dayi default**: main-row digit selection (Array) and main-row Shift+digit phrase selection (both) still work.
   - **NumericPad pref = `NUMERIC`** (default): Numpad now passes through to host even when a candidate is showing.
   - **Regression checks**: Space-as-first-selkey, Space-as-page-down, reverse-lookup notify all unchanged.
6. Comment out `DEBUG_PRINT`, remove temporary log lines, ship.

## Files modified so far in this branch

| File | Status |
|---|---|
| `docs/#136_ISSUE.md` | This document. |

No source changes yet.
