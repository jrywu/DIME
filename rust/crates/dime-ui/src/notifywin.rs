//! The notify window owned by Rust (phase 3c-5e; design in
//! `docs/RUST_WINDOW_OWNERSHIP.md`): the former `CNotifyWindow` with its state
//! (text, colors, title size, position, show times, the capture / timer flags
//! `CBaseWindow` keeps on a top-level UI window) and its shadow
//! ([`crate::shadow::ShadowWindow`]).  Drawing and behavior are
//! [`crate::notify`] (3c-6), now over this state.
//!
//! The callback (switch 中/英, show after the delay) may destroy the window; it
//! is always the last call of a handler (as in C++).  The state is in `Cell`s
//! and the text is cloned out for each use, so nothing is borrowed across it.

use crate::candwin::{self, adjust_text_color};
use crate::layout::{self, Point, Rect};
use crate::notify::{self, NotifyHost};
use crate::shadow::{self, ShadowWindow};
use crate::win::{self, Handler};
use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::rc::Rc;
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{CreateSolidBrush, DeleteObject, InvalidateRect, HBRUSH, HDC, HFONT, PAINTSTRUCT};
use windows::Win32::UI::Input::KeyboardAndMouse::ReleaseCapture;
use windows::Win32::UI::WindowsAndMessaging::{
    GetClientRect, GetWindowRect, IsWindowVisible, KillTimer, SetTimer, SetWindowPos, SWP_NOACTIVATE, SWP_NOSIZE,
    SWP_NOZORDER, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_POPUP,
};

/// `DEFAULT_TIMER_ID` (`BaseWindow.h`): routed to `_OnTimer`, which the notify window does not have.
const DEFAULT_TIMER_ID: usize = 39772;
/// `CBaseWindow::_cornerRadiusBase`.
const CORNER_RADIUS_BASE: i32 = 10;
/// `UI::DEFAULT_WINDOW_X` / `_Y`.
const DEFAULT_WINDOW_POS: i32 = -32768;

/// The environment (C++ `CConfig` / `Global`); the candidate window's.
pub type Env = candwin::Env;

/// The C++ callback (`NOTIFYWNDCALLBACK` with its object).
#[derive(Clone, Copy)]
pub struct Callback {
    pub ctx: *mut c_void,
    /// `NOTIFY_WND` action; may destroy the window.
    pub invoke: unsafe extern "C" fn(ctx: *mut c_void, action: i32, wparam: usize, lparam: isize),
}

/// A test hook called like `g_baseWindowTimerTestHook` (start 1 / end 0, elapse, id).
pub type TimerHook = unsafe extern "C" fn(ctx: *mut c_void, start: i32, elapse: u32, id: usize);

/// The state (`CNotifyWindow`'s members).
pub struct State {
    hwnd: Cell<HWND>,
    x: Cell<i32>,
    y: Cell<i32>,
    cx_title: Cell<i32>,
    cy_title: Cell<i32>,
    delay_show: Cell<u32>,
    time_to_hide: Cell<u32>,
    text: RefCell<Vec<u16>>,
    notify_type: i32,
    text_color: Cell<u32>,
    bk_color: Cell<u32>,
    brush: Cell<HBRUSH>,
    /// `_pUIObjCapture` / `_pTimerUIObj` set (this window is its own UI window).
    capture_object: Cell<bool>,
    timer_object: Cell<bool>,
    shadow: Cell<Option<Rc<ShadowWindow>>>,
    timer_hook: Cell<Option<(TimerHook, *mut c_void)>>,
    env: Env,
    callback: Callback,
}

impl State {
    fn shadow(&self) -> Option<Rc<ShadowWindow>> {
        let s = self.shadow.take();
        self.shadow.set(s.clone());
        s
    }
    fn dpi(&self) -> u32 {
        unsafe { (self.env.dpi_for_hwnd)(self.hwnd.get().0) }
    }
    fn font(&self) -> HFONT {
        HFONT(unsafe { (self.env.font)() })
    }
    fn text(&self) -> Vec<u16> {
        self.text.borrow().clone()
    }

