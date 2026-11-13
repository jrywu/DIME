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
#include "SettingsOwnerDraw.h"
#include "SettingsWinHost.h"
#include "SettingsProc.h"
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
    SettingsEditor::EditorContext ctx = { (int)wd->Mode(), wd->snapshot.maxCodes,
        wd->isDark ? 1 : 0, wd->pEngine };
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
    wd->theme = ComputeThemeColors(wd->isDark != 0);
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

// Forward declarations
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi);

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
    CConfig::WriteConfig(wd->Mode());
}

static void RegisterContentClass(HINSTANCE hInstance);

// ============================================================================
// Navigation, frame, mode switch, rebuild: SettingsDriver decides, this
// carries out. hMain is the main window.
// ============================================================================
class SettingsWindow::DriverHost : public SettingsDriver::Host {
public:
    HWND hMain;
    WindowData* wd;
    DriverHost(HWND main, WindowData* w) : hMain(main), wd(w) {}

    HWND Content() const { return wd->hContentArea; }
    HWND ControlHwnd(int i) const
    {
        return (i >= 0 && i < (int)wd->controlHandles.size()) ? wd->controlHandles[i].hWnd : nullptr;
    }

    UINT MainDpi() override { return CConfig::GetDpiForHwnd(hMain); }
    UINT ContentDpi() override { return CConfig::GetDpiForHwnd(Content()); }
    bool HasContent() override { return Content() != nullptr; }
    void MainClient(int& w, int& h) override
    {
        RECT rc; GetClientRect(hMain, &rc);
        w = rc.right;
        h = rc.bottom;
    }
    int ContentClientHeight() override
    {
        RECT rc = {};
        if (Content()) GetClientRect(Content(), &rc);
        return rc.bottom;
    }
    int VScrollWidth() override { return GetSystemMetrics(SM_CXVSCROLL); }
    bool SystemDark() override { return CConfig::IsSystemDarkTheme() != FALSE; }
    int ColorMode() override { return wd->snapshot.colorMode; }
    bool SaveEnabled() override
    {
        HWND hSave = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
        return hSave && IsWindowEnabled(hSave);
    }
    int PreviewHeight(UINT dpi) override
    {
        if (!Content()) return 0;
        HDC hdc = GetDC(Content());
        int h = CalcCandidatePreviewH(hdc, wd->snapshot, dpi);
        ReleaseDC(Content(), hdc);
        return h;
    }

