# DIME 編譯指南 / Build Guide

本文件說明如何從原始碼編譯 DIME 輸入法。

This document describes how to build DIME IME from source code.

## 系統需求 / System Requirements

### 必要軟體 / Required Software

- **Windows 10/11** (建議使用 Windows 10 1809 或更新版本 / Recommended Windows 10 1809 or later)
- **Visual Studio 2019 或更新版本** / Visual Studio 2019 or later
  - 需安裝「使用 C++ 的桌面開發」Workload / Workload: "Desktop development with C++"
  - 需安裝 Windows SDK (建議 10.0.19041.0 或更新版本) / Windows SDK (Recommended 10.0.19041.0 or later)
  - 需安裝 ARM64 建置工具 (若要編譯 ARM64 版本) / ARM64 build tools (for ARM64 build)
- **Git** (用於版本控制與自動版號生成 / For version control and auto versioning)
- **PowerShell 5.0 或更新版本** (用於自動產生安裝包 / For automated installer packaging)
- **WiX 4 CLI** (v1.3+ MSI 安裝程式所需 / Required for v1.3+ MSI installer)
  - 安裝方式 / Install: `dotnet tool install --global wix`
  - 需安裝擴充套件 / Required extensions: `WixToolset.UI.wixext`, `WixToolset.Util.wixext`, `WixToolset.BootstrapperApplications.wixext`
