# DIME Rust 移植計畫

狀態：已確認方向（2026-11-06）

## 0. 已決定事項

| 議題 | 決定 | 影響 |
|---|---|---|
| 與上游 `jrywu/DIME` 的關係 | 不追上游 | 不需要為了方便合併而保留 C++ 結構，可以直接重構或刪除 |
| 設定 GUI（DIMESettings） | 改成 Rust | 第 5b 階段要做；`DIMESettings.exe` 最終是純 Rust |
| 最終形態 | Rust 核心 + 少量 C++ | 第 4 階段不強求全部改寫：TSF 外殼可以保留為薄 C++ 層，只負責把 Rust 產生的動作套用到 TSF |

最終架構：

- `DIME.dll`：所有邏輯（設定、組字、候選字、整句、字典）與 UI 視窗在 Rust；C++ 只剩 COM 進入點、TSF sink 與 edit session 的轉接（目標 ≤ 5k 行）。
- `DIMESettings.exe`：純 Rust（CLI + GUI）。
- 舊版 `ConfigDialog.cpp`（2.6k 行）：DLL 已改成啟動 `DIMESettings.exe`（`ShowConfig.cpp`），直接刪除，不移植。

## 1. 現況

| 項目 | 規模 | 狀態 |
|---|---|---|
| 字典引擎（`DictionaryParser` / `DictionarySearch` / `TableDictionaryEngine` / Big5 過濾） | C++ ≈ 2,000 行 → Rust ≈ 1,450 行 | ✅ 已移植，`DimeUseRustEngine` 開關，對拍測試 |
| 整句輸入 Viterbi + 使用者學習 | Rust ≈ 980 行 | ✅ 僅 Rust |
| FFI（`dime-ffi`） | ≈ 840 行 | ✅ C ABI，`catch_unwind` 包住每個匯出函式 |
| 其餘 C++ | ≈ 42,000 行 / 110 個檔案 | ⏳ 本計畫範圍 |

剩下的 C++ 依「對 TSF／Win32 的相依程度」可分成四層（數字為行數，含 `.h`）：

| 層 | 主要檔案 | 行數 | TSF/COM 相依 |
|---|---|---|---|
| **A. 純邏輯** | `Config` (1.5k)、`CompositionProcessorEngine` + `Setup…` (2.8k)、`KeyProcesser` (1.0k)、`KeyStateCategory` (0.8k)、`SentenceBuffer` (0.6k)、`BaseStructure` (0.8k)、`PhoneticComposition.h`、`File` (1.0k)、`RegKey` (0.4k) | ≈ 9k | 幾乎沒有（只有 preserved key 註冊與 `CConfig` 的 ACL 設定） |
| **B. TSF 互動流程** | `KeyEventSink`、`KeyHandler*`、`CandidateHandler`、`SentenceHandler`、`Composition`、`Start/EndComposition`、`EditSession` | ≈ 3.5k | 高：邏輯與 edit session 交錯 |
| **C. UI** | `UIPresenter` (2.1k)、`CandidateWindow` (2.2k)、`ScrollBarWindow` (1.4k)、`NotifyWindow` (0.9k)、`BaseWindow` (1.1k)、`ShadowWindow`、`ButtonWindow`、`LanguageBar` (1.3k)、`UIACaretTracker` | ≈ 9.5k | 中：GDI 繪圖、`UIPresenter`／`LanguageBar` 碰 TSF |
| **D. TSF COM 外殼** | `DIME.cpp/.h` (1.6k)、`DllMain`、`Server`、`Register`、各種 sink、`Compartment`、`DisplayAttribute*`、`EnumTfCandidates`、`TipCandidate*`、`ReverseConversion*`、`SearchCandidateProvider`、`FunctionProviderSink`、`TfTextLayoutSink`、`Globals` | ≈ 8k | 全部 |
| **E. DIMESettings.exe** | `CLI` (1.3k)、`SettingsWindow` (4.4k)、`SettingsController` (0.8k)、`SettingsPageLayout/Geometry`、`DIMESettings.cpp` (0.7k)、`ConfigDialog`（2.6k，舊版對話框，DLL 也用）、`ShowConfig` | ≈ 11k | 無 TSF，Win32 GUI |

