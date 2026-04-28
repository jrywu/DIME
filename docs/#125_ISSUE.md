# Issue #125 — 提示聯想字詞 出不來 (associated phrase candidates do not appear)

- **Repository:** jrywu/DIME
- **Reporter:** peter8777555
- **Title (orig.):** DIME v1.3.616 提示聯想字詞 出不來
- **Environment:** Windows 11 23H2, Phonetic 41-key (倚天注音 41 鍵)
- **Severity:** High (core feature broken — make-phrase associative candidates)
- **Status:** Fixed in branch (2026-04-28). Awaiting next release.

---

## Symptom

After committing a candidate, the **associated-phrase candidate list** (聯想字詞)
that should appear next to the just-committed character does not appear at all,
or appears for one paint cycle and then vanishes. Reproduces in any host that
exercises the make-phrase code path; reporter saw it in Phonetic 41-key but the
bug is mode-agnostic — Dayi, Array, Phonetic, and Generic are all affected.

| DIME version | Behaviour |
|--------------|-----------|
| **v1.3.582** | Working. Reporter confirmed normal. |
| **v1.3.616** | Broken. Reporter rolled back to v1.3.582. |

The reporter's secondary UI-sizing complaint about the new settings dialog on
1920×1080 is tracked separately and is NOT covered by this issue file.

## Repro

Preconditions:

- `MakePhrase = 1` in the relevant `*Config.ini` (default ON).
- Host is **not** a CMD/PowerShell console (issue #27 intentionally suppresses
  make-phrase in console hosts).

Steps:

1. Type a reading (e.g. Phonetic 41-key for 天).
2. Pick a candidate from the first-tier candidate list (space or selkey).
3. **Expected:** committed character is inserted; the phrase associative
   candidate list (e.g. 天X variants — 天下 / 天氣 / 天空 / …) appears next to
   the inserted character.
4. **Actual (v1.3.616):** the phrase candidate list either does not appear, or
   flashes for one paint cycle and disappears.

## Timeline of the regression (UTC+8)

