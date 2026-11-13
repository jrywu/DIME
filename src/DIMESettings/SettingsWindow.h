// SettingsWindow.h — Modern settings window (sidebar + card-based content)
//
// Replaces the old launcher dialog + PropertySheet with a single window.
// Uses SettingsModel for layout, visibility, and CConfig bridging.

#pragma once

#include <windows.h>
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsGeometry.h"
#include "SettingsNav.h"
#include "SettingsControls.h"
#include "SettingsLifecycle.h"
#include "SettingsFrame.h"
#include "SettingsDriver.h"
#include "SettingsApp.h"

// Window class name for FindWindow / single-instance support
constexpr wchar_t SETTINGS_WND_CLASS[] = L"DIMESettingsMainClass";

// Custom messages
#define WM_SETTINGS_SWITCH_MODE  (WM_APP + 2)

// ============================================================================
// SettingsWindow — the main modern settings window
// ============================================================================
class SettingsWindow {
public:
    // Create and show the settings window over SettingsApp::ConfigHost and
    // run its message loop (the Rust window under DIME_USE_RUST_CONFIG).
    static int Run(HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode);
    // The C++ window over any app host (Run without DIME_USE_RUST_CONFIG).
    static int RunCpp(SettingsApp::Host* app, HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode,
        const wchar_t* caption);

    // Tests: the window as Run creates it (classes and placement value of the
    // test's choosing), without showing it or running a message loop.
    static HWND CreateForTest(SettingsApp::Host* app, HINSTANCE hInstance, IME_MODE initialMode,
        const wchar_t* caption, const wchar_t* mainClass, const wchar_t* contentClass,
        const wchar_t* placementKey, const wchar_t* placementValue);
    static bool StateForTest(HWND hMain, SettingsDriver::State* state, SettingsSnapshot* snapshot);
    // Switch to mode, then show sidebar item selectedModeIndex at navLevel /
    // cardIndex from the top (rebuilds the page).
    static void NavigateForTest(HWND hMain, IME_MODE mode, int selectedModeIndex, int navLevel, int cardIndex);
    // Paint the main window (or, with content, the content area hWnd) into target.
    static void PaintForTest(HWND hWnd, HDC target, int w, int h, bool content);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    // Both window procedures: a ProcHost over the window, then SettingsProc::Main / Content.
    static LRESULT Dispatch(HWND hWnd, bool content, UINT msg, WPARAM wParam, LPARAM lParam);

public:
    // Content area child window procedure (public: used by RegisterContentClass)
    static LRESULT CALLBACK ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
    // Public so file-static helpers in SettingsWindow.cpp can hold
    // WindowData* parameters.
    // Instance data stored via GWLP_USERDATA. The navigation fields
    // (nav.selectedModeIndex, nav.navLevel, nav.scrollPos, ...) and the dark
    // flag are SettingsDriver::State, which the driver reads and writes in place.
    struct WindowData : SettingsDriver::State {
        HINSTANCE hInstance;
        bool micaActive;

        // Theme colors (from system APIs, refreshed on WM_THEMECHANGED)
        ThemeColors theme;

        // Settings snapshot
        SettingsSnapshot snapshot;

        // CConfig, the engine, paths (SettingsApp::Host), and where this
        // window's classes and placement live.
        SettingsApp::Host* app;
        const wchar_t* contentClass;
        const wchar_t* placementKey;
        const wchar_t* placementValue;

        // Content area
        HWND hContentArea;

        // Custom table editor state (EN_CHANGE smart validation tracking)
        int ctLastLineCount;
        int ctLastEditedLine;
        int ctKeystrokeCount;

        // GDI resources
        HBRUSH hBrushSidebarBg;
        HBRUSH hBrushContentBg;
        HBRUSH hBrushCardBg;
        HFONT hFontTitle;
        HFONT hFontCardHeader;
        HFONT hFontBody;
        HFONT hFontDesc;
        HFONT hFontMDL2;       // Segoe MDL2 Assets — small, for chevrons
        HFONT hFontMDL2Icon;   // Segoe MDL2 Assets — large, for row icons
        bool hasMDL2;          // true if Segoe MDL2 Assets is installed (Win10+)

        // Win32 child controls
        struct ControlHandle {
            SettingsControlId id;
            HWND hWnd;
            int origX, origY;  // original position (before scroll offset)
            int origW, origH;  // window size (combobox origH = dropdown-open height)
            int visH;          // visible/painted height (for combo == closed control height)
        };
        std::vector<ControlHandle> controlHandles;

        WindowData() : SettingsDriver::State{}, hInstance(nullptr), micaActive(false),
            theme{}, snapshot{}, app(nullptr), contentClass(nullptr), placementKey(nullptr),
            placementValue(nullptr), hContentArea(nullptr),
            ctLastLineCount(0), ctLastEditedLine(-1), ctKeystrokeCount(0),
            hBrushSidebarBg(nullptr), hBrushContentBg(nullptr), hBrushCardBg(nullptr),
            hFontTitle(nullptr), hFontCardHeader(nullptr), hFontBody(nullptr), hFontDesc(nullptr),
            hFontMDL2(nullptr), hFontMDL2Icon(nullptr), hasMDL2(false)
        {
            nav.hoverSidebarIndex = -1;
            nav.hoverRowIndex = -1;
            nav.currentMode = (int)IME_MODE::IME_MODE_DAYI;
            nav.colorGridExpanded = 1;   // color swatches visible in custom mode
        }
        IME_MODE Mode() const { return (IME_MODE)nav.currentMode; }
    };

    // Helpers (internal — but FindControl is needed by file-static helpers
    // in SettingsWindow.cpp for the FG/BG validator, so it stays public).
private:
    // SettingsDriver::Host over the main window (SettingsWindow.cpp).
    class DriverHost;
    static void SwitchMode(HWND hWnd, WindowData* wd, IME_MODE mode);
    static void RebuildContentArea(HWND hWnd, WindowData* wd);
    static void RunFrame(HWND hWnd, WindowData* wd, SettingsFrame::FrameEvent ev);
    static void CreateThemeBrushes(WindowData* wd);
    static void DestroyThemeBrushes(WindowData* wd);
    static void CreateFonts(WindowData* wd, int dpi);
    static void DestroyFonts(WindowData* wd);
    static void AddControl(WindowData* wd, SettingsControlId id, HWND h, int x, int y, int w, int ht, int visH = -1);
    static void CreateControls(WindowData* wd, const std::vector<SettingsControls::ControlSpec>& specs);
    static void RunNav(HWND hMain, WindowData* wd, SettingsNav::NavEvent ev);
    static void HandleCommand(HWND hWnd, WindowData* wd, WPARAM wParam, LPARAM lParam);
    static void ApplyAndSave(WindowData* wd);
    static void PopulateControls(WindowData* wd);
public:
    static HWND FindControl(WindowData* wd, SettingsControlId id);
private:
    static COLORREF GetAccentColor();
};
