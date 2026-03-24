# DPI Awareness in DIME — Code Review & Improvement Roadmap

This document reviews all DPI-aware code in both the **DIME.dll** (IME DLL) and **DIMESettings.exe** (settings GUI), assesses what works well and what doesn't, and proposes a phased roadmap toward full Per-Monitor V2 DPI awareness.

---

## 1. Current Implementation

### 1.1 DIME.dll (IME DLL)

#### Dynamic Shcore.dll Loading

At DLL attach time, DIME conditionally loads `GetDpiForMonitor` from Shcore.dll on Windows 8.1+:

- **`DllMain.cpp:121-131`** — `LoadLibrary("Shcore.dll")` → `GetProcAddress("GetDpiForMonitor")` → stored via `CConfig::SetGetDpiForMonitor()`
- **`BaseStructure.h:50`** — Function pointer typedef: `_T_GetDpiForMonitor`
- **`Config.h:285-286, 371-372`** — Static members: `_GetDpiForMonitor` (function pointer), `_dpiY` (cached DPI)
- **`Config.cpp:93-94`** — Initialized to `nullptr` / `0`
- **`Globals.cpp:102`** / **`Globals.h:175-176`** — `hShcore` handle stored globally

This preserves Windows 7 compatibility — if Shcore.dll is unavailable, all DPI code gracefully falls back to `GetDeviceCaps(LOGPIXELSY)`.

#### Per-Monitor DPI Font Creation

The core DPI logic lives in `CConfig::SetDefaultTextFont()`:

- **`Config.cpp:762-804`**
  1. Queries monitor DPI: `MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST)` → `GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)`
  2. Falls back to `GetDeviceCaps(hDC, LOGPIXELSY)` on Win7 or if `GetDpiForMonitor` is unavailable
  3. Converts point size to pixel height: `MulDiv(fontSize, logPixelY, 72)`
  4. Caches `_dpiY` and deletes/recreates the font when DPI changes
  5. Fallback: if `CreateFont` fails, uses `SystemParametersInfo(SPI_GETICONTITLELOGFONT)` for face name

- **`UIPresenter.cpp:918`** — Calls `SetDefaultTextFont(parentWndHandle)` on Win8+ before showing candidate window

#### Candidate/Notify Window Layout

- **`CandidateWindow.cpp:69`** — Initial `_cyRow = UI::CANDIDATE_ROW_WIDTH` (30px baseline)
- **`CandidateWindow.cpp:384`** — Overridden by `_cyRow = candSize.cy * 5/4` after `GetTextExtentPoint32` — this is **implicitly DPI-aware** because the font height is already DPI-scaled
- **`CandidateWindow.cpp:221, 627, 719, 792`** / **`CandidateWindow.h:64`** — Scrollbar width: `GetSystemMetrics(SM_CXVSCROLL) * 3/2` (system-DPI-aware, **not** per-monitor)
- **`ScrollBarWindow.cpp:111-112`** — Scrollbar button size: same `GetSystemMetrics * 3/2` pattern
- **`Define.h:60, 71`** — `CANDWND_BORDER_WIDTH(1)` and `NOTIFYWND_BORDER_WIDTH(1)` — hardcoded 1px (acceptable; 1px borders are standard practice at all DPIs)
- **`ShadowWindow.cpp:42`** — `SHADOW_ALPHANUMBER = UI::SHADOW_WIDTH` (5px, **unscaled**)

#### Language Bar Icons

- **`LanguageBar.cpp:603, 610`** — `LoadImage` with `GetSystemMetrics(SM_CXSMICON)` / `SM_CYSMICON` (system-DPI-aware, not per-monitor)

#### Config Dialog (hosted by both DIME.dll and DIMESettings.exe)

