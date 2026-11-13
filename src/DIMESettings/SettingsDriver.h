// SettingsDriver.h — what the settings window does with navigation, frame,
// mode-switch, and rebuild events
//
// Phase 5b-9b of docs/RUST_MIGRATION_PLAN.md. SettingsWindow used to carry
// out SettingsNav / SettingsFrame effects itself (RunNav / RunFrame), and to
// switch modes, rebuild the content controls, keep the scroll bar, and move
// controls on scroll. That is here now: the window state is a State the
// driver reads and writes through a pointer (SettingsWindow::WindowData
// holds it), and every side effect goes through Host. The C++ original is
// SettingsDriver::Cpp; with DIME_USE_RUST_CONFIG the dispatched functions run
// dime-settings (driver.rs) through DimeSettingsDriverHostVtbl. Change both
// together.
//
// Host calls may re-enter the driver (ClickSave sends WM_COMMAND, which can
// rebuild the page), so neither side keeps a copy of State across a Host call.

#pragma once

#include <windows.h>
#include "SettingsNav.h"
#include "SettingsFrame.h"
#include "SettingsWindowLayout.h"

namespace SettingsDriver {

// The part of SettingsWindow::WindowData the driver owns (72 bytes).
struct State {
    SettingsNav::NavState nav;
    int isDark;
    int reserved;
};

// One entry of WindowData::controlHandles (32 bytes).
struct ControlInfo {
    int id;          // SettingsControlId
    int exists;      // hWnd != nullptr
    int alive;       // hWnd && IsWindow(hWnd)
    int visible;     // alive && IsWindowVisible(hWnd)
    int x, y, w, h;  // origX / origY / origW / origH
};

// Host methods must not throw (the Rust bridge calls them through C thunks).
// Methods on the content area do nothing while it does not exist.
class Host {
public:
    virtual ~Host() = default;

    // Facts
    virtual UINT MainDpi() = 0;                  // CConfig::GetDpiForHwnd(main)
    virtual UINT ContentDpi() = 0;               // CConfig::GetDpiForHwnd(content)
    virtual bool HasContent() = 0;
    virtual void MainClient(int& w, int& h) = 0; // GetClientRect(main)
    virtual int ContentClientHeight() = 0;       // GetClientRect(content).bottom
    virtual int VScrollWidth() = 0;              // GetSystemMetrics(SM_CXVSCROLL)
    virtual bool SystemDark() = 0;               // CConfig::IsSystemDarkTheme
    virtual int ColorMode() = 0;                 // snapshot.colorMode
    virtual bool SaveEnabled() = 0;              // 自建詞庫 [儲存] exists and is enabled
    virtual int PreviewHeight(UINT dpi) = 0;     // CalcCandidatePreviewH on a content DC

    // Child controls (WindowData::controlHandles, by index)
    virtual int ControlCount() = 0;
    virtual ControlInfo Control(int index) = 0;
    virtual void MoveControl(int index, int x, int y, int w, int h) = 0;
    virtual void ShowControl(int index, bool show) = 0;
    virtual void ResetControlRegion(int index) = 0;
    virtual bool ControlChecked(int index) = 0;            // BM_GETCHECK == BST_CHECKED
    virtual void SetControlChecked(int index, bool on) = 0;
    virtual void DestroyControls() = 0;                    // DestroyWindow each, clear the list
    // SettingsControls::BuildSpecs(in, cardW, ...) and create them.
    virtual void CreateControls(const SettingsWindowLayout::PlanInput& in, int cardW) = 0;
    virtual void Populate() = 0;                           // PopulateControls

    // Windows
    virtual void InvalidateMain(const RECT* rc, bool erase) = 0;
    virtual void InvalidateContent(bool erase) = 0;
    virtual void TrackLeave(bool content) = 0;             // TrackMouseEvent(TME_LEAVE)
    virtual void SetScrollInfo(int total, int page, int pos) = 0;
    virtual void RedrawContent() = 0;                      // RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN
    virtual void UpdateContent() = 0;
    virtual void MoveContent(int x, int y, int w, int h) = 0;
    virtual void FocusContent() = 0;
    virtual void CreateContent(int x, int y, int w, int h) = 0;
    virtual void ContentTheme(bool dark) = 0;
    virtual void SetMainPos(int x, int y, int w, int h) = 0;
    virtual void SetDarkTitle(bool dark) = 0;
    virtual void SetCaptionColor(int attribute) = 0;       // DWM attribute = theme.contentBg
    virtual void ThemeBrushes() = 0;                       // CreateThemeBrushes (from State::isDark)
    virtual void CreateFonts(int dpi) = 0;
    virtual void DestroyFonts() = 0;

    // 自建詞庫 left unsaved
    virtual int AskSaveBeforeBack() = 0;                   // IDYES / IDNO / IDCANCEL
    virtual void ClickSave() = 0;                          // WM_COMMAND BN_CLICKED of [儲存] to the content area

    // Settings
    virtual void ToggleSnapshot(int id, bool on) = 0;      // SyncToggleToSnapshot
    virtual void ApplyAndSave() = 0;
    virtual void EnumerateReverseProviders() = 0;
    virtual void LoadConfig(int mode) = 0;
    virtual void LoadColors() = 0;                         // LoadColorsForMode(GetColorMode())
    virtual void LoadSnapshot() = 0;                       // snapshot = LoadFromConfig()
    virtual void RecreateEngine(int mode) = 0;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
// RunNav: ev.kind / x / y / delta / thumb are the caller's; the rest is filled here.
void Nav(Host& host, State* st, SettingsNav::NavEvent ev);
// RunFrame: ev.kind / dpi / client size / rect are the caller's.
void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev);
void SwitchMode(Host& host, State* st, int mode);
void Rebuild(Host& host, State* st);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
void Nav(Host& host, State* st, SettingsNav::NavEvent ev);
void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev);
void SwitchMode(Host& host, State* st, int mode);
void Rebuild(Host& host, State* st);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
void Nav(Host& host, State* st, SettingsNav::NavEvent ev);
void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev);
void SwitchMode(Host& host, State* st, int mode);
void Rebuild(Host& host, State* st);
} // namespace Rust
#endif

} // namespace SettingsDriver
