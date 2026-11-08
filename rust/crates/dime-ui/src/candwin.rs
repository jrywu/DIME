//! The candidate window owned by Rust (phase 3c-5d; design in
//! `docs/RUST_WINDOW_OWNERSHIP.md`): the former `CCandidateWindow` with its
//! state, its shadow ([`crate::shadow::ShadowWindow`]) and its scroll bar
//! ([`crate::sbwin::ScrollBarWindow`]).  Messages are [`crate::candproc`],
//! drawing [`crate::candpaint`], layout [`crate::candwnd`].
//!
//! The candidate list (strings, selection, pages) stays in C++
//! (`CCandidateListModel`) and is reached through [`Model`].  As its own UI
//! window it keeps the capture / timer flags `CBaseWindow` keeps on the
//! top-level window; the scroll bar's timers arrive here and go back to it.
//!
//! Re-entrancy: the scroll bar notifies this window (`WM_VSCROLL`), whose
//! handler calls the scroll bar again; the children are `Rc`s cloned out of
//! `Cell`s for each use, and the state is in `Cell`s, so nothing is borrowed
//! across a call.

use crate::candpaint::{self, Colors};
use crate::candproc::{self, CandHost};
use crate::candwnd;
use crate::layout::{self, Point, Rect};
use crate::sbwin::{self, ScrollBarWindow};
use crate::shadow::{self, ShadowWindow};
use crate::win::{self, Handler};
use std::cell::Cell;
use std::ffi::c_void;
use std::rc::Rc;
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, POINT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    CreateRoundRectRgn, CreateSolidBrush, DeleteObject, GetMonitorInfoW, InvalidateRect, MonitorFromPoint,
    ScreenToClient, SetWindowRgn, HBRUSH, HDC, HFONT, MONITORINFO, MONITOR_DEFAULTTONEAREST, PAINTSTRUCT,
};
use windows::Win32::UI::Input::KeyboardAndMouse::{GetCapture, ReleaseCapture, SetCapture};
use windows::Win32::UI::WindowsAndMessaging::{
    GetClassWord, GetClientRect, GetWindow, GetWindowLongW, GetWindowRect, IsWindow, IsWindowVisible, KillTimer,
    MoveWindow, SetTimer, SetWindowPos, SystemParametersInfoW, GCW_ATOM, GWL_EXSTYLE, GWL_STYLE, GW_HWNDPREV,
    HWND_TOPMOST, SPI_GETWORKAREA, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE, SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS,
    WS_CHILD, WS_CLIPCHILDREN, WS_DISABLED, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_POPUP,
};

/// `UIConstants.h`.
const STAY_ON_TOP_TIMER_ID: usize = 39778;
const STAY_ON_TOP_TICK_MS: u32 = 30;
const ANIMATION_TIMER_ID: usize = 39774;
/// `DEFAULT_TIMER_ID` (`BaseWindow.h`): routed to `_OnTimer`, which the candidate window does not have.
const DEFAULT_TIMER_ID: usize = 39772;
/// `CCandidateWindow::_cornerRadiusBase`.
pub const CORNER_RADIUS_BASE: i32 = 10;

/// The environment (C++ `CConfig` / `Global`).
#[derive(Clone, Copy)]
pub struct Env {
    pub dpi_for_hwnd: unsafe extern "C" fn(hwnd: *mut c_void) -> u32,
    pub dark: unsafe extern "C" fn() -> i32,
    pub always_show: unsafe extern "C" fn() -> i32,
    /// `Global::defaultlFontHandle`.
    pub font: unsafe extern "C" fn() -> *mut c_void,
    /// `CConfig::SetDefaultTextFont(hwnd)` (`WM_DPICHANGED`).
    pub set_default_text_font: unsafe extern "C" fn(hwnd: *mut c_void),
}

/// The DIME window classes (`#135`: the stay-on-top timer ignores DIME's own windows).
#[derive(Clone, Copy, Default)]
pub struct Classes {
    pub candidate: u16,
    pub candidate_shadow: u16,
    pub scroll_bar: u16,
    pub notify: u16,
    pub notify_shadow: u16,
}

