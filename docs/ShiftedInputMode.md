# Shift 鍵英數輸入模式技術文件

## 1. 功能說明

### 1.1 核心行為

**Shift 作為「英數輸入模式切換鍵」**：
- 按住 Shift 時，所有可列印 ASCII 字元（0x20-0x7E）完全繞過輸入法引擎
- 按鍵直接對應到英數字元或符號，不經過中文輸入法處理
- CapsLock 狀態決定輸出的字元形式
- **例外**：當「全形/半形切換」設定為「以 Shift-Space 熱鍵切換」時，Shift+Space 保留給全形/半形切換使用，不會輸入空格

### 1.2 CapsLock 控制輸出字元

- **CapsLock OFF**：Shift + 按鍵 → 輸出基礎字元（小寫字母、數字、基本符號）
  - 例：Shift+A → `a`、Shift+1 → `1`、Shift+- → `-`
  
- **CapsLock ON**：Shift + 按鍵 → 輸出 Shift 字元（大寫字母、特殊符號）
  - 例：Shift+A → `A`、Shift+1 → `!`、Shift+2 → `@`、Shift+8 → `*`

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
  - CapsLock OFF：按住 Shift 輸入 "user"
  - 切換 CapsLock ON：按住 Shift 輸入 "2" → 產生 "@"
  - 切換 CapsLock OFF：按住 Shift 輸入 "example.com"
```

### 2.3 含符號的密碼

```
輸入目標：Pass123!@#
操作方式：
  - CapsLock ON：按住 Shift 輸入 "P"
  - CapsLock OFF：按住 Shift 輸入 "ass123"
  - CapsLock ON：按住 Shift 輸入 "1" → "!"、"2" → "@"、"3" → "#"
```

### 2.4 程式碼片段

```
輸入目標：int* ptr = nullptr;
操作方式：
- CapsLock OFF：按住 Shift 輸入 "int"
- 切換 CapsLock ON：按住 Shift 輸入 "8" → 產生 "*"
- 切換 CapsLock OFF：按住 Shift 輸入 " ptr = nullptr;"
說明：Shift+Space 產生空格，整個過程保持按住 Shift 鍵連續輸入
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

修改 `OnPreservedKey()` 函數，約 line 553：

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

修改 `IsVirtualKeyNeed()` 函數，約 line 587：

```cpp
// Handle Shift+printable ASCII for English input mode
// Note: Shift+Space is handled by the preserved key system (OnPreservedKey)
if (pwch && *pwch && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_RSHIFT | TF_MOD_SHIFT)) != 0)
{
    WCHAR c = *pwch;
    
    // Check for printable ASCII characters (iswprint filters control chars)
    if (iswprint(c) && c != L' ')  // Exclude space as it's handled by preserved key
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

修改 `_HandleCompositionShiftEnglishInput()` 函數，約 line 530：

```cpp
HRESULT CDIME::_HandleCompositionShiftEnglishInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
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
    else
    {
        // For non-alphabetic characters (numbers, symbols), use keyboard state
        UINT vKey = VkKeyScanExW(wch, GetKeyboardLayout(0));
        
        if (vKey != 0xFFFF)  // Valid key found
        {
            BYTE scanCode = (BYTE)MapVirtualKey(LOBYTE(vKey), MAPVK_VK_TO_VSC);
            BYTE keyState[256] = {0};
            WCHAR result[2] = {0};
            
            // Set up keyboard state based on CapsLock
            // CapsLock OFF: clear shift (get base character - numbers, lowercase, base symbols)
            // CapsLock ON: keep shift (get shifted character - special chars, uppercase)
            if (!isCapsLockOn)
            {
                // Clear shift to get base character
                keyState[VK_SHIFT] = 0x00;
                keyState[VK_LSHIFT] = 0x00;
                keyState[VK_RSHIFT] = 0x00;
                keyState[VK_CAPITAL] = 0x00; // CapsLock OFF
            }
            else
            {
                // Keep shift pressed to get shifted character
                keyState[VK_SHIFT] = 0x80;
                keyState[VK_CAPITAL] = 0x01; // CapsLock ON
            }
            
            // Get the character with the modified keyboard state
            // This automatically handles all keyboard layouts
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