- **`ConfigDialog.cpp:2169-2177`** — Dialog font initialization: queries per-monitor DPI via `GetDpiForMonitor`, overrides `GetDeviceCaps` result — **correct**
- **`ConfigDialog.cpp:1121, 1135, 1154`** — Font chooser handler: uses `GetDeviceCaps(hdc, LOGPIXELSY)` only — **missing per-monitor override** (inconsistent with line 2169)
- **`ConfigDialog.cpp:2030-2039, 2482-2491`** — RichEdit `CHARFORMAT2W.yHeight` conversion: `MulDiv(abs(lfHeight), 72 * 20, dpi)` — correctly converts to twips using DPI

#### UIConstants.h

- **`UIConstants.h:39-44`** — Comment block states: *"baseline values defined at 96 DPI... scaled at runtime"*
- **Reality:** Most constants (`CANDIDATE_ROW_WIDTH`, `SHADOW_WIDTH`, etc.) are used as raw pixel values without explicit DPI scaling multiplication. The comment is **misleading** — some values are implicitly scaled (via font-derived measurements), but others are not.

### 1.2 DIMESettings.exe

- **`DIMESettings.cpp:98-103`** — Defines `_PROCESS_DPI_AWARENESS` enum locally
- **`DIMESettings.cpp:214-226`** — On Win 8.1+, calls `SetProcessDpiAwareness(Process_System_DPI_Aware)` via dynamic loading from Shcore.dll
- This means DIMESettings renders correctly on the **primary monitor** but does **not** rescale when dragged to a monitor with different DPI

### 1.3 Manifest / Build Configuration

- **No DPI-awareness manifest** embedded in either DIME.dll or DIMESettings.exe (no `<dpiAware>` or `<dpiAwareness>` in any `.manifest` file)
- **`installer/DIME-Universal.nsi:27`** — `ManifestDPIAware true` applies to the **installer only**
- **`DIME.rc`** — Dialog resources use dialog units (Windows auto-scales these based on system DPI and font)
- DPI awareness is entirely **runtime-declared** via API calls

### 1.4 Missing Capabilities

- **No `WM_DPICHANGED` handler** — zero hits in entire codebase. Windows won't adapt when:
  - User drags a window to a monitor with different DPI
  - User changes display scale in Windows Settings while the IME is running
- **No `GetSystemMetricsForDpi`** — all `GetSystemMetrics` calls return system-DPI values, not per-monitor values
- **No `SystemParametersInfoForDpi`** or `AdjustWindowRectExForDpi`

---

## 2. Code Review Assessment

### Strengths

| # | What | Why it's good |
|---|------|---------------|
| 1 | `SetDefaultTextFont` per-monitor DPI | Correctly queries the monitor containing the window, not just system DPI |
| 2 | Dynamic Shcore.dll loading | Preserves Win7 compatibility without conditional compilation |
| 3 | Font cache invalidation | Compares `_dpiY` with current monitor DPI; recreates font on change |
| 4 | Text-measurement-derived row height | `_cyRow = candSize.cy * 5/4` naturally scales with DPI-scaled fonts |
| 5 | Robust fallback chain | Per-monitor DPI → `GetDeviceCaps(LOGPIXELSY)` → system default font |
| 6 | RichEdit twips conversion | Correct `MulDiv(lfHeight, 1440, dpi)` formula |

### Issues

| # | Severity | Location | Issue |
|---|----------|----------|-------|
| 1 | Medium | Entire codebase | No `WM_DPICHANGED` handler — no live DPI change response |
| 2 | Medium | DIMESettings.cpp:220 | System-DPI-Aware only — won't rescale on different-DPI monitor |
| 3 | Low | CandidateWindow, ScrollBarWindow | `GetSystemMetrics(SM_CXVSCROLL)` is system-DPI, not per-monitor |
| 4 | Low | ConfigDialog.cpp:1121,1135,1154 | Font chooser uses `GetDeviceCaps` without per-monitor override |
| 5 | Low | UIConstants.h:39-44 | Comment claims "scaled at runtime" but most constants are raw pixels |
| 6 | Low | ShadowWindow.cpp:42 | Shadow width (5px) unscaled — appears thinner at high DPI |
| 7 | Info | Both projects | No manifest DPI declaration — relies entirely on runtime API calls |

---