## 2. 原則

1. **由下往上、由內往外**：先移植不碰 TSF 的邏輯層，最後才換 COM 外殼。每一階段結束時產品都能出貨。
2. **一律有開關、一律有對拍**：沿用字典引擎的做法——新 Rust 模組先在 C++ 介面後面以建置開關替換，預設仍走 C++；用對拍測試證明行為一致、經過至少一次發版後才把預設切到 Rust，再下一版刪除 C++ 實作。
3. **保留怪異行為**：輸入法使用者依賴肌肉記憶，任何差異都是 bug，除非刻意修正並記錄在 `docs/`。
4. **C++ 呼叫端不動**：`CConfig`、`CCompositionProcessorEngine` 等保留原本的 C++ 介面，內部改轉呼 Rust。避免同時改幾百個呼叫點。
5. **FFI 邊界不讓 panic 通過**：維持 `catch_unwind`；COM 階段每個 vtable 方法都要包住，panic 轉成 `E_FAIL`。
6. **執行環境限制**：DLL 會載入到每個應用程式（含 Store app／AppContainer、低完整性行程），所以禁止 Rust 端在 DLL 內新增執行緒、全域鎖要避免死結、檔案存取要容忍唯讀。`/MT` + `+crt-static` 的設定沿用。

## 3. 分階段計畫

### 第 1 階段：設定（`CConfig`）→ `dime-config` crate

- **內容**：每個模式的設定結構、預設值、INI 讀寫（UTF-16）、`ApplyIniConfigEntry`、各設定項的合法值檢查、模式適用性（目前分散在 `Config.cpp`、`CLI.cpp`、`SettingsController.cpp`）。
- **作法**：新增 `dime-config`，設定項以一張表（key、型別、預設值、適用模式、範圍）描述，取代目前「GUI、CLI、`CConfig` 三處要一致」的人工同步。`CConfig` 的 static getter/setter 保留，改成讀寫 Rust 端的狀態。ACL（"ALL APPLICATION PACKAGES"）與 `%APPDATA%` 路徑可先留在 C++。
- **測試**：`ConfigTest.cpp`（2.1k 行）照跑；新增「同一份 INI 由 C++ 與 Rust 讀入後所有欄位相同、寫出位元組相同」的對拍；Rust 端單元測試覆蓋每個 key 的邊界值。
- **為什麼先做**：相依最少（被大家用、自己不用別人），而且 DLL 與 DIMESettings 共用，做完後第 5 階段的 CLI 可以直接沿用。
- **進度（2026-11-06）**：
  - ✅ 1a：`rust/crates/dime-config`（設定值、預設值、`ApplyIniConfigEntry`、載入流程與 ColorMode 遷移、`WriteConfig` 輸出、CLI key 定義表與驗證）與 FFI（`dime_settings_*`）。`DIME.dll` 在 `DimeUseRustEngine=true` 時，設定的解析、預設值與寫檔改走 Rust；`CConfig` 的 static 成員仍是儲存位置。
  - ✅ 對拍：`src/tests/RustConfigParityTest.cpp`（2,201 組 INI key/值、各模式預設值、20 份 `WriteConfig` 輸出逐位元組、`LoadConfigFromFile`）。對拍時找到並修正一個差異：`CLSIDFromString("")` 會失敗，空字串不會清除 GUID（只有 CLI 會把空字串當成清除）。
  - ✅ 1b：`DIMESettings.exe`（Win32）在 `DimeUseRustEngine=true` 時連結 `dime_ffi.lib`，定義 `DIME_USE_RUST_CONFIG`：設定的預設值與寫檔、CLI `--get`/`--set` 的取值、驗證與錯誤訊息改走 Rust（字典引擎仍是 C++）。C++ 的 `g_keyRegistry` 暫時保留，`RustCliParityTest` 檢查它與 Rust 的 key 表完全一致，並對拍 `--get` 全部 key 與 `--set` 2,014 組 key/值（回傳碼、錯誤訊息、結果）。C++ 與 Rust 版 exe 的實際 CLI 輸出也逐一比對過。`g_keyRegistry` 與 `GetKeyValue`/`ApplyKeyValue` 的 C++ 版本在第 5a 階段 CLI 整個改寫成 Rust 時刪除。
  - ⏳ 1c：把儲存位置從 `CConfig` 的 static 成員移到 Rust（`CConfig` 的 getter/setter 改成轉呼 Rust），經過一個正式版本後再把預設切到 Rust。

