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
#include "SettingsChildren.h"
#include "SettingsShell.h"
#include "SettingsApp.h"
#include "SettingsDriver.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

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

// What the next window created on this thread uses (SettingsWindow::Run /
// CreateForTest set it before CreateWindowEx; WM_CREATE takes it).
struct PendingWindow {
    SettingsApp::Host* app;
    const wchar_t* contentClass;
    const wchar_t* placementKey;
    const wchar_t* placementValue;
};
static thread_local const PendingWindow* s_pending = nullptr;

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
        wd->isDark ? 1 : 0, wd->app->Engine() };
    SettingsEditor::ValidateViewport(hRE, ctx);
}

// Find and enable/disable the Save button.
static void EnableSaveButton(SettingsWindow::WindowData* wd, BOOL enable)
{
    if (!wd) return;
    HWND hSaveBtn = SettingsWindow::FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    if (hSaveBtn) EnableWindow(hSaveBtn, enable);
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
    SettingsShell::Brushes b = SettingsShell::CreateBrushes(wd->theme);
    wd->hBrushSidebarBg = b.sidebarBg;
    wd->hBrushContentBg = b.contentBg;
    wd->hBrushCardBg    = b.cardBg;
}

void SettingsWindow::DestroyThemeBrushes(WindowData* wd)
{
    SettingsShell::Brushes b = { wd->hBrushSidebarBg, wd->hBrushContentBg, wd->hBrushCardBg };
    SettingsShell::DestroyBrushes(b);
    wd->hBrushSidebarBg = b.sidebarBg;
    wd->hBrushContentBg = b.contentBg;
    wd->hBrushCardBg    = b.cardBg;
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
    wd->app->ApplySnapshot(wd->snapshot);
    wd->app->WriteConfig((int)wd->Mode());
}

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

    UINT MainDpi() override { return wd->app->DpiForWindow(hMain); }
    UINT ContentDpi() override { return wd->app->DpiForWindow(Content()); }
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
    bool SystemDark() override { return wd->app->SystemDark(); }
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
        SettingsShell::RegisterContentClass(wd->hInstance, wd->contentClass, SettingsWindow::ContentWndProc);
        wd->hContentArea = CreateWindowExW(0, wd->contentClass, nullptr,
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
        return wd->app->ShowMessage(hMain,
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
    void EnumerateReverseProviders() override { wd->app->EnumerateReverseProviders(); }
    void LoadConfig(int mode) override { wd->app->LoadConfig(mode); }
    void LoadColors() override { wd->app->LoadColorsForMode(wd->app->ColorMode()); }
    void LoadSnapshot() override { wd->snapshot = wd->app->LoadSnapshot(); }
    void RecreateEngine(int mode) override { wd->app->RecreateEngine(mode); }
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
    ctx.dpi = wd->app->DpiForWindow(hMain);
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
}

// ============================================================================
// Child controls of a page: SettingsControls::BuildSpecs decides,
// SettingsChildren creates the windows, the window keeps the list.
// ============================================================================
void SettingsWindow::CreateControls(WindowData* wd, const std::vector<SettingsControls::ControlSpec>& specs)
{
    // A local class has the member function's access to SettingsWindow.
    class ChildHost : public SettingsChildren::Host {
    public:
        WindowData* wd;
        explicit ChildHost(WindowData* w) : wd(w) {}
        void Added(int ctrlId, HWND h, int x, int y, int w, int ht, int visH) override
        {
            AddControl(wd, (SettingsControlId)ctrlId, h, x, y, w, ht, visH);
        }
        std::wstring EditorPath() override
        {
            return wd->app->CustomTableTxtPath((int)wd->Mode());
        }
        void EditorReady(HWND hRE, int lineCount) override
        {
            wd->ctLastLineCount = lineCount;
            wd->ctLastEditedLine = -1;
            wd->ctKeystrokeCount = 0;
            // Paint the visible range now (TOM, no caret jump). The Save
            // button stays disabled: the content matches the file.
            ValidateViewport(hRE, wd);
        }
    };

    SettingsChildren::Context ctx = { wd->hContentArea, wd->hInstance, wd->hFontBody, wd->isDark,
        wd->theme.cardBg, wd->theme.textPrimary };
    ChildHost host(wd);
    SettingsChildren::Create(host, ctx, specs);
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
    SettingsApp::Host* app = wd->app;
    int count = app->ReverseCount();
    std::vector<std::wstring> names;
    for (int i = 0; i < count; i++) names.push_back(app->ReverseDescription(i));
    std::vector<SettingsPopulate::ReverseEntry> reverse;
    for (int i = 0; i < count; i++) reverse.push_back({ names[i].c_str(), app->ReverseIsCurrent(i) });
    SettingsPopulate::Inputs in = {};
    in.snapshot = &wd->snapshot;
    in.windows1809OrLater = app->Windows1809OrLater();
    in.reverseIsNull = app->ReverseIsNull();
    in.reverse = reverse.data();
    in.reverseCount = (int)reverse.size();
    SettingsPopulate::Populate(in, &FindControlThunk, wd);
}

// ============================================================================
// Run — main entry point: SettingsShell runs the window, this sets up its data.
// ============================================================================
int SettingsWindow::Run(HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode)
{
    SettingsApp::ConfigHost app;
    WCHAR caption[128];
    StringCchPrintfW(caption, 128, L"DIME 設定 v%d.%d.%d", BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT);
#ifdef DIME_USE_RUST_CONFIG
    // dime-settings::window owns the window; CConfig and the engine stay here.
    return dime_settings_window_run(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
        static_cast<SettingsApp::Host*>(&app), hInstance, nCmdShow, (int)initialMode,
        reinterpret_cast<const uint16_t*>(caption), IDI_DIMESETTINGS);
#else
    return RunCpp(&app, hInstance, nCmdShow, initialMode, caption);
#endif
}

int SettingsWindow::RunCpp(SettingsApp::Host* app, HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode,
    const wchar_t* caption)
{
    // A local class has the member function's access to SettingsWindow.
    struct Start {
        HINSTANCE instance;
        IME_MODE mode;
        static void Started(void* user, HWND hWnd)
        {
            try {
                Start* st = static_cast<Start*>(user);
                WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
                if (wd) {
                    wd->hInstance = st->instance;
                    SwitchMode(hWnd, wd, st->mode);
                }
            } catch (...) {}
        }
    };
    Start start = { hInstance, initialMode };
    PendingWindow pending = { app, CONTENT_WND_CLASS, SettingsShell::kPlacementKey, SettingsShell::kPlacementValue };
    s_pending = &pending;

    SettingsShell::RunParams p = {};
    p.instance = hInstance;
    p.nCmdShow = nCmdShow;
    p.mainClass = SETTINGS_WND_CLASS;
    p.mainProc = WndProc;
    p.iconId = IDI_DIMESETTINGS;
    p.caption = caption;
    p.placementKey = SettingsShell::kPlacementKey;
    p.placementValue = SettingsShell::kPlacementValue;
    p.started = &Start::Started;
    p.user = &start;
    int result = SettingsShell::Run(p);
    s_pending = nullptr;
    return result;
}

HWND SettingsWindow::CreateForTest(SettingsApp::Host* app, HINSTANCE hInstance, IME_MODE initialMode,
    const wchar_t* caption, const wchar_t* mainClass, const wchar_t* contentClass,
    const wchar_t* placementKey, const wchar_t* placementValue)
{
    // SettingsShell::Run without the placement, ShowWindow, and message loop.
    PendingWindow pending = { app, contentClass, placementKey, placementValue };
    s_pending = &pending;
    SettingsShell::RegisterMainClass(hInstance, mainClass, WndProc, IDI_DIMESETTINGS);
    UINT dpi = 96;
    { HDC hdc = GetDC(nullptr); if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(nullptr, hdc); } }
    SIZE initial = SettingsFrame::InitialSize(dpi);
    HWND hWnd = CreateWindowExW(0, mainClass, caption, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, initial.cx, initial.cy, nullptr, nullptr, hInstance, nullptr);
    s_pending = nullptr;
    if (!hWnd) return nullptr;
    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (wd) {
        wd->hInstance = hInstance;
        SwitchMode(hWnd, wd, initialMode);
    }
    return hWnd;
}

