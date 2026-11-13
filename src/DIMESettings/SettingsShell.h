// SettingsShell.h — the settings window's shell: placement, brushes,
// double-buffered painting, class registration, and the message loop
//
// Phase 5b-9d of docs/RUST_MIGRATION_PLAN.md. What used to be the file-static
// SaveWindowPlacement / RestoreWindowPlacement / DeleteWindowPlacement, the
// brush half of CreateThemeBrushes / DestroyThemeBrushes, the BeginPaint /
// memory-DC / BitBlt halves of the two WM_PAINT handlers, RegisterWindowClass
// / RegisterContentClass, and SettingsWindow::Run. The window still builds
// the PaintContexts and hides the controls (PaintSource), and still sets up
// its data when the window exists (RunParams::started). The C++ originals are
// SettingsShell::Cpp; with DIME_USE_RUST_CONFIG the dispatched functions run
// dime-settings (shell.rs). Change both together.

#pragma once

#include <windows.h>
#include "SettingsGeometry.h"
#include "SettingsWindowPaint.h"

namespace SettingsShell {

// Where the window placement is kept (HKCU).
constexpr wchar_t kPlacementKey[] = L"SOFTWARE\\DIME\\SettingsUI";
constexpr wchar_t kPlacementValue[] = L"WindowPlacement";

struct Brushes {
    HBRUSH sidebarBg;
    HBRUSH contentBg;
    HBRUSH cardBg;
};

// The window's half of a WM_PAINT. Callbacks must not throw.
struct PaintSource {
    void* user;
    // False: the window has no data yet (nothing is painted).
    int (*hasData)(void* user);
    // Main window: first = the main context, *hasContent = content area exists.
    // Content area: hide the controls under an open narrow sidebar, then
    // first = the content context, second = the main context.
    void (*prepare)(void* user, int content, SettingsWindowPaint::PaintContext* first,
        SettingsWindowPaint::PaintContext* second, int* hasContent);
};

struct RunParams {
    HINSTANCE instance;
    int nCmdShow;
    const wchar_t* mainClass;
    WNDPROC mainProc;
    int iconId;                 // LoadIcon(instance, MAKEINTRESOURCE(iconId))
    const wchar_t* caption;
    const wchar_t* placementKey;
    const wchar_t* placementValue;
    // The window exists (WM_CREATE has run): set up its data, switch mode.
    // Must not throw.
    void (*started)(void* user, HWND hWnd);
    void* user;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
// False when nothing valid is stored or its rectangle is on no monitor.
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
void DeletePlacement(const wchar_t* key, const wchar_t* value);
Brushes CreateBrushes(const ThemeColors& theme);
void DestroyBrushes(Brushes& b);
// The WM_PAINT handlers (BeginPaint ... EndPaint).
void PaintMain(HWND hWnd, const PaintSource& src);
void PaintContent(HWND hWnd, const PaintSource& src);
// Their bodies, painting a w x h client into target (tests use a DIB).
void PaintMainInto(HDC target, int w, int h, const PaintSource& src);
void PaintContentInto(HDC target, int w, int h, const PaintSource& src);
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId);
// Does nothing when a class with that name is already registered.
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc);
// GDI+ up, class, window, placement or nCmdShow, message loop, GDI+ down.
// -1 when the window cannot be created; else WM_QUIT's wParam.
int Run(const RunParams& p);

namespace Cpp {
bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
void DeletePlacement(const wchar_t* key, const wchar_t* value);
Brushes CreateBrushes(const ThemeColors& theme);
void DestroyBrushes(Brushes& b);
void PaintMain(HWND hWnd, const PaintSource& src);
void PaintContent(HWND hWnd, const PaintSource& src);
void PaintMainInto(HDC target, int w, int h, const PaintSource& src);
void PaintContentInto(HDC target, int w, int h, const PaintSource& src);
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId);
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc);
int Run(const RunParams& p);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
namespace Rust {
bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value);
void DeletePlacement(const wchar_t* key, const wchar_t* value);
Brushes CreateBrushes(const ThemeColors& theme);
void DestroyBrushes(Brushes& b);
void PaintMain(HWND hWnd, const PaintSource& src);
void PaintContent(HWND hWnd, const PaintSource& src);
void PaintMainInto(HDC target, int w, int h, const PaintSource& src);
void PaintContentInto(HDC target, int w, int h, const PaintSource& src);
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId);
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc);
int Run(const RunParams& p);
} // namespace Rust
#endif

} // namespace SettingsShell
