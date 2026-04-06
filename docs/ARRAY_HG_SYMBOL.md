# Fix Array "hg" symbol input (hg0, hg1, etc.)

## Context

Array.cin has new `hg0`–`hg9` entries mapping to CJK Radicals Supplement (⺀⺁⺂...). Typing `hg0` in Array mode produces an error beep instead of showing candidates.

## Why hg0 doesn't work

Digits 0–9 are not in Array's radical map. The keystroke processing chain in `KeyProcesser.cpp` is:

1. **`IsEscapeInputChar(wch)`** (line 500) — checked first, before radical validation. If TRUE, key is accepted bypassing radical check. Currently has `_keystrokeBuffer.GetLength() > 1 → goto exit` at line 369, so it only handles buffer len 0 or 1.

2. **`IsVirtualKeyKeystrokeComposition()`** (line 789) — radical validation. Checks `_KeystrokeComposition` table. Digits are not radicals → returns FALSE → key rejected → error beep.

When user types `h`, `g`, `0`:
- `h` → radical ✓ (accepted normally)
- `g` → radical ✓ (accepted normally), buffer = `hg`
- `0` → `IsEscapeInputChar('0')`: buffer len=2, **exits early** (line 369: `> 1`) → `IsVirtualKeyKeystrokeComposition('0')`: digit not a radical → **rejected**

Compare with working `w1`:
- `w` → `IsEscapeInputChar('w')`: buffer len=0, `W` matched at line 377 → accepted via escape path
- `1` → `IsEscapeInputChar('1')`: buffer len=1, `W`+digit matched at line 390 → accepted → `FUNCTION_INPUT_AND_CONVERT` triggers lookup

## What needs to change

Two functions in `src/KeyProcesser.cpp`:

### 1. `IsEscapeInputChar()` (~line 369)

Change early exit from `> 1` to `> 2`, and add a `len==2` case for HG + digit:

```cpp
if (_keystrokeBuffer.Get() == nullptr || _keystrokeBuffer.GetLength() > 2)  // was > 1
    goto exit;
```

Add after the existing `len==1` block:

```cpp
else if (_keystrokeBuffer.GetLength() == 2)
{
    WCHAR c1 = towupper(*_keystrokeBuffer.Get());
    WCHAR c2 = towupper(*(_keystrokeBuffer.Get() + 1));
    if (Global::imeMode == IME_MODE::IME_MODE_ARRAY &&
        CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
        c1 == L'H' && c2 == L'G' && wch >= L'0' && wch <= L'9')
        return TRUE;
}
```

**Why**: This lets digit `0` bypass radical validation when the buffer already contains `hg`. Same pattern as `W` + digit at len==1.

### 2. `IsEscapeInput()` (~line 332)

Add a `len==3` case for HG + digit:

```cpp
if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && len==3 &&
    CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5) {
    WCHAR c2 = *(_keystrokeBuffer.Get() + 1);
    WCHAR c3 = *(_keystrokeBuffer.Get() + 2);
    if (towupper(c) == L'H' && towupper(c2) == L'G' && c3 >= L'0' && c3 <= L'9')
        return TRUE;
}
```

**Why**: This routes the `hg0` lookup through the escape/symbol path at line 414 (`CollectWord` with full buffer), same as `w1`.

### Not changed: `IsEscapeInputLeading()`

No modification needed. `h` and `g` are valid radicals — they get accepted through the normal radical path. `maxCodes=5` is sufficient for 3 chars. Only the digit `0` needs the escape bypass.

## Files

| File | Change |
|------|--------|
| `src/KeyProcesser.cpp` | `IsEscapeInputChar()` + `IsEscapeInput()` |

## Verification

1. Build DIME.sln — 0 errors
2. Array (non-BIG5): `hg0` → CJK Radical candidates (⺀⺁⺂...)
3. `h` alone → short code result (unchanged)
4. `hg` → normal 2-char composition (unchanged)
5. `w1` → existing symbol input (unchanged)
6. Array40 BIG5: `H` + special char (unchanged)
7. Regular keys: `a`, `1q` etc. (unchanged)
