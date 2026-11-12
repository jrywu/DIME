// SettingsOwnerDraw.cpp — see SettingsOwnerDraw.h. The Cpp functions are the
// code that used to be inline in the content area's WM_DRAWITEM and
// WM_CTLCOLOR*; dime-settings (owner.rs) mirrors them.

#include <windows.h>
#include <cstddef>
#include "..\BaseStructure.h"
#include "SettingsOwnerDraw.h"
#include "SettingsWindowPaint.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsOwnerDraw {

namespace Cpp {

bool DrawItem(const DRAWITEMSTRUCT* pdis, const Context& ctx)
{
    // Owner-draw buttons (Win11 style: anti-aliased rounded corners via GDI+)
    if (pdis->CtlType == ODT_BUTTON) {
        bool pressed  = (pdis->itemState & ODS_SELECTED) != 0;
        bool disabled = (pdis->itemState & ODS_DISABLED) != 0;
        wchar_t txt[128] = {};
        GetWindowTextW(pdis->hwndItem, txt, 128);
        SettingsWindowPaint::Cpp::PaintOwnerButton(pdis->hDC, pdis->rcItem, txt, pressed, disabled,
            ctx.theme, ctx.fontBody, ctx.dpi);
        return true;
    }
    // Color swatches
    if (pdis->CtlType == ODT_STATIC && pdis->CtlID >= 4000 && pdis->CtlID < 4006) {
        int idx = pdis->CtlID - 4000;
        int r = ScaleForDpi(g_geo.swatchRadius, ctx.dpi);
        SettingsWindowPaint::Cpp::PaintSwatch(pdis->hDC, pdis->rcItem, ctx.swatch[idx], ctx.theme.cardBorder, r);
        return true;
    }
    return false;
}

bool CtlColor(HDC hdcCtrl, bool isDark, const ThemeColors& theme, HBRUSH* brush)
{
    if (!isDark) return false;
    SetTextColor(hdcCtrl, theme.textPrimary);
    SetBkColor(hdcCtrl, theme.cardBg);
    if (!*brush) *brush = CreateSolidBrush(theme.cardBg);
    return true;
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

namespace Rust {

bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx)
{
    return dime_settings_draw_item(dis, &ctx) != 0;
}

bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush)
{
    return dime_settings_ctl_color(hdc, isDark ? 1 : 0, &theme, reinterpret_cast<void**>(brush)) != 0;
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx) { return Impl::DrawItem(dis, ctx); }
bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush)
{
    return Impl::CtlColor(hdc, isDark, theme, brush);
}

} // namespace SettingsOwnerDraw