## 3. Improvement Roadmap

### Phase 1 — Quick Wins (no new APIs, no behavioral changes)

**Goal:** Fix inconsistencies and misleading documentation.

**Fallback:** No new Windows APIs introduced. All changes use existing `_GetDpiForMonitor` which is already guarded by a null check and falls back to `GetDeviceCaps(LOGPIXELSY)`. Safe on Windows 7+.

1. **Fix UIConstants.h comment** — Accurately describe which values are implicitly scaled (via font measurement) and which are used as raw pixels.

2. **ConfigDialog font chooser per-monitor DPI** — At `ConfigDialog.cpp:1121, 1135, 1154`, add the same per-monitor DPI override used at line 2169-2177:
   ```cpp
   // Before (system DPI only):
   lf.lfHeight = -MulDiv(..., GetDeviceCaps(hdc, LOGPIXELSY), 72);

   // After (per-monitor when available):
   int logPixelY = GetDeviceCaps(hdc, LOGPIXELSY);
   if (_GetDpiForMonitor) {       // null on Win7 — skipped, uses GetDeviceCaps value
       HMONITOR monitor = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
       UINT dpiX, dpiY;
       _GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
       if (dpiY > 0) logPixelY = dpiY;
   }
   lf.lfHeight = -MulDiv(..., logPixelY, 72);
   ```

3. **Add a DPI scaling helper** — Centralize the scaling pattern:
   ```cpp
   // In a shared header (e.g., BaseStructure.h or a new DpiHelper.h)
   inline int ScaleForDpi(int baseValue, UINT dpi) {
       return MulDiv(baseValue, dpi, USER_DEFAULT_SCREEN_DPI); // USER_DEFAULT_SCREEN_DPI = 96
   }
   ```
   Pure math — no Windows API dependency. Works on all versions.

**Files to modify:** `UIConstants.h`, `ConfigDialog.cpp`, `BaseStructure.h` (or new header)

### Phase 2 — Per-Monitor Improvements for DIME.dll

**Goal:** Make the IME candidate/notify windows fully per-monitor DPI correct.

**Minimum Windows version for new APIs:** Windows 10 1607 (build 14393)

**Fallback:** All new APIs (`GetSystemMetricsForDpi`, `SetThreadDpiAwarenessContext`) must be loaded dynamically from User32.dll at DLL attach time, following the same pattern used for `GetDpiForMonitor` from Shcore.dll today. On older Windows:

- `GetSystemMetricsForDpi` unavailable → fall back to `GetSystemMetrics` (system-DPI, same as current behavior)
- `SetThreadDpiAwarenessContext` unavailable → skip the call; windows are created under the host process's DPI context (same as current behavior)
- `WM_DPICHANGED` never arrives → handler is dead code, no harm (same as current behavior)

1. **Replace `GetSystemMetrics` with `GetSystemMetricsForDpi`** — For scrollbar widths and icon sizes. Load dynamically like `GetDpiForMonitor`:
   ```cpp
   // Runtime loading from User32.dll (Win10 1607+)
   typedef int (WINAPI *_T_GetSystemMetricsForDpi)(int nIndex, UINT dpi);

   // Usage with fallback:
   int vscroll = _GetSystemMetricsForDpi
       ? _GetSystemMetricsForDpi(SM_CXVSCROLL, monitorDpi)
       : GetSystemMetrics(SM_CXVSCROLL);  // Win7/8 fallback
   ```
   Apply to: `CandidateWindow.cpp`, `CandidateWindow.h`, `ScrollBarWindow.cpp`, `LanguageBar.cpp`

2. **Scale shadow width** — Use `ScaleForDpi(UI::SHADOW_WIDTH, monitorDpi)` in `ShadowWindow.cpp`. When `monitorDpi` is unknown (Win7, no window handle), pass `GetDeviceCaps(LOGPIXELSY)` as fallback, or use 96 (the constant baseline).

