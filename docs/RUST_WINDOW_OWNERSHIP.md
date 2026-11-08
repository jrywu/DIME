# 3c-5：由 Rust 擁有 UI 視窗（設計提案）

狀態：已決定（2026-11-08，見第 5 節），進行中。屬於 `RUST_MIGRATION_PLAN.md` 第 3 階段。

## 1. 現況

3a～3c-6 之後，四種 UI 視窗（候選字窗、捲軸窗、陰影窗、提示窗）的**計算、繪圖與事件處理**都在 Rust（`dime-ui` 的 `candwnd`、`candpaint`、`candproc`、`sbpaint`、`sbproc`、`shadow`、`notify`）。仍在 C++ 的是**視窗本身**：

| 項目 | 位置 | 說明 |
|---|---|---|
| 視窗類別註冊 | `Globals.cpp` 的 `RegisterWindowClass()`（`DllMain` 呼叫） | 5 個類別：`DIME.CandidateWindow`、`.CandidateShadowWindow`、`.CandidateScrollBarWindow`、`.NotifyWindow`、`.NotifyShadowWindow`；從未反註冊（`_UninitWindowClass` 沒有人呼叫） |
| HWND ↔ 物件 | `CBaseWindow::_WindowProc` + `GWLP_USERDATA` | `WM_CREATE` 時存入；`~CBaseWindow` 清為 null，但**不會** `DestroyWindow`（`UIPresenter` 先呼叫 `_Destroy()`） |
| 建立 | `CBaseWindow::_Create` | 依父視窗的 DPI awareness 決定是否暫時切成 per-monitor v2（DPI-unaware 的程式不切，否則座標錯位） |
| capture／timer 擁有者 | `_SetCaptureObject`／`_SetTimerObject` | 一律記在「最上層 UI 視窗」（候選字窗或提示窗）上；`WM_TIMER` 由靜態 WndProc 轉給 timer 物件的 `_OnTimerID` |
| 虛擬視窗 | `_RectOfVirtualWnd` 等 | atom 為 0 時沒有 HWND，只記矩形；正式環境不會發生（測試曾因此誤判，見 3c-2） |
| 物件狀態 | 各子類的成員 | Rust 經 `Dime*HostVtbl` 讀寫（共 4 個 vtable、約 110 個 callback） |
| 對外介面 | `CCandidateWindow`（約 30 個方法）、`CNotifyWindow`（約 10 個） | 呼叫者是 `UIPresenter` 與整合測試 |

死碼：`CButtonWindow`、`CScrollButtonWindow`（舊版捲軸按鈕，已無人建立）。

## 2. 目標

- 視窗類別、HWND 的建立與銷毀、訊息分派、capture／timer、視窗狀態全部在 Rust；四個 host vtable 與其 getter／setter 消失。
- `UIPresenter`（第 4 階段才處理）與整合測試看到的介面不變。
- 行為與外觀不變，延續「C++ 版仍可建置、兩者對拍」的做法，直到第 6 階段刪除 C++ 版。

## 3. 建議做法

### 3.1 Rust 端：`dime-ui::win` 基礎設施

