# Diagnostic Logging Installer Design

## Goal

Produce one diagnostic installer for GitHub issues #139 and #141 without changing the behavior or artifacts of the normal release build. The diagnostic DLLs remain optimized Release builds, with the existing `DEBUG_PRINT` facility enabled only where needed to trace host focus, key entry, composition, and host edit callbacks.

## Logging Scope

Enable `DEBUG_PRINT` only for this moderate-cost set of translation units when the diagnostic build property is set:

- `DIME.cpp`
- `ActiveLanguageProfileNotifySink.cpp`
- `ThreadMgrEventSink.cpp`
- `ThreadFocusSink.cpp`
- `KeyEventSink.cpp`
- `Composition.cpp`
- `StartComposition.cpp`
- `EndComposition.cpp`
- `TextEditSink.cpp`
- `TfTextLayoutSink.cpp`
- `Server.cpp`
- `UIACaretTracker.cpp`

High-frequency candidate, paint, notification-window, and internal key-processing sources remain disabled to avoid materially slowing typing or changing crash timing.

Normal Release and Debug builds do not define `DEBUG_PRINT` through this mechanism.

## Build Interface

Add one PowerShell entry point under `installer/` that:

1. Builds `DIME` for Win32, x64, and ARM64 using `Configuration=Release` and a diagnostic logging MSBuild property.
2. Builds `DIMESettings` as an ordinary Win32 Release binary.
3. Invokes the existing WiX deployment script in an isolated output directory with checksum/README mutation disabled.
4. Renames the Burn bundle to `DIME-Universal-logging.exe`.
5. Creates `DIME-Universal-logging.zip` containing that executable.

The script fails immediately if any platform build, packaging step, or expected artifact fails. It must not overwrite the checked-in normal `DIME-Universal.zip`, MSI files, or README checksums.

## Logger Safety

Correct creation of `%APPDATA%\DIME` so first-run logging works. Prefix each record with timestamp, process ID, and thread ID, and write to a process-specific file so simultaneous TSF host processes do not interleave records. The executable identity is included in the filename. Existing message contents remain unchanged for this first diagnostic build.

## Verification

- Confirm the project evaluates `DEBUG_PRINT` only for the twelve selected source files when the diagnostic property is enabled.
- Confirm a normal Release preprocessing/build path does not receive the definition.
- Build all required Release platforms through the new script.
- Confirm both `DIME-Universal-logging.exe` and `DIME-Universal-logging.zip` exist and the ZIP contains the postfix-named executable.
- Confirm normal checked-in installer artifacts and README are unchanged.
