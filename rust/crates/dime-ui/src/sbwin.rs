//! The candidate window's scroll bar owned by Rust (phase 3c-5c; design in
//! `docs/RUST_WINDOW_OWNERSHIP.md`).  A child [`crate::win::Window`] whose
//! state (the former `CScrollBarWindow` members) lives in `Cell`s; its events
//! are [`crate::sbproc`] and its drawing [`crate::sbpaint`], run through a
//! [`SbHost`] over the state.
//!
//! `CBaseWindow` puts a scroll bar's capture and timers on its top-level UI
//! window (the candidate window); here the candidate is reached through
//! [`Parent`] callbacks, and it forwards the `WM_TIMER`s it receives to
//! [`ScrollBarWindow::timer`].  No borrow lives across a host call: the
//! notifications to the candidate window re-enter the scroll bar.

use crate::candwnd::{self, ScrollInfo};
use crate::layout::{Point, Rect};
use crate::sbpaint;
use crate::sbproc::{self, SbHost};
use crate::win::{self, Handler};
use std::cell::Cell;
use std::ffi::c_void;
use std::rc::Rc;
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{InvalidateRect, HDC, PAINTSTRUCT};
use windows::Win32::UI::Input::KeyboardAndMouse::{EnableWindow, IsWindowEnabled};
use windows::Win32::UI::WindowsAndMessaging::{
    GetClientRect, GetWindowLongW, IsWindowVisible, MoveWindow, SendMessageW, SetLayeredWindowAttributes, GWL_EXSTYLE,
    LWA_ALPHA, LWA_COLORKEY, WINDOW_EX_STYLE, WINDOW_STYLE, WS_EX_LAYERED,
};

/// What the scroll bar needs from its environment (C++ `CConfig`).
#[derive(Clone, Copy)]
pub struct Env {
    pub dpi_for_hwnd: unsafe extern "C" fn(hwnd: *mut c_void) -> u32,
    pub dark: unsafe extern "C" fn() -> i32,
    /// `CConfig::GetAlwaysShowScrollbars`.
    pub always_show: unsafe extern "C" fn() -> i32,
}

/// The candidate window's side of the capture and the timers (`CBaseWindow`
/// keeps them on the top-level UI window).
#[derive(Clone, Copy)]
pub struct Parent {
    pub ctx: *mut c_void,
    pub hwnd: HWND,
    pub is_capture: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub start_capture: unsafe extern "C" fn(ctx: *mut c_void),
    pub end_capture: unsafe extern "C" fn(ctx: *mut c_void),
    pub start_timer: unsafe extern "C" fn(ctx: *mut c_void, elapse: u32, id: usize),
    pub end_timer: unsafe extern "C" fn(ctx: *mut c_void, id: usize),
}

/// The state (`CScrollBarWindow`'s members).
pub struct State {
    hwnd: Cell<HWND>,
    si: Cell<ScrollInfo>,
    /// `_isDragging`; the start (`_dragStartY`, `_dragStartPos`) outlives the drag, as in C++.
    dragging: Cell<bool>,
    drag_start: Cell<(i32, i32)>,
    alpha: Cell<u8>,
    fading: Cell<bool>,
    hovered: Cell<bool>,
    tracking: Cell<bool>,
    held: Cell<i32>,
    repeat: Cell<bool>,
    highlight: Cell<(Rect, i32, u32)>,
    env: Env,
    parent: Parent,
}

/// A snapshot of the state (tests).
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Snapshot {
    pub si: ScrollInfo,
    pub dragging: i32,
    pub drag_start_y: i32,
    pub drag_start_pos: i32,
    pub alpha: u32,
    pub fading: i32,
    pub hovered: i32,
    pub tracking: i32,
    pub held: i32,
    pub repeat_started: i32,
}

struct StateHost<'a>(&'a State);