### 第 2 階段：組字核心 → `dime-core::compose`

- **內容**：字根緩衝、`KeyStateCategory` 按鍵分類、`KeyProcesser` 的判斷、`IsVirtualKeyNeed`／`IsKeystrokeRange`、各模式規則（大易地址鍵、行列特別碼／短碼／詞彙結束鍵、注音組字與許氏鍵盤、萬用字元）、候選字清單產生與排序、繁簡轉換、`TCFreq`、`SentenceBuffer`。
- **作法**：把目前分散在 `KeyEventSink` → `KeyProcesser` → `KeyHandler*` 的決策抽成一個不依賴 TSF 的狀態機：

  ```rust
  pub struct ImeCore { /* 模式、字根、候選、整句緩衝… */ }
  impl ImeCore {
      /// 回傳「是否吃掉按鍵」與要執行的動作；不碰任何 Windows API。
      pub fn on_key(&mut self, key: KeyEvent, ctx: &Context) -> KeyResult;
  }
  pub enum Action {
      SetComposition { text: String, caret: usize, attrs: Vec<Span> },
      ShowCandidates { items: Vec<Candidate>, page: usize, selected: usize },
      HideCandidates, Commit(String), Notify(String), /* … */
  }
  ```

  C++ 的 sink／edit session 只負責把 `Action` 套用到 TSF。這一步也是第 3、4 階段的前提。
- **建議分兩步**：(2a) 先把 C++ 自己重構成「決策 → 動作」的形式（仍是 C++），補上錄製測試；(2b) 再把決策層換成 Rust。直接跳 (2b) 的話，沒有乾淨的 C++ 基準可以對拍。
- **測試**：建立「按鍵序列 → 動作序列」的黃金測試資料：四種模式 × 主要設定組合（`SentenceMode`、`DayiSelkeyMode`、`ArrayScope`、注音鍵盤、空白鍵選字…），用實際 `installer/*.cin` 碼表。同一序列餵給 C++ 與 Rust，逐筆比對。`TSFCoreLogicIntegrationTest`、`SelkeyModeTest`、`SentenceBufferTest` 改接新介面。
- **風險**：這是整個計畫最難、也最有價值的一段；目前邏輯與 TSF 呼叫交錯（例如 `_HandleCandidateWorker`、`SentenceHandler` 的攔截點），抽離時最容易改到行為。

### 第 3 階段：UI 視窗 → `dime-ui` crate

- **內容**：`BaseWindow` 階層、`CandidateWindow`（含水平模式、捲軸、DPI、亮／暗主題）、`NotifyWindow`、`ShadowWindow`、`ScrollBarWindow`、`ButtonWindow`；`UIPresenter` 中不碰 TSF 的版面計算（位置、翻頁、螢幕邊界）。
- **作法**：用 `windows` crate 直接呼叫 Win32／GDI，維持與現在相同的繪圖方式（`ExtTextOut` 等），不在這一步換成 Direct2D／DirectWrite，避免外觀變化與移植混在一起。版面計算抽成純函式（輸入字型度量、DPI、工作區；輸出矩形），可單元測試。
- **測試**：版面純函式做對拍；繪圖結果用「固定 DPI／主題／候選字 → 截圖」比對 C++ 版本（允許少量像素差、需人工確認）。`CandidateWindowIntegrationTest`、`NotificationWindowIntegrationTest`、`UIPresenterIntegrationTest` 改寫。
- **相關文件**：`CANDI_WINDOW.md`、`CANDI_HORIZONTAL.md`、`CANDI_SCROLL_BAR.md`、`ADAPTIVE_SHADOW.md`、`DPI_AWARE.md`、`LIGHT_DARK_THEME.md`、`MULTI_NOTIFY.md`、`LAYOUT_RENDERER.md`。

