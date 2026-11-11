// SettingsEditor.h — live validation of the 自建詞庫 (custom table) RichEdit
//
// Phase 5b-8d of docs/RUST_MIGRATION_PLAN.md. The foreground viewport pass
// (validate the visible lines, colour errors through TOM without moving the
// caret) and the editor's subclass procedure were lifted out of
// SettingsWindow.cpp; the C++ originals are SettingsEditor::Cpp. With
// DIME_USE_RUST_CONFIG the unqualified functions use dime-settings
// (editor.rs) instead. Change both together. Design: docs/RICHEDIT_VIEWPORT_VAL.md.

#pragma once

#include <windows.h>
#include <commctrl.h>

class CCompositionProcessorEngine;

namespace SettingsEditor {

// What ValidateLine needs for this page.
struct EditorContext {
    int mode;                               // IME_MODE
    UINT maxCodes;
    int isDark;                             // error / valid colours for the dark theme
    CCompositionProcessorEngine* engine;    // per-char key check; may be null
};

// The subclass posts these to its parent (the content area).
constexpr UINT kMsgRevalidate = WM_USER + 1;   // Enter / Space / paste / large delete
constexpr UINT kMsgViewport   = WM_USER + 3;   // scrolled: paint newly visible lines

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
// Inclusive range of visible lines; 0, 0 for an empty editor.
void ViewportLineRange(HWND hRE, int* firstVis, int* lastVis);
// Validate and colour the visible lines. Selection and event mask are kept.
void ValidateViewport(HWND hRE, const EditorContext& ctx);
// The subclass procedure to install with SetWindowSubclass(hRE, proc, 0, 0).
SUBCLASSPROC SubclassProc();

namespace Cpp {
void ViewportLineRange(HWND hRE, int* firstVis, int* lastVis);
void ValidateViewport(HWND hRE, const EditorContext& ctx);
LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
} // namespace Cpp

} // namespace SettingsEditor