impl SbHost for StateHost<'_> {
    fn hwnd(&mut self) -> HWND {
        self.0.hwnd.get()
    }
    fn parent_hwnd(&mut self) -> Option<HWND> {
        (!self.0.parent.hwnd.is_invalid()).then_some(self.0.parent.hwnd)
    }
    fn client_rect(&mut self) -> Rect {
        let mut rc = RECT::default();
        let h = self.0.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = GetClientRect(h, &mut rc);
            }
        }
        Rect {
            left: rc.left,
            top: rc.top,
            right: rc.right,
            bottom: rc.bottom,
        }
    }
    fn dpi(&mut self) -> u32 {
        unsafe { (self.0.env.dpi_for_hwnd)(self.0.hwnd.get().0) }
    }
    fn hover_width(&mut self) -> i32 {
        let dpi = self.dpi();
        candwnd::scale(candwnd::SCROLLBAR_HOVER_WIDTH, dpi)
    }
    fn scroll_info(&mut self) -> ScrollInfo {
        self.0.si.get()
    }
    fn set_scroll_info(&mut self, si: ScrollInfo) {
        self.0.si.set(si);
    }
    fn dragging(&mut self) -> Option<(i32, i32)> {
        self.0.dragging.get().then(|| self.0.drag_start.get())
    }
    fn set_dragging(&mut self, drag: Option<(i32, i32)>) {
        // Ending a drag keeps the start values, as CScrollBarWindow does.
        self.0.dragging.set(drag.is_some());
        if let Some(d) = drag {
            self.0.drag_start.set(d);
        }
    }
    fn alpha(&mut self) -> u8 {
        self.0.alpha.get()
    }
    fn set_alpha(&mut self, alpha: u8) {
        self.0.alpha.set(alpha);
    }
    fn fading(&mut self) -> bool {
        self.0.fading.get()
    }
    fn set_fading(&mut self, on: bool) {
        self.0.fading.set(on);
    }
    fn hovered(&mut self) -> bool {
        self.0.hovered.get()
    }
    fn set_hovered(&mut self, on: bool) {
        self.0.hovered.set(on);
    }
    fn tracking_mouse(&mut self) -> bool {
        self.0.tracking.get()
    }
    fn set_tracking_mouse(&mut self, on: bool) {
        self.0.tracking.set(on);
    }
    fn held_button(&mut self) -> i32 {
        self.0.held.get()
    }
    fn set_held_button(&mut self, button: i32) {
        self.0.held.set(button);
    }
    fn repeat_started(&mut self) -> bool {
        self.0.repeat.get()
    }
    fn set_repeat_started(&mut self, on: bool) {
        self.0.repeat.set(on);
    }
    fn is_enabled(&mut self) -> bool {
        let h = self.0.hwnd.get();
        !h.is_invalid() && unsafe { IsWindowEnabled(h) }.as_bool()
    }
    fn base_enable(&mut self, on: bool) {
        let h = self.0.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = EnableWindow(h, on);
            }
        }
    }
    fn is_visible(&mut self) -> bool {
        let h = self.0.hwnd.get();
        !h.is_invalid() && unsafe { IsWindowVisible(h) }.as_bool()
    }
    fn base_show(&mut self, on: bool) {
        win::base_show(self.0.hwnd.get(), on);
    }
    fn invalidate(&mut self) {
        let h = self.0.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = InvalidateRect(Some(h), None, false);
            }
        }
    }
    fn is_capture(&mut self) -> bool {
        unsafe { (self.0.parent.is_capture)(self.0.parent.ctx) != 0 }
    }
    fn start_capture(&mut self) {
        unsafe { (self.0.parent.start_capture)(self.0.parent.ctx) }
    }
    fn end_capture(&mut self) {
        unsafe { (self.0.parent.end_capture)(self.0.parent.ctx) }
    }
    fn start_timer(&mut self, elapse: u32, id: usize) {
        unsafe { (self.0.parent.start_timer)(self.0.parent.ctx, elapse, id) }
    }
    fn end_timer(&mut self, id: usize) {
        unsafe { (self.0.parent.end_timer)(self.0.parent.ctx, id) }
    }
    fn notify_command(&mut self, msg: u32, code: i32, pos: i32) {
        // CBaseWindow::_NotifyCommand: SendMessage(UI window, msg, MAKEWPARAM(code, pos), 0).
        let parent = self.0.parent.hwnd;
        if !parent.is_invalid() {
            let wparam = (code as u32 & 0xFFFF) as usize | (((pos as u32) & 0xFFFF) as usize) << 16;
            unsafe {
                SendMessageW(parent, msg, Some(WPARAM(wparam)), Some(LPARAM(0)));
            }
        }
    }
    fn shift_page(&mut self, pages: i32, notify: bool) {
        sbproc::shift_page(self, pages, notify);
    }
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT) {
        let st = self.0;
        let (highlight, radius, color) = st.highlight.get();
        let input = sbpaint::Input {
            hwnd: st.hwnd.get(),
            paint: Some(Rect {
                left: ps.rcPaint.left,
                top: ps.rcPaint.top,
                right: ps.rcPaint.right,
                bottom: ps.rcPaint.bottom,
            }),
            client: self.client_rect(),
            dpi: self.dpi(),
            si: st.si.get(),
            dark: unsafe { (st.env.dark)() != 0 },
            full: st.hovered.get() || unsafe { (st.env.always_show)() != 0 },
            highlight,
            highlight_radius: radius,
            highlight_color: color,
        };
        unsafe { sbpaint::paint(dc, &input) };
    }
}

