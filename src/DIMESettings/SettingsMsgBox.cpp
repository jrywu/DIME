// SettingsMsgBox.cpp — ThemedMessageBox. See SettingsMsgBox.h. The Cpp:: code
// is the window that used to live in SettingsWindow.cpp; dime-settings
// (msgboxwin.rs) mirrors it.

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <strsafe.h>
#include "..\define.h"
#include "..\BaseStructure.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "SettingsMsgBox.h"
#include "SettingsWindowLayout.h"
#include "SettingsWindowPaint.h"
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#endif
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace SettingsMsgBox {

namespace Cpp {

namespace {

struct MsgBoxData {
    const wchar_t* text;
    const wchar_t* caption;
    UINT type;
    ThemeColors tc;
    bool isDark;
    SettingsWindowLayout::MsgBoxButtons buttons;   // ids and label indexes
    int btnCount;
    int btnIds[3];
    const wchar_t* btnLabels[3];
    int defaultBtn;
    int hoverBtn;
    HICON hIcon;
    HFONT hFont;
    DpiForWindowFn dpiFor;
    int result;
};

using Layout = SettingsWindowLayout::MsgBoxLayout;

// Measure the wrapped text here; the arithmetic is SettingsWindowLayout.
Layout CalcLayout(HWND hWnd, MsgBoxData* md, UINT dpi)
{
    int maxTextW = SettingsWindowLayout::MsgBoxMaxTextWidth(dpi);
    HDC hdc = GetDC(hWnd);
    HFONT oldF = (HFONT)SelectObject(hdc, md->hFont);
    RECT rcM = { 0, 0, maxTextW, 0 };
    DrawTextW(hdc, md->text, -1, &rcM, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
    SelectObject(hdc, oldF);
    ReleaseDC(hWnd, hdc);
    return SettingsWindowLayout::MsgBoxLayoutFor(md->hIcon != nullptr, md->btnCount,
        rcM.right, rcM.bottom, dpi);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
        UINT dpi = md->dpiFor(hWnd);
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
        UINT dpi = md->dpiFor(hWnd);
        Layout L = CalcLayout(hWnd, md, dpi);
        SettingsWindowPaint::PaintMsgBox(hdc, L, md->text, md->btnLabels, md->btnCount,
            md->hoverBtn, md->defaultBtn, md->tc, md->hFont, md->hIcon, dpi);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!md) break;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        UINT dpi = md->dpiFor(hWnd);
        Layout L = CalcLayout(hWnd, md, dpi);
        int newHover = SettingsWindowLayout::MsgBoxButtonAt(L, md->btnCount, x, y);
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
        UINT dpi = md->dpiFor(hWnd);
        Layout L = CalcLayout(hWnd, md, dpi);
        int hit = SettingsWindowLayout::MsgBoxButtonAt(L, md->btnCount, x, y);
        if (hit >= 0) {
            md->result = md->btnIds[hit];
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (!md) break;
        int r = SettingsWindowLayout::MsgBoxKeyResult(md->buttons, md->defaultBtn, (UINT)wParam);
        if (r == -1) break;
        md->result = r;
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_CLOSE:
        if (md) {
            int r = SettingsWindowLayout::MsgBoxCloseResult(md->buttons);
            if (r != -1) md->result = r;
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

std::vector<std::wstring>& RegisteredClasses()
{
    static std::vector<std::wstring> names;
    return names;
}

} // namespace

int Show(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type,
    const wchar_t* className, bool isDark, const ThemeColors& tc, DpiForWindowFn dpiFor)
{
    auto& names = RegisteredClasses();
    bool known = false;
    for (const auto& n : names) if (n == className) known = true;
    if (!known) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        names.push_back(className);
    }

    MsgBoxData md = {};
    md.text = text;
    md.caption = caption;
    md.type = type;
    md.isDark = isDark;
    md.tc = tc;
    md.hoverBtn = -1;
    md.defaultBtn = 0;
    md.dpiFor = dpiFor;

    static const wchar_t* const kLabels[] = { L"是(Y)", L"否(N)", L"取消", L"確定" };
    md.buttons = SettingsWindowLayout::MsgBoxButtonsFor(type);
    md.btnCount = md.buttons.count;
    for (int i = 0; i < md.btnCount && i < 3; i++) {
        md.btnIds[i] = md.buttons.ids[i];
        int li = md.buttons.labels[i];
        md.btnLabels[i] = (li >= 0 && li < 4) ? kLabels[li] : L"";
    }

    switch (SettingsWindowLayout::MsgBoxIcon(type)) {
    case 1: md.hIcon = LoadIcon(nullptr, IDI_WARNING); break;
    case 2: md.hIcon = LoadIcon(nullptr, IDI_ERROR); break;
    case 3: md.hIcon = LoadIcon(nullptr, IDI_QUESTION); break;
    case 4: md.hIcon = LoadIcon(nullptr, IDI_INFORMATION); break;
    default: break;
    }

    UINT dpi = dpiFor(hParent ? hParent : GetDesktopWindow());
    md.hFont = CreateFontW(-ScaleForDpi(12, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HWND hMsgBox = CreateWindowExW(WS_EX_DLGMODALFRAME,
        className, caption, WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 1, 1, hParent, nullptr, GetModuleHandle(nullptr), &md);

    if (!hMsgBox) {
        if (md.hFont) DeleteObject(md.hFont);
        return MessageBoxW(hParent, text, caption, type);
    }

    if (hParent) EnableWindow(hParent, FALSE);
    ShowWindow(hMsgBox, SW_SHOW);
    UpdateWindow(hMsgBox);
    SetFocus(hMsgBox);

    md.result = IDCANCEL;
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hParent) { EnableWindow(hParent, TRUE); SetForegroundWindow(hParent); }
    return md.result;
}

} // namespace Cpp

#ifdef DIME_USE_RUST_CONFIG
static uint32_t DpiForWindowRaw(void* hWnd) { return CConfig::GetDpiForHwnd(static_cast<HWND>(hWnd)); }
#else
static UINT DpiForWindow(HWND hWnd) { return CConfig::GetDpiForHwnd(hWnd); }
#endif

int Show(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    bool isDark = CConfig::IsSystemDarkTheme();
    ThemeColors tc = ComputeThemeColors(isDark);
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_message_box(hParent, reinterpret_cast<const uint16_t*>(text),
        reinterpret_cast<const uint16_t*>(caption), type, reinterpret_cast<const uint16_t*>(kClassName),
        isDark ? 1 : 0, &tc, &DpiForWindowRaw);
#else
    return Cpp::Show(hParent, text, caption, type, kClassName, isDark, tc, &DpiForWindow);
#endif
}

} // namespace SettingsMsgBox
