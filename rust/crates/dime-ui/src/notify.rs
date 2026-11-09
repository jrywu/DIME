//! The notify window (port of `CNotifyWindow`): the 中/英, 全/半形, reverse
//! lookup and special code hints.  Drawing (`_OnPaint` / `_DrawText`), the
//! window procedure, the mouse handlers, `_Show` with its delay-show and
//! auto-hide timers, and `_OnTimerID`.  The state is reached through
//! [`NotifyHost`], implemented by the Rust-owned window (`notifywin`); the
//! callback (switch 中/英, show after the delay) is always the last call of a
//! handler.

use crate::candwnd;
use crate::layout::{Point, Rect};
use windows::core::PCWSTR;
use windows::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, POINT, RECT, SIZE, WPARAM};
use windows::Win32::Graphics::Gdi::{
    BeginPaint, CreateRoundRectRgn, EndPaint, ExtTextOutW, FillRect, GetDC, GetTextExtentPoint32W, MapWindowPoints,
    ReleaseDC, SelectObject, SetBkColor, SetBkMode, SetTextColor, SetWindowRgn, ETO_OPAQUE, HBRUSH, HDC, HFONT,
    HGDIOBJ, PAINTSTRUCT, TRANSPARENT,
};
use windows::Win32::UI::WindowsAndMessaging::{
    DefWindowProcW, GetClientRect, GetCursorPos, LoadCursorW, SetCursor, SetWindowPos, IDC_ARROW, IDC_HAND,
    MA_NOACTIVATE, SWP_HIDEWINDOW, SWP_NOACTIVATE, SWP_NOSIZE, SWP_NOZORDER, WINDOWPOS, WM_CREATE, WM_DESTROY,
    WM_DPICHANGED, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MOUSEACTIVATE, WM_MOUSEMOVE,
    WM_PAINT, WM_RBUTTONDOWN, WM_RBUTTONUP, WM_SETCURSOR, WM_SHOWWINDOW, WM_WINDOWPOSCHANGED, WM_WINDOWPOSCHANGING,
};

const WM_POINTERACTIVATE: u32 = 0x024B;
const PA_NOACTIVATE: isize = 3;

/// `UIConstants.h`.
pub const DELAY_SHOW_TIMER_ID: usize = 39775;
pub const TIME_TO_HIDE_TIMER_ID: usize = 39776;
/// The off-screen position `WM_CREATE` parks the window at.
pub const OFF_SCREEN: i32 = -32768;

/// `NOTIFY_WND`.
pub mod action {
    pub const SWITCH_CHN_ENG: i32 = 0;
    pub const SHOW_NOTIFY: i32 = 1;
}
/// `NOTIFY_TYPE::NOTIFY_CHN_ENG`: the clickable 中/英 hint.
pub const NOTIFY_CHN_ENG: i32 = 0;

fn rect(r: &Rect) -> RECT {
    RECT {
        left: r.left,
        top: r.top,
        right: r.right,
        bottom: r.bottom,
    }
}

/// The input of [`paint`].
#[derive(Debug, Clone, Copy)]
pub struct PaintInput<'a> {
    pub font: HFONT,
    /// `_brshBkColor` (may be null: `FillRect` then does nothing).
    pub bk_brush: HBRUSH,
    /// `pps->rcPaint`, `None` without a `PAINTSTRUCT`.
    pub paint: Option<Rect>,
    pub text: &'a [u16],
    pub text_color: u32,
    pub bk_color: u32,
    pub cx_title: i32,
}

/// What `_DrawText` leaves behind: the new `_cxTitle` / `_cyTitle`, and
/// whether the width changed (`_ResizeWindow` follows).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct PaintOutput {
    pub cx_title: i32,
    pub cy_title: i32,
    pub resize: bool,
}

