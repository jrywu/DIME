// SettingsProc.cpp — see SettingsProc.h. Cpp::Main / Content are the message
// switches that used to be SettingsWindow::WndProc / ContentWndProc;
// dime-settings (wndproc.rs) mirrors them.

#include <windows.h>
#include <windowsx.h>
#include "..\BaseStructure.h"
#include "SettingsProc.h"
#include "SettingsNav.h"
#include "SettingsLifecycle.h"
#include "SettingsEditor.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsProc {

namespace Cpp {

LRESULT Main(Host& h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool has = h.HasData();
    switch (msg) {
    case WM_CREATE:
        h.Create(((LPCREATESTRUCT)lParam)->hInstance);
        return 0;

    case WM_SIZE:
        if (has) h.Nav(SettingsNav::EV_MAIN_SIZE, LOWORD(lParam), HIWORD(lParam), 0, 0);
        return 0;

    case WM_PAINT:
        h.PaintMain();
        return 0;

    case WM_MOUSEWHEEL:
        if (has && h.HasContent()) h.Nav(SettingsNav::EV_MAIN_WHEEL, 0, 0, GET_WHEEL_DELTA_WPARAM(wParam), 0);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
        // Sidebar click / hover, breadcrumb back link, narrow-mode hamburger
        if (!has) break;
        h.Nav(msg == WM_LBUTTONDOWN ? SettingsNav::EV_MAIN_LBUTTON : SettingsNav::EV_MAIN_MOUSEMOVE,
            GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, 0);
        return 0;

    case WM_MOUSELEAVE:
        if (has) h.Nav(SettingsNav::EV_MAIN_MOUSELEAVE, 0, 0, 0, 0);
        return 0;

    case WM_THEMECHANGED:
        if (has) h.ThemeChanged();
        return 0;

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        mmi->ptMinTrackSize = SettingsNav::Cpp::MinTrackSize(h.ScreenDpi());
        return 0;
    }

    case WM_COPYDATA:
    {
        if (!has) break;
        COPYDATASTRUCT* pCds = (COPYDATASTRUCT*)lParam;
        if (pCds && pCds->lpData) {
            IME_MODE mode = SettingsLifecycle::Cpp::CopyDataMode((const wchar_t*)pCds->lpData);
            if (mode != IME_MODE::IME_MODE_NONE)
                h.SwitchMode((int)mode);
            h.SetForeground();
        }
        return TRUE;
    }

    case WM_SETTINGCHANGE:
        if (!has) break;
        h.SettingChanged();
        return 0;

    case WM_DPICHANGED:
        if (!has) break;
        h.DpiChanged(HIWORD(wParam), (const RECT*)lParam);
        return 0;

    case WM_DESTROY:
        h.Destroy();
        h.Quit();
        return 0;
    }
    return h.Default(msg, wParam, lParam);
}