3. **Wrap window creation with thread DPI context** — Before creating candidate/notify windows, set Per-Monitor awareness on the thread:
   ```cpp
   // Win10 1607+ only — loaded dynamically, skipped if unavailable
   DPI_AWARENESS_CONTEXT oldCtx = nullptr;
   if (_SetThreadDpiAwarenessContext)
       oldCtx = _SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

   // ... CreateWindowEx for candidate/notify window ...

   if (oldCtx && _SetThreadDpiAwarenessContext)
       _SetThreadDpiAwarenessContext(oldCtx);  // restore host process's context
   ```
   This enables `WM_DPICHANGED` delivery for DIME's windows without affecting the host process. On Win7/8.1 the function pointer is null — the call is skipped entirely.

4. **Add `WM_DPICHANGED` handler** to `CandidateWindow` and `NotifyWindow`:
   ```cpp
   case WM_DPICHANGED: {
       UINT newDpi = HIWORD(wParam);
       // Recreate font at new DPI
       CConfig::SetDefaultTextFont(hWnd);
       // Resize/reposition using the suggested rect
       RECT* prcNewWindow = (RECT*)lParam;
       SetWindowPos(hWnd, NULL,
           prcNewWindow->left, prcNewWindow->top,
           prcNewWindow->right - prcNewWindow->left,
           prcNewWindow->bottom - prcNewWindow->top,
           SWP_NOZORDER | SWP_NOACTIVATE);
       return 0;
   }
   ```
   On Win7/8: this message is never sent, so the handler never executes. No behavioral change.

**Files to modify:** `CandidateWindow.cpp`, `NotifyWindow.cpp`, `ScrollBarWindow.cpp`, `ShadowWindow.cpp`, `LanguageBar.cpp`, `DllMain.cpp` (new function pointer loading), `Globals.h/cpp` or `Config.h/cpp` (new function pointer storage)

### Phase 3 — DIMESettings Per-Monitor V2

**Goal:** Make the settings GUI fully DPI-aware across monitors.

**Minimum Windows version:** Windows 10 1703 (build 15063) for Per-Monitor V2

**Fallback:** A 3-tier runtime fallback chain ensures correct behavior on all supported Windows versions. Each API is loaded dynamically; if unavailable, the next tier is tried. On Win7 (where none exist), no DPI awareness is declared — same as current behavior.

1. **Upgrade DPI awareness level** — Replace `SetProcessDpiAwareness(Process_System_DPI_Aware)` with a tiered approach:
   ```cpp
   // All loaded dynamically — function pointers are null on older Windows

   // Tier 1: Try Win10 1703+ Per-Monitor V2
   if (_SetProcessDpiAwarenessContext &&
       _SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
       // Success — PMv2 gives automatic non-client, dialog, and common control scaling
   }
   // Tier 2: Fall back to Win8.1+ Per-Monitor V1
   else if (_SetProcessDpiAwareness &&
            SUCCEEDED(_SetProcessDpiAwareness(Process_Per_Monitor_DPI_Aware))) {
       // Per-monitor but no automatic scaling — need WM_DPICHANGED handler
   }
   // Tier 3: Fall back to Win8.1+ System DPI Aware
   else if (_SetProcessDpiAwareness) {
       _SetProcessDpiAwareness(Process_System_DPI_Aware);
       // Same as current behavior
   }
   // Tier 4: Win7 — no DPI awareness API available, no-op
   ```

2. **Add `WM_DPICHANGED` handler** to the settings dialog:
   - Recalculate font sizes
   - Resize dialog and controls proportionally
   - Update RichEdit character formats

   On Win7/8: this message is never sent (no per-monitor awareness), so the handler never executes.
   On Win10 with PMv2 (Tier 1): most rescaling is handled automatically by Windows; the handler only needs to update custom-drawn elements and fonts.
   On Win8.1/Win10 with PMv1 (Tier 2): full manual rescaling needed in the handler.

