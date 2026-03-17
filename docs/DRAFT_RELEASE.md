
DIME v1.3 是自 v1.2.441 以來的首個主要版本更新，包含全新安裝程式、多項新功能、安全強化及眾多修正。

---

## 全新安裝程式

- **全新 MSI 安裝程式**：除原有的通用安裝檔（`DIME-Universal.exe`）外，另提供輕量化的平台專屬 MSI 安裝檔（`DIME-64bit.msi` 適用於 x64/ARM64；`DIME-32bit.msi` 適用於 x86），檔案更小、並可直接使用 Windows 標準套件管理工具部署（詳見 [MSI_INSTALLER.md](https://github.com/jrywu/DIME/blob/master/docs/MSI_INSTALLER.md)）
- **安裝、升級、降級及移除全程無需重開機**：即使輸入法正在執行中亦然，採用不中斷服務的檔案替換技術
- **舊版安裝程式無縫升級**：從舊版 NSIS 安裝程式升級時，新版安裝程式會自動移除舊版，無需使用者手動操作
- **解決防毒軟體誤報問題**：舊版 NSIS 安裝程式經常被 Windows Defender 誤報為木馬程式，新版 MSI 安裝程式不再有此問題

## 新功能

- **Shift 英數輸入模式**：中文輸入模式下，按住 Shift 可直接輸入英數字元，無需切換至英文模式。支援 CapsLock 控制輸出字元大小寫及符號切換（詳見 [README — Shift 英數輸入模式](https://github.com/jrywu/DIME/blob/master/README.md#shift-英數輸入模式-v13)）
- **繁體中文（Big5）字集過濾**：新增「字集查詢範圍」設定，可過濾簡體中文及罕用字元，僅顯示 Big5 範圍內的繁體中文候選字，減少選字數量（詳見 [BIG5_FILTER.md](https://github.com/jrywu/DIME/blob/master/docs/BIG5_FILTER.md)）
- **淺色／深色模式支援**：候選字視窗支援淺色模式、深色模式及自訂三套獨立色彩組合，可設定「跟隨系統」自動切換（Windows 10 1809+）（詳見 [LIGHT_DARK_THEME.md](https://github.com/jrywu/DIME/blob/master/docs/LIGHT_DARK_THEME.md)）
- **自建詞庫即時格式檢查**：自訂詞庫編輯器內建即時驗證機制，輸入時自動以顏色標記錯誤行（三級：格式錯誤、輸入碼過長、無效字碼），支援深色／淺色主題自動配色（詳見 [CUSTOM_TABLE_VALIDATION.md](https://github.com/jrywu/DIME/blob/master/docs/CUSTOM_TABLE_VALIDATION.md)）
- **候選字頁面指示與循環翻頁**：候選字視窗顯示頁面指示，翻頁時首頁與末頁循環切換
- **AI Agent 友善的 CLI 命令列介面**：`DIMESettings.exe` 新增 headless CLI 模式，AI Agent 及自動化腳本可以命令列方式查詢、設定及管理輸入法，無需 GUI 互動（詳見 [DIME_CLI.md](https://github.com/jrywu/DIME/blob/master/docs/DIME_CLI.md) 及 [README — For AI Agents](https://github.com/jrywu/DIME/blob/master/README.md#for-ai-agents)）

## 修正

- **萬用字元與注音自建詞組**：修正萬用字元搜尋及注音自建詞組無法同時運作的問題 (#101, #99)（詳見 [README — 萬用字元](https://github.com/jrywu/DIME/blob/master/README.md#萬用字元)）
- **長候選字文字亂碼**：修正候選字文字過長時出現亂碼的問題
- **CIN 碼表空白支援**：改善 CIN 碼表解析，支援值中包含空白字元 (#102)
- **頁面緩衝區計算**：修正候選字頁面緩衝區大小計算錯誤
- **組字緩衝區溢位**：使用 escape-mode 最大長度計算按鍵緩衝區大小，避免潛在的緩衝區溢位問題
- **版本資訊一致性**：修正 DIME.dll FileVersion/ProductVersion 不一致問題，安裝程式正確顯示完整版本號

## 安全強化

- **啟用 Spectre、CFG 及 SDL 安全緩解措施**：編譯時啟用 Spectre Mitigation、Control Flow Guard 及 Security Development Lifecycle 檢查



