// SettingsWindow2.cpp — CLEAN REWRITE of the settings window
//
// Architecture rules:
// 1. Every interactive element is a REAL Win32 control (BUTTON, COMBOBOX, EDIT, RICHEDIT, STATIC)
// 2. Controls are created in RebuildContentArea() at absolute Y positions
// 3. On scroll: RepositionControlsForScroll() moves ALL controls via SetWindowPos
// 4. Owner-draw (toggle switches, color swatches, row icons) paints ON TOP of real controls
// 5. Every child control is subclassed to forward WM_MOUSEWHEEL to content area
// 6. All geometry from g_geo, all colors from wd->theme (system-derived)

#include "framework.h"
#include <CommCtrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <strsafe.h>
#include <windowsx.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <math.h>
#include <richedit.h>     // EM_GETOLEINTERFACE
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#include "..\Globals.h"
#include "..\Config.h"
#include "..\CompositionProcessorEngine.h"
#include "..\TfInputProcessorProfile.h"
#include "..\BuildInfo.h"
#include "SettingsWindow.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsGeometry.h"
#include "SettingsWindowLayout.h"
#include "SettingsWindowPaint.h"
#include "SettingsMsgBox.h"
#include "SettingsNav.h"
#include "SettingsCommand.h"
#include "SettingsEditor.h"
#include "SettingsPopulate.h"
#include "SettingsControls.h"

#include <Richedit.h>
#include <MLang.h>
#include <Shlwapi.h>
#include <regex>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define DWMWA_MICA_EFFECT 1029

// Content area child window
#define IDC_CONTENT_AREA 2000
static const wchar_t CONTENT_WND_CLASS[] = L"DIMESettingsContentArea";
static bool s_contentClassRegistered = false;

// ThemedMessageBox — owner-drawn MessageBox replacement (SettingsMsgBox.*).
static int ThemedMessageBox(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    return SettingsMsgBox::Show(hParent, text, caption, type);
}

// ============================================================================
// Child control WM_MOUSEWHEEL forwarder
// ============================================================================
static LRESULT CALLBACK ChildWheelSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_MOUSEWHEEL)
        return SendMessage(GetParent(hWnd), uMsg, wParam, lParam);
    if (uMsg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, ChildWheelSubclassProc, uIdSubclass);
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ============================================================================
// ValidateCustomTableBuffer — fast in-memory validator (issue #130).
// Iterates a UTF-16 buffer line by line and runs SettingsModel::ValidateLine.
// O(N) over text size; does NOT touch RichEdit (so no O(N²) EM_GETTEXTRANGE
// pass and no per-line repaint). Used by Import and Save where we already
// have the full buffer in memory; the per-keystroke EN_CHANGE path keeps
// using ValidateCustomTableRE because it also paints error highlights.
// Returns total error count. Fills *outFirstErrLine with the 1-based line
// number of the first error (0 if none). Appends "  第 N 行\n" entries to
// outSummary up to maxReport entries (caller responsible for buffer sizing).
// ============================================================================
static int ValidateCustomTableBuffer(const wchar_t* text, size_t wlen,
    IME_MODE mode, UINT maxCodes, CCompositionProcessorEngine* pEng,
    int* outFirstErrLine, wchar_t* outSummary, size_t summaryCch, int maxReport)
{
    return SettingsWindowLayout::ValidateBuffer(text, wlen, mode, maxCodes, pEng,
        outFirstErrLine, outSummary, summaryCch, maxReport);
}

// ============================================================================
// Issue #130: live validation of the custom table editor (SettingsEditor.*).
// The chunked background scan that used to live here was never started
// (StartBgScan had no caller since the Phase 3 final fix) and is removed;
// Save validates the whole buffer itself. See docs/RICHEDIT_VIEWPORT_VAL.md.
// ============================================================================
static void ValidateViewport(HWND hRE, SettingsWindow::WindowData* wd)
{
    if (!hRE || !wd) return;
    SettingsEditor::EditorContext ctx = { (int)wd->currentMode, wd->snapshot.maxCodes,
        wd->isDarkTheme ? 1 : 0, wd->pEngine };
    SettingsEditor::ValidateViewport(hRE, ctx);
}

// Find and enable/disable the Save button.
static void EnableSaveButton(SettingsWindow::WindowData* wd, BOOL enable)
{
    if (!wd) return;
    HWND hSaveBtn = SettingsWindow::FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    if (hSaveBtn) EnableWindow(hSaveBtn, enable);
}

// Custom table path helpers — delegated to SettingsModel
#define GetCustomTableTxtPath  SettingsModel::GetCustomTableTxtPath
#define GetCustomTableCINPath  SettingsModel::GetCustomTableCINPath

// ============================================================================
// Window placement persistence (registry)
// ============================================================================
static const wchar_t SETTINGS_UI_REG_KEY[] = L"SOFTWARE\\DIME\\SettingsUI";
static const wchar_t SETTINGS_UI_WP_VALUE[] = L"WindowPlacement";

static bool SaveWindowPlacement(HWND hWnd)
{
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(hWnd, &wp)) return false;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, 0, nullptr,
        0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, SETTINGS_UI_WP_VALUE, 0, REG_BINARY, (const BYTE*)&wp, sizeof(wp));
    RegCloseKey(hKey);
    return true;
}

static bool RestoreWindowPlacement(HWND hWnd)
{
    WINDOWPLACEMENT wp = {};
    DWORD cbData = sizeof(wp);
    DWORD dwType = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, SETTINGS_UI_WP_VALUE,
        RRF_RT_REG_BINARY, &dwType, &wp, &cbData) != ERROR_SUCCESS) return false;
    if (cbData != sizeof(wp) || wp.length != sizeof(wp)) return false;
    // Verify saved position is on a connected monitor
    if (!MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL)) return false;
    SetWindowPlacement(hWnd, &wp);
    return true;
}

static void DeleteWindowPlacement()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, SETTINGS_UI_WP_VALUE);
        RegCloseKey(hKey);
    }
}

// ============================================================================
// Mica backdrop (title bar only)
// ============================================================================
static bool ApplyMicaBackdrop(HWND hWnd)
{
    int backdropType = 2;
    if (SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(int))))
        return true;
    BOOL enable = TRUE;
    if (SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_MICA_EFFECT, &enable, sizeof(BOOL))))
        return true;
    return false;
}

// ============================================================================
// Helper: create a font
// ============================================================================
// ============================================================================
// Theme brushes + colors
// ============================================================================
void SettingsWindow::CreateThemeBrushes(WindowData* wd)
{
    DestroyThemeBrushes(wd);
    wd->theme = ComputeThemeColors(wd->isDarkTheme);
    wd->hBrushSidebarBg = CreateSolidBrush(wd->theme.sidebarBg);
    wd->hBrushContentBg = CreateSolidBrush(wd->theme.contentBg);
    wd->hBrushCardBg    = CreateSolidBrush(wd->theme.cardBg);
}

