//! Events of the candidate window's scroll bar (port of `CScrollBarWindow`'s
//! `_OnLButtonDown`, `_OnLButtonUp`, `_OnMouseMove`, `_OnTimerID`,
//! `_OnScrollActivity`, `_SetScrollInfo`, `_SetCurPos`, `_Shift*`,
//! `_SetCandidateMouseIn`, `_OnOwnerWndMoved` / `_AdjustWindowPos` and
//! `_WindowProcCallback`).
//!
//! The state stays in the C++ object and is read and written through
//! [`SbHost`]: `_NotifyCommand` sends `WM_VSCROLL` to the candidate window,
//! whose handler calls back into the scroll bar (`_ShiftPosition`), so no
//! Rust borrow may live across a host call.

use crate::candwnd::{self, hit, ScrollInfo};
use crate::layout::{Point, Rect};
use windows::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, POINT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    BeginPaint, BitBlt, CreateCompatibleBitmap, CreateCompatibleDC, DeleteDC, DeleteObject, EndPaint, MapWindowPoints,
    SelectObject, HDC, PAINTSTRUCT, SRCCOPY,
};
use windows::Win32::UI::Input::KeyboardAndMouse::{TrackMouseEvent, TME_LEAVE, TRACKMOUSEEVENT};
use windows::Win32::UI::WindowsAndMessaging::{
    DefWindowProcW, GetClientRect, GetCursorPos, GetWindowRect, IsWindow, IsWindowVisible, KillTimer,
    SetLayeredWindowAttributes, SetWindowPos, LWA_ALPHA, LWA_COLORKEY, SB_LINEDOWN, SB_LINEUP, SB_PAGEDOWN, SB_PAGEUP,
    SB_THUMBPOSITION, SB_THUMBTRACK, SWP_NOACTIVATE, SWP_NOOWNERZORDER, WM_ERASEBKGND, WM_PAINT, WM_TIMER, WM_VSCROLL,
};

const WM_MOUSELEAVE: u32 = 0x02A3;

/// `ScrollBarWindow.h` / `UIConstants.h`.
pub const BTNREPEAT_TIMER_ID: usize = 39779;
pub const BTNREPEAT_DELAY_MS: u32 = 400;
pub const BTNREPEAT_SPEED_MS: u32 = 50;
pub const FADE_TIMER_ID: usize = 39780;
pub const FADE_ALPHA_VISIBLE: u8 = 255;
pub const FADE_ALPHA_STEP: u8 = 20;
pub const COLLAPSE_TIMER_ID: usize = 39777;
pub const COLLAPSE_DELAY_MS: u32 = 2000;
/// `dwSB == -1`: `_SetCurPos` does not notify.
pub const NO_NOTIFY: i32 = -1;

/// What the scroll bar logic needs from the C++ object.
pub trait SbHost {
    fn hwnd(&mut self) -> HWND;
    /// The candidate window (`_GetParent()->_GetWnd()`), if any.
    fn parent_hwnd(&mut self) -> Option<HWND>;
    /// `_GetClientRect`.
    fn client_rect(&mut self) -> Rect;
    fn dpi(&mut self) -> u32;
    fn hover_width(&mut self) -> i32;

    // The state (CScrollBarWindow members).
    fn scroll_info(&mut self) -> ScrollInfo;
    fn set_scroll_info(&mut self, si: ScrollInfo);
    fn dragging(&mut self) -> Option<(i32, i32)>;
    /// `Some((start_y, start_pos))` starts a drag, `None` ends it.
    fn set_dragging(&mut self, drag: Option<(i32, i32)>);
    fn alpha(&mut self) -> u8;
    fn set_alpha(&mut self, alpha: u8);
    fn fading(&mut self) -> bool;
    fn set_fading(&mut self, on: bool);
    fn hovered(&mut self) -> bool;
    fn set_hovered(&mut self, on: bool);
    fn tracking_mouse(&mut self) -> bool;
    fn set_tracking_mouse(&mut self, on: bool);
    fn held_button(&mut self) -> i32;
    fn set_held_button(&mut self, button: i32);
    fn repeat_started(&mut self) -> bool;
    fn set_repeat_started(&mut self, on: bool);

