// SettingsDriver.cpp — see SettingsDriver.h. The Cpp functions are what used
// to be SettingsWindow::RunNav, RunFrame, SwitchMode, RebuildContentArea,
// UpdateScrollInfo, RepositionControlsForScroll, and ShowAllControls;
// dime-settings (driver.rs) mirrors them. Calls between Cpp functions are
// written Cpp:: (ADL on Host would also find the dispatched ones).

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsDriver.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsLifecycle.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsDriver {

namespace Cpp {

namespace {

bool SidebarTakesSpace(const State* st)
{
    return !st->nav.sidebarNarrowMode && !st->nav.sidebarCollapsed;
}

bool HasBreadcrumb(const State* st)
{
    return st->nav.navLevel == 1 && st->nav.selectedModeIndex < 4;
}

// CalcCardW: card width from the main window's client width.
int CardWidth(Host& h, UINT dpi, bool sidebarTakesSpace)
{
    int w = 0, ht = 0;
    h.MainClient(w, ht);
    return SettingsWindowLayout::CardWidth(w, dpi, sidebarTakesSpace, h.VScrollWidth());
}

void UpdateScrollInfo(Host& h, State* st)
{
    if (!h.HasContent()) return;
    int page = h.ContentClientHeight();
    st->nav.scrollMax = SettingsModel::ComputeScrollRange(st->nav.totalContentHeight, page);
    st->nav.scrollPos = SettingsModel::ClampScrollPos(st->nav.scrollPos, st->nav.scrollMax);
    h.SetScrollInfo(st->nav.totalContentHeight, page, st->nav.scrollPos);
}

// Controls whose top enters the title band are hidden (SettingsLifecycle::PlanScroll).
void RepositionControlsForScroll(Host& h, State* st)
{
    if (!h.HasContent()) return;
    UINT dpi = h.ContentDpi();
    int n = h.ControlCount();
    std::vector<ControlInfo> controls;
    std::vector<SettingsLifecycle::ScrollItem> items;
    for (int i = 0; i < n; i++) {
        ControlInfo c = h.Control(i);
        controls.push_back(c);
        items.push_back({ c.y, c.w, c.h, c.alive ? 1 : 0, c.visible ? 1 : 0 });
    }
    std::vector<SettingsLifecycle::ScrollAction> acts = SettingsLifecycle::PlanScroll(items, st->nav.scrollPos, dpi);
    for (size_t i = 0; i < acts.size() && i < controls.size(); i++) {
        const auto& a = acts[i];
        const auto& c = controls[i];
        if (!a.moved) continue;
        h.MoveControl((int)i, c.x, a.y, c.w, c.h);
        if (a.show < 0) h.ShowControl((int)i, false);
        if (a.resetRgn) h.ResetControlRegion((int)i);
        if (a.show > 0) h.ShowControl((int)i, true);
    }
}

void ShowAllControls(Host& h)
{
    int n = h.ControlCount();
    for (int i = 0; i < n; i++) {
        ControlInfo c = h.Control(i);
        if (c.alive && (c.w > 0 || c.h > 0)) h.ShowControl(i, true);
    }
}

void HideAllControls(Host& h)
{
    int n = h.ControlCount();
    for (int i = 0; i < n; i++) {
        ControlInfo c = h.Control(i);
        if (c.alive) h.ShowControl(i, false);
    }
}

// FindControl: the first control with this id, or -1 (no entry or no HWND).
int FindControl(Host& h, int id)
{
    int n = h.ControlCount();
    for (int i = 0; i < n; i++) {
        ControlInfo c = h.Control(i);
        if (c.id == id) return c.exists ? i : -1;
    }
    return -1;
}

} // namespace

void Rebuild(Host& h, State* st)
{
    if (!h.HasContent()) return;

    h.DestroyControls();

    // Sub-pages start below the breadcrumb strip painted by the main window.
    bool sidebarTakesSpace = SidebarTakesSpace(st);
    {
        int w = 0, ht = 0;
        h.MainClient(w, ht);
        UINT dpiPos = h.MainDpi();
        SettingsWindowLayout::CtrlRect rc = SettingsWindowLayout::ContentAreaRect(
            w, ht, dpiPos, sidebarTakesSpace, HasBreadcrumb(st));
        h.MoveContent(rc.x, rc.y, rc.w, rc.h);
    }

    UINT dpi = h.MainDpi();
    int cardW = CardWidth(h, dpi, sidebarTakesSpace);

    int imeCount = 0;
    const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
    int ltCount = 0;
    const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);

    SettingsWindowLayout::PlanInput in = {};
    in.dpi = dpi;
    in.selectedModeIndex = st->nav.selectedModeIndex;
    in.navLevel = st->nav.navLevel;
    in.currentCardIndex = st->nav.currentCardIndex;
    in.modeMask = SettingsModel::GetModeBitmask((IME_MODE)st->nav.currentMode);
    in.colorMode = h.ColorMode();
    in.gridExpanded = st->nav.colorGridExpanded ? 1 : 0;
    in.previewH = 0;
    if (HasBreadcrumb(st))
        in.previewH = h.PreviewHeight(dpi);
    memcpy(in.expanded, st->nav.cardExpanded, sizeof(in.expanded));

    std::vector<SettingsWindowLayout::Placement> placements;
    SettingsWindowLayout::PlanResult plan = SettingsWindowLayout::PlanControls(
        in, imeCards, imeCount, ltCards, ltCount, placements);

    h.CreateControls(in, cardW);
    if (plan.setTotal) st->nav.totalContentHeight = plan.totalHeight;
    if (plan.populate) h.Populate();

    st->nav.scrollPos = 0;
    UpdateScrollInfo(h, st);
    h.InvalidateContent(true);
}

void SwitchMode(Host& h, State* st, int mode)
{
    bool modeChanged = (mode != st->nav.currentMode);
    st->nav.currentMode = mode;
    st->nav.selectedModeIndex = SettingsModel::ModeToIndex((IME_MODE)mode);
    st->nav.navLevel = 0;
    {
        int imeCount = 0;
        const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        SettingsLifecycle::DefaultExpanded(imeCards, imeCount, ltCards, ltCount, st->nav.cardExpanded);
    }
    // Re-enumerate only on an actual mode change: LoadConfig filters this
    // IME out of the list, and the list is already filtered otherwise.
    if (modeChanged)
        h.EnumerateReverseProviders();
    h.LoadConfig(mode);
    h.LoadColors();
    h.LoadSnapshot();
    h.RecreateEngine(mode);   // custom table validation uses the mode's engine
    Cpp::Rebuild(h, st);
    h.InvalidateMain(nullptr, true);
}

void Nav(Host& h, State* st, SettingsNav::NavEvent ev)
{
    ev.dpi = h.MainDpi();
    ev.colorMode = h.ColorMode();
    ev.customTableDirty = h.SaveEnabled() ? 1 : 0;
    ev.previewH = 0;
    if (HasBreadcrumb(st) && h.HasContent())
        ev.previewH = h.PreviewHeight(ev.dpi);

    std::vector<SettingsNav::NavEffect> effects;
    SettingsNav::Handle(st->nav, ev, effects);

    for (const auto& fx : effects) {
        switch (fx.kind) {
        case SettingsNav::FX_INVALIDATE_MAIN:
            h.InvalidateMain(fx.hasRect ? &fx.rc : nullptr, fx.erase != 0);
            break;
        case SettingsNav::FX_INVALIDATE_CONTENT:
            h.InvalidateContent(fx.erase != 0);
            break;
        case SettingsNav::FX_TRACK_LEAVE_MAIN:
            h.TrackLeave(false);
            break;
        case SettingsNav::FX_TRACK_LEAVE_CONTENT:
            h.TrackLeave(true);
            break;
        case SettingsNav::FX_SCROLL_CHANGED:
            UpdateScrollInfo(h, st);
            RepositionControlsForScroll(h, st);
            h.RedrawContent();
            break;
        case SettingsNav::FX_SHOW_ALL_CONTROLS:
            ShowAllControls(h);
            break;
        case SettingsNav::FX_HIDE_ALL_CONTROLS:
            HideAllControls(h);
            break;
        case SettingsNav::FX_SWITCH_MODE:
            Cpp::SwitchMode(h, st, fx.arg);
            break;
        case SettingsNav::FX_REBUILD:
            Cpp::Rebuild(h, st);
            break;
        case SettingsNav::FX_TOGGLE_CONTROL: {
            int i = FindControl(h, fx.arg);
            if (i >= 0) {
                bool on = !h.ControlChecked(i);
                h.SetControlChecked(i, on);
                h.ToggleSnapshot(fx.arg, on);
                h.ApplyAndSave();
                h.InvalidateContent(false);
                h.UpdateContent();
            }
            break;
        }
        case SettingsNav::FX_ASK_SAVE_THEN_BACK: {
            // 自建詞庫 has unsaved changes: ask before leaving the sub-page.
            int result = h.AskSaveBeforeBack();
            if (result == IDYES)
                h.ClickSave();
            if (result == IDYES || result == IDNO) {
                SettingsNav::NavEvent back = {};
                back.kind = SettingsNav::EV_BACK;
                Cpp::Nav(h, st, back);
            }
            break;
        }
        case SettingsNav::FX_MOVE_CONTENT:
            h.MoveContent(fx.rc.left, fx.rc.top, fx.rc.right, fx.rc.bottom);
            break;
        case SettingsNav::FX_SET_FOCUS_CONTENT:
            h.FocusContent();
            break;
        case SettingsNav::FX_UPDATE_CONTENT:
            h.UpdateContent();
            break;
        case SettingsNav::FX_UPDATE_SCROLL_INFO:
            UpdateScrollInfo(h, st);
            break;
        default:
            break;
        }
    }
}

void Frame(Host& h, State* st, SettingsFrame::FrameEvent ev)
{
    ev.systemDark = h.SystemDark() ? 1 : 0;
    ev.hasContent = h.HasContent() ? 1 : 0;
    SettingsFrame::FrameState fs = { st->isDark ? 1 : 0 };
    std::vector<SettingsFrame::FrameEffect> effects;
    SettingsFrame::Handle(fs, ev, effects);
    st->isDark = fs.isDark ? 1 : 0;   // stored first: ThemeBrushes reads it

    for (const auto& fx : effects) {
        switch (fx.kind) {
        case SettingsFrame::FX_SET_DARK_TITLE:
            h.SetDarkTitle(fx.arg != 0);
            break;
        case SettingsFrame::FX_CAPTION_COLOR:
            h.SetCaptionColor(fx.arg);
            break;
        case SettingsFrame::FX_THEME_BRUSHES:
            h.ThemeBrushes();
            break;
        case SettingsFrame::FX_CREATE_FONTS:
            h.CreateFonts(fx.arg);
            break;
        case SettingsFrame::FX_DESTROY_FONTS:
            h.DestroyFonts();
            break;
        case SettingsFrame::FX_INVALIDATE_MAIN:
            h.InvalidateMain(nullptr, true);
            break;
        case SettingsFrame::FX_INVALIDATE_CONTENT:
            h.InvalidateContent(true);
            break;
        case SettingsFrame::FX_CONTENT_THEME:
            h.ContentTheme(fx.arg != 0);
            break;
        case SettingsFrame::FX_REBUILD:
            Cpp::Rebuild(h, st);
            break;
        case SettingsFrame::FX_SET_POS:
            h.SetMainPos(fx.rc.left, fx.rc.top, fx.rc.right, fx.rc.bottom);
            break;
        case SettingsFrame::FX_CREATE_CONTENT:
            h.CreateContent(fx.rc.left, fx.rc.top, fx.rc.right, fx.rc.bottom);
            break;
        default:
            break;
        }
    }
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(State) == 72, "State must match dime-settings DriverState");
static_assert(sizeof(ControlInfo) == 32, "ControlInfo must match dime-settings");

namespace Rust {

namespace {
// C thunks over Host. Host methods do not throw; catch anyway so nothing
// unwinds into Rust.
Host& H(void* ctx) { return *static_cast<Host*>(ctx); }

uint32_t MainDpi(void* c) { try { return H(c).MainDpi(); } catch (...) { return 96; } }
uint32_t ContentDpi(void* c) { try { return H(c).ContentDpi(); } catch (...) { return 96; } }
int32_t HasContent(void* c) { try { return H(c).HasContent() ? 1 : 0; } catch (...) { return 0; } }
void MainClient(void* c, int32_t* w, int32_t* ht)
{
    int a = 0, b = 0;
    try { H(c).MainClient(a, b); } catch (...) {}
    *w = a;
    *ht = b;
}
int32_t ContentClientHeight(void* c) { try { return H(c).ContentClientHeight(); } catch (...) { return 0; } }
int32_t VScrollWidth(void* c) { try { return H(c).VScrollWidth(); } catch (...) { return 0; } }
int32_t SystemDark(void* c) { try { return H(c).SystemDark() ? 1 : 0; } catch (...) { return 0; } }
int32_t ColorMode(void* c) { try { return H(c).ColorMode(); } catch (...) { return 0; } }
int32_t SaveEnabled(void* c) { try { return H(c).SaveEnabled() ? 1 : 0; } catch (...) { return 0; } }
int32_t PreviewHeight(void* c, uint32_t dpi) { try { return H(c).PreviewHeight(dpi); } catch (...) { return 0; } }
int32_t ControlCount(void* c) { try { return H(c).ControlCount(); } catch (...) { return 0; } }
void Control(void* c, int32_t i, void* out)
{
    ControlInfo info = {};
    try { info = H(c).Control(i); } catch (...) {}
    *static_cast<ControlInfo*>(out) = info;
}
void MoveControl(void* c, int32_t i, int32_t x, int32_t y, int32_t w, int32_t ht)
{
    try { H(c).MoveControl(i, x, y, w, ht); } catch (...) {}
}
void ShowControl(void* c, int32_t i, int32_t show) { try { H(c).ShowControl(i, show != 0); } catch (...) {} }
void ResetControlRegion(void* c, int32_t i) { try { H(c).ResetControlRegion(i); } catch (...) {} }
int32_t ControlChecked(void* c, int32_t i) { try { return H(c).ControlChecked(i) ? 1 : 0; } catch (...) { return 0; } }
void SetControlChecked(void* c, int32_t i, int32_t on) { try { H(c).SetControlChecked(i, on != 0); } catch (...) {} }
void DestroyControls(void* c) { try { H(c).DestroyControls(); } catch (...) {} }
void CreateControls(void* c, const void* in, int32_t cardW)
{
    try { H(c).CreateControls(*static_cast<const SettingsWindowLayout::PlanInput*>(in), cardW); } catch (...) {}
}
void Populate(void* c) { try { H(c).Populate(); } catch (...) {} }
void InvalidateMain(void* c, const int32_t* rc, int32_t erase)
{
    try { H(c).InvalidateMain((const RECT*)rc, erase != 0); } catch (...) {}
}
void InvalidateContent(void* c, int32_t erase) { try { H(c).InvalidateContent(erase != 0); } catch (...) {} }
void TrackLeave(void* c, int32_t content) { try { H(c).TrackLeave(content != 0); } catch (...) {} }
void SetScroll(void* c, int32_t total, int32_t page, int32_t pos) { try { H(c).SetScrollInfo(total, page, pos); } catch (...) {} }
void RedrawContent(void* c) { try { H(c).RedrawContent(); } catch (...) {} }
void UpdateContent(void* c) { try { H(c).UpdateContent(); } catch (...) {} }
void MoveContent(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).MoveContent(x, y, w, ht); } catch (...) {} }
void FocusContent(void* c) { try { H(c).FocusContent(); } catch (...) {} }
void CreateContent(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).CreateContent(x, y, w, ht); } catch (...) {} }
void ContentTheme(void* c, int32_t dark) { try { H(c).ContentTheme(dark != 0); } catch (...) {} }
void SetMainPos(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).SetMainPos(x, y, w, ht); } catch (...) {} }
void SetDarkTitle(void* c, int32_t dark) { try { H(c).SetDarkTitle(dark != 0); } catch (...) {} }
void SetCaptionColor(void* c, int32_t attr) { try { H(c).SetCaptionColor(attr); } catch (...) {} }
void ThemeBrushes(void* c) { try { H(c).ThemeBrushes(); } catch (...) {} }
void CreateFonts(void* c, int32_t dpi) { try { H(c).CreateFonts(dpi); } catch (...) {} }
void DestroyFonts(void* c) { try { H(c).DestroyFonts(); } catch (...) {} }
int32_t AskSaveBeforeBack(void* c) { try { return H(c).AskSaveBeforeBack(); } catch (...) { return IDCANCEL; } }
void ClickSave(void* c) { try { H(c).ClickSave(); } catch (...) {} }
void ToggleSnapshot(void* c, int32_t id, int32_t on) { try { H(c).ToggleSnapshot(id, on != 0); } catch (...) {} }
void ApplyAndSave(void* c) { try { H(c).ApplyAndSave(); } catch (...) {} }
void EnumerateReverseProviders(void* c) { try { H(c).EnumerateReverseProviders(); } catch (...) {} }
void LoadConfig(void* c, int32_t mode) { try { H(c).LoadConfig(mode); } catch (...) {} }
void LoadColors(void* c) { try { H(c).LoadColors(); } catch (...) {} }
void LoadSnapshot(void* c) { try { H(c).LoadSnapshot(); } catch (...) {} }
void RecreateEngine(void* c, int32_t mode) { try { H(c).RecreateEngine(mode); } catch (...) {} }

