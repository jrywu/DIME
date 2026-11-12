// SettingsProc.h — the settings window procedures
//
// Phase 5b-9a of docs/RUST_MIGRATION_PLAN.md. SettingsWindow::WndProc and
// ContentWndProc only build a Host and call Main / Content: which message
// does what, how its parameters are decoded, and what it returns live here.
// Every side effect goes through Host, including DefWindowProc,
// PostQuitMessage, and SetForegroundWindow. The C++ original is
// SettingsProc::Cpp; with DIME_USE_RUST_CONFIG, Main / Content run
// dime-settings (wndproc.rs) through DimeSettingsProcHostVtbl. Change both
// together. lParam must be what Windows passes with the message.

#pragma once

#include <windows.h>

namespace SettingsProc {

// Host methods must not throw (the Rust bridge calls them through C thunks).
class Host {
public:
    virtual ~Host() = default;
    virtual bool HasData() = 0;                     // WindowData attached
    virtual bool HasContent() = 0;                  // the content area exists
    virtual void Create(HINSTANCE instance) = 0;    // WM_CREATE
    virtual void Nav(int kind, int x, int y, int delta, int thumb) = 0;   // RunNav
    virtual void ThemeChanged() = 0;
    virtual void SettingChanged() = 0;
    virtual void DpiChanged(UINT dpi, const RECT* suggested) = 0;
    virtual void PaintMain() = 0;
    virtual void PaintContent() = 0;
    virtual UINT ScreenDpi() = 0;                   // LOGPIXELSY of the screen (96 if unknown)
    virtual void SwitchMode(int mode) = 0;
    virtual void SetForeground() = 0;
    virtual void Destroy() = 0;                     // save placement, free the window data
    virtual void Quit() = 0;                        // PostQuitMessage(0)
    virtual void Command(WPARAM wParam, LPARAM lParam) = 0;
    virtual void EditorRevalidate() = 0;            // kMsgRevalidate
    virtual void EditorViewport() = 0;              // kMsgViewport
    virtual bool DrawItem(const DRAWITEMSTRUCT* dis) = 0;
    virtual bool CtlColor(HDC hdc, LRESULT& brush) = 0;
    virtual LRESULT Default(UINT msg, WPARAM wParam, LPARAM lParam) = 0;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam);
} // namespace Rust
#endif

} // namespace SettingsProc