/// The candidate list (C++ `CCandidateListModel` and the index range) and the
/// selection callback.
#[derive(Clone, Copy)]
pub struct Model {
    pub ctx: *mut c_void,
    pub selection: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_selection_raw: unsafe extern "C" fn(ctx: *mut c_void, index: u32),
    pub set_selection_offset: unsafe extern "C" fn(ctx: *mut c_void, offset: i32),
    /// `SetSelection(index)` without the scroll bar (the window's `_SetSelection(index, FALSE)`).
    pub set_selection: unsafe extern "C" fn(ctx: *mut c_void, index: i32),
    /// `MovePage(offset)` without the scroll bar.
    pub move_page: unsafe extern "C" fn(ctx: *mut c_void, offset: i32),
    /// `GetCurrentPage`; 0 when it fails.
    pub current_page: unsafe extern "C" fn(ctx: *mut c_void, page: *mut i32) -> i32,
    /// `GetCurrentPage` + `AdjustPageIndex` (painting); 0 when the page is unknown.
    pub paint_page: unsafe extern "C" fn(ctx: *mut c_void, page: *mut u32, start: *mut u32) -> i32,
    pub page_count: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub page_start: unsafe extern "C" fn(ctx: *mut c_void, page: i32) -> u32,
    pub count: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    /// The index range's count; -1 without an index range.
    pub rows_in_page: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    /// The selection key of row `row` of the page (`CandIndex`).
    pub index_char: unsafe extern "C" fn(ctx: *mut c_void, row: i32) -> u16,
    /// Item `index`'s string (null: no item).
    pub item: unsafe extern "C" fn(ctx: *mut c_void, index: u32, len: *mut u32) -> *const u16,
    pub has_callback: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    /// `CANDWND_ACTION`; may destroy the window.
    pub invoke: unsafe extern "C" fn(ctx: *mut c_void, action: i32),
}

/// `_AdjustTextColor`: the inverse when the text is too close to its background.
pub fn adjust_text_color(color: u32, bk: u32) -> u32 {
    if is_too_similar(color, bk) {
        color ^ 0x00FF_FFFF
    } else {
        color
    }
}

/// `Global::IsTooSimilar`.
pub fn is_too_similar(a: u32, b: u32) -> bool {
    if (a | b) & 0xFF00_0000 != 0 {
        return false;
    }
    let d = |s: u32| ((a >> s) & 0xFF).abs_diff((b >> s) & 0xFF);
    d(0) + d(8) + d(16) < 80
}

/// The state (`CCandidateWindow`'s members).
pub struct State {
    hwnd: Cell<HWND>,
    x: Cell<i32>,
    y: Cell<i32>,
    cx_title: Cell<i32>,
    cy_row: Cell<i32>,
    padding: Cell<i32>,
    wnd_width: Cell<u32>,
    colors: Cell<Colors>,
    brush: Cell<HBRUSH>,
    tracking: Cell<bool>,
    /// `_pUIObjCapture` / `_pTimerUIObj` set (this window is its own UI window).
    capture_object: Cell<bool>,
    timer_object: Cell<bool>,
    shadow: Cell<Option<Rc<ShadowWindow>>>,
    scroll_bar: Cell<Option<Rc<ScrollBarWindow>>>,
    env: Env,
    classes: Classes,
    model: Model,
}