struct SbHandler(Rc<State>);

impl Handler for SbHandler {
    fn message(&self, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> Option<LRESULT> {
        self.0.hwnd.set(hwnd);
        Some(unsafe { sbproc::window_proc(&mut StateHost(&self.0), hwnd, msg, wparam, lparam) })
    }
}

/// A scroll bar owned by Rust.
pub struct ScrollBarWindow {
    window: Rc<win::Window>,
    state: Rc<State>,
}

impl ScrollBarWindow {
    /// `CScrollBarWindow::_Create`: a layered child of the candidate window
    /// (retried without `WS_EX_LAYERED` for hosts that refuse layered
    /// children), at the initial alpha (opaque with "always show scrollbars").
    ///
    /// # Safety
    /// `class` must be registered by [`win::register_class`]; the parent
    /// callbacks must stay valid for the scroll bar's life.
    pub unsafe fn create(
        hinstance: HINSTANCE,
        class: u16,
        ex_style: u32,
        style: u32,
        env: Env,
        parent: Parent,
    ) -> Option<ScrollBarWindow> {
        let alpha = if unsafe { (env.always_show)() } != 0 {
            sbproc::FADE_ALPHA_VISIBLE
        } else {
            0
        };
        let state = Rc::new(State {
            hwnd: Cell::new(HWND::default()),
            si: Cell::new(ScrollInfo::default()),
            dragging: Cell::new(false),
            drag_start: Cell::new((0, 0)),
            alpha: Cell::new(alpha),
            fading: Cell::new(false),
            hovered: Cell::new(false),
            tracking: Cell::new(false),
            held: Cell::new(candwnd::hit::NONE),
            repeat: Cell::new(false),
            highlight: Cell::new((Rect::default(), -1, 0)),
            env,
            parent,
        });
        let make = |ex: u32| unsafe {
            win::create(
                hinstance,
                class,
                WINDOW_EX_STYLE(ex),
                WINDOW_STYLE(style),
                Some(parent.hwnd),
                0,
                0,
                Box::new(SbHandler(Rc::clone(&state))),
            )
        };
        let window = make(ex_style | WS_EX_LAYERED.0).or_else(|| make(ex_style))?;
        let hwnd = window.hwnd();
        state.hwnd.set(hwnd);
        if unsafe { GetWindowLongW(hwnd, GWL_EXSTYLE) } as u32 & WS_EX_LAYERED.0 != 0 {
            unsafe {
                let _ = SetLayeredWindowAttributes(hwnd, COLORREF(sbpaint::COLORKEY), alpha, LWA_COLORKEY | LWA_ALPHA);
            }
        }
        Some(ScrollBarWindow { window, state })
    }

    fn host(&self) -> StateHost<'_> {
        self.state.hwnd.set(self.window.hwnd());
        StateHost(&self.state)
    }

