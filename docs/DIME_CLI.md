# DIME CLI Interface Plan

## Context

`DIMESettings.exe` currently operates exclusively as a GUI application. The `lpCmdLine` parameter in `wWinMain` is explicitly ignored (`UNREFERENCED_PARAMETER`). The goal is to add a headless CLI mode so that every setting and every action available in the IMESettings property pages can also be performed from the command line — enabling AI Agents (and scripts) to configure DIME without a display or user interaction.

---

## Design Principles

- **Additive, not breaking.** When invoked with no arguments, `DIMESettings.exe` behaves exactly as today (shows the GUI launcher).
- **Headless.** When CLI arguments are detected, the process runs without any window, then exits with a status code.
- **Machine-readable output.** Every read/query command outputs plain `key=value` lines (or JSON with `--json`) to stdout for easy AI consumption.
- **One INI file per IME mode.** The `--mode` flag is required for all read/write operations because each mode has its own config file (`DayiConfig.ini`, `ArrayConfig.ini`, `PhoneConfig.ini`, `GenericConfig.ini`).

---

## Command Syntax

```
DIMESettings.exe [--mode <mode>] <command> [options]
```

### `--mode` values

| Flag value | IME mode |
|-----------|----------|
| `dayi`    | 大易 (`IME_MODE_DAYI`) |
| `array`   | 行列 (`IME_MODE_ARRAY`) |
| `phonetic`| 注音 (`IME_MODE_PHONETIC`) |
| `generic` | 自建 (`IME_MODE_GENERIC`) |

`--mode` is **required** for all commands that read or write a config file.

---

## Commands

### `--help` / `-h`

Print usage help and exit. No `--mode` needed.

```
DIMESettings.exe --help
```

---

### `--get <key>`
Print the current value of one setting key.

```
DIMESettings.exe --mode dayi --get FontSize
FontSize=12
```

Exit 0 on success, exit 2 if key is unknown.

---

### `--get-all`
Print every setting for the selected mode, one `key=value` per line (or JSON with `--json`).

```
DIMESettings.exe --mode array --get-all
AutoCompose=1
SpaceAsPageDown=0
...
```

With `--json`:

```json
{
  "mode": "array",
  "AutoCompose": 1,
  "SpaceAsPageDown": 0,
  ...
}
```

---

### `--set <key> <value>`
Write one setting and save the config file.

```
DIMESettings.exe --mode phonetic --set FontSize 14
```

Multiple `--set` pairs may appear in one invocation:

```
DIMESettings.exe --mode array --set ColorMode 2 --set FontSize 16 --set FontItalic 0
```

Exit 0 on success, exit 1 on invalid key/value, exit 2 on write failure.

---

### `--reset`
Restore all settings for the selected mode to their built-in defaults (mirrors the "恢復預設設定" button).

```
DIMESettings.exe --mode dayi --reset
```

---

### `--import-custom <file>`
Replace the custom phrase table with the contents of `<file>` (mirrors "匯入自訂詞庫").

```
DIMESettings.exe --mode array --import-custom C:\phrases\my_phrases.txt
```

---

### `--export-custom <file>`
Write the current custom phrase table to `<file>` (mirrors "匯出自訂詞庫").

```
DIMESettings.exe --mode array --export-custom C:\backup\array_phrases.txt
```

---

### `--load-main <file>` / `--load-phrase <file>`
Replace the main or phrase dictionary with the given file (Generic mode and modes with `_loadTableMode`).

```
DIMESettings.exe --mode generic --load-main C:\tables\custom.cin
DIMESettings.exe --mode generic --load-phrase C:\tables\phrase.cin
```

---

### `--load-array <table-name>`
Load one of the built-in Array supplementary tables. `table-name` values:

| Name | Description |
|------|-------------|
| `sp`  | Array Special Code (特別碼) |
| `sc`  | Array Short Code (簡碼) |
| `ext-b` | Array Extension B |
| `ext-cd` | Array Extensions C+D |
| `ext-efg` | Array Extensions E+F+G |
| `array40` | Array 40 |
| `phrase` | Array Phrase table |

```
DIMESettings.exe --mode array --load-array ext-b
```

---

### `--list-modes`
Print the four available IME modes. No `--mode` needed.

```
DIMESettings.exe --list-modes
dayi
array
phonetic
generic
```

---

### `--list-keys`
Print all valid key names for the selected mode, with their current values and accepted value ranges.

```
DIMESettings.exe --mode phonetic --list-keys
```

---

### `--json`
Optional flag on any read command (`--get`, `--get-all`, `--list-keys`). Output is a single JSON object.

---

### `--silent`
Suppress all stdout/stderr output. Useful in scripts that only care about the exit code.

---

## Complete Key Reference

### Boolean keys (0 = off, 1 = on) — all modes unless noted