impl State {
    fn shadow(&self) -> Option<Rc<ShadowWindow>> {
        let s = self.shadow.take();
        self.shadow.set(s.clone());
        s
    }
    fn scroll_bar(&self) -> Option<Rc<ScrollBarWindow>> {
        let s = self.scroll_bar.take();
        self.scroll_bar.set(s.clone());
        s
    }
    fn dpi(&self) -> u32 {
        unsafe { (self.env.dpi_for_hwnd)(self.hwnd.get().0) }
    }
    /// `_GetScrollBarWidth`.
    fn sb_width(&self) -> i32 {
        match self.scroll_bar() {
            Some(sb) => sb.hover_width(),
            None => candwnd::scale(candwnd::SCROLLBAR_HOVER_WIDTH, self.dpi()),
        }
    }
    fn client_rect(&self) -> Rect {
        let mut rc = RECT::default();
        let h = self.hwnd.get();
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
    fn font(&self) -> HFONT {
        HFONT(unsafe { (self.env.font)() })
    }

    /// `CBaseWindow::_StartCapture` / `_EndCapture` / `_IsCapture` for this window.
    fn start_capture(&self) {
        self.capture_object.set(true);
        unsafe {
            SetCapture(self.hwnd.get());
        }
    }
    fn end_capture(&self) {
        self.capture_object.set(false);
        unsafe {
            let _ = ReleaseCapture();
        }
    }
    /// `CBaseWindow::_StartTimer` / `_EndTimer`.
    fn start_timer(&self, elapse: u32, id: usize) {
        self.timer_object.set(true);
        unsafe {
            SetTimer(Some(self.hwnd.get()), id, elapse, None);
        }
    }
    fn end_timer(&self, id: usize) {
        if id == DEFAULT_TIMER_ID {
            self.timer_object.set(false);
        }
        unsafe {
            let _ = KillTimer(Some(self.hwnd.get()), id);
        }
    }

    /// `_ResizeWindow`.
    fn resize_window(&self) {
        let rows = unsafe { (self.model.rows_in_page)(self.model.ctx) };
        if rows < 0 {
            return;
        }
        let sbw = self.sb_width();
        let (w, h) = candwnd::window_size(self.cx_title.get(), self.cy_row.get(), self.padding.get(), rows, sbw);
        base_resize(self.hwnd.get(), self.x.get(), self.y.get(), w, h, self.dpi());
        if let Some(sb) = self.scroll_bar() {
            // Shorten the scroll bar by the corner arc at the top and bottom.
            let r = candwnd::scrollbar_rect(&self.client_rect(), sbw, self.dpi(), CORNER_RADIUS_BASE);
            sb.resize(r.left, r.top, r.width(), r.height());
        }
    }

    /// `_OnPaint` (the Rust drawing of 3c-1, with the state here).
    fn paint(&self, dc: HDC, ps: &PAINTSTRUCT) {
        let m = &self.model;
        let (mut page, mut start) = (0u32, 0u32);
        let has_page = unsafe { (m.paint_page)(m.ctx, &mut page, &mut start) } != 0;
        let rows_in_page = unsafe { (m.rows_in_page)(m.ctx) };
        let draw_list = has_page && rows_in_page >= 0;
        let sbw = self.sb_width();
        let dpi = self.dpi();
        let count = unsafe { (m.count)(m.ctx) };
        let selection = unsafe { (m.selection)(m.ctx) };

        let mut texts: Vec<Option<Vec<u16>>> = Vec::new();
        let mut keys: Vec<u16> = Vec::new();
        let mut selected = None;
        let mut sb_origin = None;
        if draw_list {
            let mut index = start;
            while index < count && (texts.len() as i32) < rows_in_page {
                keys.push(unsafe { (m.index_char)(m.ctx, texts.len() as i32) });
                let mut len = 0u32;
                let p = unsafe { (m.item)(m.ctx, index, &mut len) };
                texts.push((!p.is_null()).then(|| unsafe { std::slice::from_raw_parts(p, len as usize) }.to_vec()));
                if selection == index as i32 {
                    selected = Some(texts.len() - 1);
                }
                index += 1;
            }
            // RC26: the selection highlight is mirrored on a non-layered scroll bar.
            if let Some(sb) = self.scroll_bar() {
                let h = sb.hwnd();
                if !h.is_invalid() && unsafe { GetWindowLongW(h, GWL_EXSTYLE) } as u32 & WS_EX_LAYERED.0 == 0 {
                    let mut rc = RECT::default();
                    unsafe {
                        let _ = GetWindowRect(h, &mut rc);
                    }
                    let mut pt = POINT { x: rc.left, y: rc.top };
                    unsafe {
                        let _ = ScreenToClient(self.hwnd.get(), &mut pt);
                    }
                    sb_origin = Some((pt.x, pt.y));
                }
            }
        }
        let rows: Vec<candpaint::Row> = keys
            .iter()
            .zip(texts.iter())
            .map(|(&k, t)| candpaint::Row {
                index_char: k,
                text: t.as_deref(),
            })
            .collect();
        let input = candpaint::ListInput {
            font: self.font(),
            bk_brush: self.brush.get(),
            paint: Rect {
                left: ps.rcPaint.left,
                top: ps.rcPaint.top,
                right: ps.rcPaint.right,
                bottom: ps.rcPaint.bottom,
            },
            draw_list,
            wnd_width: self.wnd_width.get(),
            sb_width: sbw,
            dpi,
            rows_in_page: rows_in_page.max(0),
            rows: &rows,
            selected,
            colors: self.colors.get(),
            sb_origin,
        };
        let out = unsafe { candpaint::paint_list(dc, &input) };

        if let Some(h) = out.highlight {
            if let Some(sb) = self.scroll_bar() {
                sb.set_selection_highlight(Rect::default(), -1, 0);
                if let Some(h) = h {
                    sb.set_selection_highlight(h.rect, h.radius, h.color);
                }
            }
        }
        if let Some(mt) = out.metrics {
            self.cx_title.set(mt.cx_title);
            self.cy_row.set(mt.cy_row);
            self.padding.set(mt.padding);
            self.wnd_width.set(mt.wnd_width);
            if mt.needs_resize {
                self.resize_window();
            }
            // _DrawPageIndicator: absolute client coordinates, after any resize.
            let page_count = unsafe { (m.page_count)(m.ctx) };
            if page_count > 1 {
                let ind = candpaint::IndicatorInput {
                    font: self.font(),
                    client: self.client_rect(),
                    cy_row: self.cy_row.get(),
                    rows_in_page,
                    sb_width: self.sb_width(),
                    dpi: self.dpi(),
                    current_page: page,
                    page_count,
                    number_color: self.colors.get().number,
                };
                unsafe { candpaint::paint_page_indicator(dc, &ind) };
            }
        }
        unsafe { candpaint::paint_end(dc, out.old_font) };
    }
}

/// `CBaseWindow::_Resize` for a popup: move and size, then the rounded region.
fn base_resize(hwnd: HWND, x: i32, y: i32, cx: i32, cy: i32, dpi: u32) {
    if hwnd.is_invalid() {
        return;
    }
    unsafe {
        let _ = MoveWindow(hwnd, x, y, cx, cy, true);
        let style = GetWindowLongW(hwnd, GWL_STYLE) as u32;
        if style & WS_POPUP.0 != 0 && cx > 0 && cy > 0 {
            let radius = candwnd::scale(CORNER_RADIUS_BASE, dpi);
            let rgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, radius, radius);
            SetWindowRgn(hwnd, Some(rgn), true);
        }
    }
}