/// `_OnPaint` with `_DrawText`.  `None` when nothing was measured (no
/// `PAINTSTRUCT`, or an empty text, where the C++ original divides by zero).
///
/// # Safety
/// `dc` must be a valid device context.
pub unsafe fn paint(dc: HDC, input: &PaintInput) -> Option<PaintOutput> {
    let mut out = None;
    unsafe {
        SetBkMode(dc, TRANSPARENT);
        let old = SelectObject(dc, input.font.into());
        if let Some(p) = &input.paint {
            FillRect(dc, &rect(p), input.bk_brush);
            out = draw_text(dc, input, p);
        }
        SelectObject(dc, old);
    }
    out
}

/// `_DrawText`.
unsafe fn draw_text(dc: HDC, input: &PaintInput, prc: &Rect) -> Option<PaintOutput> {
    let len = i32::try_from(input.text.len()).unwrap_or(i32::MAX);
    if len == 0 {
        return None;
    }
    unsafe {
        let old = SelectObject(dc, input.font.into());
        let mut size = SIZE::default();
        let _ = GetTextExtentPoint32W(dc, input.text, &mut size);
        let cx = size.cx.wrapping_add(size.cx / len);
        let cy = size.cy.wrapping_mul(3) / 2;
        let rc = RECT {
            left: prc.left,
            top: prc.top,
            right: prc.left.wrapping_add(cx),
            bottom: prc.top.wrapping_add(cy),
        };
        SetTextColor(dc, COLORREF(input.text_color));
        SetBkColor(dc, COLORREF(input.bk_color));
        let _ = ExtTextOutW(
            dc,
            size.cx / len / 2,
            cy / 5,
            ETO_OPAQUE,
            Some(&rc),
            PCWSTR(input.text.as_ptr()),
            input.text.len() as u32,
            None,
        );
        SelectObject(dc, old);
        Some(PaintOutput {
            cx_title: cx,
            cy_title: cy,
            resize: input.cx_title != cx,
        })
    }
}

/// What the notify window logic needs from the C++ object.
pub trait NotifyHost {
    fn hwnd(&mut self) -> HWND;
    fn text(&mut self) -> Vec<u16>;
    fn font(&mut self) -> HFONT;
    fn notify_type(&mut self) -> i32;
    /// `_TextMetric` (never filled: all zero) — `(tmMaxCharWidth, tmAveCharWidth, tmHeight)`.
    fn text_metric(&mut self) -> (i32, i32, i32);
    fn set_title_size(&mut self, cx: i32, cy: i32);
    fn set_position(&mut self, x: i32, y: i32);
    fn set_show_times(&mut self, delay_show: u32, time_to_hide: u32);
    fn time_to_hide(&mut self) -> u32;
    fn corner_radius_base(&mut self) -> i32;
    fn dpi(&mut self) -> u32;
    /// The callback (`NOTIFY_WND` action); it may destroy the window.
    fn invoke(&mut self, action: i32, wparam: usize, lparam: isize);
    /// `_OnPaint`.
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT);
    fn dpi_changed(&mut self, hwnd: HWND);
    /// `CBaseWindow::_Show`.
    fn base_show(&mut self, on: bool);
    fn is_capture(&mut self) -> bool;
    fn end_capture(&mut self);
    fn is_timer(&mut self) -> bool;
    fn start_timer(&mut self, elapse: u32, id: usize);
    fn end_timer(&mut self, id: usize);
    fn has_shadow(&mut self) -> bool;
    fn shadow_hwnd(&mut self) -> HWND;
    fn shadow_show(&mut self, show: i32);
    fn shadow_owner_moved(&mut self, resized: bool);
    fn delete_shadow(&mut self);
}

fn client_rect(hwnd: HWND) -> Rect {
    let mut rc = RECT::default();
    unsafe {
        let _ = GetClientRect(hwnd, &mut rc);
    }
    Rect {
        left: rc.left,
        top: rc.top,
        right: rc.right,
        bottom: rc.bottom,
    }
}

fn set_cursor(id: PCWSTR) {
    unsafe {
        SetCursor(LoadCursorW(None, id).ok());
    }
}

