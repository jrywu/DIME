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

在 Shift 英數模式中按 Shift+Backspace 不會中斷模式——`_isShiftedEnglish` 旗標在 Shift+VK_BACK 時保持不變。放開 Shift 或重新按下 Shift（VK_SHIFT key-down 事件）會正確重設旗標。

## 5. 狀態追蹤旗標

### 5.1 `_isShiftedEnglish`（`CDIME` 成員）

追蹤上一個按鍵是否為 Shift 英數輸出。用於判斷後續萬用字元應作為 Shift 英數還是組字。

**更新邏輯**（`KeyEventSink.cpp`，`OnKeyDown()` 結尾）：

- `FUNCTION_SHIFT_ENGLISH_INPUT` → 設 `TRUE`
- Shift+Backspace（`code == VK_BACK` 且 Shift 按住）→ 保持不變
- 其他情況（含 VK_SHIFT 重新按下）→ 設 `FALSE`

### 5.2 `_pendingWildcardInput`（`CDIME` 成員）

追蹤萬用字元是否以 Shift 按住狀態啟動組字。解決因 `TF_ES_ASYNCDONTCARE` 造成 `_IsComposing()` 延遲更新的問題，同時確保 Shift 按住期間後續按鍵不觸發 Shift 英數模式。

**設定**（`KeyEventSink.cpp`，`OnKeyDown()` 結尾）：萬用字元 `FUNCTION_INPUT` 且 Shift 按住且非 Shift 英數模式時設 `TRUE`。

**清除**：Shift 放開時設 `FALSE`。Shift 持續按住期間保持不變（包括非字根鍵被忽略時）。

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
| `src/KeyEventSink.cpp` | `OnKeyDown()` | 更新 `_isShiftedEnglish` 和 `_pendingWildcardInput` 旗標 |
| `src/KeyProcesser.cpp` | `IsVirtualKeyNeed()` | Shift 英數偵測（`!fComposing` + Shift + 可列印字元）、字根表與萬用字元過濾 |
| `src/KeyHandler.cpp` | `_HandleCompositionShiftEnglishInput()` | CapsLock 字元轉換、取消既有組字、輸出字元 |
| `src/SetupCompositionProcesseorEngine.cpp` | `OnPreservedKey()` | Shift+Space 處理（全形/半形切換或注入空格） |
| `src/DIME.h` / `src/DIME.cpp` | `CDIME` 類別 | `_isShiftedEnglish`、`_pendingWildcardInput` 成員宣告與初始化 |
