# Fix: Shift English Mode Intercepts Symbol Radical Keys (Issue #134)

## Context

The "Shift 英數輸入模式" feature lets users type inverted-case/ASCII by holding Shift. Users with custom IME tables that map shifted symbols (e.g. `<>:?"{}_+()*&^%$#@!` via `%keyname`/`%chardef`) can no longer input those radicals — pressing `Shift+,` outputs `<` as English ASCII instead of entering composition.

The owner confirmed the approach: _"先檢查字根，若是字根鍵即時帶 shift 也應該進組字"_ — if the shifted character itself is a radical, it should go into composition.

A test build was shared. The reporter confirmed `<>?:*_+` are now fixed, but `"[]\!@#$%^&()` (i.e. `"`, `{`, `|`, `}`, `!`, `@`, `#`, `$`, `%`, `^`, `&`, `(`, `)`) still output as half-width ASCII.

---

## Committed Fix (commit 312c1a0) — Partial

Added the shifted-char check **after** the base-char radical check in [`src/KeyProcesser.cpp`](../src/KeyProcesser.cpp) ~line 680.

**Why it is incomplete — two remaining root causes.**

---

## Fix A — Reposition the shifted-char check

### Problem

For symbols like `!@#$%^&()` and `"`, the base key (digits 1–0, or apostrophe `'`) is **not** a radical. The Shift English block returns `FALSE` at the base-char check before ever reaching our shifted-char check. The base-char check must run **after** the shifted-char check, not before it.

### Code change

**File**: [`src/KeyProcesser.cpp`](../src/KeyProcesser.cpp) lines 656–689.

Replace the current block with:

```cpp
            // ① Shifted char is itself a radical — enter composition immediately
            if (IsVirtualKeyKeystrokeComposition(uCode, pwch, pKeyState, KEYSTROKE_FUNCTION::FUNCTION_NONE))
                return TRUE;

            // ② Base char determines whether Shift English mode applies at all
            WCHAR baseChar = (WCHAR)MapVirtualKey(uCode, MAPVK_VK_TO_CHAR);
            if (Global::imeMode == IME_MODE::IME_MODE_DAYI && IsDayiAddressChar(baseChar))
            {
                // Dayi address char — skip radical map check, fall through to Shift English
            }
            else
            {
                WCHAR upper = towupper(baseChar);
                if (upper >= 32 && upper < 32 + MAX_RADICAL && (UINT)(upper - 32) < _KeystrokeComposition.Count())
                {
                    const _KEYSTROKE& ks = *_KeystrokeComposition.GetAt(upper - 32);
                    if (ks.Function == KEYSTROKE_FUNCTION::FUNCTION_NONE)
                        return FALSE;  // base not a radical — bypass to system
                }
                else
                {
                    return FALSE;  // out of range — bypass to system
                }
            }

            if (pKeyState)
            {
                pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
                pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
            }
            return TRUE;
```

### Side effects

| Case | Behaviour |
|------|-----------|
| Shift+A–Z | `IsVirtualKeyKeystrokeComposition` returns `FALSE` for A–Z with Shift held (existing guard line 893–894). Falls through to base-char check → Shift English. **Unchanged.** |
| Dayi address chars | Shifted chars of Dayi address keys (`~"{}\_\|`) are not Dayi radicals → `IsVirtualKeyKeystrokeComposition` returns `FALSE` → falls through to Dayi special case → Shift English. **Unchanged in practice.** |
| `pKeyState` state | When `IsVirtualKeyKeystrokeComposition` returns `FALSE` it resets `pKeyState` to `{NONE, NONE}`; the base-char path then either returns `FALSE` (caller ignores `pKeyState`) or overwrites it with `FUNCTION_SHIFT_ENGLISH_INPUT`. No conflict. |

---

## Fix B — Expand MAX_RADICAL to cover `{`, `|`, `}`, `~`

### Problem