| Date / time | Commit | What it did |
|-------------|--------|-------------|
| 2026-04-01 09:36 | `a97d654` *Fix caret tracking, associated phrase anchor, and shadow gap* | Added two guards inside `CUIPresenter::_StartCandidateList`: (i) skip `MakeCandidateWindow` when `_pCandidateWnd` already exists, (ii) fall back to `_rectCompRange` when `_GetTextExt` fails. Required for the make-phrase repositioning call introduced in the same commit. |
| **2026-04-01 10:22** | **v1.3.582 release (CI bot)** | Built right after a97d654 — guards in place — phrase works. |
| 2026-04-08 10:49 | `f898dd6` *Suppress CHN/ENG notify popup for non-editable windows on focus change* | **Silently reverted both guards** in `_StartCandidateList` while the rest of the commit was about an unrelated CHN/ENG notify suppression. |
| **2026-04-08 22:12** | **v1.3.616 release (CI bot)** | Built after f898dd6 — guards gone — phrase blinks/destroyed. |
| 2026-04-27 13:13 | `a75b180` (#126) | Adjusted the `_In_opt_` SAL annotation only; the broken block was untouched. Did not contribute to #125 but is sometimes mis-attributed because it was the most recent edit to the file. |
| 2026-04-28 (today) | This branch | Restores the two guards + fixes `_GetTextExt` returning `S_OK` with zero rect. |

The v1.3.582 ↔ v1.3.616 boundary aligns exactly with the f898dd6 revert.

## Root cause

After committing a candidate, [`CDIME::_HandleCandidateWorker`](../src/CandidateHandler.cpp#L202-L244)
runs the make-phrase block (`if (CConfig::GetMakePhrase() && !isCMDShell())`):

```cpp
_pUIPresenter->_ClearCandidateList();        // does NOT dispose _pCandidateWnd
_pUIPresenter->_SetCandidateText(...);       // repopulates the live window with phrase items
_pUIPresenter->_SetCandidateSelection(-1, FALSE);
_candidateMode = CANDIDATE_MODE::CANDIDATE_PHRASE;
...
_pUIPresenter->_StartCandidateList(_tfClientId, pDocMgr, pContext, ec, nullptr,
    _pCompositionProcessorEngine->GetCandidateWindowWidth());   // reposition for caret tracking
```

That trailing `_StartCandidateList(nullptr)` call is the make-phrase
"reposition the already-populated phrase window using `_rectCompRange`"
hook. It only works correctly when `_StartCandidateList` honours two
preconditions:

1. **`MakeCandidateWindow` skipped if a window already exists.** Otherwise
   `MakeCandidateWindow` returns `E_FAIL` (its early-return at
   [UIPresenter.cpp:1604-1608](../src/UIPresenter.cpp#L1604-L1608)
   rejects calls with `_pCandidateWnd != nullptr`), the outer
   `_StartCandidateList` runs the `Exit` branch's `_EndCandidateList()`,
   the just-populated phrase window is destroyed, and `E_FAIL`
   propagates back to the caller.
2. **`_GetTextExt` fallback to `_rectCompRange` when no composition range exists.**
   After `_HandleComplete` commits the picked candidate, the composition is
   ended — `_pRangeComposition` becomes `nullptr`. `_GetTextExt` was supposed
   to "fail cleanly" in this case so the caller could fall back to
   `_rectCompRange` (the saved last-known composition rect of the
   just-committed character).

`f898dd6` reverted **both** preconditions:

- Removed the `if (_pCandidateWnd) hr = S_OK;` guard before
  `MakeCandidateWindow`.
- Removed the `else _LayoutChangeNotification(&_rectCompRange, TRUE);`
  fallback for the `_GetTextExt`-fails branch.

While verifying the fix in this branch a third defect surfaced — independent of
f898dd6 but only visible once preconditions 1 and 2 were back:

- `_GetTextExt` ([TfTextLayoutSink.cpp:268](../src/TfTextLayoutSink.cpp#L268))
  was returning `S_OK` with the caller's rect left at its `{0,0,0,0}`
  initialiser when `_pRangeComposition == nullptr` (the "no composition"
  state used by the make-phrase repositioning call). a97d654's commit
  message claimed *"_GetTextExt fails cleanly"* but the function never
  actually returned a failure code. Result: the phrase window is positioned
  at screen origin (0, 0).

## Path audit — every way associated phrase can fail to show with `MakePhrase = ON`

`_HandleCandidateWorker` make-phrase block:

| Branch / condition | Outcome | Bug? |
|---|---|---|
| `CConfig::GetMakePhrase() == FALSE` | `else` → `_DeleteCandidateList(TRUE)` | Not a bug — user-disabled |
| `isCMDShell()` TRUE | skip make-phrase block | Intentional (issue #27) |
| `candidatePhraseList.Count() == 0` | `_HandleCancel(ec, pContext)` | Correct — dictionary has no phrase entries for the committed char |
| `pContext->GetDocumentMgr()` fails | skip `_StartCandidateList` | Rare TSF error, defensive skip |
| `_pUIPresenter` / `_pCompositionProcessorEngine` null | skip | Only during teardown |
| **`_StartCandidateList` E_FAIL via `MakeCandidateWindow` rejection** | Exit branch fires `_EndCandidateList()` — phrase window destroyed mid-show | **Bug — #125 primary** |
| **`_StartCandidateList` reaches `_GetTextExt` which returns `S_OK` with `{0,0,0,0}`** | phrase window positioned at screen origin (0, 0) instead of next to the committed character | **Bug — #125 secondary** |

The only non-trivial code paths that "lose" the phrase window when
make-phrase is enabled are the two starred bugs. Everything else is
either user-controlled, host-class-controlled (CMD shell), or an
intentional/correct response to absent dictionary data.

## Fix (this branch)

| File | Change |
|---|---|
| `src/UIPresenter.cpp` (`_StartCandidateList`) | Restored `if (_pCandidateWnd) { hr = S_OK; } else { MakeCandidateWindow(...); }` guard. Restored `else _LayoutChangeNotification(&_rectCompRange, TRUE);` fallback for the `_GetTextExt`-fails branch. |
| `src/TfTextLayoutSink.cpp` (`_GetTextExt`) | Return `E_FAIL` when `_pRangeComposition == nullptr` (was returning `S_OK` with unpopulated zero rect). Also plugs a pre-existing `pContextView` leak on the failure path. |

The fix is **mode-agnostic** — it lives entirely in `CUIPresenter` and
`CTfTextLayoutSink`, not in any IME-specific keystroke or composition handler —
so Phonetic 41-key (the reporter's setup), Dayi, Array, Phonetic standard, and
Generic are all fixed by the same edits.

## Verification

With `DEBUG_PRINT` enabled in `UIPresenter.cpp`, `CandidateWindow.cpp`,
`CandidateHandler.cpp`, `KeyHandler.cpp`, `KeyProcesser.cpp`, `Composition.cpp`,
`TfTextLayoutSink.cpp`:

1. Type a reading (Phonetic 41-key, Dayi `dj`, Array `ev`, etc.) → pick a
   first-tier candidate → confirm phrase associative list (multiple items) is
   visible **and stays visible** at the correct position next to the committed
   character.
2. Repeat across multiple commits in the same session — phrase list should
   appear after each commit.
3. Repeat in CMD / PowerShell host → phrase should NOT appear (issue #27
   suppression preserved).
4. Repeat with `MakePhrase = 0` → phrase should NOT appear (user pref preserved).
5. Spot-check the host matrix (Notepad, Word, Excel, Edge, VS Code, LINE, Teams)
   for phrase positioning correctness — covered by [CARET_TRACKING.md §10](CARET_TRACKING.md).

`DEBUG_PRINT` toggled back off after verification.

## Related work in this branch

- [#127_ISSUE.md](#127_ISSUE.md) "Co-fixed regressions found during verification" — full debug-log evidence and the original investigation thread that surfaced this bug while testing #127.
- [CARET_TRACKING.md §4](CARET_TRACKING.md) "Regression and re-fix (2026-04-28, during #127 verification)" — architectural note on the f898dd6 silent revert and the third `_GetTextExt` defect.
- [REVERSE_CONV_REFACTOR.md](REVERSE_CONV_REFACTOR.md) — parallel reverse-conversion fixes; same release should pick up both.

## Recommended action

Close #125 against the release that ships this branch's commits. The
1920×1080 settings-UI complaint in the original report is a separate UI-sizing
issue and should be split into a new issue (out of scope here).

## Lesson

The original a97d654 fix introduced **three coupled changes** —
`MakeCandidateWindow` skip, `_rectCompRange` fallback, and the implicit
contract that `_GetTextExt` should fail (not silently succeed with zero rect)
when no composition range exists. The first two were code; the third was
behavioural and only documented in the commit message.

`f898dd6`, an unrelated commit on a different feature branch, removed the
first two without noticing they were load-bearing for the make-phrase path,
and the third was never enforced in code. None of the three were exercised
by automated tests. The result: a v1.3.616-only regression that survived
~3 weeks, three releases, and an unrelated #126 patch before being noticed.

When extracting model logic from a view (or any silent revert during a
seemingly unrelated change), audit:

- Side-effect lines that share an event with the moved code.
- Pre/post-conditions documented only in commit messages or comments.
- Whether the contract is enforced by code or by convention.

Convention-only contracts decay silently.