/// `CandHost` over the state and the model.
struct Host<'a>(&'a State);

impl CandHost for Host<'_> {
    fn selection(&mut self) -> i32 {
        unsafe { (self.0.model.selection)(self.0.model.ctx) }
    }
    fn set_selection_raw(&mut self, index: u32) {
        unsafe { (self.0.model.set_selection_raw)(self.0.model.ctx, index) }
    }
    fn set_selection_offset(&mut self, offset: i32) {
        unsafe { (self.0.model.set_selection_offset)(self.0.model.ctx, offset) }
    }
    fn set_selection(&mut self, index: i32) {
        unsafe { (self.0.model.set_selection)(self.0.model.ctx, index) }
    }
    fn move_page(&mut self, offset: i32) {
        unsafe { (self.0.model.move_page)(self.0.model.ctx, offset) }
    }
    fn current_page(&mut self) -> Option<i32> {
        let mut p = 0;
        (unsafe { (self.0.model.current_page)(self.0.model.ctx, &mut p) } != 0).then_some(p)
    }
    fn page_count(&mut self) -> u32 {
        unsafe { (self.0.model.page_count)(self.0.model.ctx) }
    }
    fn page_start(&mut self, page: i32) -> u32 {
        unsafe { (self.0.model.page_start)(self.0.model.ctx, page) }
    }
    fn count(&mut self) -> u32 {
        unsafe { (self.0.model.count)(self.0.model.ctx) }
    }
    fn rows_in_page(&mut self) -> Option<u32> {
        u32::try_from(unsafe { (self.0.model.rows_in_page)(self.0.model.ctx) }).ok()
    }
    fn padding(&mut self) -> i32 {
        self.0.padding.get()
    }
    fn cy_row(&mut self) -> i32 {
        self.0.cy_row.get()
    }
    fn wnd_width(&mut self) -> u32 {
        self.0.wnd_width.get()
    }
    fn set_initial_metrics(&mut self, cx_title: i32, cy_row: i32, padding: i32) {
        self.0.cx_title.set(cx_title);
        self.0.cy_row.set(cy_row);
        self.0.padding.set(padding);
    }
    fn sb_width(&mut self) -> i32 {
        self.0.sb_width()
    }
    fn font(&mut self) -> HFONT {
        self.0.font()
    }
    fn bk_brush(&mut self) -> HBRUSH {
        self.0.brush.get()
    }
    fn has_callback(&mut self) -> bool {
        unsafe { (self.0.model.has_callback)(self.0.model.ctx) != 0 }
    }
    fn invoke(&mut self, action: i32) {
        unsafe { (self.0.model.invoke)(self.0.model.ctx, action) }
    }
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT) {
        self.0.paint(dc, ps);
    }
    fn invalidate(&mut self) {
        let h = self.0.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = InvalidateRect(Some(h), None, false);
            }
        }
    }
    fn dpi_changed(&mut self, hwnd: HWND) {
        unsafe { (self.0.env.set_default_text_font)(hwnd.0) }
    }
    fn is_capture(&mut self) -> bool {
        self.0.capture_object.get()
    }
    fn tracking_mouse(&mut self) -> bool {
        self.0.tracking.get()
    }
    fn set_tracking_mouse(&mut self, on: bool) {
        self.0.tracking.set(on);
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
    fn has_scroll_bar(&mut self) -> bool {
        self.0.scroll_bar().is_some()
    }
    fn sb_hwnd(&mut self) -> HWND {
        self.0.scroll_bar().map_or(HWND::default(), |s| s.hwnd())
    }
    fn sb_client_rect(&mut self) -> Rect {
        let mut rc = RECT::default();
        if let Some(sb) = self.0.scroll_bar() {
            unsafe {
                let _ = GetClientRect(sb.hwnd(), &mut rc);
            }
        }
        Rect {
            left: rc.left,
            top: rc.top,
            right: rc.right,
            bottom: rc.bottom,
        }
    }
    fn sb_is_enabled(&mut self) -> bool {
        self.0.scroll_bar().is_some_and(|s| s.is_enabled())
    }
    fn sb_show(&mut self, show: bool) {
        if let Some(s) = self.0.scroll_bar() {
            s.show(show);
        }
    }
    fn sb_owner_moved(&mut self, _resized: bool) {
        if let Some(s) = self.0.scroll_bar() {
            s.owner_moved();
        }
    }
    fn sb_set_candidate_mouse_in(&mut self, inside: bool) {
        if let Some(s) = self.0.scroll_bar() {
            s.set_candidate_mouse_in(inside);
        }
    }
    fn sb_on_scroll_activity(&mut self) {
        if let Some(s) = self.0.scroll_bar() {
            s.on_scroll_activity();
        }
    }
    fn sb_shift_position(&mut self, position: i32) {
        if let Some(s) = self.0.scroll_bar() {
            s.shift_position(position, false);
        }
    }
    fn sb_mouse(&mut self, msg: u32, pt: Point) {
        use windows::Win32::UI::WindowsAndMessaging::{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE};
        if let Some(s) = self.0.scroll_bar() {
            match msg {
                WM_LBUTTONDOWN => s.lbutton_down(pt),
                WM_LBUTTONUP => s.lbutton_up(),
                WM_MOUSEMOVE => s.mouse_move(),
                _ => {}
            }
        }
    }
}

