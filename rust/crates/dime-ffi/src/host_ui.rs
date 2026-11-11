//! Phase 4b-2c: the presenter's UI, called from `FfiHost` instead of bouncing
//! through `CUIPresenter`. C++ still reads TSF (context view, layout sink, UI
//! element) and passes the facts in; nothing here is borrowed across a C++
//! callback or a window call that can re-enter.
//!
//! `page == 0` skips the page index. The C++ original divides by the page size
//! and only crashes; that difference is accepted.

use dime_core::keyclass::function;
use dime_ui::candpaint::Colors;
use dime_ui::candproc::HWND;
use dime_ui::candwin::Classes as CandClasses;
use dime_ui::notifywin::Classes as NotifyClasses;
use dime_ui::presenter::{LayoutFacts, ShowFacts};
use dime_ui::presenterwin::{self, CandCreate, Presenter};
use dime_ui::win::HINSTANCE;
use dime_ui::layout::{Point, Rect};
use std::ffi::c_void;

/// `NOTIFY_TYPE`.
pub const NOTIFY_CHN_ENG: u32 = 0;
#[allow(dead_code)] // `NOTIFY_BEEP`: the host still probes it; no Rust caller names it.
pub const NOTIFY_BEEP: u32 = 2;
pub const NOTIFY_REVERSE: u32 = 3;
#[allow(dead_code)] // `NOTIFY_SPECIAL`: tests and C++ pass the slot number; Rust compares only CHN_ENG.
pub const NOTIFY_SPECIAL: u32 = 4;

/// `_UpdateUIElement` flag sets. 0 keeps the flags already stored.
#[allow(dead_code)] // `UI_KEEP`: C++ `which == 0` leaves the flags; Rust never sends it.
pub const UI_KEEP: u32 = 0;
pub const UI_SELECTION: u32 = 1;
pub const UI_SELECTION_PAGE: u32 = 2;
pub const UI_COUNT_STRING: u32 = 3;
pub const UI_HIDE: u32 = 4;

/// What `notify_show_facts` fills (`DimeNotifyShowFacts`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct NotifyShowFacts {
    pub has_context: i32,
    pub have_view: i32,
    pub parent: *mut c_void,
    pub view_parent: *mut c_void,
    pub hinstance: *mut c_void,
    pub notify_class: u32,
    pub notify_shadow_class: u32,
    pub color: u32,
    pub bk: u32,
    pub refresh: i32,
    pub has_parent: i32,
    pub rc_left: i32,
    pub rc_top: i32,
    pub rc_right: i32,
    pub rc_bottom: i32,
    pub caret_x: i32,
    pub caret_y: i32,
}

/// What `candidate_open` fills (`DimeCandOpenFacts`). `go`: 0 skip (`S_OK`),
/// 1 make the window, 2 layout failed (caller ends the list).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct CandOpenFacts {
    pub go: i32,
    pub hr: i32,
    pub text_service: i32,
    pub show_mode: i32,
    pub parent: *mut c_void,
    pub hinstance: *mut c_void,
    pub range: *mut c_void,
    pub width: u32,
    pub fill: u32,
    pub number: u32,
    pub number_bk: u32,
    pub text: u32,
    pub bk: u32,
    pub selected_text: u32,
    pub selected_bk: u32,
    pub cls_candidate: u32,
    pub cls_shadow: u32,
    pub cls_scroll: u32,
    pub cls_notify: u32,
    pub cls_notify_shadow: u32,
}

/// `SetPageIndexWithScrollInfo`: page starts `i * page`. Empty when `page` is 0.
pub fn page_starts(item_count: u32, page: u32) -> Vec<u32> {
    if page == 0 {
        return Vec::new();
    }
    let pages = item_count.wrapping_add(page - 1) / page;
    (0..pages).map(|i| i * page).collect()
}