// Order must match DimeSettingsDriverHostVtbl (dime_ffi.h) and
// DriverHostVtbl (settings_window.rs).
const DimeSettingsDriverHostVtbl kVtbl = {
    MainDpi, ContentDpi, HasContent, MainClient, ContentClientHeight, VScrollWidth, SystemDark,
    ColorMode, SaveEnabled, PreviewHeight,
    ControlCount, Control, MoveControl, ShowControl, ResetControlRegion,
    ControlChecked, SetControlChecked, DestroyControls, CreateControls, Populate,
    InvalidateMain, InvalidateContent, TrackLeave, SetScroll, RedrawContent, UpdateContent,
    MoveContent, FocusContent, CreateContent, ContentTheme, SetMainPos, SetDarkTitle,
    SetCaptionColor, ThemeBrushes, CreateFonts, DestroyFonts,
    AskSaveBeforeBack, ClickSave,
    ToggleSnapshot, ApplyAndSave, EnumerateReverseProviders, LoadConfig, LoadColors, LoadSnapshot,
    RecreateEngine,
};
} // namespace

void Nav(Host& host, State* st, SettingsNav::NavEvent ev)
{
    dime_settings_driver_nav(&kVtbl, &host, st, &ev);
}

void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev)
{
    dime_settings_driver_frame(&kVtbl, &host, st, &ev);
}

void SwitchMode(Host& host, State* st, int mode)
{
    dime_settings_driver_switch_mode(&kVtbl, &host, st, mode);
}

void Rebuild(Host& host, State* st)
{
    dime_settings_driver_rebuild(&kVtbl, &host, st);
}

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

void Nav(Host& host, State* st, SettingsNav::NavEvent ev) { Impl::Nav(host, st, ev); }
void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev) { Impl::Frame(host, st, ev); }
void SwitchMode(Host& host, State* st, int mode) { Impl::SwitchMode(host, st, mode); }
void Rebuild(Host& host, State* st) { Impl::Rebuild(host, st); }

} // namespace SettingsDriver
