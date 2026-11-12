// SettingsFrame.cpp — see SettingsFrame.h. Cpp::Handle is the logic that used
// to be inline in WM_CREATE, UpdateTheme, WM_SETTINGCHANGE, and
// WM_DPICHANGED; dime-settings (frame.rs) mirrors it.

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsFrame.h"
#include "SettingsGeometry.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsFrame {

namespace Cpp {

namespace {
struct Out {
    std::vector<FrameEffect>& v;
    void add(int kind, int arg = 0) { v.push_back({ kind, arg, {} }); }
    void addRect(int kind, int x, int y, int w, int h) { v.push_back({ kind, 0, { x, y, w, h } }); }
};

// UpdateTheme. Its caption colour uses DWM attribute 36; the other paths use 35.
void UpdateTheme(FrameState& s, const FrameEvent& ev, Out& o)
{
    s.isDark = ev.systemDark ? 1 : 0;
    o.add(FX_THEME_BRUSHES);
    o.add(FX_CREATE_FONTS, (int)ev.dpi);
    if (s.isDark)
        o.add(FX_SET_DARK_TITLE, 1);
    // Match title bar color to content background (DWMWA_CAPTION_COLOR = 36)
    o.add(FX_CAPTION_COLOR, 36);
    o.add(FX_INVALIDATE_MAIN);
    if (ev.hasContent) o.add(FX_INVALIDATE_CONTENT);
}
} // namespace

void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& outVec)
{
    Out o{ outVec };
    switch (ev.kind) {
    case EV_CREATE: {
        s.isDark = ev.systemDark ? 1 : 0;
        o.add(FX_THEME_BRUSHES);
        o.add(FX_CREATE_FONTS, (int)ev.dpi);
        if (s.isDark)
            o.add(FX_SET_DARK_TITLE, 1);
        // Match title bar color to content background (DWMWA_CAPTION_COLOR = 35)
        o.add(FX_CAPTION_COLOR, 35);
        int sidebarW = ScaleForDpi(g_geo.sidebarWidth, ev.dpi);
        o.addRect(FX_CREATE_CONTENT, sidebarW, 0, (int)((unsigned)ev.clientW - (unsigned)sidebarW), ev.clientH);
        if (s.isDark)
            o.add(FX_CONTENT_THEME, 1);
        break;
    }
    case EV_THEME_CHANGED:
        UpdateTheme(s, ev, o);
        break;
    case EV_SETTING_CHANGE: {
        // Detect system theme change (dark/light mode)
        int wasDark = s.isDark;
        s.isDark = ev.systemDark ? 1 : 0;
        if (wasDark != s.isDark) {
            UpdateTheme(s, ev, o);
            // Re-apply dark mode to title bar, update title bar color
            o.add(FX_SET_DARK_TITLE, s.isDark);
            o.add(FX_CAPTION_COLOR, 35);
            // Dark mode scrollbar
            if (ev.hasContent) o.add(FX_CONTENT_THEME, s.isDark);
            // Rebuild to re-apply dark theme to child controls (combos, edits)
            o.add(FX_REBUILD);
        }
        break;
    }
    case EV_DPI_CHANGED:
        // Resize window to suggested rect
        if (ev.hasRect)
            o.addRect(FX_SET_POS, ev.rc.left, ev.rc.top,
                (int)((unsigned)ev.rc.right - (unsigned)ev.rc.left), (int)((unsigned)ev.rc.bottom - (unsigned)ev.rc.top));
        // Recreate fonts for new DPI, rebuild with DPI-scaled positions
        o.add(FX_DESTROY_FONTS);
        o.add(FX_CREATE_FONTS, (int)ev.dpi);
        o.add(FX_REBUILD);
        o.add(FX_CAPTION_COLOR, 35);
        o.add(FX_INVALIDATE_MAIN);
        break;
    default:
        break;
    }
}

SIZE InitialSize(UINT dpi)
{
    return { ScaleForDpi(g_geo.windowDefaultW, dpi), ScaleForDpi(g_geo.windowDefaultH, dpi) };
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(FrameState) == 4, "FrameState must match dime-settings");
static_assert(sizeof(FrameEvent) == 48, "FrameEvent must match dime-settings");
static_assert(sizeof(FrameEffect) == 24, "FrameEffect must match dime-settings");

namespace Rust {

void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out)
{
    FrameEffect buf[32];
    size_t count = 0;
    dime_settings_frame_handle(&s, &ev, buf, _countof(buf), &count);
    for (size_t i = 0; i < count && i < _countof(buf); i++)
        out.push_back(buf[i]);
}

SIZE InitialSize(UINT dpi)
{
    int32_t sz[2] = {};
    dime_settings_frame_initial_size(dpi, sz);
    return { sz[0], sz[1] };
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out) { Impl::Handle(s, ev, out); }
SIZE InitialSize(UINT dpi) { return Impl::InitialSize(dpi); }

} // namespace SettingsFrame