/// What an arrow key does to the candidate list (`AdviseUIChangedByArrowKey`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arrow {
    /// `_MoveCandidateSelection`.
    MoveSelection(i32),
    /// `_MoveCandidatePage`.
    MovePage(i32),
    /// `_SetCandidateSelection` (`MOVETO_TOP` 0, `MOVETO_BOTTOM` -1).
    Select(i32),
}

/// `true` when the UI-element update includes `TF_CLUIE_CURRENTPAGE`.
pub fn arrow_updates_page(arrow: Arrow) -> bool {
    !matches!(arrow, Arrow::MoveSelection(_))
}

/// `ui_less` is the presenter's `!_isShowMode`, not the text service's UI-less flag.
pub fn arrow_action(function_id: u32, ui_less: bool, switch_pages: bool) -> Option<Arrow> {
    const UP: i32 = -1;
    const DOWN: i32 = 1;
    match function_id {
        function::MOVE_UP => {
            if !ui_less {
                Some(Arrow::MoveSelection(UP))
            } else if switch_pages {
                Some(Arrow::MovePage(UP))
            } else {
                None
            }
        }
        function::MOVE_DOWN => {
            if !ui_less {
                Some(Arrow::MoveSelection(DOWN))
            } else if switch_pages {
                Some(Arrow::MovePage(DOWN))
            } else {
                None
            }
        }
        function::MOVE_LEFT => {
            if ui_less {
                Some(Arrow::MoveSelection(UP))
            } else if switch_pages {
                Some(Arrow::MovePage(UP))
            } else {
                None
            }
        }
        function::MOVE_RIGHT => {
            if ui_less {
                Some(Arrow::MoveSelection(DOWN))
            } else if switch_pages {
                Some(Arrow::MovePage(DOWN))
            } else {
                None
            }
        }
        function::MOVE_PAGE_UP => Some(Arrow::MovePage(UP)),
        function::MOVE_PAGE_DOWN => Some(Arrow::MovePage(DOWN)),
        function::MOVE_PAGE_TOP => Some(Arrow::Select(0)),
        function::MOVE_PAGE_BOTTOM => Some(Arrow::Select(-1)),
        _ => None,
    }
}

fn hwnd(p: *mut c_void) -> Option<HWND> {
    (!p.is_null()).then_some(HWND(p))
}

/// `ShowNotifyText` after C++ read the facts. Returns whether the caller should
/// `_ProbeComposition` (a view was obtained; the kind check stays in C++).
pub fn show_notify(p: &Presenter, text: &[u16], kind: u32, facts: &NotifyShowFacts) -> bool {
    if kind as usize >= presenterwin::NOTIFY_COUNT {
        return false;
    }
    // Issue #127: a mode-state slot must not cover a visible compositional hint.
    if kind == NOTIFY_CHN_ENG && p.any_hint_visible() {
        return false;
    }
    p.dispose(Some(kind as usize));
    if facts.has_context == 0 {
        return false;
    }
    let classes = NotifyClasses { notify: facts.notify_class as u16, notify_shadow: facts.notify_shadow_class as u16 };
    let made = unsafe { p.make_notify(kind as usize, HINSTANCE(facts.hinstance), classes, hwnd(facts.parent), text) };
    if !made {
        return false;
    }
    p.set_text_color(facts.color, facts.bk);
    if facts.have_view == 0 {
        return false;
    }
    let show = ShowFacts {
        refresh: facts.refresh,
        has_parent: facts.has_parent,
        rc_caret: Rect { left: facts.rc_left, top: facts.rc_top, right: facts.rc_right, bottom: facts.rc_bottom },
        gui_caret: Point { x: facts.caret_x, y: facts.caret_y },
    };
    let caret_updated = p.notify_location_for_show(&show);
    p.show(kind as usize, true, 0, 0);
    if p.exists(kind as usize) {
        p.restack_for_show(caret_updated);
    }
    true
}

/// Hide the candidate window. Returns whether any notify slot exists
/// (`ClearNotify` then ends the layout and disposes them).
pub fn hide_candidate(p: &Presenter) -> bool {
    if let Some(w) = p.cand_window() {
        w.show(false);
    }
    p.any_exists()
}