    // CBaseWindow.
    fn is_enabled(&mut self) -> bool;
    /// `CBaseWindow::_Enable`.
    fn base_enable(&mut self, on: bool);
    fn is_visible(&mut self) -> bool;
    /// `CBaseWindow::_Show`.
    fn base_show(&mut self, on: bool);
    fn invalidate(&mut self);
    fn is_capture(&mut self) -> bool;
    fn start_capture(&mut self);
    fn end_capture(&mut self);
    fn start_timer(&mut self, elapse: u32, id: usize);
    fn end_timer(&mut self, id: usize);
    /// `_NotifyCommand(WM_VSCROLL, code, pos)`: re-enters the scroll bar.
    fn notify_command(&mut self, msg: u32, code: i32, pos: i32);
    /// The virtual `_ShiftPage` (the C++ object's own dispatch).
    fn shift_page(&mut self, pages: i32, notify: bool);
    /// `_OnPaint`.
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT);
}

/// `_Show`: only when the visibility changes.
pub fn show(host: &mut dyn SbHost, on: bool) {
    if host.is_visible() != on {
        host.base_show(on);
    }
}

/// `_Enable`: only when it changes.
pub fn enable(host: &mut dyn SbHost, on: bool) {
    if host.is_enabled() != on {
        host.base_enable(on);
    }
}

/// `_SetCurPos`: clamp, enable, repaint, notify the candidate window.
pub fn set_cur_pos(host: &mut dyn SbHost, pos: i32, code: i32) {
    let mut si = host.scroll_info();
    let pos = candwnd::clamp_pos(pos, si.max);
    si.pos = pos;
    host.set_scroll_info(si);

    let enabled = si.max > si.page;
    enable(host, enabled);
    if enabled && host.is_visible() {
        host.invalidate();
    }
    if (host.is_capture() && code != NO_NOTIFY) || code == SB_THUMBPOSITION.0 || code == SB_THUMBTRACK.0 {
        host.notify_command(WM_VSCROLL, code, pos);
    }
}

/// `_ShiftLine`.
pub fn shift_line(host: &mut dyn SbHost, lines: i32, notify: bool) {
    let code = if lines > 0 {
        SB_LINEDOWN.0
    } else if lines < 0 {
        SB_LINEUP.0
    } else {
        0
    };
    let pos = host.scroll_info().pos.wrapping_add(lines);
    set_cur_pos(host, pos, if notify { code } else { NO_NOTIFY });
}

/// `_ShiftPage`.
pub fn shift_page(host: &mut dyn SbHost, pages: i32, notify: bool) {
    let code = if pages > 0 {
        SB_PAGEDOWN.0
    } else if pages < 0 {
        SB_PAGEUP.0
    } else {
        0
    };
    let si = host.scroll_info();
    let pos = si.pos.wrapping_add(si.page.wrapping_mul(pages));
    set_cur_pos(host, pos, if notify { code } else { NO_NOTIFY });
}

/// `_ShiftPosition`.
pub fn shift_position(host: &mut dyn SbHost, pos: i32, notify: bool) {
    set_cur_pos(host, pos, if notify { SB_THUMBPOSITION.0 } else { NO_NOTIFY });
}

/// `_SetScrollInfo`.
pub fn set_scroll_info(host: &mut dyn SbHost, si: ScrollInfo) {
    host.set_scroll_info(si);
    let enabled = si.max > si.page;
    set_cur_pos(host, si.pos, NO_NOTIFY);
    enable(host, enabled);
    if enabled {
        on_scroll_activity(host); // show thin line immediately
    } else {
        show(host, false); // single-page: hide completely
    }
}

fn set_layered_alpha(hwnd: HWND, alpha: u8) {
    unsafe {
        let _ = SetLayeredWindowAttributes(
            hwnd,
            COLORREF(crate::sbpaint::COLORKEY),
            alpha,
            LWA_COLORKEY | LWA_ALPHA,
        );
    }
}

/// `_OnScrollActivity`: show the bar at full alpha.
pub fn on_scroll_activity(host: &mut dyn SbHost) {
    // Don't show scrollbar for single-page candidate lists
    if !host.is_enabled() {
        return;
    }
    // Kill any lingering fade timer
    if host.fading() {
        unsafe {
            let _ = KillTimer(Some(host.hwnd()), FADE_TIMER_ID);
        }
        host.set_fading(false);
    }
    // Show the window and ensure it is at full alpha
    if !host.is_visible() {
        show(host, true);
    }
    if host.alpha() < FADE_ALPHA_VISIBLE {
        host.set_alpha(FADE_ALPHA_VISIBLE);
        let hwnd = host.hwnd();
        if !hwnd.is_invalid() {
            set_layered_alpha(hwnd, FADE_ALPHA_VISIBLE);
        }
    }
}