### 第 4 階段：TSF COM 外殼 — 精簡為薄 C++ 層（全面改寫為選做）

依「Rust 核心 + 少量 C++」的決定，本階段的必要部分是**把 C++ 外殼縮到只剩轉接**：第 2、3 階段完成後，`KeyHandler*`、`CandidateHandler`、`SentenceHandler`、`UIPresenter` 中的邏輯已移到 Rust，C++ 只保留 COM 進入點、class factory、`Register`、各 sink 的 TSF 介面實作、edit session，以及把 Rust `Action` 套用到 TSF 的程式碼。`Globals`、`BaseStructure` 中已無人使用的部分刪除。

以下為**選做**：日後若要連外殼也改成 Rust，作法如下。

- **內容**：`DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` / `DllUnregisterServer`（`DIME.def` 的四個匯出）、class factory、TIP 註冊（CLSID、語言設定檔 GUID、類別、圖示）、`ITfTextInputProcessorEx` 與所有 sink、edit session、compartment、display attribute、語言列按鈕、反查（`ITfReverseConversion*`）、`ITfFnSearchCandidateProvider`、text layout sink、UIA 游標追蹤。
- **作法**：`windows` crate 的 `#[implement]`。`dime-ffi` 的 `staticlib` 改為新增 `cdylib` 產出 `DIME.dll`。可以分批：先讓 Rust 的 `CDIME` 對應物取代 C++，UI 暫時經由 FFI 回呼 C++（若第 3 階段尚未完成），反之亦然。
- **相容性要求**：CLSID、profile GUID、登錄機碼、`%APPDATA%\DIME\` 檔案格式**全部不變**，安裝程式（WiX `regsvr32` custom action、`DIMEInstallerCA`）不需修改；Win32／x64／ARM64／ARM64EC 四個平台都要產出。
- **測試**：`TSFIntegrationTest*` 改成對 DLL 黑箱測試（`CoCreateInstance` + 模擬 thread manager）；加上手動測試清單：記事本、Word、Edge／Chrome、Store app（唯讀設定）、命令提示字元、遊戲全螢幕、多螢幕／混合 DPI、登入畫面（若有支援）。參考 `TEST_PLAN.md`、`CARET_TRACKING*.md`。
- **風險**：COM 生命週期（AddRef／Release、`DllCanUnloadNow` 計數）錯誤會讓宿主程式當掉；TSF 回呼的重入（edit session 在同步／非同步下的差異）。

### 第 5 階段：DIMESettings.exe

- **5a. CLI**（`CLI.cpp`）：第 1 階段完成後很直接，exit code（0／1／2／3）與輸出格式維持 `docs/DIME_CLI.md`。`CLIIntegrationTest`、`CLIParserTest` 當成黑箱測試繼續用（對 exe 下指令比對輸出）。
- **5b. 設定 GUI**（`SettingsWindow`、`SettingsController`、`SettingsPageLayout`、`SettingsGeometry`、`DIMESettings.cpp`，≈ 5.8k 行）→ 新的 `dime-settings` binary crate，產出 `DIMESettings.exe`。
  - **作法**：用 `windows` crate 直接移植現有的 Win32 視窗，不換 GUI 框架。這樣外觀、DPI、亮／暗主題、鍵盤操作和現在一樣，執行檔也小。`SettingsController`（不碰視窗的狀態與驗證）先移植並沿用 `SettingsControllerTest` 的案例；`SettingsPageLayout`／`SettingsGeometry` 是純計算，直接做單元測試。
  - **相關文件**：`NEW_DIME_SETTIGNS.md`、`DIME_SETTINGS_COMBO_FLOATING.md`、`RICHEDIT_VIEWPORT_VAL.md`、`CUSTOM_TABLE_VALIDATION.md`、`LIGHT_DARK_THEME.md`、`DPI_AWARE.md`。
  - **測試**：`SettingsDialogIntegrationTest`、`RichEditViewportTest` 改成對新 exe 的 UI 自動化測試（UIA），加上各頁面截圖比對與手動清單。
  - **清理**：舊版 `ConfigDialog.cpp` 與 `ShowConfig.cpp` 中的 legacy PropertySheet 分支直接刪除。
  - `DIMESettings.exe` 只有 Win32 一種，CI 與安裝程式路徑不變。

### 第 6 階段：清理

- 移除 C++ 實作與 `DimeUseRust*` 開關、`RustTableDictionaryEngine` 轉接層、`TestStubs.cpp`。
- C++ 單元測試（CppUnitTestFramework）中針對已刪除類別的部分轉成 Rust 測試；黑箱測試（CLI、TSF）保留。
- 更新 `CLAUDE.md`、`HOW_TO_BUILD.md`、CI workflow：`DIMESettings.exe` 改由 cargo 建置；`DIME.dll` 仍由 MSBuild 建置薄 C++ 外殼並連結 Rust 靜態函式庫（若做了第 4 階段選做部分，則改由 cargo 產出 `cdylib`）。

## 4. 順序與相依

```
第1階段 設定 ──┬──> 第2階段 組字核心 ──> 第4階段 精簡 TSF 外殼 ──> 第6階段 清理
              │           │                       ^                ^
              │           └──> 第3階段 UI ─────────┘                │
              └──> 第5a階段 CLI ──> 第5b階段 設定 GUI ──────────────┘
