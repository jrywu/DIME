//! The presenter owned by Rust (migration phase 4b-2a, `docs/RUST_TSF_SHELL.md`):
//! `CUIPresenter`'s placement state (`_candLocation`, `_notifyLocation`,
//! `_rectCompRange`) and its notify-window stack (`_pNotifyWnds`, one Rust
//! [`NotifyWindow`] per `NOTIFY_TYPE` slot), with the decisions of
//! [`crate::presenter`] over them.  The candidate window is still held by C++
//! in this step and reached through [`CandidateHost`].
//!
//! C++ keeps what needs TSF (the parent window from the context view, the
//! layout sink, `_ProbeComposition`) and the notify callback
//! (`_NotifyChangeNotification`).  The callback may come back into this object
//! (show, dispose) while a window method is on the stack, so the state is in
//! `Cell`s and windows are cloned out of the slot table before any call:
//! nothing is borrowed across a call into a window.

use crate::layout::{NotifySlot, Point, Rect};
use crate::notifywin::{self, Classes, NotifyWindow};
use crate::presenter::{self, Host, LayoutFacts, ShowFacts, State};
use std::cell::{Cell, RefCell};
use std::rc::Rc;
use windows::Win32::Foundation::{HINSTANCE, HWND};
use windows::Win32::UI::WindowsAndMessaging::{GetSystemMetrics, SM_CYSCREEN};

/// `NOTIFY_TYPE::NOTIFY_COUNT`.
pub const NOTIFY_COUNT: usize = 5;
/// `NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP` / `NOTIFY_SPECIAL_CODE`: the compositional hints.
pub const NOTIFY_REVERSE_LOOKUP: usize = 3;
pub const NOTIFY_SPECIAL_CODE: usize = 4;
/// `UI::DEFAULT_WINDOW_X` / `_Y`.
pub const DEFAULT_WINDOW_POS: i32 = -32768;

/// The candidate window C++ still holds (`_pCandidateWnd`).
pub trait CandidateHost {
    /// `_pCandidateWnd != nullptr`.
    fn has_candidate(&mut self) -> bool;
    fn client_rect(&mut self) -> Rect;
    fn window_extent(&mut self, comp: &Rect, cand: &Rect) -> Point;
    fn move_to(&mut self, p: Point);
    /// Only asked when there is one.
    fn visible(&mut self) -> bool;
    fn window_rect(&mut self) -> Rect;
}

/// A slot whose object exists (`_pNotifyWnds[i] != nullptr`); the window is
/// `None` when it could not be created (`_Create` still succeeded, as in C++).
struct Slot {
    window: Option<Rc<NotifyWindow>>,
}

pub struct Presenter {
    state: Cell<State>,
    slots: RefCell<Vec<Option<Slot>>>,
    env: notifywin::Env,
    callback: notifywin::Callback,
}

