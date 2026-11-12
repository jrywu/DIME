// SettingsLifecycle.h — launch arguments, the single-instance mode switch,
// page defaults, and scroll placement of the settings window
//
// Phase 5b-8h of docs/RUST_MIGRATION_PLAN.md. Decisions that used to be
// inline in wWinMain (which UI to launch), WM_COPYDATA (the mode a second
// instance asks for), SwitchMode (the default card expand state), and
// RepositionControlsForScroll (where each control goes, hidden in the title
// band). The C++ original is SettingsLifecycle::Cpp; with
// DIME_USE_RUST_CONFIG the dispatched functions run dime-settings
// (lifecycle.rs). Change both together.

#pragma once

#include <windows.h>
#include <vector>
#include "..\BaseStructure.h"
#include "SettingsPageLayout.h"
#include "SettingsWindowLayout.h"

namespace SettingsLifecycle {

// What wWinMain makes of its command line (an empty or null line: all
// zero, guiMode DAYI). resetUi: delete the saved window placement.
struct LaunchArgs {
    int legacy;
    int resetUi;
    int newUi;
    int guiMode;             // IME_MODE
};

// One tracked control for PlanScroll.
struct ScrollItem {
    int origY;
    int origW;
    int origH;
    int alive;               // live HWND
    int visible;             // IsWindowVisible before the move
};

// With moved: SetWindowPos to (origX, y); show -1 hides, 1 shows; resetRgn
// clears the window region (before showing).
struct ScrollAction {
    int moved;
    int y;
    int resetRgn;
    int show;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
LaunchArgs ParseLaunch(const wchar_t* cmdLine);
// IME_MODE_NONE: no switch.
IME_MODE CopyDataMode(const wchar_t* cmdLine);
void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots]);
std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
LaunchArgs ParseLaunch(const wchar_t* cmdLine);
IME_MODE CopyDataMode(const wchar_t* cmdLine);
void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots]);
std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
// ---- Always the Rust port (parity tests) ----------------------------------
namespace Rust {
LaunchArgs ParseLaunch(const wchar_t* cmdLine);
IME_MODE CopyDataMode(const wchar_t* cmdLine);
void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots]);
std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi);
} // namespace Rust
#endif

} // namespace SettingsLifecycle
