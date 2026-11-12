// SettingsWinHost.h — the Win32 calls of the settings command host
//
// Phase 5b-8k of docs/RUST_MIGRATION_PLAN.md. The window-facing methods of
// the WindowHost class in SettingsWindow::HandleCommand: the font, colour,
// open and save dialogs, writing a UTF-16 file with a BOM, the Documents
// folder, LOGPIXELSY, and the custom table editor's text, line index, caret,
// and line count. CConfig, the table parser, and the viewport validation
// stay in WindowHost. The C++ original is SettingsWinHost::Cpp; with
// DIME_USE_RUST_CONFIG the dispatched functions run dime-settings
// (winhost.rs). Change both together.

#pragma once

#include <windows.h>
#include <string>

namespace SettingsWinHost {

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
int LogPixelsY(HWND wnd);
// Dialogs return true on OK; owner is the main window.
bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10);
bool PickColor(HWND owner, COLORREF& rgb);
// filter 0: CIN / TXT tables; otherwise phrase text files.
bool OpenFileDialog(HWND owner, int filter, std::wstring& path);
// path: the initial name in, the chosen one out.
bool SaveFileDialog(HWND owner, std::wstring& path);
// UTF-16 LE with a BOM; false only when the file can't be created.
bool WriteBomFile(const std::wstring& path, const std::wstring& text);
std::wstring DocumentsPath();
std::wstring EditorText(HWND re);
// Event mask off while setting the text; returns the new line count.
int SetEditorTextMuted(HWND re, const std::wstring& text);
int EditorLineIndex(HWND re, int line);
// Focus, caret, scroll to it (the caller then validates the viewport).
void EditorFocusAt(HWND re, int cp);
int EditorLineCount(HWND re);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
int LogPixelsY(HWND wnd);
bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10);
bool PickColor(HWND owner, COLORREF& rgb);
bool OpenFileDialog(HWND owner, int filter, std::wstring& path);
bool SaveFileDialog(HWND owner, std::wstring& path);
bool WriteBomFile(const std::wstring& path, const std::wstring& text);
std::wstring DocumentsPath();
std::wstring EditorText(HWND re);
int SetEditorTextMuted(HWND re, const std::wstring& text);
int EditorLineIndex(HWND re, int line);
void EditorFocusAt(HWND re, int cp);
int EditorLineCount(HWND re);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
int LogPixelsY(HWND wnd);
bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10);
bool PickColor(HWND owner, COLORREF& rgb);
bool OpenFileDialog(HWND owner, int filter, std::wstring& path);
bool SaveFileDialog(HWND owner, std::wstring& path);
bool WriteBomFile(const std::wstring& path, const std::wstring& text);
std::wstring DocumentsPath();
std::wstring EditorText(HWND re);
int SetEditorTextMuted(HWND re, const std::wstring& text);
int EditorLineIndex(HWND re, int line);
void EditorFocusAt(HWND re, int cp);
int EditorLineCount(HWND re);
} // namespace Rust
#endif

} // namespace SettingsWinHost