impl Presenter {
    /// The constructor: the locations off-screen, no slots.  `callback` gets
    /// every notify window's actions (`_NotifyWndCallback`).
    pub fn new(env: notifywin::Env, callback: notifywin::Callback) -> Presenter {
        let off = Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS };
        Presenter {
            state: Cell::new(State {
                cand_location: off,
                notify_location: off,
                rect_comp_range: Rect { left: off.x, top: off.y, right: off.x, bottom: off.y },
            }),
            slots: RefCell::new((0..NOTIFY_COUNT).map(|_| None).collect()),
            env,
            callback,
        }
    }

    pub fn state(&self) -> State {
        self.state.get()
    }
    pub fn set_state(&self, st: State) {
        self.state.set(st);
    }
    /// `ResetCompRange`.
    pub fn reset_comp_range(&self) {
        let mut st = self.state.get();
        st.rect_comp_range = Rect::default();
        st.notify_location = Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS };
        self.state.set(st);
    }

    /// The window of slot `i` (cloned out: no borrow is held while it is used).
    pub fn window(&self, i: usize) -> Option<Rc<NotifyWindow>> {
        self.slots.borrow().get(i).and_then(|s| s.as_ref()).and_then(|s| s.window.clone())
    }
    /// `_pNotifyWnds[i] != nullptr`.
    pub fn exists(&self, i: usize) -> bool {
        self.slots.borrow().get(i).is_some_and(|s| s.is_some())
    }
    pub fn any_exists(&self) -> bool {
        self.slots.borrow().iter().any(|s| s.is_some())
    }

    /// `MakeNotifyWindow` after C++ found the parent: the slot's object and
    /// its window, once (the text is used only when the window is created).
    /// False for a slot out of range (`S_FALSE`).
    ///
    /// # Safety
    /// The class atoms must be registered by [`crate::win::register_class`] (or 0).
    pub unsafe fn make_notify(
        &self,
        i: usize,
        hinstance: HINSTANCE,
        classes: Classes,
        parent: Option<HWND>,
        text: &[u16],
    ) -> bool {
        if i >= NOTIFY_COUNT {
            return false;
        }
        if self.exists(i) {
            return true;
        }
        // The object before the window, as `make_unique` then `_Create`.
        self.slots.borrow_mut()[i] = Some(Slot { window: None });
        let w = unsafe { NotifyWindow::create(hinstance, classes, parent, i as i32, text, self.env, self.callback) };
        if let Some(s) = self.slots.borrow_mut()[i].as_mut() {
            s.window = w.map(Rc::new);
        }
        true
    }

    /// `DisposeNotifyWindow(i)`; `None`: every slot.
    pub fn dispose(&self, i: Option<usize>) {
        let taken: Vec<Slot> = {
            let mut slots = self.slots.borrow_mut();
            match i {
                None => slots.iter_mut().filter_map(Option::take).collect(),
                Some(i) => slots.get_mut(i).and_then(Option::take).into_iter().collect(),
            }
        };
        // Dropped after the borrow ends: destroying a window sends messages.
        drop(taken);
    }

    /// `SetNotifyText`.
    pub fn set_string(&self, i: usize, text: &[u16]) {
        if let Some(w) = self.window(i) {
            w.set_string(text);
        }
    }
    /// `ShowNotify`.
    pub fn show(&self, i: usize, on: bool, delay_show: u32, time_to_hide: u32) {
        if let Some(w) = self.window(i) {
            w.show(on, delay_show, time_to_hide);
        }
    }
    /// `_SetNotifyTextColor`: every slot.
    pub fn set_text_color(&self, color: u32, bk: u32) {
        for i in 0..NOTIFY_COUNT {
            if let Some(w) = self.window(i) {
                w.set_text_color(color, bk);
                w.set_fill_color(bk);
                w.invalidate();
            }
        }
    }
    pub fn is_visible(&self, i: usize) -> bool {
        self.window(i).is_some_and(|w| w.is_visible())
    }
    /// `IsNotifyShown`.
    pub fn any_visible(&self) -> bool {
        (0..NOTIFY_COUNT).any(|i| self.is_visible(i))
    }
    /// `_AnyHintSlotVisible`.
    pub fn any_hint_visible(&self) -> bool {
        self.is_visible(NOTIFY_REVERSE_LOOKUP) || self.is_visible(NOTIFY_SPECIAL_CODE)
    }

    fn with_host<C: CandidateHost, R>(&self, cand: &mut C, f: impl FnOnce(&mut Owned<'_, C>, &mut State) -> R) -> R {
        let mut st = self.state.get();
        let r = f(&mut Owned { p: self, cand }, &mut st);
        self.state.set(st);
        r
    }

    /// `_LayoutChangeNotification` after C++ read the facts.
    pub fn layout_change(&self, cand: &mut impl CandidateHost, f: &LayoutFacts) {
        self.with_host(cand, |h, st| presenter::layout_change(h, st, f));
    }
    /// `ShowNotifyText` before showing; returns `caretUpdated`.
    pub fn notify_location_for_show(&self, f: &ShowFacts) -> bool {
        let mut st = self.state.get();
        let r = presenter::notify_location_for_show(&mut st, f);
        self.state.set(st);
        r
    }
    /// `ShowNotifyText` after showing (no delay).
    pub fn restack_for_show(&self, cand: &mut impl CandidateHost, caret_updated: bool) {
        self.with_host(cand, |h, st| presenter::restack_for_show(h, st, caret_updated));
    }
    /// `_RestackVisibleSlots`.
    pub fn restack_visible_slots(&self, cand: &mut impl CandidateHost, grow_up: bool) {
        self.with_host(cand, |h, st| presenter::restack_visible_slots(h, st, grow_up));
    }
}