/// `_SetCandidateMouseIn`: expand now, or collapse after a delay.
pub fn set_candidate_mouse_in(host: &mut dyn SbHost, inside: bool) {
    if inside {
        // Mouse entered scrollbar column: show full scrollbar immediately.
        host.end_timer(COLLAPSE_TIMER_ID); // cancel any pending collapse
        if !host.hovered() {
            host.set_hovered(true);
            host.invalidate();
        }
    } else {
        // Mouse left scrollbar column: schedule collapse after 2s delay.
        host.start_timer(COLLAPSE_DELAY_MS, COLLAPSE_TIMER_ID);
    }
}

/// `_OnLButtonDown`.
pub fn on_lbutton_down(host: &mut dyn SbHost, pt: Point) {
    host.start_capture();
    let (client, dpi, si) = (host.client_rect(), host.dpi(), host.scroll_info());
    match candwnd::hit_test(&client, dpi, &si, pt) {
        hit::BTN_UP => {
            host.notify_command(WM_VSCROLL, SB_PAGEUP.0, 0);
            host.shift_page(-1, false);
            host.set_held_button(hit::BTN_UP);
            host.set_repeat_started(false);
            host.start_timer(BTNREPEAT_DELAY_MS, BTNREPEAT_TIMER_ID);
        }
        hit::BTN_DOWN => {
            host.notify_command(WM_VSCROLL, SB_PAGEDOWN.0, 0);
            host.shift_page(1, false);
            host.set_held_button(hit::BTN_DOWN);
            host.set_repeat_started(false);
            host.start_timer(BTNREPEAT_DELAY_MS, BTNREPEAT_TIMER_ID);
        }
        hit::THUMB => {
            let pos = host.scroll_info().pos;
            host.set_dragging(Some((pt.y, pos)));
        }
        hit::TRACK_ABOVE => host.shift_page(-1, true),
        hit::TRACK_BELOW => host.shift_page(1, true),
        _ => {}
    }
}

/// `_OnLButtonUp`: end a drag or a held button.
pub fn on_lbutton_up(host: &mut dyn SbHost) {
    if host.dragging().is_some() {
        host.set_dragging(None);
        let pos = host.scroll_info().pos;
        set_cur_pos(host, pos, SB_THUMBPOSITION.0);
    }
    if host.held_button() != hit::NONE {
        host.end_timer(BTNREPEAT_TIMER_ID);
        host.set_held_button(hit::NONE);
        host.set_repeat_started(false);
    }
    if host.is_capture() {
        host.end_capture();
    }
    host.invalidate();
}

/// `_OnMouseMove`: hover, and the thumb drag.
pub fn on_mouse_move(host: &mut dyn SbHost) {
    // Register for WM_MOUSELEAVE if not already tracking
    if !host.tracking_mouse() {
        let mut tme = TRACKMOUSEEVENT {
            cbSize: std::mem::size_of::<TRACKMOUSEEVENT>() as u32,
            dwFlags: TME_LEAVE,
            hwndTrack: host.hwnd(),
            dwHoverTime: 0,
        };
        unsafe {
            let _ = TrackMouseEvent(&mut tme);
        }
        host.set_tracking_mouse(true);
    }
    if !host.hovered() {
        host.set_hovered(true);
        on_scroll_activity(host);
        host.invalidate();
    }
    if let Some((start_y, start_pos)) = host.dragging() {
        let (client, dpi, si) = (host.client_rect(), host.dpi(), host.scroll_info());
        let p = candwnd::parts(&client, dpi, &si);
        if p.track.height().wrapping_sub(p.thumb.height()) > 0 {
            // Get cursor in scrollbar client coords
            let mut cursor = POINT::default();
            let hwnd = host.hwnd();
            unsafe {
                let _ = GetCursorPos(&mut cursor);
                let mut pts = [cursor];
                MapWindowPoints(None, Some(hwnd), &mut pts);
                cursor = pts[0];
            }
            let delta = cursor.y.wrapping_sub(start_y);
            if let Some(pos) = candwnd::drag_position(&client, dpi, &si, start_pos, delta) {
                set_cur_pos(host, pos, SB_THUMBTRACK.0);
            }
        }
    }
}

