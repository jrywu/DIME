# 第 4 階段：把 TSF 外殼縮成薄 C++ 層（設計提案）

狀態：已決定（2026-11-10，見第 6 節），進行中。屬於 `RUST_MIGRATION_PLAN.md` 第 4 階段的必要部分；外殼本身改寫成 Rust（COM `#[implement]`、`cdylib`）仍是選做，不在本文件範圍。

## 1. 現況

第 2、3 階段完成後，Rust 建置的 `DIME.dll` 中：

- **已在 Rust**：設定、字典引擎、組字緩衝、候選字產生與選擇、整句、按鍵是否吃掉（`dime_decide_key_eaten`）、全部 handler 流程（`dime-core::session`，經 `DimeHost` vtable 的 63 個 callback 操作 TSF 與 UI）、所有 UI 視窗（`dime-ui`）。
- **仍在 C++、且 Rust 建置下仍會執行的邏輯**（不是 TSF 介面本身）：

| 區塊 | 檔案（行數含 `.h`） | 內容 | 和 TSF 的關係 |
|---|---|---|---|
| 按鍵路由 | `KeyStateCategory` (844)、`KeyHandlerEditSession` (128)、`CDIME::_HandleXxx` 包裝（`KeyHandler`／`CandidateHandler`／`SentenceHandler` 的 Rust 分派部分） | edit session 內依「按鍵類別 × 狀態」選 handler，再轉呼 `dime_session_handle_*`；純轉手 | 只需要 edit cookie 與 context |
| 呈現器 | `UIPresenter` (2,094) | 候選字窗／提示窗的生命週期、位置（`_LayoutChangeNotification`、`ShowNotifyText` 的游標來源與 Firefox 等後備、提示窗堆疊策略 #127）、顏色套用、候選清單與選取／翻頁的記帳、`_CandidateChangeNotification`／`_NotifyChangeNotification` | 混合：`ITfCandidateListUIElement`、`BeginUIElement`、text layout sink、取 context view 的 HWND 是 TSF；其餘是邏輯與 Win32 查詢 |
| 引擎設定 | `SetupCompositionProcesseorEngine` (1,711)、`CompositionProcessorEngine` 的非分派部分 | 依模式載入／重新載入碼表（自訂碼表優先、Big5 過濾、檔案時間檢查）、按鍵表、繁簡轉換表、字頻表、候選字範圍 | preserved key 註冊是 TSF，其餘是邏輯與檔案 |
| 語言列與 compartment | `LanguageBar` (1,279) 的 `CDIME::On*`／`show*Notify`／`*CompartmentUpdated` | 中英／全半形／繁簡切換時更新設定、compartment 與提示 | 按鈕（`CLangBarItemButton`）與 compartment 讀寫是 COM；切換決策是邏輯 |
| 其他 | `DIME.cpp` 的 `_LoadConfig`／`DoBeep` 等、`Globals` (966)、`BaseStructure` | 設定載入時機、提示音、全域表格 | 少量 |

- **C++ 原版**：依原則 2，每個已移植的部分都還保留 C++ 實作（`_XxxCpp`、`DimeUseRustEngine=false`），第 6 階段才刪除。所以 Rust 建置的 `DIME.dll` 目前仍編譯約 34k 行 C++，其中相當部分是 `#ifdef` 排除或未呼叫的原版。

### 1.1 「≤ 5k 行」目標的現實下限

把上表的邏輯全部移走之後，剩下的純 COM／TSF 介面檔（`Register`、`Server`、`DllMain`、各 sink、`EditSession` 系列、`Composition`／`Start`／`EndComposition`、`Compartment`、`DisplayAttribute*`、`Tip*`、`EnumTfCandidates`、`ReverseConversion*`、`SearchCandidateProvider`、`TfTextLayoutSink`、`UIACaretTracker`、`TfInputProcessorProfile`）目前合計約 **6.4k 行**；再加上 `CDIME` 類別本體、`UIPresenter` 的 `ITfCandidateListUIElement`、語言列按鈕的 COM，以及 Rust 轉接檔（`RustSessionHost` 等約 1.8k），**下限約 9～10k 行**。計畫第 0 節原本的「≤ 5k」只有在把部分 COM 也搬到 Rust（選做）時才達得到；已改成實際下限（第 6 節決定 1）。

