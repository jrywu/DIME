# Shift 鍵英數輸入模式技術文件

## 1. 概述

Shift 英數輸入模式允許使用者在中文輸入模式下，按住 Shift 鍵直接輸入英數字元，無需切換至英文模式。此模式**僅在組字緩衝區為空時啟動**。

## 2. 啟動與退出條件

| 條件 | 結果 |
|------|------|
| 組字緩衝區為空 + Shift 按住 + 可列印字元 | 進入 Shift 英數模式 |
| 組字緩衝區為空 + Shift 按住 + 萬用字元（`*`、`?`） | 萬用字元進入組字（非 Shift 英數） |
| 組字緩衝區非空（含萬用字元組字） | Shift 英數不啟動 |
| 按鍵不在字根表且非大易地址字元 | 繞過輸入法，交系統處理 |

**例外**：注音輸入法僅 `?` 為萬用字元，`*` 不映射字根，Shift+8 在注音下作為 Shift 英數輸出。

## 3. CapsLock 輸出規則

| 按鍵類型 | CapsLock OFF | CapsLock ON |
|---------|--------------|-------------|
| 字母鍵 | 小寫 | 大寫 |
| 數字鍵（`1`~`0`） | Shift 符號（`!@#$%^&*()`） | 基礎數字 |
| 其他符號鍵 | 基礎符號 | Shift 符號 |

## 4. 萬用字元交互規則

### 4.1 Shift 英數模式中遇到萬用字元

已在 Shift 英數模式（`_isShiftedEnglish == TRUE`）時，萬用字元 `*`、`?` 仍作為 Shift 英數輸出，不進入組字。此判斷在 `_IsKeyEaten()` 中於 `IsVirtualKeyNeed()` 之前執行。

### 4.2 萬用字元啟動組字後的 Shift 按鍵處理

當萬用字元在組字緩衝區為空時進入組字（首字元），後續 Shift 按鍵遵循以下規則（透過 `_pendingWildcardInput` 旗標控制）：

| 後續按鍵 | 行為 |
|---------|------|
| 萬用字元（`*`、`?`，注音僅 `?`） | 進入組字緩衝區 |
| 所有其他按鍵（含字根鍵） | 吃掉並忽略（不傳給應用程式） |

Shift 按住期間所有非萬用字元按鍵皆被阻擋——即使底層按鍵為字根鍵（如注音的 1234567890-），Shift 狀態下的字元（!@#$%^&*()_）並非字根，不應進入組字。使用者必須**放開 Shift** 才能回到一般組字輸入或重新啟動 Shift 英數模式。

### 4.3 Shift+Backspace 保持 Shift 英數狀態

在 Shift 英數模式中按 Shift+Backspace 不會中斷模式——`_isShiftedEnglish` 旗標在 Shift+VK_BACK 時保持不變。放開 Shift 時（`OnKeyUp` 收到 `VK_SHIFT`）會正確重設旗標。

## 5. 狀態追蹤旗標

### 5.1 `_isShiftedEnglish`（`CDIME` 成員）

追蹤上一個按鍵是否為 Shift 英數輸出。用於判斷後續萬用字元應作為 Shift 英數還是組字。

**更新邏輯**（`KeyEventSink.cpp`）：

| 位置 | 條件 | 結果 |
| ------ | ------ | ------ |
| `OnKeyDown()` 結尾 | `FUNCTION_SHIFT_ENGLISH_INPUT` | 設 `TRUE` |
| `OnKeyDown()` 結尾 | `code == VK_BACK` 且 Shift 按住（Shift+Backspace） | 保持不變 |
| `OnKeyDown()` 結尾 | 其他情況（含 VK_SHIFT 重新按下） | 設 `FALSE` |
| `OnTestKeyDown()` | `wParam == VK_SHIFT` 且 `lParam` bit 30 = 0（新按下，非重複） | 設 `FALSE` |
| `OnKeyUp()` | `wParam == VK_SHIFT` | 設 `FALSE`（防禦性備援） |

`OnTestKeyDown` 的新按下偵測是 Windows 10 的主要清除路徑（見第 9 節）。

### 5.2 `_pendingWildcardInput`（`CDIME` 成員）

追蹤萬用字元是否以 Shift 按住狀態啟動組字。解決因 `TF_ES_ASYNCDONTCARE` 造成 `_IsComposing()` 延遲更新的問題，同時確保 Shift 按住期間後續按鍵不觸發 Shift 英數模式。

**設定**（`KeyEventSink.cpp`，`OnKeyDown()` 結尾）：萬用字元 `FUNCTION_INPUT` 且 Shift 按住且非 Shift 英數模式時設 `TRUE`。

