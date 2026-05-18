# Issue #135 — Firefox search-bar autocomplete popup covers the **associated** candidate window

- **Repository:** jrywu/DIME
- **Reporter:** rmmaniac
- **Title (orig.):** dime 1.3.532版後的行列輸入候選字視窗問題
- **Severity:** Medium (UX — phrase candidates are unselectable when Firefox autocomplete is open; first-tier candidates still work)
- **Status:** **Fixed (2026-05-15).** Root cause confirmed via `DEBUG_PRINT` Z-order tracing (Firefox stamps `WS_EX_TOPMOST` on its `MozillaDropShadowWindowClass` autocomplete popup ~50-500ms *after* our phrase show, winning the topmost band). Fix lands a raw `SetTimer`/`TIMERPROC` re-stamp loop on the candidate HWND while it's visible. Visually verified: phrase candidate stays above Firefox autocomplete with no blink.

## Symptom

In Firefox's URL bar / web-page search inputs (e.g. Google / Yahoo search box) where Firefox shows an autocomplete suggestions popup:

- **First-tier (normal) candidate window:** displays correctly above Firefox's suggestions popup — selectable as usual.
- **Associated phrase candidate window** (the second-tier list that appears after committing a character in Array / Phonetic phrase mode): is **covered / hidden** by Firefox's suggestions popup, leaving the user unable to pick a phrase. Forces them to type characters one-by-one.

The reporter confirms the regression started **after build 1.3.532**. Reproduced locally on 2026-05-15.

Reporter clarified after reproduction:

> "The normal candi not covered is because the prediction popup not shown."

That is — first-tier candidates appear DURING composition, *before* Firefox sees a committed character to autocomplete on, so Firefox's popup isn't open yet. After commit (which triggers phrase mode), Firefox receives the character, opens the popup, and the popup ends up in front of the phrase candidate.

Reporter screenshots: <https://github.com/user-attachments/assets/87bcd22b-5db0-476d-892c-2eccf97c3bd5>, plus a follow-up showing items 4-0 of a 10-row candidate list visible below the popup, items 1-3 hidden behind it (Z-order: popup in front of candidate).

## Reproduction

1. Launch Firefox, navigate to a page with a search input that opens a suggestions popup (Google / Yahoo / Firefox URL bar itself).
2. Click into the search box, switch to DIME (Array or Phonetic phrase mode).
3. Type a character that has phrase associations (e.g. Array `dj` → 明, Phonetic `ev` → 天).
4. Press Space to commit. The associated phrase list should appear.
5. **Observed:** Firefox's suggestions dropdown covers the phrase list. The first-tier candidate that produced 明 / 天 was visible fine on top of (or before) the same dropdown.

## Initial hypothesis (later falsified by visual test)

Both `CCandidateWindow` and Firefox's autocomplete popup use `WS_EX_TOPMOST`. Among multiple topmost windows, the most-recently-stamped-`HWND_TOPMOST` wins Z-order.