    /// `CBaseWindow::_EndCapture` (the notify window never starts one itself).
    fn end_capture(&self) {
        self.capture_object.set(false);
        unsafe {
            let _ = ReleaseCapture();
        }
    }
    /// `CBaseWindow::_StartTimer` / `_EndTimer` (`_SetTimerObject`).
    fn start_timer(&self, elapse: u32, id: usize) {
        if let Some((hook, ctx)) = self.timer_hook.get() {
            unsafe { hook(ctx, 1, elapse, id) };
        }
        self.timer_object.set(true);
        unsafe {
            SetTimer(Some(self.hwnd.get()), id, elapse, None);
        }
    }
    fn end_timer(&self, id: usize) {
        if let Some((hook, ctx)) = self.timer_hook.get() {
            unsafe { hook(ctx, 0, 0, id) };
        }
        if id == DEFAULT_TIMER_ID {
            self.timer_object.set(false);
        }
        unsafe {
            let _ = KillTimer(Some(self.hwnd.get()), id);
        }
    }
    fn invalidate(&self) {
        let h = self.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = InvalidateRect(Some(h), None, false);
            }
        }
    }

    /// `_ResizeWindow`: `CBaseWindow::_Resize` to the title size, then invalidate.
    fn resize_window(&self) {
        candwin::base_resize(
            self.hwnd.get(),
            self.x.get(),
            self.y.get(),
            self.cx_title.get(),
            self.cy_title.get(),
            self.dpi(),
        );
        self.invalidate();
    }

    /// `_OnPaint` (`ps` `None`: no `PAINTSTRUCT`).
    fn paint(&self, dc: HDC, ps: Option<&PAINTSTRUCT>) {
        let text = self.text();
        let input = notify::PaintInput {
            font: self.font(),
            bk_brush: self.brush.get(),
            paint: ps.map(|p| Rect {
                left: p.rcPaint.left,
                top: p.rcPaint.top,
                right: p.rcPaint.right,
                bottom: p.rcPaint.bottom,
            }),
            text: &text,
            text_color: self.text_color.get(),
            bk_color: self.bk_color.get(),
            cx_title: self.cx_title.get(),
        };
        if let Some(out) = unsafe { notify::paint(dc, &input) } {
            self.cx_title.set(out.cx_title);
            self.cy_title.set(out.cy_title);
            if out.resize {
                self.resize_window();
            }
        }
    }
}

/// `NotifyHost` over the state.
struct Host<'a>(&'a State);

impl NotifyHost for Host<'_> {
    fn hwnd(&mut self) -> HWND {
        self.0.hwnd.get()
    }
    fn text(&mut self) -> Vec<u16> {
        self.0.text()
    }
    fn font(&mut self) -> HFONT {
        self.0.font()
    }
    fn notify_type(&mut self) -> i32 {
        self.0.notify_type
    }
    fn text_metric(&mut self) -> (i32, i32, i32) {
        (0, 0, 0) // _TextMetric is never filled
    }
    fn set_title_size(&mut self, cx: i32, cy: i32) {
        self.0.cx_title.set(cx);
        self.0.cy_title.set(cy);
    }
    fn set_position(&mut self, x: i32, y: i32) {
        self.0.x.set(x);
        self.0.y.set(y);
    }
    fn set_show_times(&mut self, delay_show: u32, time_to_hide: u32) {
        self.0.delay_show.set(delay_show);
        self.0.time_to_hide.set(time_to_hide);
    }
    fn time_to_hide(&mut self) -> u32 {
        self.0.time_to_hide.get()
    }
    fn corner_radius_base(&mut self) -> i32 {
        CORNER_RADIUS_BASE
    }
    fn dpi(&mut self) -> u32 {
        self.0.dpi()
    }
    fn invoke(&mut self, action: i32, wparam: usize, lparam: isize) {
        let cb = self.0.callback;
        unsafe { (cb.invoke)(cb.ctx, action, wparam, lparam) }
    }
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT) {
        self.0.paint(dc, Some(ps));
    }
    fn dpi_changed(&mut self, hwnd: HWND) {
        unsafe { (self.0.env.set_default_text_font)(hwnd.0) }
    }
    fn base_show(&mut self, on: bool) {
        win::base_show(self.0.hwnd.get(), on);
    }
    fn is_capture(&mut self) -> bool {
        self.0.capture_object.get()
    }
    fn end_capture(&mut self) {
        self.0.end_capture();
    }
    fn is_timer(&mut self) -> bool {
        self.0.timer_object.get()
    }
    fn start_timer(&mut self, elapse: u32, id: usize) {
        self.0.start_timer(elapse, id);
    }
    fn end_timer(&mut self, id: usize) {
        self.0.end_timer(id);
    }
    fn has_shadow(&mut self) -> bool {
        self.0.shadow().is_some()
    }
    fn shadow_hwnd(&mut self) -> HWND {
        self.0.shadow().map_or(HWND::default(), |s| s.hwnd())
    }
    fn shadow_show(&mut self, show: i32) {
        if let Some(s) = self.0.shadow() {
            s.show(show != 0);
        }
    }
    fn shadow_owner_moved(&mut self, resized: bool) {
        if let Some(s) = self.0.shadow() {
            s.owner_moved(resized);
        }
    }
    fn delete_shadow(&mut self) {
        drop(self.0.shadow.take());
    }
}

