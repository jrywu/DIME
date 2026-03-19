# Shift 鍵英數輸入模式技術文件

## 1. 功能說明

### 1.1 核心行為

**Shift 作為「英數輸入模式切換鍵」**：
- 按住 Shift 時，所有可列印 ASCII 字元（0x20-0x7E）完全繞過輸入法引擎
- 按鍵直接對應到英數字元或符號，不經過中文輸入法處理
- CapsLock 狀態決定輸出的字元形式
- **例外**：當「全形/半形切換」設定為「以 Shift-Space 熱鍵切換」時，Shift+Space 保留給全形/半形切換使用，不會輸入空格

### 1.2 CapsLock 控制輸出字元

- **字母鍵**：CapsLock OFF → 小寫、CapsLock ON → 大寫
  - 例：Shift+A → CapsLock OFF 產生 `a`、CapsLock ON 產生 `A`

- **數字鍵（0-9）**：CapsLock OFF → Shift 符號、CapsLock ON → 數字
  - 例：Shift+1 → CapsLock OFF 產生 `!`、CapsLock ON 產生 `1`
  - 例：Shift+2 → CapsLock OFF 產生 `@`、CapsLock ON 產生 `2`

- **其他符號鍵**：CapsLock OFF → 基本符號、CapsLock ON → Shift 符號
  - 例：Shift+- → CapsLock OFF 產生 `-`、CapsLock ON 產生 `_`
  - 例：Shift+= → CapsLock OFF 產生 `=`、CapsLock ON 產生 `+`

## 2. 使用範例

**注意**：以下範例假設「全形/半形切換」設定為「半型」或「全型」。若設定為「以 Shift-Space 熱鍵切換」，則 Shift+Space 會切換全形/半形，而非輸入空格；此時需要放開 Shift 鍵後按 Space 來輸入空格。

### 2.1 基本中英混合輸入

```
輸入目標：請使用 git commit 命令
操作方式：輸入「請使用」→ 按住 Shift 輸入 " git commit " → 輸入「命令」
說明：按住 Shift 可連續輸入英數及空格，Shift+Space 直接產生空格
```

### 2.2 電子郵件地址

```
輸入目標：user@example.com
操作方式：
  - CapsLock OFF：按住 Shift 輸入 "user" → 不放 Shift 輸入 "2" 產生 "@" → 繼續輸入 "example.com"
說明：CapsLock OFF 時數字鍵產生 Shift 符號，全程不需切換 CapsLock
```

### 2.3 含符號的密碼

```
輸入目標：Pass123!@#
操作方式：
  - CapsLock ON：按住 Shift 輸入 "P"
  - 切換 CapsLock OFF：按住 Shift 輸入 "ass123" → 繼續輸入 "1" 產生 "!"、"2" 產生 "@"、"3" 產生 "#"
說明：CapsLock OFF 時字母產生小寫、數字鍵產生 Shift 符號，全程 CapsLock OFF 即可
```

### 2.4 程式碼片段

```
輸入目標：int* ptr = nullptr;
操作方式：
- CapsLock OFF：按住 Shift 輸入 "int" → 不放 Shift 輸入 "8" 產生 "*" → 繼續輸入 " ptr = nullptr;"
說明：CapsLock OFF 時數字鍵產生 Shift 符號，Shift+Space 產生空格，全程不需切換 CapsLock
```

## 3. 實作說明

### 3.1 技術架構

**Shift+Space 處理機制**：
- Shift+Space 註冊為系統保留鍵（Preserved Key），確保輸入法優先處理
- 在 `OnPreservedKey` 處理器中檢查「全形/半形切換」設定：
  - 若設定為「以 Shift-Space 熱鍵切換」→ 執行全形/半形切換
  - 若設定為「半型」或「全型」→ 注入空格字元
- 設定變更立即生效，無需重啟應用程式

**其他 Shift+字元處理**：
- 在 `IsVirtualKeyNeed()` 中檢測 Shift 修飾鍵
- 透過 `_HandleCompositionShiftEnglishInput()` 處理字元轉換
- CapsLock 控制大小寫及符號形式

### 3.2 修改檔案

**檔案 1：`src/SetupCompositionProcesseorEngine.cpp`**

修改 `OnPreservedKey()` 函數，約 line 1249：