- **類別註冊**：`register_classes(hinstance)` 以相同名稱與樣式（popup 類別不含 `CS_IME`、不含 `CS_DROPSHADOW`；子視窗／陰影類別含 `CS_IME`）註冊，WndProc 是 Rust 的 `extern "system" fn`。`DllMain` 在 Rust 建置下改呼叫它；同時補上 `DLL_PROCESS_DETACH` 時的反註冊（目前沒有，屬既有缺陷，順便修）。
- **物件對應**：`GWLP_USERDATA` 存 `*mut WindowCell`，`WM_NCCREATE` 時設定、`WM_NCDESTROY` 時清除並釋放。視窗物件只有一個擁有者（外部 handle），HWND 被銷毀時物件標記為已銷毀，handle 之後的呼叫一律是 no-op。
- **重入**：訊息處理會重入（捲軸通知候選字窗、回呼可能銷毀視窗）。狀態放在 `Cell`／短暫的 `RefCell` 借用裡，任何會送訊息或呼叫回呼的動作之前先釋放借用；回呼一律是處理流程的最後一步（現有規則）。
- **capture／timer**：照 `CBaseWindow` 的語意重做：擁有者記在最上層視窗，`WM_TIMER` 轉給登記的子物件。
- **分派語意照舊**（`CBaseWindow::_WindowProc`）：`WM_CREATE` 之前的訊息給 `DefWindowProc`；`WM_TIMER` 一律攔下、轉給 timer 物件後回傳 0，不進視窗自己的處理；**沒有設定 UI 視窗的物件（陰影窗）在 `WM_CREATE` 之後所有訊息都回傳 `S_FALSE`、不呼叫 `DefWindowProc`**（陰影窗的訊息處理因此是死碼，`ShadowMessagesAreNotDispatched` 釘住）。
- **建立時的 DPI context**：照搬 `_Create` 的判斷（`GetWindowDpiAwarenessContext`／`AreDpiAwarenessContextsEqual` 動態載入）。

### 3.2 視窗物件搬進 Rust，`UIPresenter` 直接使用

- `CandidateWindow`、`ScrollBar`、`Shadow`、`NotifyWindow` 成為 Rust 的型別，自行持有狀態（3c-1～3c-6 的 host 讀寫改成直接存取欄位）。陰影窗與捲軸窗變成候選字窗／提示窗的內部成員，不再有 C++ 物件。
- **`UIPresenter` 直接持有 Rust handle**（`dime_ui_candidate_*`／`dime_ui_notify_*`），不經過 C++ 外殼。`UIPresenter` 依 `DIME_USE_RUST_ENGINE` 分派：Rust 建置用 handle，C++ 建置照舊用 `CCandidateWindow`／`CNotifyWindow`（和第 2 階段 `KeyHandler` 的分派方式相同）。
- **候選清單模型仍是 C++**（`CCandidateListModel`，字串在 C++）：Rust 候選字窗經一個縮小的 host（取字串、選取／翻頁、回呼）存取；模型的擁有者從 `CCandidateWindow` 改為 `UIPresenter`（Rust 建置下）。
- C++ 建置（`DimeUseRustEngine=false`）維持現在的 `CBaseWindow` 實作不變；`CCandidateWindow`／`CNotifyWindow` 的整合測試照舊測 C++ 類別，`UIPresenterIntegrationTest` 與黃金打字情境在 Rust 建置下測 Rust 視窗。

### 3.3 驗證

- 既有對拍測試改成「C++ 物件 vs Rust 物件」各建一個真實視窗跑相同序列，比對方式不變（像素、狀態、回呼、計時器、capture）。測試 DLL 兩種實作都有編譯，所以可以並排。
- 新增 `win` 基礎設施的單元測試：註冊／反註冊、建立／銷毀（含在回呼中銷毀）、`WM_NCDESTROY` 後 handle 為 no-op、capture／timer 繞道。
- 黃金打字情境（KeyFlow）要在 Rust 建置下全部通過。這組會實際建立視窗並處理訊息。
- 植入錯誤確認每個轉接點都確實走到 Rust（3c-6 的教訓）。

## 4. 分步