3. **Consider application manifest** — Embed a DPI-awareness manifest in DIMESettings.exe:
   ```xml
   <application xmlns="urn:schemas-microsoft-com:asm.v3">
     <windowsSettings>
       <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
       <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2,PerMonitor</dpiAwareness>
     </windowsSettings>
   </application>
   ```
   The manifest itself has built-in fallback: Windows reads `<dpiAwareness>` on Win10 1607+ (picks PMv2 or PMv1), and falls back to `<dpiAware>` on Win8.1 (picks system or per-monitor). On Win7, both elements are ignored — no DPI awareness declared.

   Note: This manifest applies to DIMESettings.exe **only**. For DIME.dll (an in-process COM DLL loaded into arbitrary host processes), a manifest is **not appropriate** — the DLL must use `SetThreadDpiAwarenessContext` as described in Phase 2.

**Files to modify:** `DIMESettings.cpp`, `ConfigDialog.cpp`, potentially add a `.manifest` file for DIMESettings.exe

---

## 4. Windows Version Compatibility Matrix

| Feature | Minimum Windows | API |
|---------|----------------|-----|
| System DPI awareness | Win 8.1 | `SetProcessDpiAwareness` (Shcore.dll) |
| Per-monitor DPI query | Win 8.1 | `GetDpiForMonitor` (Shcore.dll) |
| Per-monitor V1 awareness | Win 8.1 | `SetProcessDpiAwareness(Process_Per_Monitor_DPI_Aware)` |
| `GetSystemMetricsForDpi` | Win 10 1607 | User32.dll |
| `SystemParametersInfoForDpi` | Win 10 1607 | User32.dll |
| `GetDpiForWindow` | Win 10 1607 | User32.dll |
| Per-Monitor V2 awareness | Win 10 1703 | `SetProcessDpiAwarenessContext` (User32.dll) |
| `WM_DPICHANGED` | Win 8.1 (PMv1) | Window message |
| `WM_DPICHANGED_BEFOREPARENT/AFTERPARENT` | Win 10 1703 (PMv2) | Window message |

**DIME's current minimum supported OS:** Windows 7 — all new DPI APIs must be loaded dynamically with graceful fallback.

---

## 5. Key Consideration: IME DLL vs. Standalone App

DIME.dll is loaded **in-process** into host applications (Notepad, Word, browsers, etc.). This has important implications:

- **Cannot set process-level DPI awareness** — the host process owns that decision
- **Must use `SetThreadDpiAwarenessContext`** (Win10 1607+) scoped to the thread creating IME windows, then restore the original context afterward
- **Cannot embed a DPI manifest** in the DLL — manifests only apply to EXEs
- The host process's DPI awareness affects how Windows delivers `WM_DPICHANGED` to DIME's windows

This is why DIME.dll currently uses only `GetDpiForMonitor` queries (reactive) rather than declaring DPI awareness (proactive). Phase 2 would need to wrap window creation with thread-level DPI context switching.

---

## 6. Test Plan

DPI scaling logic is difficult to test in headless CI because it depends on monitors, DPI settings, and GDI resources. The strategy is to split testing into **unit-testable pure logic** and **manual/integration verification**.

### 6.1 Unit Tests (CppUnitTest, in `src/tests/`)

These tests exercise the scaling math and helper functions without requiring a real display or window.

#### A. `ScaleForDpi` helper (once Phase 1 adds it)

```
TEST_CLASS(DpiHelperTests)

  ScaleForDpi_At96Dpi_ReturnsOriginal
    Assert: ScaleForDpi(16, 96)  == 16

  ScaleForDpi_At120Dpi_Scales125Percent
    Assert: ScaleForDpi(16, 120) == 20

  ScaleForDpi_At144Dpi_Scales150Percent
    Assert: ScaleForDpi(16, 144) == 24

  ScaleForDpi_At192Dpi_Scales200Percent
    Assert: ScaleForDpi(16, 192) == 32

  ScaleForDpi_ZeroDpi_ReturnsZero
    Assert: ScaleForDpi(16, 0)   == 0

  ScaleForDpi_ZeroBase_ReturnsZero
    Assert: ScaleForDpi(0, 144)  == 0

  ScaleForDpi_OddValues_RoundsCorrectly
    Assert: ScaleForDpi(5, 144) == MulDiv(5, 144, 96)  // == 7 or 8, verify MulDiv rounding
```

