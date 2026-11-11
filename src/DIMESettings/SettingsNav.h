// SettingsNav.h — navigation, hover, scrolling, and sizing of the settings window
//
// Phase 5b-8b of docs/RUST_MIGRATION_PLAN.md. The mouse / wheel / scroll /
// size handlers of SettingsWindow::WndProc and ContentWndProc are a reducer:
// Handle() updates a NavState from one NavEvent and appends the effects the
// window procedure must carry out, in order (switch mode, rebuild controls,
// invalidate, toggle a checkbox, ...). The C++ original is
// SettingsNav::Cpp::Handle; with DIME_USE_RUST_CONFIG, Handle calls
// dime-settings (nav.rs) instead. Change both together.

#pragma once

#include <windows.h>
#include <vector>
#include "SettingsWindowLayout.h"

namespace SettingsNav {

// The navigation part of SettingsWindow::WindowData.
struct NavState {
    int selectedModeIndex;   // 0-3 = IME mode, 5 = 載入碼表
    int hoverSidebarIndex;
    int navLevel;
    int currentCardIndex;
    int hoverRowIndex;
    int currentMode;         // IME_MODE
    int scrollPos;
    int scrollMax;
    int totalContentHeight;
    int sidebarCollapsed;
    int sidebarNarrowMode;
    int colorGridExpanded;
    bool cardExpanded[SettingsWindowLayout::kExpandedSlots];
};

enum NavEventKind {
    EV_MAIN_SIZE = 1,        // x = client width, y = client height
    EV_MAIN_WHEEL,           // delta
    EV_MAIN_LBUTTON,         // x, y
    EV_MAIN_MOUSEMOVE,       // x, y
    EV_MAIN_MOUSELEAVE,
    EV_CONTENT_VSCROLL,      // delta = LOWORD(wParam), thumb = HIWORD(wParam)
    EV_CONTENT_WHEEL,        // delta
    EV_CONTENT_MOUSEMOVE,    // x, y
    EV_CONTENT_MOUSELEAVE,
    EV_CONTENT_LBUTTON,      // x, y
    EV_BACK,                 // NavigateBack (after the unsaved-table prompt)
};

struct NavEvent {
    int kind;
    int x;
    int y;
    int delta;
    int thumb;
    UINT dpi;
    int previewH;            // CalcCandidatePreviewH, for sub-page rows
    int colorMode;           // snapshot.colorMode
    int customTableDirty;    // 自建詞庫 [儲存] is enabled
};

enum NavEffectKind {
    FX_INVALIDATE_MAIN = 1,  // erase; hasRect ? rc : whole window
    FX_INVALIDATE_CONTENT,   // erase
    FX_TRACK_LEAVE_MAIN,
    FX_TRACK_LEAVE_CONTENT,
    FX_SCROLL_CHANGED,       // UpdateScrollInfo, RepositionControlsForScroll, RedrawWindow(content)
    FX_SHOW_ALL_CONTROLS,
    FX_HIDE_ALL_CONTROLS,
    FX_SWITCH_MODE,          // arg = IME_MODE
    FX_REBUILD,              // RebuildContentArea(main)
    FX_TOGGLE_CONTROL,       // arg = SettingsControlId of a toggle row
    FX_ASK_SAVE_THEN_BACK,   // unsaved 自建詞庫: prompt, then EV_BACK
    FX_MOVE_CONTENT,         // rc = x, y, w, h
    FX_SET_FOCUS_CONTENT,
    FX_UPDATE_CONTENT,       // UpdateWindow(content)
    FX_UPDATE_SCROLL_INFO,
};

struct NavEffect {
    int kind;
    int arg;
    int erase;
    int hasRect;
    RECT rc;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& out);
// WM_GETMINMAXINFO: minimum track size at the screen dpi.
POINT MinTrackSize(UINT screenDpi);

namespace Cpp {
void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& out);
POINT MinTrackSize(UINT screenDpi);
} // namespace Cpp

} // namespace SettingsNav
