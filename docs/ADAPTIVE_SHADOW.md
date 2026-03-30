# Adaptive Shadow Color

**Date:** 2026-03-30

---

## 1. Problem

The candidate/notify shadow uses `CConfig::GetEffectiveDarkMode()` to pick a single shadow color for the entire bitmap:

| IME mode | Shadow color | Alpha |
|----------|-------------|-------|
| Light / Custom | `RGB(0, 0, 0)` (black) | `SHADOW_MAX_ALPHA = 35` (~14%) |
| Dark | `RGB(200, 200, 200)` (light gray) | `SHADOW_MAX_ALPHA = 35` (~14%) |

This fails when the IME's color mode doesn't match the host app's background. Example: IME in dark mode on Win10 Notepad (no dark theme support) → light gray shadow on white background → invisible.

---

## 2. Solution

Sample the actual screen pixels behind the shadow window, then choose shadow color **per-pixel** based on background luminance.

### Why per-pixel works

`_InitShadow()` already iterates every pixel for SDF alpha computation. Adding a background luminance check per pixel is negligible overhead. The shadow window spans 14px beyond the candidate on all sides, so it may straddle two backgrounds (e.g. candidate overlapping the edge of a dark and light window). Per-pixel adaptation handles this correctly.

---

## 3. Implementation

**File:** `ShadowWindow.cpp` — `_InitShadow()` (lines 225–336)

### Step 1: Capture background

After the shadow DIB is allocated and zeroed (line 268), before the SDF loop:

1. `BitBlt` from the screen DC at the shadow window's position into a temporary bitmap
2. `GetDIBits` into a top-down pixel buffer for direct indexing

The shadow window is visible at this point (`_Show` calls `CBaseWindow::_Show(TRUE)` before `_InitShadow`), but `UpdateLayeredWindow` hasn't been called yet — so the screen shows the true background, not the shadow itself.

### Step 2: Per-pixel color selection

Replace the global `shadowR/G/B` variables (currently set once from `GetEffectiveDarkMode()`) with per-pixel computation inside the SDF loop:

1. Read background pixel at `(x, y)` from the captured buffer (Y-flipped: shadow DIB is bottom-up, capture is top-down)
2. Compute luminance: `lum = (299*R + 587*G + 114*B) / 1000`
3. `lum > 128` → dark shadow `RGB(0, 0, 0)`; else → light shadow `RGB(200, 200, 200)`
4. Pre-multiply and write pixel as before

### Step 3: Cleanup

`free()` the background buffer after the loop, before `UpdateLayeredWindow`.

---

## 4. When background is captured

`_InitShadow` is called at three existing sites. No new call sites needed.

| Call site | Trigger | Captures fresh background? |
|-----------|---------|---------------------------|
| `_Show(TRUE)` | Shadow first appears | Yes — shadow visible but no bitmap yet; screen shows true background |
| `_OnOwnerWndMoved(isResized=TRUE)` | Owner window resized | Yes — but in practice this rarely fires; candidate width is fixed at `_wndWidth` CJK characters |
| `_OnSettingsChanged` | Color mode changed | Yes |
| `_OnOwnerWndMoved(isResized=FALSE)` | Owner moved only | No `_InitShadow` call — only `_AdjustWindowPos` |

In practice, `_InitShadow` runs once per candidate appearance (`_Show(TRUE)`). The candidate width is determined by `_wndWidth` (configured page width), not by input content, so resize is rare. The background capture effectively happens once at the right moment — before `UpdateLayeredWindow`, with the true background visible.

---

## 5. Y-coordinate mapping

The shadow DIB uses `biHeight > 0` (bottom-up: row 0 = bottom of image). The captured background uses `biHeight = -size.cy` (top-down: row 0 = top of image). To read the correct background pixel for shadow pixel `(x, y)`:

```
bgY = (size.cy - 1) - y
pBg = pBgBits + (bgY * size.cx + x)
```

---

## 6. Files to modify

| File | Change |
|------|--------|
| `ShadowWindow.cpp` | `_InitShadow()`: capture background, per-pixel luminance → shadow color |

No header changes. No new members — the background buffer is local to `_InitShadow`.

---

## 7. Verification

1. **Dark IME on light host app** (Win10 Notepad): shadow should be dark (black), visible against white background
2. **Dark IME on dark host app** (VS Code dark theme): shadow should be light gray glow, visible against dark background
3. **Light IME on light host app**: shadow should be dark (same as current behavior)
4. **Mixed background** (candidate straddling two windows): shadow color should transition per-pixel at the boundary
5. **Resize candidate** (type longer candidates): shadow should re-render with fresh background capture
