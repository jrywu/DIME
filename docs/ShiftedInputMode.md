# Shift 鍵英數輸入模式技術文件

## 1. 功能說明

### 1.1 核心行為

**Shift 作為「英數輸入模式切換鍵」**：
- 按住 Shift 時，所有可列印 ASCII 字元（0x20-0x7E）完全繞過輸入法引擎
- 按鍵直接對應到英數字元或符號，不經過中文輸入法處理
- CapsLock 狀態決定輸出的字元形式

### 1.2 CapsLock 控制輸出字元

- **CapsLock OFF**：Shift + 按鍵 → 輸出基礎字元（小寫字母、數字、基本符號）
  - 例：Shift+A → `a`、Shift+1 → `1`、Shift+- → `-`
  
- **CapsLock ON**：Shift + 按鍵 → 輸出 Shift 字元（大寫字母、特殊符號）
  - 例：Shift+A → `A`、Shift+1 → `!`、Shift+2 → `@`、Shift+8 → `*`

## 2. 使用範例

### 2.1 基本中英混合輸入

```
輸入目標：請使用 git commit 命令
操作方式：輸入「請使用」→ 按住 Shift 輸入 "git" → 放開 Shift 輸入空格 → 按住 Shift 輸入 "commit" → 放開 Shift → 輸入「命令」
```

### 2.2 電子郵件地址

```
輸入目標：user@example.com
操作方式：
  - CapsLock OFF：按住 Shift 輸入 "user"
  - 切換 CapsLock ON：按住 Shift+2 輸入 "@"
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
操作方式：CapsLock OFF 輸入 "int" → CapsLock ON 輸入 "*" → CapsLock OFF 輸入 " ptr = nullptr;"
```

## 3. 實作說明

### 3.1 修改檔案

**檔案 1：`src/KeyProcesser.cpp`**

修改 `IsVirtualKeyNeed()` 函數，約 line 587：

```cpp
BOOL CKeyProcesser::IsVirtualKeyNeed(UINT uCode, _In_reads_(3) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        *pKeyState = KEYSTROKE_STATE::KEYSTROKE_NONE;
    }

    // Check if Shift key is pressed alone
    if (Global::IsShiftKeyDownOnly())
    {
        WCHAR c = pwch[0];
        
        // Check for printable ASCII characters (iswprint filters control chars)
        if (iswprint(c))
        {
            if (pKeyState)
            {
                *pKeyState = KEYSTROKE_STATE::KEYSTROKE_SHIFT;
            }
            return TRUE;
        }
    }

    // ... rest of original logic ...
}
```

**檔案 2：`src/KeyHandler.cpp`**

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