    pub fn hwnd(&self) -> HWND {
        self.window.hwnd()
    }
    pub fn hover_width(&self) -> i32 {
        self.host().hover_width()
    }
    /// `CBaseWindow::_Resize` for a child window: `MoveWindow` (no rounded region).
    pub fn resize(&self, x: i32, y: i32, cx: i32, cy: i32) {
        let h = self.window.hwnd();
        if !h.is_invalid() {
            unsafe {
                let _ = MoveWindow(h, x, y, cx, cy, true);
            }
        }
    }
    pub fn is_enabled(&self) -> bool {
        self.host().is_enabled()
    }
    pub fn show(&self, on: bool) {
        sbproc::show(&mut self.host(), on);
    }
    pub fn set_scroll_info(&self, si: ScrollInfo) {
        sbproc::set_scroll_info(&mut self.host(), si);
    }
    pub fn shift_line(&self, lines: i32, notify: bool) {
        sbproc::shift_line(&mut self.host(), lines, notify);
    }
    pub fn shift_page(&self, pages: i32, notify: bool) {
        sbproc::shift_page(&mut self.host(), pages, notify);
    }
    pub fn shift_position(&self, pos: i32, notify: bool) {
        sbproc::shift_position(&mut self.host(), pos, notify);
    }
    pub fn set_candidate_mouse_in(&self, inside: bool) {
        sbproc::set_candidate_mouse_in(&mut self.host(), inside);
    }
    pub fn on_scroll_activity(&self) {
        sbproc::on_scroll_activity(&mut self.host());
    }
    pub fn owner_moved(&self) {
        sbproc::on_owner_moved(&mut self.host());
    }
    pub fn lbutton_down(&self, pt: Point) {
        sbproc::on_lbutton_down(&mut self.host(), pt);
    }
    pub fn lbutton_up(&self) {
        sbproc::on_lbutton_up(&mut self.host());
    }
    pub fn mouse_move(&self) {
        sbproc::on_mouse_move(&mut self.host());
    }
    /// A timer the candidate window received for the scroll bar (`_OnTimerID`).
    pub fn timer(&self, id: usize) {
        sbproc::on_timer_id(&mut self.host(), id);
    }
    /// `_SetSelectionHighlight` (RC26).
    pub fn set_selection_highlight(&self, rect: Rect, radius: i32, color: u32) {
        self.state.highlight.set((rect, radius, color));
    }
    /// `_OnPaint` into `dc` (tests: `None` for no `PAINTSTRUCT`).
    ///
    /// # Safety
    /// `dc` must be a valid device context.
    pub unsafe fn paint(&self, dc: HDC, paint: Option<Rect>) {
        let mut host = self.host();
        match paint {
            Some(p) => {
                let mut ps = PAINTSTRUCT {
                    rcPaint: RECT {
                        left: p.left,
                        top: p.top,
                        right: p.right,
                        bottom: p.bottom,
                    },
                    ..Default::default()
                };
                host.paint(dc, &mut ps);
            }
            None => {
                let st = &self.state;
                let (highlight, radius, color) = st.highlight.get();
                let input = sbpaint::Input {
                    hwnd: st.hwnd.get(),
                    paint: None,
                    client: host.client_rect(),
                    dpi: host.dpi(),
                    si: st.si.get(),
                    dark: unsafe { (st.env.dark)() != 0 },
                    full: st.hovered.get() || unsafe { (st.env.always_show)() != 0 },
                    highlight,
                    highlight_radius: radius,
                    highlight_color: color,
                };
                unsafe { sbpaint::paint(dc, &input) };
            }
        }
    }
    /// The state (tests).
    pub fn snapshot(&self) -> Snapshot {
        let st = &self.state;
        let (y, p) = st.drag_start.get();
        Snapshot {
            si: st.si.get(),
            dragging: i32::from(st.dragging.get()),
            drag_start_y: y,
            drag_start_pos: p,
            alpha: u32::from(st.alpha.get()),
            fading: i32::from(st.fading.get()),
            hovered: i32::from(st.hovered.get()),
            tracking: i32::from(st.tracking.get()),
            held: st.held.get(),
            repeat_started: i32::from(st.repeat.get()),
        }
    }
    /// `_SetCurPos` (tests).
    pub fn set_cur_pos(&self, pos: i32, code: i32) {
        sbproc::set_cur_pos(&mut self.host(), pos, code);
    }
    /// `_Enable` (tests).
    pub fn enable(&self, on: bool) {
        sbproc::enable(&mut self.host(), on);
    }
    pub fn set_hovered(&self, on: bool) {
        self.state.hovered.set(on);
    }
    /// `(rect, radius, color)` of the RC26 highlight (tests).
    pub fn selection_highlight(&self) -> (Rect, i32, u32) {
        self.state.highlight.get()
    }
}

impl Drop for ScrollBarWindow {
    fn drop(&mut self) {
        self.window.destroy();
    }
}
