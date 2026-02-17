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
- **NSIS (Nullsoft Scriptable Install System)** (用於編譯安裝程式 / For compiling the installer)
  - 必須加入系統 PATH / Must be added to system PATH
  - 下載位置 / Download: [https://nsis.sourceforge.io/](https://nsis.sourceforge.io/)
- **NSIS Registry Plugin** (NSIS 註冊表擴充外掛 / NSIS registry extension plugin)
  - 用於安裝程式的註冊表操作 / For registry operations in the installer
  - 下載位置 / Download: [https://nsis.sourceforge.io/Registry_plug-in](https://nsis.sourceforge.io/Registry_plug-in)

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

### 1. 準備安裝程式檔案 / Prepare Installer Files

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

### 2. 複製建置檔案到安裝程式目錄 / Copy Build Files to Installer Directory

將編譯好的 DLL 和 EXE 檔案複製到 `installer/` 目錄：

Copy the built DLL and EXE files to the `installer/` directory:

```bash
# 從 src/Release/ 目錄複製到 installer/
# DIME.dll 需要三個平台版本 / DIME.dll requires three platform versions
copy src\Release\Win32\DIME.dll installer\system32.x86\
copy src\Release\x64\DIME.dll installer\system32.x64\
copy src\Release\ARM64EC\DIME.dll installer\system32.arm64\

# DIMESettings.exe 使用 Win32 版本 / DIMESettings.exe uses Win32 build
copy src\Release\Win32\DIMESettings.exe installer\
```

**注意：** 安裝程式需要來自 **Win32 Release** 建置的 `DIMESettings.exe`，以確保相容性。

**Note:** The installer requires `DIMESettings.exe` from the **Win32 Release** build for compatibility.

### 3. 自動建置安裝程式與校驗和 / Automated Installer Build and Checksums

使用 PowerShell 腳本自動完成安裝程式編譯、ZIP 封裝、校驗和計算與 README 更新：

Use the PowerShell script to automate installer compilation, ZIP packaging, checksum calculation, and README updates:

```powershell
cd installer
.\deploy-installer.ps1
```

此腳本會自動執行以下步驟：

This script automatically performs the following steps:

1. 使用 NSIS 編譯 `DIME-Universal.exe` 安裝程式
2. 建立 `DIME-Universal.zip` 壓縮檔
3. 計算 `.exe` 和 `.zip` 的 SHA-256 校驗和
4. 自動更新 `README.md` 中的校驗和表格（`CHECKSUM_START` ~ `CHECKSUM_END` 區段）

**前置需求 / Prerequisites:**

- NSIS 必須已安裝並加入系統 PATH / NSIS must be installed and added to system PATH
- 所有必要的建置檔案（DIME.dll 與 DIMESettings.exe）已複製到 `installer/` 目錄
- 所有字典檔案（`.cin`）已放置在 `installer/` 目錄

**輸出檔案 / Output Files:**

```
installer/
├── DIME-Universal.exe    # 通用安裝程式
└── DIME-Universal.zip    # 安裝程式壓縮檔
```

---

**最後更新 / Last Updated:** 2025-02-11
