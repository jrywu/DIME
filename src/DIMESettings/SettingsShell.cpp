// SettingsShell.cpp — see SettingsShell.h. The Cpp functions are what used to
// be in SettingsWindow.cpp (window placement, theme brushes, the WM_PAINT
// double buffering, class registration, Run); dime-settings (shell.rs)
// mirrors them.

#include <windows.h>
#include <gdiplus.h>
#include "..\BaseStructure.h"
#include "SettingsShell.h"
#include "SettingsFrame.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

#pragma comment(lib, "gdiplus.lib")

namespace SettingsShell {

namespace Cpp {

bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(hWnd, &wp)) return false;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr,
        0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, value, 0, REG_BINARY, (const BYTE*)&wp, sizeof(wp));
    RegCloseKey(hKey);
    return true;
}

bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    WINDOWPLACEMENT wp = {};
    DWORD cbData = sizeof(wp);
    DWORD dwType = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, value,
        RRF_RT_REG_BINARY, &dwType, &wp, &cbData) != ERROR_SUCCESS) return false;
    if (cbData != sizeof(wp) || wp.length != sizeof(wp)) return false;
    // Verify saved position is on a connected monitor
    if (!MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL)) return false;
    SetWindowPlacement(hWnd, &wp);
    return true;
}

void DeletePlacement(const wchar_t* key, const wchar_t* value)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, value);
        RegCloseKey(hKey);
    }
}

Brushes CreateBrushes(const ThemeColors& theme)
{
    Brushes b = {};
    b.sidebarBg = CreateSolidBrush(theme.sidebarBg);
    b.contentBg = CreateSolidBrush(theme.contentBg);
    b.cardBg    = CreateSolidBrush(theme.cardBg);
    return b;
}

void DestroyBrushes(Brushes& b)
{
    if (b.sidebarBg) { DeleteObject(b.sidebarBg); b.sidebarBg = nullptr; }
    if (b.contentBg) { DeleteObject(b.contentBg); b.contentBg = nullptr; }
    if (b.cardBg)    { DeleteObject(b.cardBg);    b.cardBg = nullptr; }
}

void PaintMainInto(HDC target, int w, int h, const PaintSource& src)
{
    HDC hdc = target;
    HDC hdcMem = CreateCompatibleDC(target);
    HBITMAP hBmpMem = CreateCompatibleBitmap(target, w, h);
    HBITMAP hOldBmpMem = (HBITMAP)SelectObject(hdcMem, hBmpMem);
    if (hdcMem) hdc = hdcMem;
    if (src.hasData(src.user)) {
        SettingsWindowPaint::PaintContext main = {};
        int hasContent = 0;
        src.prepare(src.user, 0, &main, nullptr, &hasContent);
        SettingsWindowPaint::PaintMainFrame(hdc, main, hasContent != 0);
    }
    if (hdcMem) {
        BitBlt(target, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBmpMem);
        DeleteObject(hBmpMem);
        DeleteDC(hdcMem);
    }
}

void PaintContentInto(HDC target, int w, int h, const PaintSource& src)
{
    if (!src.hasData(src.user)) return;
    // Double-buffer: paint to off-screen bitmap, then BitBlt to screen
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP hBmp = CreateCompatibleBitmap(target, w, h);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);
    SettingsWindowPaint::PaintContext content = {}, main = {};
    int unused = 0;
    src.prepare(src.user, 1, &content, &main, &unused);
    SettingsWindowPaint::PaintContentFrame(hdc, content, main);
    // Blit off-screen buffer to screen (excludes child control areas via WS_CLIPCHILDREN)
    BitBlt(target, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdc);
}

void PaintMain(HWND hWnd, const PaintSource& src)
{
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);
    Cpp::PaintMainInto(hdcScreen, rc.right, rc.bottom, src);
    EndPaint(hWnd, &ps);
}

void PaintContent(HWND hWnd, const PaintSource& src)
{
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);
    Cpp::PaintContentInto(hdcScreen, rc.right, rc.bottom, src);
    EndPaint(hWnd, &ps);
}

bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = proc;
    wcex.hInstance = instance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = name;
    wcex.hIcon = LoadIcon(instance, MAKEINTRESOURCE(iconId));
    wcex.hIconSm = wcex.hIcon;
    return RegisterClassExW(&wcex) != 0;
}