## 2. 目標

- Rust 建置下，C++ 只做三件事：實作 TSF／COM 介面、在 edit session 內執行 Rust 要求的 TSF 操作（`DimeHost` callback）、把 TSF 事件轉成一個 FFI 呼叫交給 Rust。
- 所有「決定要做什麼」的程式碼在 Rust：按鍵路由、呈現器的狀態與位置、引擎設定、語言列切換。
- 行為不變（黃金打字情境、對拍、整合測試）；C++ 建置照舊可建、可測，直到第 6 階段。

## 3. 建議做法

### 3.1 沿用的模式

- **Rust 主導、C++ 當 host**（2c 的決定）：新的 Rust 模組擁有狀態，需要 TSF 時經 host callback 回到 C++；不改成「Rust 回傳動作清單、C++ 套用」。callback 一律不丟例外、回呼可能重入或刪掉物件時要放在最後一步（現有規則）。
- **Win32 查詢直接在 Rust**：`GetGUIThreadInfo`、`GetCaretPos`、`ClientToScreen`、`GetForegroundWindow`、螢幕工作區等不經 C++（`dime-ui` 已依賴 `windows` crate）。只有 TSF 物件（`ITfContext`、`ITfContextView`、thread manager、edit cookie）留在 C++。
- **兩種建置並存**：Rust 建置在呼叫點依 `DIME_USE_RUST_ENGINE` 分派，C++ 原版保留給 C++ 建置；3c-5f 的做法（`DIME.vcxproj` 依 `DimeRustEnabled` 排除整個檔案）在一個區塊完全移走後套用。
- **驗證**：KeyFlow 黃金情境（真實 TSF + 未修改的 `CDIME`）是主力；邏輯可抽成純函式的部分另做 C++ ↔ Rust 對拍；每個轉接點植入錯誤確認走到 Rust。

### 3.2 按鍵路由

`CKeyHandlerEditSession::DoEditSession` 在 Rust 建置下改成單一呼叫 `dime_session_handle_key(host, category, function, code, wch)`；`KeyStateCategory` 的「類別 × 狀態 → handler」表（含 `CKeyStateSentence` 的整句攔截）搬到 `dime-core::session`。`CDIME::_HandleXxx` 的 Rust 分派包裝隨之不再需要（C++ 建置照舊）。`KeyStateCategory.cpp` 之後只編進 C++ 建置。

### 3.3 呈現器（`UIPresenter`）

- 新的 `dime-ui::presenter` 擁有 `UIPresenter` 的非 TSF 狀態：`_candLocation`、`_notifyLocation`、`_rectCompRange`、提示窗各 slot、候選字窗、顏色，並直接持有 Rust 視窗（`CRustCandidateWindow`／`CRustNotifyWindow` 這兩個薄型別隨之消失）。
- 搬過去的邏輯：`_LayoutChangeNotification`、`ShowNotifyText`（游標來源、後備、`_RestackVisibleSlots`）、`ClearNotify`／`DisposeNotifyWindow`、`_StartCandidateList`／`_EndCandidateList`／`_SetCandidateText` 與選取／翻頁、顏色套用、兩個視窗回呼的決策。
- 留在 C++ 的 `CUIPresenter`：`ITfUIElement`／`ITfCandidateListUIElement(Behavior)` 介面（`GetCount`、`GetString` 等改成讀 Rust 的清單）、`BeginUIElement`／`EndUIElement`／`UpdateUIElement`、`CTfTextLayoutSink`（把版面變化轉給 Rust）、需要 TSF 的 host callback（取 context view 的 HWND、`_ProbeComposition`、`OnPreservedKey`）。
- `DimeHost` 中的 UI callback（`create_and_start_candidate`、`set_candidate_text`、`show_notify`…）改成 Rust 內部呼叫，不再經 C++。

### 3.4 引擎設定