- Normal candi: `MakeCandidateWindow` → `CreateWindowEx(WS_EX_TOPMOST, …)` → implicit promotion to top of topmost band. Firefox popup not yet open. Cand wins.
- Phrase candi: reuse branch ([UIPresenter.cpp:554-564](../src/UIPresenter.cpp#L554-L564)) skips `CreateWindowEx`. `CBaseWindow::_Show` ([BaseWindow.cpp:308-309](../src/BaseWindow.cpp#L308-L309)) uses `SWP_NOZORDER`. Firefox popup opens after commit, gets fresh top-of-band. Cand loses.

**Speculative fix attempted (2026-05-15):** insert `SetWindowPos(hCand, HWND_TOPMOST, ...)` in the reuse branch before `Show()`. Built, deployed, tested.

**Result: did NOT fix the symptom.** Phrase candidate still covered by Firefox popup. Reverted from `src/UIPresenter.cpp`.

## Why the speculative fix failed — open questions

Three reasons the one-shot re-promotion can fail to land. Until we have `DEBUG_PRINT` evidence, all three are open hypotheses:

**H1 — Wrong branch is being taken.** Maybe the phrase-show path doesn't actually take the reuse branch; `_pCandidateWnd` could be `nullptr` here for some host-specific reason, sending us through `MakeCandidateWindow` (which already does implicit topmost-band promotion — yet the bug still occurs). If so, the speculative fix never executed in the failing scenario.

**H2 — Firefox stamps `HWND_TOPMOST` *after* our show.** The order may be:
1. DIME commits the character → sends WM_CHAR or TSF commit event to Firefox.
2. DIME's `_StartCandidateList` runs: my speculative `SetWindowPos(HWND_TOPMOST)` lands; phrase candidate at top of topmost band.
3. *Then* Firefox's UI thread processes the input, opens / refreshes its autocomplete popup, stamps its own `HWND_TOPMOST`.
4. Firefox now sits above DIME's candidate. One-shot promotion lost the race.

**H3 — Some other Z-order interaction we haven't identified.** Examples:
- Shadow-window (`_pShadowWnd`, also `WS_EX_TOPMOST`) restacking inside `WM_WINDOWPOSCHANGING` ([CandidateWindow.cpp:441-473](../src/CandidateWindow.cpp#L441-L473)) inadvertently demoting the candidate.
- The owner of the popup (passed as `parentWndHandle` to `_CreateMainWindow`) being lowered by TSF / focus mechanics, dragging owned popups down with it.
- Firefox using a different layering mechanism (DWM-based panel, child window of a different process, etc.) that DIME can't out-stamp via standard `SetWindowPos`.

We don't have enough evidence to pick. Hence: `DEBUG_PRINT` first.

## Investigation plan — `DEBUG_PRINT` instrumentation

### What to enable

Uncomment the `#define DEBUG_PRINT` at the top of these files. After the investigation, comment them back out (matching the pattern in [docs/#127_ISSUE.md](#127_ISSUE.md) §"Verification (post-fix)").

| File | Reason |
| --- | --- |
| `src/UIPresenter.cpp` | Trace `_StartCandidateList` (which branch taken), `_LayoutChangeNotification`, `_RestackVisibleSlots` |
| `src/CandidateWindow.cpp` | Trace `_Show`, `_Move`, `_CreateMainWindow` for both first-tier and phrase shows |
| `src/CandidateHandler.cpp` | Trace `_HandleCandidateWorker` make-phrase block |
| `src/BaseWindow.cpp` | Trace `_Show` SetWindowPos calls if needed |

### Targeted log points to add (temporary, removed after fix)

These additions help answer the three open hypotheses. They go in the reuse branch of `_StartCandidateList` and around `_Show`:

```cpp
// UIPresenter.cpp::_StartCandidateList, after _StartLayout / BeginUIElement
debugPrint(L"#135 _StartCandidateList: _pCandidateWnd=%p (reuse=%d)",
           _pCandidateWnd.get(), _pCandidateWnd ? 1 : 0);

if (_pCandidateWnd)
{
    hr = S_OK;
    HWND hCand = _pCandidateWnd->_GetWnd();
    HWND prev  = hCand ? GetWindow(hCand, GW_HWNDPREV) : nullptr;
    debugPrint(L"#135 reuse-branch: hCand=%p prevInZ=%p (this is who's above us)", hCand, prev);
    if (prev)
    {
        WCHAR cls[64] = {0}; GetClassNameW(prev, cls, 64);
        DWORD pid = 0; GetWindowThreadProcessId(prev, &pid);
        LONG exStyle = GetWindowLongW(prev, GWL_EXSTYLE);
        debugPrint(L"#135   prevInZ class=%ls pid=%lu exStyle=0x%lx topmost=%d",
                   cls, pid, exStyle, (exStyle & WS_EX_TOPMOST) ? 1 : 0);
    }
    // (Do NOT add the SetWindowPos here yet — we want a clean before-state.)
}
```

A second snapshot at the end of `_StartCandidateList`, after `_LayoutChangeNotification`:

```cpp
if (_pCandidateWnd)
{
    HWND hCand = _pCandidateWnd->_GetWnd();
    HWND prev  = hCand ? GetWindow(hCand, GW_HWNDPREV) : nullptr;
    debugPrint(L"#135 end-of-StartCandidateList: hCand=%p prevInZ=%p", hCand, prev);
}
```

If `prevInZ` is non-null at end-of-StartCandidateList and belongs to Firefox (`pid` matches firefox.exe, class contains `MozillaWindowClass` or `MozillaTransitionWindowClass`), and `exStyle & WS_EX_TOPMOST`, that confirms H2 (Firefox stamped after us).

If `_pCandidateWnd` is **null** at the entry log, that confirms H1.

If `prevInZ` is one of *our own* windows (shadow, scrollbar, settings dialog) or `prev == NULL`, the popup isn't competing in the topmost band — H3 territory, dig further.

### Data collection

1. Build Release|x64 with VS 2026 MSBuild via `.claude/scripts/build_135.ps1` (handles the `buildInfo.cmd` PATHEXT issue).
2. Deploy DLL to the active install path. Verify with timestamp / version that the new DLL is actually loaded.
3. Repro per "Reproduction" section above.
4. Save `%APPDATA%\DIME\debug.txt` from the moment the user types `dj` through Space-commit and the phrase candidate appearing.
5. Attach trace to this doc (in `## Trace evidence` section, to be added).

### Verifying the new DLL is actually loaded

A common failure mode in prior issues (per [docs/#127_ISSUE.md](#127_ISSUE.md) verification pattern) is the IME host still mapping the old DLL. Quick verifications:

- Check `BuildInfo.h` regenerated timestamp / commit-hash — visible to a small `debugPrint(L"#135 build=%ls", BUILDINFO_GIT_SHORT_SHA)` on DLL attach.
- `tasklist /m DIME.dll` in PowerShell — verify which processes have loaded it; check timestamp of the loaded path.
- If unsure: kill all hosts that map the IME (Firefox, Notepad, Chrome, etc.), re-register the DLL, restart hosts.

## Why we are NOT shipping any code fix yet

- The speculative `SetWindowPos(HWND_TOPMOST)` fix in the reuse branch was reverted from `src/UIPresenter.cpp` on 2026-05-15 after visual repro showed it didn't work.
- Adding any further mechanism (e.g. a timer to keep re-stamping topmost, a `SetWinEventHook` for popups appearing) before we know which hypothesis is correct risks landing another speculative change that masks rather than fixes the issue.
- The user has previously emphasised (project guideline §7): *if you've failed 3+ times on the same issue, do more research before the next move.* This is failure #1, and the right next move is evidence.

## Files touched so far in this branch (rolled back)

| File | Status |
| --- | --- |
| `src/UIPresenter.cpp` | Edit applied 2026-05-15, then reverted same day. Currently unchanged from `master`. |
| `src/UIConstants.h`   | Edit applied 2026-05-15 (timer constants for proposed stay-on-top timer), then reverted. Currently unchanged from `master`. |
| `docs/#135_ISSUE.md`  | This document. |

## Next steps (in order)

1. **Add the `DEBUG_PRINT` instrumentation listed above.** No behavioural code changes.
2. Build, deploy, repro in Firefox.
3. Capture `debug.txt`, paste relevant section under "Trace evidence" below.
4. Pick the hypothesis the trace supports.
5. Design a targeted fix for that hypothesis.
6. Implement, verify visually, then comment out `DEBUG_PRINT` and remove the temporary `#135` log lines.

## Trace evidence (2026-05-15)

`DEBUG_PRINT` enabled in `UIPresenter.cpp`, `CandidateWindow.cpp`, `CandidateHandler.cpp` plus a temporary 500ms-delayed `TIMERPROC` Z-order walker attached after `_StartCandidateList` returns. Repro: Firefox Google search box, type `dj` → Space.

### At the moment of phrase show (synchronous trace)

```text
CUIPresenter::_StartCandidateList()
CUIPresenter::BeginUIElement(), _isShowMode = 1, _uiElementId = 2, hresult = 0
#135 _StartCandidateList: _pCandidateWnd=000001BC690817A0 (reuse=1)              ← reuse branch confirmed
#135 reuse-branch entry: hCand=...3D2124 visible=1 prevInZ=...030746
#135   prevInZ class=ThumbnailDeviceHelperWnd pid=1452 exStyle=0x8280088 topmost=1 rect=(0,0,1,1)
...
#135 end-of-StartCandidateList: hCand=...3D2124 visible=1 prevInZ=...030746
#135   prevInZ class=ThumbnailDeviceHelperWnd pid=1452 exStyle=0x8280088 topmost=1 rect=(0,0,1,1)
```

At `t=0` our candidate is at the top of the topmost band. The only thing above us is `ThumbnailDeviceHelperWnd` (PID 1452 = `dwm.exe` / system, 1×1 px, invisible). **No competitor.**

### 500ms after phrase show (delayed snapshot)

```text
#135 DELAYED Z-snap: hCand=...3D2124 visible=1 prevInZ=...0200A7C
#135   above[0] hwnd=...0200A7C class=MozillaDropShadowWindowClass pid=20312 visible=1 exStyle=0x88 topmost=1 style=0x96000000 rect=(2547,149,3092,604)
#135   above[1] hwnd=...030746  class=ThumbnailDeviceHelperWnd ...
#135   above[2..8] (system helpers, all invisible or off-screen)
```

**Firefox's `MozillaDropShadowWindowClass` is now in slot `above[0]` — directly above our candidate.** PID 20312 = `firefox.exe`. The popup is:

- `visible=1` — actually being painted
- `exStyle=0x88` — `WS_EX_TOOLWINDOW (0x80) | WS_EX_TOPMOST (0x08)`
- `topmost=1`
- `rect=(2547, 149, 3092, 604)` — fully envelops our candidate at (2682, 151, …)

Our candidate is now `above[1]+1` in Z-order, sitting *below* Firefox's popup.

### Verdict

**Hypothesis H2 confirmed.** Firefox creates / stamps `MozillaDropShadowWindowClass` with `WS_EX_TOPMOST` **after** our phrase show finishes. Standard Win32 topmost-band rules apply: the most-recently-stamped TOPMOST wins, and that's Firefox. Our one-time speculative `SetWindowPos(HWND_TOPMOST)` (rolled back earlier) was correctly placed but ran *before* Firefox's stamp, so it lost the race.

**H1 and H3 disproved.** Reuse branch IS taken (`reuse=1`). Nothing exotic about the Z-order at our show time. The competition is a plain Win32 TOPMOST race that we are losing on timing.

## Fix (landed 2026-05-15)

### Strategy

While the candidate window is visible, re-stamp `HWND_TOPMOST` periodically so we win Z-order against any host TOPMOST popup that opens asynchronously after our show. Stop on `_Show(FALSE)` / window destroy. Skip the re-stamp when no *real* competitor is above us (avoids 33 Hz no-op `SetWindowPos` cascading into shadow-window re-stacking and visual flicker).

### Implementation iterations (what was tried, what worked)

The path to the landed fix went through three iterations as evidence accumulated.

**Iteration 1 — one-shot `SetWindowPos(HWND_TOPMOST)` at phrase-show time (failed).** Inserted in the reuse branch of `_StartCandidateList` before `Show()`. Visual test on 2026-05-15: phrase candidate still covered by Firefox popup. Reverted.

**Iteration 2 — `_StartTimer` / `_OnTimerID` polling loop (failed).** Added `STAY_ON_TOP_TIMER_ID` and used the existing `CBaseWindow::_StartTimer` mechanism. With `DEBUG_PRINT` enabled, `_StartTimer` confirmed: `_pTimerUIObj` set non-null, valid HWND, valid `_pUIWnd`. But `CCandidateWindow::_OnTimerID` was **never invoked** for `STAY_ON_TOP_TIMER_ID` — `WM_TIMER` dispatch through `CBaseWindow::_WindowProc` silently dropped it on this HWND. Root cause unknown — possibly something specific to how the candidate window's message pump interacts with TSF host threads. The same `NotifyWindow` / `ScrollBarWindow` code path is known to work elsewhere, so this is candidate-window-specific. Documented as TODO; worked around in iteration 3.

**Iteration 3 — raw `SetTimer(hwnd, ID, ms, TIMERPROC)` polling loop (works).** Bypasses the BaseWindow `WM_TIMER` dispatcher entirely by registering a `TIMERPROC` callback directly with Win32. The diagnostic `DELAYED Z-snap` proved this mechanism fires reliably on the same HWND. After landing this, visual test confirmed phrase candidate stays above Firefox popup. However: at 100ms tick interval, brief blink visible every ~second (Firefox's popup wins for up to 100ms between our re-stamps).

**Iteration 3 follow-ups (final):**
- Reduced tick to **30ms** (below human perception threshold, ~2 frames at 60Hz).
- Added a **"skip if no real competitor" check** inside the TIMERPROC: walks `GW_HWNDPREV` chain, ignores invisible windows and tiny system helpers (≤8×8 px like `ThumbnailDeviceHelperWnd`, our own shadow). Only calls `SetWindowPos(HWND_TOPMOST)` if a real visible window is above us. Eliminates 33 Hz of no-op `SetWindowPos` cascading into shadow `_OnOwnerWndMoved` → shadow re-stacking → visible flicker when no race is happening.

Visually verified: phrase candidate stays above Firefox autocomplete, no blink, no flicker. CHN/ENG notify, reverse-lookup notify, special-code notify, candidate scrollbar — all unaffected.

### Why not other approaches

- **`SetWinEventHook` (event-driven).** Cleaner in principle. Rejected because: (a) DIME is loaded into every TSF-using host as a per-process DLL, so a global hook means N hooks system-wide for N hosts; (b) accessibility events are not guaranteed to fire for layered / hardware-accelerated popups; (c) lifecycle is fragile — leaked hooks persist; (d) the polling cost is already negligible (~33 Hz × 1-3 sec/candidate × mostly-no-op syscalls).
- **Unconditional `SWP_NOZORDER` removal in `CBaseWindow::_Show`.** Would affect notify slots, scroll-bar, settings dialog — broader behavioural change requiring its own audit. Candidate-window scope is sufficient.
- **Per-show tick counter (1-second cap).** Initially planned with `_stayOnTopTicksLeft` member. Dropped after iteration 3: running while visible is simpler, `KillTimer` is automatic on `_Show(FALSE)` and window destroy, and the "skip if no real competitor" check makes long-lived sessions effectively free.

### Final landed code

#### `src/UIConstants.h`

```cpp
constexpr UINT_PTR STAY_ON_TOP_TIMER_ID = 39778; // #135: re-stamp HWND_TOPMOST to beat host TOPMOST popups that open async after our show (Firefox MozillaDropShadowWindowClass)
constexpr UINT    STAY_ON_TOP_TICK_MS   = 30;    // Tick interval — runs while candidate is visible, KillTimer on hide / destroy. 30ms keeps the cover-gap below human perception (~16ms frame)
```

#### `src/CandidateWindow.cpp` — `CCandidateWindow::_Show`

Added after the existing `SetCapture` / `ReleaseCapture` block at the end of `_Show`:

```cpp
// #135: keep candidate above any host WS_EX_TOPMOST popup that opens async
// after our show (Firefox autocomplete races our show by ~50-500ms). Re-stamp
// HWND_TOPMOST every STAY_ON_TOP_TICK_MS while window is visible. Using a raw
// TIMERPROC instead of _StartTimer / _OnTimerID because the latter's WM_TIMER
// dispatch path silently drops timer events on the candidate HWND for reasons
// not yet root-caused — but raw TIMERPROC on the same HWND fires reliably
// (proven by the diagnostic DELAYED Z-snap that lives on this HWND too).
// SetTimer with an existing ID just resets the period — no stacking.
if (isShowWnd)
{
    HWND h = _GetWnd();
    if (h)
    {
        SetTimer(h, UI::STAY_ON_TOP_TIMER_ID, UI::STAY_ON_TOP_TICK_MS,
            [](HWND hwnd, UINT, UINT_PTR, DWORD) -> void
            {
                if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;

                // Only re-stamp when a visible non-trivial window is above us in the
                // topmost band. Walks GW_HWNDPREV chain and ignores system helpers
                // (tiny 1x1 rects like ThumbnailDeviceHelperWnd, our own shadow, etc.).
                // Avoids 33Hz no-op SetWindowPos calls that cascade into shadow
                // re-stacking and visual flicker.
                HWND cur = GetWindow(hwnd, GW_HWNDPREV);
                int  depth = 0;
                while (cur && depth < 20)
                {
                    if (IsWindowVisible(cur))
                    {
                        RECT rc;
                        if (GetWindowRect(cur, &rc))
                        {
                            int w = rc.right - rc.left;
                            int hgt = rc.bottom - rc.top;
                            if (w > 8 && hgt > 8)
                            {
                                // Real visible competitor — re-stamp ourselves.
                                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                                return;
                            }
                        }
                    }
                    cur = GetWindow(cur, GW_HWNDPREV);
                    depth++;
                }
            });
    }
}
else
{
    HWND h = _GetWnd();
    if (h) KillTimer(h, UI::STAY_ON_TOP_TIMER_ID);
}
```

### Why this is safe

- Scope is limited to `CCandidateWindow` (first-tier + phrase). Notify slots, scroll-bar, shadow, settings — untouched.
- `SetWindowPos(HWND_TOPMOST, …, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)` only updates Z-order; no move, no resize, no focus change.
- The skip-if-no-competitor check ensures `SetWindowPos` runs only when needed — when no real competitor exists, the TIMERPROC just walks a few HWNDs and returns. No `WM_WINDOWPOSCHANGING` cascade into the shadow window.
- `KillTimer` on `_Show(FALSE)`. `DestroyWindow` also implicitly kills all the window's timers — safe across crash / abnormal cleanup paths.
- Shadow window's `WM_WINDOWPOSCHANGING` handler ([CandidateWindow.cpp:441-473](../src/CandidateWindow.cpp#L441-L473)) only forces `SWP_NOZORDER` when `hwndInsertAfter == _pShadowWnd->_GetWnd()`. We pass `HWND_TOPMOST` (not the shadow), so the guard doesn't trip. Shadow `_OnOwnerWndMoved` only fires when we actually re-stamp (i.e. a competitor was detected), which is rare.
- Cost: 33 Hz timer tick + GW_HWNDPREV chain walk (5-20 syscalls) while visible. Modern Windows handles millions of syscalls/sec — utterly below noise floor. When no competitor, zero `SetWindowPos`. When competitor exists, exactly one `SetWindowPos` per tick to win the race.

### Open TODO (out of scope for #135)

**`_StartTimer` / `_OnTimerID` mechanism silently fails on `CCandidateWindow`.** Iteration 2 confirmed `_pTimerUIObj` was set correctly and `SetTimer` succeeded, but `_OnTimerID` was never invoked for the `STAY_ON_TOP_TIMER_ID` — the WM_TIMER message never reached `CBaseWindow::_WindowProc`'s dispatcher (no `CCandidateWindow::_OnTimer():` log lines even with `DEBUG_PRINT` enabled). The same code path works for `NotifyWindow` / `ScrollBarWindow`, so this is candidate-window-specific. Not investigated further because the raw `TIMERPROC` workaround is just as correct and well-isolated. If this mechanism is ever needed on the candidate window (e.g. for fade animation revival), root-cause it first.

## Verification

### Visual repro

| Step | Before fix | After fix |
|---|---|---|
| Type `dj` + Space in Firefox Google search box | Phrase candidate items 1-3 covered by Firefox autocomplete popup; only items 4-0 visible below | Phrase candidate fully visible above Firefox autocomplete |
| Same in Firefox URL bar | Same coverage | Fully visible |
| Same in non-Firefox host (Notepad, Word, Edge, VS Code, Teams) | Already fine | Still fine, no regression |
| First-tier candidate during composition | Already fine | Still fine, no regression |

### Trace evidence supporting the fix

While `DEBUG_PRINT` and the temporary `DELAYED Z-snap` diagnostic were active during fix development, the 500ms post-show Z-order walker confirmed that the unfixed build's `above[0]` was `MozillaDropShadowWindowClass pid=20312 visible=1 topmost=1 rect=(2547,149,3092,604)` — Firefox popup directly above our candidate at (2682, 151). After the fix landed, the same trace shows only invisible / tiny system-helper windows above us; `MozillaDropShadowWindowClass` no longer appears in the chain.

### Regression checks

1. **LINE Win+. emoji panel after triggering phrase candidates** — emoji still inserts (no `WS_EX_TOPMOST` / probe interaction broken).
2. **CHN/ENG notify slots, reverse-lookup notify, special-code notify** — unchanged (different windows, different class, no interaction with our `STAY_ON_TOP_TIMER_ID`).
3. **Long-lived candidate windows** (user pauses on first-tier for 10+ seconds before selecting) — no visible flicker, no measurable CPU. The skip-if-no-competitor check keeps the tick cost at "walk a handful of HWNDs" when no real race is happening.
4. **Rapid show/hide cycles** (typing fast, candidate flips frequently) — `KillTimer` on `_Show(FALSE)` stops the loop cleanly; subsequent `_Show(TRUE)` calls `SetTimer` again. `SetTimer` with the same ID resets the period rather than stacking, so no leaks.
5. **Notify slots that overlap with the candidate** — restack via `_RestackVisibleSlots` is independent of our TOPMOST tick. Visual check confirms no displacement.
6. **CHN/ENG notify in Firefox** — separate window, not subject to this fix. If a similar Z-order race shows up for notify slots in the future, the same pattern can be ported there.

### Cleanup (completed 2026-05-15)

- Removed temporary `#135 DIAG` log lines from `_StartCandidateList`.
- Removed the `#135 DELAYED Z-snap` diagnostic `TIMERPROC` block.
- Restored `//#define DEBUG_PRINT` (commented out) in `UIPresenter.cpp`, `CandidateWindow.cpp`, `CandidateHandler.cpp`.
- Kept the `STAY_ON_TOP_TIMER_ID` constant and the `CCandidateWindow::_Show` TIMERPROC as the permanent landed change.

## Follow-up: flicker + persistent Firefox coverage when reverse-lookup notify is on (2026-05-18)

### Symptom

After the 2026-05-15 fix shipped, a follow-up screenshot shows: in Firefox with the reverse-lookup notify (e.g. Dayi `0^7^` hint) visible alongside the phrase candidate, the candidate is half-covered by Firefox autocomplete AND flickering. With reverse-lookup notify OFF, the original fix still works correctly.

### Root cause

The TIMERPROC's "skip if no real competitor" check rejected only invisible windows and tiny (≤8×8) system helpers. It did **not** distinguish foreign-process competitors (Firefox `MozillaDropShadowWindowClass`) from our own-process `WS_EX_TOPMOST` windows (notify slots, shadow, scrollbar).

The reverse-lookup notify ([NotifyWindow.cpp:131](../src/NotifyWindow.cpp#L131)) is `WS_EX_TOPMOST`, visible, and well above 8×8 (~60–100 px wide). When stamped TOPMOST at creation, it sits above the candidate in z-order. Every 30 ms the candidate's TIMERPROC walked `GW_HWNDPREV`, found the notify, and called `SetWindowPos(HWND_TOPMOST)`. Each such call cascaded through:

1. Candidate `WM_WINDOWPOSCHANGING` → `_pShadowWnd->_OnOwnerWndMoved` → shadow `SetWindowPos`.
2. Candidate `WM_WINDOWPOSCHANGED` → `_pShadowWnd->_OnOwnerWndMoved` again.

33 Hz shadow re-stacking — the visible flicker. Worse, the constant re-stamp churn against our own notify wasted the budget that was supposed to win the race against Firefox's autocomplete popup, so items 1–6 of the phrase candidate sat behind the popup.

### Fix

In `CCandidateWindow::_Show`'s TIMERPROC competitor walk ([CandidateWindow.cpp:352-401](../src/CandidateWindow.cpp#L352-L401)), call `GetClassWord(hwnd, GCW_ATOM)` for each candidate competitor and skip windows whose class atom matches one of DIME's registered atoms: `AtomCandidateWindow`, `AtomCandidateShadowWindow`, `AtomCandidateScrollBarWindow`, `AtomNotifyWindow`, `AtomNotifyShadowWindow`. Our own notify / shadow / scrollbar are never real Z-order competitors with the candidate — they're either off-axis (notify is to the left, no visual overlap), or part of the candidate's own visual (shadow, scrollbar).

Only non-DIME `WS_EX_TOPMOST` windows (Firefox `MozillaDropShadowWindowClass`, etc.) now trigger the re-stamp. Visually: notify and candidate coexist with no shadow re-stack churn; Firefox popup still gets out-stamped on each tick.

#### Why PID-based filtering does NOT work here

The first attempt at this follow-up used `GetWindowThreadProcessId` with `GetCurrentProcessId()` as the filter — that's wrong, because **DIME is a TSF IME DLL loaded in-process into the host application (firefox.exe)**. Every window DIME creates while running in Firefox returns Firefox's PID — and so does Firefox's own autocomplete popup. The PID filter therefore skipped Firefox's popup too, and the candidate stopped re-stamping entirely (visible regression: both notify and candidate covered by the Firefox popup, no longer "winning" the race). Class-atom comparison is the correct discriminator because DIME registers its own class atoms regardless of host.

### Files modified (follow-up)

| File | Change |
|---|---|
| `src/CandidateWindow.cpp` | Added `GetClassWord(hwnd, GCW_ATOM)` check inside the TIMERPROC's GW_HWNDPREV walk, skipping windows whose class atom is one of DIME's registered atoms. |

## Files modified (cumulative)

| File | Change |
|---|---|
| `src/UIConstants.h`     | Added `STAY_ON_TOP_TIMER_ID` and `STAY_ON_TOP_TICK_MS` constants. |
| `src/CandidateWindow.cpp` | Added raw `SetTimer` / `TIMERPROC` block at the end of `_Show(BOOL)` that re-stamps `HWND_TOPMOST` while visible (with skip-if-no-real-foreign-competitor); `KillTimer` on hide. |

No header changes, no preference changes, no new methods.