```cpp
else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
{
    if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
    {
        // When not in Shift-Space switching mode, inject a space character instead
        ITfDocumentMgr* pDocMgrFocus = nullptr;
        ITfContext* pContext = nullptr;
        
        if (SUCCEEDED(pThreadMgr->GetFocus(&pDocMgrFocus)) && pDocMgrFocus)
        {
            if (SUCCEEDED(pDocMgrFocus->GetTop(&pContext)) && pContext)
            {
                // Inject space character via shift English input handler
                _KEYSTROKE_STATE keyState;
                keyState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
                keyState.Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
                
                if (_pTextService)
                {
                    // Create an edit session to inject the space character
                    CKeyHandlerEditSession* pEditSession = new (std::nothrow) CKeyHandlerEditSession(_pTextService, pContext, VK_SPACE, L' ', keyState);
                    if (pEditSession)
                    {
                        HRESULT hrSession = S_OK;
                        pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
                        pEditSession->Release();
                    }
                }
                
                pContext->Release();
            }
            pDocMgrFocus->Release();
        }
        
        *pIsEaten = TRUE;
        return;
    }
    // ... handle full/half-width switching ...
}
```

**檔案 2：`src/KeyProcesser.cpp`**

修改 `IsVirtualKeyNeed()` 函數，約 line 624：

```cpp
// Handle Shift+printable ASCII for English input mode-----------------------------------------------------
// Only when not composing (empty buffer)
// Note: Shift+Space is handled by the preserved key system (OnPreservedKey)
if (!(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && IsEscapeInputLeading()) &&
    !fComposing && pwch && *pwch && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_RSHIFT | TF_MOD_SHIFT)) != 0 )
{
    WCHAR c = *pwch;

    // Check for printable ASCII characters (iswprint filters control chars)
    // Exclude valid wildcard chars so they enter composition for wildcard search
    // In Phonetic mode, only ? is a valid wildcard (* is not mapped)
    if (iswprint(c) && c != L' ' &&
        !(IsWildcardChar(c) && !(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && c == L'*')))
    {
        if (pKeyState)
        {
            pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
            pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
        }
        return TRUE;
    }
}
```

**檔案 3：`src/KeyHandler.cpp`**

修改 `_HandleCompositionShiftEnglishInput()` 函數，約 line 522：

```cpp
HRESULT CDIME::_HandleCompositionShiftEnglishInput(TfEditCookie ec, _In_ ITfContext *pContext, UINT code, WCHAR wch)
{
    HRESULT hr = S_OK;

    // Cancel any existing composition first
    if (_IsComposing())
    {
        _HandleCancel(ec, pContext);
    }

    WCHAR outputChar = wch; // Default fallback
    BOOL isCapsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    // For alphabetic characters, simply apply case conversion based on CapsLock
    if (iswalpha(wch))
    {
        outputChar = isCapsLockOn ? towupper(wch) : towlower(wch);
    }
    else if (code >= '0' && code <= '9')
    {
        // Digit keys (1~0): CapsLock OFF → shifted symbol (!@#$%^&*())
        //                   CapsLock ON  → base digit (1234567890)
        // wch is already the shifted char ('!' etc.). Use code directly with
        // ToUnicodeEx to avoid VkKeyScanExW back-computation for the ON case.
        if (!isCapsLockOn)
        {
            outputChar = wch;  // already '!', '@', etc.
        }
        else
        {
            BYTE scanCode = (BYTE)MapVirtualKey(code, MAPVK_VK_TO_VSC);
            BYTE keyState[256] = {0};
            WCHAR result[2] = {0};
            if (ToUnicodeEx(code, scanCode, keyState, result, 2, 0, GetKeyboardLayout(0)) == 1)
                outputChar = result[0];
        }
    }
    else
    {
        // Other symbol keys: CapsLock OFF → base char, CapsLock ON → shifted char
        // Note: Space is unaffected by CapsLock/Shift - ToUnicodeEx always returns space
        UINT vKey = VkKeyScanExW(wch, GetKeyboardLayout(0));

        if (vKey != 0xFFFF)  // Valid key found
        {
            BYTE scanCode = (BYTE)MapVirtualKey(LOBYTE(vKey), MAPVK_VK_TO_VSC);
            BYTE keyState[256] = {0};
            WCHAR result[2] = {0};

            if (!isCapsLockOn)
            {
                // Clear shift to get base character
                keyState[VK_SHIFT] = 0x00;
                keyState[VK_LSHIFT] = 0x00;
                keyState[VK_RSHIFT] = 0x00;
                keyState[VK_CAPITAL] = 0x00;
            }
            else
            {
                // Keep shift pressed to get shifted character
                keyState[VK_SHIFT] = 0x80;
                keyState[VK_CAPITAL] = 0x01;
            }

            if (ToUnicodeEx(LOBYTE(vKey), scanCode, keyState, result, 2, 0, GetKeyboardLayout(0)) == 1)
            {
                outputChar = result[0];
            }
        }
    }

    CStringRange charString;
    charString.Set(&outputChar, 1);

    hr = _AddCharAndFinalize(ec, pContext, &charString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);
    return hr;
}
```