void SettingsWindow::DestroyThemeBrushes(WindowData* wd)
{
    if (wd->hBrushSidebarBg)  { DeleteObject(wd->hBrushSidebarBg);  wd->hBrushSidebarBg = nullptr; }
    if (wd->hBrushContentBg)  { DeleteObject(wd->hBrushContentBg);  wd->hBrushContentBg = nullptr; }
    if (wd->hBrushCardBg)     { DeleteObject(wd->hBrushCardBg);     wd->hBrushCardBg = nullptr; }
}

void SettingsWindow::CreateFonts(WindowData* wd, int dpi)
{
    DestroyFonts(wd);
    SettingsWindowPaint::Fonts f = {};
    SettingsWindowPaint::CreateFonts((UINT)dpi, &f);
    wd->hFontTitle      = f.title;
    wd->hFontCardHeader = f.cardHeader;
    wd->hFontBody       = f.body;
    wd->hFontDesc       = f.desc;
    wd->hFontMDL2       = f.mdl2;
    wd->hFontMDL2Icon   = f.mdl2Icon;
    wd->hasMDL2         = f.hasMDL2 != 0;
}

void SettingsWindow::DestroyFonts(WindowData* wd)
{
    if (wd->hFontTitle)      { DeleteObject(wd->hFontTitle);      wd->hFontTitle = nullptr; }
    if (wd->hFontCardHeader) { DeleteObject(wd->hFontCardHeader); wd->hFontCardHeader = nullptr; }
    if (wd->hFontBody)       { DeleteObject(wd->hFontBody);       wd->hFontBody = nullptr; }
    if (wd->hFontDesc)       { DeleteObject(wd->hFontDesc);       wd->hFontDesc = nullptr; }
    if (wd->hFontMDL2)       { DeleteObject(wd->hFontMDL2);       wd->hFontMDL2 = nullptr; }
    if (wd->hFontMDL2Icon)   { DeleteObject(wd->hFontMDL2Icon);   wd->hFontMDL2Icon = nullptr; }
}

// Mode helpers — delegated to SettingsModel
#define IndexToMode  SettingsModel::IndexToMode
#define ModeToIndex  SettingsModel::ModeToIndex

COLORREF SettingsWindow::GetAccentColor() { return ComputeThemeColors(false).accent; }

// ============================================================================
// Scroll helpers
// ============================================================================
void SettingsWindow::UpdateScrollInfo(WindowData* wd)
{
    if (!wd->hContentArea) return;
    RECT rc; GetClientRect(wd->hContentArea, &rc);
    wd->scrollMax = SettingsModel::ComputeScrollRange(wd->totalContentHeight, rc.bottom);
    wd->scrollPos = SettingsModel::ClampScrollPos(wd->scrollPos, wd->scrollMax);
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, wd->totalContentHeight, (UINT)rc.bottom, wd->scrollPos };
    SetScrollInfo(wd->hContentArea, SB_VERT, &si, TRUE);
}

void SettingsWindow::RepositionControlsForScroll(WindowData* wd)
{
    if (!wd->hContentArea) return;
    UINT dpi = CConfig::GetDpiForHwnd(wd->hContentArea);
    int titleBottom = ScaleForDpi(g_geo.topMargin, dpi);
    for (auto& ch : wd->controlHandles) {
        if (!(ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0)))
            continue;
        int newY = ch.origY - wd->scrollPos;
        SetWindowPos(ch.hWnd, nullptr, ch.origX, newY,
            ch.origW, ch.origH, SWP_NOZORDER | SWP_NOACTIVATE);
        // Snap-hide any control whose top enters the title cover band.
        // Settings rows are short (~24 px) — partial clipping looks like a
        // floating control above the title rather than a graceful reveal.
        if (newY < titleBottom) {
            if (IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
        } else {
            SetWindowRgn(ch.hWnd, nullptr, TRUE);
            if (!IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_SHOW);
        }
    }
}

void SettingsWindow::ShowAllControls(WindowData* wd)
{
    for (auto& ch : wd->controlHandles) {
        if (ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0))
            ShowWindow(ch.hWnd, SW_SHOW);
    }
}

// Forward declarations
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi);

// Navigation state <-> WindowData.
static SettingsNav::NavState LoadNav(const SettingsWindow::WindowData* wd)
{
    SettingsNav::NavState n = {};
    n.selectedModeIndex = wd->selectedModeIndex;
    n.hoverSidebarIndex = wd->hoverSidebarIndex;
    n.navLevel = wd->navLevel;
    n.currentCardIndex = wd->currentCardIndex;
    n.hoverRowIndex = wd->hoverRowIndex;
    n.currentMode = (int)wd->currentMode;
    n.scrollPos = wd->scrollPos;
    n.scrollMax = wd->scrollMax;
    n.totalContentHeight = wd->totalContentHeight;
    n.sidebarCollapsed = wd->sidebarCollapsed ? 1 : 0;
    n.sidebarNarrowMode = wd->sidebarNarrowMode ? 1 : 0;
    n.colorGridExpanded = wd->colorGridExpanded ? 1 : 0;
    memcpy(n.cardExpanded, wd->cardExpanded, sizeof(n.cardExpanded));
    return n;
}

static void StoreNav(SettingsWindow::WindowData* wd, const SettingsNav::NavState& n)
{
    wd->selectedModeIndex = n.selectedModeIndex;
    wd->hoverSidebarIndex = n.hoverSidebarIndex;
    wd->navLevel = n.navLevel;
    wd->currentCardIndex = n.currentCardIndex;
    wd->hoverRowIndex = n.hoverRowIndex;
    wd->currentMode = (IME_MODE)n.currentMode;
    wd->scrollPos = n.scrollPos;
    wd->scrollMax = n.scrollMax;
    wd->totalContentHeight = n.totalContentHeight;
    wd->sidebarCollapsed = n.sidebarCollapsed != 0;
    wd->sidebarNarrowMode = n.sidebarNarrowMode != 0;
    wd->colorGridExpanded = n.colorGridExpanded != 0;
    memcpy(wd->cardExpanded, n.cardExpanded, sizeof(wd->cardExpanded));
}