/// `_Show(isShowWnd, delayShow, timeToHide)`.
pub fn show(host: &mut dyn NotifyHost, on: bool, delay_show: u32, time_to_hide: u32) {
    host.set_show_times(delay_show, time_to_hide);
    if host.is_timer() {
        host.end_timer(DELAY_SHOW_TIMER_ID);
        host.end_timer(TIME_TO_HIDE_TIMER_ID);
    }
    if !on || delay_show == 0 {
        // ignore delayShow if isShowWnd is FALSE
        if host.has_shadow() {
            host.shadow_show(i32::from(on));
        }
        host.base_show(on);
        // Re-apply rounded region AFTER window is visible (Win11 DWM discards it on hidden windows).
        if on {
            let hwnd = host.hwnd();
            let rc = client_rect(hwnd);
            let (cx, cy) = (rc.width(), rc.height());
            if cx > 0 && cy > 0 {
                let radius = candwnd::scale(host.corner_radius_base(), host.dpi());
                unsafe {
                    let rgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, radius, radius);
                    SetWindowRgn(hwnd, Some(rgn), true);
                }
            }
        }
    }
    if on {
        if delay_show > 0 {
            host.start_timer(delay_show, DELAY_SHOW_TIMER_ID); // show the notify after the delay
        } else if time_to_hide > 0 {
            host.start_timer(time_to_hide, TIME_TO_HIDE_TIMER_ID);
        }
    }
    if delay_show == 0 && !(on && host.notify_type() == NOTIFY_CHN_ENG) && host.is_capture() {
        host.end_capture();
    }
}

/// `_OnTimerID`: show after the delay, hide after the time.
pub fn on_timer_id(host: &mut dyn NotifyHost, id: usize) {
    if id == DELAY_SHOW_TIMER_ID {
        host.end_timer(DELAY_SHOW_TIMER_ID);
        let (tth, ty) = (host.time_to_hide(), host.notify_type());
        host.invoke(action::SHOW_NOTIFY, tth as usize, ty as isize);
    } else if id == TIME_TO_HIDE_TIMER_ID {
        show(host, false, 0, 0);
    }
}

fn on_lbutton_down(host: &mut dyn NotifyHost, hwnd: HWND, pt: Point) {
    if candwnd::contains(&client_rect(hwnd), pt) {
        if host.notify_type() == NOTIFY_CHN_ENG {
            host.invoke(action::SWITCH_CHN_ENG, 0, 0);
        }
    } else {
        // hide the notify if click outside the notify window.
        show(host, false, 0, 0);
    }
}

fn on_mouse_move(host: &mut dyn NotifyHost, hwnd: HWND, pt: Point) {
    if candwnd::contains(&client_rect(hwnd), pt) && host.notify_type() == NOTIFY_CHN_ENG {
        set_cursor(IDC_HAND);
    } else {
        set_cursor(IDC_ARROW);
    }
}

fn handle_mouse(host: &mut dyn NotifyHost, hwnd: HWND, msg: u32, pt: Point) {
    match msg {
        WM_MOUSEMOVE => on_mouse_move(host, hwnd, pt),
        WM_LBUTTONDOWN => on_lbutton_down(host, hwnd, pt),
        WM_RBUTTONDOWN => show(host, false, 0, 0), // hide on a right click
        _ => {}                                    // WM_LBUTTONUP: CBaseWindow::_OnLButtonUp does nothing
    }
}

