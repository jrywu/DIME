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
| ✅ 4a | 按鍵路由進 Rust：`session::handle_key`（`keyclass` 補齊 `PHRASE`／`PHRASE_FROM_KEYSTROKE`／`SENTENCE` 類別與 `FINALIZE_TEXTSTORE_AND_INPUT`／`FINALIZE_CANDIDATELIST`／`DOUBLE_SINGLE_BYTE` 功能）逐條對應 `CKeyStateComposing`／`Candidate`／`Phrase`／`Sentence`／`Null`：兩步驟的組合依序呼叫兩個 handler、回傳第二個的結果；`CKeyStateNull` 與未覆寫的功能回傳 `E_NOTIMPL`，`KeyStateHandler` 不路由的功能回傳 `E_INVALIDARG`。Rust 建置的 `CKeyHandlerEditSession::DoEditSession` 只建立一個 `CRustSessionHost` 並呼叫 `dime_session_handle_key`（原本兩步驟各建一個 host；host 的暫存只在單次 callback 內有效，共用不影響行為），`KeyStateCategory.cpp` 只編進 C++ 建置。Rust 單元測試：不路由的組合不碰 host、13 種組合與直接呼叫 handler 的呼叫紀錄相同。植入錯誤：Candidate 的「選字並輸入」少了輸入（KeyFlow 3 個情境抓到）、Composing 的 Backspace 改成取消（6 個情境抓到）；Composing 的方向鍵改走候選字窗版本時 KeyFlow 看不出差別（兩者只差一次設回原選取範圍的 `SetSelection`），改由 Rust 單元測試把關（`Mock` 記錄 `composition_arrow_key`）。`CDIME::_HandleXxx` 中只被路由表呼叫的 13 個 Rust 分派包裝在 Rust 建置下已無呼叫者，留到 4e 清理 | 低 |
| 4b | 呈現器。分三步： | 高：和 TSF、游標追蹤、多提示窗互動最多 |
| ✅ 4b-1 | `dime-ui::presenter`：`layout_change`（`_LayoutChangeNotification` 讀完事實之後的部分）、`notify_location_for_show`（`ShowNotifyText` 的游標來源與後備，純函式）、`restack_for_show`（顯示後從同一錨點重排）。C++ 讀 TSF／Win32 事實（`PresenterLayoutFacts`／`PresenterShowFacts`：組字範圍、有無父視窗、游標、`rcCaret`）後呼叫 `_LayoutChangeApply`／`_NotifyLocationForShow`／`_RestackForShow`，依 `_useRustLayout` 分派到 Rust 或 C++ 原版（`_XxxCpp`，改成吃同一組事實）；位置狀態每次傳入取回（`DimePresenterState`），視窗經 `DimePresenterHostVtbl`（`CUIPresenterLayoutHost`）操作；`_DecideStackGrowUp`／`_RestackVisibleSlots` 在 Rust 端直接用 `layout::stack_grow_up`／`layout::restack`。順帶：`ShowNotifyText` 的 `GUITHREADINFO` 與 `POINT` 原本每次 `new` 而不釋放，改用堆疊變數；`GetGUIThreadInfo` 失敗時原本讀到未初始化的 `rcCaret`，改為歸零（視為沒有游標位置）；`new` 失敗才會走的 `GetCaretPos` 分支（會丟例外的 `new` 不會回傳 null）刪除。`PresenterLayoutParityTest`：兩個 presenter 各有相同的真實視窗（候選字窗無／隱藏／顯示，四個提示窗 slot 各自無／隱藏／顯示），一個用 C++、一個用 Rust，40 組 × 25 步隨機事實（反向、空、1 像素與一般的組字範圍，第一次呼叫，有無父視窗，游標在內或外；暫時或一般 slot、空或真的 `rcCaret`；未初始化的位置），比對位置狀態、回傳值與每個視窗的矩形與可見：1,000 步相同；另確認正式預設（Rust 建置用 Rust）。植入錯誤（候選字窗可見時錨點少減 slot 高度、翻轉後重排不對齊 x、後備位置少加 `SHADOW_SPREAD / 2`）都被抓到。過程中發現：測試起初把 `MakeCandidateWindow` 的寬度當成像素（120～400），但它是字元數（正式程式傳 `GetCandidateWindowWidth()`，預設 3 + 1），候選字窗因此寬達 2,600～6,300 像素；Rust 視窗照常運作，C++ 建置的 `CCandidateWindow`／`CShadowWindow` 在這種寬度下經過幾次移動後讓測試行程當掉或卡住（`UILayout` 的位置計算沒有無界迴圈，問題在 C++ 視窗本身；Rust 建置已不編譯它們，未再追查）。測試改用 3～8 個字元 | 中 |
| 4b-2 | 呈現器擁有狀態與 Rust 視窗；`CUIPresenter` 只剩 TSF 介面與 host；`DimeHost` 的 UI callback 改成 Rust 內部呼叫。分三小步： | 高 |
| ✅ 4b-2a | `dime-ui::presenterwin::Presenter`（`DimePresenter`，`CUIPresenter::_rp`）擁有位置狀態（`_candLocation`／`_notifyLocation`／`_rectCompRange`）與提示窗堆疊（每個 `NOTIFY_TYPE` slot 一個 Rust `NotifyWindow`；slot 物件存在但視窗建立失敗時仍算存在，和 C++ 相同），4b-1 的決策改在它身上執行；候選字窗仍由 C++ 持有，經 `DimePresenterCandVtbl`（`CUIPresenterCandHost`）操作；4b-1 的 `DimePresenterHostVtbl` 與三個無狀態 FFI 移除。C++ 保留需要 TSF 的部分（從 context view 取父視窗、`_ProbeComposition`）與提示窗回呼（`_NotifyChangeNotification`，經 `_NotifyWndInvoke`）。`MakeNotifyWindow`／`SetNotifyText`／`ShowNotify`／`DisposeNotifyWindow`／`ClearNotify`／`IsNotifyShown`／`_AnyHintSlotVisible`／`_SetNotifyTextColor`／`ToShowUIWindows`／`ResetCompRange`／`GetCandLocation`／`GetCompRangeHeight`／`_RestackVisibleSlots` 依 `_useRustPresenter`（原 `_useRustLayout`）分派，C++ 原版保留到 4b-3。回呼可能重入呈現器（顯示、銷毀），所以狀態放 `Cell`、視窗先從 slot 表複製 `Rc` 再呼叫，不跨呼叫持有借用。`PresenterLayoutParityTest` 擴充成 60 組 × 40 步，加入建立、改字、顯示、隱藏、銷毀、清除、顏色、`ToShowUIWindows`、`ResetCompRange`、重排，並比對查詢結果（是否顯示、提示是否可見、哪些 slot 存在、候選字位置、組字高度）與每個 slot 以自身繪圖程序畫進 DIB 的像素雜湊（2,400 步）。植入錯誤：提示可見只看反查（抓到）、全部銷毀漏掉 slot 0（抓到）、再次建立時改掉文字與設定顏色時漏掉背景——這兩個一開始沒抓到（只比矩形與標題大小時看不到文字與顏色，測試中的視窗沒有收到 `WM_PAINT`），加上像素雜湊後都抓到。全 C++ 測試組合（`DimeTestRustEngine=false`）起初當掉：該組合的 `DIME.NotifyWindow` 類別是 `CBaseWindow` 的視窗程序註冊的，對拍時切到 Rust 呈現器就用它建立 Rust 視窗；改由 `_SetNotifyClassesForTest` 給 Rust 測試類別（正式程式只在 Rust 建置使用 Rust 呈現器）。這個組合下對拍也順帶逐像素比對 C++ `CNotifyWindow` 與 Rust 提示窗的繪圖 | 中 |
| 4b-2b | 候選字窗與候選清單記帳交給呈現器（`_StartCandidateList`／`_EndCandidateList`／`_SetCandidateText`、選取與翻頁、顏色）；`ITfCandidateListUIElement` 讀 Rust 的清單 | 高 |
| 4b-2c | `DimeHost` 的 UI callback（`create_and_start_candidate`、`set_candidate_text`、`show_notify`…）改成 Rust 內部呼叫 | 中 |
| 4b-3 | 移除 `CRustCandidateWindow`／`CRustNotifyWindow`；`UIPresenter` 的邏輯部分只編進 C++ 建置 | 低 |
| 4c | 引擎設定（3.4） | 中：碼表重新載入與自訂碼表優先順序 |
| 4d | 語言列與 compartment（3.5） | 中：compartment 事件的重入 |
| 4e | 清理：`Globals`／`BaseStructure` 中 Rust 建置已無人使用的部分改成只編進 C++ 建置或刪除；4a 之後只剩路由表呼叫的 `CDIME::_HandleXxx` 包裝在 Rust 建置下移除；`DIME.vcxproj` 依 `DimeRustEnabled` 排除已完全移走的檔案；統計剩下的 C++ 行數 | 低 |

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