#### B. Font size point-to-pixel conversion

Verify the `MulDiv(fontSize, dpi, 72)` formula used throughout:

```
TEST_CLASS(FontDpiConversionTests)

  PointToPixel_12pt_At96Dpi
    Assert: MulDiv(12, 96, 72) == 16

  PointToPixel_12pt_At120Dpi
    Assert: MulDiv(12, 120, 72) == 20

  PointToPixel_12pt_At144Dpi
    Assert: MulDiv(12, 144, 72) == 24

  PointToPixel_10pt_At96Dpi
    Assert: MulDiv(10, 96, 72) == 13  // verify rounding

  TwipsConversion_16px_At96Dpi
    // RichEdit twips = MulDiv(abs(lfHeight), 72 * 20, dpi)
    Assert: MulDiv(16, 1440, 96) == 240  // 12pt = 240 twips

  TwipsConversion_20px_At120Dpi
    Assert: MulDiv(20, 1440, 120) == 240  // still 12pt = 240 twips
```

#### C. `SetDefaultTextFont` DPI caching logic

Test that `_dpiY` caching works correctly (font is recreated only when DPI changes):

```
TEST_CLASS(SetDefaultTextFontCacheTests)

  // These require mocking GetDpiForMonitor or testing with nullptr hWnd (fallback path)

  NullHwnd_UsesFallbackDpi
    // Call SetDefaultTextFont(nullptr) — should use GetDeviceCaps fallback
    // Verify font handle is created (non-null Global::defaultlFontHandle)

  SameDpi_DoesNotRecreatFont
    // Call SetDefaultTextFont twice with same DPI conditions
    // Verify font handle pointer is unchanged on second call

  DpiChanged_RecreatesFont
    // Simulate DPI change by modifying _dpiY, then call SetDefaultTextFont
    // Verify font handle pointer changes
```

Note: Tests that create real HFONT objects need `TEST_METHOD_CLEANUP` to delete them and reset `Global::defaultlFontHandle = nullptr`.

### 6.2 Manual Verification Checklist

These scenarios require a real display and cannot be automated in unit tests:

#### Single Monitor Tests

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| 1 | Default DPI (100%) | Set display to 100% scaling, open any app, activate DIME | Candidate window text is readable, scrollbar proportional |
| 2 | 125% scaling | Set display to 125%, activate DIME | Font and layout scale proportionally |
| 3 | 150% scaling | Set display to 150%, activate DIME | No clipping, borders visible, shadow visible |
| 4 | 200% scaling (4K) | Set display to 200%, activate DIME | All elements scale, no pixelation in text |
| 5 | Custom scaling (e.g. 175%) | Set display to 175%, activate DIME | Smooth scaling, no layout breakage |

#### Multi-Monitor Tests (after Phase 2)

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| 6 | Move to higher-DPI monitor | Start typing on 100% monitor, drag focus app to 200% monitor | Candidate window rescales (font, scrollbar, shadow) |
| 7 | Move to lower-DPI monitor | Start typing on 200% monitor, drag focus app to 100% monitor | Candidate window shrinks proportionally |
| 8 | DIMESettings across monitors | Open settings on 100% monitor, drag to 200% monitor | Dialog and controls rescale (Phase 3) |

#### DPI Change While Running

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| 9 | Change DPI with IME active | While DIME candidate window is showing, change display scaling in Settings | Candidate window updates to new scale |
| 10 | Change DPI with settings open | While DIMESettings dialog is open, change display scaling | Settings dialog updates (Phase 3) |

### 6.3 Test File Location

- Unit tests: `src/tests/DpiScalingTest.cpp` (new file)
- Add to `src/tests/DIMETests.vcxproj` with `<PrecompiledHeader>Use</PrecompiledHeader>` for all 8 platform/config combos
- Include `pch.h` (already includes `Config.h`, `BaseStructure.h`, `windows.h`)
