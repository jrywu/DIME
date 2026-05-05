
## 修正與改進 (v1.3.616 之後)

本次更新主要修正 v1.3.616 之後回報的候選字、提示視窗、數字鍵盤與自建詞庫匯入問題，並補強設定工具與自動化測試。

### 新功能與改進

- **反查與特別碼提示可同時顯示**：行列輸入法在同一次候選字確認後，若同時有「反查輸入字根」與「特別碼」提示，現在會以兩個提示視窗並列堆疊顯示，不會互相覆蓋。
- **自建詞庫匯入流程強化**：設定工具匯入自建詞庫時支援 UTF-16、UTF-8 與系統 ANSI/Big5 文字檔，並改用非 regex 的格式檢查，避免大型檔案造成 UI 卡住。
- **自建詞庫編輯器大型檔案支援**：提高 RichEdit 文字上限，加入 viewport-only 即時標示與儲存前完整驗證；若內容有錯誤，會跳到第一個錯誤位置，方便修正。
- **命令列匯入行為一致化**：`DIMESettings.exe --import-custom` 改用與 GUI 相同的讀檔、驗證與輸出流程，並加入大型自建詞庫匯入驗證測試。

### 修正

- **修正聯想詞候選清單消失問題 (#125)**：修正 v1.3.616 中提交候選字後，聯想詞清單只閃一下就消失或定位到螢幕左上角的問題。聯想詞現在會穩定顯示在剛輸入文字附近。
- **修正數字鍵盤造成主程式閃退 (#126)**：修正中文輸入並顯示聯想詞後，立即按右側數字鍵盤數字可能造成 Notepad++、VS Code、Word 等宿主程式崩潰的問題；同時保留數字鍵盤在候選視窗中選字、無候選視窗時輸入數字的混合模式。
- **修正反查與特別碼提示位置漂移 (#127)**：修正關閉浮動中英切換視窗時，反查輸入字根、特別碼與其他提示會出現在遠離游標位置的問題；提示視窗現在不再依賴「浮動中英切換視窗」設定來取得游標位置。
- **修正 Firefox / Gmail / 搜尋列中的提示位置問題 (#127)**：修正 Firefox 中提示視窗可能跑到頁面左上角、瀏覽器視窗右側、前一個輸入欄位的 Y 位置，或在候選視窗翻到上方時與組字列重疊的問題。
- **修正自建詞庫大型檔案匯入卡住 (#130)**：修正匯入數萬行自建詞庫時設定視窗無限轉圈或停止回應的問題；Big5/CP950 檔案也不再因錯誤的讀檔路徑而被靜默截斷。
- **修正設定頁面子控制項浮在標題上方**：修正捲動新版設定頁面時，ComboBox、Edit、RichEdit 等子控制項會浮在固定標題文字上方的問題。
- **修正 Big5 範圍測試判斷**：調整 Big5 範圍測試，正確計入 CP950-only 行，避免測試誤判。
- **修正行列 Ext-EF 碼表描述文字錯字**。

### 測試與文件

- 新增 CLI 自建詞庫匯入整合測試、RichEdit viewport 驗證測試、設定控制器測試，並補強提示視窗與候選視窗相關整合測試。
- 相關說明與設計文件：
	- [#125 聯想詞候選清單消失](https://github.com/jrywu/DIME/blob/master/docs/%23125_ISSUE.md)
	- [#126 數字鍵盤造成主程式閃退](https://github.com/jrywu/DIME/blob/master/docs/%23126_ISSUE.md)
	- [#127 反查與特別碼提示位置漂移](https://github.com/jrywu/DIME/blob/master/docs/%23127_ISSUE.md)
	- [#130 自建詞庫大型檔案匯入卡住](https://github.com/jrywu/DIME/blob/master/docs/%23130_ISSUE.md)
	- [Big5 字集過濾](https://github.com/jrywu/DIME/blob/master/docs/BIG5_FILTER.md)
	- [Caret tracking](https://github.com/jrywu/DIME/blob/master/docs/CARET_TRACKING.md)
	- [Caret tracking probe](https://github.com/jrywu/DIME/blob/master/docs/CARET_TRACKING_PROBE.md)
	- [自建詞庫驗證](https://github.com/jrywu/DIME/blob/master/docs/CUSTOM_TABLE_VALIDATION.md)
	- [設定頁面子控制項浮在標題上方](https://github.com/jrywu/DIME/blob/master/docs/DIME_SETTINGS_COMBO_FLOATING.md)
	- [多提示視窗](https://github.com/jrywu/DIME/blob/master/docs/MULTI_NOTIFY.md)
	- [Reverse conversion refactor](https://github.com/jrywu/DIME/blob/master/docs/REVERSE_CONV_REFACTOR.md)
	- [RichEdit viewport validation](https://github.com/jrywu/DIME/blob/master/docs/RICHEDIT_VIEWPORT_VAL.md)
	- [測試計畫](https://github.com/jrywu/DIME/blob/master/docs/TEST_PLAN.md)
	- [測試報告](https://github.com/jrywu/DIME/blob/master/docs/TEST_REPORT.md)