void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc)
{
    WNDCLASSEXW existing = { sizeof(existing) };
    if (GetClassInfoExW(instance, name, &existing)) return;
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = proc;
    wcex.hInstance = instance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = name;
    RegisterClassExW(&wcex);
}

int Run(const RunParams& p)
{
    // Initialize GDI+ for anti-aliased toggle switches
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    Cpp::RegisterMainClass(p.instance, p.mainClass, p.mainProc, p.iconId);
    UINT dpi = 96;
    { HDC hdc = GetDC(nullptr); if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(nullptr, hdc); } }
    SIZE initial = SettingsFrame::InitialSize(dpi);

    HWND hWnd = CreateWindowExW(0, p.mainClass, p.caption,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initial.cx, initial.cy,
        nullptr, nullptr, p.instance, nullptr);
    if (!hWnd) return -1;

    if (p.started) p.started(p.user, hWnd);
    // Restore saved window placement, or use default (CW_USEDEFAULT + nCmdShow)
    if (!Cpp::RestorePlacement(hWnd, p.placementKey, p.placementValue))
        ShowWindow(hWnd, p.nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(DimeSettingsPaintSource) == sizeof(PaintSource), "DimeSettingsPaintSource");
static_assert(sizeof(DimeSettingsRunParams) == sizeof(RunParams), "DimeSettingsRunParams");
static_assert(sizeof(Brushes) == 3 * sizeof(void*), "Brushes");

namespace Rust {

static const DimeSettingsPaintSource* Src(const PaintSource& s)
{
    return reinterpret_cast<const DimeSettingsPaintSource*>(&s);
}

bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    return dime_settings_save_placement(hWnd, (const uint16_t*)key, (const uint16_t*)value) != 0;
}
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    return dime_settings_restore_placement(hWnd, (const uint16_t*)key, (const uint16_t*)value) != 0;
}
void DeletePlacement(const wchar_t* key, const wchar_t* value)
{
    dime_settings_delete_placement((const uint16_t*)key, (const uint16_t*)value);
}
Brushes CreateBrushes(const ThemeColors& theme)
{
    Brushes b = {};
    dime_settings_create_brushes(&theme, (void**)&b);
    return b;
}
void DestroyBrushes(Brushes& b) { dime_settings_destroy_brushes((void**)&b); }
void PaintMain(HWND hWnd, const PaintSource& src) { dime_settings_shell_paint_main(hWnd, Src(src)); }
void PaintContent(HWND hWnd, const PaintSource& src) { dime_settings_shell_paint_content(hWnd, Src(src)); }
void PaintMainInto(HDC target, int w, int h, const PaintSource& src)
{
    dime_settings_shell_paint_main_into(target, w, h, Src(src));
}
void PaintContentInto(HDC target, int w, int h, const PaintSource& src)
{
    dime_settings_shell_paint_content_into(target, w, h, Src(src));
}
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId)
{
    return dime_settings_register_main_class(instance, (const uint16_t*)name, (const void*)proc, iconId) != 0;
}
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc)
{
    dime_settings_register_content_class(instance, (const uint16_t*)name, (const void*)proc);
}
int Run(const RunParams& p)
{
    return dime_settings_shell_run(reinterpret_cast<const DimeSettingsRunParams*>(&p));
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value) { return Impl::SavePlacement(hWnd, key, value); }
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value) { return Impl::RestorePlacement(hWnd, key, value); }
void DeletePlacement(const wchar_t* key, const wchar_t* value) { Impl::DeletePlacement(key, value); }
Brushes CreateBrushes(const ThemeColors& theme) { return Impl::CreateBrushes(theme); }
void DestroyBrushes(Brushes& b) { Impl::DestroyBrushes(b); }
void PaintMain(HWND hWnd, const PaintSource& src) { Impl::PaintMain(hWnd, src); }
void PaintContent(HWND hWnd, const PaintSource& src) { Impl::PaintContent(hWnd, src); }
void PaintMainInto(HDC target, int w, int h, const PaintSource& src) { Impl::PaintMainInto(target, w, h, src); }
void PaintContentInto(HDC target, int w, int h, const PaintSource& src) { Impl::PaintContentInto(target, w, h, src); }
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId)
{
    return Impl::RegisterMainClass(instance, name, proc, iconId);
}
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc)
{
    Impl::RegisterContentClass(instance, name, proc);
}
int Run(const RunParams& p) { return Impl::Run(p); }

} // namespace SettingsShell