struct NotifyHandler(Rc<State>);

impl Handler for NotifyHandler {
    fn message(&self, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> Option<LRESULT> {
        self.0.hwnd.set(hwnd);
        Some(unsafe { notify::window_proc(&mut Host(&self.0), hwnd, msg, wparam, lparam) })
    }
    /// `CBaseWindow` routes `WM_TIMER` to the timer object: this window's `_OnTimerID`.
    fn timer(&self, _hwnd: HWND, id: usize) {
        if !self.0.timer_object.get() || id == DEFAULT_TIMER_ID {
            return; // no timer object, or _OnTimer (none on the notify window)
        }
        notify::on_timer_id(&mut Host(&self.0), id);
    }
}

/// A notify window owned by Rust.
pub struct NotifyWindow {
    window: Rc<win::Window>,
    state: Rc<State>,
}

/// The classes of the notify window and its shadow.
#[derive(Clone, Copy)]
pub struct Classes {
    pub notify: u16,
    pub notify_shadow: u16,
}

impl NotifyWindow {
    /// The constructor and `CNotifyWindow::_Create`: the text (when not
    /// empty), the popup (`WM_CREATE` measures it), its shadow, then
    /// `_ResizeWindow`.  `None` when the main window could not be created (a
    /// failed shadow skips `_ResizeWindow`, as in C++).
    ///
    /// # Safety
    /// The class atoms must be registered by [`win::register_class`]; the
    /// environment and callback must stay valid for the window's life.
    pub unsafe fn create(
        hinstance: HINSTANCE,
        classes: Classes,
        parent: Option<HWND>,
        notify_type: i32,
        text: &[u16],
        env: Env,
        callback: Callback,
    ) -> Option<NotifyWindow> {
        let state = Rc::new(State {
            hwnd: Cell::new(HWND::default()),
            x: Cell::new(DEFAULT_WINDOW_POS),
            y: Cell::new(DEFAULT_WINDOW_POS),
            cx_title: Cell::new(0),
            cy_title: Cell::new(0),
            delay_show: Cell::new(0),
            time_to_hide: Cell::new(0),
            text: RefCell::new(Vec::new()),
            notify_type,
            text_color: Cell::new(0),
            bk_color: Cell::new(0),
            brush: Cell::new(HBRUSH::default()),
            capture_object: Cell::new(false),
            timer_object: Cell::new(false),
            shadow: Cell::new(None),
            timer_hook: Cell::new(None),
            env,
            callback,
        });
        if !text.is_empty() {
            *state.text.borrow_mut() = text.to_vec();
        }
        let window = unsafe {
            win::create(
                hinstance,
                classes.notify,
                WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                WS_POPUP,
                parent,
                0,
                0,
                Box::new(NotifyHandler(Rc::clone(&state))),
            )
        }?;
        state.hwnd.set(window.hwnd());
        let senv = shadow::Env {
            dpi_for_hwnd: env.dpi_for_hwnd,
            dark: env.dark,
        };
        let Some(s) = (unsafe {
            ShadowWindow::create(
                hinstance,
                classes.notify_shadow,
                (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED).0,
                WS_POPUP.0,
                window.hwnd(),
                CORNER_RADIUS_BASE,
                senv,
            )
        }) else {
            return Some(NotifyWindow { window, state });
        };
        state.shadow.set(Some(Rc::new(s)));
        state.resize_window();
        Some(NotifyWindow { window, state })
    }

    fn st(&self) -> &State {
        self.state.hwnd.set(self.window.hwnd());
        &self.state
    }