| 步驟 | 內容 | 風險 |
|---|---|---|
| ✅ 3c-5a | `dime-ui::win`：類別註冊／反註冊（同名稱與樣式）、`Window`（`GWLP_USERDATA` 指向裝箱的 `Rc<Window>`，`WM_CREATE` 存入、`WM_NCDESTROY` 釋放；每次訊息處理自持一份 `Rc`，視窗在自己的訊息中被銷毀也安全）、`CBaseWindow::_WindowProc` 的分派語意（`WM_TIMER` 攔下、非分派視窗回 `S_FALSE`）、建立時的 DPI context（動態載入）。capture／timer 的繞道留給各視窗型別（候選字窗把計時器轉給捲軸）。單元測試以真實視窗驗證分派、計時器、`S_FALSE`、在自己的訊息中銷毀、物件在視窗與外部參考都消失後才釋放、重複註冊失敗與反註冊（x64 與 i686） | 中：生命週期與重入 |
| ✅ 3c-5b | 陰影窗由 Rust 擁有：`shadow::ShadowWindow`（`win::Window` + 存在 `Cell` 的狀態，沿用 3c-4 的計算經 `StateHost`），`CRustShadowWindow`（`src/RustShadowWindow.*`）是候選字窗／提示窗持有的薄 C++ 型別（`DimeShadowWindow` 依建置開關選它或 `CShadowWindow`）；兩個陰影類別在 Rust 建置下由 `dime_ui_register_window_class` 註冊（名稱不變），五個類別在 `DLL_PROCESS_DETACH` 反註冊（`Global::UnregisterWindowClass`，僅 Rust 建置）。3c-4 的 `DimeShadowHostVtbl` 與 `CShadowWindow` 內的 Rust 分派移除（C++ 建置回到純 C++ 原版，`ShadowCpp` 保留給影像對拍）。`ShadowParityTest` 的行為與分派測試改為 C++ `CShadowWindow` 與 `CRustShadowWindow` 並排（各用自己註冊的測試類別）；植入錯誤（建立時不做 `_Initialize`）被抓到 | 低 |
| ✅ 3c-5c | 捲軸窗由 Rust 擁有：`sbwin::ScrollBarWindow`（子視窗，狀態在 `Cell`，事件與繪圖沿用 `sbproc`／`sbpaint`）。候選字窗改透過 `IDimeScrollBar`（`ScrollBarWindow.h`）操作捲軸，C++ `CScrollBarWindow` 與 `CRustScrollBarWindow`（`src/RustScrollBarWindow.*`）都實作它；依建置開關選擇，測試可用 `_SetScrollBarForTest` 指定實作與類別。`CBaseWindow` 把捲軸的 capture／計時器放在候選字窗上：Rust 捲軸經 `DimeSbParent` 回呼請候選字窗處理，候選字窗把收到的捲軸計時器（`_OnTimerID` 的 default）轉給它。Rust 建置下捲軸類別由 Rust 註冊。3c-3 的 `DimeSbHostVtbl` 與 `CScrollBarWindow` 內的 Rust 分派／`_OnPaintRust` 移除。`ScrollBarPaintMatchesCpp`、`ScrollBarProcParityTest` 改為兩個候選字窗各用一種捲軸並排（新增「把 `WM_TIMER` 送給候選字窗」的步驟，走真正的計時器轉交）；植入錯誤（通知候選字窗時代碼與位置對調）被抓到 | 中 |
| 3c-5d | 候選字窗由 Rust 擁有；`UIPresenter` 在 Rust 建置下直接用 handle；模型改由 `UIPresenter` 持有、經 host 給 Rust | 高：介面最多、和 `UIPresenter` 互動最多 |
| 3c-5e | 提示窗由 Rust 擁有；`UIPresenter` 的提示窗堆疊改用 handle | 中 |
| 3c-5f | Rust 建置不再編譯 C++ 視窗子類；刪除 `CButtonWindow`／`CScrollButtonWindow` 死碼（兩種建置） | 低 |

每一步都有自己的對拍與完整矩陣，個別提交。

## 5. 決定（2026-11-08，使用者）

1. **直接改 `UIPresenter`**：不保留 `CCandidateWindow`／`CNotifyWindow` 外殼；`UIPresenter` 在 Rust 建置下直接持有 Rust handle，C++ 建置照舊（依 `DIME_USE_RUST_ENGINE` 分派）。
2. **狀態搬進 Rust**：host vtable 的 getter／setter 隨之消失；對拍改成 C++ 物件與 Rust 物件並排。
3. **視窗類別名稱**：沿用現有名稱（提案建議，未另行決定）。
4. **反註冊**：Rust 建置下補上 `DLL_PROCESS_DETACH` 的視窗類別反註冊；C++ 建置維持原狀。
5. **死碼**：`CButtonWindow`／`CScrollButtonWindow` 在 3c-5f 刪除（兩種建置）。