// Feed one event to SettingsNav, then carry out its effects in order.
void SettingsWindow::RunNav(HWND hMain, WindowData* wd, SettingsNav::NavEvent ev)
{
    HWND hContent = wd->hContentArea;
    ev.dpi = CConfig::GetDpiForHwnd(hMain);
    ev.colorMode = wd->snapshot.colorMode;
    HWND hSaveBtn = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    ev.customTableDirty = (hSaveBtn && IsWindowEnabled(hSaveBtn)) ? 1 : 0;
    ev.previewH = 0;
    if (wd->navLevel == 1 && wd->selectedModeIndex < 4 && hContent) {
        HDC hdcTmp = GetDC(hContent);
        ev.previewH = CalcCandidatePreviewH(hdcTmp, wd->snapshot, ev.dpi);
        ReleaseDC(hContent, hdcTmp);
    }

    SettingsNav::NavState nav = LoadNav(wd);
    std::vector<SettingsNav::NavEffect> effects;
    SettingsNav::Handle(nav, ev, effects);
    StoreNav(wd, nav);

    for (const auto& fx : effects) {
        switch (fx.kind) {
        case SettingsNav::FX_INVALIDATE_MAIN:
            InvalidateRect(hMain, fx.hasRect ? &fx.rc : nullptr, fx.erase ? TRUE : FALSE);
            break;
        case SettingsNav::FX_INVALIDATE_CONTENT:
            if (hContent) InvalidateRect(hContent, nullptr, fx.erase ? TRUE : FALSE);
            break;
        case SettingsNav::FX_TRACK_LEAVE_MAIN:
        case SettingsNav::FX_TRACK_LEAVE_CONTENT: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE,
                fx.kind == SettingsNav::FX_TRACK_LEAVE_MAIN ? hMain : hContent };
            TrackMouseEvent(&tme);
            break;
        }
        case SettingsNav::FX_SCROLL_CHANGED:
            UpdateScrollInfo(wd);
            RepositionControlsForScroll(wd);
            RedrawWindow(hContent, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            break;
        case SettingsNav::FX_SHOW_ALL_CONTROLS:
            ShowAllControls(wd);
            break;
        case SettingsNav::FX_HIDE_ALL_CONTROLS:
            for (auto& ch : wd->controlHandles)
                if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
            break;
        case SettingsNav::FX_SWITCH_MODE:
            SwitchMode(hMain, wd, (IME_MODE)fx.arg);
            break;
        case SettingsNav::FX_REBUILD:
            RebuildContentArea(hMain, wd);
            break;
        case SettingsNav::FX_TOGGLE_CONTROL: {
            SettingsControlId id = (SettingsControlId)fx.arg;
            HWND hCtrl = FindControl(wd, id);
            if (hCtrl) {
                BOOL checked = (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool newState = !checked;
                SendMessage(hCtrl, BM_SETCHECK, newState ? BST_CHECKED : BST_UNCHECKED, 0);
                SettingsModel::SyncToggleToSnapshot(wd->snapshot, id, newState);
                ApplyAndSave(wd);
                InvalidateRect(hContent, nullptr, FALSE);
                UpdateWindow(hContent);
            }
            break;
        }
        case SettingsNav::FX_ASK_SAVE_THEN_BACK: {
            // 自建詞庫 has unsaved changes: ask before leaving the sub-page.
            int result = ThemedMessageBox(hMain,
                L"自建詞庫尚未儲存，是否要儲存後離開？\n\n[是] 儲存並離開\n[否] 不儲存離開\n[取消] 繼續編輯",
                L"自建詞庫未儲存", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (result == IDYES) {
                HWND hSave = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                SendMessageW(hContent, WM_COMMAND,
                    MAKEWPARAM(3000 + CTRL_CUSTOM_TABLE_SAVE, BN_CLICKED), (LPARAM)hSave);
            }
            if (result == IDYES || result == IDNO) {
                SettingsNav::NavEvent back = {};
                back.kind = SettingsNav::EV_BACK;
                RunNav(hMain, wd, back);
            }
            break;
        }
        case SettingsNav::FX_MOVE_CONTENT:
            MoveWindow(hContent, fx.rc.left, fx.rc.top, fx.rc.right, fx.rc.bottom, FALSE);
            break;
        case SettingsNav::FX_SET_FOCUS_CONTENT:
            SetFocus(hContent);
            break;
        case SettingsNav::FX_UPDATE_CONTENT:
            UpdateWindow(hContent);
            break;
        case SettingsNav::FX_UPDATE_SCROLL_INFO:
            UpdateScrollInfo(wd);
            break;
        default:
            break;
        }
    }
}

// ============================================================================
// Shared geometry helpers — used by ALL paint paths and RebuildContentArea
// ============================================================================

// Card width from main window — stable regardless of scrollbar visibility
static int CalcCardW(HWND hMainWnd, UINT dpi, bool sidebarTakesSpace)
{
    RECT rcMain; GetClientRect(hMainWnd, &rcMain);
    return SettingsWindowLayout::CardWidth(rcMain.right, dpi, sidebarTakesSpace,
        GetSystemMetrics(SM_CXVSCROLL));
}

// Candidate preview height; RebuildContentArea and the painter must agree.
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    return SettingsWindowPaint::CandidatePreviewHeight(hdc, s, dpi);
}

HWND SettingsWindow::FindControl(WindowData* wd, SettingsControlId id)
{
    for (auto& ch : wd->controlHandles)
        if (ch.id == id) return ch.hWnd;
    return nullptr;
}

// Map toggle control ID → snapshot bool field, update snapshot from checkbox state
// SyncToggleToSnapshot moved to SettingsController.cpp — use SettingsModel::SyncToggleToSnapshot()

void SettingsWindow::ApplyAndSave(WindowData* wd)
{
    SettingsModel::ApplyToConfig(wd->snapshot);
    CConfig::WriteConfig(wd->currentMode);
}

void SettingsWindow::UpdateTheme(HWND hWnd, WindowData* wd)
{
    wd->isDarkTheme = CConfig::IsSystemDarkTheme();
    CreateThemeBrushes(wd);
    CreateFonts(wd, CConfig::GetDpiForHwnd(hWnd));
    if (wd->isDarkTheme) {
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
    }
    // Match title bar color to content background (DWMWA_CAPTION_COLOR = 36)
    COLORREF captionColor = wd->theme.contentBg;
    DwmSetWindowAttribute(hWnd, 36, &captionColor, sizeof(COLORREF));
    InvalidateRect(hWnd, nullptr, TRUE);
    if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
}

// ============================================================================
// TODO: The rest of the clean implementation follows
// Each function will be written step by step
// ============================================================================

// Placeholder stubs — will be replaced one by one
void SettingsWindow::SwitchMode(HWND hWnd, WindowData* wd, IME_MODE mode)
{
    bool modeChanged = (mode != wd->currentMode);
    wd->currentMode = mode;
    wd->selectedModeIndex = ModeToIndex(mode);
    wd->navLevel = 0;
    // Initialize expand state from layout defaultExpanded
    memset(wd->cardExpanded, 0, sizeof(wd->cardExpanded));
    {
        int imeCount = 0;
        const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
        for (int i = 0; i < imeCount && i < 16; i++)
            wd->cardExpanded[i] = imeCards[i].defaultExpanded;
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        for (int i = 0; i < ltCount && (4 + i) < 16; i++)
            wd->cardExpanded[4 + i] = ltCards[i].defaultExpanded;
    }
    // Re-enumerate and reload only on actual mode change.
    // SetIMEMode() filters self from the list but skips if mode is unchanged.
    // Don't re-enumerate when staying on the same mode (e.g., sidebar click from
    // Level 1 back to Level 0) — the list is already correctly filtered.
    if (modeChanged)
        CConfig::EnumerateReverseConversionProviders(1028);
    CConfig::LoadConfig(mode);
    CConfig::LoadColorsForMode((IME_COLOR_MODE)CConfig::GetColorMode());
    wd->snapshot = SettingsModel::LoadFromConfig();
    // Recreate composition engine for the new mode (custom table validation)
    SettingsModel::DestroyEngine(wd->pEngine);
    wd->pEngine = SettingsModel::CreateEngine(mode);
    RebuildContentArea(hWnd, wd);
    InvalidateRect(hWnd, nullptr, TRUE);
}

