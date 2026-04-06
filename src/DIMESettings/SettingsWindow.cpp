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

// ============================================================================
// ThemedMessageBox — owner-drawn MessageBox replacement.
// Reads theme colors from ComputeThemeColors() at display time so it
// automatically follows the current system light/dark theme.
// ============================================================================
namespace ThemedMsgBox {
    static const wchar_t WND_CLASS[] = L"DIMEThemedMsgBoxClass";
    static bool s_registered = false;
    static int s_result = 0;

    struct MsgBoxData {
        const wchar_t* text;
        const wchar_t* caption;
        UINT type;
        ThemeColors tc;         // computed from system theme at creation time
        bool isDark;
        int btnCount;
        int btnIds[3];
        const wchar_t* btnLabels[3];
        int defaultBtn;
        int hoverBtn;
        HICON hIcon;
        HFONT hFont;
    };

    struct Layout {
        RECT rcClient;
        int textX, textY, textW, textH;
        int btnY, btnW, btnH, btnGap;
        int iconX, iconY, iconSize;
        int margin;
    };

    static Layout CalcLayout(HWND hWnd, MsgBoxData* md, UINT dpi)
    {
        Layout L = {};
        L.margin = ScaleForDpi(20, dpi);
        L.iconSize = ScaleForDpi(32, dpi);
        int iconGap = ScaleForDpi(14, dpi);
        L.btnH = ScaleForDpi(30, dpi);
        L.btnW = ScaleForDpi(88, dpi);
        L.btnGap = ScaleForDpi(8, dpi);
        L.iconX = L.margin;
        L.iconY = L.margin;
        L.textX = md->hIcon ? (L.margin + L.iconSize + iconGap) : L.margin;

        int maxTextW = ScaleForDpi(340, dpi);
        HDC hdc = GetDC(hWnd);
        HFONT oldF = (HFONT)SelectObject(hdc, md->hFont);
        RECT rcM = { 0, 0, maxTextW, 0 };
        DrawTextW(hdc, md->text, -1, &rcM, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
        SelectObject(hdc, oldF);
        ReleaseDC(hWnd, hdc);

        L.textW = rcM.right;
        L.textH = rcM.bottom;
        if (md->hIcon && L.textH < L.iconSize) L.textH = L.iconSize;
        L.textY = L.margin;

        int contentBottom = L.margin + L.textH + ScaleForDpi(20, dpi);
        L.btnY = contentBottom;
        int totalBtnW = md->btnCount * L.btnW + (md->btnCount - 1) * L.btnGap;
        int clientW = L.textX + L.textW + L.margin;
        int minW = totalBtnW + 2 * L.margin;
        if (clientW < minW) clientW = minW;
        int clientH = L.btnY + L.btnH + L.margin;
        SetRect(&L.rcClient, 0, 0, clientW, clientH);
        return L;
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MsgBoxData* md = (MsgBoxData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

        switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            md = (MsgBoxData*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)md);
            if (md->isDark) {
                BOOL useDark = TRUE;
                DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
            }
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            Layout L = CalcLayout(hWnd, md, dpi);
            RECT rcWnd = L.rcClient;
            AdjustWindowRectEx(&rcWnd, GetWindowLong(hWnd, GWL_STYLE), FALSE, GetWindowLong(hWnd, GWL_EXSTYLE));
            int wndW = rcWnd.right - rcWnd.left, wndH = rcWnd.bottom - rcWnd.top;
            HWND hP = GetParent(hWnd);
            RECT rcP; GetWindowRect(hP ? hP : GetDesktopWindow(), &rcP);
            SetWindowPos(hWnd, nullptr, (rcP.left + rcP.right - wndW) / 2,
                (rcP.top + rcP.bottom - wndH) / 2, wndW, wndH, SWP_NOZORDER);
            return 0;
        }

        case WM_PAINT: {
            if (!md) break;
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            Layout L = CalcLayout(hWnd, md, dpi);
            const ThemeColors& tc = md->tc;

            // Background
            HBRUSH hBg = CreateSolidBrush(tc.contentBg);
            FillRect(hdc, &L.rcClient, hBg);
            DeleteObject(hBg);

            // Icon
            if (md->hIcon)
                DrawIconEx(hdc, L.iconX, L.iconY, md->hIcon, L.iconSize, L.iconSize, 0, nullptr, DI_NORMAL);

            // Text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, tc.textPrimary);
            HFONT oldF = (HFONT)SelectObject(hdc, md->hFont);
            RECT rcText = { L.textX, L.textY, L.textX + L.textW, L.textY + L.textH };
            DrawTextW(hdc, md->text, -1, &rcText, DT_WORDBREAK | DT_LEFT);

            // Buttons — right-aligned
            int totalBtnW = md->btnCount * L.btnW + (md->btnCount - 1) * L.btnGap;
            int btnX = L.rcClient.right - L.margin - totalBtnW;
            for (int i = 0; i < md->btnCount; i++) {
                RECT rcBtn = { btnX, L.btnY, btnX + L.btnW, L.btnY + L.btnH };
                bool isHover = (i == md->hoverBtn);
                bool isDefault = (i == md->defaultBtn);
                COLORREF fill = isHover ? tc.buttonFillHover : tc.buttonFill;
                COLORREF border = isDefault ? tc.accent : tc.buttonBorder;

                HBRUSH hBtn = CreateSolidBrush(fill);
                HPEN hPen = CreatePen(PS_SOLID, 1, border);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hBtn);
                HPEN oldPn = (HPEN)SelectObject(hdc, hPen);
                int r = ScaleForDpi(4, dpi);
                RoundRect(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, r, r);
                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPn);
                DeleteObject(hBtn);
                DeleteObject(hPen);

                SetTextColor(hdc, tc.buttonText);
                DrawTextW(hdc, md->btnLabels[i], -1, &rcBtn, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                btnX += L.btnW + L.btnGap;
            }
            SelectObject(hdc, oldF);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!md) break;
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            Layout L = CalcLayout(hWnd, md, dpi);
            int totalBtnW = md->btnCount * L.btnW + (md->btnCount - 1) * L.btnGap;
            int btnX = L.rcClient.right - L.margin - totalBtnW;
            int newHover = -1;
            for (int i = 0; i < md->btnCount; i++) {
                if (x >= btnX && x < btnX + L.btnW && y >= L.btnY && y < L.btnY + L.btnH)
                    { newHover = i; break; }
                btnX += L.btnW + L.btnGap;
            }
            if (newHover != md->hoverBtn) {
                md->hoverBtn = newHover;
                InvalidateRect(hWnd, nullptr, FALSE);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd };
                TrackMouseEvent(&tme);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (md && md->hoverBtn != -1) { md->hoverBtn = -1; InvalidateRect(hWnd, nullptr, FALSE); }
            return 0;