/// `_WindowProcCallback`.
///
/// # Safety
/// `hwnd` is the notify window; `lparam` follows `msg`.
pub unsafe fn window_proc(host: &mut dyn NotifyHost, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    let def = || unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) };
    match msg {
        WM_CREATE => {
            unsafe {
                let dc = GetDC(Some(hwnd));
                if !dc.is_invalid() {
                    let old: HGDIOBJ = SelectObject(dc, host.font().into());
                    let text = host.text();
                    if !text.is_empty() {
                        let mut size = SIZE::default();
                        let _ = GetTextExtentPoint32W(dc, &text, &mut size);
                        let len = i32::try_from(text.len()).unwrap_or(i32::MAX);
                        host.set_title_size(size.cx.wrapping_add(size.cx / len), size.cy.wrapping_mul(3) / 2);
                    } else {
                        let (max_w, ave_w, height) = host.text_metric();
                        host.set_title_size(
                            max_w.wrapping_mul(4).wrapping_add(ave_w.wrapping_mul(5) / 2),
                            height.wrapping_mul(2),
                        );
                    }
                    host.set_position(OFF_SCREEN, OFF_SCREEN); // out of screen initially
                    SelectObject(dc, old);
                    ReleaseDC(Some(hwnd), dc);
                }
            }
            LRESULT(0)
        }
        WM_DESTROY => {
            host.delete_shadow();
            LRESULT(0)
        }
        WM_WINDOWPOSCHANGED => {
            if let Some(wp) = unsafe { (lparam.0 as *const WINDOWPOS).as_ref() } {
                if host.has_shadow() {
                    host.shadow_owner_moved((wp.flags.0 & SWP_NOSIZE.0) == 0);
                }
                crate::candproc::fire_light_dismiss(hwnd, wp);
            }
            def()
        }
        WM_WINDOWPOSCHANGING => {
            if let Some(wp) = unsafe { (lparam.0 as *mut WINDOWPOS).as_mut() } {
                if host.has_shadow() {
                    if (wp.flags.0 & SWP_HIDEWINDOW.0) != 0 {
                        host.shadow_show(0);
                    }
                    // don't go behind of shadow
                    if (wp.flags.0 & SWP_NOZORDER.0) == 0 && wp.hwndInsertAfter == host.shadow_hwnd() {
                        wp.flags |= SWP_NOZORDER;
                    }
                    host.shadow_owner_moved((wp.flags.0 & SWP_NOSIZE.0) == 0);
                }
            }
            def()
        }
        WM_SHOWWINDOW => {
            if host.has_shadow() {
                host.shadow_show(wparam.0 as i32);
            }
            def()
        }
        WM_PAINT => {
            unsafe {
                let mut ps = PAINTSTRUCT::default();
                let dc = BeginPaint(hwnd, &mut ps);
                host.paint(dc, &mut ps);
                let _ = EndPaint(hwnd, &ps);
                ReleaseDC(Some(hwnd), dc); // as in C++ (after EndPaint)
            }
            LRESULT(0)
        }
        WM_SETCURSOR => {
            let mut cursor = POINT::default();
            unsafe {
                let _ = GetCursorPos(&mut cursor);
                let mut pts = [cursor];
                MapWindowPoints(None, Some(hwnd), &mut pts);
                cursor = pts[0];
            }
            let mouse = ((lparam.0 as usize >> 16) & 0xFFFF) as u32;
            handle_mouse(
                host,
                hwnd,
                mouse,
                Point {
                    x: cursor.x,
                    y: cursor.y,
                },
            );
            LRESULT(1)
        }
        WM_MOUSEMOVE | WM_LBUTTONDOWN | WM_MBUTTONDOWN | WM_RBUTTONDOWN | WM_LBUTTONUP | WM_MBUTTONUP
        | WM_RBUTTONUP => {
            let v = lparam.0 as u32;
            let pt = Point {
                x: (v & 0xFFFF) as u16 as i16 as i32,
                y: (v >> 16) as u16 as i16 as i32,
            };
            handle_mouse(host, hwnd, msg, pt);
            LRESULT(0)
        }
        WM_MOUSEACTIVATE => {
            let mouse = ((lparam.0 as usize >> 16) & 0xFFFF) as u32;
            if mouse == WM_LBUTTONDOWN || mouse == WM_RBUTTONDOWN || mouse == WM_MBUTTONDOWN {
                LRESULT(MA_NOACTIVATE as isize)
            } else {
                def()
            }
        }
        WM_POINTERACTIVATE => LRESULT(PA_NOACTIVATE),
        WM_DPICHANGED => {
            host.dpi_changed(hwnd);
            if let Some(rc) = unsafe { (lparam.0 as *const RECT).as_ref() } {
                unsafe {
                    let _ = SetWindowPos(
                        hwnd,
                        None,
                        rc.left,
                        rc.top,
                        rc.right - rc.left,
                        rc.bottom - rc.top,
                        SWP_NOZORDER | SWP_NOACTIVATE,
                    );
                }
            }
            LRESULT(0)
        }
        _ => def(),
    }
}
