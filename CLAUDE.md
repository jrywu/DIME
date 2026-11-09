# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

DIME 是 Windows 8/10/11 的中文輸入法，以 100% 原生 C++ 實作 Windows TSF（Text Services Framework），無任何第三方相依。同一個 `DIME.dll` 提供四種輸入法模式（`IME_MODE`，定義於 `src/BaseStructure.h`）：大易 `DAYI`、行列 `ARRAY`、注音 `PHONETIC`、自訂 `GENERIC`。

## 建置

需求：Visual Studio（C++ 桌面開發 workload、Windows SDK；CI 使用 `PlatformToolset=v143`）。方案檔是 `src/DIME.sln`，指令要在 `src/` 下執行。

```powershell
cd src
msbuild DIME.sln /p:Configuration=Debug /p:Platform=x64                       # 整個方案（含測試）
msbuild DIME.sln /p:Configuration=Release /p:Platform=x64 /t:DIME              # 僅 IME DLL
msbuild DIME.sln /p:Configuration=Release /p:Platform=Win32 /t:DIMESettings    # 設定程式（僅 Win32）
```

- `DIME` 專案 → `DIME.dll`（Win32 / x64 / ARM64 / ARM64EC）；`DIMESettings` 專案 → `DIMESettings.exe`（僅 Win32，所有平台共用）；`DIMETests` 專案 → 單元測試 DLL。
- 每次建置的 PreBuildEvent 會執行 `src/buildInfo.cmd`，以 git commit 數與日期重新產生 **受版控的** `src/BuildInfo.h`（版號格式 `1.3.<commit數>.<日期>`）。建置後 `BuildInfo.h` 出現 diff 屬正常；除非是要發版，否則不要把它一起 commit。

## 測試

使用 Microsoft CppUnitTestFramework（`TEST_CLASS` / `TEST_METHOD`），所有測試在 `src/tests/`，編成單一 `DIMETests.dll`，輸出到 `src/bin/<Platform>/<Configuration>/`。

```powershell
cd src
msbuild DIME.sln /p:Configuration=Debug /p:Platform=x64
vstest.console.exe bin\x64\Debug\DIMETests.dll                                   # 全部
vstest.console.exe bin\x64\Debug\DIMETests.dll /Tests:ParseModeTests               # 單一測試類別或方法（名稱片段比對）
vstest.console.exe bin\x64\Debug\DIMETests.dll /TestCaseFilter:"FullyQualifiedName~CLI"
```

- CI（`.github/workflows/DIME_tests.yml`）跑 Win32/x64 × Debug/Release 矩陣。
- `src/tests/run_all_coverage.ps1` 以 OpenCppCoverage 產生覆蓋率報告。
- **按鍵流程黃金測試**（`KeyFlowTest.cpp` + `KeyFlowHarness.*`）：真實 TSF thread manager + 自製 `ITextStoreACP`，把按鍵腳本送進未修改的 `CDIME`，逐鍵記錄吃鍵與否、文件文字、組字、候選字，與 `src/tests/keyflow/*.txt` 比對。改動按鍵處理、候選字、整句等行為時，先跑這組；行為是刻意改變時設 `DIME_KEYFLOW_RECORD=1` 重新錄製並檢查 diff。測試用 `Global::testAppDataOverride` 與 `%ProgramW6432%` 指向暫存資料夾，不碰真實 `%APPDATA%\DIME`；產品程式碼取得 `%APPDATA%` 一律用 `Global::GetAppDataPath()`。
- 測試專案**直接編譯** `src/` 與 `src/DIMESettings/` 的產品 `.cpp`（不是連結 `DIME.dll`），並定義 `DIME_UNIT_TESTING`。此巨集會在 `Config.h`、`DIME.h`、`File.h`、`UIPresenter.h`、`CLI.h` 等處開放測試用 hook；`TestStubs.cpp` 補上被排除的 `DllMain.cpp` 符號。新增產品 `.cpp` 而測試需要用到時，要一併加進 `src/tests/DIMETests.vcxproj`。

## 架構重點