| Key | 設定介面 | Default | Modes |
|-----|---------|---------|-------|
| `AutoCompose` | 打字時同步組字 | 0 (1 for Array) | all |
| `SpaceAsPageDown` | 以空白鍵換頁 | 1 | all |
| `SpaceAsFirstCandSelkey` | 空白鍵為第一選字鍵 | 0 | generic |
| `ArrowKeySWPages` | 以方向鍵換頁 | 0 | all |
| `ClearOnBeep` | 錯誤組字時清除字根 | 1 | all |
| `DoBeep` | 錯誤組字嗶聲提示 | 1 | all |
| `DoBeepNotify` | 錯誤組字提示窗開啟 | 1 | all |
| `DoBeepOnCandi` | 有候選字時嗶聲提示 | 0 | dayi |
| `ActivatedKeyboardMode` | 預設輸入模式 (中文=1, 英數=0) | 1 | all |
| `MakePhrase` | 提示聯想字詞 | 1 | all |
| `ShowNotifyDesktop` | 在桌面模式顯示浮動中英切換視窗 | 1 | all |
| `DoHanConvert` | 輸出字元 (繁體=0, 簡體=1) | 0 | all |
| `FontItalic` | 字型斜體 | 0 | all |
| `CustomTablePriority` | 自建詞優先 | 0 | all |
| `DayiArticleMode` | 地址鍵輸入符號 | 0 | dayi |
| `ArrayForceSP` | 僅接受輸入特別碼 | 0 | array |
| `ArrayNotifySP` | 特別碼提示 | 0 | array |
| `ArraySingleQuoteCustomPhrase` | 以'鍵查詢自建詞庫 | 0 | array |
| `Big5Filter` | 字集查詢範圍：繁體中文 | 0 | dayi, phonetic, generic |

### Integer / enum keys

| Key | 設定介面 | Default | Accepted values |
|-----|---------|---------|----------------|
| `MaxCodes` | 組字區最大長度 | 4 (Array: 5) | 1–20 |
| `FontSize` | 字型大小 | 12 | 8–72 |
| `FontWeight` | 字型粗細 | 400 | 100–900 (400=normal, 700=bold) |
| `IMEShiftMode` | 中英切換熱鍵 | 0 | 0=左右Shift, 1=右Shift, 2=左Shift, 3=僅Ctrl+Space |
| `DoubleSingleByteMode` | 全半形輸入模式 | 1 | 0=以Shift-Space切換, 1=半形, 2=全形 |
| `ColorMode` | 色彩模式 | 0 | 0=跟隨系統, 1=淺色模式, 2=深色模式, 3=自訂 |
| `NumericPad` | 九宮格數字鍵盤 | 0 | 0=數字輸入, 1=組字, 2=僅組字 |
| `ArrayScope` | 字集查詢範圍 (行列) | 0 | 0=行列30 Ext-A, 1=Ext-AB, 2=Ext-ABCD, 3=Ext-ABCDE, 4=行列40 Big5, 5=行列30 Big5 |
| `PhoneticKeyboardLayout` | 鍵盤對應選擇 | 0 | 0=標準注音鍵盤, 1=許氏鍵盤 |

### String keys

| Key | 設定介面 | Default | Notes |
|-----|---------|---------|-------|
| `FontFaceName` | 字型 | `微軟正黑體` | Up to 32 chars |
| `ReverseConversionCLSID` | 反查輸入字根 (CLSID) | empty | `{XXXXXXXX-...}` format |
| `ReverseConversionGUIDProfile` | 反查輸入字根 (GUIDProfile) | empty | `{XXXXXXXX-...}` format |
| `ReverseConversionDescription` | 反查輸入字根 (Description) | empty | Free text |

### Color keys (hex, `0xRRGGBB`)

Each color palette (自訂/淺色/深色) has six color roles:

| Color role | 設定介面 | Custom key | Light key | Dark key |
|-----------|---------|------------|-----------|----------|
| 一般字型字體 | 一般字型字體 | `ItemColor` | `LightItemColor` | `DarkItemColor` |
| 選取字型字體 | 選取字型字體 | `SelectedItemColor` | `LightSelectedItemColor` | `DarkSelectedItemColor` |
| 一般字型背景 | 一般字型背景 | `ItemBGColor` | `LightItemBGColor` | `DarkItemBGColor` |
| 聯想字詞字體 | 聯想字詞字體 | `PhraseColor` | `LightPhraseColor` | `DarkPhraseColor` |
| 標號字型字體 | 標號字型字體 | `NumberColor` | `LightNumberColor` | `DarkNumberColor` |
| 選取字型背景 | 選取字型背景 | `SelectedBGItemColor` | `LightSelectedBGItemColor` | `DarkSelectedBGItemColor` |

- Custom-theme colors (自訂色彩) are used when `ColorMode=3`
- Light-theme colors (淺色模式) are used when `ColorMode=1`
- Dark-theme colors (深色模式) are used when `ColorMode=2`
- When `ColorMode=0` (跟隨系統), DIME auto-switches between light and dark palettes based on Windows settings

Example:
```
DIMESettings.exe --mode array --set ColorMode 3 --set ItemColor 0xFFFFFF --set ItemBGColor 0x1E1E1E
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid argument or unknown key/value |
| 2 | I/O error (config file read/write failed) |
| 3 | Key not applicable to the selected mode |

---

## AI Agent Usage Examples

```bash
# Query current settings
DIMESettings.exe --mode array --get-all --json

# Enable dark theme for Array
DIMESettings.exe --mode array --set ColorMode 2

# Set a custom font
DIMESettings.exe --mode dayi --set FontFaceName "Noto Sans TC" --set FontSize 14

# Turn on Big5 filter for Phonetic
DIMESettings.exe --mode phonetic --set Big5Filter 1

# Reset Dayi to defaults
DIMESettings.exe --mode dayi --reset

# Export custom phrases for backup
DIMESettings.exe --mode array --export-custom C:\backup\array_custom.txt

# Load supplementary Array tables
DIMESettings.exe --mode array --load-array ext-b

# Silence output, check only exit code
DIMESettings.exe --mode generic --set AutoCompose 1 --silent
echo %ERRORLEVEL%
```

