// SettingsControls.h — which child controls a settings page creates
//
// Phase 5b-8f of docs/RUST_MIGRATION_PLAN.md. The decisions of
// CreateRowControl / CreateColorGridControls / CreateRichEditorControl
// (window class, styles, caption, HMENU id, rectangles, font, dark theme)
// were lifted out of SettingsWindow.cpp into a list of ControlSpec in
// creation order; RebuildContentArea only creates the windows. The C++
// original is SettingsControls::Cpp::BuildSpecs; with DIME_USE_RUST_CONFIG,
// BuildSpecs runs dime-settings (controls.rs). Change both together.

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "SettingsWindowLayout.h"

namespace SettingsControls {

enum ControlClass { CLS_BUTTON = 0, CLS_COMBOBOX, CLS_EDIT, CLS_STATIC, CLS_RICHEDIT };
enum ControlRole { ROLE_PLAIN = 0, ROLE_RICH_EDITOR, ROLE_SAVE_BUTTON };
enum DarkTheme { DARK_NONE = 0, DARK_CFD, DARK_EXPLORER };   // SetWindowTheme when dark

struct ControlSpec {
    int ctrlId;              // SettingsControlId recorded with AddControl
    int menuId;              // HMENU of the child window
    int role;                // ControlRole
    int controlClass;        // ControlClass
    DWORD exStyle;
    DWORD style;
    bool hasText;            // false: CreateWindowEx with a null caption
    std::wstring text;
    RECT create;             // x, y, w, h passed to CreateWindowEx (left, top, right = w, bottom = h)
    RECT add;                // AddControl x, y, w, h
    int visH;                // AddControl visH (-1: default)
    bool setFont;            // WM_SETFONT with the body font
    int darkTheme;           // DarkTheme applied only when the window is dark
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
// Specs for the page in `in`, in creation order. cardW is CalcCardW.
std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount);

namespace Cpp {
std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
std::vector<ControlSpec> BuildSpecsRust(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount);
#endif

// Class name for CreateWindowEx.
const wchar_t* ClassName(int controlClass);

} // namespace SettingsControls
