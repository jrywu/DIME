//! What the settings window does with navigation, frame, mode-switch, and
//! rebuild events (phase 5b-9b).
//!
//! Port of `SettingsDriver::Cpp` (`src/DIMESettings/SettingsDriver.cpp`,
//! formerly `SettingsWindow::RunNav`, `RunFrame`, `SwitchMode`,
//! `RebuildContentArea`, `UpdateScrollInfo`, `RepositionControlsForScroll`,
//! and `ShowAllControls`). The window state is a [`DriverState`] read and
//! written in place; every side effect goes through [`DriverHost`].
//!
//! Host calls may re-enter the driver (`click_save` sends `WM_COMMAND`, which
//! can rebuild the page) with the same state pointer, so the functions take a
//! raw pointer and never hold a reference to it across a host call.

use dime_config::page_layout::{ime_cards, load_cards};
use dime_config::settings_model::{clamp_scroll_pos, compute_scroll_range, mode_bitmask, mode_to_index};
use dime_config::ImeMode;

use crate::frame::{self, FrameEvent, FrameState};
use crate::layout::{card_width, content_area_rect, plan_controls, PlanInput};
use crate::lifecycle::{default_expanded, plan_scroll, ScrollItem};
use crate::nav::{self, NavEvent, NavState};

/// `SettingsDriver::State` (72 bytes).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DriverState {
    pub nav: NavState,
    pub is_dark: i32,
    pub reserved: i32,
}

/// `SettingsDriver::ControlInfo` (32 bytes): one entry of `controlHandles`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ControlInfo {
    pub id: i32,
    /// `hWnd != nullptr`.
    pub exists: i32,
    /// `hWnd && IsWindow(hWnd)`.
    pub alive: i32,
    /// `alive && IsWindowVisible(hWnd)`.
    pub visible: i32,
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

const IDYES: i32 = 6;
const IDNO: i32 = 7;

/// What the driver asks of the window (`SettingsDriver::Host`). Methods on
/// the content area do nothing while it does not exist.
pub trait DriverHost {
    fn main_dpi(&mut self) -> u32;
    fn content_dpi(&mut self) -> u32;
    fn has_content(&mut self) -> bool;
    /// `GetClientRect(main)`: width, height.
    fn main_client(&mut self) -> (i32, i32);
    fn content_client_height(&mut self) -> i32;
    /// `GetSystemMetrics(SM_CXVSCROLL)`.
    fn vscroll_width(&mut self) -> i32;
    fn system_dark(&mut self) -> bool;
    /// `snapshot.colorMode`.
    fn color_mode(&mut self) -> i32;
    /// 自建詞庫 [儲存] exists and is enabled.
    fn save_enabled(&mut self) -> bool;
    /// `CalcCandidatePreviewH` on a content DC.
    fn preview_height(&mut self, dpi: u32) -> i32;

    fn control_count(&mut self) -> i32;
    fn control(&mut self, index: i32) -> ControlInfo;
    fn move_control(&mut self, index: i32, x: i32, y: i32, w: i32, h: i32);
    fn show_control(&mut self, index: i32, show: bool);
    fn reset_control_region(&mut self, index: i32);
    fn control_checked(&mut self, index: i32) -> bool;
    fn set_control_checked(&mut self, index: i32, on: bool);
    /// `DestroyWindow` each control and clear the list.
    fn destroy_controls(&mut self);
    /// `SettingsControls::BuildSpecs(input, card_w)` and create them.
    fn create_controls(&mut self, input: &PlanInput, card_w: i32);
    /// `PopulateControls`.
    fn populate(&mut self);