        case WM_LBUTTONDOWN: {
            if (!md) break;
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            Layout L = CalcLayout(hWnd, md, dpi);
            int totalBtnW = md->btnCount * L.btnW + (md->btnCount - 1) * L.btnGap;
            int btnX = L.rcClient.right - L.margin - totalBtnW;
            for (int i = 0; i < md->btnCount; i++) {
                if (x >= btnX && x < btnX + L.btnW && y >= L.btnY && y < L.btnY + L.btnH) {
                    s_result = md->btnIds[i];
                    DestroyWindow(hWnd);
                    return 0;
                }
                btnX += L.btnW + L.btnGap;
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (!md) break;
            if (wParam == VK_RETURN) {
                s_result = md->btnIds[md->defaultBtn];
                DestroyWindow(hWnd);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                for (int i = 0; i < md->btnCount; i++) {
                    if (md->btnIds[i] == IDCANCEL) { s_result = IDCANCEL; DestroyWindow(hWnd); return 0; }
                    if (md->btnIds[i] == IDNO) { s_result = IDNO; DestroyWindow(hWnd); return 0; }
                }
                s_result = md->btnIds[md->defaultBtn];
                DestroyWindow(hWnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (md) {
                for (int i = 0; i < md->btnCount; i++)
                    if (md->btnIds[i] == IDCANCEL) { s_result = IDCANCEL; break; }
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (md && md->hFont) { DeleteObject(md->hFont); md->hFont = nullptr; }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
} // namespace ThemedMsgBox

static int ThemedMessageBox(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    using namespace ThemedMsgBox;

    if (!s_registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = WND_CLASS;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    MsgBoxData md = {};
    md.text = text;
    md.caption = caption;
    md.type = type;
    md.isDark = CConfig::IsSystemDarkTheme();
    md.tc = ComputeThemeColors(md.isDark);
    md.hoverBtn = -1;
    md.defaultBtn = 0;

    if (type & MB_YESNOCANCEL) {
        md.btnCount = 3;
        md.btnIds[0] = IDYES;   md.btnLabels[0] = L"是(Y)";
        md.btnIds[1] = IDNO;    md.btnLabels[1] = L"否(N)";
        md.btnIds[2] = IDCANCEL; md.btnLabels[2] = L"取消";
    } else if (type & MB_YESNO) {
        md.btnCount = 2;
        md.btnIds[0] = IDYES;   md.btnLabels[0] = L"是(Y)";
        md.btnIds[1] = IDNO;    md.btnLabels[1] = L"否(N)";
    } else if (type & MB_OKCANCEL) {
        md.btnCount = 2;
        md.btnIds[0] = IDOK;    md.btnLabels[0] = L"確定";
        md.btnIds[1] = IDCANCEL; md.btnLabels[1] = L"取消";
    } else {
        md.btnCount = 1;
        md.btnIds[0] = IDOK;    md.btnLabels[0] = L"確定";
    }

    if (type & MB_ICONWARNING)         md.hIcon = LoadIcon(nullptr, IDI_WARNING);
    else if (type & MB_ICONERROR)      md.hIcon = LoadIcon(nullptr, IDI_ERROR);
    else if (type & MB_ICONQUESTION)   md.hIcon = LoadIcon(nullptr, IDI_QUESTION);
    else if (type & MB_ICONINFORMATION) md.hIcon = LoadIcon(nullptr, IDI_INFORMATION);

    UINT dpi = CConfig::GetDpiForHwnd(hParent ? hParent : GetDesktopWindow());
    md.hFont = CreateFontW(-ScaleForDpi(12, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HWND hMsgBox = CreateWindowExW(WS_EX_DLGMODALFRAME,
        WND_CLASS, caption, WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 1, 1, hParent, nullptr, GetModuleHandle(nullptr), &md);

    if (!hMsgBox) {
        if (md.hFont) DeleteObject(md.hFont);
        return MessageBoxW(hParent, text, caption, type);
    }

    if (hParent) EnableWindow(hParent, FALSE);
    ShowWindow(hMsgBox, SW_SHOW);
    UpdateWindow(hMsgBox);
    SetFocus(hMsgBox);

    s_result = IDCANCEL;
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hParent) { EnableWindow(hParent, TRUE); SetForegroundWindow(hParent); }
    return s_result;
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
// ValidateCustomTableRE — view-side wrapper that calls SettingsModel::ValidateLine
// per line and applies CHARFORMAT error colors to the RichEdit control.
// ============================================================================
static BOOL ValidateCustomTableRE(HWND hRE, IME_MODE mode, UINT maxCodes, bool isDark,
    CCompositionProcessorEngine* pEngine = nullptr)
{
    if (!hRE) return TRUE;

    CHARRANGE origSel = {0, 0};
    SendMessageW(hRE, EM_EXGETSEL, 0, (LPARAM)&origSel);

    COLORREF errorFormat = isDark ? CUSTOM_TABLE_DARK_ERROR_FORMAT : CUSTOM_TABLE_LIGHT_ERROR_FORMAT;
    COLORREF errorLength = isDark ? CUSTOM_TABLE_DARK_ERROR_LENGTH : CUSTOM_TABLE_LIGHT_ERROR_LENGTH;
    COLORREF errorChar   = isDark ? CUSTOM_TABLE_DARK_ERROR_CHAR   : CUSTOM_TABLE_LIGHT_ERROR_CHAR;
    COLORREF validColor  = isDark ? CUSTOM_TABLE_DARK_VALID        : CUSTOM_TABLE_LIGHT_VALID;

    CHARFORMAT2W cfClear = {};
    cfClear.cbSize = sizeof(cfClear);
    cfClear.dwMask = CFM_COLOR;
    cfClear.crTextColor = validColor;
    SendMessageW(hRE, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfClear);

    BOOL allOk = TRUE;
    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);

    for (int li = 0; li < lineCount; ++li) {
        LONG start = (LONG)SendMessageW(hRE, EM_LINEINDEX, (WPARAM)li, 0);
        if (start == -1) continue;
        LONG len = (LONG)SendMessageW(hRE, EM_LINELENGTH, (WPARAM)start, 0);
        if (len <= 0) continue;

        std::vector<WCHAR> linebuf((size_t)max((LONG)len, 1L) + 1);
        TEXTRANGEW tr;
        tr.chrg.cpMin = start;
        tr.chrg.cpMax = start + len;
        tr.lpstrText = linebuf.data();
        LRESULT got = SendMessageW(hRE, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        if (got <= 0) continue;

        auto result = SettingsModel::ValidateLine(linebuf.data(), (int)got, mode, maxCodes, pEngine);
        if (!result.valid) {
            allOk = FALSE;
            for (const auto& err : result.errors) {
                COLORREF color;
                switch (err.error) {
                case SettingsModel::LineError::Format:     color = errorFormat; break;
                case SettingsModel::LineError::KeyTooLong: color = errorLength; break;
                case SettingsModel::LineError::InvalidChar: color = errorChar;  break;
                default: continue;
                }
                SendMessageW(hRE, EM_SETSEL, (WPARAM)(start + err.errorStart),
                    (LPARAM)(start + err.errorStart + err.errorLen));
                CHARFORMAT2W cf = {};
                cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = color;
                SendMessageW(hRE, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            }
        }
    }

    SendMessageW(hRE, EM_EXSETSEL, 0, (LPARAM)&origSel);
    SendMessageW(hRE, EM_SETSEL, (WPARAM)origSel.cpMin, (LPARAM)origSel.cpMin);
    SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
    return allOk;
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

// Subclass proc for the custom table RichEdit:
// - Prevents select-all on focus
// - Posts WM_USER+1 to parent (hContentArea) for deferred validation on Enter/Space/Paste/large-delete
static LRESULT CALLBACK CustomTableRE_SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    if (uMsg == WM_SETFOCUS) {
        CHARRANGE cr = {0, 0};
        SendMessageW(hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hWnd, EM_SCROLLCARET, 0, 0);
    } else if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
        } else if (wParam == VK_DELETE || wParam == VK_BACK) {
            CHARRANGE sel;
            SendMessageW(hWnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMax - sel.cpMin > CUSTOM_TABLE_LARGE_DELETION_THRESHOLD)
                PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
        }
    } else if (uMsg == WM_PASTE) {
        PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
    } else if (uMsg == WM_MOUSEWHEEL) {
        // Cursor-aware routing: if cursor is over the RichEdit, let it scroll internally;
        // otherwise forward to the outer content area scrollbar.
        POINT pt;
        GetCursorPos(&pt);
        RECT rcRE;
        GetWindowRect(hWnd, &rcRE);
        if (!PtInRect(&rcRE, pt))
            return SendMessageW(GetParent(hWnd), uMsg, wParam, lParam);
        // Cursor is inside RichEdit → fall through to DefSubclassProc (RichEdit scrolls)
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, CustomTableRE_SubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
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
static HFONT MakeFont(int ptSize, int dpi, int weight)
{
    return CreateFontW(-MulDiv(ptSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft JhengHei UI");
}

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
    wd->hFontTitle      = MakeFont(g_geo.fontTitlePt, dpi, FW_SEMIBOLD);
    wd->hFontCardHeader = MakeFont(g_geo.fontCardHeaderPt, dpi, FW_NORMAL);
    wd->hFontBody       = MakeFont(g_geo.fontBodyPt, dpi, FW_NORMAL);
    wd->hFontDesc       = MakeFont(g_geo.fontDescPt, dpi, FW_NORMAL);
    // Segoe MDL2 Assets — small for chevrons
    wd->hFontMDL2 = CreateFontW(-MulDiv(g_geo.fontBodyPt - 2, dpi, 72), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // Segoe MDL2 Assets — large for row icons (sized to rowIconSize)
    wd->hFontMDL2Icon = CreateFontW(-ScaleForDpi(g_geo.rowIconSize, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // Check if MDL2 is actually available (Win10+). If not, fallback to text chevrons.
    wd->hasMDL2 = false;
    if (wd->hFontMDL2) {
        HDC hdcChk = GetDC(nullptr);
        HFONT hOld = (HFONT)SelectObject(hdcChk, wd->hFontMDL2);
        WCHAR faceName[LF_FACESIZE] = {};
        GetTextFaceW(hdcChk, LF_FACESIZE, faceName);
        wd->hasMDL2 = (wcscmp(faceName, L"Segoe MDL2 Assets") == 0);
        SelectObject(hdcChk, hOld);
        ReleaseDC(nullptr, hdcChk);
    }
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
    for (auto& ch : wd->controlHandles) {
        if (ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0))
            SetWindowPos(ch.hWnd, nullptr, ch.origX, ch.origY - wd->scrollPos,
                ch.origW, ch.origH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// Calculate candidate preview height using same metrics as CCandidateWindow::_DrawList
// Calculate candidate preview height using a DC for accurate CJK font metrics.
// Draw card background with subtle Win11-style bottom shadow using GDI+
// Shadow color adapts to theme: dark shadow on light bg, light shadow on dark bg
static void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg,
    bool isHover, COLORREF hoverColor)
{
    // Shadow only in light mode — Win11 dark cards have no shadow
    int lum = (299 * GetRValue(contentBg) + 587 * GetGValue(contentBg) + 114 * GetBValue(contentBg)) / 1000;
    bool bgIsLight = (lum > 128);

    if (bgIsLight) {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        Gdiplus::REAL r = (Gdiplus::REAL)cornerR;
        int expand = 0;
        int yOff = 1;
        Gdiplus::GraphicsPath sp;
        Gdiplus::RectF srf((Gdiplus::REAL)(x - expand), (Gdiplus::REAL)(y - expand + yOff),
            (Gdiplus::REAL)(w + 2 * expand), (Gdiplus::REAL)(h + 2 * expand));
        sp.AddArc(srf.X, srf.Y, r, r, 180, 90);
        sp.AddArc(srf.X + srf.Width - r, srf.Y, r, r, 270, 90);
        sp.AddArc(srf.X + srf.Width - r, srf.Y + srf.Height - r, r, r, 0, 90);
        sp.AddArc(srf.X, srf.Y + srf.Height - r, r, r, 90, 90);
        sp.CloseFigure();
        Gdiplus::SolidBrush sb(Gdiplus::Color(20, 0, 0, 0));
        g.FillPath(&sb, &sp);
    } // end bgIsLight

    // Card background
    HPEN hPen = CreatePen(PS_SOLID, 1, cardBorder);
    HBRUSH hBr = CreateSolidBrush(cardBg);
    SelectObject(hdc, hPen); SelectObject(hdc, hBr);
    RoundRect(hdc, x, y, x + w, y + h, cornerR, cornerR);
    DeleteObject(hBr); DeleteObject(hPen);

    // Hover overlay
    if (isHover) {
        HBRUSH hHov = CreateSolidBrush(hoverColor);
        HPEN hHovPen = CreatePen(PS_SOLID, 1, hoverColor);
        SelectObject(hdc, hHov); SelectObject(hdc, hHovPen);
        RoundRect(hdc, x, y, x + w, y + h, cornerR, cornerR);
        DeleteObject(hHov); DeleteObject(hHovPen);
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
static void PaintCandidatePreviewCard(HDC hdc, const SettingsSnapshot& s, int margin, int curY, int cardW, int totalH, UINT dpi);
static int CalcRowHeight(HDC hdc, const LayoutRow& lr, const SettingsSnapshot& s, UINT dpi);

int SettingsWindow::HitTestCardList(int y, const LayoutCard* cards, int cardCount,
    int modeMask, const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
    int curY2 = ScaleForDpi(g_geo.topMargin, dpi) + pad - scrollPos;
    for (int i = 0; i < cardCount; i++) {
        bool vis = (cards[i].rowCount == 0);
        if (!vis) for (int r = 0; !vis && r < cards[i].rowCount; r++)
            if (IsRowVisible(cards[i].rows[r], modeMask)) vis = true;
        if (!vis) continue;
        int numCh = 0;
        int expIdx = expandBaseIdx + i;
        if (cards[i].action == RowAction::ExpandSection && cardExpanded[expIdx] && cards[i].rows)
            for (int r = 0; r < cards[i].rowCount; r++)
                if (IsRowVisible(cards[i].rows[r], modeMask)) numCh++;
        int cardH = 2 * pad + catRowH + (numCh > 0 ? numCh * (cRowH + 1) : 0);
        if (y >= curY2 - pad && y < curY2 - pad + cardH) return i;
        curY2 += cardH - 2 * pad + 2 * pad + cardGap;
    }
    return -1;
}

bool SettingsWindow::HandleCardListClick(int y, HWND hWnd, HWND hParent, WindowData* wd,
    const LayoutCard* cards, int cardCount, int modeMask, int expandBaseIdx, UINT dpi)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
    int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad - wd->scrollPos;

    for (int i = 0; i < cardCount; i++) {
        bool vis = (cards[i].rowCount == 0);
        if (!vis) for (int r = 0; !vis && r < cards[i].rowCount; r++)
            if (IsRowVisible(cards[i].rows[r], modeMask)) vis = true;
        if (!vis) continue;

        int expIdx = expandBaseIdx + i;

        // Click on category row?
        if (y >= curY && y < curY + catRowH) {
            if (cards[i].action == RowAction::NavigateToCard) {
                SettingsWindow::NavigateToCategory(hParent, wd, i);
            } else if (cards[i].action == RowAction::ExpandSection) {
                wd->cardExpanded[expIdx] = !wd->cardExpanded[expIdx];
                wd->scrollPos = 0;
                SettingsWindow::RebuildContentArea(hParent, wd);
                SettingsWindow::UpdateScrollInfo(wd);
                InvalidateRect(hWnd, nullptr, FALSE);
                UpdateWindow(hWnd);
            }
            return true;
        }

        // Click on expanded children?
        if (cards[i].action == RowAction::ExpandSection && wd->cardExpanded[expIdx] && cards[i].rows) {
            int childY = curY + catRowH + pad;
            int numCh = 0;
            for (int r = 0; r < cards[i].rowCount; r++) {
                const LayoutRow& lr = cards[i].rows[r];
                if (!IsRowVisible(lr, modeMask)) continue;
                childY += 1; // 1px separator
                if (y >= childY && y < childY + cRowH) {
                    if (lr.type == RowType::Toggle) {
                        HWND hCtrl = FindControl(wd, lr.id);
                        if (hCtrl) {
                            BOOL checked = (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
                            bool newState = !checked;
                            SendMessage(hCtrl, BM_SETCHECK, newState ? BST_CHECKED : BST_UNCHECKED, 0);
                            SettingsModel::SyncToggleToSnapshot(wd->snapshot, lr.id, newState);
                            SettingsWindow::ApplyAndSave(wd);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                        }
                    }
                    return true;
                }
                childY += cRowH;
                numCh++;
            }
            int totalCardH = 2 * pad + catRowH + numCh * (cRowH + 1);
            curY += totalCardH + cardGap;
        } else {
            curY += catRowH + 2 * pad + cardGap;
        }
    }
    return false;
}

bool SettingsWindow::HandleRowCardClick(int y, HWND hWnd, WindowData* wd,
    const LayoutCard& card, UINT dpi)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
    HDC hdcTmp = GetDC(hWnd);
    auto calcH = [&](const LayoutRow& lr) { return CalcRowHeight(hdcTmp, lr, wd->snapshot, dpi); };
    auto isShown = [&](const LayoutRow& lr) -> bool {
        if (!IsRowVisible(lr, modeMask)) return false;
        if (lr.expandParent != CTRL_NONE && lr.expandParent == CTRL_COLOR_MODE
            && (wd->snapshot.colorMode != 3 || !wd->colorGridExpanded)) return false;
        return true;
    };

    int cardTop = ScaleForDpi(g_geo.topPad, dpi) - wd->scrollPos;
    bool handled = false;
    for (int r = 0; card.rows && r < card.rowCount; r++) {
        const LayoutRow& lr = card.rows[r];
        if (!isShown(lr)) continue;
        if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
            int h = calcH(lr);
            int endR = r + 1;
            while (endR < card.rowCount && card.rows[endR].expandParent != CTRL_NONE
                   && !card.rows[endR].cardBreakBefore) {
                if (isShown(card.rows[endR])) h += calcH(card.rows[endR]);
                endR++;
            }
            int cardH = h + 2 * pad;
            if (y >= cardTop && y < cardTop + cardH) {
                int curY = cardTop + pad;
                for (int ri = r; ri < endR; ri++) {
                    if (!isShown(card.rows[ri])) continue;
                    int thisH = calcH(card.rows[ri]);
                    if (y >= curY && y < curY + thisH) {
                        if (card.rows[ri].action == RowAction::ExpandSection
                            && card.rows[ri].type == RowType::ComboBox && wd->snapshot.colorMode == 3) {
                            wd->colorGridExpanded = !wd->colorGridExpanded;
                            SettingsWindow::RebuildContentArea(GetParent(hWnd), wd);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                            handled = true; break;
                        }
                        if (card.rows[ri].type == RowType::Toggle) {
                            HWND hCtrl = FindControl(wd, card.rows[ri].id);
                            if (hCtrl) {
                                BOOL checked = (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
                                bool newState = !checked;
                                SendMessage(hCtrl, BM_SETCHECK, newState ? BST_CHECKED : BST_UNCHECKED, 0);
                                SettingsModel::SyncToggleToSnapshot(wd->snapshot, card.rows[ri].id, newState);
                                SettingsWindow::ApplyAndSave(wd);
                                InvalidateRect(hWnd, nullptr, FALSE);
                                UpdateWindow(hWnd);
                            }
                            handled = true; break;
                        }
                    }
                    curY += thisH;
                }
                if (!handled) handled = true; // clicked in card but not on actionable row
                break;
            }
            cardTop += cardH + cardGap;
            r = endR - 1;
        }
    }
    ReleaseDC(hWnd, hdcTmp);
    return handled;
}

int SettingsWindow::HitTestRowCards(int y, HWND hWnd, WindowData* wd,
    const LayoutCard& card, UINT dpi)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
    HDC hdcTmp = GetDC(hWnd);
    auto calcH = [&](const LayoutRow& lr) { return CalcRowHeight(hdcTmp, lr, wd->snapshot, dpi); };
    auto isShown = [&](const LayoutRow& lr) -> bool {
        if (!IsRowVisible(lr, modeMask)) return false;
        if (lr.expandParent != CTRL_NONE && lr.expandParent == CTRL_COLOR_MODE
            && (wd->snapshot.colorMode != 3 || !wd->colorGridExpanded)) return false;
        return true;
    };
    int cardTop = ScaleForDpi(g_geo.topPad, dpi) - wd->scrollPos;
    int result = -1;
    for (int r = 0; r < card.rowCount; r++) {
        const LayoutRow& lr = card.rows[r];
        if (!isShown(lr)) continue;
        if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
            int h = calcH(lr);
            int endR = r + 1;
            while (endR < card.rowCount && card.rows[endR].expandParent != CTRL_NONE
                   && !card.rows[endR].cardBreakBefore) {
                if (isShown(card.rows[endR])) h += calcH(card.rows[endR]);
                endR++;
            }
            int totalH = h + 2 * pad;
            if (y >= cardTop && y < cardTop + totalH) { result = r; break; }
            cardTop += totalH + cardGap;
            r = endR - 1;
        }
    }
    ReleaseDC(hWnd, hdcTmp);
    return result;
}

// ============================================================================
// Shared geometry helpers — used by ALL paint paths and RebuildContentArea
// ============================================================================

// Card width from main window — stable regardless of scrollbar visibility
static int CalcCardW(HWND hMainWnd, UINT dpi, bool sidebarTakesSpace)
{
    RECT rcMain; GetClientRect(hMainWnd, &rcMain);
    int sidebarW = sidebarTakesSpace ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int sbW = GetSystemMetrics(SM_CXVSCROLL);
    return rcMain.right - sidebarW - 2 * margin - sbW;
}

// Right edge X for controls (combo, button, toggle, swatch) — always left of chevron area
static int CalcCtrlRight(int margin, int cardW, UINT dpi, bool hasChevron)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int ctrlRM = ScaleForDpi(g_geo.controlRightMargin, dpi);
    int chevArea = hasChevron ? ScaleForDpi(g_geo.chevronW + 4, dpi) : 0;
    return margin + cardW - pad - ctrlRM - chevArea;
}

// Row height for any RowType
static int CalcRowHeight(HDC hdc, const LayoutRow& lr, const SettingsSnapshot& s, UINT dpi)
{
    if (lr.type == RowType::CandidatePreview)
        return CalcCandidatePreviewH(hdc, s, dpi);
    if (lr.type == RowType::ColorGrid) {
        int swS = ScaleForDpi(g_geo.swatchSize, dpi);
        int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
        return 2 * (swS + swGap);
    }
    if (lr.type == RowType::RichEditor)
        return ScaleForDpi(g_geo.editorCardH, dpi);
    return lr.description ? ScaleForDpi(g_geo.rowHeight, dpi) : ScaleForDpi(g_geo.rowHeightSingle, dpi);
}

// Both RebuildContentArea and PaintSettingsDetail must call this so controls
// are positioned at the same Y as the painted preview bottom.
// Uses GetTextExtentPoint32 — same API as PaintSettingsDetail's actual drawing code.
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    HFONT hF = CreateFontW(-MulDiv(s.fontSize, dpi, 72), 0, 0, 0,
        s.fontWeight, s.fontItalic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, s.fontFaceName);
    if (!hF) return ScaleForDpi(172, dpi);
    HFONT hOld = (HFONT)SelectObject(hdc, hF);
    WCHAR test[] = L"國國國國";
    SIZE sz; GetTextExtentPoint32(hdc, test, 4, &sz);
    int cyLine = sz.cy * 5 / 4;  // must match PaintSettingsDetail's cyLine
    int fistLineOffset = cyLine / 3;
    int candWndH = 6 * cyLine + fistLineOffset + cyLine / 3;
    SelectObject(hdc, hOld);
    DeleteObject(hF);
    return candWndH + ScaleForDpi(g_geo.previewGap, dpi);
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
    // Re-enumerate reverse conversion providers before LoadConfig filters out self
    CConfig::EnumerateReverseConversionProviders(1028);
    CConfig::LoadConfig(mode);
    wd->snapshot = SettingsModel::LoadFromConfig();
    // Recreate composition engine for the new mode (custom table validation)
    SettingsModel::DestroyEngine(wd->pEngine);
    wd->pEngine = SettingsModel::CreateEngine(mode);
    RebuildContentArea(hWnd, wd);
    InvalidateRect(hWnd, nullptr, TRUE);
}

void SettingsWindow::NavigateToCategory(HWND hWnd, WindowData* wd, int cardIndex)
{
    wd->navLevel = 1;
    wd->currentCardIndex = cardIndex;
    wd->scrollPos = 0;
    RebuildContentArea(hWnd, wd);
    InvalidateRect(hWnd, nullptr, TRUE);
    if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
}

void SettingsWindow::NavigateBack(HWND hWnd, WindowData* wd)
{
    wd->navLevel = 0;
    wd->scrollPos = 0;
    RebuildContentArea(hWnd, wd);
    InvalidateRect(hWnd, nullptr, TRUE);
    if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
}

// Stub implementations — these will be properly written in subsequent steps
void SettingsWindow::PaintSidebar(HWND hWnd, HDC hdc, WindowData* wd)
{
    if (wd->sidebarCollapsed) return;
    RECT rcClient; GetClientRect(hWnd, &rcClient);
    UINT dpi = CConfig::GetDpiForHwnd(hWnd);
    const ThemeColors& tc = wd->theme;
    int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
    int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
    int itemR = ScaleForDpi(g_geo.sidebarItemR, dpi);
    int iconSize = ScaleForDpi(g_geo.sidebarIconSize, dpi);
    int iconGap = ScaleForDpi(g_geo.sidebarIconGap, dpi);
    int padL = ScaleForDpi(g_geo.sidebarItemPadLeft, dpi);

    // Background
    RECT rcSidebar = { 0, 0, sidebarW, rcClient.bottom };
    FillRect(hdc, &rcSidebar, wd->hBrushSidebarBg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, wd->hFontBody);

    int sidebarCount = 0;
    const SidebarItemDef* items = ::GetSidebarItems(&sidebarCount);
    int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);

    for (int i = 0; i < sidebarCount; i++) {
        // Separator
        if (items[i].iconResourceId == -1) {
            int sepY = startY + i * itemH + itemH / 2;
            HPEN hSep = CreatePen(PS_SOLID, 1, tc.cardBorder);
            HPEN oldPen = (HPEN)SelectObject(hdc, hSep);
            MoveToEx(hdc, padL, sepY, nullptr);
            LineTo(hdc, sidebarW - padL, sepY);
            SelectObject(hdc, oldPen);
            DeleteObject(hSep);
            continue;
        }

        int itemPadX = ScaleForDpi(g_geo.sidebarItemR, dpi); // 4px inset for hover/select rounded rect
        RECT rcItem = { itemPadX, startY + i * itemH, sidebarW - itemPadX, startY + (i + 1) * itemH };
        bool isSelected = (i == wd->selectedModeIndex);

        // Highlight
        if (isSelected || i == wd->hoverSidebarIndex) {
            COLORREF hlColor = isSelected ? tc.sidebarSelect : tc.sidebarHover;
            HBRUSH hBr = CreateSolidBrush(hlColor);
            HPEN hPen = CreatePen(PS_SOLID, 1, hlColor);
            SelectObject(hdc, hBr); SelectObject(hdc, hPen);
            RoundRect(hdc, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, itemR * 2, itemR * 2);
            DeleteObject(hBr); DeleteObject(hPen);
        }

        // Accent bar
        if (isSelected) {
            int barW = ScaleForDpi(g_geo.sidebarAccentBarW, dpi);
            int barH = ScaleForDpi(g_geo.sidebarAccentBarH, dpi);
            int barX = rcItem.left + ScaleForDpi(g_geo.sidebarItemR, dpi);
            int barY = rcItem.top + (itemH - barH) / 2;
            int barR = ScaleForDpi(g_geo.sidebarAccentBarR, dpi);
            HBRUSH hAcc = CreateSolidBrush(tc.accent);
            HRGN hRgn = CreateRoundRectRgn(barX, barY, barX + barW + 1, barY + barH + 1, barR, barR);
            FillRgn(hdc, hRgn, hAcc);
            DeleteObject(hRgn); DeleteObject(hAcc);
        }

        // Icon from .ico
        int iconX = rcItem.left + padL;
        int iconY = rcItem.top + (itemH - iconSize) / 2;
        if (items[i].iconResourceId > 0) {
            HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(items[i].iconResourceId),
                IMAGE_ICON, iconSize, iconSize, 0);
            if (hIcon) {
                DrawIconEx(hdc, iconX, iconY, hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                DestroyIcon(hIcon);
            }
        }

        // Text
        SetTextColor(hdc, tc.textPrimary);
        RECT rcText = rcItem;
        rcText.left = iconX + iconSize + iconGap;
        DrawTextW(hdc, items[i].label, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    // Hamburger is drawn in ContentWndProc — not here
    SelectObject(hdc, oldFont);
}
void SettingsWindow::PaintContent(HWND hWnd, HDC hdc, WindowData* wd)
{
    RECT rc; GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, wd->hBrushContentBg);

    if (wd->selectedModeIndex == 5) {
        // 載入碼表 page — uses shared PaintCardList renderer
        int ltPageCount = 0;
        const LayoutCard* ltPage = GetLoadTablePageLayout(&ltPageCount);
        // expandBaseIdx=4: cardExpanded[4+i] stores expand states (avoids overlap with IME mode cards 0-3)
        PaintCardList(hWnd, hdc, wd, L"載入碼表", ltPage, ltPageCount, SM_MODE_ALL, 4);
    } else if (wd->navLevel == 0) {
        PaintCategoryList(hWnd, hdc, wd);
    } else {
        PaintSettingsDetail(hWnd, hdc, wd);
    }

    // Hamburger is drawn in main WndProc WM_PAINT (above content area, all levels)
}
void SettingsWindow::PaintCategoryList(HWND hWnd, HDC hdc, WindowData* wd)
{
    int sidebarCount = 0;
    const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
    const wchar_t* title = (wd->selectedModeIndex < sidebarCount && sItems[wd->selectedModeIndex].label)
        ? sItems[wd->selectedModeIndex].label : L"DIME";
    int layoutCount = 0;
    const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
    int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
    PaintCardList(hWnd, hdc, wd, title, cards, layoutCount, modeMask, 0);
}

void SettingsWindow::PaintCardList(HWND hWnd, HDC hdc, WindowData* wd,
    const wchar_t* pageTitle, const LayoutCard* cards, int layoutCount,
    int modeMask, int expandBaseIdx)
{
    RECT rcClient; GetClientRect(hWnd, &rcClient);
    UINT dpi = CConfig::GetDpiForHwnd(GetParent(hWnd));
    const ThemeColors& tc = wd->theme;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cornerR = ScaleForDpi(g_geo.cardRadius, dpi);
    int sepIndent = ScaleForDpi(g_geo.rowSepIndent, dpi);
    int iconSz = ScaleForDpi(g_geo.rowIconSize, dpi);
    int iconPadL = ScaleForDpi(g_geo.rowIconPadLeft, dpi);
    int iconGap = ScaleForDpi(g_geo.rowIconGap, dpi);
    int rowH = ScaleForDpi(g_geo.rowHeight, dpi);
    int rowHS = ScaleForDpi(g_geo.rowHeightSingle, dpi);

    SetBkMode(hdc, TRANSPARENT);

    // Section title
    HFONT oldFont = (HFONT)SelectObject(hdc, wd->hFontTitle);
    SetTextColor(hdc, tc.textPrimary);
    int titleLeft0 = (wd->sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    int titleY, titleB;
    if (wd->sidebarNarrowMode) {
        titleY = -wd->scrollPos;  // align with hamburger top (0)
        titleB = ScaleForDpi(g_geo.hamburgerSize, dpi) - wd->scrollPos;
    } else {
        titleY = ScaleForDpi(g_geo.topPad, dpi) - wd->scrollPos;
        titleB = titleY + ScaleForDpi(g_geo.titleAreaH, dpi);
    }
    RECT rcTitle = { titleLeft0, titleY, rcClient.right - margin, titleB };
    DrawTextW(hdc, pageTitle, -1, &rcTitle, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    // Win11 style: each category row is its own card with spacing between
    int cardW = CalcCardW(GetParent(hWnd), dpi, !wd->sidebarNarrowMode && !wd->sidebarCollapsed);
    int cardGapDpi = ScaleForDpi(g_geo.cardGap, dpi);
    int curY = ScaleForDpi(g_geo.topMargin, dpi) - wd->scrollPos;

    for (int i = 0; i < layoutCount; i++) {
        bool vis = (cards[i].rowCount == 0);
        if (!vis) for (int r = 0; !vis && r < cards[i].rowCount; r++) {
            if (IsRowVisible(cards[i].rows[r], modeMask)) vis = true;
        }
        if (!vis) continue;

        // Card wraps parent row + expanded children (Win11 style)
        int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
        int numChildren = 0;
        int expIdx = expandBaseIdx + i;
        if (cards[i].action == RowAction::ExpandSection && wd->cardExpanded[expIdx] && cards[i].rows) {
            for (int r = 0; r < cards[i].rowCount; r++)
                if (IsRowVisible(cards[i].rows[r], modeMask)) numChildren++;
        }
        int cardH = 2 * pad + catRowH + (numChildren > 0 ? numChildren * (cRowH + 1) : 0);

        // Draw card with shadow + hover
        bool isHover = (i == wd->hoverRowIndex);
        if (numChildren > 0) {
            // Expanded: shadow on full card, hover only on parent row
            DrawCardWithShadow(hdc, margin, curY, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, false, tc.rowHover);
            if (isHover) {
                HBRUSH hHov = CreateSolidBrush(tc.rowHover);
                HPEN hHovPen = CreatePen(PS_SOLID, 1, tc.rowHover);
                SelectObject(hdc, hHov); SelectObject(hdc, hHovPen);
                RoundRect(hdc, margin, curY, margin + cardW, curY + 2 * pad + catRowH, cornerR, cornerR);
                DeleteObject(hHov); DeleteObject(hHovPen);
            }
        } else {
            DrawCardWithShadow(hdc, margin, curY, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, isHover, tc.rowHover);
        }

        int rowTop = curY + pad; // start inside card padding

        // Category row: icon + label + description + chevron
        int labelLeft = margin + iconPadL + iconSz + iconGap;

        // Icon — glyph from Segoe MDL2 Assets (hollow style) or fallback colored rect
        {
            int ix = margin + iconPadL, iy = rowTop + (catRowH - iconSz) / 2;
            if (cards[i].iconGlyph && wd->hFontMDL2Icon && wd->hasMDL2) {
                HFONT hOldMDL2 = (HFONT)SelectObject(hdc, wd->hFontMDL2Icon);
                SetTextColor(hdc, tc.textPrimary);
                wchar_t glyph[2] = { cards[i].iconGlyph, 0 };
                RECT rcGlyph = { ix, iy, ix + iconSz, iy + iconSz };
                DrawTextW(hdc, glyph, 1, &rcGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(hdc, hOldMDL2);
            } else if (cards[i].iconColor) {
                COLORREF ic = cards[i].iconColor;
                HBRUSH hIBr = CreateSolidBrush(ic);
                HPEN hIPen = CreatePen(PS_SOLID, 1, ic);
                SelectObject(hdc, hIBr); SelectObject(hdc, hIPen);
                RoundRect(hdc, ix, iy, ix + iconSz, iy + iconSz, iconSz / 4, iconSz / 4);
                DeleteObject(hIBr); DeleteObject(hIPen);
            }
        }

        // Label
        SelectObject(hdc, wd->hFontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcL = { labelLeft, rowTop + ScaleForDpi(g_geo.rowLabelTopOff, dpi), margin + cardW - pad - ScaleForDpi(g_geo.chevronW + g_geo.rowLabelMidOff, dpi), rowTop + catRowH / 2 + ScaleForDpi(g_geo.rowLabelMidOff, dpi) };
        DrawTextW(hdc, cards[i].title, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        // Description
        if (cards[i].description) {
            SelectObject(hdc, wd->hFontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcD = { labelLeft, rowTop + catRowH / 2 - ScaleForDpi(g_geo.rowLabelMidOff, dpi), margin + cardW - pad - ScaleForDpi(g_geo.chevronW + g_geo.rowLabelMidOff, dpi), rowTop + catRowH - ScaleForDpi(g_geo.rowDescBotOff, dpi) };
            DrawTextW(hdc, cards[i].description, -1, &rcD, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }

        // Chevron — MDL2 glyphs (Win10+) or text fallback (Win7/8)
        if (wd->hasMDL2 && wd->hFontMDL2) SelectObject(hdc, wd->hFontMDL2);
        else SelectObject(hdc, wd->hFontBody);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcChev = { margin + cardW - pad - ScaleForDpi(g_geo.chevronW, dpi), rowTop, margin + cardW - pad, rowTop + catRowH };
        if (cards[i].action == RowAction::NavigateToCard)
            DrawTextW(hdc, wd->hasMDL2 ? L"\xE76C" : L">", -1, &rcChev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        else if (cards[i].action == RowAction::ExpandSection)
            DrawTextW(hdc, wd->cardExpanded[expIdx] ? (wd->hasMDL2 ? L"\xE70E" : L"\x2227") : (wd->hasMDL2 ? L"\xE70D" : L"\x2228"), -1, &rcChev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        // Expanded children — inside parent card, 1px separator between all rows
        if (cards[i].action == RowAction::ExpandSection && wd->cardExpanded[expIdx] && cards[i].rows) {
            int childY = rowTop + catRowH + pad;  // after title row's bottom padding
            int childIdx = 0;
            for (int r = 0; r < cards[i].rowCount; r++) {
                const LayoutRow& lr = cards[i].rows[r];
                if (!IsRowVisible(lr, modeMask)) continue;

                // 1px separator — full width, including before first child (after parent row)
                {
                    HPEN hCSep = CreatePen(PS_SOLID, 1, BlendColor(tc.cardBg, tc.textSecondary, 40));
                    SelectObject(hdc, hCSep);
                    MoveToEx(hdc, margin + 1, childY, nullptr);
                    LineTo(hdc, margin + cardW - 1, childY);
                    DeleteObject(hCSep);
                    childY += 1;
                }

                // Unified child row rendering
                int childCtrlRight = CalcCtrlRight(margin, cardW, dpi, false);
                PaintRow(hdc, wd, lr, margin, childY, cardW, cRowH, childCtrlRight, dpi, true);
                childY += cRowH;
                childIdx++;
            }
        }

        // Advance past this card + gap
        curY += cardH + cardGapDpi;
    }

    // Repaint section title area on top of scrolled cards
    int titleAreaBottom = ScaleForDpi(g_geo.topMargin, dpi);
    RECT rcTitleCover = { 0, 0, rcClient.right, titleAreaBottom };
    FillRect(hdc, &rcTitleCover, wd->hBrushContentBg);
    SelectObject(hdc, wd->hFontTitle);
    SetTextColor(hdc, tc.textPrimary);
    SetBkMode(hdc, TRANSPARENT);
    // Hamburger is drawn by PaintContent (shared across all levels)
    SelectObject(hdc, wd->hFontTitle);
    SetTextColor(hdc, tc.textPrimary);
    // In narrow mode, shift title right of hamburger and align vertically with hamburger
    int titleLeft = (wd->sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    RECT rcTitle2;
    if (wd->sidebarNarrowMode)
        rcTitle2 = { titleLeft, 0, rcClient.right - margin, ScaleForDpi(g_geo.hamburgerSize, dpi) };
    else
        rcTitle2 = { titleLeft, ScaleForDpi(g_geo.topPad, dpi), rcClient.right - margin, ScaleForDpi(g_geo.topPad + g_geo.titleAreaH, dpi) };
    DrawTextW(hdc, pageTitle, -1, &rcTitle2, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SelectObject(hdc, oldFont);
}
// ============================================================================
// PaintRow — unified row renderer for ALL pages
// ============================================================================
void SettingsWindow::PaintRow(HDC hdc, WindowData* wd,
    const LayoutRow& lr, int margin, int curY, int cardW, int thisH,
    int ctrlRightEdge, UINT dpi, bool isChild)
{
    const ThemeColors& tc = wd->theme;
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int iconSz = ScaleForDpi(g_geo.rowIconSize, dpi);
    int iconPadL = ScaleForDpi(g_geo.rowIconPadLeft, dpi);
    int iconGap = ScaleForDpi(g_geo.rowIconGap, dpi);
    int sepIndent = ScaleForDpi(g_geo.rowSepIndent, dpi);

    // CandidatePreview
    if (lr.type == RowType::CandidatePreview) {
        PaintCandidatePreviewCard(hdc, wd->snapshot, margin, curY, cardW, thisH, dpi);
        return;
    }

    // Child icon starts where parent label starts (aligned with parent title text)
    // Parent: icon at margin+iconPadL, label at margin+iconPadL+iconSz+iconGap
    // Child:  icon at margin+iconPadL+iconSz+iconGap, label after child icon
    bool hasIcon = (lr.iconGlyph || lr.iconColor);
    int childIconStart = margin + iconPadL + iconSz + iconGap; // = where parent label starts

    if (lr.type != RowType::ColorGrid && lr.type != RowType::RichEditor && hasIcon) {
        int ix = isChild ? childIconStart : (margin + iconPadL);
        int iy = curY + (thisH - iconSz) / 2;
        if (lr.iconGlyph && wd->hFontMDL2Icon && wd->hasMDL2) {
            HFONT hOldMDL2 = (HFONT)SelectObject(hdc, wd->hFontMDL2Icon);
            SetTextColor(hdc, tc.textPrimary);
            wchar_t glyph[2] = { lr.iconGlyph, 0 };
            RECT rcGlyph = { ix, iy, ix + iconSz, iy + iconSz };
            DrawTextW(hdc, glyph, 1, &rcGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            SelectObject(hdc, hOldMDL2);
        } else if (lr.iconColor) {
            HBRUSH hIBr = CreateSolidBrush(lr.iconColor);
            HPEN hIPen = CreatePen(PS_SOLID, 1, lr.iconColor);
            SelectObject(hdc, hIBr); SelectObject(hdc, hIPen);
            RoundRect(hdc, ix, iy, ix + iconSz, iy + iconSz, iconSz / 4, iconSz / 4);
            DeleteObject(hIBr); DeleteObject(hIPen);
        }
    }

    // Label left: child with icon starts after child icon; child without icon uses sepIndent
    int labelLeft = isChild ? (hasIcon ? (childIconStart + iconSz + iconGap)
                                       : (margin + sepIndent))
                            : (margin + iconPadL + iconSz + iconGap);

    // RichEditor: header label + description only
    if (lr.type == RowType::RichEditor) {
        int hdrH = ScaleForDpi(g_geo.editorHeaderH / 2, dpi);
        SelectObject(hdc, wd->hFontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcRH = { margin + pad, curY + pad / 2, margin + cardW - pad, curY + hdrH };
        DrawTextW(hdc, lr.label, -1, &rcRH, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        if (lr.description) {
            SelectObject(hdc, wd->hFontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcRI = { margin + pad, curY + hdrH, margin + cardW - pad, curY + ScaleForDpi(g_geo.editorHeaderH, dpi) };
            DrawTextW(hdc, lr.description, -1, &rcRI, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        return;
    }

    // ColorGrid: swatch labels
    if (lr.type == RowType::ColorGrid) {
        const wchar_t* swLabels[] = { L"候選字文字", L"選取文字", L"候選字背景", L"聯想詞文字", L"編號文字", L"選取背景" };
        int swS = ScaleForDpi(g_geo.swatchSize, dpi);
        int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
        int cellH = swS + swGap;
        int baseX = margin;
        SelectObject(hdc, wd->hFontDesc);
        SIZE szLbl; GetTextExtentPoint32W(hdc, swLabels[0], 5, &szLbl);
        int maxLblW = szLbl.cx;
        int gap = maxLblW / 5;
        int rightEdge = ctrlRightEdge - margin;
        int colW = rightEdge / 3;
        for (int si = 0; si < 6; si++) {
            int col = si % 3, row2 = si / 3;
            int colRight = baseX + (col + 1) * colW;
            int swatchX = colRight - swS;
            int labelX = swatchX - gap - maxLblW;
            int sy = curY + row2 * cellH;
            SetTextColor(hdc, tc.textSecondary);
            RECT rcSL = { labelX, sy, labelX + maxLblW, sy + swS };
            DrawTextW(hdc, swLabels[si], -1, &rcSL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        return;
    }

    // Label + description
    if (lr.type != RowType::ColorGrid) {
        SelectObject(hdc, isChild ? wd->hFontBody : wd->hFontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        int lt = curY + ScaleForDpi(lr.description ? g_geo.rowLabelTopOff : 0, dpi);
        int lb = lr.description ? curY + thisH / 2 + ScaleForDpi(g_geo.rowLabelMidOff, dpi) : curY + thisH;
        int comboW = ScaleForDpi(g_geo.comboWidth, dpi);
        RECT rcL = { labelLeft, lt, ctrlRightEdge - comboW, lb };

        if (lr.type == RowType::Clickable && lr.action == RowAction::OpenFontDialog) {
            WCHAR preview[128];
            StringCchPrintfW(preview, 128, L"%s  —  %s, %upt", lr.label, wd->snapshot.fontFaceName, wd->snapshot.fontSize);
            DrawTextW(hdc, preview, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        } else {
            DrawTextW(hdc, lr.label, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }

        if (lr.description) {
            SelectObject(hdc, wd->hFontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcD = { labelLeft, curY + thisH / 2 - ScaleForDpi(g_geo.rowLabelMidOff, dpi), rcL.right, curY + thisH - ScaleForDpi(g_geo.rowDescBotOff, dpi) };
            DrawTextW(hdc, lr.description, -1, &rcD, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
    }

    // ExpandSection chevron (e.g., color mode when custom)
    if (lr.action == RowAction::ExpandSection && lr.type == RowType::ComboBox
        && wd->snapshot.colorMode == 3) {
        int chevW = ScaleForDpi(g_geo.chevronW, dpi);
        int chevArea = ScaleForDpi(g_geo.chevronW + 4, dpi);
        if (wd->hasMDL2 && wd->hFontMDL2) SelectObject(hdc, wd->hFontMDL2);
        else SelectObject(hdc, wd->hFontBody);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcChev2 = { ctrlRightEdge + ScaleForDpi(4, dpi), curY, ctrlRightEdge + chevArea, curY + thisH };
        DrawTextW(hdc, wd->colorGridExpanded
            ? (wd->hasMDL2 ? L"\xE70E" : L"\x2227")
            : (wd->hasMDL2 ? L"\xE70D" : L"\x2228"),
            -1, &rcChev2, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    // Toggle graphic
    if (lr.type == RowType::Toggle) {
        int tw = ScaleForDpi(g_geo.toggleW, dpi), th = ScaleForDpi(g_geo.toggleH, dpi);
        int tx = ctrlRightEdge - tw;
        int ty = curY + (thisH - th) / 2;
        HWND hCtrl = FindControl(wd, lr.id);
        bool isOn = hCtrl ? (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED) : false;
        RECT rcTog = { tx, ty, tx + tw, ty + th };
        SettingsWindow::PaintToggleSwitch(hdc, rcTog, isOn, false, tc);
        SetTextColor(hdc, tc.textPrimary);
        SelectObject(hdc, wd->hFontBody);
        RECT rcSt = { tx - ScaleForDpi(g_geo.toggleLabelW, dpi), ty, tx - ScaleForDpi(g_geo.toggleLabelGapR, dpi), ty + th };
        DrawTextW(hdc, isOn ? L"開啟" : L"關閉", -1, &rcSt, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
    }
}

void SettingsWindow::AddControl(WindowData* wd, SettingsControlId id, HWND h, int x, int y, int w, int ht)
{
    WindowData::ControlHandle ch = {};
    ch.id = id; ch.hWnd = h; ch.origX = x; ch.origY = y; ch.origW = w; ch.origH = ht;
    wd->controlHandles.push_back(ch);
    if (h) SetWindowSubclass(h, ChildWheelSubclassProc, 1, 0);
}

// ============================================================================
// CreateRowControl — unified control creation for any RowType
// ============================================================================
void SettingsWindow::CreateRowControl(WindowData* wd, const LayoutRow& lr,
    int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi)
{
    int comboW = ScaleForDpi(g_geo.comboWidth, dpi);
    int editW = ScaleForDpi(g_geo.editWidth, dpi);
    int btnW = ScaleForDpi(g_geo.buttonWidth, dpi);


    switch (lr.type) {
    case RowType::Toggle:
    {
        HWND h = CreateWindowExW(0, L"BUTTON", lr.label,
            WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        AddControl(wd, lr.id, h, 0, 0, 0, 0);
        break;
    }
    case RowType::ComboBox:
    {
        HWND h = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            ctrlRight - comboW, cy, comboW, ScaleForDpi(g_geo.comboDropH, dpi),
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) {
            SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
            if (wd->isDarkTheme) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        }
        AddControl(wd, lr.id, h, ctrlRight - comboW, cy, comboW, ScaleForDpi(g_geo.comboDropH, dpi));
        break;
    }
    case RowType::Edit:
    {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            ctrlRight - editW, cy, editW, ctrlH,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) {
            SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
            if (wd->isDarkTheme) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        }
        AddControl(wd, lr.id, h, ctrlRight - editW, cy, editW, ctrlH);
        break;
    }
    case RowType::Button:
    case RowType::Clickable:
    {
        const wchar_t* btnText = (lr.action == RowAction::LoadTable) ? L"載入" :
                                 (lr.action == RowAction::OpenFontDialog) ? L"變更字型" :
                                 (lr.action == RowAction::ExportCustomTable) ? L"匯出" :
                                 (lr.action == RowAction::ImportCustomTable) ? L"匯入" :
                                 (lr.action == RowAction::ResetDefaults) ? L"還原預設值" : lr.label;
        HWND h = CreateWindowExW(0, L"BUTTON", btnText,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            ctrlRight - btnW, cy, btnW, btnH,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
        AddControl(wd, lr.id, h, ctrlRight - btnW, cy, btnW, btnH);
        break;
    }
    default: break;
    }
}

// ============================================================================
// CreateColorGridControls — owner-draw swatch grid for RowType::ColorGrid
// ============================================================================
void SettingsWindow::CreateColorGridControls(WindowData* wd, int margin, int curY, int ctrlRight, UINT dpi)
{
    int swS = ScaleForDpi(g_geo.swatchSize, dpi);
    int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
    int baseX = margin;
    int cellH = swS + swGap;
    int rightEdge = ctrlRight - margin;
    int colW = rightEdge / 3;
    for (int si = 0; si < 6; si++) {
        int col = si % 3, row2 = si / 3;
        int colRight = baseX + (col + 1) * colW;
        int sx = colRight - swS;
        int sy = curY + row2 * cellH;
        HWND h = CreateWindowExW(0, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            sx, sy, swS, swS,
            wd->hContentArea, (HMENU)(UINT_PTR)(4000 + si), wd->hInstance, nullptr);
        AddControl(wd, (SettingsControlId)(CTRL_COLOR_FR + si), h, sx, sy, swS, swS);
    }
}

// ============================================================================
// CreateRichEditorControl — owner-draw editor for RowType::RichEditor
// ============================================================================
void SettingsWindow::CreateRichEditorControl(WindowData* wd, const LayoutRow& lr,
    int margin, int cardW, int pad, int curY, int thisH, int ctrlRight, int btnW, int btnH, UINT dpi)
{
    static HMODULE hMsftEdit = LoadLibraryW(L"msftedit.dll");
    int reX = margin + pad;
    int reY = curY + ScaleForDpi(g_geo.editorHeaderH, dpi);
    int reW = cardW - 2 * pad;
    int reH = thisH - ScaleForDpi(g_geo.editorHeaderH + g_geo.rowHeightSingle, dpi);
    HWND hRE = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        reX, reY, reW, reH,
        wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
    if (hRE) {
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
    AddControl(wd, lr.id, hRE, reX, reY, reW, reH);
    if (hRE) RemoveWindowSubclass(hRE, ChildWheelSubclassProc, 1);

    // Load existing custom table from file
    if (hRE) {
        WCHAR txtPath[MAX_PATH] = {};
        GetCustomTableTxtPath(wd->currentMode, txtPath, MAX_PATH);
        if (PathFileExistsW(txtPath)) {
            HANDLE hFile = CreateFileW(txtPath, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD dwSize = GetFileSize(hFile, nullptr);
                if (dwSize != INVALID_FILE_SIZE && dwSize > 0) {
                    LPWSTR buf = new (std::nothrow) WCHAR[dwSize + 1];
                    if (buf) {
                        ZeroMemory(buf, (dwSize + 1) * sizeof(WCHAR));
                        DWORD dwRead = 0;
                        ReadFile(hFile, buf, dwSize, &dwRead, nullptr);
                        CHARFORMAT2W cf = {};
                        cf.cbSize = sizeof(cf);
                        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
                        StringCchCopyW(cf.szFaceName, LF_FACESIZE, L"Microsoft JhengHei UI");
                        cf.yHeight = MulDiv(10 * 20, dpi, 96);
                        cf.wWeight = FW_NORMAL;
                        SendMessageW(hRE, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
                        LPCWSTR text = buf;
                        if (dwRead >= 2 && buf[0] == 0xFEFF) text = buf + 1;
                        SetWindowTextW(hRE, text);
                        delete[] buf;
                    }
                }
                CloseHandle(hFile);
            }
        }
        SetWindowSubclass(hRE, CustomTableRE_SubclassProc, 0, 0);
        SendMessageW(hRE, EM_SETEVENTMASK, 0, ENM_CHANGE);
        wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
        wd->ctLastEditedLine = -1;
        wd->ctKeystrokeCount = 0;
    }

    // [儲存] button below RichEdit
    int saveY = reY + reH + ScaleForDpi(g_geo.topPad, dpi);
    HWND hSave = CreateWindowExW(0, L"BUTTON", L"儲存",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
        ctrlRight - btnW, saveY, btnW, btnH,
        wd->hContentArea, (HMENU)(UINT_PTR)(3000 + CTRL_CUSTOM_TABLE_SAVE), wd->hInstance, nullptr);
    if (hSave) SendMessage(hSave, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
    AddControl(wd, CTRL_CUSTOM_TABLE_SAVE, hSave, ctrlRight - btnW, saveY, btnW, btnH);
}

// Candidate + Notify preview — owner-draw card for RowType::CandidatePreview
static void PaintCandidatePreviewCard(HDC hdc, const SettingsSnapshot& s,
    int margin, int curY, int cardW, int totalH, UINT dpi)
{
    int prevTop = curY;
    int padLeft = ScaleForDpi(g_geo.rowIconPadLeft, dpi);  // left padding from card edge

    // Create the candidate font
    HFONT hCandFont = CreateFontW(-MulDiv(s.fontSize, dpi, 72), 0, 0, 0,
        s.fontWeight, s.fontItalic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, s.fontFaceName);
    if (!hCandFont) hCandFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HFONT hOldF = (HFONT)SelectObject(hdc, hCandFont);

    // Metrics: exact copy from CCandidateWindow::_DrawList
    const int wndWidth = 4;
    WCHAR testStr[5] = L"國國國國";
    SIZE candSize = {};
    GetTextExtentPoint32(hdc, testStr, wndWidth, &candSize);
    TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
    int cxLine = max((int)tm.tmAveCharWidth, (int)candSize.cx / wndWidth);
    int cyLine = candSize.cy * 5 / 4;
    int fistLineOffset = cyLine / 3;
    int leftPadding = cyLine / 2;
    int cxTitle = candSize.cx + leftPadding + cxLine;

    const wchar_t* candNums[] = { L"1", L"2", L"3", L"4", L"5", L"6" };
    const wchar_t* candTexts[] = { L"國", L"過", L"鍋", L"裹", L"鴿", L"擱" };
    int candCount = 6;
    int selectedIdx = 0;
    int candWndH = candCount * cyLine + fistLineOffset + cyLine / 3;
    int cornerRadius = MulDiv(8, dpi, 96);

    int prevX = margin + padLeft;

    // SDF shadow for candidate window (same algorithm as ShadowWindow.cpp)
    {
        int sp = ScaleForDpi(14, dpi); // SHADOW_SPREAD
        int shadowW = cxTitle + sp * 2;
        int shadowH = candWndH + sp * 2;
        int shadowX = prevX - sp;
        int shadowY = prevTop - sp;

        // Shadow adapts to content bg — same as ShadowWindow.cpp
        // Detect if the IME preview is dark or light based on itemBGColor
        int imeLum = (299 * GetRValue(s.itemBGColor) + 587 * GetGValue(s.itemBGColor) + 114 * GetBValue(s.itemBGColor)) / 1000;
        BOOL imeIsDark = (imeLum <= 128);
        // Detect content bg luminance
        COLORREF contentBg = GetSysColor(COLOR_3DFACE);
        int contentLum = (299 * GetRValue(contentBg) + 587 * GetGValue(contentBg) + 114 * GetBValue(contentBg)) / 1000;
        BOOL bgIsLight = (contentLum > 128);
        // Default: dark IME → light shadow, light IME → dark shadow
        BYTE shadowR = imeIsDark ? 200 : 0;
        BYTE shadowG = imeIsDark ? 200 : 0;
        BYTE shadowB = imeIsDark ? 200 : 0;
        BYTE maxAlpha = 35;
        if (imeIsDark && bgIsLight) {
            // Dark IME on light bg: flip to dark shadow with boosted alpha
            shadowR = 0; shadowG = 0; shadowB = 0;
            maxAlpha = 45;
        } else if (!imeIsDark && !bgIsLight) {
            // Light IME on dark bg: flip to light shadow
            shadowR = 200; shadowG = 200; shadowB = 200;
        }

        double r = cornerRadius * 0.5;
        double half_w = cxTitle * 0.5;
        double half_h = candWndH * 0.5;
        double inner_rx = half_w - r;
        double inner_ry = half_h - r;

        HDC dcMem = CreateCompatibleDC(hdc);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = shadowW;
        bi.bmiHeader.biHeight = -shadowH; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hBmp && pBits) {
            memset(pBits, 0, (size_t)shadowW * shadowH * 4);
            HBITMAP hOld = (HBITMAP)SelectObject(dcMem, hBmp);
            struct RGBALPHA { BYTE b, g, r, a; };
            for (int y = 0; y < shadowH; y++) {
                for (int x = 0; x < shadowW; x++) {
                    double ox = x - sp;
                    double oy = y - sp;
                    double qx = fabs(ox - half_w) - inner_rx;
                    double qy = fabs(oy - half_h) - inner_ry;
                    double len = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
                    double sdf = len + min(max(qx, qy), 0.0) - r;
                    if (sdf <= -1.0) continue;
                    double dist = max(0.0, sdf) / (double)sp;
                    if (dist > 1.0) continue;
                    double f = 1.0 - dist;
                    BYTE alpha = (BYTE)(maxAlpha * f * f);
                    if (alpha == 0) continue;
                    RGBALPHA* px = (RGBALPHA*)pBits + ((size_t)y * shadowW + x);
                    px->r = (BYTE)((DWORD)shadowR * alpha / 255);
                    px->g = (BYTE)((DWORD)shadowG * alpha / 255);
                    px->b = (BYTE)((DWORD)shadowB * alpha / 255);
                    px->a = alpha;
                }
            }
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, shadowX, shadowY, shadowW, shadowH, dcMem, 0, 0, shadowW, shadowH, bf);
            SelectObject(dcMem, hOld);
            DeleteObject(hBmp);
        }
        DeleteDC(dcMem);
    }

    // Draw candidate window background
    HBRUSH hBkBr = CreateSolidBrush(s.itemBGColor);
    HPEN hBkPen = CreatePen(PS_SOLID, 1, s.itemBGColor);
    SelectObject(hdc, hBkBr); SelectObject(hdc, hBkPen);
    RoundRect(hdc, prevX, prevTop, prevX + cxTitle, prevTop + candWndH, cornerRadius, cornerRadius);
    DeleteObject(hBkBr); DeleteObject(hBkPen);

    // Draw candidate rows
    int cyOffset = candSize.cy / 8 + fistLineOffset;
    for (int pi = 0; pi < candCount; pi++) {
        RECT rc;
        rc.top = prevTop + pi * cyLine + fistLineOffset;
        rc.bottom = rc.top + cyLine;
        bool isSelected = (pi == selectedIdx);

        if (isSelected) {
            RECT rcFullRow = { prevX, rc.top, prevX + cxTitle, rc.bottom };
            HBRUSH hFRBr = CreateSolidBrush(s.itemBGColor);
            FillRect(hdc, &rcFullRow, hFRBr);
            DeleteObject(hFRBr);

            int hMargin = leftPadding;
            int vInset = MulDiv(1, dpi, 96);
            int selCornerR = MulDiv(4, dpi, 96);
            RECT rcHL = { prevX + hMargin, rc.top + vInset, prevX + cxTitle - hMargin, rc.bottom - vInset };
            HBRUSH hSelBr = CreateSolidBrush(s.selectedBGColor);
            SelectObject(hdc, hSelBr); SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            RoundRect(hdc, rcHL.left, rcHL.top, rcHL.right, rcHL.bottom, selCornerR, selCornerR);
            DeleteObject(hSelBr);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, s.selectedColor);
            rc.left = prevX + leftPadding;
            rc.right = prevX + leftPadding + cxLine;
            ExtTextOut(hdc, prevX + leftPadding, prevTop + pi * cyLine + cyOffset, 0, &rc, candNums[pi], 1, NULL);
            rc.left = prevX + leftPadding + cxLine;
            rc.right = prevX + cxTitle;
            ExtTextOut(hdc, prevX + leftPadding + cxLine, prevTop + pi * cyLine + cyOffset, 0, &rc, candTexts[pi], (DWORD)wcslen(candTexts[pi]), NULL);
        } else {
            rc.left = prevX + leftPadding;
            rc.right = prevX + leftPadding + cxLine;
            SetTextColor(hdc, s.numberColor);
            SetBkColor(hdc, s.itemBGColor);
            SetBkMode(hdc, OPAQUE);
            ExtTextOut(hdc, prevX + leftPadding, prevTop + pi * cyLine + cyOffset, ETO_OPAQUE, &rc, candNums[pi], 1, NULL);
            rc.left = prevX + leftPadding + cxLine;
            rc.right = prevX + cxTitle;
            SetTextColor(hdc, s.itemColor);
            SetBkColor(hdc, s.itemBGColor);
            ExtTextOut(hdc, prevX + leftPadding + cxLine, prevTop + pi * cyLine + cyOffset, ETO_OPAQUE, &rc, candTexts[pi], (DWORD)wcslen(candTexts[pi]), NULL);
        }
        if (isSelected) SetBkMode(hdc, OPAQUE);
    }

    // Notify window preview (right side) — exact from CNotifyWindow::_DrawText
    const wchar_t* notifyText = L"中文";
    int notifyLen = (int)wcslen(notifyText);
    SIZE notifySize;
    GetTextExtentPoint32(hdc, notifyText, notifyLen, &notifySize);
    int notifyW = notifySize.cx + notifySize.cx / notifyLen;
    int notifyHt = notifySize.cy * 3 / 2;
    int notifyX = prevX + cxTitle + ScaleForDpi(20, dpi);
    int notifyY = prevTop + (candWndH - notifyHt) / 2;
    int nCorner = MulDiv(8, dpi, 96);

    // SDF shadow for notify window (same algorithm as ShadowWindow.cpp)
    {
        int sp = ScaleForDpi(14, dpi);
        int shadowW = notifyW + sp * 2;
        int shadowH = notifyHt + sp * 2;
        int shadowX = notifyX - sp;
        int shadowY = notifyY - sp;

        // Shadow adapts to content bg — same as ShadowWindow.cpp
        // Detect if the IME preview is dark or light based on itemBGColor
        int imeLum = (299 * GetRValue(s.itemBGColor) + 587 * GetGValue(s.itemBGColor) + 114 * GetBValue(s.itemBGColor)) / 1000;
        BOOL imeIsDark = (imeLum <= 128);
        // Detect content bg luminance
        COLORREF contentBg = GetSysColor(COLOR_3DFACE);
        int contentLum = (299 * GetRValue(contentBg) + 587 * GetGValue(contentBg) + 114 * GetBValue(contentBg)) / 1000;
        BOOL bgIsLight = (contentLum > 128);
        // Default: dark IME → light shadow, light IME → dark shadow
        BYTE shadowR = imeIsDark ? 200 : 0;
        BYTE shadowG = imeIsDark ? 200 : 0;
        BYTE shadowB = imeIsDark ? 200 : 0;
        BYTE maxAlpha = 35;
        if (imeIsDark && bgIsLight) {
            // Dark IME on light bg: flip to dark shadow with boosted alpha
            shadowR = 0; shadowG = 0; shadowB = 0;
            maxAlpha = 45;
        } else if (!imeIsDark && !bgIsLight) {
            // Light IME on dark bg: flip to light shadow
            shadowR = 200; shadowG = 200; shadowB = 200;
        }

        double r = nCorner * 0.5;
        double half_w = notifyW * 0.5;
        double half_h = notifyHt * 0.5;
        double inner_rx = half_w - r;
        double inner_ry = half_h - r;

        HDC dcMem = CreateCompatibleDC(hdc);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = shadowW;
        bi.bmiHeader.biHeight = -shadowH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hBmp && pBits) {
            memset(pBits, 0, (size_t)shadowW * shadowH * 4);
            HBITMAP hOld = (HBITMAP)SelectObject(dcMem, hBmp);
            struct RGBALPHA { BYTE b, g, r, a; };
            for (int y = 0; y < shadowH; y++) {
                for (int x = 0; x < shadowW; x++) {
                    double ox = x - sp;
                    double oy = y - sp;
                    double qx = fabs(ox - half_w) - inner_rx;
                    double qy = fabs(oy - half_h) - inner_ry;
                    double len = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
                    double sdf = len + min(max(qx, qy), 0.0) - r;
                    if (sdf <= -1.0) continue;
                    double dist = max(0.0, sdf) / (double)sp;
                    if (dist > 1.0) continue;
                    double f = 1.0 - dist;
                    BYTE alpha = (BYTE)(maxAlpha * f * f);
                    if (alpha == 0) continue;
                    RGBALPHA* px = (RGBALPHA*)pBits + ((size_t)y * shadowW + x);
                    px->r = (BYTE)((DWORD)shadowR * alpha / 255);
                    px->g = (BYTE)((DWORD)shadowG * alpha / 255);
                    px->b = (BYTE)((DWORD)shadowB * alpha / 255);
                    px->a = alpha;
                }
            }
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, shadowX, shadowY, shadowW, shadowH, dcMem, 0, 0, shadowW, shadowH, bf);
            SelectObject(dcMem, hOld);
            DeleteObject(hBmp);
        }
        DeleteDC(dcMem);
    }

    // Notify background
    HBRUSH hNBr = CreateSolidBrush(s.itemBGColor);
    HPEN hNPen = CreatePen(PS_SOLID, 1, s.itemBGColor);
    SelectObject(hdc, hNBr); SelectObject(hdc, hNPen);
    RoundRect(hdc, notifyX, notifyY, notifyX + notifyW, notifyY + notifyHt, nCorner, nCorner);
    DeleteObject(hNBr); DeleteObject(hNPen);

    // Notify text — same positioning as CNotifyWindow::_DrawText, but TRANSPARENT
    // (ETO_OPAQUE would overwrite rounded corners — RoundRect already painted the bg)
    SetTextColor(hdc, s.itemColor);
    SetBkMode(hdc, TRANSPARENT);
    ExtTextOut(hdc, notifyX + notifySize.cx / notifyLen / 2,
        notifyY + notifyHt / 5, 0, nullptr,
        notifyText, (DWORD)notifyLen, NULL);

    SelectObject(hdc, hOldF);
    if (hCandFont != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hCandFont);
}

void SettingsWindow::PaintSettingsDetail(HWND hWnd, HDC hdc, WindowData* wd)
{
    RECT rcClient; GetClientRect(hWnd, &rcClient);
    UINT dpi = CConfig::GetDpiForHwnd(GetParent(hWnd));
    const ThemeColors& tc = wd->theme;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cornerR = ScaleForDpi(g_geo.cardRadius, dpi);
    int sepIndent = ScaleForDpi(g_geo.rowSepIndent, dpi);
    int iconSz = ScaleForDpi(g_geo.rowIconSize, dpi);
    int iconPadL = ScaleForDpi(g_geo.rowIconPadLeft, dpi);
    int iconGap = ScaleForDpi(g_geo.rowIconGap, dpi);
    int rowH = ScaleForDpi(g_geo.rowHeight, dpi);
    int rowHS = ScaleForDpi(g_geo.rowHeightSingle, dpi);

    SetBkMode(hdc, TRANSPARENT);

    int layoutCount = 0;
    const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
    if (wd->currentCardIndex < 0 || wd->currentCardIndex >= layoutCount) return;
    const LayoutCard& card = allCards[wd->currentCardIndex];

    int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);

    // Preview is now a RowType::CandidatePreview card in the layout — no special handling needed

    int cardW = CalcCardW(GetParent(hWnd), dpi, !wd->sidebarNarrowMode && !wd->sidebarCollapsed);
    // Right edge for ALL controls — left of chevron area on Level 1
    int ctrlRightX = CalcCtrlRight(margin, cardW, dpi, true);
    int chevArea = ScaleForDpi(g_geo.chevronW + 4, dpi);

    // Helper: calculate row height
    auto calcRowH = [&](const LayoutRow& lr) -> int {
        return CalcRowHeight(hdc, lr, wd->snapshot, dpi);
    };

    // Helper: check if row is visible in current context
    auto isRowShown = [&](const LayoutRow& lr) -> bool {
        if (!IsRowVisible(lr, modeMask)) return false;
        if (lr.expandParent != CTRL_NONE && lr.expandParent == CTRL_COLOR_MODE
            && (wd->snapshot.colorMode != 3 || !wd->colorGridExpanded)) return false;
        return true;
    };

    // Win11 pattern: each top-level row (+ its children) is its own card.
    // cardBreakBefore also forces a new card.
    // Top-level rows have spacing between cards; expanded children are inside parent card.
    struct CardSpan { int startRow; int endRow; int height; };
    CardSpan cardSpans[32]; int numCards = 0;
    {
        for (int r = 0; r < card.rowCount; r++) {
            const LayoutRow& lr = card.rows[r];
            if (!isRowShown(lr)) continue;
            // Top-level row or cardBreakBefore → start a new card
            if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
                int h = calcRowH(lr);
                // Include all visible children that follow
                int endR = r + 1;
                while (endR < card.rowCount && card.rows[endR].expandParent != CTRL_NONE
                       && !card.rows[endR].cardBreakBefore) {
                    if (isRowShown(card.rows[endR]))
                        h += calcRowH(card.rows[endR]);
                    endR++;
                }
                if (numCards < 32)
                    cardSpans[numCards++] = { r, endR, h + 2 * pad };
                r = endR - 1; // loop will r++
            }
        }
    }

    // Draw each card (each top-level row + children = one card, with cardGap between)
    SetBkMode(hdc, TRANSPARENT);  // reset after candidate preview's OPAQUE
    HFONT oldFont = (HFONT)SelectObject(hdc, wd->hFontBody);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cardTop = ScaleForDpi(g_geo.topPad, dpi) - wd->scrollPos;
    for (int ci = 0; ci < numCards; ci++) {
        int cardH = cardSpans[ci].height;

        // Skip card background + hover for CandidatePreview (transparent)
        bool isPreviewCard = (cardSpans[ci].startRow < card.rowCount
            && card.rows[cardSpans[ci].startRow].type == RowType::CandidatePreview);
        if (!isPreviewCard) {
            bool isHoverL2 = (cardSpans[ci].startRow == wd->hoverRowIndex);
            DrawCardWithShadow(hdc, margin, cardTop, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, isHoverL2, tc.rowHover);
        }

        int curY = cardTop + pad;
        for (int r = cardSpans[ci].startRow; r < cardSpans[ci].endRow; r++) {
            const LayoutRow& lr = card.rows[r];
            if (!isRowShown(lr)) continue;
            int thisH = calcRowH(lr);

        // Unified row rendering
        bool isChild = (card.rows[r].expandParent != CTRL_NONE);
        PaintRow(hdc, wd, lr, margin, curY, cardW, thisH, ctrlRightX, dpi, isChild);

        curY += thisH;
        } // end row loop
        cardTop += cardH + cardGap;
    } // end card loop
    SelectObject(hdc, oldFont);
}
void SettingsWindow::PaintBreadcrumb(HWND hWnd, HDC hdc, WindowData* wd)
{
    UINT dpi = CConfig::GetDpiForHwnd(GetParent(hWnd));
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
    const ThemeColors& tc = wd->theme;
    SetBkMode(hdc, TRANSPARENT);

    // All breadcrumb parts: same font (hFontTitle), same size.
    // Win11: parent=textPrimary, ›=secondary, current=primary.
    HFONT oldFont = (HFONT)SelectObject(hdc, wd->hFontTitle);

    // IME name — primary color (clickable back-link)
    SetTextColor(hdc, tc.textPrimary);
    int sbc = 0; const SidebarItemDef* si = ::GetSidebarItems(&sbc);
    const wchar_t* modeName = (wd->selectedModeIndex < sbc && si[wd->selectedModeIndex].label)
        ? si[wd->selectedModeIndex].label : L"DIME";
    // In narrow mode, breadcrumb starts after hamburger area; normal mode uses margin
    int bcLeft = (wd->sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    // Use DT_CALCRECT only to measure width; restore top/bottom so DT_VCENTER works correctly
    RECT rcM = { bcLeft, 0, bcLeft + ScaleForDpi(g_geo.bcModeNameW, dpi), bcH };
    DrawTextW(hdc, modeName, -1, &rcM, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT);
    int modeRight = rcM.right;
    rcM = { bcLeft, 0, modeRight, bcH };
    DrawTextW(hdc, modeName, -1, &rcM, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    // Separator › — secondary color
    SetTextColor(hdc, tc.textSecondary);
    RECT rcS = { modeRight, 0, modeRight + ScaleForDpi(g_geo.bcSepW, dpi), bcH };
    DrawTextW(hdc, L"  \x203A  ", -1, &rcS, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT);
    int sepRight = rcS.right;
    rcS = { modeRight, 0, sepRight, bcH };
    DrawTextW(hdc, L"  \x203A  ", -1, &rcS, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    // Category name — primary color (current page)
    SetTextColor(hdc, tc.textPrimary);
    int lc = 0; const LayoutCard* lCards = GetIMEPageLayout(&lc);
    const wchar_t* catName = (wd->currentCardIndex >= 0 && wd->currentCardIndex < lc)
        ? lCards[wd->currentCardIndex].title : L"";
    RECT rcC = { sepRight, 0, sepRight + ScaleForDpi(g_geo.bcCatNameW, dpi), bcH };
    DrawTextW(hdc, catName, -1, &rcC, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    SelectObject(hdc, oldFont);
}
void SettingsWindow::PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    int trackW = rc.right - rc.left;
    int trackH = rc.bottom - rc.top;
    int knobSize = trackH - 8;   // Win11: 12px knob in 20px track
    int knobInset = 4;
    Gdiplus::REAL radius = (Gdiplus::REAL)trackH;

    auto toColor = [](COLORREF c, BYTE a = 255) {
        return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
    };

    // Track rounded rect path
    Gdiplus::GraphicsPath trackPath;
    Gdiplus::RectF trackRect((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)trackW, (Gdiplus::REAL)trackH);
    trackPath.AddArc(trackRect.X, trackRect.Y, radius, radius, 90, 180);
    trackPath.AddArc(trackRect.X + trackRect.Width - radius, trackRect.Y, radius, radius, 270, 180);
    trackPath.CloseFigure();

    if (isOn) {
        // Filled track
        Gdiplus::SolidBrush trackBrush(toColor(tc.toggleOnTrack));
        g.FillPath(&trackBrush, &trackPath);
        // Knob (right side)
        Gdiplus::SolidBrush knobBrush(toColor(tc.toggleOnKnob));
        g.FillEllipse(&knobBrush,
            (Gdiplus::REAL)(rc.right - knobSize - knobInset - 1),
            (Gdiplus::REAL)(rc.top + knobInset),
            (Gdiplus::REAL)knobSize, (Gdiplus::REAL)knobSize);
    } else {
        // Border-only track
        Gdiplus::Pen trackPen(toColor(tc.toggleOffBorder), 1.5f);
        g.DrawPath(&trackPen, &trackPath);
        // Knob (left side)
        Gdiplus::SolidBrush knobBrush(toColor(tc.toggleOffKnob));
        g.FillEllipse(&knobBrush,
            (Gdiplus::REAL)(rc.left + knobInset + 1),
            (Gdiplus::REAL)(rc.top + knobInset),
            (Gdiplus::REAL)knobSize, (Gdiplus::REAL)knobSize);
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
    {
        RECT rcMain; GetClientRect(hWnd, &rcMain);
        UINT dpiPos = CConfig::GetDpiForHwnd(hWnd);
        int sidebarW2 = (!wd->sidebarNarrowMode && !wd->sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpiPos) : 0;
        bool hasBc = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
        // Reserve breadcrumb strip height whenever we have a breadcrumb (any mode)
        int bcH2 = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpiPos) : 0;
        MoveWindow(wd->hContentArea, sidebarW2, bcH2,
            rcMain.right - sidebarW2, rcMain.bottom - bcH2, FALSE);
    }

    RECT rcContent; GetClientRect(wd->hContentArea, &rcContent);
    RECT rcMain2; GetClientRect(hWnd, &rcMain2);
    UINT dpi = CConfig::GetDpiForHwnd(hWnd);
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cardW = CalcCardW(hWnd, dpi, !wd->sidebarNarrowMode && !wd->sidebarCollapsed);
    int rowH = ScaleForDpi(g_geo.rowHeight, dpi);
    int rowHS = ScaleForDpi(g_geo.rowHeightSingle, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int comboW = ScaleForDpi(g_geo.comboWidth, dpi);
    int editW = ScaleForDpi(g_geo.editWidth, dpi);
    int btnW = ScaleForDpi(g_geo.buttonWidth, dpi);
    int ctrlH = ScaleForDpi(g_geo.ctrlHeight, dpi);
    int btnH = ScaleForDpi(g_geo.buttonHeight, dpi);
    // Level 0 children: no chevron. Level 1 sub-pages: has chevron.
    bool isLevel1 = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
    int ctrlRight = CalcCtrlRight(margin, cardW, dpi, isLevel1);

    int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);

    // Helper lambda: create a control and store it
    auto addCtrl = [&](SettingsControlId id, HWND h, int ox, int oy, int ow, int oh) {
        if (!h) return;
        WindowData::ControlHandle ch;
        ch.id = id; ch.hWnd = h;
        ch.origX = ox; ch.origY = oy; ch.origW = ow; ch.origH = oh;
        wd->controlHandles.push_back(ch);
        // Subclass for mouse wheel forwarding
        if (IsWindowVisible(h))
            SetWindowSubclass(h, ChildWheelSubclassProc, 1, 0);
    };

    if (wd->selectedModeIndex == 5) {
        // =========================================================
        // 載入碼表 page — same per-card structure as Level 0
        // =========================================================
        int ltPageCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltPageCount);
        int cardGapLT = ScaleForDpi(g_geo.cardGap, dpi);
        int cRowHLT = ScaleForDpi(g_geo.childRowH, dpi);
        int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad;

        for (int i = 0; i < ltPageCount; i++) {
            int expIdx = 4 + i;

            // Create button for the card's own row (standalone cards with RowAction::None)
            if (ltCards[i].action == RowAction::None && ltCards[i].rows && ltCards[i].rowCount > 0) {
                const LayoutRow& lr = ltCards[i].rows[0];
                int cy = curY + (catRowH - btnH) / 2;
                CreateRowControl(wd, lr, ctrlRight, cy, ctrlH, btnH, dpi);
                curY += catRowH + 2 * pad + cardGapLT;
            } else if (ltCards[i].action == RowAction::ExpandSection && wd->cardExpanded[expIdx] && ltCards[i].rows) {
                // Expanded: children inside parent card
                int childY = curY + catRowH + pad;
                for (int r = 0; r < ltCards[i].rowCount; r++) {
                    const LayoutRow& lr = ltCards[i].rows[r];
                    childY += 1;
                    int cy = childY + (cRowHLT - btnH) / 2;
                    CreateRowControl(wd, lr, ctrlRight, cy, ctrlH, btnH, dpi);
                    childY += cRowHLT;
                }
                int numCh = ltCards[i].rowCount;
                int totalCardH = 2 * pad + catRowH + numCh * (cRowHLT + 1);
                curY += totalCardH + cardGapLT;
            } else {
                // Collapsed expandable card (no children shown)
                curY += catRowH + 2 * pad + cardGapLT;
            }
        }
        wd->totalContentHeight = curY;

    } else if (wd->navLevel == 0 && wd->selectedModeIndex < 4) {
        // =========================================================
        // Level 0: IME mode page — create controls for expanded cards
        // =========================================================
        int layoutCount = 0;
        const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
        int cardGapL0 = ScaleForDpi(g_geo.cardGap, dpi);
        int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad; // first card starts at topMargin + pad

        for (int i = 0; i < layoutCount; i++) {
            // Check card visibility
            bool vis = (cards[i].rowCount == 0);
            if (!vis) {
                for (int r = 0; !vis && r < cards[i].rowCount; r++) {
                    if (IsRowVisible(cards[i].rows[r], modeMask))
                        vis = true;
                }
            }
            if (!vis) continue;

            if (cards[i].action == RowAction::ExpandSection && wd->cardExpanded[i] && cards[i].rows) {
                // Children inside parent card, after parent row
                int cRowHL0 = ScaleForDpi(g_geo.childRowH, dpi);
                int childY = curY + catRowH + pad;  // after title row's bottom padding
                int childIdx = 0;
                int numCh = 0;
                for (int r = 0; r < cards[i].rowCount; r++) {
                    const LayoutRow& lr = cards[i].rows[r];
                    if (!IsRowVisible(lr, modeMask)) continue;
                    childY += 1; // 1px separator (including before first child)
                    int cy = childY + (cRowHL0 - ctrlH) / 2;
                    CreateRowControl(wd, lr, ctrlRight, cy, ctrlH, btnH, dpi);
                    childY += cRowHL0;
                    childIdx++;
                    numCh++;
                }
                // Card height = pad + catRowH + children + pad
                int totalCardH = 2 * pad + catRowH + numCh * (cRowHL0 + 1);
                curY += totalCardH + cardGapL0;
            } else {
                curY += catRowH + 2 * pad + cardGapL0;
            }
        }
        wd->totalContentHeight = curY;
        PopulateControls(wd);

    } else if (wd->navLevel == 1 && wd->selectedModeIndex < 4) {
        // =========================================================
        // Level 1: sub-page — create controls for current card's rows
        // =========================================================
        int layoutCount = 0;
        const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
        if (wd->currentCardIndex < 0 || wd->currentCardIndex >= layoutCount) {
            wd->totalContentHeight = 0;
        } else {
            const LayoutCard& card = cards[wd->currentCardIndex];
            int cardGapDpi = ScaleForDpi(g_geo.cardGap, dpi);
            int curY = ScaleForDpi(g_geo.topPad, dpi) + pad;
            bool firstTopLevel = true;

            if (card.rows) {
                for (int r = 0; r < card.rowCount; r++) {
                    const LayoutRow& lr = card.rows[r];
                    if (!IsRowVisible(lr, modeMask)) continue;
                    if (lr.expandParent != CTRL_NONE) {
                        if (lr.expandParent == CTRL_COLOR_MODE
                            && (wd->snapshot.colorMode != 3 || !wd->colorGridExpanded))
                            continue;
                    }

                    // Each top-level row starts a new card (Win11 style: spacing between cards)
                    if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
                        if (!firstTopLevel)
                            curY += pad + cardGapDpi + pad; // close prev card + gap + open new card
                        firstTopLevel = false;
                    }

                    HDC hdcTmp = GetDC(wd->hContentArea);
                    int thisH = CalcRowHeight(hdcTmp, lr, wd->snapshot, dpi);
                    ReleaseDC(wd->hContentArea, hdcTmp);
                    int cy = curY + (thisH - ctrlH) / 2;

                    switch (lr.type) {
                    case RowType::RichEditor:
                        CreateRichEditorControl(wd, lr, margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi);
                        break;
                    case RowType::Toggle:
                    case RowType::ComboBox:
                    case RowType::Edit:
                    case RowType::Clickable:
                    case RowType::Button:
                        CreateRowControl(wd, lr, ctrlRight, cy, ctrlH, btnH, dpi);
                        break;
                    case RowType::ColorGrid:
                        CreateColorGridControls(wd, margin, curY, ctrlRight, dpi);
                        break;
                    }
                    curY += thisH;
                }
            }
            wd->totalContentHeight = curY + pad + ScaleForDpi(g_geo.cardGap, dpi);
            PopulateControls(wd);
        }
    }

    wd->scrollPos = 0;
    UpdateScrollInfo(wd);
    InvalidateRect(wd->hContentArea, nullptr, TRUE);
}
static void ComboAddAndSelect(HWND hCombo, const wchar_t* items[], int count, int sel)
{
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
    if (sel >= 0 && sel < count)
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
}

void SettingsWindow::PopulateControls(WindowData* wd)
{
    const SettingsSnapshot& s = wd->snapshot;
    HWND h;

    if ((h = FindControl(wd, CTRL_COLOR_MODE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        // Must match ConfigDialog.cpp exactly: "跟隨系統模式" only on Win10 1809+
        if (Global::isWindows1809OrLater) {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"跟隨系統模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"淡色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"深色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"自訂");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM));
        }
        // Select by item data matching colorMode
        int count = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < count; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.colorMode) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0);
                break;
            }
        }
    }
    if ((h = FindControl(wd, CTRL_SHOW_NOTIFY)))
        SendMessage(h, BM_SETCHECK, s.showNotifyDesktop ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_SCOPE))) {
        // Exact from ConfigDialog.cpp — uses CB_SETITEMDATA
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        struct { const wchar_t* text; int data; } arrayScopeItems[] = {
            { L"行列30 Big5 (繁體中文)", (int)ARRAY_SCOPE::ARRAY30_BIG5 },
            { L"行列30 Unicode Ext-A",  (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A },
            { L"行列30 Unicode Ext-AB", (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB },
            { L"行列30 Unicode Ext-A~D",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD },
            { L"行列30 Unicode Ext-A~G",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCDE },
            { L"行列40 Big5",           (int)ARRAY_SCOPE::ARRAY40_BIG5 },
        };
        for (auto& item : arrayScopeItems) {
            LRESULT idx = SendMessage(h, CB_ADDSTRING, 0, (LPARAM)item.text);
            SendMessage(h, CB_SETITEMDATA, idx, (LPARAM)item.data);
        }
        // Select by item data
        int cnt = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < cnt; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.arrayScope) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0); break;
            }
        }
    }
    if ((h = FindControl(wd, CTRL_CHARSET_SCOPE))) {
        const wchar_t* f[] = { L"完整字集", L"繁體中文" };
        ComboAddAndSelect(h, f, 2, s.big5Filter ? 1 : 0);
    }
    if ((h = FindControl(wd, CTRL_NUMERIC_PAD))) {
        const wchar_t* p[] = { L"數字鍵盤輸入數字符號", L"數字鍵盤輸入字根", L"僅用數字鍵盤輸入字根" };
        ComboAddAndSelect(h, p, 3, s.numericPad);
    }
    if ((h = FindControl(wd, CTRL_PHONETIC_KB))) {
        const wchar_t* l[] = { L"標準鍵盤", L"倚天鍵盤" };
        ComboAddAndSelect(h, l, 2, s.phoneticKeyboardLayout);
    }
    if ((h = FindControl(wd, CTRL_MAX_CODES))) {
        WCHAR buf[16]; StringCchPrintfW(buf, 16, L"%u", s.maxCodes);
        SetWindowTextW(h, buf);
    }
    if ((h = FindControl(wd, CTRL_ARROW_KEY_SW_PAGES)))
        SendMessage(h, BM_SETCHECK, s.arrowKeySWPages ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_SPACE_AS_PAGEDOWN)))
        SendMessage(h, BM_SETCHECK, s.spaceAsPageDown ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_SPACE_AS_FIRST_CAND)))
        SendMessage(h, BM_SETCHECK, s.spaceAsFirstCandSelkey ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_AUTO_COMPOSE)))
        SendMessage(h, BM_SETCHECK, s.autoCompose ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_CLEAR_ON_BEEP)))
        SendMessage(h, BM_SETCHECK, s.clearOnBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_MAKE_PHRASE)))
        SendMessage(h, BM_SETCHECK, s.makePhrase ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_CUSTOM_TABLE_PRIORITY)))
        SendMessage(h, BM_SETCHECK, s.customTablePriority ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP)))
        SendMessage(h, BM_SETCHECK, s.doBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP_NOTIFY)))
        SendMessage(h, BM_SETCHECK, s.doBeepNotify ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP_CANDI)))
        SendMessage(h, BM_SETCHECK, s.doBeepOnCandi ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_IME_SHIFT_MODE))) {
        const wchar_t* m[] = { L"左右SHIFT鍵", L"右SHIFT鍵", L"左SHIFT鍵", L"無(僅Ctrl-Space鍵)" };
        ComboAddAndSelect(h, m, 4, s.imeShiftMode);
    }
    if ((h = FindControl(wd, CTRL_DOUBLE_SINGLE_BYTE))) {
        const wchar_t* d[] = { L"以 Shift-Space 熱鍵切換", L"半型", L"全型" };
        ComboAddAndSelect(h, d, 3, s.doubleSingleByteMode);
    }
    if ((h = FindControl(wd, CTRL_REVERSE_CONVERSION))) {
        // List is populated at startup and filtered by LoadConfig in SwitchMode
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"(無)");
        if (IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL))
            SendMessage(h, CB_SETCURSEL, (WPARAM)0, 0);
        CDIMEArray<LanguageProfileInfo>* infoList = CConfig::GetReverseConvervsionInfoList();
        if (infoList) {
            for (UINT i = 0; i < infoList->Count(); i++) {
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)infoList->GetAt(i)->description);
                if (IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), infoList->GetAt(i)->guidProfile))
                    SendMessage(h, CB_SETCURSEL, (WPARAM)i + 1, 0);
            }
        }
    }
    if ((h = FindControl(wd, CTRL_KEYBOARD_OPEN_CLOSE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"中文模式");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"英數模式");
        SendMessage(h, CB_SETCURSEL, s.activatedKeyboardMode ? 0 : 1, 0);
    }
    if ((h = FindControl(wd, CTRL_OUTPUT_CHT_CHS))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"繁體中文");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"簡體中文");
        SendMessage(h, CB_SETCURSEL, s.doHanConvert ? 1 : 0, 0);
    }
    if ((h = FindControl(wd, CTRL_DAYI_ARTICLE)))
        SendMessage(h, BM_SETCHECK, s.dayiArticleMode ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_FORCE_SP)))
        SendMessage(h, BM_SETCHECK, s.arrayForceSP ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_NOTIFY_SP)))
        SendMessage(h, BM_SETCHECK, s.arrayNotifySP ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_SINGLE_QUOTE)))
        SendMessage(h, BM_SETCHECK, s.arraySingleQuoteCustomPhrase ? BST_CHECKED : BST_UNCHECKED, 0);
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
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            int winW = LOWORD(lParam);
            int winH = HIWORD(lParam);
            int collapseBreak = ScaleForDpi(g_geo.sidebarCollapseBreak, dpi);
            bool isNarrow = (winW < collapseBreak);
            if (!isNarrow) {
                wd->sidebarCollapsed = false;
                wd->sidebarNarrowMode = false;
            } else {
                wd->sidebarNarrowMode = true;
                wd->sidebarCollapsed = true; // auto-collapse on resize
            }
            // In narrow mode, content always at x=0 full width
            int sidebarW = isNarrow ? 0 : ScaleForDpi(g_geo.sidebarWidth, dpi);
            MoveWindow(wd->hContentArea, sidebarW, 0, winW - sidebarW, winH, FALSE);
            RebuildContentArea(hWnd, wd);
            InvalidateRect(hWnd, nullptr, TRUE);
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
            PaintSidebar(hWnd, hdc, wd);
            // Paint breadcrumb (navLevel==1 only)
            if (wd->navLevel == 1 && wd->selectedModeIndex < 4 && wd->hContentArea) {
                UINT dpi = CConfig::GetDpiForHwnd(hWnd);
                int sidebarW = (!wd->sidebarNarrowMode && !wd->sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
                int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
                // Sidebar overlay covers breadcrumb in narrow mode when open
                if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
                    int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpi);
                    // Paint sidebar bg over breadcrumb strip (clipped to sidebar width)
                    HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, bcH);
                    int savedDC = SaveDC(hdc);
                    ExtSelectClipRgn(hdc, hClip, RGN_AND);
                    PaintSidebar(hWnd, hdc, wd);
                    RestoreDC(hdc, savedDC);
                    DeleteObject(hClip);
                    // Fill remainder of breadcrumb strip with content bg
                    RECT rcBcRest = { sidebarWOv, 0, 32767, bcH };
                    FillRect(hdc, &rcBcRest, wd->hBrushContentBg);
                    // Hamburger on top of sidebar overlay (so user can close it)
                    {
                        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
                        RECT rcHbBg = { 0, 0, hbSize, hbSize };
                        FillRect(hdc, &rcHbBg, wd->hBrushSidebarBg);
                        SetBkMode(hdc, TRANSPARENT);
                        if (wd->hasMDL2 && wd->hFontMDL2Icon) {
                            SelectObject(hdc, wd->hFontMDL2Icon);
                            SetTextColor(hdc, wd->theme.textPrimary);
                            DrawTextW(hdc, L"\xE700", -1, &rcHbBg, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                        } else {
                            SelectObject(hdc, wd->hFontCardHeader);
                            SetTextColor(hdc, wd->theme.textPrimary);
                            DrawTextW(hdc, L"\x2261", -1, &rcHbBg, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                        }
                    }
                } else {
                    RECT rcBC = { sidebarW, 0, 32767, bcH };
                    FillRect(hdc, &rcBC, wd->hBrushContentBg);
                    int savedDC = SaveDC(hdc);
                    SetViewportOrgEx(hdc, sidebarW, 0, nullptr);
                    PaintBreadcrumb(wd->hContentArea, hdc, wd);
                    RestoreDC(hdc, savedDC);
                    // Hamburger in breadcrumb strip (Level 1 narrow mode)
                    if (wd->sidebarNarrowMode) {
                        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
                        RECT rcHbBg = { sidebarW, 0, sidebarW + hbSize, hbSize };
                        FillRect(hdc, &rcHbBg, wd->hBrushContentBg);
                        RECT rcHb = { sidebarW, 0, sidebarW + hbSize, hbSize };
                        SetBkMode(hdc, TRANSPARENT);
                        if (wd->hasMDL2 && wd->hFontMDL2Icon) {
                            SelectObject(hdc, wd->hFontMDL2Icon);
                            SetTextColor(hdc, wd->theme.textPrimary);
                            DrawTextW(hdc, L"\xE700", -1, &rcHb, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                        } else {
                            SelectObject(hdc, wd->hFontCardHeader);
                            SetTextColor(hdc, wd->theme.textPrimary);
                            DrawTextW(hdc, L"\x2261", -1, &rcHb, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                        }
                    }
                }
            }
            // Hamburger and sidebar overlay are drawn in ContentWndProc (content area covers main window in narrow mode)
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
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int oldPos = wd->scrollPos;
            { UINT dpiW = CConfig::GetDpiForHwnd(hWnd); wd->scrollPos -= MulDiv(delta, ScaleForDpi(g_geo.rowHeight * 3, dpiW), WHEEL_DELTA); }
            wd->scrollPos = SettingsModel::ClampScrollPos(wd->scrollPos, wd->scrollMax);
            if (wd->scrollPos != oldPos) {
                UpdateScrollInfo(wd);
                RepositionControlsForScroll(wd);
                RedrawWindow(wd->hContentArea, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        // Sidebar click + breadcrumb back click
        if (!wd) break;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        UINT dpi = CConfig::GetDpiForHwnd(hWnd);
        int sidebarW = (!wd->sidebarNarrowMode && !wd->sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;

        // Hamburger click in breadcrumb strip (Level 1 narrow mode)
        if (wd->sidebarNarrowMode && wd->navLevel == 1) {
            int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
            if (x >= sidebarW && x < sidebarW + hbSize && y < hbSize) {
                wd->sidebarCollapsed = !wd->sidebarCollapsed;
                if (wd->sidebarCollapsed)
                    ShowAllControls(wd);
                else
                    for (auto& ch : wd->controlHandles)
                        if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
                InvalidateRect(wd->hContentArea, nullptr, TRUE);
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }

        // Breadcrumb back click (main window area above content area, navLevel==1 only)
        if (x >= sidebarW && wd->navLevel == 1 && wd->selectedModeIndex < 4) {
            int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
            if (y < bcH) {
                int margin = ScaleForDpi(g_geo.contentMargin, dpi);
                int relX = x - sidebarW;
                if (relX >= margin && relX < margin + ScaleForDpi(g_geo.bcModeNameW, dpi)) {
                    // Dirty check: if custom table editor has unsaved changes, ask user
                    HWND hSaveBtn = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                    if (hSaveBtn && IsWindowEnabled(hSaveBtn)) {
                        int result = ThemedMessageBox(hWnd,
                            L"自建詞庫尚未儲存，是否要儲存後離開？\n\n[是] 儲存並離開\n[否] 不儲存離開\n[取消] 繼續編輯",
                            L"自建詞庫未儲存", MB_YESNOCANCEL | MB_ICONQUESTION);
                        if (result == IDYES) {
                            SendMessageW(wd->hContentArea, WM_COMMAND,
                                MAKEWPARAM(3000 + CTRL_CUSTOM_TABLE_SAVE, BN_CLICKED),
                                (LPARAM)hSaveBtn);
                        } else if (result == IDCANCEL) {
                            return 0;
                        }
                    }
                    NavigateBack(hWnd, wd);
                    return 0;
                }
            }
            break; // click in content area but not on breadcrumb — ignore in WndProc
        }

        if (wd->sidebarCollapsed) break;
        if (x >= sidebarW) break;
        // Hamburger click handled in ContentWndProc

        int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
        int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
        int sidebarCount = 0;
        const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
        int idx = SettingsModel::SidebarHitTest(y - startY, itemH, sidebarCount);
        if (idx < 0 || idx >= sidebarCount) break;
        if (sItems[idx].iconResourceId == -1) break; // separator

        if (idx < 4) {
            IME_MODE newMode = IndexToMode(idx);
            if (newMode != wd->currentMode || wd->navLevel == 1 || wd->selectedModeIndex != idx)
                SwitchMode(hWnd, wd, newMode);
        } else if (idx == 5) {
            wd->selectedModeIndex = 5;
            wd->navLevel = 0;
            wd->scrollPos = 0;
            // Reset 載入碼表 expand states from layout defaults
            {
                int ltCount = 0;
                const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
                for (int li = 0; li < ltCount && (4 + li) < 16; li++)
                    wd->cardExpanded[4 + li] = ltCards[li].defaultExpanded;
            }
            RebuildContentArea(hWnd, wd);
            InvalidateRect(hWnd, nullptr, TRUE);
            if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
        }
        // Auto-collapse sidebar after selection in narrow mode
        if (wd->sidebarNarrowMode) {
            wd->sidebarCollapsed = true;
            ShowAllControls(wd);
            if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!wd) break;
        UINT dpi = CConfig::GetDpiForHwnd(hWnd);
        int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int newHover = -1;
        if (x < sidebarW && !wd->sidebarCollapsed) {
            int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
            int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
            int sc = 0; ::GetSidebarItems(&sc);
            newHover = SettingsModel::SidebarHitTest(y - startY, itemH, sc);
        }
        if (newHover != wd->hoverSidebarIndex) {
            wd->hoverSidebarIndex = newHover;
            RECT rcS = { 0, 0, sidebarW, 32767 };
            InvalidateRect(hWnd, &rcS, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (wd && wd->hoverSidebarIndex != -1) {
            wd->hoverSidebarIndex = -1;
            UINT dpi = CConfig::GetDpiForHwnd(hWnd);
            RECT rcS = { 0, 0, ScaleForDpi(g_geo.sidebarWidth, dpi), 32767 };
            InvalidateRect(hWnd, &rcS, FALSE);
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
        mmi->ptMinTrackSize.x = ScaleForDpi(g_geo.windowMinW, dpi2);
        mmi->ptMinTrackSize.y = ScaleForDpi(g_geo.windowMinH, dpi2);
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

            PaintContent(hWnd, hdc, wd);
            // Sidebar overlay — painted within off-screen DC, clipped to sidebar width
            if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
                HWND hMain = GetParent(hWnd);
                UINT dpiOv = CConfig::GetDpiForHwnd(hMain);
                int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpiOv);
                for (auto& ch : wd->controlHandles) {
                    if (ch.hWnd && IsWindow(ch.hWnd))
                        ShowWindow(ch.hWnd, SW_HIDE);
                }
                bool hasBc = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
                int bcOff = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpiOv) : 0;
                HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, 32767);
                int savedOv = SaveDC(hdc);
                ExtSelectClipRgn(hdc, hClip, RGN_AND);
                if (bcOff) SetViewportOrgEx(hdc, 0, -bcOff, nullptr);
                PaintSidebar(hMain, hdc, wd);
                RestoreDC(hdc, savedOv);
                DeleteObject(hClip);
            }
            // Hamburger in content area (Level 0 only; Level 1 draws it in breadcrumb strip)
            if (wd->sidebarNarrowMode && wd->navLevel == 0) {
                UINT dpiHb = CConfig::GetDpiForHwnd(GetParent(hWnd));
                int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpiHb);
                RECT rcHbBg = { 0, 0, hbSize, hbSize };
                FillRect(hdc, &rcHbBg, wd->hBrushContentBg);
                RECT rcHb = { 0, 0, hbSize, hbSize };
                SetBkMode(hdc, TRANSPARENT);
                if (wd->hasMDL2 && wd->hFontMDL2Icon) {
                    SelectObject(hdc, wd->hFontMDL2Icon);
                    SetTextColor(hdc, wd->theme.textPrimary);
                    DrawTextW(hdc, L"\xE700", -1, &rcHb, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                } else {
                    SelectObject(hdc, wd->hFontCardHeader);
                    SetTextColor(hdc, wd->theme.textPrimary);
                    DrawTextW(hdc, L"\x2261", -1, &rcHb, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                }
            }
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
    {
        if (!wd) break;
        int oldPos = wd->scrollPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:    { UINT dpi2 = CConfig::GetDpiForHwnd(hWnd); wd->scrollPos -= ScaleForDpi(g_geo.rowHeight, dpi2); break; }
        case SB_LINEDOWN:  { UINT dpi2 = CConfig::GetDpiForHwnd(hWnd); wd->scrollPos += ScaleForDpi(g_geo.rowHeight, dpi2); break; }
        case SB_PAGEUP:    wd->scrollPos -= wd->totalContentHeight / 4; break;
        case SB_PAGEDOWN:  wd->scrollPos += wd->totalContentHeight / 4; break;
        case SB_THUMBTRACK: wd->scrollPos = HIWORD(wParam); break;
        }
        wd->scrollPos = SettingsModel::ClampScrollPos(wd->scrollPos, wd->scrollMax);
        if (wd->scrollPos != oldPos) {
            UpdateScrollInfo(wd);
            RepositionControlsForScroll(wd);
            RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!wd) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int oldPos = wd->scrollPos;
        { UINT dpiW = CConfig::GetDpiForHwnd(hWnd); wd->scrollPos -= MulDiv(delta, ScaleForDpi(g_geo.rowHeight * 3, dpiW), WHEEL_DELTA); }
        wd->scrollPos = SettingsModel::ClampScrollPos(wd->scrollPos, wd->scrollMax);
        if (wd->scrollPos != oldPos) {
            UpdateScrollInfo(wd);
            RepositionControlsForScroll(wd);
            RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!wd) break;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        HWND hParent = GetParent(hWnd);
        UINT dpi = CConfig::GetDpiForHwnd(hParent);

        // Sidebar hover in narrow overlay mode
        if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
            int sidebarWM = ScaleForDpi(g_geo.sidebarWidth, dpi);
            if (x < sidebarWM) {
                // In Level 1, content area is offset by bcH — adjust Y to main window coords
                bool hasBcHv = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
                int bcOffHv = hasBcHv ? ScaleForDpi(g_geo.breadcrumbH, dpi) : 0;
                int mainYHv = y + bcOffHv;
                int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
                int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
                int sc = 0; ::GetSidebarItems(&sc);
                int newSbHover = SettingsModel::SidebarHitTest(mainYHv - startY, itemH, sc);
                if (newSbHover != wd->hoverSidebarIndex) {
                    wd->hoverSidebarIndex = newSbHover;
                    InvalidateRect(hWnd, nullptr, FALSE);
                    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd };
                    TrackMouseEvent(&tme);
                }
                return 0;
            } else {
                if (wd->hoverSidebarIndex != -1) {
                    wd->hoverSidebarIndex = -1;
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }

        int pad = ScaleForDpi(g_geo.cardPad, dpi);
        int catRowH2 = ScaleForDpi(g_geo.categoryRowH, dpi);
        int cardGapM = ScaleForDpi(g_geo.cardGap, dpi);

        int newHover = -1;
        if (wd->navLevel == 0 && wd->selectedModeIndex < 4) {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
            int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
            newHover = HitTestCardList(y, cards, layoutCount, modeMask, wd->cardExpanded, 0, dpi, wd->scrollPos);
        } else if (wd->selectedModeIndex == 5) {
            int ltCount = 0;
            const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
            newHover = HitTestCardList(y, ltCards, ltCount, SM_MODE_ALL, wd->cardExpanded, 4, dpi, wd->scrollPos);
        } else if (wd->navLevel == 1 && wd->selectedModeIndex < 4) {
            int layoutCount = 0;
            const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
            if (wd->currentCardIndex >= 0 && wd->currentCardIndex < layoutCount)
                newHover = HitTestRowCards(y, hWnd, wd, allCards[wd->currentCardIndex], dpi);
        }
        if (newHover != wd->hoverRowIndex) {
            wd->hoverRowIndex = newHover;
            InvalidateRect(hWnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (wd && wd->hoverRowIndex != -1) {
            wd->hoverRowIndex = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!wd) break;
        SetFocus(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        HWND hParent = GetParent(hWnd);
        UINT dpi = CConfig::GetDpiForHwnd(hParent);

        // Hamburger click — toggle sidebar overlay (MUST be before sidebar click handler)
        if (wd->sidebarNarrowMode) {
            int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
            if (x >= 0 && x < hbSize && y >= 0 && y < hbSize) {
                wd->sidebarCollapsed = !wd->sidebarCollapsed;
                if (wd->sidebarCollapsed)
                    ShowAllControls(wd);
                else
                    for (auto& ch : wd->controlHandles)
                        if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
                InvalidateRect(hWnd, nullptr, TRUE);
                InvalidateRect(hParent, nullptr, TRUE);
                return 0;
            }
        }

        // In narrow mode with sidebar shown as overlay — handle sidebar clicks here
        if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
            int sidebarWHT = ScaleForDpi(g_geo.sidebarWidth, dpi);
            if (x < sidebarWHT) {
                // Sidebar item click — compute Y in main window coords
                bool hasBcOv = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
                int bcOffOv = hasBcOv ? ScaleForDpi(g_geo.breadcrumbH, dpi) : 0;
                int mainY = y + bcOffOv;
                int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
                int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
                int sidebarCount = 0;
                const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
                int idx = SettingsModel::SidebarHitTest(mainY - startY, itemH, sidebarCount);
                if (idx >= 0 && idx < sidebarCount && sItems[idx].iconResourceId != -1) {
                    // Auto-collapse sidebar after selection
                    wd->sidebarCollapsed = true;
                    if (idx < 4) {
                        IME_MODE newMode = IndexToMode(idx);
                        if (newMode != wd->currentMode || wd->navLevel == 1 || wd->selectedModeIndex != idx)
                            SwitchMode(hParent, wd, newMode);
                    } else if (idx == 5) {
                        wd->selectedModeIndex = 5;
                        wd->navLevel = 0;
                        wd->scrollPos = 0;
                        int ltCount = 0;
                        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
                        for (int li = 0; li < ltCount && (4 + li) < 16; li++)
                            wd->cardExpanded[4 + li] = ltCards[li].defaultExpanded;
                        RebuildContentArea(hParent, wd);
                        InvalidateRect(hParent, nullptr, TRUE);
                        if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
                    }
                    ShowAllControls(wd);
                }
                return 0;
            }
            // Click outside sidebar — auto-collapse
            wd->sidebarCollapsed = true;
            ShowAllControls(wd);
            InvalidateRect(hWnd, nullptr, TRUE);
            InvalidateRect(hParent, nullptr, TRUE);
            // Don't return — let the click pass through to content
        }

        if (wd->navLevel == 0 && wd->selectedModeIndex < 4) {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
            int modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
            if (HandleCardListClick(y, hWnd, hParent, wd, cards, layoutCount, modeMask, 0, dpi))
                return 0;
        } else if (wd->navLevel == 1) {
            int layoutCount = 0;
            const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
            if (wd->currentCardIndex >= 0 && wd->currentCardIndex < layoutCount) {
                if (HandleRowCardClick(y, hWnd, wd, allCards[wd->currentCardIndex], dpi))
                    return 0;
            }
        } else if (wd->selectedModeIndex == 5) {
            int ltPageCount = 0;
            const LayoutCard* ltCards = GetLoadTablePageLayout(&ltPageCount);
            if (HandleCardListClick(y, hWnd, hParent, wd, ltCards, ltPageCount, SM_MODE_ALL, 4, dpi))
                return 0;
        }
        return 0;
    }

    case WM_COMMAND:
    {
        if (!wd) break;
        int notifyCode = HIWORD(wParam);
        int ctrlId = LOWORD(wParam);

        if (notifyCode == BN_CLICKED) {
            // Action-based dispatch: find LayoutRow by id, dispatch by row->action
            SettingsControlId btnId = (SettingsControlId)(ctrlId - 3000);
            RowAction action = RowAction::None;

            // Search all layout trees for matching row
            auto findAction = [&](const LayoutRow* rows, int count) {
                for (int i = 0; i < count; i++)
                    if (rows[i].id == btnId) { action = rows[i].action; return; }
            };
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
            for (int ci = 0; ci < layoutCount && action == RowAction::None; ci++)
                if (cards[ci].rows) findAction(cards[ci].rows, cards[ci].rowCount);
            if (action == RowAction::None) {
                int ltCount = 0;
                const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
                for (int ci = 0; ci < ltCount && action == RowAction::None; ci++)
                    if (ltCards[ci].rows) findAction(ltCards[ci].rows, ltCards[ci].rowCount);
            }
            // Programmatically created button not in layout tree
            if (action == RowAction::None && btnId == CTRL_CUSTOM_TABLE_SAVE)
                action = RowAction::SaveCustomTable;

            switch (action) {
            case RowAction::OpenFontDialog:
            {
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pCF = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSEFONTW)>(GetProcAddress(hDlgLib, "ChooseFontW"));
                    if (pCF) {
                        HDC hdcScr = GetDC(hWnd);
                        int lpY = GetDeviceCaps(hdcScr, LOGPIXELSY);
                        ReleaseDC(hWnd, hdcScr);
                        LOGFONTW lf = {};
                        StringCchCopyW(lf.lfFaceName, LF_FACESIZE, wd->snapshot.fontFaceName);
                        lf.lfHeight = -MulDiv(wd->snapshot.fontSize, lpY, 72);
                        lf.lfWeight = wd->snapshot.fontWeight;
                        lf.lfItalic = (BYTE)wd->snapshot.fontItalic;
                        lf.lfCharSet = DEFAULT_CHARSET;
                        CHOOSEFONTW cf = {};
                        cf.lStructSize = sizeof(cf);
                        cf.hwndOwner = GetParent(hWnd);
                        cf.lpLogFont = &lf;
                        cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
                        if (pCF(&cf)) {
                            StringCchCopyW(wd->snapshot.fontFaceName, LF_FACESIZE, lf.lfFaceName);
                            wd->snapshot.fontSize = cf.iPointSize / 10;
                            wd->snapshot.fontWeight = lf.lfWeight;
                            wd->snapshot.fontItalic = lf.lfItalic;
                            ApplyAndSave(wd);
                            // Preview height changed — rebuild controls at new positions
                            RebuildContentArea(GetParent(hWnd), wd);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            case RowAction::ResetDefaults:
            {
                CConfig::ResetAllDefaults(wd->currentMode);
                CConfig::WriteConfig(wd->currentMode);
                CConfig::LoadConfig(wd->currentMode);
                wd->snapshot = SettingsModel::LoadFromConfig();
                RebuildContentArea(GetParent(hWnd), wd);
                InvalidateRect(hWnd, nullptr, FALSE);
                UpdateWindow(hWnd);
                break;
            }
            case RowAction::LoadTable:
            {
                // Determine target .cin filename based on control id
                const wchar_t* targetSuffix = SettingsModel::GetLoadTableSuffix((SettingsControlId)btnId);
                if (targetSuffix) {
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
                            ofn.lpstrFilter = L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0";
                            if (pGetOpen(&ofn)) {
                                WCHAR wszAppData[MAX_PATH] = {};
                                SHGetSpecialFolderPathW(nullptr, wszAppData, CSIDL_APPDATA, TRUE);
                                WCHAR pathToWrite[MAX_PATH] = {};
                                StringCchPrintfW(pathToWrite, MAX_PATH, L"%s%s", wszAppData, targetSuffix);
                                if (CConfig::parseCINFile(pathToLoad, pathToWrite))
                                    ThemedMessageBox(GetParent(hWnd), L"自訂碼表載入完成。", L"DIME 自訂碼表", MB_ICONINFORMATION);
                                else
                                    ThemedMessageBox(GetParent(hWnd), L"自訂碼表載入發生錯誤 !!", L"DIME 自訂碼表", MB_ICONERROR);
                            }
                        }
                        FreeLibrary(hDlgLib);
                    }
                }
                break;
            }
            case RowAction::SaveCustomTable:
            {
                // Validate before saving — abort if errors found (same as ConfigDialog PSN_APPLY)
                HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                if (hRE && !ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine)) {
                    const wchar_t* msgText = wd->isDarkTheme
                        ? L"自建詞庫格式錯誤：\n\n"
                          L"• 淡洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
                          L"• 亮橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
                          L"• 淡紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
                          L"請修正所有標示的錯誤後再套用"
                        : L"自建詞庫格式錯誤：\n\n"
                          L"• 洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
                          L"• 橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
                          L"• 紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
                          L"請修正所有標示的錯誤後再套用";
                    ThemedMessageBox(GetParent(hWnd), msgText, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
                    break;
                }
                // Save RichEdit content to per-IME custom table file
                if (hRE) {
                    WCHAR txtPath[MAX_PATH] = {};
                    GetCustomTableTxtPath(wd->currentMode, txtPath, MAX_PATH);

                    DWORD dwLen = GetWindowTextLengthW(hRE);
                    LPWSTR text = new (std::nothrow) WCHAR[dwLen + 1];
                    if (text) {
                        ZeroMemory(text, (dwLen + 1) * sizeof(WCHAR));
                        GetWindowTextW(hRE, text, dwLen + 1);
                        HANDLE hFile = CreateFileW(txtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            WCHAR bom = 0xFEFF;
                            WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
                            WriteFile(hFile, text, dwLen * sizeof(WCHAR), &written, nullptr);
                            CloseHandle(hFile);
                            // Parse .txt to .cin so DIME picks up the changes
                            WCHAR cinPath[MAX_PATH] = {};
                            GetCustomTableCINPath(wd->currentMode, cinPath, MAX_PATH);
                            if (!CConfig::parseCINFile(txtPath, cinPath, TRUE))
                                ThemedMessageBox(GetParent(hWnd), L"自建詞庫載入發生錯誤！", L"DIME 自建詞庫", MB_ICONERROR);
                        }
                        delete[] text;
                    }
                    // Disable save button (no longer dirty)
                    HWND hSaveBtn = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                    if (hSaveBtn) EnableWindow(hSaveBtn, FALSE);
                }
                break;
            }
            case RowAction::ExportCustomTable:
            {
                // Validate before export — abort if errors found
                {
                    HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                    if (hRE && !ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine)) {
                        const wchar_t* msgText = wd->isDarkTheme
                            ? L"自建詞庫格式錯誤：\n\n"
                              L"• 淡洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
                              L"• 亮橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
                              L"• 淡紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
                              L"請修正所有標示的錯誤後再套用"
                            : L"自建詞庫格式錯誤：\n\n"
                              L"• 洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
                              L"• 橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
                              L"• 紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
                              L"請修正所有標示的錯誤後再套用";
                        ThemedMessageBox(GetParent(hWnd), msgText, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
                        break;
                    }
                }
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pGetSave = reinterpret_cast<_T_GetSaveFileName>(GetProcAddress(hDlgLib, "GetSaveFileNameW"));
                    if (pGetSave) {
                        WCHAR pathToWrite[MAX_PATH] = {};
                        WCHAR wszDoc[MAX_PATH] = {};
                        SHGetSpecialFolderPathW(nullptr, wszDoc, CSIDL_PERSONAL, FALSE);
                        StringCchPrintfW(pathToWrite, MAX_PATH, L"%s\\customTable.txt", wszDoc);
                        OPENFILENAMEW ofn = {};
                        ofn.lStructSize = sizeof(OPENFILENAMEW);
                        ofn.hwndOwner = GetParent(hWnd);
                        ofn.lpstrFile = pathToWrite;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT;
                        ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
                        if (pGetSave(&ofn)) {
                            // Write RichEdit content to selected file
                            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                            if (hRE) {
                                DWORD dwLen = GetWindowTextLengthW(hRE);
                                LPWSTR text = new (std::nothrow) WCHAR[dwLen + 1];
                                if (text) {
                                    ZeroMemory(text, (dwLen + 1) * sizeof(WCHAR));
                                    GetWindowTextW(hRE, text, dwLen + 1);
                                    HANDLE hFile = CreateFileW(pathToWrite, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                                    if (hFile != INVALID_HANDLE_VALUE) {
                                        DWORD written = 0;
                                        WCHAR bom = 0xFEFF;
                                        WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
                                        WriteFile(hFile, text, dwLen * sizeof(WCHAR), &written, nullptr);
                                        CloseHandle(hFile);
                                    }
                                    delete[] text;
                                }
                            }
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            case RowAction::ImportCustomTable:
            {
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
                        ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
                        if (pGetOpen(&ofn)) {
                            // Read file and set into RichEdit
                            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                            if (hRE) {
                                HANDLE hFile = CreateFileW(pathToLoad, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                                if (hFile != INVALID_HANDLE_VALUE) {
                                    DWORD dwSize = GetFileSize(hFile, nullptr);
                                    if (dwSize != INVALID_FILE_SIZE) {
                                        LPWSTR buf = new (std::nothrow) WCHAR[dwSize + 1];
                                        if (buf) {
                                            ZeroMemory(buf, (dwSize + 1) * sizeof(WCHAR));
                                            DWORD dwRead = 0;
                                            ReadFile(hFile, buf, dwSize, &dwRead, nullptr);
                                            // Skip BOM if present
                                            LPCWSTR text = buf;
                                            if (dwRead >= 2 && buf[0] == 0xFEFF) text = buf + 1;
                                            SetWindowTextW(hRE, text);
                                            delete[] buf;
                                        }
                                    }
                                    CloseHandle(hFile);
                                }
                                // Enable save button (content changed)
                                HWND hSaveBtn = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                                if (hSaveBtn) EnableWindow(hSaveBtn, TRUE);
                            }
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            default: break;
            }
        }

        if (notifyCode == EN_CHANGE) {
            int settingsId = ctrlId - 3000;
            if ((SettingsControlId)settingsId == CTRL_CUSTOM_TABLE_EDITOR) {
                // Enable [儲存] button (mark dirty)
                HWND hSave = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                if (hSave) EnableWindow(hSave, TRUE);

                // Smart validation (mirrors ConfigDialog EN_CHANGE logic)
                HWND hRE = (HWND)lParam;
                CHARRANGE sel;
                SendMessageW(hRE, EM_EXGETSEL, 0, (LPARAM)&sel);
                int currentLine = (int)SendMessageW(hRE, EM_LINEFROMCHAR, (WPARAM)sel.cpMin, 0);
                int currentLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);

                bool structuralChange = (wd->ctLastLineCount > 0 && currentLineCount != wd->ctLastLineCount);
                wd->ctLastLineCount = currentLineCount;

                if (structuralChange) {
                    ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);
                    wd->ctKeystrokeCount = 0;
                    wd->ctLastEditedLine = currentLine;
                } else {
                    wd->ctKeystrokeCount++;
                    if (currentLine != wd->ctLastEditedLine) {
                        ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);
                        wd->ctLastEditedLine = currentLine;
                        wd->ctKeystrokeCount = 0;
                    } else if (wd->ctKeystrokeCount >= CUSTOM_TABLE_VALIDATE_KEYSTROKE_THRESHOLD) {
                        ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);
                        wd->ctKeystrokeCount = 0;
                    }
                }
            }
            else if ((SettingsControlId)settingsId == CTRL_MAX_CODES) {
                WCHAR buf[16] = {};
                GetWindowTextW((HWND)lParam, buf, 16);
                UINT val;
                if (SettingsModel::ParseMaxCodes(buf, val)) {
                    wd->snapshot.maxCodes = val;
                    ApplyAndSave(wd);
                }
            }
        }

        if (notifyCode == CBN_SELCHANGE) {
            int settingsId = ctrlId - 3000;
            HWND hCombo = (HWND)lParam;
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                switch ((SettingsControlId)settingsId) {
                case CTRL_COLOR_MODE: {
                    // Uses item data, not direct index
                    int itemData = (int)SendMessage(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
                    wd->snapshot.colorMode = itemData;
                    break;
                }
                case CTRL_ARRAY_SCOPE: {
                    int itemData = (int)SendMessage(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
                    wd->snapshot.arrayScope = itemData;
                    break;
                }
                case CTRL_REVERSE_CONVERSION:  CConfig::SetReverseConversionSelection(sel); break;
                default:
                    SettingsModel::SyncComboToSnapshot(wd->snapshot, (SettingsControlId)settingsId, sel);
                    break;
                }
                if ((SettingsControlId)settingsId == CTRL_COLOR_MODE) {
                    // Auto-expand color grid when switching to custom
                    wd->colorGridExpanded = (wd->snapshot.colorMode == 3);
                    // Only save the color mode enum — don't overwrite palette colors
                    CConfig::SetColorMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                    CConfig::WriteConfig(wd->currentMode);
                    // Load the new mode's palette into snapshot for preview
                    CConfig::LoadColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                    const ColorInfo* clrs = CConfig::GetColors();
                    wd->snapshot.itemColor      = clrs[0].color;
                    wd->snapshot.selectedColor  = clrs[1].color;
                    wd->snapshot.itemBGColor    = clrs[2].color;
                    wd->snapshot.phraseColor    = clrs[3].color;
                    wd->snapshot.numberColor    = clrs[4].color;
                    wd->snapshot.selectedBGColor = clrs[5].color;
                    RebuildContentArea(GetParent(hWnd), wd);
                } else {
                    ApplyAndSave(wd);
                }
                InvalidateRect(hWnd, nullptr, FALSE);
                UpdateWindow(hWnd);
            }
        }

        // Color swatch click (STN_CLICKED or BN_CLICKED from swatch statics)
        if (notifyCode == STN_CLICKED || (notifyCode == BN_CLICKED && ctrlId >= 4000 && ctrlId < 4006)) {
            int swIdx = ctrlId - 4000;
            if (swIdx >= 0 && swIdx < 6) {
                static COLORREF custColors[16] = {};
                COLORREF* fields[] = {
                    &wd->snapshot.itemColor, &wd->snapshot.selectedColor, &wd->snapshot.itemBGColor,
                    &wd->snapshot.phraseColor, &wd->snapshot.numberColor, &wd->snapshot.selectedBGColor
                };
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pCC = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSECOLORW)>(GetProcAddress(hDlgLib, "ChooseColorW"));
                    if (pCC) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = GetParent(hWnd);
                        cc.rgbResult = *fields[swIdx];
                        cc.lpCustColors = custColors;
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (pCC(&cc)) {
                            *fields[swIdx] = cc.rgbResult;
                            // Save custom colors to the current mode's palette
                            CConfig::LoadColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                            // Update colors[] with the new swatch value
                            const_cast<ColorInfo*>(CConfig::GetColors())[swIdx].color = cc.rgbResult;
                            CConfig::SaveColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                            ApplyAndSave(wd);
                            // Invalidate the swatch control so it redraws with new color
                            InvalidateRect((HWND)lParam, nullptr, TRUE);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
            }
        }
        break;
    }

    case WM_USER + 1:  // Deferred validation: posted by subclass proc after Enter/Space/Paste/large-delete
    {
        if (!wd) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (hRE) {
            ValidateCustomTableRE(hRE, wd->currentMode, wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);
            wd->ctKeystrokeCount = 0;
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        if (!wd) break;
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pdis) break;

        // Owner-draw buttons (Win11 style: anti-aliased rounded corners via GDI+)
        if (pdis->CtlType == ODT_BUTTON) {
            const ThemeColors& tc = wd->theme;
            HDC hdc = pdis->hDC;
            RECT rc = pdis->rcItem;
            UINT dpiBtn = CConfig::GetDpiForHwnd(GetParent(hWnd));
            Gdiplus::REAL r = (Gdiplus::REAL)ScaleForDpi(4, dpiBtn);

            bool pressed  = (pdis->itemState & ODS_SELECTED) != 0;
            bool disabled = (pdis->itemState & ODS_DISABLED) != 0;

            COLORREF fill   = disabled ? tc.buttonFillDisabled :
                              pressed  ? tc.buttonFillPressed  : tc.buttonFill;
            COLORREF border = tc.buttonBorder;
            COLORREF text   = disabled ? tc.buttonTextDisabled : tc.buttonText;

            auto toColor = [](COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); };

            // Fill background with card bg to eliminate white corners
            HBRUSH hBgBr = CreateSolidBrush(tc.cardBg);
            FillRect(hdc, &rc, hBgBr);
            DeleteObject(hBgBr);

            // Anti-aliased rounded button
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            Gdiplus::RectF rr((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                (Gdiplus::REAL)(rc.right - rc.left - 1), (Gdiplus::REAL)(rc.bottom - rc.top - 1));
            Gdiplus::REAL d = r * 2;
            path.AddArc(rr.X, rr.Y, d, d, 180, 90);
            path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
            path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
            path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush fillBrush(toColor(fill));
            g.FillPath(&fillBrush, &path);
            Gdiplus::Pen borderPen(toColor(border), 1.0f);
            g.DrawPath(&borderPen, &path);

            wchar_t txt[128] = {};
            GetWindowTextW(pdis->hwndItem, txt, 128);
            if (txt[0]) {
                HFONT hOldF = (HFONT)SelectObject(hdc, wd->hFontBody);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, text);
                DrawTextW(hdc, txt, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(hdc, hOldF);
            }
            return TRUE;
        }

        // Color swatches
        if (pdis->CtlType == ODT_STATIC && pdis->CtlID >= 4000 && pdis->CtlID < 4006) {
            int idx = pdis->CtlID - 4000;
            COLORREF colors[] = {
                wd->snapshot.itemColor, wd->snapshot.selectedColor, wd->snapshot.itemBGColor,
                wd->snapshot.phraseColor, wd->snapshot.numberColor, wd->snapshot.selectedBGColor
            };
            HDC hdc = pdis->hDC;
            RECT rc = pdis->rcItem;
            HBRUSH hBr = CreateSolidBrush(colors[idx]);
            HPEN hPen = CreatePen(PS_SOLID, 1, wd->theme.cardBorder);
            SelectObject(hdc, hBr); SelectObject(hdc, hPen);
            int r = ScaleForDpi(g_geo.swatchRadius, CConfig::GetDpiForHwnd(GetParent(hWnd)));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
            DeleteObject(hBr); DeleteObject(hPen);
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