// Everything the painters read. hPaint is the window painted into (its client
// size); dpi and card width come from the main window.
static SettingsWindowPaint::PaintContext MakePaintContext(HWND hPaint, HWND hMain,
    SettingsWindow::WindowData* wd)
{
    SettingsWindowPaint::PaintContext ctx = {};
    ctx.theme = wd->theme;
    ctx.snapshot = wd->snapshot;
    ctx.fontTitle = wd->hFontTitle;
    ctx.fontCardHeader = wd->hFontCardHeader;
    ctx.fontBody = wd->hFontBody;
    ctx.fontDesc = wd->hFontDesc;
    ctx.fontMDL2 = wd->hFontMDL2;
    ctx.fontMDL2Icon = wd->hFontMDL2Icon;
    ctx.brushSidebarBg = wd->hBrushSidebarBg;
    ctx.brushContentBg = wd->hBrushContentBg;
    ctx.iconModule = GetModuleHandleW(nullptr);
    ctx.dpi = CConfig::GetDpiForHwnd(hMain);
    ctx.hasMDL2 = wd->hasMDL2 ? 1 : 0;
    RECT rc; GetClientRect(hPaint, &rc);
    ctx.clientW = rc.right;
    ctx.clientH = rc.bottom;
    ctx.cardW = CalcCardW(hMain, ctx.dpi, !wd->sidebarNarrowMode && !wd->sidebarCollapsed);
    ctx.sysFace = GetSysColor(COLOR_3DFACE);
    ctx.selectedModeIndex = wd->selectedModeIndex;
    ctx.hoverSidebarIndex = wd->hoverSidebarIndex;
    ctx.navLevel = wd->navLevel;
    ctx.currentCardIndex = wd->currentCardIndex;
    ctx.hoverRowIndex = wd->hoverRowIndex;
    ctx.currentMode = (int)wd->currentMode;
    ctx.scrollPos = wd->scrollPos;
    ctx.sidebarNarrowMode = wd->sidebarNarrowMode ? 1 : 0;
    ctx.sidebarCollapsed = wd->sidebarCollapsed ? 1 : 0;
    ctx.colorGridExpanded = wd->colorGridExpanded ? 1 : 0;
    memcpy(ctx.cardExpanded, wd->cardExpanded, sizeof(ctx.cardExpanded));
    // Toggle state of the first control with each id (FindControl order).
    bool seen[SettingsWindowPaint::kToggleSlots] = {};
    for (auto& ch : wd->controlHandles) {
        if (ch.id < 0 || ch.id >= SettingsWindowPaint::kToggleSlots || seen[ch.id]) continue;
        seen[ch.id] = true;
        ctx.toggleOn[ch.id] = (ch.hWnd && SendMessage(ch.hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    }
    return ctx;
}

void SettingsWindow::AddControl(WindowData* wd, SettingsControlId id, HWND h, int x, int y, int w, int ht, int visH)
{
    WindowData::ControlHandle ch = {};
    ch.id = id; ch.hWnd = h; ch.origX = x; ch.origY = y; ch.origW = w; ch.origH = ht;
    ch.visH = (visH > 0) ? visH : ht;
    wd->controlHandles.push_back(ch);
    if (h) SetWindowSubclass(h, ChildWheelSubclassProc, 1, 0);
}

// ============================================================================
// Child controls of a page: SettingsControls::BuildSpecs decides, this creates.
// ============================================================================
// After the RichEdit is created: text limit, theme, the saved custom table,
// the editor subclass, and the first viewport pass.
void SettingsWindow::SetupRichEditor(WindowData* wd, HWND hRE)
{
    // Issue #130: RichEdit50W defaults to ~32K-64K characters; SetWindowTextW
    // bypasses the cap but typing is then rejected. Raise it to 100 MB.
    SendMessageW(hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100 * 1024 * 1024));
    SendMessage(hRE, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
    if (wd->isDarkTheme) {
        SetWindowTheme(hRE, L"DarkMode_Explorer", nullptr);
        SendMessage(hRE, EM_SETBKGNDCOLOR, 0, (LPARAM)wd->theme.cardBg);
        CHARFORMAT2W cf2 = {};
        cf2.cbSize = sizeof(cf2);
        cf2.dwMask = CFM_COLOR;
        cf2.crTextColor = wd->theme.textPrimary;
        SendMessage(hRE, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf2);
    }
}

void SettingsWindow::LoadRichEditor(WindowData* wd, HWND hRE)
{
    // Load the saved custom table with the encoding-aware reader (issue #130).
    WCHAR txtPath[MAX_PATH] = {};
    GetCustomTableTxtPath(wd->currentMode, txtPath, MAX_PATH);
    if (PathFileExistsW(txtPath)) {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
        StringCchCopyW(cf.szFaceName, LF_FACESIZE, L"Microsoft JhengHei UI");
        cf.yHeight = 10 * 20;  // 10pt in twips — RichEdit applies DPI itself
        cf.wWeight = FW_NORMAL;
        SendMessageW(hRE, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

        size_t wlen = 0;
        LPWSTR text = SettingsModel::LoadTextFileAsUtf16(txtPath, &wlen);
        if (text) {
            SetWindowTextW(hRE, text);
            delete[] text;
        }
    }
    SetWindowSubclass(hRE, SettingsEditor::SubclassProc(), 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, ENM_CHANGE);
    wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    wd->ctLastEditedLine = -1;
    wd->ctKeystrokeCount = 0;
    // Paint the visible range now (TOM, no caret jump). The Save button stays
    // disabled: the content matches the file.
    ValidateViewport(hRE, wd);
}

void SettingsWindow::CreateControls(WindowData* wd, const std::vector<SettingsControls::ControlSpec>& specs)
{
    static HMODULE hMsftEdit = nullptr;
    for (const auto& spec : specs) {
        if (spec.controlClass == SettingsControls::CLS_RICHEDIT && !hMsftEdit)
            hMsftEdit = LoadLibraryW(L"msftedit.dll");
        HWND h = CreateWindowExW(spec.exStyle, SettingsControls::ClassName(spec.controlClass),
            spec.hasText ? spec.text.c_str() : nullptr, spec.style,
            spec.create.left, spec.create.top, spec.create.right, spec.create.bottom,
            wd->hContentArea, (HMENU)(UINT_PTR)spec.menuId, wd->hInstance, nullptr);
        if (spec.role == SettingsControls::ROLE_RICH_EDITOR) {
            if (h) SetupRichEditor(wd, h);
            AddControl(wd, (SettingsControlId)spec.ctrlId, h, spec.add.left, spec.add.top, spec.add.right, spec.add.bottom, spec.visH);
            if (h) {
                RemoveWindowSubclass(h, ChildWheelSubclassProc, 1);
                LoadRichEditor(wd, h);
            }
            continue;
        }
        if (h && spec.setFont)
            SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
        if (h && wd->isDarkTheme && spec.darkTheme == SettingsControls::DARK_CFD)
            SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        else if (h && wd->isDarkTheme && spec.darkTheme == SettingsControls::DARK_EXPLORER)
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        AddControl(wd, (SettingsControlId)spec.ctrlId, h, spec.add.left, spec.add.top, spec.add.right, spec.add.bottom, spec.visH);
    }
}

void SettingsWindow::RebuildContentArea(HWND hWnd, WindowData* wd)
{
    if (!wd->hContentArea) return;

    // Destroy all existing child controls
    for (auto& ch : wd->controlHandles) {
        if (ch.hWnd) DestroyWindow(ch.hWnd);
    }
    wd->controlHandles.clear();

    // Position content area based on navLevel:
    // navLevel==1 (sub-page): start below the breadcrumb strip painted in main WndProc
    // navLevel==0: start at y=0
    bool sidebarTakesSpace = !wd->sidebarNarrowMode && !wd->sidebarCollapsed;
    {
        RECT rcMain; GetClientRect(hWnd, &rcMain);
        UINT dpiPos = CConfig::GetDpiForHwnd(hWnd);
        bool hasBc = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
        SettingsWindowLayout::CtrlRect rcArea = SettingsWindowLayout::ContentAreaRect(
            rcMain.right, rcMain.bottom, dpiPos, sidebarTakesSpace, hasBc);
        MoveWindow(wd->hContentArea, rcArea.x, rcArea.y, rcArea.w, rcArea.h, FALSE);
    }

    UINT dpi = CConfig::GetDpiForHwnd(hWnd);
    int cardW = CalcCardW(hWnd, dpi, sidebarTakesSpace);

    int imeCount = 0;
    const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
    int ltCount = 0;
    const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);

    SettingsWindowLayout::PlanInput in = {};
    in.dpi = dpi;
    in.selectedModeIndex = wd->selectedModeIndex;
    in.navLevel = wd->navLevel;
    in.currentCardIndex = wd->currentCardIndex;
    in.modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
    in.colorMode = wd->snapshot.colorMode;
    in.gridExpanded = wd->colorGridExpanded ? 1 : 0;
    in.previewH = 0;
    if (wd->navLevel == 1 && wd->selectedModeIndex < 4) {
        HDC hdcTmp = GetDC(wd->hContentArea);
        in.previewH = CalcCandidatePreviewH(hdcTmp, wd->snapshot, dpi);
        ReleaseDC(wd->hContentArea, hdcTmp);
    }
    memcpy(in.expanded, wd->cardExpanded, sizeof(in.expanded));

    std::vector<SettingsWindowLayout::Placement> placements;
    SettingsWindowLayout::PlanResult plan = SettingsWindowLayout::PlanControls(
        in, imeCards, imeCount, ltCards, ltCount, placements);

    CreateControls(wd, SettingsControls::BuildSpecs(in, cardW, imeCards, imeCount, ltCards, ltCount));
    if (plan.setTotal) wd->totalContentHeight = plan.totalHeight;
    if (plan.populate) PopulateControls(wd);

    wd->scrollPos = 0;
    UpdateScrollInfo(wd);
    InvalidateRect(wd->hContentArea, nullptr, TRUE);
}
// Fill the controls from the snapshot (SettingsPopulate.*).
static HWND FindControlThunk(void* ctx, SettingsControlId id)
{
    return SettingsWindow::FindControl(static_cast<SettingsWindow::WindowData*>(ctx), id);
}

void SettingsWindow::PopulateControls(WindowData* wd)
{
    // The reverse-conversion providers are enumerated at startup and
    // filtered by LoadConfig in SwitchMode.
    std::vector<SettingsPopulate::ReverseEntry> reverse;
    CDIMEArray<LanguageProfileInfo>* infoList = CConfig::GetReverseConvervsionInfoList();
    if (infoList) {
        for (UINT i = 0; i < infoList->Count(); i++) {
            reverse.push_back({ infoList->GetAt(i)->description,
                IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), infoList->GetAt(i)->guidProfile) != FALSE });
        }
    }
    SettingsPopulate::Inputs in = {};
    in.snapshot = &wd->snapshot;
    in.windows1809OrLater = Global::isWindows1809OrLater != FALSE;
    in.reverseIsNull = IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL) != FALSE;
    in.reverse = reverse.data();
    in.reverseCount = (int)reverse.size();
    SettingsPopulate::Populate(in, &FindControlThunk, wd);
}