    /// `rc` is left, top, right, bottom; `None` is the whole window.
    fn invalidate_main(&mut self, rc: Option<[i32; 4]>, erase: bool);
    fn invalidate_content(&mut self, erase: bool);
    /// `TrackMouseEvent(TME_LEAVE)` on the main window or the content area.
    fn track_leave(&mut self, content: bool);
    fn set_scroll_info(&mut self, total: i32, page: i32, pos: i32);
    /// `RedrawWindow(content, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN)`.
    fn redraw_content(&mut self);
    fn update_content(&mut self);
    fn move_content(&mut self, x: i32, y: i32, w: i32, h: i32);
    fn focus_content(&mut self);
    fn create_content(&mut self, x: i32, y: i32, w: i32, h: i32);
    fn content_theme(&mut self, dark: bool);
    fn set_main_pos(&mut self, x: i32, y: i32, w: i32, h: i32);
    fn set_dark_title(&mut self, dark: bool);
    /// `DwmSetWindowAttribute(attribute, theme.contentBg)`.
    fn set_caption_color(&mut self, attribute: i32);
    /// `CreateThemeBrushes` from the state's dark flag.
    fn theme_brushes(&mut self);
    fn create_fonts(&mut self, dpi: i32);
    fn destroy_fonts(&mut self);

    /// The unsaved 自建詞庫 prompt: `IDYES`, `IDNO`, or `IDCANCEL`.
    fn ask_save_before_back(&mut self) -> i32;
    /// `WM_COMMAND` `BN_CLICKED` of [儲存] to the content area.
    fn click_save(&mut self);

    /// `SyncToggleToSnapshot`.
    fn toggle_snapshot(&mut self, id: i32, on: bool);
    fn apply_and_save(&mut self);
    fn enumerate_reverse_providers(&mut self);
    fn load_config(&mut self, mode: i32);
    /// `LoadColorsForMode(GetColorMode())`.
    fn load_colors(&mut self);
    /// `snapshot = LoadFromConfig()`.
    fn load_snapshot(&mut self);
    fn recreate_engine(&mut self, mode: i32);
}

// SAFETY (all `unsafe` blocks below): the caller of the public functions
// guarantees `st` is valid for reads and writes for the whole call. Each
// access is a short read or write that ends before the next host call, so a
// host call re-entering the driver with the same pointer never overlaps a
// live reference.

fn sidebar_takes_space(st: *mut DriverState) -> bool {
    let n = unsafe { &(*st).nav };
    n.sidebar_narrow_mode == 0 && n.sidebar_collapsed == 0
}

fn has_breadcrumb(st: *mut DriverState) -> bool {
    let n = unsafe { &(*st).nav };
    n.nav_level == 1 && n.selected_mode_index < 4
}

/// `CalcCardW`.
fn calc_card_w(h: &mut dyn DriverHost, dpi: u32, sidebar: bool) -> i32 {
    let (w, _) = h.main_client();
    let vs = h.vscroll_width();
    card_width(w, dpi, sidebar, vs)
}

fn update_scroll_info(h: &mut dyn DriverHost, st: *mut DriverState) {
    if !h.has_content() {
        return;
    }
    let page = h.content_client_height();
    let (total, pos) = unsafe {
        let n = &mut (*st).nav;
        n.scroll_max = compute_scroll_range(n.total_content_height, page);
        n.scroll_pos = clamp_scroll_pos(n.scroll_pos, n.scroll_max);
        (n.total_content_height, n.scroll_pos)
    };
    h.set_scroll_info(total, page, pos);
}

fn reposition_controls_for_scroll(h: &mut dyn DriverHost, st: *mut DriverState) {
    if !h.has_content() {
        return;
    }
    let dpi = h.content_dpi();
    let n = h.control_count();
    let controls: Vec<ControlInfo> = (0..n).map(|i| h.control(i)).collect();
    let items: Vec<ScrollItem> = controls
        .iter()
        .map(|c| ScrollItem {
            orig_y: c.y,
            orig_w: c.w,
            orig_h: c.h,
            alive: i32::from(c.alive != 0),
            visible: i32::from(c.visible != 0),
        })
        .collect();
    let scroll_pos = unsafe { (*st).nav.scroll_pos };
    let acts = plan_scroll(&items, scroll_pos, dpi);
    for (i, (a, c)) in acts.iter().zip(&controls).enumerate() {
        if a.moved == 0 {
            continue;
        }
        let i = i as i32;
        h.move_control(i, c.x, a.y, c.w, c.h);
        if a.show < 0 {
            h.show_control(i, false);
        }
        if a.reset_rgn != 0 {
            h.reset_control_region(i);
        }
        if a.show > 0 {
            h.show_control(i, true);
        }
    }
}

