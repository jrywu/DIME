# Fix: Shift English Mode Intercepts Symbol Radical Keys (Issue #134)

## Context

The "Shift 英數輸入模式" feature lets users type inverted-case/ASCII by holding Shift. When introduced, it checks whether the **base (unshifted) key** is a radical before activating. If the base key IS a radical (e.g. `,` for a user who mapped `,` → 素), it triggers Shift English output — even though the **shifted character** (e.g. `<`) might itself be a radical in the user's custom table.

Users with IME tables that map shifted-symbol characters as radicals (e.g. `<>:?"{}_+()*&^%$#@!` via `%keyname`/`%chardef`) can no longer input those radicals, because `Shift+,` outputs `<` as English ASCII instead of entering composition. The owner confirmed the fix: _"先檢查字根，若是字根鍵即時帶 shift 也應該進組字"_ — if the shifted character itself is a radical, it should go into composition.

## Root Cause

In [`src/KeyProcesser.cpp`](../src/KeyProcesser.cpp) lines 642–687, the Shift English block:
1. Gets the **base (unshifted)** character via `MapVirtualKey(uCode, MAPVK_VK_TO_CHAR)`
2. If base char IS a radical → falls through to `FUNCTION_SHIFT_ENGLISH_INPUT` ✗
3. If base char is NOT a radical → `return FALSE` (bypass to system) ✓

It never checks whether the **shifted character** (`*pwch`) is also a radical. For `Shift+,` with `<` as a radical: base=`,` is a radical, so Shift English activates — but `<` should have gone to composition.

## Fix

**File**: [`src/KeyProcesser.cpp`](../src/KeyProcesser.cpp), around line 679.

Insert 2 lines **before** the `FUNCTION_SHIFT_ENGLISH_INPUT` assignment, after the existing Dayi/baseChar radical checks pass:

```cpp
            // If the shifted char is itself a radical key, enter composition instead
            if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, KEYSTROKE_FUNCTION::FUNCTION_NONE))
                return TRUE;

            if (pKeyState)
            {
                pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
                pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
            }
            return TRUE;
```

### Why this works

`IsVirtualKeyKeystrokeComposition` (line 859) already uses `*pwch` (the actual shifted char) to look up the radical table. It:
- Returns `FALSE` for A–Z with Shift held (line 893–894) → letters still trigger Shift English ✓
- Returns `TRUE` + sets `pKeyState` correctly if `*pwch` is a valid radical → enters composition ✓
- Returns `FALSE` if out-of-range or no dictionary → falls through to Shift English ✓

No new logic needed — the existing function handles all edge cases including Phonetic mode, Dayi mode, and range bounds.

### What is unchanged

- Shift+letter (A–Z) behavior: unchanged (guarded by line 893 in `IsVirtualKeyKeystrokeComposition`)
- Dayi address char special case: unchanged (Dayi chars with no shifted-radical still get Shift English)
- Mid-composition symbol keys: already handled by the separate `fComposing` path at line 830
- IME modes without custom symbol radicals: unaffected (shifted char won't be in their radical table)

## Verification

1. Build Release|x64.
2. Load the user's custom cin table (or any table with symbols in `%keyname`).
3. Press `Shift+,` (or any symbol key whose shifted char is a radical): should enter composition with `<` rather than outputting `<` as ASCII.
4. Press `Shift+A` through `Shift+Z`: should still output inverted-case English letters.
5. Press `Shift+1` (where `!` is NOT a radical): should output `!` as ASCII (bypass unchanged).
6. Verify existing DIME built-in modes (Array, Dayi, Phonetic) are unaffected.