struct CandHandler(Rc<State>);

impl Handler for CandHandler {
    fn message(&self, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> Option<LRESULT> {
        self.0.hwnd.set(hwnd);
        Some(unsafe { candproc::window_proc(&mut Host(&self.0), hwnd, msg, wparam, lparam) })
    }
    /// `CBaseWindow` routes `WM_TIMER` to the timer object: this window, whose
    /// `_OnTimerID` handles its animation timer and forwards the rest to the scroll bar.
    fn timer(&self, _hwnd: HWND, id: usize) {
        if !self.0.timer_object.get() || id == DEFAULT_TIMER_ID {
            return; // no timer object, or _OnTimer (none on the candidate window)
        }
        if id == ANIMATION_TIMER_ID {
            self.0.end_timer(ANIMATION_TIMER_ID);
        } else if let Some(sb) = self.0.scroll_bar() {
            sb.timer(id);
        }
    }
}

// The scroll bar's side of the capture and timers (`sbwin::Parent`), on this window.
unsafe extern "C" fn sb_is_capture(ctx: *mut c_void) -> i32 {
    i32::from(unsafe { &*(ctx as *const State) }.capture_object.get())
}
unsafe extern "C" fn sb_start_capture(ctx: *mut c_void) {
    unsafe { &*(ctx as *const State) }.start_capture();
}
unsafe extern "C" fn sb_end_capture(ctx: *mut c_void) {
    unsafe { &*(ctx as *const State) }.end_capture();
}
unsafe extern "C" fn sb_start_timer(ctx: *mut c_void, elapse: u32, id: usize) {
    unsafe { &*(ctx as *const State) }.start_timer(elapse, id);
}
unsafe extern "C" fn sb_end_timer(ctx: *mut c_void, id: usize) {
    unsafe { &*(ctx as *const State) }.end_timer(id);
}

thread_local! {
    /// The DIME classes for the stay-on-top timer procedure (one set per process).
    static CLASSES: Cell<Classes> = Cell::new(Classes::default());
}

/// #135: keep the candidate above host topmost popups that open after it (Firefox's
/// autocomplete) by re-stamping `HWND_TOPMOST` while a visible non-DIME window is above it.
unsafe extern "system" fn stay_on_top(hwnd: HWND, _msg: u32, _id: usize, _time: u32) {
    unsafe {
        if !IsWindow(Some(hwnd)).as_bool() || !IsWindowVisible(hwnd).as_bool() {
            return;
        }
        let c = CLASSES.with(|c| c.get());
        let mut cur = GetWindow(hwnd, GW_HWNDPREV).unwrap_or_default();
        let mut depth = 0;
        while !cur.is_invalid() && depth < 20 {
            if IsWindowVisible(cur).as_bool() {
                let cls = GetClassWord(cur, GCW_ATOM.0);
                let ours = cls == c.candidate
                    || cls == c.candidate_shadow
                    || cls == c.scroll_bar
                    || cls == c.notify
                    || cls == c.notify_shadow;
                if !ours {
                    let mut rc = RECT::default();
                    if GetWindowRect(cur, &mut rc).is_ok() && rc.right - rc.left > 8 && rc.bottom - rc.top > 8 {
                        // A real non-DIME visible competitor: re-stamp ourselves.
                        let _ = SetWindowPos(
                            hwnd,
                            Some(HWND_TOPMOST),
                            0,
                            0,
                            0,
                            0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
                        );
                        return;
                    }
                }
            }
            cur = GetWindow(cur, GW_HWNDPREV).unwrap_or_default();
            depth += 1;
        }
    }
}

/// `CBaseWindow::GetWorkAreaFromPoint`: the monitor's work area, widened to the
/// monitor when the point is outside it horizontally (Store apps' snapped view).
pub fn work_area_from_point(pt: Point) -> Rect {
    unsafe {
        let monitor = MonitorFromPoint(POINT { x: pt.x, y: pt.y }, MONITOR_DEFAULTTONEAREST);
        if !monitor.is_invalid() {
            let mut mi = MONITORINFO {
                cbSize: std::mem::size_of::<MONITORINFO>() as u32,
                ..Default::default()
            };
            if GetMonitorInfoW(monitor, &mut mi).as_bool() {
                let mut wa = mi.rcWork;
                if !(pt.x < wa.right && pt.x > wa.left) {
                    if pt.x > wa.right {
                        wa.right = mi.rcMonitor.right;
                    }
                    if pt.x < wa.left {
                        wa.left = mi.rcMonitor.left;
                    }
                }
                return Rect {
                    left: wa.left,
                    top: wa.top,
                    right: wa.right,
                    bottom: wa.bottom,
                };
            }
        }
        let mut wa = RECT::default();
        let _ = SystemParametersInfoW(
            SPI_GETWORKAREA,
            0,
            Some(&mut wa as *mut RECT as *mut c_void),
            SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0),
        );
        Rect {
            left: wa.left,
            top: wa.top,
            right: wa.right,
            bottom: wa.bottom,
        }
    }
}