    int ControlCount() override { return (int)wd->controlHandles.size(); }
    SettingsDriver::ControlInfo Control(int i) override
    {
        SettingsDriver::ControlInfo c = {};
        if (i < 0 || i >= (int)wd->controlHandles.size()) return c;
        const auto& ch = wd->controlHandles[i];
        bool alive = ch.hWnd && IsWindow(ch.hWnd);
        c.id = (int)ch.id;
        c.exists = ch.hWnd ? 1 : 0;
        c.alive = alive ? 1 : 0;
        c.visible = (alive && IsWindowVisible(ch.hWnd)) ? 1 : 0;
        c.x = ch.origX;
        c.y = ch.origY;
        c.w = ch.origW;
        c.h = ch.origH;
        return c;
    }
    void MoveControl(int i, int x, int y, int w, int h) override
    {
        if (HWND hc = ControlHwnd(i)) SetWindowPos(hc, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void ShowControl(int i, bool show) override
    {
        if (HWND hc = ControlHwnd(i)) ShowWindow(hc, show ? SW_SHOW : SW_HIDE);
    }
    void ResetControlRegion(int i) override
    {
        if (HWND hc = ControlHwnd(i)) SetWindowRgn(hc, nullptr, TRUE);
    }
    bool ControlChecked(int i) override
    {
        HWND hc = ControlHwnd(i);
        return hc && SendMessage(hc, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void SetControlChecked(int i, bool on) override
    {
        if (HWND hc = ControlHwnd(i)) SendMessage(hc, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void DestroyControls() override
    {
        for (auto& ch : wd->controlHandles)
            if (ch.hWnd) DestroyWindow(ch.hWnd);
        wd->controlHandles.clear();
    }
    void CreateControls(const SettingsWindowLayout::PlanInput& in, int cardW) override
    {
        int imeCount = 0;
        const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        SettingsWindow::CreateControls(wd, SettingsControls::BuildSpecs(in, cardW, imeCards, imeCount, ltCards, ltCount));
    }
    void Populate() override { PopulateControls(wd); }

    void InvalidateMain(const RECT* rc, bool erase) override { InvalidateRect(hMain, rc, erase ? TRUE : FALSE); }
    void InvalidateContent(bool erase) override
    {
        if (Content()) InvalidateRect(Content(), nullptr, erase ? TRUE : FALSE);
    }
    void TrackLeave(bool content) override
    {
        HWND target = content ? Content() : hMain;
        if (!target) return;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, target };
        TrackMouseEvent(&tme);
    }
    void SetScrollInfo(int total, int page, int pos) override
    {
        if (!Content()) return;
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total, (UINT)page, pos };
        ::SetScrollInfo(Content(), SB_VERT, &si, TRUE);
    }
    void RedrawContent() override
    {
        if (Content()) RedrawWindow(Content(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    void UpdateContent() override
    {
        if (Content()) UpdateWindow(Content());
    }
    void MoveContent(int x, int y, int w, int h) override
    {
        if (Content()) MoveWindow(Content(), x, y, w, h, FALSE);
    }
    void FocusContent() override
    {
        if (Content()) SetFocus(Content());
    }
    void CreateContent(int x, int y, int w, int h) override
    {
        RegisterContentClass(wd->hInstance);
        wd->hContentArea = CreateWindowExW(0, CONTENT_WND_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            x, y, w, h, hMain, (HMENU)IDC_CONTENT_AREA, wd->hInstance, nullptr);
        SetWindowLongPtr(wd->hContentArea, GWLP_USERDATA, (LONG_PTR)wd);
    }
    void ContentTheme(bool dark) override
    {
        if (Content()) SetWindowTheme(Content(), dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    }
    void SetMainPos(int x, int y, int w, int h) override
    {
        SetWindowPos(hMain, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetDarkTitle(bool dark) override
    {
        BOOL useDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hMain, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
    }
    void SetCaptionColor(int attribute) override
    {
        COLORREF cbg = wd->theme.contentBg;
        DwmSetWindowAttribute(hMain, (DWORD)attribute, &cbg, sizeof(COLORREF));
    }
    void ThemeBrushes() override { CreateThemeBrushes(wd); }
    void CreateFonts(int dpi) override { SettingsWindow::CreateFonts(wd, dpi); }
    void DestroyFonts() override { SettingsWindow::DestroyFonts(wd); }

    int AskSaveBeforeBack() override
    {
        return ThemedMessageBox(hMain,
            L"自建詞庫尚未儲存，是否要儲存後離開？\n\n[是] 儲存並離開\n[否] 不儲存離開\n[取消] 繼續編輯",
            L"自建詞庫未儲存", MB_YESNOCANCEL | MB_ICONQUESTION);
    }
    void ClickSave() override
    {
        HWND hSave = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
        SendMessageW(Content(), WM_COMMAND, MAKEWPARAM(3000 + CTRL_CUSTOM_TABLE_SAVE, BN_CLICKED), (LPARAM)hSave);
    }

    void ToggleSnapshot(int id, bool on) override
    {
        SettingsModel::SyncToggleToSnapshot(wd->snapshot, (SettingsControlId)id, on);
    }
    void ApplyAndSave() override { SettingsWindow::ApplyAndSave(wd); }
    void EnumerateReverseProviders() override { CConfig::EnumerateReverseConversionProviders(1028); }
    void LoadConfig(int mode) override { CConfig::LoadConfig((IME_MODE)mode); }
    void LoadColors() override { CConfig::LoadColorsForMode((IME_COLOR_MODE)CConfig::GetColorMode()); }
    void LoadSnapshot() override { wd->snapshot = SettingsModel::LoadFromConfig(); }
    void RecreateEngine(int mode) override
    {
        SettingsModel::DestroyEngine(wd->pEngine);
        wd->pEngine = SettingsModel::CreateEngine((IME_MODE)mode);
    }
};

void SettingsWindow::RunNav(HWND hMain, WindowData* wd, SettingsNav::NavEvent ev)
{
    DriverHost host(hMain, wd);
    SettingsDriver::Nav(host, wd, ev);
}

void SettingsWindow::RunFrame(HWND hWnd, WindowData* wd, SettingsFrame::FrameEvent ev)
{
    DriverHost host(hWnd, wd);
    SettingsDriver::Frame(host, wd, ev);
}

void SettingsWindow::SwitchMode(HWND hWnd, WindowData* wd, IME_MODE mode)
{
    DriverHost host(hWnd, wd);
    SettingsDriver::SwitchMode(host, wd, (int)mode);
}

void SettingsWindow::RebuildContentArea(HWND hWnd, WindowData* wd)
{
    DriverHost host(hWnd, wd);
    SettingsDriver::Rebuild(host, wd);
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
    ctx.cardW = CalcCardW(hMain, ctx.dpi, !wd->nav.sidebarNarrowMode && !wd->nav.sidebarCollapsed);
    ctx.sysFace = GetSysColor(COLOR_3DFACE);
    ctx.selectedModeIndex = wd->nav.selectedModeIndex;
    ctx.hoverSidebarIndex = wd->nav.hoverSidebarIndex;
    ctx.navLevel = wd->nav.navLevel;
    ctx.currentCardIndex = wd->nav.currentCardIndex;
    ctx.hoverRowIndex = wd->nav.hoverRowIndex;
    ctx.currentMode = (int)wd->Mode();
    ctx.scrollPos = wd->nav.scrollPos;
    ctx.sidebarNarrowMode = wd->nav.sidebarNarrowMode ? 1 : 0;
    ctx.sidebarCollapsed = wd->nav.sidebarCollapsed ? 1 : 0;
    ctx.colorGridExpanded = wd->nav.colorGridExpanded ? 1 : 0;
    memcpy(ctx.cardExpanded, wd->nav.cardExpanded, sizeof(ctx.cardExpanded));
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
    if (wd->isDark) {
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
    GetCustomTableTxtPath(wd->Mode(), txtPath, MAX_PATH);
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
        if (h && (wd->isDark != 0) && spec.darkTheme == SettingsControls::DARK_CFD)
            SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        else if (h && (wd->isDark != 0) && spec.darkTheme == SettingsControls::DARK_EXPLORER)
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        AddControl(wd, (SettingsControlId)spec.ctrlId, h, spec.add.left, spec.add.top, spec.add.right, spec.add.bottom, spec.visH);
    }
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
    SIZE initial = SettingsFrame::InitialSize(dpi);

    WCHAR caption[128];
    StringCchPrintfW(caption, 128, L"DIME 設定 v%d.%d.%d", BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT);

    HWND hWnd = CreateWindowExW(0, SETTINGS_WND_CLASS, caption,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initial.cx, initial.cy,
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
// Window procedures: SettingsProc decides, ProcHost carries out.
// ============================================================================
LRESULT CALLBACK SettingsWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return Dispatch(hWnd, false, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return Dispatch(hWnd, true, msg, wParam, lParam);
}

LRESULT SettingsWindow::Dispatch(HWND hWnd, bool content, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // A local class has the member function's access to SettingsWindow.
    class ProcHost : public SettingsProc::Host {
    public:
        HWND hWnd;          // the window receiving the message
        HWND hMain;         // the main window
        WindowData* wd;
        ProcHost(HWND h, HWND main, WindowData* w) : hWnd(h), hMain(main), wd(w) {}

        bool HasData() override { return wd != nullptr; }
        bool HasContent() override { return wd && wd->hContentArea != nullptr; }
        void Create(HINSTANCE hInst) override
        {
            wd = new WindowData();
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)wd);
            wd->hInstance = hInst;
            wd->micaActive = false; // Don't use Mica — it makes title bar translucent
            RECT rc; GetClientRect(hWnd, &rc);
            SettingsFrame::FrameEvent ev = {};
            ev.kind = SettingsFrame::EV_CREATE;
            ev.dpi = CConfig::GetDpiForHwnd(hWnd);
            ev.clientW = rc.right;
            ev.clientH = rc.bottom;
            RunFrame(hWnd, wd, ev);
        }
        void Nav(int kind, int x, int y, int delta, int thumb) override
        {
            SettingsNav::NavEvent ev = {};
            ev.kind = kind;
            ev.x = x;
            ev.y = y;
            ev.delta = delta;
            ev.thumb = thumb;
            RunNav(hMain, wd, ev);
        }
        void Frame(int kind)
        {
            SettingsFrame::FrameEvent ev = {};
            ev.kind = kind;
            ev.dpi = CConfig::GetDpiForHwnd(hWnd);
            RunFrame(hWnd, wd, ev);
        }
        void ThemeChanged() override { Frame(SettingsFrame::EV_THEME_CHANGED); }
        void SettingChanged() override { Frame(SettingsFrame::EV_SETTING_CHANGE); }
        void DpiChanged(UINT dpi, const RECT* suggested) override
        {
            SettingsFrame::FrameEvent ev = {};
            ev.kind = SettingsFrame::EV_DPI_CHANGED;
            ev.dpi = dpi;
            if (suggested) {
                ev.hasRect = 1;
                ev.rc = *suggested;
            }
            RunFrame(hWnd, wd, ev);
        }
        void PaintMain() override
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
        }
        void PaintContent() override
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
                if (wd->nav.sidebarNarrowMode && !wd->nav.sidebarCollapsed) {
                    for (auto& ch : wd->controlHandles)
                        if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
                }
                SettingsWindowPaint::PaintContext contentCtx = MakePaintContext(hWnd, hMain, wd);
                SettingsWindowPaint::PaintContext mainCtx = MakePaintContext(hMain, hMain, wd);
                SettingsWindowPaint::PaintContentFrame(hdc, contentCtx, mainCtx);
                // Blit off-screen buffer to screen (excludes child control areas via WS_CLIPCHILDREN)
                BitBlt(hdcScreen, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
                SelectObject(hdc, hOldBmp);
                DeleteObject(hBmp);
                DeleteDC(hdc);
            }
            EndPaint(hWnd, &ps);
        }
        UINT ScreenDpi() override
        {
            UINT dpi = 96;
            HDC hdc = GetDC(nullptr);
            if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(nullptr, hdc); }
            return dpi;
        }
        void SwitchMode(int mode) override { SettingsWindow::SwitchMode(hWnd, wd, (IME_MODE)mode); }
        void SetForeground() override { SetForegroundWindow(hWnd); }
        void Destroy() override
        {
            SaveWindowPlacement(hWnd);
            if (wd) {
                SettingsModel::DestroyEngine(wd->pEngine);
                DestroyThemeBrushes(wd);
                DestroyFonts(wd);
                for (auto& ch : wd->controlHandles) { if (ch.hWnd) DestroyWindow(ch.hWnd); }
                delete wd;
                wd = nullptr;
                SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
            }
        }
        void Quit() override { PostQuitMessage(0); }
        void Command(WPARAM wParam, LPARAM lParam) override { HandleCommand(hWnd, wd, wParam, lParam); }
        void EditorRevalidate() override
        {
            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
            if (hRE) {
                ValidateViewport(hRE, wd);
                EnableSaveButton(wd, TRUE);
                wd->ctKeystrokeCount = 0;
            }
        }
        void EditorViewport() override
        {
            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
            if (hRE) ValidateViewport(hRE, wd);   // newly-visible lines need painting
        }
        bool DrawItem(const DRAWITEMSTRUCT* pdis) override
        {
            SettingsOwnerDraw::Context ctx = {};
            ctx.theme = wd->theme;
            ctx.fontBody = wd->hFontBody;
            ctx.dpi = CConfig::GetDpiForHwnd(hMain);
            const SettingsSnapshot& sn = wd->snapshot;
            COLORREF swatch[6] = { sn.itemColor, sn.selectedColor, sn.itemBGColor,
                sn.phraseColor, sn.numberColor, sn.selectedBGColor };
            memcpy(ctx.swatch, swatch, sizeof(swatch));
            return SettingsOwnerDraw::DrawItem(pdis, ctx);
        }
        bool CtlColor(HDC hdc, LRESULT& brush) override
        {
            if (!SettingsOwnerDraw::CtlColor(hdc, (wd->isDark != 0), wd->theme, &wd->hBrushCardBg)) return false;
            brush = (LRESULT)wd->hBrushCardBg;
            return true;
        }
        LRESULT Default(UINT msg, WPARAM wParam, LPARAM lParam) override
        {
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
    };

    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    ProcHost host(hWnd, content ? GetParent(hWnd) : hWnd, wd);
    return content ? SettingsProc::Content(host, msg, wParam, lParam)
                   : SettingsProc::Main(host, msg, wParam, lParam);
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

        // Win32 calls: SettingsWinHost. CConfig and the editor's viewport stay here.
        int LogPixelsY() override { return SettingsWinHost::LogPixelsY(hWnd); }
        bool PickFont(LOGFONTW& lf, int& pointSize10) override
        {
            return SettingsWinHost::PickFont(GetParent(hWnd), lf, pointSize10);
        }
        bool PickColor(COLORREF& rgb) override { return SettingsWinHost::PickColor(GetParent(hWnd), rgb); }
        bool OpenFileDialog(int filter, std::wstring& path) override
        {
            return SettingsWinHost::OpenFileDialog(GetParent(hWnd), filter, path);
        }
        bool SaveFileDialog(std::wstring& path) override
        {
            return SettingsWinHost::SaveFileDialog(GetParent(hWnd), path);
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
        std::wstring DocumentsPath() override { return SettingsWinHost::DocumentsPath(); }
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
            return SettingsWinHost::WriteBomFile(path, text);
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
            return hRE ? SettingsWinHost::EditorText(hRE) : std::wstring();
        }
        void SetEditorTextMuted(const std::wstring& text) override
        {
            HWND hRE = Editor();
            if (!hRE) return;
            wd->ctLastLineCount = SettingsWinHost::SetEditorTextMuted(hRE, text);
            wd->ctLastEditedLine = -1;
            wd->ctKeystrokeCount = 0;
        }
        int EditorLineIndex(int line) override
        {
            HWND hRE = Editor();
            return hRE ? SettingsWinHost::EditorLineIndex(hRE, line) : -1;
        }
        void EditorFocusAt(int cp) override
        {
            HWND hRE = Editor();
            if (!hRE) return;
            SettingsWinHost::EditorFocusAt(hRE, cp);
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
            if (hRE) wd->ctLastLineCount = SettingsWinHost::EditorLineCount(hRE);
        }
        int ValidateBuffer(const std::wstring& text, int maxReport, int& firstErrorLine,
            std::wstring& summary) override
        {
            wchar_t errSummary[1024] = {};
            int first = 0;
            int count = ValidateCustomTableBuffer(text.c_str(), text.size(), wd->Mode(),
                wd->snapshot.maxCodes, wd->pEngine, &first, errSummary, _countof(errSummary), maxReport);
            firstErrorLine = first;
            summary = errSummary;
            return count;
        }
    };

    SettingsCommand::CommandEvent ev = {};
    ev.notifyCode = HIWORD(wParam);
    ev.ctrlId = LOWORD(wParam);
    ev.mode = (int)wd->Mode();
    HWND hSender = (HWND)lParam;
    if (ev.notifyCode == CBN_SELCHANGE && hSender) {
        ev.comboSel = (int)SendMessage(hSender, CB_GETCURSEL, 0, 0);
        if (ev.comboSel != CB_ERR)
            ev.comboItemData = (int)SendMessage(hSender, CB_GETITEMDATA, (WPARAM)ev.comboSel, 0);
    }
    if (ev.notifyCode == EN_CHANGE && ev.ctrlId - 3000 == CTRL_MAX_CODES && hSender)
        GetWindowTextW(hSender, ev.editText, 16);

    WindowHost host(hWnd, wd, hSender);
    SettingsCommand::Handle(host, wd->snapshot, wd->nav.colorGridExpanded, ev);
}