`MAX_RADICAL = 64` means the composition table covers only ASCII 32–96. Characters `{`=123, `|`=124, `}`=125, `~`=126 are above this ceiling. Two cascading failures:

1. **Load time** ([`src/SetupCompositionProcesseorEngine.cpp:871`](../src/SetupCompositionProcesseorEngine.cpp#L871)): the cin loader skips any keyname entry with char > `MAX_RADICAL + 32 = 96`, so `{|}~` are **never loaded** into `_KeystrokeComposition`.
2. **Lookup time** ([`src/KeyProcesser.cpp:881`](../src/KeyProcesser.cpp#L881)): `IsVirtualKeyKeystrokeComposition` rejects `c > 32 + MAX_RADICAL` — returns `FALSE` even if the table were populated.

Fix A alone cannot fix these: even with the repositioned check, `IsVirtualKeyKeystrokeComposition` returns `FALSE` for `{|}~` because they were never loaded.

### Code changes

#### [`src/Define.h:159`](../src/Define.h#L159)
```cpp
// before
#define MAX_RADICAL 64 // ascii table 0x20 (0d32) ~ 0x60 (0d96)

// after
#define MAX_RADICAL 96 // ascii table 0x20 (0d32) ~ 0x80 (0d128)
```

#### [`src/PhoneticComposition.h`](../src/PhoneticComposition.h) — extend both static arrays

`vpStandardKeyTable[MAX_RADICAL+1]` and `vpEtenKeyToStandardKeyTable[MAX_RADICAL+1]` are statically sized. Growing MAX_RADICAL by 32 requires appending 32 zeros to each (chars 97–128 cover the `a`–`~` range; phonetic has no strokes there):

```cpp
// At the end of vpStandardKeyTable — append after the existing '`' entry (currently last):
// '{',  '|',  '}',  '~', and 28 more zeros to reach index 96
   0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
```
Same for `vpEtenKeyToStandardKeyTable`.

### Side effects

| Area | Effect |
|------|--------|
| Phonetic mode | New table slots all zero; `towupper()` maps a–z to A–Z (indices 33–58), so lookups never reach the new range. Zero behavioral change. |
| Array / Dayi | Radical tables use A–Z + digits only (< char 96). Zero behavioral change. |
| Config validation | `cu <= 32 + MAX_RADICAL` shifts ceiling 96 → 128. Still rejects non-printable chars. No functional issue. |
| DictionarySearch asserts | `size() < MAX_RADICAL` ceiling raised. Still protects correctly. |
| Memory | 32 extra `_KEYSTROKE` structs (~few hundred bytes). Negligible. |
| Chars > 128 | Still rejected by `c > MAX_RADICAL + 32 = 128`. |

---

## Feature Request — Configurable digit-key behavior in Shift English mode

Requested by user **internlin** in issue comments. Currently Shift English mode applies the same CapsLock-inversion logic to all key types. The request is a per-category preference specifically for the digit row.

### Options

| Index | Chinese label | Behaviour |
|-------|--------------|-----------|
| 0 | `CapsLock 關輸出符號` | CapsLock OFF → symbol (`!`), CapsLock ON → digit (`1`) **(default — current code behaviour)** |
| 1 | `CapsLock 關輸出數字` | CapsLock OFF → digit (`1`), CapsLock ON → symbol (`!`) |
| 2 | `永遠輸出符號` | Always output the Shift symbol (`!@#…`) regardless of CapsLock |

### Implementation — all files

#### 1. [`src/BaseStructure.h`](../src/BaseStructure.h) — new enum (near `IME_SHIFT_MODE`)
```cpp
enum class IME_SHIFT_ENGLISH_DIGIT_MODE
{
    SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL = 0,  // CapsLock OFF → symbol (!), ON → digit (1) — default
    SHIFT_ENGLISH_DIGIT_CAPS_OFF_DIGIT,        // CapsLock OFF → digit (1), ON → symbol (!)
    SHIFT_ENGLISH_DIGIT_ALWAYS_SYMBOL          // always symbol regardless of CapsLock
};
```

#### 2. [`src/Config.h`](../src/Config.h) — accessors (near `SetIMEShiftMode` / `GetIMEShiftMode`)
```cpp
static void SetShiftEnglishDigitMode(IME_SHIFT_ENGLISH_DIGIT_MODE mode);
static IME_SHIFT_ENGLISH_DIGIT_MODE GetShiftEnglishDigitMode();
```

#### 3. [`src/Config.cpp`](../src/Config.cpp)
- Static member declaration (near `_imeShiftMode`):
  ```cpp
  IME_SHIFT_ENGLISH_DIGIT_MODE CConfig::_shiftEnglishDigitMode = IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL;
  ```
- Registry load/save: use key name `L"ShiftEnglishDigitMode"` (see SettingsKeys.h below)
- Default reset: `_shiftEnglishDigitMode = IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL;`

#### 4. [`src/DIMESettings/SettingsKeys.h`](../src/DIMESettings/SettingsKeys.h)
```cpp
constexpr const wchar_t ShiftEnglishDigitMode[] = L"ShiftEnglishDigitMode";
```

#### 5. [`src/DIMESettings/SettingsPageLayout.cpp`](../src/DIMESettings/SettingsPageLayout.cpp) — add row after `CTRL_NUMERIC_PAD` (九宮格數字鍵盤)

```cpp
{ CTRL_SHIFT_ENGLISH_DIGIT_MODE, RowType::ComboBox, RowAction::None, ... L"Shift英數模式數字鍵行為" }
```
Add `CTRL_SHIFT_ENGLISH_DIGIT_MODE` to the `CTRL_ID` enum in the same file.

#### 6. [`src/DIMESettings/SettingsWindow.cpp`](../src/DIMESettings/SettingsWindow.cpp) — combo population (near `CTRL_IME_SHIFT_MODE` block ~line 2738)
```cpp
if ((h = FindControl(wd, CTRL_SHIFT_ENGLISH_DIGIT_MODE))) {
    const wchar_t* d[] = {
        L"CapsLock 關輸出符號",
        L"CapsLock 關輸出數字",
        L"永遠輸出符號"
    };
    ComboAddAndSelect(h, d, 3, s.shiftEnglishDigitMode);
}
```

#### 7. [`src/DIMESettings/SettingsController.cpp`](../src/DIMESettings/SettingsController.cpp)
- Snap read (~line 138): `snap.shiftEnglishDigitMode = (int)CConfig::GetShiftEnglishDigitMode();`
- Snap apply (~line 181): `CConfig::SetShiftEnglishDigitMode((IME_SHIFT_ENGLISH_DIGIT_MODE)snap.shiftEnglishDigitMode);`
- Combo handler (~line 535): `case CTRL_SHIFT_ENGLISH_DIGIT_MODE: s.shiftEnglishDigitMode = sel; break;`

#### 8. [`src/KeyHandler.cpp`](../src/KeyHandler.cpp) — `_HandleCompositionShiftEnglishInput` digit section (~lines 541–559)

Branch on the new config for digit VK codes (`VK_0`–`VK_9`). VK_0–VK_9 equal ASCII `'0'`–`'9'`, so `(WCHAR)code` gives the base digit directly without any `ToUnicodeEx` call:
```cpp
else if (code >= '0' && code <= '9') {
    WCHAR symbol = wch;         // shifted symbol ('!', '@', '$', …)
    WCHAR digit  = (WCHAR)code; // VK_4 == '4', etc.
    switch (CConfig::GetShiftEnglishDigitMode()) {
        case IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_DIGIT:
            outputChar = isCapsLockOn ? symbol : digit; break;
        case IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_ALWAYS_SYMBOL:
            outputChar = symbol; break;
        default: // SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL — current behaviour
            outputChar = isCapsLockOn ? digit : symbol; break;
    }
}
```

#### 9. [`src/ConfigDialog.cpp`](../src/ConfigDialog.cpp) — legacy settings dialog
Add the same combo box near the existing `IDC_COMBO_IME_SHIFT_MODE` block with the same 3 Chinese strings. Requires a new dialog control ID `IDC_COMBO_SHIFT_ENGLISH_DIGIT_MODE`.

---

## Fix C — Gate the wildcard bypass on numpad and digit-key preferences

### Actual current flow for `*` and `?`

There are **two separate code paths** that handle wildcards. Which one fires depends on the `_isShiftedEnglish` flag in `KeyEventSink`:

1. **Pre-existing `_isShiftedEnglish` gate** ([`src/KeyEventSink.cpp:193-204`](../src/KeyEventSink.cpp#L193-L204)) — set TRUE after any prior keystroke handled as `FUNCTION_SHIFT_ENGLISH_INPUT`, cleared when Shift is released (with a `Shift+Backspace` carve-out). While TRUE, any wildcard char with Shift held is forced to `FUNCTION_SHIFT_ENGLISH_INPUT` — output as ASCII via `_HandleCompositionShiftEnglishInput`, NOT composition.
2. **Wildcard bypass in `KeyProcesser.cpp:824`** — fires only when path 1 is skipped (`_isShiftedEnglish=FALSE`). Catches `*` / `?` and returns `FUNCTION_INPUT`, sending the char into composition as a wildcard.

Resulting behaviour for `*` (Shift+8) **before Fix C**:

| Scenario | Path taken | Result |
|----------|-----------|--------|
| Fresh Shift hold, `*` is the first keystroke (`_isShiftedEnglish=FALSE`) | KeyProcesser wildcard bypass | `*` enters composition |
| Continuing Shift hold after any Shift+letter (`_isShiftedEnglish=TRUE`) | KeyEventSink:193 → Shift English output | `*` output as ASCII |

So in **typical use** (Shift+a, Shift+b, Shift+8…), `*` does NOT enter composition — it's already output as ASCII via path 1. The fresh-first-keystroke case is the only one that still falls through to composition.

The KeyHandler digit branch added in the feature commit (`KeyHandler.cpp _HandleCompositionShiftEnglishInput`, digit section ~line 541) already handles the per-pref digit/symbol output for path 1. So `Shift+8` after `Shift+a` already follows the new digit-mode pref correctly.

**Numpad `*` (VK_MULTIPLY)** — no Shift is held, so path 1 never fires. Path 2 (wildcard bypass) always catches it → composition. The existing `九宮格數字鍵盤 = NUMERIC` gating only lives in `IsVirtualKeyKeystrokeComposition` (which the wildcard bypass doesn't call), so it's effectively bypassed for the wildcard case.

### What Fix C addresses

- **Numpad `*` / `?` (Change 2)**: gate the wildcard bypass on the existing 九宮格 = NUMERIC pref so numpad keys output to system instead of entering wildcard composition. This is a real gap — without Change 2, numpad `*` enters composition regardless of the 九宮格 setting.
- **Digit-row fresh-keystroke (Change 1)**: route `Shift+digit` through the Shift English block when the new digit-mode pref wants digit output, so the very first Shift+8 in a Shift hold also outputs `8`. Without Change 1, fresh Shift+8 enters wildcard composition even when pref=digit. Continuing-Shift presses already handled by the KeyEventSink flag path.

### Code change

**File**: [`src/KeyProcesser.cpp`](../src/KeyProcesser.cpp)

**Change 1** — line 653-654, conditionally allow wildcards into Shift English when digit-mode wants digit output:

```cpp
        // Allow wildcards through the Shift English block only when the digit-row pref
        // resolves to "digit output" — in that case we want to output the digit (e.g. 8),
        // not the wildcard char (*). Otherwise, wildcards fall through to the wildcard
        // bypass below as before.
        bool digitRowWantsDigit = false;
        if (uCode >= '0' && uCode <= '9')
        {
            bool capsOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            switch (CConfig::GetShiftEnglishDigitMode())
            {
            case IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_DIGIT:    digitRowWantsDigit = !capsOn; break;
            case IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_ALWAYS_SYMBOL:     digitRowWantsDigit = false;   break;
            default: /* SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL */                        digitRowWantsDigit = capsOn;  break;
            }
        }

        if (iswprint(c) && c != L' ' &&
            (digitRowWantsDigit ||
             !(IsWildcardChar(c) && !(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && c == L'*'))))
        {
            // existing Fix A body
            ...
        }
```

**Change 2** — line 796, gate the wildcard bypass on the numpad pref:

```cpp
        // Numpad NUMERIC pref: numpad keys output directly to system, even for * / ?
        if (IsWildcardChar(*pwch) && !(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && *pwch == L'*') &&
            !(CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_NUMERIC &&
              uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
        {
            if (pKeyState)
            {
                pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
                pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_INPUT;
            }
            return TRUE;
        }
```

### Behaviour matrix

**Numpad `*` (VK_MULTIPLY), buffer empty:** Shift not held, so `_isShiftedEnglish` path never applies — only path 2 matters.

| 九宮格 setting | Before | After |
|----------------|--------|-------|
| NUMERIC_PAD_NUMERIC (數字鍵盤輸入數字符號) | composition (wildcard) | output `*` to system |
| NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY (僅用數字鍵盤輸入字根) | composition (wildcard) | composition (wildcard) |
| Default (composition) | composition (wildcard) | composition (wildcard) |

**Digit-row Shift+8 → `*`, buffer empty, fresh first keystroke (`_isShiftedEnglish=FALSE`):**

| Digit-mode setting | CapsLock | Before | After |
|--------------------|----------|--------|-------|
| CapsLock 關輸出符號 (default) | OFF | composition (wildcard) | composition (wildcard) |
| CapsLock 關輸出符號 (default) | ON  | composition (wildcard) | output `8` |
| CapsLock 關輸出數字 | OFF | composition (wildcard) | output `8` |
| CapsLock 關輸出數字 | ON  | composition (wildcard) | composition (wildcard) |
| 永遠輸出符號 | any | composition (wildcard) | composition (wildcard) |

**Digit-row Shift+8 → `*`, buffer empty, continuing Shift hold (`_isShiftedEnglish=TRUE`):** unaffected by Fix C — already routed through `_HandleCompositionShiftEnglishInput` digit branch (added in the feature commit), so digit/symbol output already follows the new digit-mode pref.

| Digit-mode setting | CapsLock | Before & After |
|--------------------|----------|----------------|
| CapsLock 關輸出符號 (default) | OFF | output `*` |
| CapsLock 關輸出符號 (default) | ON  | output `8` |
| CapsLock 關輸出數字 | OFF | output `8` |
| CapsLock 關輸出數字 | ON  | output `*` |
| 永遠輸出符號 | any | output `*` |

### Side effects

| Case | Behaviour |
|------|-----------|
| Buffer non-empty (`fComposing`) | Unaffected — wildcard composition for active search still works; Shift English block is guarded by `!fComposing`. |
| Phonetic mode | `*` unchanged (not a wildcard in Phonetic); `?` unchanged for both numpad and digit-row paths. |
| Numpad `/` (VK_DIVIDE) | Same Change 2 gating as `*` — when pref = NUMERIC, outputs `/` to system. |
| Digit-row Shift+digit where shifted char is a mapped radical but **not** a wildcard (e.g. `!@#$%^&()`) | Fix A still applies — composition vs Shift English decided by Fix A's `IsVirtualKeyKeystrokeComposition` check. The digit-mode pref overrides for wildcards only (Change 1); radicals continue to enter composition so the radical-input feature keeps working. |
| `_isShiftedEnglish` flag path | Unchanged by Fix C — `KeyEventSink:193-204` continues to intercept wildcards during a continuing Shift hold, routing them to `_HandleCompositionShiftEnglishInput`. The KeyHandler digit branch (committed in the feature commit) handles digit/symbol output per pref. |

---

## README.md — New Documentation Section

### Where to insert

Two insertion points in [`README.md`](../README.md):

**1. 輸入設定 table** (after line 340, after the `九宮格數字鍵盤` row — logically grouped with numeric key behaviour):

```markdown
| Shift英數模式數字鍵行為 | 設定在 Shift 英數輸入模式下，數字鍵的輸出行為（`CapsLock 關輸出數字，開輸出符號`、`CapsLock 關輸出符號，開輸出數字`、`永遠輸出符號`） |
```

**2. Shift 英數輸入模式 section** (after line 265, after the existing `> **數字鍵備註**` blockquote):

```markdown
#### 數字鍵行為設定

在「輸入設定」中的「Shift英數模式數字鍵行為」可調整數字鍵區在 Shift 英數模式下的輸出規則：

| 選項 | CapsLock OFF | CapsLock ON | 說明 |
|------|-------------|-------------|------|
| CapsLock 關輸出符號，開輸出數字 | Shift 字元（`!`） | 基礎數字（`1`） | **預設行為** |
| CapsLock 關輸出數字，開輸出符號 | 基礎數字（`1`） | Shift 字元（`!`） | |
| 永遠輸出符號 | Shift 字元（`!`） | Shift 字元（`!`） | 無論 CapsLock 狀態一律輸出符號 |

Shift+數字鍵的輸出對照：

- 「CapsLock 關輸出符號」（預設）：CapsLock OFF → `!@#$%^&*()`，CapsLock ON → `1234567890`
- 「CapsLock 關輸出數字」：CapsLock OFF → `1234567890`，CapsLock ON → `!@#$%^&*()`
- 「永遠輸出符號」：無論 CapsLock 狀態一律輸出 `!@#$%^&*()`

> **與字根互動**：當偏好設定為輸出符號（預設或「永遠輸出符號」）且該符號在自訂碼表中是字根（如 `(`、`!`），會進入組字而非直接輸出符號；若偏好設定為輸出數字（如「CapsLock 關輸出數字」且 CapsLock OFF），則一律直接輸出數字。萬用字元 `*` 不論偏好設定皆輸出為英數（不進入萬用字元搜尋；如需萬用字元搜尋，請放開 Shift 鍵以一般中文模式輸入）。
```

---

## Tests

No dedicated tests exist for the Shift English input path. Add to the existing test suite (`src/tests/`):

| Test | What to verify |
|------|---------------|
| `SettingsControllerTest.cpp` | New `shiftEnglishDigitMode` field round-trips correctly through snap/apply |
| `ConfigTest.cpp` | `GetShiftEnglishDigitMode` / `SetShiftEnglishDigitMode` persist to/from registry; default is `INVERTED` |
| New `KeyProcesserTest.cpp` (or add to existing) | Shift+digit where digit is not radical → composition when shifted char is radical; Shift+letter still triggers Shift English; Shift+digit where neither is radical → bypass |

No existing tests need modification — no existing test exercises the Shift English code path.

---

## Verification

1. Build Release|x64.
2. Load the user's cin from the issue (`!@#$%^&*()-=_+<>?:"[]{}\|~` as keynames).
3. Press each symbol that requires Shift — `!@#$%^&()` (Shift+digits), `"` (Shift+`'`), `{|}` (Shift+`[]\`), `<>?:_+` (Shift+symbol keys):
   - Expected: all enter composition, none output as half-width ASCII.
4. Verify Shift+A–Z still outputs inverted-case English.
5. Verify `Shift+1` in a table where `!` is **not** a radical passes through to the system.
6. Verify Array, Dayi, Phonetic modes are unaffected.
7. For the digit-mode preference: cycle through all 3 settings and confirm digit-row output changes accordingly.