/// `_OnTimerID`: the held-button repeat and the hover collapse.
pub fn on_timer_id(host: &mut dyn SbHost, id: usize) {
    if id == BTNREPEAT_TIMER_ID {
        if !host.repeat_started() {
            host.set_repeat_started(true);
            host.end_timer(BTNREPEAT_TIMER_ID);
            host.start_timer(BTNREPEAT_SPEED_MS, BTNREPEAT_TIMER_ID);
        }
        match host.held_button() {
            hit::BTN_UP => {
                host.notify_command(WM_VSCROLL, SB_PAGEUP.0, 0);
                host.shift_page(-1, false);
            }
            hit::BTN_DOWN => {
                host.notify_command(WM_VSCROLL, SB_PAGEDOWN.0, 0);
                host.shift_page(1, false);
            }
            _ => {}
        }
    } else if id == COLLAPSE_TIMER_ID {
        host.end_timer(COLLAPSE_TIMER_ID);
        if host.hovered() {
            host.set_hovered(false);
            host.invalidate();
        }
    }
}

/// `_OnOwnerWndMoved` / `_AdjustWindowPos`: follow the candidate window.
pub fn on_owner_moved(host: &mut dyn SbHost) {
    let hwnd = host.hwnd();
    unsafe {
        if !IsWindow(Some(hwnd)).as_bool() || !IsWindowVisible(hwnd).as_bool() {
            return;
        }
    }
    let Some(parent) = host.parent_hwnd() else {
        return;
    };
    let mut rc = RECT::default();
    unsafe {
        let _ = GetWindowRect(parent, &mut rc);
    }
    let sb = host.hover_width(); // always hover width; idle vs hover is paint-only
    let r = candwnd::attached_rect(rc.right - rc.left, rc.bottom - rc.top, sb);
    unsafe {
        let _ = SetWindowPos(
            hwnd,
            Some(parent),
            r.left,
            r.top,
            r.width(),
            r.height(),
            SWP_NOOWNERZORDER | SWP_NOACTIVATE,
        );
    }
}

/// `_WindowProcCallback`.
///
/// # Safety
/// `hwnd` is the scroll bar.
pub unsafe fn window_proc(host: &mut dyn SbHost, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_ERASEBKGND => LRESULT(1),
        WM_PAINT => {
            unsafe {
                let mut ps = PAINTSTRUCT::default();
                let dc = BeginPaint(hwnd, &mut ps);
                let mut rc = RECT::default();
                let _ = GetClientRect(hwnd, &mut rc);
                let (w, h) = (rc.right - rc.left, rc.bottom - rc.top);
                let mem = CreateCompatibleDC(Some(dc));
                let bmp = CreateCompatibleBitmap(dc, w, h);
                let old = SelectObject(mem, bmp.into());
                let mut ps_mem = ps;
                ps_mem.rcPaint = rc;
                host.paint(mem, &mut ps_mem);
                let _ = BitBlt(dc, 0, 0, w, h, Some(mem), 0, 0, SRCCOPY);
                SelectObject(mem, old);
                let _ = DeleteObject(bmp.into());
                let _ = DeleteDC(mem);
                let _ = EndPaint(hwnd, &ps);
            }
            LRESULT(0)
        }
        WM_MOUSELEAVE => {
            host.set_tracking_mouse(false);
            LRESULT(0)
        }
        WM_TIMER => {
            // (CBaseWindow::_WindowProc routes WM_TIMER to _OnTimerID first; kept as in C++.)
            if wparam.0 == FADE_TIMER_ID {
                let alpha = host.alpha();
                if alpha > FADE_ALPHA_STEP {
                    host.set_alpha(alpha - FADE_ALPHA_STEP);
                    set_layered_alpha(hwnd, alpha - FADE_ALPHA_STEP);
                } else {
                    host.set_alpha(0);
                    host.set_fading(false);
                    unsafe {
                        let _ = KillTimer(Some(hwnd), FADE_TIMER_ID);
                    }
                    // Hide window — works on all Windows versions including Win7
                    show(host, false);
                }
            }
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) },
    }
}
