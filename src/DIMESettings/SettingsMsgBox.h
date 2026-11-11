// SettingsMsgBox.h — ThemedMessageBox, the owner-drawn modal MessageBox of the
// settings window.
//
// Phase 5b-8a of docs/RUST_MIGRATION_PLAN.md. The window (class, window
// procedure, modal loop) was lifted out of SettingsWindow.cpp; the C++
// original is SettingsMsgBox::Cpp::Show. With DIME_USE_RUST_CONFIG, Show runs
// the Rust window in dime-settings (msgboxwin.rs) instead; change both together.

#pragma once

#include <windows.h>
#include "SettingsGeometry.h"

namespace SettingsMsgBox {

// The class the shipped message box registers. Tests pass their own names so
// the C++ and Rust windows can run side by side.
constexpr wchar_t kClassName[] = L"DIMEThemedMsgBoxClass";

typedef UINT (*DpiForWindowFn)(HWND hWnd);

// Theme from CConfig::IsSystemDarkTheme(), dpi from CConfig::GetDpiForHwnd.
int Show(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type);

namespace Cpp {
// Modal: returns IDOK / IDYES / IDNO / IDCANCEL (or MessageBoxW's result when
// the window cannot be created).
int Show(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type,
    const wchar_t* className, bool isDark, const ThemeColors& tc, DpiForWindowFn dpiFor);
} // namespace Cpp

} // namespace SettingsMsgBox