/// The candidate window `candidate_open` described. False when one already exists.
///
/// # Safety
/// The class atoms must be registered.
pub unsafe fn make_candidate(p: &Presenter, facts: &CandOpenFacts) -> bool {
    let classes = CandClasses {
        candidate: facts.cls_candidate as u16,
        candidate_shadow: facts.cls_shadow as u16,
        scroll_bar: facts.cls_scroll as u16,
        notify: facts.cls_notify as u16,
        notify_shadow: facts.cls_notify_shadow as u16,
    };
    let colors = Colors {
        number: facts.number,
        number_bk: facts.number_bk,
        text: facts.text,
        bk: facts.bk,
        selected_text: facts.selected_text,
        selected_bk: facts.selected_bk,
    };
    let create = CandCreate {
        hinstance: HINSTANCE(facts.hinstance),
        classes,
        parent: hwnd(facts.parent),
        wnd_width: facts.width,
        range: facts.range,
        colors,
        fill: facts.fill,
    };
    unsafe { p.make_candidate(&create) }
}

/// Add `items` (text, key), then the page index of that new list (not the
/// accumulated window count) and the string length. No-ops without a candidate.
pub fn add_candidate_page(p: &Presenter, items: &[(&[u16], &[u16])], with_key: bool, range: *const c_void, width: u32) {
    if !p.has_candidate() {
        return;
    }
    p.add_items(items, with_key);
    p.set_index_range(range);
    if !range.is_null() {
        let page = p.candidate_page_size();
        if page != 0 {
            let pages = page_starts(items.len() as u32, page);
            p.set_page_index(&pages);
            if let Some(w) = p.cand_window() {
                w.set_scroll_info(items.len() as i32, page as i32);
            }
        }
    }
    if let Some(w) = p.cand_window() {
        w.set_cand_string_length(width);
    }
}

/// Copy a list string out before any other presenter call. Empty when there is
/// no item (an empty item's text is null, same as `CandList::string`).
pub fn list_string(p: &Presenter, index: i32) -> Vec<u16> {
    let Some(list) = p.cand_list() else { return Vec::new() };
    copy_units(list.string(index))
}

/// The highlighted item's key code, copied out immediately.
pub fn selected_key(p: &Presenter) -> Vec<u16> {
    let Some(list) = p.cand_list() else { return Vec::new() };
    copy_units(list.selected_key_code())
}

fn copy_units(ptr_len: (*const u16, u32)) -> Vec<u16> {
    let (ptr, len) = ptr_len;
    if ptr.is_null() || len == 0 {
        return Vec::new();
    }
    // The allocation stays until the list changes; this is the only use of the pointer.
    unsafe { std::slice::from_raw_parts(ptr, len as usize) }.to_vec()
}

