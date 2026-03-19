# Issue #92: Hotkey for Simplified/Traditional Chinese Toggle

## Context

Users who frequently switch between traditional and simplified Chinese output currently must open the settings dialog (Ctrl+\) to change the setting — too slow for frequent use (see [issue #92](https://github.com/jrywu/DIME/issues/92)). This plan adds a hotkey to toggle `DoHanConvert` in real-time, with persistence via `CConfig::WriteConfig()`.

## Hotkey Choice

**`Ctrl+Shift+H`** — H for HanConvert (漢字轉換).

- Rare conflict with application hotkeys (Ctrl+Shift+H is seldom used).
- TSF preserved keys are intercepted at the IME level before apps see them when the IME is **active (Chinese mode)**.
- Follows the existing modifier pattern (Shift+Space for full/half-width, Ctrl+\ for config).

## Design

### What already exists (reuse these)

| Component | Location | Purpose |
|-----------|----------|---------|
| `CConfig::SetDoHanConvert()` / `GetDoHanConvert()` | `src/Config.h:249-250` | In-memory toggle |
| `CConfig::WriteConfig()` | `src/Config.h:266` | Persist to INI |
| `GetSCFromTC()` / `GetTCFromSC()` | `src/CompositionProcessorEngine.h:103-104` | TC↔SC conversion |
| `SetupHanCovertTable()` | `src/SetupCompositionProcesseorEngine.cpp:1308-1361` | Lazy-load TCSC.cin |
| `CandidateHandler.cpp:184-187` | `src/CandidateHandler.cpp` | Applies SC conversion on commit |
| Preserved key infrastructure | `SetupPreserved()`, `OnPreservedKey()` | Hotkey registration + dispatch |
| `showChnEngNotify()` / `showFullHalfShapeNotify()` | `src/DIME.h:204-205` | On-screen popup notifications |

### No language bar button

Per user decision — hotkey + on-screen notification popup only. No new compartment or language bar button needed.

### Persistence

Toggle calls `CConfig::SetDoHanConvert()` then `CConfig::WriteConfig(Global::imeMode)` so the setting persists across sessions, matching existing behavior of settings changed via the config dialog.

## Implementation Steps

### Step 1: Define new GUID for preserved key

**File: `src/Globals.h`** (~line 77)
- Add: `extern const GUID DIMEGuidHanConvertPreserveKey;`

**File: `src/Globals.cpp`** (~line 121)
- Add: new GUID constant (generate with `uuidgen` or hardcode a new one)

### Step 2: Add preserved key member

**File: `src/CompositionProcessorEngine.h`** (~line 245)
- Add: `XPreservedKey _PreservedKey_HanConvert;`

### Step 3: Register the hotkey in SetupPreserved()

**File: `src/SetupCompositionProcesseorEngine.cpp`** (~line 1097, inside `SetupPreserved()`)

Add after the Config preserved key block:
```cpp
TF_PRESERVEDKEY preservedKeyHanConvert = { 0 };
preservedKeyHanConvert.uVKey = 'H';
preservedKeyHanConvert.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
SetPreservedKey(Global::DIMEGuidHanConvertPreserveKey, preservedKeyHanConvert,
    L"Toggle Simplified/Traditional", &_PreservedKey_HanConvert);

InitPreservedKey(&_PreservedKey_HanConvert, pThreadMgr, tfClientId);
```

### Step 4: Handle the hotkey in OnPreservedKey()

**File: `src/SetupCompositionProcesseorEngine.cpp`** (~line 1293, inside `OnPreservedKey()`)

Add a new `else if` branch before the final `else`:
```cpp
else if (IsEqualGUID(rguid, _PreservedKey_HanConvert.Guid))
{
    BOOL currentState = CConfig::GetDoHanConvert();
    CConfig::SetDoHanConvert(currentState ? FALSE : TRUE);
    CConfig::WriteConfig(Global::imeMode);

    // Lazy-load TCSC table if enabling for the first time
    if (!currentState)  // just enabled
        SetupHanCovertTable();

    // Show notification: 繁體 or 簡體
    if (_pTextService)
        _pTextService->showHanConvertNotify(currentState ? FALSE : TRUE);

    *pIsEaten = TRUE;
}
```

### Step 5: Add notification function

**File: `src/DIME.h`** (~line 205)
- Add declaration: `void showHanConvertNotify(BOOL isSimplified, UINT delayShow = 0);`

**File: find the implementation of `showChnEngNotify`** — implement `showHanConvertNotify` following the same pattern, showing 「繁」/「簡」.

### Step 6: Uninitialize preserved key on cleanup

**File: `src/CompositionProcessorEngine.h`** or wherever `UninitPreservedKey` is called for the other keys — ensure `_PreservedKey_HanConvert.UninitPreservedKey(pThreadMgr)` is called during teardown.

### Step 7: Update README

**File: `README.md`**
- Add a row in the hotkey/feature section documenting the new Ctrl+Shift+H toggle.

## Files to Modify (Summary)

1. `src/Globals.h` — new GUID declaration
2. `src/Globals.cpp` — new GUID definition
3. `src/CompositionProcessorEngine.h` — new `XPreservedKey` member
4. `src/SetupCompositionProcesseorEngine.cpp` — register + handle hotkey
5. `src/DIME.h` — `showHanConvertNotify()` declaration
6. `src/DIME.cpp` or notification source file — `showHanConvertNotify()` implementation
7. `README.md` — document the feature

## Verification

1. **Build**: Compile all platforms (Win32, x64, ARM64) — no new dependencies
2. **Manual test**:
   - Enable DIME, switch to Chinese mode
   - Press Ctrl+Shift+H → verify notification popup shows 「簡」
   - Type a character → verify simplified output (e.g. 「国」instead of「國」)
   - Press Ctrl+Shift+H again → verify popup shows 「繁」, output reverts to traditional
   - Restart app → verify the setting persisted (still in the last-selected mode)
3. **Existing tests**: Run `DIMETests` — no regressions expected since this is additive
4. **Edge case**: Toggle while composing — the `DoHanConvert` flag is checked at commit time (`CandidateHandler.cpp:184`), so toggling mid-composition should affect the next committed character
