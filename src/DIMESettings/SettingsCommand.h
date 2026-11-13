// SettingsCommand.h — WM_COMMAND of the settings content area
//
// Phase 5b-8c of docs/RUST_MIGRATION_PLAN.md. Buttons (font, reset, load
// table, custom table save / export / import), combo boxes, the max-codes
// edit, the custom-table editor's EN_CHANGE, and the color swatches. The
// logic (which action, validation, messages, paths, snapshot updates, the
// order of calls) is Handle(); every side effect goes through Host (dialogs,
// CConfig, files, the RichEdit, repaint). The C++ original is
// SettingsCommand::Cpp::Handle; with DIME_USE_RUST_CONFIG, Handle runs
// dime-settings (command.rs) through the C vtable in dime_ffi.h. Change both
// together.

#pragma once

#include <windows.h>
#include <string>
#include "SettingsController.h"

namespace SettingsCommand {

struct CommandEvent {
    int notifyCode;          // HIWORD(wParam)
    int ctrlId;              // LOWORD(wParam): 3000 + SettingsControlId, 4000 + swatch
    int mode;                // IME_MODE of the page
    int comboSel;            // CBN_SELCHANGE: CB_GETCURSEL
    int comboItemData;       // CBN_SELCHANGE: CB_GETITEMDATA of comboSel
    wchar_t editText[16];    // EN_CHANGE of CTRL_MAX_CODES: GetWindowTextW(buf, 16)
};

class Host {
public:
    virtual ~Host() = default;
    // Dialogs
    virtual int LogPixelsY() = 0;
    virtual bool PickFont(LOGFONTW& lf, int& pointSize10) = 0;
    virtual bool PickColor(COLORREF& rgb) = 0;
    virtual bool OpenFileDialog(int filter, std::wstring& path) = 0;   // filter 0: .cin/.txt, 1: .txt
    virtual bool SaveFileDialog(std::wstring& path) = 0;               // in: default, out: chosen
    virtual int ShowMessage(const std::wstring& text, const wchar_t* caption, UINT type) = 0;
    // Paths and files
    virtual std::wstring AppDataPath() = 0;
    virtual std::wstring DocumentsPath() = 0;
    virtual std::wstring CustomTableTxtPath(int mode) = 0;
    virtual std::wstring CustomTableCinPath(int mode) = 0;
    virtual bool WriteBomFile(const std::wstring& path, const std::wstring& text) = 0;
    virtual bool LoadTextFile(const std::wstring& path, std::wstring& text) = 0;
    virtual bool ParseCin(const std::wstring& src, const std::wstring& dst, bool customTable) = 0;
    // CConfig
    virtual void ResetAllDefaults(int mode) = 0;
    virtual void WriteConfig(int mode) = 0;
    virtual void LoadConfig(int mode) = 0;
    virtual SettingsSnapshot LoadSnapshot() = 0;
    virtual void ApplyAndSave() = 0;                                // the window's snapshot
    virtual void SetColorMode(int colorMode) = 0;
    virtual void LoadColorsForMode(int colorMode) = 0;
    virtual void GetColors(COLORREF out[6]) = 0;
    virtual void SetColor(int index, COLORREF color) = 0;
    virtual void SaveColorsForMode(int colorMode) = 0;
    virtual void SetReverseConversionSelection(int sel) = 0;
    // Window
    virtual void Rebuild() = 0;
    virtual void InvalidateContent(bool erase) = 0;
    virtual void UpdateContent() = 0;
    virtual void InvalidateSwatch(int index) = 0;
    // 自建詞庫 editor
    virtual bool EditorExists() = 0;
    virtual std::wstring EditorText() = 0;
    virtual void SetEditorTextMuted(const std::wstring& text) = 0;  // EN_CHANGE off, counters reset
    virtual int EditorLineIndex(int line) = 0;                       // EM_LINEINDEX
    virtual void EditorFocusAt(int cp) = 0;                          // focus, caret, scroll, viewport
    virtual void ValidateViewport() = 0;
    virtual void CancelBgScan() = 0;
    virtual void EnableSave(bool enable) = 0;
    virtual void UpdateLineCount() = 0;
    virtual int ValidateBuffer(const std::wstring& text, int maxReport, int& firstErrorLine,
        std::wstring& summary) = 0;                                  // summary of up to 1023 chars
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
// snapshot and colorGridExpanded are the window's; Handle updates them in place
// (Host::Rebuild reads them during the call).
void Handle(Host& host, SettingsSnapshot& snapshot, int& colorGridExpanded, const CommandEvent& ev);

namespace Cpp {
void Handle(Host& host, SettingsSnapshot& snapshot, int& colorGridExpanded, const CommandEvent& ev);
} // namespace Cpp

#ifdef DIME_RUST_AVAILABLE
// Run the Rust version against any Host (the dispatch above uses it; tests too).
void HandleRust(Host& host, SettingsSnapshot& snapshot, int& colorGridExpanded, const CommandEvent& ev);
#endif

} // namespace SettingsCommand