/// A candidate window owned by Rust.
pub struct CandidateWindow {
    window: Rc<win::Window>,
    state: Rc<State>,
}

impl CandidateWindow {
    /// `CCandidateWindow::_Create`: the popup (`WM_CREATE` measures the font),
    /// its shadow, its scroll bar, then `_ResizeWindow`.  `None` when the
    /// main window could not be created (a failed shadow / scroll bar is
    /// tolerated, as in C++).
    ///
    /// # Safety
    /// The class atoms must be registered by [`win::register_class`]; the
    /// model and environment callbacks must stay valid for the window's life.
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn create(
        hinstance: HINSTANCE,
        classes: Classes,
        parent: Option<HWND>,
        wnd_width: u32,
        env: Env,
        model: Model,
        colors: Colors,
        fill: u32,
    ) -> Option<CandidateWindow> {
        CLASSES.with(|c| c.set(classes));
        let state = Rc::new(State {
            hwnd: Cell::new(HWND::default()),
            x: Cell::new(-32768),
            y: Cell::new(-32768),
            cx_title: Cell::new(0),
            cy_row: Cell::new(30),
            padding: Cell::new(0),
            wnd_width: Cell::new(wnd_width),
            colors: Cell::new(colors),
            brush: Cell::new(unsafe { CreateSolidBrush(COLORREF(fill)) }),
            tracking: Cell::new(false),
            capture_object: Cell::new(false),
            timer_object: Cell::new(false),
            shadow: Cell::new(None),
            scroll_bar: Cell::new(None),
            env,
            classes,
            model,
        });
        let window = unsafe {
            win::create(
                hinstance,
                classes.candidate,
                WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                WS_POPUP | WS_CLIPCHILDREN,
                parent,
                0,
                0,
                Box::new(CandHandler(Rc::clone(&state))),
            )
        }?;
        state.hwnd.set(window.hwnd());

        let senv = shadow::Env {
            dpi_for_hwnd: env.dpi_for_hwnd,
            dark: env.dark,
        };
        // As _Create: a failed shadow skips the scroll bar and _ResizeWindow, a failed
        // scroll bar skips _ResizeWindow; the window itself is kept either way.
        let Some(s) = (unsafe {
            ShadowWindow::create(
                hinstance,
                classes.candidate_shadow,
                (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED).0,
                (WS_DISABLED | WS_POPUP).0,
                window.hwnd(),
                CORNER_RADIUS_BASE,
                senv,
            )
        }) else {
            return Some(CandidateWindow { window, state });
        };
        state.shadow.set(Some(Rc::new(s)));
        let parent_cb = sbwin::Parent {
            ctx: Rc::as_ptr(&state) as *mut c_void,
            hwnd: window.hwnd(),
            is_capture: sb_is_capture,
            start_capture: sb_start_capture,
            end_capture: sb_end_capture,
            start_timer: sb_start_timer,
            end_timer: sb_end_timer,
        };
        let sbenv = sbwin::Env {
            dpi_for_hwnd: env.dpi_for_hwnd,
            dark: env.dark,
            always_show: env.always_show,
        };
        let Some(sb) = (unsafe {
            ScrollBarWindow::create(
                hinstance,
                classes.scroll_bar,
                (WS_EX_TOPMOST | WS_EX_TOOLWINDOW).0,
                WS_CHILD.0,
                sbenv,
                parent_cb,
            )
        }) else {
            return Some(CandidateWindow { window, state });
        };
        state.scroll_bar.set(Some(Rc::new(sb)));
        state.resize_window();
        Some(CandidateWindow { window, state })
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
        Rect {
            left: rc.left,
            top: rc.top,
            right: rc.right,
            bottom: rc.bottom,
        }
    }
    pub fn client_rect(&self) -> Rect {
        self.st().client_rect()
    }
    /// `_GetWidth`.
    pub fn width(&self) -> i32 {
        let st = self.st();
        candwnd::window_size(st.cx_title.get(), st.cy_row.get(), st.padding.get(), 0, st.sb_width()).0
    }
    pub fn resize_window(&self) {
        self.st().resize_window();
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
            use windows::Win32::UI::WindowsAndMessaging::SWP_NOZORDER;
            unsafe {
                let _ = SetWindowPos(h, None, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
            }
        }
    }
    /// `_InvalidateRect`.
    pub fn invalidate(&self) {
        Host(self.st()).invalidate();
    }
    /// `_SetCandStringLength`.
    pub fn set_cand_string_length(&self, wnd_width: u32) {
        self.st().wnd_width.set(wnd_width);
    }
    pub fn set_number_color(&self, color: u32, bk: u32) {
        let st = self.st();
        let mut c = st.colors.get();
        c.number = adjust_text_color(color, bk);
        c.number_bk = bk;
        st.colors.set(c);
    }
    pub fn set_text_color(&self, color: u32, bk: u32) {
        let st = self.st();
        let mut c = st.colors.get();
        c.text = adjust_text_color(color, bk);
        c.bk = bk;
        st.colors.set(c);
    }
    pub fn set_selected_text_color(&self, color: u32, bk: u32) {
        let st = self.st();
        let mut c = st.colors.get();
        c.selected_text = adjust_text_color(color, bk);
        c.selected_bk = bk;
        st.colors.set(c);
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
    /// `CBaseWindow::_GetWindowExtent`: where to put the window around the text.
    pub fn window_extent(&self, text: &Rect, window: &Rect) -> Point {
        let work = work_area_from_point(layout::work_area_probe(text, window));
        layout::fit_point_around_text_extent(text, &work, window)
    }

    /// `_Show`.
    pub fn show(&self, on: bool) {
        let st = self.st();
        let h = st.hwnd.get();
        if on {
            // A 0x0 popup gets no WM_PAINT: size it first.
            let rc = st.client_rect();
            if rc.width() == 0 || rc.height() == 0 {
                st.resize_window();
            }
        }
        if let Some(s) = st.shadow() {
            s.show(on);
        }
        if let Some(sb) = st.scroll_bar() {
            sb.show(on && sb.is_enabled());
        }
        win::base_show(h, on);
        if on {
            // Re-apply the rounded region after the window is visible (Win11 DWM drops it on hidden windows).
            let rc = st.client_rect();
            let (cx, cy) = (rc.width(), rc.height());
            if cx > 0 && cy > 0 && !h.is_invalid() {
                let radius = candwnd::scale(CORNER_RADIUS_BASE, st.dpi());
                unsafe {
                    let rgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, radius, radius);
                    SetWindowRgn(h, Some(rgn), true);
                }
            }
        }
        unsafe {
            if on && !st.capture_object.get() {
                SetCapture(h);
            }
            if !on && st.capture_object.get() {
                let _ = ReleaseCapture();
            }
        }
        if !h.is_invalid() {
            unsafe {
                if on {
                    SetTimer(Some(h), STAY_ON_TOP_TIMER_ID, STAY_ON_TOP_TICK_MS, Some(stay_on_top));
                } else {
                    let _ = KillTimer(Some(h), STAY_ON_TOP_TIMER_ID);
                }
            }
        }
    }

