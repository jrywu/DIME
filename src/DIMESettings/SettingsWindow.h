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

// Window class name for FindWindow / single-instance support
constexpr wchar_t SETTINGS_WND_CLASS[] = L"DIMESettingsMainClass";

// Custom messages
#define WM_SETTINGS_SWITCH_MODE  (WM_APP + 2)

// ============================================================================
// SettingsWindow — the main modern settings window
// ============================================================================
class SettingsWindow {
public:
    // Create and show the settings window. Returns 0 on success.
    static int Run(HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode);

    // Register the window class. Called once.
    static bool RegisterWindowClass(HINSTANCE hInstance);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    // Both window procedures: a ProcHost over the window, then SettingsProc::Main / Content.
    static LRESULT Dispatch(HWND hWnd, bool content, UINT msg, WPARAM wParam, LPARAM lParam);

public:
    // Content area child window procedure (public: used by RegisterContentClass)
    static LRESULT CALLBACK ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
    // Instance data stored via GWLP_USERDATA. Public so file-static helpers
    // in SettingsWindow.cpp (foreground/background validator, etc.) can hold
    // WindowData* parameters.
    struct WindowData {
        HINSTANCE hInstance;
        bool isDarkTheme;
        bool micaActive;

        // Theme colors (from system APIs, refreshed on WM_THEMECHANGED)
        ThemeColors theme;

        // Sidebar state
        int selectedModeIndex;    // 0-3 = IME mode, 5 = 載入碼表
        int hoverSidebarIndex;    // -1 = none

        // Navigation
        int navLevel;             // 0 = Level 1 (category list), 1 = Level 2 (sub-page)
        int currentCardIndex;     // Which card in s_imePageLayout[] is showing in Level 2
        bool cardExpanded[16];    // Which expandable cards are open (Level 1 ∨ sections)

        // Hover
        int hoverRowIndex;        // -1 = none; index into current layout.controls

        // Current IME mode
        IME_MODE currentMode;

        // Settings snapshot
        SettingsSnapshot snapshot;

        // Composition engine for custom table validation (ValidateCompositionKeyCharFull)
        CCompositionProcessorEngine* pEngine;

        // Total content height (for scroll)
        int totalContentHeight;

        // Cached preview height (computed in RebuildContentArea, reused in PaintSettingsDetail)
        // cachedPreviewH removed — preview is now a RowType::CandidatePreview card

        // Content area
        HWND hContentArea;
        int scrollPos;
        int scrollMax;

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
        bool colorGridExpanded; // true = color swatches visible in custom mode
        bool sidebarCollapsed; // true = sidebar hidden when window is narrow
        bool sidebarNarrowMode; // true = window is in narrow mode (< collapseBreak)

        // Win32 child controls
        struct ControlHandle {
            SettingsControlId id;
            HWND hWnd;
            int origX, origY;  // original position (before scroll offset)
            int origW, origH;  // window size (combobox origH = dropdown-open height)
            int visH;          // visible/painted height (for combo == closed control height)
        };
        std::vector<ControlHandle> controlHandles;

        WindowData() : hInstance(nullptr), isDarkTheme(false), micaActive(false),
            theme{}, selectedModeIndex(0), hoverSidebarIndex(-1),
            navLevel(0), currentCardIndex(0), hoverRowIndex(-1),
            currentMode(IME_MODE::IME_MODE_DAYI), snapshot{}, pEngine(nullptr),
            hContentArea(nullptr), scrollPos(0), scrollMax(0),
            ctLastLineCount(0), ctLastEditedLine(-1), ctKeystrokeCount(0),
            hBrushSidebarBg(nullptr), hBrushContentBg(nullptr), hBrushCardBg(nullptr),
            hFontTitle(nullptr), hFontCardHeader(nullptr), hFontBody(nullptr), hFontDesc(nullptr),
            hFontMDL2(nullptr), hFontMDL2Icon(nullptr), hasMDL2(false), colorGridExpanded(true), sidebarCollapsed(false), sidebarNarrowMode(false)
        {
            memset(cardExpanded, 0, sizeof(cardExpanded));
            totalContentHeight = 0;
            // cachedPreviewH removed
        }
    };

    // Helpers (internal — but FindControl is needed by file-static helpers
    // in SettingsWindow.cpp for the FG/BG validator, so it stays public).
private:
    static void SwitchMode(HWND hWnd, WindowData* wd, IME_MODE mode);
    static void RebuildContentArea(HWND hWnd, WindowData* wd);
    static void RunFrame(HWND hWnd, WindowData* wd, SettingsFrame::FrameEvent ev);
    static void CreateThemeBrushes(WindowData* wd);
    static void DestroyThemeBrushes(WindowData* wd);
    static void CreateFonts(WindowData* wd, int dpi);
    static void DestroyFonts(WindowData* wd);
    static void AddControl(WindowData* wd, SettingsControlId id, HWND h, int x, int y, int w, int ht, int visH = -1);
    static void CreateControls(WindowData* wd, const std::vector<SettingsControls::ControlSpec>& specs);
    static void SetupRichEditor(WindowData* wd, HWND hRE);
    static void LoadRichEditor(WindowData* wd, HWND hRE);
    static void RunNav(HWND hMain, WindowData* wd, SettingsNav::NavEvent ev);
    static void HandleCommand(HWND hWnd, WindowData* wd, WPARAM wParam, LPARAM lParam);
    static void ShowAllControls(WindowData* wd);
    static void UpdateScrollInfo(WindowData* wd);
    static void RepositionControlsForScroll(WindowData* wd);
    static void ApplyAndSave(WindowData* wd);
    static void PopulateControls(WindowData* wd);
public:
    static HWND FindControl(WindowData* wd, SettingsControlId id);
private:
    static COLORREF GetAccentColor();
};