fn show_all_controls(h: &mut dyn DriverHost) {
    let n = h.control_count();
    for i in 0..n {
        let c = h.control(i);
        if c.alive != 0 && (c.w > 0 || c.h > 0) {
            h.show_control(i, true);
        }
    }
}

fn hide_all_controls(h: &mut dyn DriverHost) {
    let n = h.control_count();
    for i in 0..n {
        let c = h.control(i);
        if c.alive != 0 {
            h.show_control(i, false);
        }
    }
}

/// `FindControl`: the first control with `id`, if it has an HWND.
fn find_control(h: &mut dyn DriverHost, id: i32) -> Option<i32> {
    let n = h.control_count();
    for i in 0..n {
        let c = h.control(i);
        if c.id == id {
            return (c.exists != 0).then_some(i);
        }
    }
    None
}

/// `SettingsDriver::Cpp::Rebuild` (`RebuildContentArea`).
///
/// # Safety
/// `st` is valid for reads and writes for the whole call.
pub unsafe fn rebuild(h: &mut dyn DriverHost, st: *mut DriverState) {
    if !h.has_content() {
        return;
    }
    h.destroy_controls();

    // Sub-pages start below the breadcrumb strip painted by the main window.
    let sidebar = sidebar_takes_space(st);
    {
        let (w, ht) = h.main_client();
        let dpi_pos = h.main_dpi();
        let rc = content_area_rect(w, ht, dpi_pos, sidebar, has_breadcrumb(st));
        h.move_content(rc.x, rc.y, rc.w, rc.h);
    }

    let dpi = h.main_dpi();
    let card_w = calc_card_w(h, dpi, sidebar);

    let mut input = {
        let n = unsafe { &(*st).nav };
        PlanInput {
            dpi,
            selected_mode_index: n.selected_mode_index,
            nav_level: n.nav_level,
            current_card_index: n.current_card_index,
            mode_mask: mode_bitmask(ImeMode::from_i32(n.current_mode)),
            color_mode: 0,
            grid_expanded: i32::from(n.color_grid_expanded != 0),
            preview_h: 0,
            expanded: n.card_expanded,
        }
    };
    input.color_mode = h.color_mode();
    if has_breadcrumb(st) {
        input.preview_h = h.preview_height(dpi);
    }

    let (_, plan) = plan_controls(&input, ime_cards(), load_cards());

    h.create_controls(&input, card_w);
    if plan.set_total != 0 {
        unsafe { (*st).nav.total_content_height = plan.total_height };
    }
    if plan.populate != 0 {
        h.populate();
    }

    unsafe { (*st).nav.scroll_pos = 0 };
    update_scroll_info(h, st);
    h.invalidate_content(true);
}

/// `SettingsDriver::Cpp::SwitchMode`.
///
/// # Safety
/// `st` is valid for reads and writes for the whole call.
pub unsafe fn switch_mode(h: &mut dyn DriverHost, st: *mut DriverState, mode: i32) {
    let mode_changed = unsafe {
        let n = &mut (*st).nav;
        let changed = mode != n.current_mode;
        n.current_mode = mode;
        n.selected_mode_index = mode_to_index(ImeMode::from_i32(mode));
        n.nav_level = 0;
        let expanded = default_expanded(ime_cards(), load_cards());
        for (slot, e) in n.card_expanded.iter_mut().zip(expanded) {
            *slot = u8::from(e);
        }
        changed
    };
    // Re-enumerate only on an actual mode change: LoadConfig filters this
    // IME out of the list, and the list is already filtered otherwise.
    if mode_changed {
        h.enumerate_reverse_providers();
    }
    h.load_config(mode);
    h.load_colors();
    h.load_snapshot();
    h.recreate_engine(mode);
    unsafe { rebuild(h, st) };
    h.invalidate_main(None, true);
}

