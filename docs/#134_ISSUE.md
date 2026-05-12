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