```

第 1 與 5a 可以先平行做；第 3 階段可以在 2a（C++ 重構）之後和 2b 平行；第 5b 只依賴第 1 與 5a，可以和第 2、3 階段平行。

## 5. 粗估規模

| 階段 | 要移植的 C++ | 難度 | 主要風險 |
|---|---|---|---|
| 1 設定 | ≈ 1.5k 行 | 低 | INI 寫出格式、與 DIMESettings 共用 |
| 2 組字核心 | ≈ 9k 行（含重構） | **高** | 行為差異，按鍵邏輯與 TSF 交錯 |
| 3 UI | ≈ 9.5k 行 | 中高 | 外觀差異、DPI／主題、多螢幕 |
| 4 精簡 TSF 外殼 | 刪減 ≈ 3k 行，保留 ≈ 5k 行 C++ | 中 | 動作套用順序、edit session 重入 |
| 4 選做：外殼改 Rust | ≈ 5k 行 | **高** | COM 生命週期、宿主程式穩定性 |
| 5a CLI | ≈ 1.3k 行 | 低 | — |
| 5b 設定 GUI | ≈ 5.8k 行（另刪除 `ConfigDialog` 2.6k 行） | 中 | 外觀、RichEdit 行為、DPI |

## 6. 尚待決定的事

1. **繪圖技術**：第 3 階段維持 GDI（建議），還是趁機換 Direct2D／DirectWrite（外觀會變，需要另開議題）。
2. **發版節奏**：每個階段切換預設前，要經過幾個版本的開關期（建議至少一個正式版本）。

## 7. 建議的第一步

第 1 階段（設定）：範圍小、相依清楚、有完整的 `ConfigTest` 與 CLI 測試保護，而且能順便消除「GUI、CLI、`CConfig` 三處要一致」的維護負擔，也是 5a／5b 的基礎。完成後接著做 2a（把 C++ 按鍵流程重構成「決策 → 動作」並建立錄製測試），這是第 2～4 階段的基礎。