// ============================================================================
// Window class registration
// ============================================================================
bool SettingsWindow::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = SETTINGS_WND_CLASS;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DIMESETTINGS));
    wcex.hIconSm = wcex.hIcon;
    return RegisterClassExW(&wcex) != 0;
}

static void RegisterContentClass(HINSTANCE hInstance)
{
    if (s_contentClassRegistered) return;
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = SettingsWindow::ContentWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = CONTENT_WND_CLASS;
    RegisterClassExW(&wcex);
    s_contentClassRegistered = true;
}

// ============================================================================
// Run — main entry point
// ============================================================================
int SettingsWindow::Run(HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode)
{
    // Initialize GDI+ for anti-aliased toggle switches
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    RegisterWindowClass(hInstance);
    UINT dpi = 96;
    { HDC hdc = GetDC(nullptr); if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(nullptr, hdc); } }

    WCHAR caption[128];
    StringCchPrintfW(caption, 128, L"DIME 設定 v%d.%d.%d", BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT);

    HWND hWnd = CreateWindowExW(0, SETTINGS_WND_CLASS, caption,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ScaleForDpi(g_geo.windowDefaultW, dpi), ScaleForDpi(g_geo.windowDefaultH, dpi),
        nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return -1;

    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (wd) {
        wd->hInstance = hInstance;
        SwitchMode(hWnd, wd, initialMode);
    }
    // Restore saved window placement, or use default (CW_USEDEFAULT + nCmdShow)
    if (!RestoreWindowPlacement(hWnd))
        ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

// ============================================================================
// Main window procedure
// ============================================================================
LRESULT CALLBACK SettingsWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE:
    {
        wd = new WindowData();
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)wd);
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
        wd->hInstance = hInst;
        wd->isDarkTheme = CConfig::IsSystemDarkTheme();
        UINT dpi = CConfig::GetDpiForHwnd(hWnd);
        CreateThemeBrushes(wd);
        CreateFonts(wd, dpi);
        if (wd->isDarkTheme) {
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
        }
        // Match title bar color to content background (DWMWA_CAPTION_COLOR = 35)
        COLORREF cbg = wd->theme.contentBg;
        DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
        wd->micaActive = false; // Don't use Mica — it makes title bar translucent

        RegisterContentClass(hInst);
        RECT rc; GetClientRect(hWnd, &rc);
        int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
        wd->hContentArea = CreateWindowExW(0, CONTENT_WND_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            sidebarW, 0, rc.right - sidebarW, rc.bottom,
            hWnd, (HMENU)IDC_CONTENT_AREA, hInst, nullptr);
        SetWindowLongPtr(wd->hContentArea, GWLP_USERDATA, (LONG_PTR)wd);
        if (wd->isDarkTheme)
            SetWindowTheme(wd->hContentArea, L"DarkMode_Explorer", nullptr);
        return 0;
    }

    case WM_SIZE:
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_SIZE;
            ev.x = LOWORD(lParam);
            ev.y = HIWORD(lParam);
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);
        HDC hdc = hdcScreen;
        RECT rcWnd; GetClientRect(hWnd, &rcWnd);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBmpMem = CreateCompatibleBitmap(hdcScreen, rcWnd.right, rcWnd.bottom);
        HBITMAP hOldBmpMem = (HBITMAP)SelectObject(hdcMem, hBmpMem);
        if (hdcMem) hdc = hdcMem;
        if (wd) {
            SettingsWindowPaint::PaintContext main = MakePaintContext(hWnd, hWnd, wd);
            SettingsWindowPaint::PaintMainFrame(hdc, main, wd->hContentArea != nullptr);
        }
        if (hdcMem) {
            BitBlt(hdcScreen, 0, 0, rcWnd.right, rcWnd.bottom, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOldBmpMem);
            DeleteObject(hBmpMem);
            DeleteDC(hdcMem);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (wd && wd->hContentArea) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_WHEEL;
            ev.delta = GET_WHEEL_DELTA_WPARAM(wParam);
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    {
        // Sidebar click / hover, breadcrumb back link, narrow-mode hamburger
        if (!wd) break;
        SettingsNav::NavEvent ev = {};
        ev.kind = (msg == WM_LBUTTONDOWN) ? SettingsNav::EV_MAIN_LBUTTON : SettingsNav::EV_MAIN_MOUSEMOVE;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        RunNav(hWnd, wd, ev);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_MOUSELEAVE;
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_THEMECHANGED:
        if (wd) UpdateTheme(hWnd, wd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        UINT dpi2 = 96;
        HDC hdc2 = GetDC(nullptr);
        if (hdc2) { dpi2 = GetDeviceCaps(hdc2, LOGPIXELSY); ReleaseDC(nullptr, hdc2); }
        mmi->ptMinTrackSize = SettingsNav::MinTrackSize(dpi2);
        return 0;
    }

    case WM_COPYDATA:
    {
        if (!wd) break;
        COPYDATASTRUCT* pCds = (COPYDATASTRUCT*)lParam;
        if (pCds && pCds->lpData) {
            const wchar_t* cmdLine = (const wchar_t*)pCds->lpData;
            const wchar_t* modeStr = wcsstr(cmdLine, L"--mode ");
            if (modeStr) {
                modeStr += 7;
                wchar_t modeName[32] = {};
                for (int j = 0; modeStr[j] && modeStr[j] != L' ' && j < 31; j++)
                    modeName[j] = modeStr[j];
                IME_MODE mode = SettingsModel::StringToImeMode(modeName);
                if (mode != IME_MODE::IME_MODE_NONE)
                    SwitchMode(hWnd, wd, mode);
            }
            SetForegroundWindow(hWnd);
        }
        return TRUE;
    }

    case WM_SETTINGCHANGE:
    {
        if (!wd) break;
        // Detect system theme change (dark/light mode)
        bool wasDark = wd->isDarkTheme;
        wd->isDarkTheme = CConfig::IsSystemDarkTheme();
        if (wasDark != wd->isDarkTheme) {
            UpdateTheme(hWnd, wd);
            // Re-apply dark mode to title bar
            BOOL useDark = wd->isDarkTheme ? TRUE : FALSE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
            // Update title bar color
            COLORREF cbg = wd->theme.contentBg;
            DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
            // Dark mode scrollbar
            if (wd->hContentArea)
                SetWindowTheme(wd->hContentArea, wd->isDarkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
            // Rebuild to re-apply dark theme to child controls (combos, edits)
            RebuildContentArea(hWnd, wd);
        }
        return 0;
    }

    case WM_DPICHANGED:
    {
        if (!wd) break;
        UINT newDpi = HIWORD(wParam);
        // Resize window to suggested rect
        RECT* prcNew = (RECT*)lParam;
        if (prcNew)
            SetWindowPos(hWnd, nullptr, prcNew->left, prcNew->top,
                prcNew->right - prcNew->left, prcNew->bottom - prcNew->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        // Recreate fonts for new DPI
        DestroyFonts(wd);
        CreateFonts(wd, newDpi);
        // Rebuild content with new DPI-scaled positions
        RebuildContentArea(hWnd, wd);
        // Update title bar color
        COLORREF cbg = wd->theme.contentBg;
        DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }

    case WM_DESTROY:
        SaveWindowPlacement(hWnd);
        if (wd) {
            SettingsModel::DestroyEngine(wd->pEngine);
            DestroyThemeBrushes(wd);
            DestroyFonts(wd);
            for (auto& ch : wd->controlHandles) { if (ch.hWnd) DestroyWindow(ch.hWnd); }
            delete wd;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================================
// WM_COMMAND: SettingsCommand::Handle with the window as its Host
// ============================================================================
void SettingsWindow::HandleCommand(HWND hWnd, WindowData* wd, WPARAM wParam, LPARAM lParam)
{
    // A local class has the member function's access to SettingsWindow.
    class WindowHost : public SettingsCommand::Host {
    public:
        HWND hWnd;          // content area
        WindowData* wd;
        HWND hSender;       // the control that sent WM_COMMAND
        WindowHost(HWND h, WindowData* w, HWND sender) : hWnd(h), wd(w), hSender(sender) {}

        HWND Editor() { return FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR); }

        int LogPixelsY() override
        {
            HDC hdcScr = GetDC(hWnd);
            int lpY = GetDeviceCaps(hdcScr, LOGPIXELSY);
            ReleaseDC(hWnd, hdcScr);
            return lpY;
        }
        bool PickFont(LOGFONTW& lf, int& pointSize10) override
        {
            bool ok = false;
            HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
            if (hDlgLib) {
                auto pCF = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSEFONTW)>(GetProcAddress(hDlgLib, "ChooseFontW"));
                if (pCF) {
                    CHOOSEFONTW cf = {};
                    cf.lStructSize = sizeof(cf);
                    cf.hwndOwner = GetParent(hWnd);
                    cf.lpLogFont = &lf;
                    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
                    if (pCF(&cf)) { pointSize10 = cf.iPointSize; ok = true; }
                }
                FreeLibrary(hDlgLib);
            }
            return ok;
        }
        bool PickColor(COLORREF& rgb) override
        {
            static COLORREF custColors[16] = {};
            bool ok = false;
            HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
            if (hDlgLib) {
                auto pCC = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSECOLORW)>(GetProcAddress(hDlgLib, "ChooseColorW"));
                if (pCC) {
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = GetParent(hWnd);
                    cc.rgbResult = rgb;
                    cc.lpCustColors = custColors;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (pCC(&cc)) { rgb = cc.rgbResult; ok = true; }
                }
                FreeLibrary(hDlgLib);
            }
            return ok;
        }
        bool OpenFileDialog(int filter, std::wstring& path) override
        {
            bool ok = false;
            HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
            if (hDlgLib) {
                auto pGetOpen = reinterpret_cast<_T_GetOpenFileName>(GetProcAddress(hDlgLib, "GetOpenFileNameW"));
                if (pGetOpen) {
                    WCHAR pathToLoad[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(OPENFILENAMEW);
                    ofn.hwndOwner = GetParent(hWnd);
                    ofn.lpstrFile = pathToLoad;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    ofn.lpstrFilter = (filter == 0) ? L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0"
                                                    : L"詞庫文字檔(*.txt)\0*.txt\0\0";
                    if (pGetOpen(&ofn)) { path = pathToLoad; ok = true; }
                }
                FreeLibrary(hDlgLib);
            }
            return ok;
        }
        bool SaveFileDialog(std::wstring& path) override
        {
            bool ok = false;
            HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
            if (hDlgLib) {
                auto pGetSave = reinterpret_cast<_T_GetSaveFileName>(GetProcAddress(hDlgLib, "GetSaveFileNameW"));
                if (pGetSave) {
                    WCHAR pathToWrite[MAX_PATH] = {};
                    StringCchCopyW(pathToWrite, MAX_PATH, path.c_str());
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(OPENFILENAMEW);
                    ofn.hwndOwner = GetParent(hWnd);
                    ofn.lpstrFile = pathToWrite;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT;
                    ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
                    if (pGetSave(&ofn)) { path = pathToWrite; ok = true; }
                }
                FreeLibrary(hDlgLib);
            }
            return ok;
        }
        int ShowMessage(const std::wstring& text, const wchar_t* caption, UINT type) override
        {
            return ThemedMessageBox(GetParent(hWnd), text.c_str(), caption, type);
        }
        std::wstring AppDataPath() override
        {
            WCHAR buf[MAX_PATH] = {};
            Global::GetAppDataPath(buf, TRUE);
            return buf;
        }
        std::wstring DocumentsPath() override
        {
            WCHAR buf[MAX_PATH] = {};
            SHGetSpecialFolderPathW(nullptr, buf, CSIDL_PERSONAL, FALSE);
            return buf;
        }
        std::wstring CustomTableTxtPath(int mode) override
        {
            WCHAR buf[MAX_PATH] = {};
            GetCustomTableTxtPath((IME_MODE)mode, buf, MAX_PATH);
            return buf;
        }
        std::wstring CustomTableCinPath(int mode) override
        {
            WCHAR buf[MAX_PATH] = {};
            GetCustomTableCINPath((IME_MODE)mode, buf, MAX_PATH);
            return buf;
        }
        bool WriteBomFile(const std::wstring& path, const std::wstring& text) override
        {
            HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0;
            WCHAR bom = 0xFEFF;
            WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
            WriteFile(hFile, text.c_str(), (DWORD)(text.size() * sizeof(WCHAR)), &written, nullptr);
            CloseHandle(hFile);
            return true;
        }
        bool LoadTextFile(const std::wstring& path, std::wstring& text) override
        {
            size_t wlen = 0;
            LPWSTR buf = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &wlen);
            if (!buf) return false;
            text.assign(buf, wlen);
            delete[] buf;
            return true;
        }
        bool ParseCin(const std::wstring& src, const std::wstring& dst, bool customTable) override
        {
            return CConfig::parseCINFile(src.c_str(), dst.c_str(), customTable ? TRUE : FALSE) != FALSE;
        }
        void ResetAllDefaults(int mode) override { CConfig::ResetAllDefaults((IME_MODE)mode); }
        void WriteConfig(int mode) override { CConfig::WriteConfig((IME_MODE)mode); }
        void LoadConfig(int mode) override { CConfig::LoadConfig((IME_MODE)mode); }
        SettingsSnapshot LoadSnapshot() override { return SettingsModel::LoadFromConfig(); }
        void ApplyAndSave() override { SettingsWindow::ApplyAndSave(wd); }
        void SetColorMode(int m) override { CConfig::SetColorMode((IME_COLOR_MODE)m); }
        void LoadColorsForMode(int m) override { CConfig::LoadColorsForMode((IME_COLOR_MODE)m); }
        void GetColors(COLORREF out[6]) override
        {
            const ColorInfo* clrs = CConfig::GetColors();
            for (int i = 0; i < 6; i++) out[i] = clrs[i].color;
        }
        void SetColor(int index, COLORREF color) override
        {
            const_cast<ColorInfo*>(CConfig::GetColors())[index].color = color;
        }
        void SaveColorsForMode(int m) override { CConfig::SaveColorsForMode((IME_COLOR_MODE)m); }
        void SetReverseConversionSelection(int sel) override { CConfig::SetReverseConversionSelection(sel); }
        void Rebuild() override { RebuildContentArea(GetParent(hWnd), wd); }
        void InvalidateContent(bool erase) override { InvalidateRect(hWnd, nullptr, erase ? TRUE : FALSE); }
        void UpdateContent() override { UpdateWindow(hWnd); }
        void InvalidateSwatch(int) override { InvalidateRect(hSender, nullptr, TRUE); }
        bool EditorExists() override { return Editor() != nullptr; }
        std::wstring EditorText() override
        {
            HWND hRE = Editor();
            if (!hRE) return {};
            DWORD dwLen = GetWindowTextLengthW(hRE);
            std::wstring text(dwLen + 1, L'\0');
            GetWindowTextW(hRE, &text[0], dwLen + 1);
            text.resize(dwLen);
            return text;
        }
        void SetEditorTextMuted(const std::wstring& text) override
        {
            HWND hRE = Editor();
            if (!hRE) return;
            LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
            SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);
            SetWindowTextW(hRE, text.c_str());
            wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
            wd->ctLastEditedLine = -1;
            wd->ctKeystrokeCount = 0;
            SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
        }
        int EditorLineIndex(int line) override
        {
            HWND hRE = Editor();
            return hRE ? (int)(LONG)SendMessageW(hRE, EM_LINEINDEX, (WPARAM)line, 0) : -1;
        }
        void EditorFocusAt(int cp) override
        {
            HWND hRE = Editor();
            if (!hRE) return;
            // SetFocus first: the subclass's WM_SETFOCUS resets the selection.
            SetFocus(hRE);
            SendMessageW(hRE, EM_SETSEL, (WPARAM)cp, (LPARAM)cp);
            SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
            ::ValidateViewport(hRE, wd);
        }
        void ValidateViewport() override
        {
            HWND hRE = Editor();
            if (hRE) ::ValidateViewport(hRE, wd);
        }
        void CancelBgScan() override {}   // no background scan exists any more
        void EnableSave(bool enable) override { EnableSaveButton(wd, enable ? TRUE : FALSE); }
        void UpdateLineCount() override
        {
            HWND hRE = Editor();
            if (hRE) wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
        }
        int ValidateBuffer(const std::wstring& text, int maxReport, int& firstErrorLine,
            std::wstring& summary) override
        {
            wchar_t errSummary[1024] = {};
            int first = 0;
            int count = ValidateCustomTableBuffer(text.c_str(), text.size(), wd->currentMode,
                wd->snapshot.maxCodes, wd->pEngine, &first, errSummary, _countof(errSummary), maxReport);
            firstErrorLine = first;
            summary = errSummary;
            return count;
        }
    };

    SettingsCommand::CommandEvent ev = {};
    ev.notifyCode = HIWORD(wParam);
    ev.ctrlId = LOWORD(wParam);
    ev.mode = (int)wd->currentMode;
    HWND hSender = (HWND)lParam;
    if (ev.notifyCode == CBN_SELCHANGE && hSender) {
        ev.comboSel = (int)SendMessage(hSender, CB_GETCURSEL, 0, 0);
        if (ev.comboSel != CB_ERR)
            ev.comboItemData = (int)SendMessage(hSender, CB_GETITEMDATA, (WPARAM)ev.comboSel, 0);
    }
    if (ev.notifyCode == EN_CHANGE && ev.ctrlId - 3000 == CTRL_MAX_CODES && hSender)
        GetWindowTextW(hSender, ev.editText, 16);

    WindowHost host(hWnd, wd, hSender);
    SettingsCommand::Handle(host, wd->snapshot, wd->colorGridExpanded, ev);
}

// ============================================================================
// Content area window procedure
// ============================================================================
LRESULT CALLBACK SettingsWindow::ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);
        if (wd) {
            // Double-buffer: paint to off-screen bitmap, then BitBlt to screen
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            HDC hdc = CreateCompatibleDC(hdcScreen);
            HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, rcClient.right, rcClient.bottom);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);

            // The open narrow sidebar is painted over the page: hide the child controls.
            if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
                for (auto& ch : wd->controlHandles)
                    if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
            }
            SettingsWindowPaint::PaintContext content = MakePaintContext(hWnd, GetParent(hWnd), wd);
            SettingsWindowPaint::PaintContext main = MakePaintContext(GetParent(hWnd), GetParent(hWnd), wd);
            SettingsWindowPaint::PaintContentFrame(hdc, content, main);
            // Blit off-screen buffer to screen (excludes child control areas via WS_CLIPCHILDREN)
            BitBlt(hdcScreen, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
            SelectObject(hdc, hOldBmp);
            DeleteObject(hBmp);
            DeleteDC(hdc);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    {
        if (!wd) break;
        SettingsNav::NavEvent ev = {};
        switch (msg) {
        case WM_VSCROLL:
            ev.kind = SettingsNav::EV_CONTENT_VSCROLL;
            ev.delta = LOWORD(wParam);
            ev.thumb = HIWORD(wParam);
            break;
        case WM_MOUSEWHEEL:
            ev.kind = SettingsNav::EV_CONTENT_WHEEL;
            ev.delta = GET_WHEEL_DELTA_WPARAM(wParam);
            break;
        case WM_MOUSEMOVE:
            ev.kind = SettingsNav::EV_CONTENT_MOUSEMOVE;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            break;
        default:
            ev.kind = SettingsNav::EV_CONTENT_LBUTTON;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            break;
        }
        RunNav(GetParent(hWnd), wd, ev);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_CONTENT_MOUSELEAVE;
            RunNav(GetParent(hWnd), wd, ev);
        }
        break;
    }

    case WM_COMMAND:
        if (!wd) break;
        HandleCommand(hWnd, wd, wParam, lParam);
        break;

    case WM_USER + 1:  // Deferred trigger from subclass after Enter/Space/Paste/large-delete
    {
        if (!wd) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (hRE) {
            ValidateViewport(hRE, wd);
            EnableSaveButton(wd, TRUE);
            wd->ctKeystrokeCount = 0;
        }
        return 0;
    }

    case WM_USER + 3:  // Forwarded from subclass on WM_VSCROLL / WM_MOUSEWHEEL
    {
        if (!wd) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (hRE) ValidateViewport(hRE, wd);   // newly-visible lines need painting
        return 0;
    }

    case WM_DRAWITEM:
    {
        if (!wd) break;
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pdis) break;

        // Owner-draw buttons (Win11 style: anti-aliased rounded corners via GDI+)
        if (pdis->CtlType == ODT_BUTTON) {
            UINT dpiBtn = CConfig::GetDpiForHwnd(GetParent(hWnd));
            bool pressed  = (pdis->itemState & ODS_SELECTED) != 0;
            bool disabled = (pdis->itemState & ODS_DISABLED) != 0;
            wchar_t txt[128] = {};
            GetWindowTextW(pdis->hwndItem, txt, 128);
            SettingsWindowPaint::PaintOwnerButton(pdis->hDC, pdis->rcItem, txt, pressed, disabled,
                wd->theme, wd->hFontBody, dpiBtn);
            return TRUE;
        }

        // Color swatches
        if (pdis->CtlType == ODT_STATIC && pdis->CtlID >= 4000 && pdis->CtlID < 4006) {
            int idx = pdis->CtlID - 4000;
            COLORREF colors[] = {
                wd->snapshot.itemColor, wd->snapshot.selectedColor, wd->snapshot.itemBGColor,
                wd->snapshot.phraseColor, wd->snapshot.numberColor, wd->snapshot.selectedBGColor
            };
            int r = ScaleForDpi(g_geo.swatchRadius, CConfig::GetDpiForHwnd(GetParent(hWnd)));
            SettingsWindowPaint::PaintSwatch(pdis->hDC, pdis->rcItem, colors[idx], wd->theme.cardBorder, r);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        if (!wd || !wd->isDarkTheme) break;
        HDC hdcCtrl = (HDC)wParam;
        SetTextColor(hdcCtrl, wd->theme.textPrimary);
        SetBkColor(hdcCtrl, wd->theme.cardBg);
        if (!wd->hBrushCardBg) wd->hBrushCardBg = CreateSolidBrush(wd->theme.cardBg);
        return (LRESULT)wd->hBrushCardBg;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
