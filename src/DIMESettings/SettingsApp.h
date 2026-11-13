// SettingsApp.h — what the settings window needs from the rest of DIME
//
// Phase 5b-9e of docs/RUST_MIGRATION_PLAN.md. The settings window (C++
// SettingsWindow, or the Rust dime-settings::window under
// DIME_USE_RUST_CONFIG) reaches CConfig, the composition engine used to
// check custom-table keys, the table parser, the custom-table paths, the
// reverse-conversion list, and the themed message box only through Host.
// ConfigHost is the real one; tests give both windows a recording fake.
// The Rust window calls Host through DimeSettingsAppVtbl (MakeVtbl); the
// order must match dime_ffi.h and settings_window.rs (AppVtbl). Host methods
// must not throw.

#pragma once

#include <windows.h>
#include <string>
#include "SettingsController.h"

class CCompositionProcessorEngine;

namespace SettingsApp {

class Host {
public:
    virtual ~Host() = default;
    virtual UINT DpiForWindow(HWND hWnd) = 0;                 // CConfig::GetDpiForHwnd
    virtual bool SystemDark() = 0;                            // CConfig::IsSystemDarkTheme
    virtual int ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type) = 0;
    virtual void LoadConfig(int mode) = 0;
    virtual void WriteConfig(int mode) = 0;
    virtual void ResetAllDefaults(int mode) = 0;
    virtual int ColorMode() = 0;
    virtual void SetColorMode(int colorMode) = 0;
    virtual void LoadColorsForMode(int colorMode) = 0;
    virtual void SaveColorsForMode(int colorMode) = 0;
    virtual void GetColors(COLORREF out[6]) = 0;
    virtual void SetColor(int index, COLORREF color) = 0;
    virtual void SetReverseSelection(int sel) = 0;
    virtual void EnumerateReverseProviders() = 0;
    virtual int ReverseCount() = 0;
    virtual std::wstring ReverseDescription(int i) = 0;
    virtual bool ReverseIsCurrent(int i) = 0;
    virtual bool ReverseIsNull() = 0;
    virtual SettingsSnapshot LoadSnapshot() = 0;              // SettingsModel::LoadFromConfig
    virtual void ApplySnapshot(const SettingsSnapshot& s) = 0;  // SettingsModel::ApplyToConfig
    virtual bool ParseCin(const wchar_t* src, const wchar_t* dst, bool customTable) = 0;
    virtual void RecreateEngine(int mode) = 0;
    virtual void DestroyEngine() = 0;
    // May be null (no per-character key check).
    virtual CCompositionProcessorEngine* Engine() = 0;
    // The per-character key check of Engine(); only called when it exists.
    virtual bool EngineCharOk(wchar_t c) = 0;
    virtual std::wstring CustomTableTxtPath(int mode) = 0;
    virtual std::wstring CustomTableCinPath(int mode) = 0;
    virtual std::wstring AppDataPath() = 0;                   // Global::GetAppDataPath(create)
    virtual bool Windows1809OrLater() = 0;
};

// The real Host: CConfig, SettingsModel, ThemedMessageBox. Owns the engine.
class ConfigHost : public Host {
public:
    ~ConfigHost() override;
    UINT DpiForWindow(HWND hWnd) override;
    bool SystemDark() override;
    int ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type) override;
    void LoadConfig(int mode) override;
    void WriteConfig(int mode) override;
    void ResetAllDefaults(int mode) override;
    int ColorMode() override;
    void SetColorMode(int colorMode) override;
    void LoadColorsForMode(int colorMode) override;
    void SaveColorsForMode(int colorMode) override;
    void GetColors(COLORREF out[6]) override;
    void SetColor(int index, COLORREF color) override;
    void SetReverseSelection(int sel) override;
    void EnumerateReverseProviders() override;
    int ReverseCount() override;
    std::wstring ReverseDescription(int i) override;
    bool ReverseIsCurrent(int i) override;
    bool ReverseIsNull() override;
    SettingsSnapshot LoadSnapshot() override;
    void ApplySnapshot(const SettingsSnapshot& s) override;
    bool ParseCin(const wchar_t* src, const wchar_t* dst, bool customTable) override;
    void RecreateEngine(int mode) override;
    void DestroyEngine() override;
    CCompositionProcessorEngine* Engine() override { return _engine; }
    bool EngineCharOk(wchar_t c) override;
    std::wstring CustomTableTxtPath(int mode) override;
    std::wstring CustomTableCinPath(int mode) override;
    std::wstring AppDataPath() override;
    bool Windows1809OrLater() override;
private:
    CCompositionProcessorEngine* _engine = nullptr;
};

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
// The C vtable over a Host (ctx is the Host*).
const void* Vtbl();
#endif

} // namespace SettingsApp
