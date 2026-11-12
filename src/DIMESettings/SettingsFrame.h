// SettingsFrame.h — window creation, theme and dpi changes of the settings window
//
// Phase 5b-8i of docs/RUST_MIGRATION_PLAN.md. WM_CREATE, WM_THEMECHANGED
// (UpdateTheme), WM_SETTINGCHANGE, and WM_DPICHANGED are a reducer:
// Handle() updates the dark-theme flag from one FrameEvent and lists the
// Win32 effects in order (brushes, fonts, DWM attributes, content area,
// rebuild, ...); SettingsWindow::RunFrame carries them out. The C++
// original is SettingsFrame::Cpp; with DIME_USE_RUST_CONFIG the dispatched
// functions run dime-settings (frame.rs). Change both together.

#pragma once

#include <windows.h>
#include <vector>

namespace SettingsFrame {

struct FrameState {
    int isDark;
};

enum FrameEventKind {
    EV_CREATE = 1,           // dpi, clientW, clientH
    EV_THEME_CHANGED,        // dpi = window dpi
    EV_SETTING_CHANGE,       // dpi = window dpi
    EV_DPI_CHANGED,          // dpi = new dpi; hasRect, rc = suggested rect
};

struct FrameEvent {
    int kind;
    int systemDark;          // CConfig::IsSystemDarkTheme()
    UINT dpi;
    int hasContent;
    int clientW;
    int clientH;
    int hasRect;
    RECT rc;                 // left, top, right, bottom
    int reserved;
};

enum FrameEffectKind {
    FX_SET_DARK_TITLE = 1,   // DWMWA_USE_IMMERSIVE_DARK_MODE = arg
    FX_CAPTION_COLOR,        // DwmSetWindowAttribute(arg, theme.contentBg); arg 35 or 36 (UpdateTheme)
    FX_THEME_BRUSHES,        // CreateThemeBrushes
    FX_CREATE_FONTS,         // CreateFonts(arg)
    FX_DESTROY_FONTS,
    FX_INVALIDATE_MAIN,      // erase
    FX_INVALIDATE_CONTENT,   // erase
    FX_CONTENT_THEME,        // SetWindowTheme(content, arg ? DarkMode_Explorer : null)
    FX_REBUILD,              // RebuildContentArea
    FX_SET_POS,              // SetWindowPos(main, rc = x, y, w, h)
    FX_CREATE_CONTENT,       // register the class, create the content area at rc = x, y, w, h
};

struct FrameEffect {
    int kind;
    int arg;
    RECT rc;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out);
// The window size Run creates at the screen dpi.
SIZE InitialSize(UINT dpi);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out);
SIZE InitialSize(UINT dpi);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out);
SIZE InitialSize(UINT dpi);
} // namespace Rust
#endif

} // namespace SettingsFrame