## 4. 規劃中功能：非字根鍵直接繞過

### 4.1 規則

若 Shift+按鍵 的按鍵**不在**當前輸入法模式的字根對應表（Radical Map）中，且**不是**大易地址字元（Address Char），應直接繞過輸入法，將按鍵交回系統處理，不做任何轉換。

- 按鍵**在**字根表中，或為大易地址字元 → 由 `_HandleCompositionShiftEnglishInput()` 處理（依 CapsLock 狀態轉換字元，即本文件第 1-3 節的行為）
- 按鍵**不在**字根表中，且非大易地址字元 → 直接返回 `FALSE`（不吃按鍵），讓系統原樣送出

大易地址字元（`` ` ``、`'`、`[`、`]`、`-`、`\`）定義於 `Global::dayiAddressCharTable`，透過 `IsDayiAddressChar()` 檢查。該函數在非大易模式下直接返回 `FALSE`，因此不影響其他輸入法。

### 4.2 修改位置

**`src/KeyProcesser.cpp`** — `IsVirtualKeyNeed()` 的 Shift+英數處理區塊（約 line 624）

在現有的 `iswprint(c) && c != L' '` 條件中加入字根表檢查，使非字根鍵跳過 `FUNCTION_SHIFT_ENGLISH_INPUT`，讓按鍵落入後續流程或返回系統。

### 4.3 修改程式碼

**`src/KeyProcesser.cpp`** — 將現有 Shift+英數區塊（約 line 624）修改為：

```cpp
// Handle Shift+printable ASCII for English input mode-----------------------------------------------------
// Only when not composing (empty buffer)
// Note: Shift+Space is handled by the preserved key system (OnPreservedKey)
if (!(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && IsEscapeInputLeading()) &&
    !fComposing && pwch && *pwch && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_RSHIFT | TF_MOD_SHIFT)) != 0 )
{
    WCHAR c = *pwch;

    // Check for printable ASCII characters (iswprint filters control chars)
    // Exclude valid wildcard chars so they enter composition for wildcard search
    // In Phonetic mode, only ? is a valid wildcard (* is not mapped)
    if (iswprint(c) && c != L' ' &&
        !(IsWildcardChar(c) && !(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && c == L'*')))
    {
        // Check if key is in the radical map (or Dayi address char table) for the active IME mode
        // If not, bypass to system without processing
        // Use base (unshifted) char from virtual key code, not the shifted char in c
        // e.g. Shift+, sends '<' but ',' is the radical key to check
        WCHAR baseChar = (WCHAR)MapVirtualKey(uCode, MAPVK_VK_TO_CHAR);
        if (Global::imeMode == IME_MODE::IME_MODE_DAYI && IsDayiAddressChar(baseChar))
        {
            // Dayi address char (`, ', [, ], -, \) — skip radical map check
        }
        else
        {
            WCHAR upper = towupper(baseChar);
            if (upper >= 32 && upper < 32 + MAX_RADICAL && (UINT)(upper - 32) < _KeystrokeComposition.Count())
            {
                const _KEYSTROKE& ks = *_KeystrokeComposition.GetAt(upper - 32);
                if (ks.Function == KEYSTROKE_FUNCTION::FUNCTION_NONE)
                    return FALSE;  // Not a radical key — bypass to system
            }
            else
            {
                return FALSE;  // Out of range — bypass to system
            }
        }

        if (pKeyState)
        {
            pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
            pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
        }
        return TRUE;
    }
}
```
