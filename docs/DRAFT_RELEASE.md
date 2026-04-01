
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

## 修正與改進 (v1.3.532 之後)

### 新功能

- **繁簡轉換快捷鍵**：新增 Ctrl+Shift+\ 快捷鍵進行繁簡轉換（[#92](https://github.com/jrywu/DIME/issues/92)）
- **Shift 英數全形輸出**：全形模式下 Shift 英數輸入及萬用字元（`?`、`*`）皆可輸出全形字元（[#119](https://github.com/jrywu/DIME/issues/119)）
- **重新設計候選字視窗捲軸**：新增完整捲軸及細線滑桿兩種樣式，支援 Windows 11 視覺風格 (詳見[README — 全新設計選字視窗與提示視窗](https://github.com/jrywu/DIME/blob/master/README.md#%E5%85%A8%E6%96%B0%E8%A8%AD%E8%A8%88%E9%81%B8%E5%AD%97%E8%A6%96%E7%AA%97%E8%88%87%E6%8F%90%E7%A4%BA%E8%A6%96%E7%AA%97-v13553))
- **Per-Monitor DPI 支援**：候選字視窗支援多螢幕不同 DPI 縮放，確保跨螢幕顯示一致
- **自適應陰影色彩**：候選字視窗陰影自動偵測背景明暗，在淺色背景顯示深色陰影、深色背景顯示淺色陰影，確保任何背景下皆清晰可見

### 修正

- **修正 Windows 10 下 Shift 英數模式殘留問題**：切換視窗後 `_isShiftedEnglish` 狀態未正確重置（[#115](https://github.com/jrywu/DIME/issues/115)）
- **修正 Shift 英數模式下萬用字元未正確處理**：組字中按 Shift+英文鍵時，待處理萬用字元未被消化（[#115](https://github.com/jrywu/DIME/issues/115)）
- **修正非字根鍵在組字時未被攔截**：組字中按下非字根的 Shift 組合鍵時，按鍵事件有時會洩漏至應用程式（[#116](https://github.com/jrywu/DIME/issues/116)）
- **修正聯想詞候選字與 Shift 英數模式衝突**：聯想詞候選字顯示時，Shift+字母未正確由 Shift 英數模式處理
- **修正舊版 Win32 應用程式捲軸背景顯示為洋紅色**：捲軸背景改為使用候選字視窗背景色，選取列亦正確延伸至捲軸區域（[#120](https://github.com/jrywu/DIME/issues/120) [#121](https://github.com/jrywu/DIME/issues/121)）
- **修正 Windows 11 檔案總管搜尋框候選字視窗跳位**：SearchHost.exe 回傳異常座標 `{0,0,1,1}` 時，候選字視窗不再跳至螢幕左上角，改為保持上一次有效位置
- **移除聯想詞空白字元暫留**：聯想詞候選字視窗不再需要插入空白字元至組字緩衝區作為定位錨點，改用上次儲存的組字範圍 `_rectCompRange` 回退定位
- **修正候選字視窗翻轉至上方時位置偏低**：`WM_CREATE` 未設定 `_padding`（底部留白），導致初始視窗高度比實際少 12px，翻轉定位後第一次繪製觸發重新調整大小，候選字底部越過組字範圍
- **候選字視窗與組字範圍增加陰影間距**：候選字視窗與組字文字之間加入 `SHADOW_SPREAD / 2`（7px）間距，避免陰影與組字文字重疊

