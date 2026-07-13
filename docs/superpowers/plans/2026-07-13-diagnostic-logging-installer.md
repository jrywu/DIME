# Diagnostic Logging Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one Release diagnostic installer for issues #139 and #141 with moderate-scope logging and `-logging` artifact names.

**Architecture:** An opt-in MSBuild property adds `DEBUG_PRINT` only to twelve selected translation units. The logger writes process-specific, identified records. One PowerShell entry point builds Release binaries and delegates WiX packaging to the existing deployment script in an isolated output directory.

**Tech Stack:** MSBuild/Visual C++, PowerShell 5+, WiX 6.

## Global Constraints

- Normal builds and checked-in installer artifacts must remain unchanged.
- Diagnostic binaries use `Configuration=Release`.
- Outputs are `DIME-Universal-logging.exe` and `DIME-Universal-logging.zip`.
- High-frequency candidate, paint, notification-window, and internal key-processing logging stays disabled.

---

### Task 1: Contract test

**Files:**
- Create: `src/tests/Test-DiagnosticInstaller.ps1`

- [ ] Write assertions for the exact twelve source files, opt-in property, hardened log filename/metadata, Release build commands, isolated packaging, and postfix artifacts.
- [ ] Run the test and confirm it fails because the production changes do not exist.

### Task 2: Selective logging and logger hardening

**Files:**
- Modify: `src/DIME.vcxproj`
- Modify: `src/Globals.h`

- [ ] Add conditional `DEBUG_PRINT` metadata to exactly the twelve approved `ClCompile` items.
- [ ] Correct log-directory creation and add timestamp, PID, TID, executable identity, and process-specific filenames.
- [ ] Run the contract test and C++ compilation checks.

### Task 3: Single diagnostic builder

**Files:**
- Create: `installer/build-logging-installer.ps1`

- [ ] Build DIME Win32/x64/ARM64 as Release with `DiagnosticLogging=true` and DIMESettings Win32 as ordinary Release.
- [ ] Package through `deploy-wix-installer.ps1` with isolated output and no README/checksum mutation.
- [ ] Rename the Burn executable and create the ZIP using `-logging` postfixes.
- [ ] Run the contract test, execute the builder, and inspect ZIP contents and repository diff.