**清除**：`OnTestKeyDown()` 偵測到 VK_SHIFT 新按下（lParam bit 30 = 0）時設 `FALSE`（Windows 10 主要路徑）；`OnKeyDown()` 的 Shift 未按住分支亦會清除；`OnKeyUp(VK_SHIFT)` 為防禦性備援。Shift 持續按住期間保持不變（包括非字根鍵被忽略時）。

**作用**（`KeyEventSink.cpp`，`_IsKeyEaten()` 中）：

1. 檢查按鍵是否為有效萬用字元（`IsWildcardChar`，注音排除 `*`）
2. 若為萬用字元 → 放行，交由 `IsVirtualKeyNeed()` 正常處理進入組字
3. 若非萬用字元 → 吃掉並忽略（不傳給應用程式，不進入 Shift 英數或組字）

## 6. Shift+Space 處理

Shift+Space 註冊為 TSF Preserved Key，由 `OnPreservedKey()` 處理（`SetupCompositionProcesseorEngine.cpp`）：

- 「全形/半形切換」設為「以 Shift-Space 熱鍵切換」→ 執行全形/半形切換
- 設為「半型」或「全型」→ 注入空格字元（透過 `FUNCTION_SHIFT_ENGLISH_INPUT` 編輯工作階段）

## 7. 非字根鍵繞過規則

Shift+按鍵的按鍵若不在當前輸入法的字根表中且非大易地址字元，直接繞過輸入法交系統處理。

- 字根表檢查使用 `MapVirtualKey(uCode, MAPVK_VK_TO_CHAR)` 取得基礎字元，再查 `_KeystrokeComposition` 陣列
- 大易地址字元（`` ` ``、`'`、`[`、`]`、`-`、`\`）透過 `IsDayiAddressChar()` 檢查，非大易模式返回 `FALSE`
- 非字根鍵返回 `FALSE`（不吃按鍵），讓系統原樣送出

## 8. 相關原始碼

| 檔案 | 函數 | 職責 |
|------|------|------|
| `src/KeyEventSink.cpp` | `_IsKeyEaten()` | Shift 英數萬用字元檢查、`_pendingWildcardInput` 覆寫 `isComposing`、非字根鍵攔截 |
| `src/KeyEventSink.cpp` | `OnTestKeyDown()` | 偵測 VK_SHIFT 新按下（lParam bit 30 = 0）並清除旗標（Windows 10 主要路徑） |
| `src/KeyEventSink.cpp` | `OnKeyDown()` | 更新 `_isShiftedEnglish` 和 `_pendingWildcardInput` 旗標 |
| `src/KeyEventSink.cpp` | `OnKeyUp()` | 清除旗標（VK_SHIFT 放開時，防禦性備援） |
| `src/KeyProcesser.cpp` | `IsVirtualKeyNeed()` | Shift 英數偵測（`!fComposing` + Shift + 可列印字元）、字根表與萬用字元過濾 |
| `src/KeyHandler.cpp` | `_HandleCompositionShiftEnglishInput()` | CapsLock 字元轉換、取消既有組字、輸出字元 |
| `src/SetupCompositionProcesseorEngine.cpp` | `OnPreservedKey()` | Shift+Space 處理（全形/半形切換或注入空格） |
| `src/DIME.h` / `src/DIME.cpp` | `CDIME` 類別 | `_isShiftedEnglish`、`_pendingWildcardInput` 成員宣告與初始化 |

## 9. Windows 10 相容性：`_isShiftedEnglish` 殘留問題

**現象**：Shift 英數模式輸入後，放開 Shift 再重新按 Shift+萬用字元，萬用字元仍被當作 Shift 英數輸出，不進入組字。

**根本原因**：Windows 10 的 TSF 對 IME 未吃掉（`pIsEaten=FALSE`）的按鍵，**同時跳過 `OnKeyDown` 和 `OnKeyUp`**，僅呼叫 `OnTestKeyDown`。DIME 不攔截裸 `VK_SHIFT`，因此兩者皆不觸發，`_isShiftedEnglish` 永遠無法透過現有的 `OnKeyDown`/`OnKeyUp` 重設路徑清除，導致舊值殘留。Windows 11 則無此問題（`OnKeyDown(VK_SHIFT)` 始終被呼叫）。

**修正**：在 `OnTestKeyDown` 中偵測 **VK_SHIFT 新按下**（`lParam` bit 30 = 0，表示按鍵先前為放開狀態），即清除兩個旗標：

```cpp
if (wParam == VK_SHIFT && !(lParam & 0x40000000))
{
    _isShiftedEnglish = FALSE;
    _pendingWildcardInput = FALSE;
}
```

lParam bit 30 = 0 代表使用者確實放開並重新按下 Shift（而非持續按住的自動重複），因此此條件精確對應「Shift 手勢已結束並重新開始」的語意。`OnKeyUp(VK_SHIFT)` 保留相同清除邏輯作為防禦性備援。