`SetupDictionaryFile` 的載入／重新載入策略、按鍵表、繁簡與字頻表、候選字範圍搬到 Rust（碼表緩衝區與 `CMemoryFile` 已有 Rust 對應）。preserved key 的 TSF 註冊留在 C++，按鍵清單由 Rust 提供。

### 3.5 語言列與 compartment

切換決策（開／關、全／半形、繁簡、`show*Notify`、compartment 變化時的設定同步）搬到 Rust；`CLangBarItemButton` 與 compartment 的讀寫留在 C++，以 host callback 提供。

## 4. 分步

| 步驟 | 內容 | 風險 |
|---|---|---|
| 4a | 按鍵路由進 Rust（3.2）。驗證：KeyFlow 全部情境（每個按鍵都經過路由；`KeyStateCategory` 沒有專屬單元測試），另以植入錯誤確認每個類別都走到 Rust | 低 |
| 4b | 呈現器。分三步： | 高：和 TSF、游標追蹤、多提示窗互動最多 |
| 4b-1 | `dime-ui::presenter` 的位置與提示窗策略（`_LayoutChangeNotification`、`ShowNotifyText`、堆疊）：C++ 收集事實、Rust 決定位置的純函式先上，對拍 `UIPresenter` 原版 | 中 |
| 4b-2 | 呈現器擁有狀態與 Rust 視窗；`CUIPresenter` 只剩 TSF 介面與 host；`DimeHost` 的 UI callback 改成 Rust 內部呼叫 | 高 |
| 4b-3 | 移除 `CRustCandidateWindow`／`CRustNotifyWindow`；`UIPresenter` 的邏輯部分只編進 C++ 建置 | 低 |
| 4c | 引擎設定（3.4） | 中：碼表重新載入與自訂碼表優先順序 |
| 4d | 語言列與 compartment（3.5） | 中：compartment 事件的重入 |
| 4e | 清理：`Globals`／`BaseStructure` 中 Rust 建置已無人使用的部分改成只編進 C++ 建置或刪除；`DIME.vcxproj` 依 `DimeRustEnabled` 排除已完全移走的檔案；統計剩下的 C++ 行數 | 低 |

每一步都有自己的對拍與完整矩陣，個別提交。

## 5. 測試的缺口

- KeyFlow 不看視窗（見記憶「KeyFlow does not see windows」）：4b 需要 `UIPresenter` 層級的真實視窗測試（沿用 `IT06_04_CandidateWindowIsReal`／`NotifyWindowIsReal` 的做法），加上位置對拍。
- 游標來源的應用程式差異（Firefox 的 `rcCaret` 全 0、浮動組字窗的舊程式、Store app）無法在單元測試重現，4b 結束時需要手動清單：記事本、Word、Edge／Chrome、Firefox、Store app、命令提示字元、多螢幕／混合 DPI（`TEST_PLAN.md`、`CARET_TRACKING*.md`）。
- 4d 的 compartment 事件：`LanguageBarIntegrationTest` 只測按鈕本身，需要補一組透過真實 thread manager 切換中英／全半形並檢查設定、compartment 與提示的測試（目前只有 KeyFlow 間接涵蓋）。

## 6. 決定（2026-11-10，使用者）

1. **C++ 行數目標**：改成實際下限——Rust 建置下 C++ 只剩 TSF／COM 介面與轉接，約 9～10k 行；COM 介面維持 C++（外殼改寫仍是選做）。
2. **順序**：4a → 4b → 4c → 4d → 4e。
3. **呈現器**：Rust 擁有狀態（3.3）：`dime-ui::presenter` 擁有位置、提示窗 slot、候選清單記帳與 Rust 視窗並直接呼叫 Win32；C++ `CUIPresenter` 只留 TSF 介面與 host callback。
4. **舊版設定對話框**：4e 從 `DIME.dll` 移除（`ShowConfig.cpp` 找不到 `DIMESettings.exe` 時改為顯示錯誤訊息）；`DIMESettings.exe` 共用的 `ConfigDialog.cpp` 程式碼留到第 5b 階段。
