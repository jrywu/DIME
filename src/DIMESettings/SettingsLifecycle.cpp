// SettingsLifecycle.cpp — see SettingsLifecycle.h. The Cpp functions follow
// the code that used to be inline in wWinMain, WM_COPYDATA, SwitchMode, and
// RepositionControlsForScroll; dime-settings (lifecycle.rs) mirrors them.

#include <windows.h>
#include "SettingsLifecycle.h"
#include "SettingsController.h"
#include "SettingsGeometry.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsLifecycle {

namespace Cpp {

namespace {
// The mode name after "--mode ": up to 31 characters, stopping at a space.
IME_MODE ModeAfter(const wchar_t* modeArg)
{
    wchar_t modeName[32] = {};
    for (int j = 0; modeArg[j] && modeArg[j] != L' ' && j < 31; j++)
        modeName[j] = modeArg[j];
    return SettingsModel::StringToImeMode(modeName);
}
} // namespace

LaunchArgs ParseLaunch(const wchar_t* lpCmdLine)
{
    LaunchArgs a = {};
    a.guiMode = (int)IME_MODE::IME_MODE_DAYI;
    if (!lpCmdLine || lpCmdLine[0] == L'\0')
        return a;
    if (wcsstr(lpCmdLine, L"--legacy"))
        a.legacy = 1;
    // --reset-ui: reset the window placement and launch the GUI (not a CLI command)
    if (wcsstr(lpCmdLine, L"--reset-ui")) {
        a.resetUi = 1;
        a.newUi = 1;
    }
    // --mode for a GUI launch (only if no CLI commands are present)
    const wchar_t* modeArg = wcsstr(lpCmdLine, L"--mode ");
    // --reset-ui must not match --reset (CLI command): check for --reset NOT followed by -
    bool hasResetCmd = false;
    { const wchar_t* r = wcsstr(lpCmdLine, L"--reset");
      if (r && r[7] != L'-') hasResetCmd = true; }
    bool hasCLICommand = wcsstr(lpCmdLine, L"--get") || wcsstr(lpCmdLine, L"--set") ||
        hasResetCmd || wcsstr(lpCmdLine, L"--import") ||
        wcsstr(lpCmdLine, L"--export") || wcsstr(lpCmdLine, L"--load") ||
        wcsstr(lpCmdLine, L"--list") || wcsstr(lpCmdLine, L"--help");
    if (modeArg && !hasCLICommand && !a.legacy) {
        IME_MODE parsed = ModeAfter(modeArg + 7);
        if (parsed != IME_MODE::IME_MODE_NONE) {
            a.guiMode = (int)parsed;
            a.newUi = 1;
        }
    }
    return a;
}

IME_MODE CopyDataMode(const wchar_t* cmdLine)
{
    if (!cmdLine) return IME_MODE::IME_MODE_NONE;
    const wchar_t* modeStr = wcsstr(cmdLine, L"--mode ");
    if (!modeStr) return IME_MODE::IME_MODE_NONE;
    return ModeAfter(modeStr + 7);
}

void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots])
{
    memset(out, 0, sizeof(bool) * SettingsWindowLayout::kExpandedSlots);
    for (int i = 0; i < imeCount && i < 16; i++)
        out[i] = ime[i].defaultExpanded;
    for (int i = 0; i < loadCount && (4 + i) < 16; i++)
        out[4 + i] = load[i].defaultExpanded;
}

std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi)
{
    int titleBottom = ScaleForDpi(g_geo.topMargin, dpi);
    std::vector<ScrollAction> out;
    for (const ScrollItem& it : items) {
        ScrollAction a = {};
        if (it.alive && (it.origW > 0 || it.origH > 0)) {
            a.moved = 1;
            a.y = (int)((unsigned)it.origY - (unsigned)scrollPos);
            // Snap-hide any control whose top enters the title cover band.
            // Settings rows are short (~24 px) — partial clipping looks like a
            // floating control above the title rather than a graceful reveal.
            if (a.y < titleBottom) {
                if (it.visible) a.show = -1;
            } else {
                a.resetRgn = 1;
                if (!it.visible) a.show = 1;
            }
        }
        out.push_back(a);
    }
    return out;
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(LaunchArgs) == sizeof(DimeLaunchArgs), "LaunchArgs");
static_assert(sizeof(ScrollItem) == sizeof(DimeScrollItem) && sizeof(ScrollItem) == 20, "ScrollItem");
static_assert(sizeof(ScrollAction) == sizeof(DimeScrollAction) && sizeof(ScrollAction) == 16, "ScrollAction");
static_assert(sizeof(bool) == 1, "bool");

namespace Rust {

LaunchArgs ParseLaunch(const wchar_t* cmdLine)
{
    DimeLaunchArgs r = {};
    r.gui_mode = (int)IME_MODE::IME_MODE_DAYI;
    dime_settings_parse_launch(reinterpret_cast<const uint16_t*>(cmdLine), &r);
    return { r.legacy, r.reset_ui, r.new_ui, r.gui_mode };
}

IME_MODE CopyDataMode(const wchar_t* cmdLine)
{
    return (IME_MODE)dime_settings_copy_data_mode(reinterpret_cast<const uint16_t*>(cmdLine));
}

void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots])
{
    uint8_t raw[SettingsWindowLayout::kExpandedSlots] = {};
    dime_settings_default_expanded(ime, imeCount, load, loadCount, raw);
    for (int i = 0; i < SettingsWindowLayout::kExpandedSlots; i++)
        out[i] = raw[i] != 0;
}

std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi)
{
    std::vector<ScrollAction> out(items.size());
    if (!items.empty())
        dime_settings_plan_scroll(reinterpret_cast<const DimeScrollItem*>(items.data()), items.size(), scrollPos, dpi,
            reinterpret_cast<DimeScrollAction*>(out.data()));
    return out;
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

LaunchArgs ParseLaunch(const wchar_t* cmdLine) { return Impl::ParseLaunch(cmdLine); }
IME_MODE CopyDataMode(const wchar_t* cmdLine) { return Impl::CopyDataMode(cmdLine); }
void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots])
{
    Impl::DefaultExpanded(ime, imeCount, load, loadCount, out);
}
std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi)
{
    return Impl::PlanScroll(items, scrollPos, dpi);
}

} // namespace SettingsLifecycle