    pub fn hwnd(&self) -> HWND {
        self.window.hwnd()
    }
    pub fn is_visible(&self) -> bool {
        let h = self.window.hwnd();
        !h.is_invalid() && unsafe { IsWindowVisible(h) }.as_bool()
    }
    pub fn window_rect(&self) -> Rect {
        let mut rc = RECT::default();
        let h = self.window.hwnd();
        if !h.is_invalid() {
            unsafe {
                let _ = GetWindowRect(h, &mut rc);
            }
        }
        to_rect(rc)
    }
    pub fn client_rect(&self) -> Rect {
        let mut rc = RECT::default();
        let h = self.window.hwnd();
        if !h.is_invalid() {
            unsafe {
                let _ = GetClientRect(h, &mut rc);
            }
        }
        to_rect(rc)
    }
    /// `_GetWidth` / `_GetHeight`.
    pub fn title_size(&self) -> (i32, i32) {
        (self.state.cx_title.get(), self.state.cy_title.get())
    }
    /// `_Move`.
    pub fn move_to(&self, x: i32, y: i32) {
        let st = self.st();
        if st.x.get() == x && st.y.get() == y {
            return;
        }
        st.x.set(x);
        st.y.set(y);
        let h = st.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = SetWindowPos(h, None, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
            }
        }
    }
    /// `_Show(isShowWnd, delayShow, timeToHide)`.
    pub fn show(&self, on: bool, delay_show: u32, time_to_hide: u32) {
        notify::show(&mut Host(self.st()), on, delay_show, time_to_hide);
    }
    /// `_OnTimerID` (as the timer object; tests call it directly as C++ tests do).
    pub fn on_timer_id(&self, id: usize) {
        notify::on_timer_id(&mut Host(self.st()), id);
    }
    /// `_SetTextColor`.
    pub fn set_text_color(&self, color: u32, bk: u32) {
        let st = self.st();
        st.text_color.set(adjust_text_color(color, bk));
        st.bk_color.set(bk);
    }
    /// `_SetFillColor`: a new background brush.
    pub fn set_fill_color(&self, fill: u32) {
        let st = self.st();
        let old = st.brush.replace(unsafe { CreateSolidBrush(COLORREF(fill)) });
        if !old.is_invalid() {
            unsafe {
                let _ = DeleteObject(old.into());
            }
        }
    }
    /// `_InvalidateRect`.
    pub fn invalidate(&self) {
        self.st().invalidate();
    }
    /// `_SetString` (an empty text keeps the old one, as in C++).
    pub fn set_string(&self, text: &[u16]) {
        if !text.is_empty() {
            *self.state.text.borrow_mut() = text.to_vec();
        }
    }
    /// `CBaseWindow::_GetWindowExtent`.
    pub fn window_extent(&self, text: &Rect, window: &Rect) -> Point {
        let work = candwin::work_area_from_point(layout::work_area_probe(text, window));
        layout::fit_point_around_text_extent(text, &work, window)
    }
    pub fn shadow(&self) -> Option<Rc<ShadowWindow>> {
        self.st().shadow()
    }
    /// `(cx_title, cy_title, x, y, delay_show, time_to_hide)` (tests).
    pub fn state_for_test(&self) -> [i32; 6] {
        let st = &self.state;
        [
            st.cx_title.get(),
            st.cy_title.get(),
            st.x.get(),
            st.y.get(),
            st.delay_show.get() as i32,
            st.time_to_hide.get() as i32,
        ]
    }
    /// `_OnPaint` into `dc` (tests); `paint` `None`: no `PAINTSTRUCT`.
    ///
    /// # Safety
    /// `dc` must be a valid device context.
    pub unsafe fn paint(&self, dc: HDC, paint: Option<Rect>) {
        let ps = paint.map(|p| PAINTSTRUCT {
            rcPaint: RECT {
                left: p.left,
                top: p.top,
                right: p.right,
                bottom: p.bottom,
            },
            ..Default::default()
        });
        self.st().paint(dc, ps.as_ref());
    }
    /// The timer test hook (`g_baseWindowTimerTestHook` for this window).
    pub fn set_timer_hook(&self, hook: Option<(TimerHook, *mut c_void)>) {
        self.state.timer_hook.set(hook);
    }
}

fn to_rect(rc: RECT) -> Rect {
    Rect {
        left: rc.left,
        top: rc.top,
        right: rc.right,
        bottom: rc.bottom,
    }
}

impl Drop for NotifyWindow {
    fn drop(&mut self) {
        // ~CNotifyWindow (after UIPresenter's _Destroy): brush, the default timer, shadow.
        self.window.destroy();
        let b = self.state.brush.replace(HBRUSH::default());
        if !b.is_invalid() {
            unsafe {
                let _ = DeleteObject(b.into());
            }
        }
        if self.state.timer_object.get() {
            self.state.end_timer(DEFAULT_TIMER_ID);
        }
        drop(self.state.shadow.take());
    }
}