/// [`presenter::Host`] over the slots of a [`Presenter`] and the C++ candidate window.
struct Owned<'a, C> {
    p: &'a Presenter,
    cand: &'a mut C,
}

impl<C: CandidateHost> Host for Owned<'_, C> {
    fn has_candidate(&mut self) -> bool {
        self.cand.has_candidate()
    }
    fn candidate_client_rect(&mut self) -> Rect {
        self.cand.client_rect()
    }
    fn candidate_window_extent(&mut self, comp: &Rect, cand: &Rect) -> Point {
        self.cand.window_extent(comp, cand)
    }
    fn candidate_move(&mut self, p: Point) {
        self.cand.move_to(p)
    }
    fn candidate_visible(&mut self) -> bool {
        self.cand.visible()
    }
    fn candidate_window_rect(&mut self) -> Rect {
        self.cand.window_rect()
    }
    fn slots(&mut self) -> Vec<NotifySlot> {
        (0..NOTIFY_COUNT)
            .map(|i| match self.p.window(i) {
                Some(w) => {
                    let (cx, cy) = w.title_size();
                    NotifySlot { visible: i32::from(w.is_visible()), width: cx, height: cy }
                }
                None => NotifySlot::default(),
            })
            .collect()
    }
    fn slot_client_rect(&mut self, i: usize) -> Rect {
        self.p.window(i).map(|w| w.client_rect()).unwrap_or_default()
    }
    fn slot_window_extent(&mut self, i: usize, comp: &Rect, rect: &Rect) -> Point {
        self.p.window(i).map(|w| w.window_extent(comp, rect)).unwrap_or_default()
    }
    fn slot_move(&mut self, i: usize, p: Point) {
        if let Some(w) = self.p.window(i) {
            w.move_to(p.x, p.y);
        }
    }
    fn screen_bottom(&mut self) -> i32 {
        unsafe { GetSystemMetrics(SM_CYSCREEN) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_void;

    unsafe extern "C" fn dpi(_: *mut c_void) -> u32 {
        96
    }
    unsafe extern "C" fn zero() -> i32 {
        0
    }
    unsafe extern "C" fn font() -> *mut c_void {
        std::ptr::null_mut()
    }
    unsafe extern "C" fn set_font(_: *mut c_void) {}
    unsafe extern "C" fn invoke(_: *mut c_void, _: i32, _: usize, _: isize) {}

    fn presenter() -> Presenter {
        let env = notifywin::Env { dpi_for_hwnd: dpi, dark: zero, always_show: zero, font, set_default_text_font: set_font };
        Presenter::new(env, notifywin::Callback { ctx: std::ptr::null_mut(), invoke })
    }

    #[test]
    fn slots_exist_without_a_window_and_are_disposed() {
        let p = presenter();
        assert_eq!(p.state().notify_location, Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS });
        let none = Classes { notify: 0, notify_shadow: 0 };
        // Class atom 0: the window cannot be created, the slot still exists (as C++).
        assert!(unsafe { p.make_notify(3, HINSTANCE::default(), none, None, &[0x4e2d]) });
        assert!(unsafe { p.make_notify(1, HINSTANCE::default(), none, None, &[]) });
        assert!(!unsafe { p.make_notify(NOTIFY_COUNT, HINSTANCE::default(), none, None, &[]) }, "out of range");
        assert!(p.exists(3) && p.exists(1) && !p.exists(0) && p.any_exists());
        assert!(p.window(3).is_none() && !p.any_visible() && !p.any_hint_visible());
        p.show(3, true, 0, 0);
        p.dispose(Some(3));
        assert!(!p.exists(3) && p.exists(1));
        p.dispose(None);
        assert!(!p.any_exists());
        p.set_state(State { rect_comp_range: Rect { left: 1, top: 2, right: 3, bottom: 4 }, ..p.state() });
        p.reset_comp_range();
        assert_eq!(p.state().rect_comp_range, Rect::default());
    }
}