/// `candidate_place` writes a `DimeLayoutFacts`, which is a [`LayoutFacts`].
pub fn layout_from_place(p: &Presenter, facts: &LayoutFacts) {
    p.layout_change(facts);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn page_starts_match_the_cpp_division() {
        assert!(page_starts(0, 10).is_empty());
        assert_eq!(page_starts(10, 10), vec![0]);
        assert_eq!(page_starts(11, 10), vec![0, 10]);
        assert_eq!(page_starts(25, 10), vec![0, 10, 20]);
        assert!(page_starts(5, 0).is_empty());
    }

    #[test]
    fn arrow_matches_advise_ui_changed_by_arrow_key() {
        assert_eq!(arrow_action(function::MOVE_DOWN, false, false), Some(Arrow::MoveSelection(1)));
        assert_eq!(arrow_action(function::MOVE_DOWN, true, true), Some(Arrow::MovePage(1)));
        assert_eq!(arrow_action(function::MOVE_DOWN, true, false), None);
        assert_eq!(arrow_action(function::MOVE_LEFT, false, true), Some(Arrow::MovePage(-1)));
        assert_eq!(arrow_action(function::MOVE_LEFT, true, false), Some(Arrow::MoveSelection(-1)));
        assert_eq!(arrow_action(function::MOVE_PAGE_UP, false, false), Some(Arrow::MovePage(-1)));
        assert_eq!(arrow_action(function::MOVE_PAGE_TOP, true, false), Some(Arrow::Select(0)));
        assert_eq!(arrow_action(function::MOVE_PAGE_BOTTOM, false, false), Some(Arrow::Select(-1)));
        assert!(!arrow_updates_page(Arrow::MoveSelection(1)));
        assert!(arrow_updates_page(Arrow::MovePage(1)));
        assert!(arrow_updates_page(Arrow::Select(-1)));
    }

    #[test]
    fn chn_eng_does_not_cover_a_visible_hint() {
        let p = test_presenter();
        let classes = test_notify_classes();
        let facts = NotifyShowFacts {
            has_context: 1,
            have_view: 1,
            hinstance: module_handle(),
            notify_class: classes.notify as u32,
            notify_shadow_class: classes.notify_shadow as u32,
            color: 0,
            bk: 0x00_ff_ff_ff,
            ..NotifyShowFacts::default()
        };
        let text: Vec<u16> = "特別".encode_utf16().collect();
        assert!(show_notify(&p, &text, NOTIFY_SPECIAL, &facts));
        assert!(p.exists(NOTIFY_SPECIAL as usize));
        assert!(p.is_visible(NOTIFY_SPECIAL as usize));
        let mode: Vec<u16> = "中".encode_utf16().collect();
        assert!(!show_notify(&p, &mode, NOTIFY_CHN_ENG, &facts));
        assert!(!p.exists(NOTIFY_CHN_ENG as usize));
        assert!(p.exists(NOTIFY_SPECIAL as usize));
    }

    fn module_handle() -> *mut c_void {
        #[link(name = "kernel32")]
        extern "system" {
            fn GetModuleHandleW(name: *const u16) -> *mut c_void;
        }
        unsafe { GetModuleHandleW(std::ptr::null()) }
    }

    fn test_notify_classes() -> NotifyClasses {
        use dime_ui::win::{ClassKind, PCWSTR, register_class};
        use std::sync::OnceLock;
        static ATOMS: OnceLock<(u16, u16)> = OnceLock::new();
        let (notify, shadow) = *ATOMS.get_or_init(|| {
            let h = HINSTANCE(module_handle());
            let n = wide("DIME.Test.HostUiNotify");
            let s = wide("DIME.Test.HostUiShadow");
            unsafe {
                (
                    register_class(h, PCWSTR(n.as_ptr()), ClassKind::Popup),
                    register_class(h, PCWSTR(s.as_ptr()), ClassKind::Child),
                )
            }
        });
        NotifyClasses { notify, notify_shadow: shadow }
    }

    fn test_presenter() -> Presenter {
        unsafe extern "C" fn dpi(_: *mut c_void) -> u32 { 96 }
        unsafe extern "C" fn zero() -> i32 { 0 }
        unsafe extern "C" fn font() -> *mut c_void { std::ptr::null_mut() }
        unsafe extern "C" fn set_font(_: *mut c_void) {}
        unsafe extern "C" fn invoke(_: *mut c_void, _: i32) {}
        unsafe extern "C" fn ninvoke(_: *mut c_void, _: i32, _: usize, _: isize) {}
        unsafe extern "C" fn range_count(_: *const c_void) -> i32 { 10 }
        unsafe extern "C" fn range_char(_: *const c_void, _: i32) -> u16 { b'1' as u16 }
        let env = dime_ui::candwin::Env { dpi_for_hwnd: dpi, dark: zero, always_show: zero, font, set_default_text_font: set_font };
        let callback = dime_ui::notifywin::Callback { ctx: std::ptr::null_mut(), invoke: ninvoke };
        let source = presenterwin::CandSource { ctx: std::ptr::null_mut(), invoke, range_count, range_char };
        Presenter::new(env, callback, source)
    }

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(std::iter::once(0)).collect()
    }
}
