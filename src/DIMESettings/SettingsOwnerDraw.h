// SettingsOwnerDraw.h — owner-drawn controls and dark control colours of the
// settings window
//
// Phase 5b-8j of docs/RUST_MIGRATION_PLAN.md. The content area's
// WM_DRAWITEM (owner-drawn buttons, the six colour swatches) and
// WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX (card colours in the dark theme).
// The C++ original is SettingsOwnerDraw::Cpp; with DIME_USE_RUST_CONFIG the
// dispatched functions run dime-settings (owner.rs). Change both together.

#pragma once

#include <windows.h>
#include "SettingsGeometry.h"

namespace SettingsOwnerDraw {

// What the owner-drawn controls read. Same layout as dime-settings
// owner::Context.
struct Context {
    ThemeColors theme;
    HFONT fontBody;
    UINT dpi;                // main window dpi
    COLORREF swatch[6];      // candidate, selected, candidate bg, phrase, number, selected bg
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
// Owner-drawn buttons and the swatches (HMENU 4000-4005); false: not ours.
bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx);
// Dark theme: card text / background colours and the card brush (created
// in *brush when null). False in the light theme (not handled).
bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx);
bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx);
bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush);
} // namespace Rust
#endif

} // namespace SettingsOwnerDraw