- **NSIS (Nullsoft Scriptable Install System)** (僅舊版安裝程式需要 / Only required for legacy installer)
  - 必須加入系統 PATH / Must be added to system PATH
  - 下載位置 / Download: [https://nsis.sourceforge.io/](https://nsis.sourceforge.io/)


## 編譯步驟 / Build Steps

### 1. 取得原始碼 / Get Source Code

#### 方法 A：使用 Git 複製 / Clone with Git

```bash
git clone https://github.com/jrywu/DIME.git
cd DIME
```

#### 方法 B：下載 ZIP 檔案 / Download ZIP

從 [GitHub](https://github.com/jrywu/DIME) 下載原始碼 ZIP 檔案並解壓縮。

Download the source code ZIP file from [GitHub](https://github.com/jrywu/DIME) and extract it.

### 2. 開啟專案 / Open Project

1. 啟動 Visual Studio
2. 開啟 `DIME.sln` 方案檔

### 3. 選擇建置組態 / Select Build Configuration

DIME 支援以下建置組態：

DIME supports the following build configurations:

| 組態 Configuration | 平台 Platform | 說明 Description |
|-------------------|--------------|-----------------|
| Debug | Win32 | 32 位元除錯版本 / 32-bit debug build |
| Debug | x64 | 64 位元除錯版本 / 64-bit debug build |
| Debug | ARM64 | ARM64 除錯版本 / ARM64 debug build |
| Debug | ARM64EC | ARM64EC 除錯版本 / ARM64EC debug build |
| Release | Win32 | 32 位元正式版本 / 32-bit release build |
| Release | x64 | 64 位元正式版本 / 64-bit release build |
| Release | ARM64 | ARM64 正式版本 / ARM64 release build |
| Release | ARM64EC | ARM64EC 正式版本 / ARM64EC release build |

**建議：** 日常開發使用 Debug 組態，正式發布使用 Release 組態。

**Recommendation:** Use Debug configuration for daily development and Release configuration for official releases.

### 4. 編譯專案 / Build Project

#### 編譯單一平台 / Build Single Platform

1. 在工具列選擇組態（Debug/Release）和平台（Win32/x64/ARM64/ARM64EC）
2. 選擇 **建置 → 建置方案** (Ctrl+Shift+B)

或使用命令列：

Or use command line:

```bash
# 建置 x64 Release 版本
msbuild DIME.sln /p:Configuration=Release /p:Platform=x64

# 建置 Win32 Release 版本
msbuild DIME.sln /p:Configuration=Release /p:Platform=Win32

# 建置 ARM64EC Release 版本
msbuild DIME.sln /p:Configuration=Release /p:Platform=ARM64EC
```

#### 編譯所有平台 / Build All Platforms

**建置安裝程式所需檔案：**

Building the installer requires at least the following Release builds:
- **DIME 專案 (DIME project):**
  - Release Win32 → `DIME.dll`
  - Release x64 → `DIME.dll`
  - Release ARM64EC → `DIME.dll`
- **DIMESettings 專案 (DIMESettings project):**
  - Release Win32 → `DIMESettings.exe` (僅支援 Win32 / Win32 only)

使用批次建置功能：

Use Batch Build feature:

1. 選擇 **建置 → 批次建置** (Build → Batch Build)
2. 勾選以下組態 / Select the following configurations:
   - **DIME** - Release Win32
   - **DIME** - Release x64
   - **DIME** - Release ARM64EC
   - **DIMESettings** - Release Win32
3. 點選 **建置** (Build)

**說明：** DIME.sln 方案包含兩個專案：**DIME** 專案建置輸入法核心 `DIME.dll`（支援多平台），**DIMESettings** 專案建置設定程式 `DIMESettings.exe`（僅支援 Win32）。

**Note:** The DIME.sln solution contains two projects: **DIME** project builds the IME core `DIME.dll` (multi-platform support), and **DIMESettings** project builds the settings program `DIMESettings.exe` (Win32 only).

或使用命令列編譯所有 Release 組態：

Or build all required Release configurations via command line:

```bash
# 建置 DIME 專案的所有平台 / Build DIME project for all platforms
msbuild DIME.sln /p:Configuration=Release /p:Platform=Win32 /t:DIME
msbuild DIME.sln /p:Configuration=Release /p:Platform=x64 /t:DIME
msbuild DIME.sln /p:Configuration=Release /p:Platform=ARM64EC /t:DIME

# 建置 DIMESettings 專案 (僅 Win32) / Build DIMESettings project (Win32 only)
msbuild DIME.sln /p:Configuration=Release /p:Platform=Win32 /t:DIMESettings
```

### 5. 建置輸出 / Build Output

編譯完成後，輸出檔案位於：

After building, output files are located at:

```
DIME/
├── src/
│   └── Release/
│       ├── Win32/
│       │   ├── DIME.dll          # DIME 專案：32 位元輸入法核心
│       │   └── DIMESettings.exe  # DIMESettings 專案：設定程式 (所有平台共用)
│       ├── x64/
│       │   └── DIME.dll          # DIME 專案：64 位元輸入法核心
│       └── ARM64EC/
│           └── DIME.dll          # DIME 專案：ARM64EC 輸入法核心
```

**注意：** DIME.sln 包含兩個專案。**DIME** 專案建置 `DIME.dll` (支援 Win32/x64/ARM64EC)，**DIMESettings** 專案建置 `DIMESettings.exe` (僅支援 Win32，供所有平台共用)。

**Note:** DIME.sln contains two projects. **DIME** project builds `DIME.dll` (supports Win32/x64/ARM64EC), and **DIMESettings** project builds `DIMESettings.exe` (Win32 only, shared across all platforms).

## 編譯安裝程式 / Build Installer

DIME 有兩套安裝程式系統：

DIME has two installer systems:

| 安裝程式 Installer | 版本 Version | 技術 Technology | 自動化腳本 Script |
| --- | --- | --- | --- |
| WiX 4 MSI (建議 / Recommended) | v1.3+ | WiX 4 / Burn bundle | `installer\deploy-wix-installer.ps1` |
| NSIS (舊版 / Legacy) | v1.2 及更早 | NSIS | `installer\deploy-installer.ps1` |

**v1.3 起建議使用 WiX MSI 安裝程式。** NSIS 安裝程式仍可編譯，但不再用於正式發布。

**WiX MSI installer is recommended starting from v1.3.** The NSIS installer can still be built but is no longer used for official releases.

### 準備安裝程式檔案 / Prepare Installer Files

確保所有必要的字典檔案已放置在 `installer/` 目錄：

Ensure all required dictionary files are placed in the `installer/` directory:

```
installer/
├── Array.cin              # 行列 30 主碼表
├── Array40.cin            # 行列 40 主碼表
├── Array-Ext-B.cin        # 行列 CJK Ext-B
├── Array-Ext-CD.cin       # 行列 CJK Ext-CD
├── Array-Ext-EF.cin       # 行列 CJK Ext-EF
├── Array-Phrase.cin       # 行列詞庫
├── Array-shortcode.cin    # 行列簡碼
├── Array-special.cin      # 行列特別碼
├── phone.cin              # 注音碼表
├── TCFreq.cin             # 常用字頻率表
└── TCSC.cin               # 繁簡轉換表
```

**重要：** 所有 `.cin` 字典檔案必須使用 **UTF-16 LE (Little Endian)** 編碼格式。

**Important:** All `.cin` dictionary files must use **UTF-16 LE (Little Endian)** encoding.

---

### WiX MSI 安裝程式 (v1.3+) / WiX MSI Installer (v1.3+)

WiX 安裝程式產生一個 Burn bundle (`DIME-Universal.exe`)，內含兩個 MSI 套件：

The WiX installer produces a Burn bundle (`DIME-Universal.exe`) containing two MSI packages:

- `DIME-64bit.msi` — 64 位元 Windows (AMD64 + ARM64)
- `DIME-32bit.msi` — 32 位元 Windows

#### 額外需求 / Additional Requirements

- **WiX 4 CLI** — 透過 .NET tool 安裝 / Install via .NET tool:

  ```powershell
  dotnet tool install --global wix
  ```

- **WiX Extensions** — 需要以下擴充套件 / Required extensions:

  ```powershell
  wix extension add WixToolset.UI.wixext
  wix extension add WixToolset.Util.wixext
  wix extension add WixToolset.BootstrapperApplications.wixext
  ```
- **Custom Action DLL** — `installer\wix\DIMEInstallerCA\DIMEInstallerCA.vcxproj` 須先編譯（腳本會自動處理）/ Must be built first (the script handles this automatically)

#### 自動建置 / Automated Build

```powershell
cd installer
.\deploy-wix-installer.ps1
```

此腳本會自動執行以下步驟：

This script automatically performs the following steps:

1. 複製建置產出物（DLL、EXE）到安裝程式目錄 / Copies build artifacts (DLLs, EXE) to the installer directory
2. 編譯 Custom Action DLL (`DIMEInstallerCA.dll`) 的 x64 與 Win32 版本 / Builds Custom Action DLL (`DIMEInstallerCA.dll`) for x64 and Win32
3. 編譯 WiX MSI (`DIME-64bit.msi`、`DIME-32bit.msi`) 與 Burn bundle (`DIME-Universal.exe`) / Builds WiX MSIs and Burn bundle
4. 建立 `DIME-Universal.zip` 壓縮檔 / Creates ZIP archive
5. 計算 SHA-256 校驗和並更新 `README.md` / Calculates SHA-256 checksums and updates `README.md`

**可選參數 / Optional Parameters:**

| 參數 Parameter | 說明 Description |
| --- | --- |
| `-ProductVersionOverride "1.3.500.0"` | 覆蓋版號（跳過 BuildInfo.h 解析）/ Override version (skip BuildInfo.h parsing) |
| `-OutputDir "path"` | 指定輸出目錄 / Specify output directory |
| `-NonInteractive` | 無人值守模式（CI/CD 用）/ Unattended mode (for CI/CD) |
| `-SkipChecksumUpdate` | 跳過 ZIP 與校驗和步驟 / Skip ZIP and checksum steps |

**輸出檔案 / Output Files:**

```
installer/
├── DIME-Universal.exe    # Burn bundle 安裝程式 (自動偵測 OS 架構)
├── DIME-64bit.msi        # 64 位元 MSI (可獨立安裝)
├── DIME-32bit.msi        # 32 位元 MSI (可獨立安裝)
└── DIME-Universal.zip    # 安裝程式壓縮檔
```

詳細的 MSI 安裝程式架構與機制，請參閱 `docs/MSI_INSTALLER.md`。

For detailed MSI installer architecture and mechanism, see `docs/MSI_INSTALLER.md`.

---

### NSIS 安裝程式 (舊版) / NSIS Installer (Legacy)

NSIS 安裝程式用於 v1.2 及更早版本。v1.3 的 WiX 安裝程式包含自動 NSIS 遷移功能，可偵測並移除舊版 NSIS 安裝。

The NSIS installer was used for v1.2 and earlier. The v1.3 WiX installer includes automatic NSIS migration that detects and removes legacy NSIS installations.

#### NSIS 需求 / NSIS Requirements

- **NSIS (Nullsoft Scriptable Install System)** — 必須加入系統 PATH / Must be added to system PATH
  - 下載位置 / Download: [https://nsis.sourceforge.io/](https://nsis.sourceforge.io/)
- **NSIS Registry Plugin** — 用於安裝程式的註冊表操作 / For registry operations in the installer
  - 下載位置 / Download: [https://nsis.sourceforge.io/Registry_plug-in](https://nsis.sourceforge.io/Registry_plug-in)

#### 手動複製建置檔案 / Manual Copy of Build Files

```bash
copy src\Release\Win32\DIME.dll installer\system32.x86\
copy src\Release\x64\DIME.dll installer\system32.x64\
copy src\Release\ARM64EC\DIME.dll installer\system32.arm64\
copy src\Release\Win32\DIMESettings.exe installer\
```

#### NSIS 自動建置 / NSIS Automated Build

```powershell
cd installer
.\deploy-installer.ps1
```

此腳本會自動執行以下步驟：

This script automatically performs the following steps:

1. 複製建置產出物到安裝程式目錄 / Copies build artifacts to installer directory
2. 使用 NSIS 編譯 `DIME-Universal.exe` 安裝程式 / Builds `DIME-Universal.exe` using NSIS
3. 建立 `DIME-Universal.zip` 壓縮檔 / Creates ZIP archive
4. 計算 SHA-256 校驗和並更新 `README.md` / Calculates SHA-256 checksums and updates `README.md`

**輸出檔案 / Output Files:**

```
installer/
├── DIME-Universal.exe    # NSIS 通用安裝程式
└── DIME-Universal.zip    # 安裝程式壓縮檔
```

---

**最後更新 / Last Updated:** 2026-03-18