    /// `_SetScrollInfo(nMax, nPage)`.
    pub fn set_scroll_info(&self, max: i32, page: i32) {
        if let Some(sb) = self.st().scroll_bar() {
            sb.set_scroll_info(candwnd::ScrollInfo { max, page, pos: 0 });
        }
    }
    /// The scroll bar side of `_MoveSelection` / `_SetSelection` / `_MovePage` (the model moved already).
    pub fn sb_shift_line(&self, lines: i32, notify: bool) {
        if let Some(sb) = self.st().scroll_bar() {
            sb.shift_line(lines, notify);
        }
    }
    pub fn sb_shift_page(&self, pages: i32, notify: bool) {
        if let Some(sb) = self.st().scroll_bar() {
            sb.shift_page(pages, notify);
        }
    }
    pub fn sb_shift_position(&self, pos: i32, notify: bool) {
        if let Some(sb) = self.st().scroll_bar() {
            sb.shift_position(pos, notify);
        }
    }
    pub fn scroll_bar(&self) -> Option<Rc<ScrollBarWindow>> {
        self.st().scroll_bar()
    }
    pub fn shadow(&self) -> Option<Rc<ShadowWindow>> {
        self.st().shadow()
    }
    /// The layout members (tests): `(cx_title, cy_row, padding, wnd_width)`.
    pub fn layout_state(&self) -> (i32, i32, i32, u32) {
        let st = self.st();
        (st.cx_title.get(), st.cy_row.get(), st.padding.get(), st.wnd_width.get())
    }
    pub fn set_layout_state(&self, cx_title: i32, cy_row: i32, padding: i32, wnd_width: u32) {
        let st = self.st();
        st.cx_title.set(cx_title);
        st.cy_row.set(cy_row);
        st.padding.set(padding);
        st.wnd_width.set(wnd_width);
    }
    pub fn is_tracking_mouse(&self) -> bool {
        self.st().tracking.get()
    }
    /// `_OnPaint` into `dc` with `rcPaint` = `paint` (tests).
    ///
    /// # Safety
    /// `dc` must be a valid device context.
    pub unsafe fn paint(&self, dc: HDC, paint: Rect) {
        let ps = PAINTSTRUCT {
            rcPaint: RECT {
                left: paint.left,
                top: paint.top,
                right: paint.right,
                bottom: paint.bottom,
            },
            ..Default::default()
        };
        self.st().paint(dc, &ps);
    }
    /// The class atoms this window was created with.
    pub fn classes(&self) -> Classes {
        self.state.classes
    }
    /// The window currently holding the mouse capture is this one.
    pub fn has_capture(&self) -> bool {
        (unsafe { GetCapture() }) == self.window.hwnd()
    }
}

impl Drop for CandidateWindow {
    fn drop(&mut self) {
        // ~CCandidateWindow: brush, shadow, scroll bar (after UIPresenter's _Destroy).
        self.window.destroy();
        drop(self.state.shadow.take());
        drop(self.state.scroll_bar.take());
        let b = self.state.brush.replace(HBRUSH::default());
        if !b.is_invalid() {
            unsafe {
                let _ = DeleteObject(b.into());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn text_color_adjusts_when_too_close() {
        assert_eq!(adjust_text_color(0x000000, 0xFFFFFF), 0x000000);
        assert_eq!(adjust_text_color(0x101010, 0x000000), 0xEFEFEF);
        assert!(is_too_similar(0x102030, 0x102030));
        assert!(!is_too_similar(0x01000000, 0x01000000), "not RGB");
        assert!(is_too_similar(0x000000, 0x00004F), "79 < 80");
        assert!(!is_too_similar(0x000000, 0x000050), "80");
    }
}