- **TSF 文字服務（`DIME.dll`）**：`CDIME`（`DIME.cpp/.h`）是 TSF text service 主體，實作各種 sink，分散在多個檔案：`KeyEventSink.cpp`、`ThreadMgrEventSink.cpp`、`TextEditSink.cpp`、`ThreadFocusSink.cpp`、`ActiveLanguageProfileNotifySink.cpp`、`FunctionProviderSink.cpp` 等。COM 註冊在 `Register.cpp` / `Server.cpp` / `DllMain.cpp`。
- **按鍵處理流程**：`KeyEventSink` → `KeyProcesser` / `KeyStateCategory`（判斷按鍵類別）→ `KeyHandler*.cpp`（透過 `EditSession` 在 TSF edit session 中修改 composition）→ `CompositionProcessorEngine`（字根緩衝、查字典、產生候選字；初始化在 `SetupCompositionProcesseorEngine.cpp`，檔名拼錯是原樣）。
- **字典**：`.cin` 碼表由 `DictionaryParser` / `DictionarySearch` 解析與搜尋，`TableDictionaryEngine` 包裝，檔案透過 `File.cpp` 的記憶體映射（`CMemoryFile`）讀取。反查走 TSF 標準介面（`ReverseConversion*.cpp`、`SearchCandidateProvider`）。
- **UI**：`UIPresenter` 協調候選字窗 `CandidateWindow`、提示窗 `NotifyWindow`、`ShadowWindow`、`ScrollBarWindow`（皆繼承 `BaseWindow`）；`LanguageBar` 為語言列按鈕。游標位置追蹤用 TSF text layout 加 `UIACaretTracker`（UIA fallback）。
- **設定**：`CConfig`（`Config.cpp`，全為 static）讀寫 `%APPDATA%\DIME\` 下每個模式各自的 INI（`DayiConfig.ini`、`ArrayConfig.ini`、`PhoneConfig.ini`、`GenericConfig.ini`），本身也用 `TableDictionaryEngine` 解析 INI。Store app（AppContainer）只能唯讀，`Config.cpp` 會為 "ALL APPLICATION PACKAGES" 開讀取權限。
- **`DIMESettings.exe`**：與 DLL 共用 `Config.cpp`、`ConfigDialog.cpp`、字典相關檔案等原始碼（見 `DIMESettings.vcxproj`）。內含新版 GUI（`SettingsWindow` / `SettingsController` / `SettingsPageLayout` / `SettingsGeometry`，key 定義在 `SettingsKeys.h`）以及無頭 CLI（`CLI.cpp`，文件見 `docs/DIME_CLI.md`；exit code：0 成功、1 參數錯誤、2 I/O 錯誤、3 該 key 不適用此模式）。修改設定項時，GUI、CLI、`CConfig` 三處都要一致。
- 各功能的設計文件與議題紀錄放在 `docs/*.md`（例如 `CANDI_WINDOW.md`、`CARET_TRACKING*.md`、`LIGHT_DARK_THEME.md`、`MSI_INSTALLER.md`），修改相關區域前先查閱。

## Rust 移植（進行中）

`rust/` 是逐步改寫成 Rust 的 Cargo workspace（第 1、2 階段完成：設定、字典引擎、組字核心、按鍵處理流程）。**預設建置使用 Rust 核心**（`DimeUseRustEngine` 預設為 true，建置時需要 Rust 工具鏈與對應的 target）；`/p:DimeUseRustEngine=false` 仍可建出全 C++ 版本，兩者以相同的測試與黃金情境驗證，CI 兩種都跑。`DIMESettings.exe` 的字典引擎一律是 C++（開關只讓它的設定邏輯改走 Rust）。`rust/Cargo.lock` 有進版控（建置用 `cargo --locked`）。

- **開關**：`DimeUseRustEngine`（預設 true）會定義 `DIME_RUST_AVAILABLE` + `DIME_USE_RUST_ENGINE`，建置前自動跑 cargo（Release 用 `+crt-static` 配合 `/MT`，輸出到 `rust/target/crt-static/`）。支援 Win32/x64/ARM64/ARM64EC。CI 的 Build workflow 手動觸發時有 `rust_engine` 選項。
- **接線方式**：所有正式程式碼都透過 `CTableDictionaryEngine::Create()` 建立引擎，開關打開時回傳 `CRustTableDictionaryEngine`（`src/RustTableDictionaryEngine.*`，覆寫虛擬的 `CollectWord*` / `ParseConfig`）。不要再直接 `new CTableDictionaryEngine`。碼表緩衝區仍由 `CFile`/`CMemoryFile` 提供（含重新載入與 Big5 過濾）；回傳字串在轉接層內 intern，指標在引擎存活期間有效；排序仍是 C++。`[Config]` 套用到 `CConfig` 的對應表在 `ApplyIniConfigEntry()`（`DictionarySearch.cpp`），C++ 與 Rust 兩條路徑共用。

- `crates/dime-core`：`DictionaryParser` / `DictionarySearch` / `TableDictionaryEngine` 與 `CMemoryFile` Big5 過濾的逐行移植。刻意保留 C++ 的怪異行為（例如 chardef 中沒有分隔字元的行會中止搜尋、`%selkey` 只認 tab 分隔、排序演算法的平手順序），因為呼叫端依賴這些行為；修改前先確認對拍測試。地區相關比較透過 `TextOps` trait，Windows 上的 `WinTextOps` 呼叫與 C++ 相同的 `CompareStringW` / `GetStringTypeExW`。
- `crates/dime-config`：`CConfig` 的移植（移植計畫第 1 階段，見 `docs/RUST_MIGRATION_PLAN.md`）：設定值與預設值、`ApplyIniConfigEntry`、`LoadConfig` 的載入流程（含舊版 ColorMode 遷移）、`WriteConfig` 的輸出（逐位元組相同）、CLI 的 key 定義表與 `--get`/`--set` 驗證。數字解析刻意模擬 MSVC 的 `_wtoi`/`wcstol`/`wcstoul`（全形數字、`0x`/八進位前綴、溢位截斷）。定義 `DIME_USE_RUST_CONFIG` 時（`DIME.dll` 與 `DIMESettings.exe` 在 `DimeUseRustEngine=true` 時都會定義；DIMESettings 只有 Win32，且不定義 `DIME_USE_RUST_ENGINE`，字典引擎仍是 C++），設定值存放在 Rust 的全域物件，`CConfig` 的設定成員（`DIME_CONFIG_VALUE` / `DIME_CONFIG_DEFINE`）變成指向它的參考；未定義時仍是一般 static 成員。呼叫 `dime_settings_*` 一律透過 `CRustConfigScope`（Rust 模式下就是全域物件；否則是快照，`Commit()` 後寫回）。`ReverseConversionDescription` 要用 `Get/SetReverseConversionDescriptionText()` 存取，不可對它 `delete[]`。新增設定項時，`DimeConfigValues`（308 bytes，兩邊都有 size 檢查）、`DIME_CONFIG_DEFINE` 對應欄位、`ExportValues`/`ImportValues` 要一起改。`DIMETests` 預設（`DimeTestRustEngine=true`）等同 `DimeUseRustEngine=true` 的完整 Rust 組合；`/p:DimeTestRustEngine=false` 測全 C++ 版（黃金情境要用這個模式錄製，再以預設模式比對）；`/p:DimeTestRustEngine=false /p:DimeTestRustConfig=true` 只把設定儲存換成 Rust。CLI 的 `GetKeyValue`/`ApplyKeyValue` 在該巨集下改用 `dime_settings_get_key`/`dime_settings_set_key`；新增 key 時 `CLI.cpp` 的 `g_keyRegistry` 與 `keys.rs` 的 `KEYS` 要一致（`RustCliParityTest` 會檢查）。
- 候選字選擇與翻頁：`dime-core::candidates` ↔ `CRustCandidateListModel`（`src/RustCandidateListModel.*`）。候選字串留在 C++ 的 `CCandidateListModel`；一律用 `CCandidateListModel::Create()` 建立（`CCandidateWindow` 已如此）。對拍：`RustCandidateListModelParityTest`。
- 按鍵分類：`dime-core::keyclass` ↔ `CCompositionProcessorEngine::_IsVirtualKeyNeedRust`（`KeyProcesser.cpp`）。`IsVirtualKeyNeed` 在 `DIME_USE_RUST_ENGINE` 時改走 Rust，C++ 版保留為 `_IsVirtualKeyNeedCpp`。輸入是 `DimeKeyInput` 快照；`IsVirtualKeyNeed` 新增讀取的狀態或設定時，兩邊與快照都要一起改。對拍：`KeyClassifyParityTest`。
- 組字緩衝：`dime-core::composition` ↔ `AddVirtualKey`／`RemoveVirtualKey`／`IsEscapeInput`／`GetReadingString`／`buildKeyStrokesFromPhoneticSyllable` 的 `_XxxRust` 版本（緩衝區仍是 C++ 配置）。改這些函式時 C++ 與 Rust 兩版要一起改。對拍：`CompositionBufferParityTest`。
- 候選字產生：`dime-core::candgen` 產生 `GetCandidateList` 的步驟（聯想詞查詢鍵 `associated_phrase_key` 也在這裡；CIN 聯想詞表精確比對），`_GetCandidateListRust`（`CompositionProcessorEngine.cpp`）對 C++ 持有的碼表引擎執行。`GetCandidateList` 的搜尋邏輯要改時，`_GetCandidateListCpp` 與 `candgen.rs` 一起改。對拍：`CandidateGenParityTest`（假引擎記錄每次搜尋）。
- 整句緩衝區：`dime-core::sentence_buffer` ↔ `CSentenceBuffer`（`SentenceBuffer.*`），FFI `dime_sentence_buffer_*`。改 `CSentenceBuffer` 時兩邊一起改。對拍：`SentenceBufferParityTest`。
- Handler 流程：`dime-core::session`（`Host` trait）↔ `src/RustSessionHost.*`（`DimeHost` vtable，`CDIME` 的 friend）。已移植的 handler 在 `KeyHandler.cpp` 等處依 `DIME_USE_RUST_ENGINE` 分派，C++ 原版改名 `_XxxCpp`；改這些 handler 時兩邊一起改。Host callback 不可丟例外。vtable 欄位順序在 `dime-ffi` 的 `DimeHostVtbl`、`dime_ffi.h`、`RustSessionHost.cpp` 的 `s_vtbl` 三處必須一致。Rust 擁有的狀態在 `session::SessionState`（`CDIME::_pRustSession`）：整句模式、Shift 英數／萬用字元等待旗標、`_candidateMode`／`_isCandidateWithWildcard`（後兩者在 Rust 建置下是綁到 session 欄位的 C++ reference）。驗證靠黃金打字情境：用預設建置錄製，再以 `/p:DimeTestRustEngine=true` 比對。
- `crates/dime-ui`（第 3 階段）：UI 的純計算。`layout.rs` ↔ `src/UILayout.*`（視窗定位、提示窗堆疊；C++ 原版在 `UILayout::Cpp`，`CBaseWindow`／`CUIPresenter` 呼叫分派版本）；`candwnd.rs` ↔ 同一組 `UILayout::Cand*`／`ScrollBar*`（候選字窗的列版面、截斷加寬、點擊測試、頁碼，捲軸的按鈕／滑塊／拖曳；`CCandidateWindow`／`CScrollBarWindow` 呼叫分派版本）。改這些計算時 `UILayout::Cpp` 與 Rust 一起改；`MulDiv` 用 `UILayout::ScaleForDpi`／`MulDivInt`（Rust 版模擬系統 `MulDiv`，x86 與 x64 在 `INT_MIN` 上行為不同，依目標架構選用）。`UILayout.cpp` 內的 `Cpp` 函式互相呼叫時要寫 `Cpp::`（否則 ADL 會找到外層同名函式而模稜兩可）。對拍：`UILayoutParityTest`。`candpaint.rs`（僅 Windows）是候選字窗的 GDI 繪圖、`candproc.rs`（僅 Windows）是候選字窗的訊息處理，兩者只給 Rust 候選字窗（`candwin.rs`）使用；C++ `CCandidateWindow`（C++ 建置）的 `_OnPaint`／`_WindowProcCallback` 是純 C++ 原版。改繪圖或訊息處理時兩邊一起改，對拍 `CandidatePaintParityTest::PaintMatchesCpp`（多種字型，真實視窗畫進 DIB 逐像素比對）與 `CandidateWindowParityTest`（繪圖、隨機訊息、操作）。`sbpaint.rs`（僅 Windows）是捲軸的繪圖（Rust 捲軸使用）；對拍 `CandidatePaintParityTest::ScrollBarPaintMatchesCpp`（兩種捲軸並排）。`sbproc.rs`（僅 Windows）是捲軸的事件邏輯（`SbHost`），`sbwin.rs` 是 Rust 擁有的捲軸視窗（狀態在 `Cell`；通知候選字窗會重入捲軸，不可跨呼叫持有借用）。候選字窗經 `IDimeScrollBar` 操作捲軸（C++ `CScrollBarWindow` 或 `CRustScrollBarWindow`，依建置開關；測試用 `_SetScrollBarForTest` 指定），Rust 捲軸的 capture／計時器經 `DimeSbParent` 放在候選字窗上，候選字窗 `_OnTimerID` 轉回給它；對拍 `ScrollBarProcParityTest`（`g_baseWindowTimerTestHook` 記錄計時器）。`shadow.rs` 是陰影窗（影像計算跨平台、視窗僅 Windows）；Rust 建置下陰影窗由 Rust 擁有（`shadow::ShadowWindow`，C++ 端是 `CRustShadowWindow`，候選字窗／提示窗持有 `DimeShadowWindow`），C++ 建置用原本的 `CShadowWindow`，其計算在 `ShadowCpp::Color`／`FillPixels`；對拍 `ShadowParityTest`（影像逐位元組比對，浮點運算順序要和 C++ 一致）。`win.rs` 是 Rust 擁有視窗的基礎設施（類別註冊、`GWLP_USERDATA` 對應、`CBaseWindow::_WindowProc` 的分派語意），設計見 `docs/RUST_WINDOW_OWNERSHIP.md`；Rust 建置下用 Rust 視窗程序註冊的類別不能拿來建立 C++ 的 `CBaseWindow` 物件（測試要另外註冊自己的類別）。`candwin.rs`（僅 Windows）是 Rust 擁有的候選字窗（自有狀態，擁有陰影窗與捲軸窗）；Rust 建置下 `UIPresenter` 持有 `DimeCandidateWindow` = `CRustCandidateWindow`（`src/RustCandidateWindow.*`，不是視窗：持有 `CCandidateListModel` 與 Rust handle，Rust 經 `DimeCandModel` 回呼讀候選清單），C++ 建置是 `CCandidateWindow`；`DIME.CandidateWindow` 類別在 Rust 建置下由 Rust 註冊，測試中的 C++ `CCandidateWindow` 改用 `EnsureWindowClassesRegistered()` 註冊的 `g_cppCandidateWindowAtomForTest`。對拍 `CandidateWindowParityTest`（C++ `CCandidateWindow` 與 `CRustCandidateWindow` 並排），`UIPresenterIntegrationTest::IT06_04_CandidateWindowIsReal` 確認 `UIPresenter` 建立的是真實視窗。`notify.rs`（僅 Windows）是提示窗的繪圖與行為，只給 Rust 提示窗（`notifywin.rs`）使用；C++ `CNotifyWindow`（C++ 建置）是純 C++ 原版。`notifywin.rs` 是 Rust 擁有的提示窗（自有狀態與陰影窗），Rust 建置下 `UIPresenter` 的提示窗堆疊持有 `DimeNotifyWindow` = `CRustNotifyWindow`（`src/RustNotifyWindow.*`，不是視窗），C++ 建置是 `CNotifyWindow`；`DIME.NotifyWindow` 類別在 Rust 建置下由 Rust 註冊，測試中的 C++ `CNotifyWindow` 改用 `g_cppNotifyWindowAtomForTest`（同候選字窗）。對拍 `NotifyParityTest`（C++ `CNotifyWindow` 對 `CRustNotifyWindow`），`UIPresenterIntegrationTest::IT06_04_NotifyWindowIsReal` 確認 `UIPresenter` 建立的是真實視窗。Rust 視窗沒有「虛擬視窗」（atom 為 0 時 C++ 只記可見旗標），檢查可見性的測試要先註冊類別。改用 Rust 物件並排對拍時，以植入 Rust 端錯誤確認對拍會失敗。需要真實視窗的測試要呼叫 `tests/WindowClassTestHelper.h` 的 `EnsureWindowClassesRegistered()`，不要直接呼叫 `Global::RegisterWindowClass()`（重複註冊會把 atom 設為 0，視窗變成沒有 HWND 的虛擬視窗）；收尾用 `DestroyAndDelete()`（先 `_Destroy()` 再 `delete`，和 `UIPresenter` 相同），不要只 `delete`，否則 HWND、可見視窗與計時器會留到後面會處理訊息的 KeyFlow 測試。`dime-ui` 依賴官方 `windows` crate（workspace 唯一的外部相依，關閉預設功能，只開需要的 `Win32_*` 功能；第一次建置要能從 crates.io 下載）。
- `crates/dime-ffi`：C ABI（標頭 `include/dime_ffi.h`），編成 `dime_ffi.lib`。所有匯出函式都以 `catch_unwind` 包住，panic 不得跨出邊界。
- `src/tests/RustParityTest.cpp`：把同一份輸入同時餵給 C++ 與 Rust 引擎並逐筆比對（`installer/*.cin` 全部碼表 + 合成的 CIN/TTS 案例）。`DIMETests.vcxproj` 會在 Win32/x64 建置前自動執行 `cargo build --release -p dime-ffi --target <i686|x86_64>-pc-windows-msvc`；ARM64/ARM64EC 不編譯這些測試。`RustConfigParityTest.cpp` 對拍設定：所有 INI key × 邊界值、各模式預設值、`WriteConfig` 輸出位元組、`LoadConfigFromFile`。`RustEngineAdapterTest.cpp` 則透過 C++ 介面比對 `CTableDictionaryEngine` 與 `CRustTableDictionaryEngine`（含 INI 對 `CConfig` 的副作用、檔案重新載入）。
- 本機建置注意：若環境變數 `NoDefaultCurrentDirectoryInExePath=1`，`DIME.vcxproj` 的 `BuildInfo.cmd` 預建置步驟會失敗；`DIME.vcxproj` 需要 Spectre 版 MSVC 程式庫（本機沒有時可加 `/p:SpectreMitigation=false` 僅供驗證）。

### 整句輸入（`SentenceMode`，目前僅大易）

- 設定 `SentenceMode=1`（INI / CLI / 設定視窗「整句輸入」）後，空白鍵或結束鍵不再跳出候選字窗，而是把該字（連同該字碼的全部候選字）加入組字區的「整句」，由 `rust/crates/dime-core/src/sentence.rs` 的 Viterbi 依字頻（`TCFreq.cin`）與詞庫（`Array-Phrase.cin` 的詞，不用其字碼）重選整句；Enter／空白送出，←→ 移動、↓ 改選游標所在字（選定即釘住），Backspace／Delete 刪字，Esc 放棄。標點、地址鍵、Shift 英數等直接輸出的鍵會先送出整句。
- TSF 端在 `SentenceHandler.cpp`（`CDIME::_Sentence*`、`CKeyStateSentence`），資料結構在 `SentenceBuffer.*`（不依賴 TSF，`tests/SentenceBufferTest.cpp` 有單元測試與實際大易碼表的整句測試）。攔截點：`_IsKeyEaten`、`_HandleCompositionConvert`、`_HandleCandidateWorker`、`_HandleCancel`、`_HandleCompositionBackspace`、`_TerminateComposition`、`OnCompositionTerminated`。
- 學習：送出整句時，使用者手動選定（釘住）的字會被 `UserModel`（Rust）記住「該字碼偏好的字」與「它和前後字組成的雙字詞」，之後轉換加分（同一字碼要選約 3 次才會在沒有上下文時取代第一候選字；詞在上下文中一次就生效）。存在 `%APPDATA%\DIME\SentenceUser.txt`（UTF-16，刪除即重置）；多個行程共用此檔，存檔時重新讀檔再加上本行程新增的部分（`_pSentencePending`），不會互相覆蓋。Store app／低完整性行程不寫檔，只留在記憶體。
- 沒有 Rust（`DIME_RUST_AVAILABLE` 未定義）時仍可用，但只會取第一候選字（沒有上下文判斷、也不學習）。

```powershell
cargo test --manifest-path rust/Cargo.toml                 # Rust 單元測試
cargo clippy --manifest-path rust/Cargo.toml --all-targets
vstest.console.exe src\bin\x64\Debug\DIMETests.dll /Tests:RustParityTest   # C++ ↔ Rust 對拍
```

## 安裝程式與發版

- 現行安裝程式為 WiX 4（`installer/wix/`，Burn bundle `DIME-Universal.exe` 內含 `DIME-64bit.msi` / `DIME-32bit.msi`，custom action 在 `DIMEInstallerCA/`）；以 `installer\deploy-wix-installer.ps1` 建置（參數：`-ProductVersionOverride`、`-NonInteractive`、`-SkipChecksumUpdate`、`-OutputDir`）。NSIS（`DIME-Universal.nsi`、`deploy-installer.ps1`）為舊版。
- `installer/` 中的 MSI/ZIP 與 `README.md` 裡的版本、SHA-256 checksum 區塊（`<!-- ... _START -->` / `_END` 標記之間）由 CI bot 自動更新（`build_installer.yml`、`new_release.yml`），不要手動修改這些區塊或二進位檔。發版說明取自 `docs/DRAFT_RELEASE.md`。
- 隨安裝程式出貨的 `installer/*.cin` 必須是 **UTF-16 LE**；使用者透過 CLI/設定載入的 `.cin` 則是 UTF-8（README「自訂碼表格式」一節有完整格式說明）。`Tables/` 是碼表原始檔/歷史版本。
- 執行期不要直接修改 `%APPDATA%\DIME\` 下的檔案；設定與碼表變更一律透過 `DIMESettings.exe` CLI。