/// `SettingsDriver::Cpp::Nav` (`RunNav`): `ev.kind`, `x`, `y`, `delta`, and
/// `thumb` are the caller's; the rest is filled here.
///
/// # Safety
/// `st` is valid for reads and writes for the whole call.
pub unsafe fn nav(h: &mut dyn DriverHost, st: *mut DriverState, mut ev: NavEvent) {
    ev.dpi = h.main_dpi();
    ev.color_mode = h.color_mode();
    ev.custom_table_dirty = i32::from(h.save_enabled());
    ev.preview_h = 0;
    if has_breadcrumb(st) && h.has_content() {
        ev.preview_h = h.preview_height(ev.dpi);
    }

    let effects = unsafe { nav::handle(&mut (*st).nav, &ev) };

    for fx in effects {
        match fx.kind {
            nav::FX_INVALIDATE_MAIN => {
                let rc = (fx.has_rect != 0).then_some(fx.rc);
                h.invalidate_main(rc, fx.erase != 0);
            }
            nav::FX_INVALIDATE_CONTENT => h.invalidate_content(fx.erase != 0),
            nav::FX_TRACK_LEAVE_MAIN => h.track_leave(false),
            nav::FX_TRACK_LEAVE_CONTENT => h.track_leave(true),
            nav::FX_SCROLL_CHANGED => {
                update_scroll_info(h, st);
                reposition_controls_for_scroll(h, st);
                h.redraw_content();
            }
            nav::FX_SHOW_ALL_CONTROLS => show_all_controls(h),
            nav::FX_HIDE_ALL_CONTROLS => hide_all_controls(h),
            nav::FX_SWITCH_MODE => unsafe { switch_mode(h, st, fx.arg) },
            nav::FX_REBUILD => unsafe { rebuild(h, st) },
            nav::FX_TOGGLE_CONTROL => {
                if let Some(i) = find_control(h, fx.arg) {
                    let on = !h.control_checked(i);
                    h.set_control_checked(i, on);
                    h.toggle_snapshot(fx.arg, on);
                    h.apply_and_save();
                    h.invalidate_content(false);
                    h.update_content();
                }
            }
            nav::FX_ASK_SAVE_THEN_BACK => {
                // 自建詞庫 has unsaved changes: ask before leaving the sub-page.
                let result = h.ask_save_before_back();
                if result == IDYES {
                    h.click_save();
                }
                if result == IDYES || result == IDNO {
                    let back = NavEvent {
                        kind: nav::EV_BACK,
                        ..NavEvent::default()
                    };
                    unsafe { self::nav(h, st, back) };
                }
            }
            nav::FX_MOVE_CONTENT => h.move_content(fx.rc[0], fx.rc[1], fx.rc[2], fx.rc[3]),
            nav::FX_SET_FOCUS_CONTENT => h.focus_content(),
            nav::FX_UPDATE_CONTENT => h.update_content(),
            nav::FX_UPDATE_SCROLL_INFO => update_scroll_info(h, st),
            _ => {}
        }
    }
}