bool SettingsWindow::StateForTest(HWND hMain, SettingsDriver::State* state, SettingsSnapshot* snapshot)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hMain, GWLP_USERDATA);
    if (!wd) return false;
    if (state) *state = *static_cast<SettingsDriver::State*>(wd);
    if (snapshot) *snapshot = wd->snapshot;
    return true;
}

void SettingsWindow::NavigateForTest(HWND hMain, IME_MODE mode, int selectedModeIndex, int navLevel, int cardIndex)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hMain, GWLP_USERDATA);
    if (!wd) return;
    SwitchMode(hMain, wd, mode);
    wd->nav.selectedModeIndex = selectedModeIndex;
    wd->nav.navLevel = navLevel;
    wd->nav.currentCardIndex = cardIndex;
    wd->nav.scrollPos = 0;
    RebuildContentArea(hMain, wd);
    InvalidateRect(hMain, nullptr, FALSE);
}

void SettingsWindow::PaintForTest(HWND hWnd, HDC target, int w, int h, bool content)
{
    // The WM_PAINT bodies (SettingsShell::Paint*Into) with this window's source.
    class Probe {
    public:
        HWND hWnd, hMain;
        WindowData* wd;
        static int HasData(void* u) { return static_cast<Probe*>(u)->wd ? 1 : 0; }
        static void Prepare(void* u, int content, SettingsWindowPaint::PaintContext* first,
            SettingsWindowPaint::PaintContext* second, int* hasContent)
        {
            Probe* p = static_cast<Probe*>(u);
            if (!content) {
                *first = MakePaintContext(p->hWnd, p->hWnd, p->wd);
                *hasContent = p->wd->hContentArea != nullptr ? 1 : 0;
                return;
            }
            // Same as WM_PAINT: the open narrow sidebar hides the child controls.
            if (p->wd->nav.sidebarNarrowMode && !p->wd->nav.sidebarCollapsed) {
                for (auto& ch : p->wd->controlHandles)
                    if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
            }
            *first = MakePaintContext(p->hWnd, p->hMain, p->wd);
            *second = MakePaintContext(p->hMain, p->hMain, p->wd);
        }
    };
    Probe probe = { hWnd, content ? GetParent(hWnd) : hWnd, (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA) };
    SettingsShell::PaintSource src = { &probe, &Probe::HasData, &Probe::Prepare };
    if (content) SettingsShell::PaintContentInto(target, w, h, src);
    else SettingsShell::PaintMainInto(target, w, h, src);
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
            if (s_pending) {
                wd->app = s_pending->app;
                wd->contentClass = s_pending->contentClass;
                wd->placementKey = s_pending->placementKey;
                wd->placementValue = s_pending->placementValue;
            }
            RECT rc; GetClientRect(hWnd, &rc);
            SettingsFrame::FrameEvent ev = {};
            ev.kind = SettingsFrame::EV_CREATE;
            ev.dpi = wd->app->DpiForWindow(hWnd);
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
            ev.dpi = wd->app->DpiForWindow(hWnd);
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
        // WM_PAINT: SettingsShell double-buffers, this builds the contexts.
        static int HasDataThunk(void* user) { return static_cast<ProcHost*>(user)->wd ? 1 : 0; }
        static void PrepareThunk(void* user, int content, SettingsWindowPaint::PaintContext* first,
            SettingsWindowPaint::PaintContext* second, int* hasContent)
        {
            try {
                ProcHost* h = static_cast<ProcHost*>(user);
                WindowData* wd = h->wd;
                if (!content) {
                    *first = MakePaintContext(h->hWnd, h->hWnd, wd);
                    *hasContent = wd->hContentArea != nullptr ? 1 : 0;
                    return;
                }
                // The open narrow sidebar is painted over the page: hide the child controls.
                if (wd->nav.sidebarNarrowMode && !wd->nav.sidebarCollapsed) {
                    for (auto& ch : wd->controlHandles)
                        if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
                }
                *first = MakePaintContext(h->hWnd, h->hMain, wd);
                *second = MakePaintContext(h->hMain, h->hMain, wd);
            } catch (...) {}
        }
        SettingsShell::PaintSource Source() { return { this, &HasDataThunk, &PrepareThunk }; }
        void PaintMain() override { SettingsShell::PaintMain(hWnd, Source()); }
        void PaintContent() override { SettingsShell::PaintContent(hWnd, Source()); }
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
            if (wd) {
                SettingsShell::SavePlacement(hWnd, wd->placementKey, wd->placementValue);
                wd->app->DestroyEngine();
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
            ctx.dpi = wd->app->DpiForWindow(hMain);
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
            return wd->app->ShowMessage(GetParent(hWnd), text.c_str(), caption, type);
        }
        std::wstring AppDataPath() override { return wd->app->AppDataPath(); }
        std::wstring DocumentsPath() override { return SettingsWinHost::DocumentsPath(); }
        std::wstring CustomTableTxtPath(int mode) override { return wd->app->CustomTableTxtPath(mode); }
        std::wstring CustomTableCinPath(int mode) override { return wd->app->CustomTableCinPath(mode); }
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
            return wd->app->ParseCin(src.c_str(), dst.c_str(), customTable);
        }
        void ResetAllDefaults(int mode) override { wd->app->ResetAllDefaults(mode); }
        void WriteConfig(int mode) override { wd->app->WriteConfig(mode); }
        void LoadConfig(int mode) override { wd->app->LoadConfig(mode); }
        SettingsSnapshot LoadSnapshot() override { return wd->app->LoadSnapshot(); }
        void ApplyAndSave() override { SettingsWindow::ApplyAndSave(wd); }
        void SetColorMode(int m) override { wd->app->SetColorMode(m); }
        void LoadColorsForMode(int m) override { wd->app->LoadColorsForMode(m); }
        void GetColors(COLORREF out[6]) override { wd->app->GetColors(out); }
        void SetColor(int index, COLORREF color) override { wd->app->SetColor(index, color); }
        void SaveColorsForMode(int m) override { wd->app->SaveColorsForMode(m); }
        void SetReverseConversionSelection(int sel) override { wd->app->SetReverseSelection(sel); }
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
                wd->snapshot.maxCodes, wd->app->Engine(), &first, errSummary, _countof(errSummary), maxReport);
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