LRESULT Content(Host& h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool has = h.HasData();
    switch (msg) {
    case WM_PAINT:
        h.PaintContent();
        return 0;

    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
        if (!has) break;
        switch (msg) {
        case WM_VSCROLL:
            h.Nav(SettingsNav::EV_CONTENT_VSCROLL, 0, 0, LOWORD(wParam), HIWORD(wParam));
            break;
        case WM_MOUSEWHEEL:
            h.Nav(SettingsNav::EV_CONTENT_WHEEL, 0, 0, GET_WHEEL_DELTA_WPARAM(wParam), 0);
            break;
        case WM_MOUSEMOVE:
            h.Nav(SettingsNav::EV_CONTENT_MOUSEMOVE, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, 0);
            break;
        default:
            h.Nav(SettingsNav::EV_CONTENT_LBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, 0);
            break;
        }
        return 0;

    case WM_MOUSELEAVE:
        if (has) h.Nav(SettingsNav::EV_CONTENT_MOUSELEAVE, 0, 0, 0, 0);
        break;

    case WM_COMMAND:
        if (!has) break;
        h.Command(wParam, lParam);
        break;

    case SettingsEditor::kMsgRevalidate:   // deferred trigger from the editor subclass
        if (!has) break;
        h.EditorRevalidate();
        return 0;

    case SettingsEditor::kMsgViewport:     // forwarded WM_VSCROLL / WM_MOUSEWHEEL
        if (!has) break;
        h.EditorViewport();
        return 0;

    case WM_DRAWITEM:
    {
        if (!has) break;
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pdis) break;
        if (h.DrawItem(pdis)) return TRUE;
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        if (!has) break;
        LRESULT brush = 0;
        if (!h.CtlColor((HDC)wParam, brush)) break;
        return brush;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return h.Default(msg, wParam, lParam);
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

namespace Rust {

namespace {
// C thunks over Host. Host methods do not throw; catch anyway so nothing
// unwinds into Rust.
Host& H(void* ctx) { return *static_cast<Host*>(ctx); }

int32_t HasData(void* c) { try { return H(c).HasData() ? 1 : 0; } catch (...) { return 0; } }
int32_t HasContent(void* c) { try { return H(c).HasContent() ? 1 : 0; } catch (...) { return 0; } }
void Create(void* c, intptr_t inst) { try { H(c).Create((HINSTANCE)inst); } catch (...) {} }
void Nav(void* c, int32_t k, int32_t x, int32_t y, int32_t d, int32_t t) { try { H(c).Nav(k, x, y, d, t); } catch (...) {} }
void ThemeChanged(void* c) { try { H(c).ThemeChanged(); } catch (...) {} }
void SettingChanged(void* c) { try { H(c).SettingChanged(); } catch (...) {} }
void DpiChanged(void* c, uint32_t dpi, const int32_t* rc) { try { H(c).DpiChanged(dpi, (const RECT*)rc); } catch (...) {} }
void PaintMain(void* c) { try { H(c).PaintMain(); } catch (...) {} }
void PaintContent(void* c) { try { H(c).PaintContent(); } catch (...) {} }
uint32_t ScreenDpi(void* c) { try { return H(c).ScreenDpi(); } catch (...) { return 96; } }
void SwitchMode(void* c, int32_t m) { try { H(c).SwitchMode(m); } catch (...) {} }
void SetForeground(void* c) { try { H(c).SetForeground(); } catch (...) {} }
void Destroy(void* c) { try { H(c).Destroy(); } catch (...) {} }
void Quit(void* c) { try { H(c).Quit(); } catch (...) {} }
void Command(void* c, uintptr_t w, intptr_t l) { try { H(c).Command((WPARAM)w, (LPARAM)l); } catch (...) {} }
void EditorRevalidate(void* c) { try { H(c).EditorRevalidate(); } catch (...) {} }
void EditorViewport(void* c) { try { H(c).EditorViewport(); } catch (...) {} }
int32_t DrawItem(void* c, intptr_t dis)
{
    try { return H(c).DrawItem((const DRAWITEMSTRUCT*)dis) ? 1 : 0; } catch (...) { return 0; }
}
int32_t CtlColor(void* c, intptr_t hdc, intptr_t* brush)
{
    try {
        LRESULT b = 0;
        bool handled = H(c).CtlColor((HDC)hdc, b);
        *brush = (intptr_t)b;
        return handled ? 1 : 0;
    } catch (...) { return 0; }
}
intptr_t Def(void* c, uint32_t msg, uintptr_t w, intptr_t l)
{
    try { return (intptr_t)H(c).Default(msg, (WPARAM)w, (LPARAM)l); } catch (...) { return 0; }
}

// Order must match DimeSettingsProcHostVtbl (dime_ffi.h) and ProcHostVtbl
// (settings_window.rs).
const DimeSettingsProcHostVtbl kVtbl = {
    HasData, HasContent, Create, Nav, ThemeChanged, SettingChanged, DpiChanged,
    PaintMain, PaintContent, ScreenDpi, SwitchMode, SetForeground, Destroy, Quit,
    Command, EditorRevalidate, EditorViewport, DrawItem, CtlColor, Def,
};
} // namespace

LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return (LRESULT)dime_settings_main_proc(&kVtbl, &host, msg, (uintptr_t)wParam, (intptr_t)lParam);
}

LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return (LRESULT)dime_settings_content_proc(&kVtbl, &host, msg, (uintptr_t)wParam, (intptr_t)lParam);
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam) { return Impl::Main(host, msg, wParam, lParam); }
LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam) { return Impl::Content(host, msg, wParam, lParam); }

} // namespace SettingsProc