/// `SettingsDriver::Cpp::Frame` (`RunFrame`): `ev.kind`, `dpi`, the client
/// size, and the rectangle are the caller's.
///
/// # Safety
/// `st` is valid for reads and writes for the whole call.
pub unsafe fn frame(h: &mut dyn DriverHost, st: *mut DriverState, mut ev: FrameEvent) {
    ev.system_dark = i32::from(h.system_dark());
    ev.has_content = i32::from(h.has_content());
    let mut fs = FrameState {
        is_dark: i32::from(unsafe { (*st).is_dark } != 0),
    };
    let effects = frame::handle(&mut fs, &ev);
    // Stored first: theme_brushes reads it.
    unsafe { (*st).is_dark = i32::from(fs.is_dark != 0) };

    for fx in effects {
        match fx.kind {
            frame::FX_SET_DARK_TITLE => h.set_dark_title(fx.arg != 0),
            frame::FX_CAPTION_COLOR => h.set_caption_color(fx.arg),
            frame::FX_THEME_BRUSHES => h.theme_brushes(),
            frame::FX_CREATE_FONTS => h.create_fonts(fx.arg),
            frame::FX_DESTROY_FONTS => h.destroy_fonts(),
            frame::FX_INVALIDATE_MAIN => h.invalidate_main(None, true),
            frame::FX_INVALIDATE_CONTENT => h.invalidate_content(true),
            frame::FX_CONTENT_THEME => h.content_theme(fx.arg != 0),
            frame::FX_REBUILD => unsafe { rebuild(h, st) },
            frame::FX_SET_POS => h.set_main_pos(fx.rc[0], fx.rc[1], fx.rc[2], fx.rc[3]),
            frame::FX_CREATE_CONTENT => h.create_content(fx.rc[0], fx.rc[1], fx.rc[2], fx.rc[3]),
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_sizes() {
        assert_eq!(std::mem::size_of::<DriverState>(), 72);
        assert_eq!(std::mem::size_of::<ControlInfo>(), 32);
    }

    /// A host with one toggle control that records its calls.
    #[derive(Default)]
    struct Rec {
        log: Vec<String>,
        checked: bool,
        answer: i32,
    }

    impl DriverHost for Rec {
        fn main_dpi(&mut self) -> u32 {
            96
        }
        fn content_dpi(&mut self) -> u32 {
            96
        }
        fn has_content(&mut self) -> bool {
            true
        }
        fn main_client(&mut self) -> (i32, i32) {
            (900, 700)
        }
        fn content_client_height(&mut self) -> i32 {
            600
        }
        fn vscroll_width(&mut self) -> i32 {
            17
        }
        fn system_dark(&mut self) -> bool {
            false
        }
        fn color_mode(&mut self) -> i32 {
            0
        }
        fn save_enabled(&mut self) -> bool {
            true
        }
        fn preview_height(&mut self, _: u32) -> i32 {
            40
        }
        fn control_count(&mut self) -> i32 {
            1
        }
        fn control(&mut self, _: i32) -> ControlInfo {
            ControlInfo {
                id: 26,
                exists: 1,
                alive: 1,
                visible: 1,
                x: 0,
                y: 100,
                w: 40,
                h: 20,
            }
        }
        fn move_control(&mut self, i: i32, x: i32, y: i32, w: i32, h: i32) {
            self.log.push(format!("move {i} {x} {y} {w} {h}"));
        }
        fn show_control(&mut self, i: i32, show: bool) {
            self.log.push(format!("show {i} {show}"));
        }
        fn reset_control_region(&mut self, i: i32) {
            self.log.push(format!("rgn {i}"));
        }
        fn control_checked(&mut self, _: i32) -> bool {
            self.checked
        }
        fn set_control_checked(&mut self, _: i32, on: bool) {
            self.checked = on;
            self.log.push(format!("check {on}"));
        }
        fn destroy_controls(&mut self) {
            self.log.push("destroy".into());
        }
        fn create_controls(&mut self, _: &PlanInput, card_w: i32) {
            self.log.push(format!("create {card_w}"));
        }
        fn populate(&mut self) {
            self.log.push("populate".into());
        }
        fn invalidate_main(&mut self, rc: Option<[i32; 4]>, erase: bool) {
            self.log.push(format!("invMain {rc:?} {erase}"));
        }
        fn invalidate_content(&mut self, erase: bool) {
            self.log.push(format!("invContent {erase}"));
        }
        fn track_leave(&mut self, content: bool) {
            self.log.push(format!("track {content}"));
        }
        fn set_scroll_info(&mut self, t: i32, p: i32, pos: i32) {
            self.log.push(format!("scroll {t} {p} {pos}"));
        }
        fn redraw_content(&mut self) {
            self.log.push("redraw".into());
        }
        fn update_content(&mut self) {
            self.log.push("update".into());
        }
        fn move_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
            self.log.push(format!("moveContent {x} {y} {w} {h}"));
        }
        fn focus_content(&mut self) {
            self.log.push("focus".into());
        }
        fn create_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
            self.log.push(format!("createContent {x} {y} {w} {h}"));
        }
        fn content_theme(&mut self, dark: bool) {
            self.log.push(format!("contentTheme {dark}"));
        }
        fn set_main_pos(&mut self, x: i32, y: i32, w: i32, h: i32) {
            self.log.push(format!("pos {x} {y} {w} {h}"));
        }
        fn set_dark_title(&mut self, dark: bool) {
            self.log.push(format!("darkTitle {dark}"));
        }
        fn set_caption_color(&mut self, a: i32) {
            self.log.push(format!("caption {a}"));
        }
        fn theme_brushes(&mut self) {
            self.log.push("brushes".into());
        }
        fn create_fonts(&mut self, dpi: i32) {
            self.log.push(format!("fonts {dpi}"));
        }
        fn destroy_fonts(&mut self) {
            self.log.push("destroyFonts".into());
        }
        fn ask_save_before_back(&mut self) -> i32 {
            self.log.push("ask".into());
            self.answer
        }
        fn click_save(&mut self) {
            self.log.push("save".into());
        }
        fn toggle_snapshot(&mut self, id: i32, on: bool) {
            self.log.push(format!("sync {id} {on}"));
        }
        fn apply_and_save(&mut self) {
            self.log.push("apply".into());
        }
        fn enumerate_reverse_providers(&mut self) {
            self.log.push("enum".into());
        }
        fn load_config(&mut self, m: i32) {
            self.log.push(format!("load {m}"));
        }
        fn load_colors(&mut self) {
            self.log.push("colors".into());
        }
        fn load_snapshot(&mut self) {
            self.log.push("snapshot".into());
        }
        fn recreate_engine(&mut self, m: i32) {
            self.log.push(format!("engine {m}"));
        }
    }

    #[test]
    fn switch_mode_enumerates_only_on_change() {
        let mut h = Rec::default();
        let mut st = DriverState::default();
        unsafe { switch_mode(&mut h, &mut st, 0) };
        assert!(!h.log.contains(&"enum".to_string()));
        assert_eq!(h.log.first().map(String::as_str), Some("load 0"));
        h.log.clear();
        unsafe { switch_mode(&mut h, &mut st, 2) };
        assert_eq!(h.log.first().map(String::as_str), Some("enum"));
        assert_eq!(st.nav.current_mode, 2);
        assert_eq!(st.nav.selected_mode_index, 2);
        assert_eq!(st.nav.scroll_pos, 0);
        assert_eq!(h.log.last().map(String::as_str), Some("invMain None true"));
    }

    #[test]
    fn frame_stores_dark_before_effects() {
        let mut h = Rec::default();
        let mut st = DriverState {
            is_dark: 1,
            ..DriverState::default()
        };
        let ev = FrameEvent {
            kind: frame::EV_THEME_CHANGED,
            dpi: 120,
            ..FrameEvent::default()
        };
        unsafe { frame(&mut h, &mut st, ev) };
        assert_eq!(st.is_dark, 0);
        assert_eq!(h.log.first().map(String::as_str), Some("brushes"));
    }

    #[test]
    fn back_does_not_prompt() {
        let mut h = Rec {
            answer: 2,
            ..Rec::default()
        };
        let mut st = DriverState::default();
        st.nav.nav_level = 1;
        st.nav.selected_mode_index = 0;
        unsafe {
            nav(
                &mut h,
                &mut st,
                NavEvent {
                    kind: nav::EV_BACK,
                    ..NavEvent::default()
                },
            )
        };
        // EV_BACK itself navigates; no prompt is involved.
        assert!(!h.log.contains(&"ask".to_string()));
    }
}
