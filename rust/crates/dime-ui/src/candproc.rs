//! Message handling of the candidate window (port of
//! `CCandidateWindow::_WindowProcCallback`, `_HandleMouseMsg`,
//! `_OnLButtonDown`, `_OnLButtonUp`, `_OnMouseMove`, `_OnVScroll` and
//! `_FireMessageToLightDismiss`).  Win32 calls that touch only the window go
//! straight to the system; everything owned by C++ (the candidate list model,
//! the shadow and scroll bar windows, the selection callback, painting) goes
//! through [`CandHost`].
//!
//! The selection callback may destroy the window: [`CandHost::invoke`] is
//! always the last thing a handler does, as in the C++ original.

use crate::candwnd;
use crate::layout::{Point, Rect};
use windows::core::PCWSTR;
pub use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Foundation::{POINT, RECT};
pub use windows::Win32::Graphics::Gdi::PAINTSTRUCT;
use windows::Win32::Graphics::Gdi::{
    BeginPaint, BitBlt, CreateCompatibleBitmap, CreateCompatibleDC, DeleteDC, DeleteObject, EndPaint, GetDC,
    GetTextExtentPoint32W, MapWindowPoints, ReleaseDC, SelectObject, HBRUSH, HDC, HFONT, SRCCOPY,
};
use windows::Win32::UI::Accessibility::NotifyWinEvent;
use windows::Win32::UI::Input::KeyboardAndMouse::{SetCapture, TrackMouseEvent, TME_LEAVE, TRACKMOUSEEVENT};
use windows::Win32::UI::WindowsAndMessaging::{
    DefWindowProcW, GetClientRect, GetCursorPos, IsWindow, IsWindowVisible, LoadCursorW, SetCursor, SetWindowPos,
    CHILDID_SELF, EVENT_OBJECT_IME_CHANGE, EVENT_OBJECT_IME_HIDE, EVENT_OBJECT_IME_SHOW, IDC_ARROW, IDC_HAND,
    MA_NOACTIVATE, OBJID_CLIENT, SB_LINEDOWN, SB_LINEUP, SB_PAGEDOWN, SB_PAGEUP, SB_THUMBPOSITION, SB_THUMBTRACK,
    SWP_HIDEWINDOW, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOREDRAW, SWP_NOSIZE, SWP_NOZORDER, SWP_SHOWWINDOW, WINDOWPOS,
    WM_CREATE, WM_CTLCOLORSCROLLBAR, WM_DESTROY, WM_DPICHANGED, WM_ERASEBKGND, WM_LBUTTONDOWN, WM_LBUTTONUP,
    WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MOUSEACTIVATE, WM_MOUSEMOVE, WM_MOUSEWHEEL, WM_PAINT, WM_RBUTTONDOWN,
    WM_RBUTTONUP, WM_SETCURSOR, WM_SHOWWINDOW, WM_VSCROLL, WM_WINDOWPOSCHANGED, WM_WINDOWPOSCHANGING,
};

/// `WM_MOUSELEAVE` (`WinUser.h`, not in the `windows` crate's message list).
const WM_MOUSELEAVE: u32 = 0x02A3;
/// `WM_POINTERACTIVATE` and `PA_NOACTIVATE`.
const WM_POINTERACTIVATE: u32 = 0x024B;
const PA_NOACTIVATE: isize = 3;
/// The character `WM_CREATE` measures (as `_DrawList`).
const TEST_CHAR: u16 = 0x570B; // 國

/// `CANDWND_ACTION`.
pub mod action {
    pub const ITEM_SELECT: i32 = 0;
    pub const CANCEL: i32 = 1;
}

/// What the window procedure needs from the C++ side.
pub trait CandHost {
    // The candidate list model (CCandidateListModel) and the paging wrappers.
    fn selection(&mut self) -> i32;
    fn set_selection_raw(&mut self, index: u32);
    /// `_SetSelectionOffset`.
    fn set_selection_offset(&mut self, offset: i32);
    /// `_SetSelection(index, FALSE)`.
    fn set_selection(&mut self, index: i32);
    /// `_MovePage(offset, FALSE)`.
    fn move_page(&mut self, offset: i32);
    /// `_GetCurrentPage`; `None` when it fails.
    fn current_page(&mut self) -> Option<i32>;
    fn page_count(&mut self) -> u32;
    fn page_start(&mut self, page: i32) -> u32;
    fn count(&mut self) -> u32;
    /// `_pIndexRange->Count()`; `None` without an index range.
    fn rows_in_page(&mut self) -> Option<u32>;

    // Layout members.
    fn padding(&mut self) -> i32;
    fn cy_row(&mut self) -> i32;
    fn wnd_width(&mut self) -> u32;
    fn set_initial_metrics(&mut self, cx_title: i32, cy_row: i32, padding: i32);
    /// `_GetScrollBarWidth`.
    fn sb_width(&mut self) -> i32;
    /// `Global::defaultlFontHandle`.
    fn font(&mut self) -> HFONT;
    /// `_brshBkColor` (may be null).
    fn bk_brush(&mut self) -> HBRUSH;

    fn has_callback(&mut self) -> bool;
    /// The selection callback; it may destroy the window.
    fn invoke(&mut self, action: i32);
    /// `_OnPaint`.
    fn paint(&mut self, dc: HDC, ps: &mut PAINTSTRUCT);
    /// `_InvalidateRect`.
    fn invalidate(&mut self);
    /// `CConfig::SetDefaultTextFont(hwnd)`.
    fn dpi_changed(&mut self, hwnd: HWND);
    /// `_IsCapture` (a scroll bar drag holds the capture object).
    fn is_capture(&mut self) -> bool;
    fn tracking_mouse(&mut self) -> bool;
    fn set_tracking_mouse(&mut self, on: bool);

    // The shadow window.
    fn has_shadow(&mut self) -> bool;
    fn shadow_hwnd(&mut self) -> HWND;
    fn shadow_show(&mut self, show: i32);
    fn shadow_owner_moved(&mut self, resized: bool);
    fn delete_shadow(&mut self);

    // The scroll bar window.
    fn has_scroll_bar(&mut self) -> bool;
    fn sb_hwnd(&mut self) -> HWND;
    fn sb_client_rect(&mut self) -> Rect;
    fn sb_is_enabled(&mut self) -> bool;
    fn sb_show(&mut self, show: bool);
    fn sb_owner_moved(&mut self, resized: bool);
    fn sb_set_candidate_mouse_in(&mut self, inside: bool);
    fn sb_on_scroll_activity(&mut self);
    fn sb_shift_position(&mut self, position: i32);
    /// The scroll bar's `_OnLButtonDown` / `_OnLButtonUp` / `_OnMouseMove`.
    fn sb_mouse(&mut self, msg: u32, pt: Point);
}

fn lo_word(v: usize) -> u32 {
    (v & 0xFFFF) as u32
}
fn hi_word(v: usize) -> u32 {
    ((v >> 16) & 0xFFFF) as u32
}

fn point(p: Point) -> POINT {
    POINT { x: p.x, y: p.y }
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

/// `MapWindowPoints(from, to, &pt, 1)`.
fn map_point(from: Option<HWND>, to: Option<HWND>, p: Point) -> Point {
    let mut pts = [point(p)];
    unsafe {
        MapWindowPoints(from, to, &mut pts);
    }
    Point {
        x: pts[0].x,
        y: pts[0].y,
    }
}

/// `_WindowProcCallback`.
///
/// # Safety
/// `hwnd` is the candidate window; `lparam` follows `msg` (`WINDOWPOS*`,
/// `RECT*` for `WM_DPICHANGED`).
pub unsafe fn window_proc(host: &mut dyn CandHost, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    let def = || unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) };
    match msg {
        WM_CREATE => {
            on_create(host, hwnd);
            LRESULT(0)
        }
        WM_DESTROY => {
            host.delete_shadow();
            LRESULT(0)
        }
        WM_WINDOWPOSCHANGED => {
            let wp = lparam.0 as *const WINDOWPOS;
            if let Some(wp) = unsafe { wp.as_ref() } {
                let resized = (wp.flags.0 & SWP_NOSIZE.0) == 0;
                // move shadow
                if host.has_shadow() {
                    host.shadow_owner_moved(resized);
                }
                // move v-scroll
                if host.has_scroll_bar() {
                    host.sb_owner_moved(resized);
                }
                fire_light_dismiss(hwnd, wp);
            }
            def()
        }
        WM_WINDOWPOSCHANGING => {
            let wp = lparam.0 as *mut WINDOWPOS;
            if let Some(wp) = unsafe { wp.as_mut() } {
                // show/hide shadow
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
                // show/hide v-scroll
                if host.has_scroll_bar() {
                    if (wp.flags.0 & SWP_HIDEWINDOW.0) != 0 {
                        host.sb_show(false);
                    }
                    host.sb_owner_moved((wp.flags.0 & SWP_NOSIZE.0) == 0);
                }
            }
            def()
        }
        WM_SHOWWINDOW => {
            // show/hide shadow
            if host.has_shadow() {
                host.shadow_show(wparam.0 as i32);
            }
            if host.has_scroll_bar() {
                // Only show scrollbar if multi-page; always hide when candidate hides
                let show = wparam.0 as i32 != 0 && host.sb_is_enabled();
                host.sb_show(show);
            }
            def()
        }
        // suppress background erase to prevent flicker during scrolling
        WM_ERASEBKGND => LRESULT(1),
        WM_CTLCOLORSCROLLBAR => {
            // the scroll bar's track matches the candidate background in all color modes
            let brush = host.bk_brush();
            if !brush.is_invalid() {
                LRESULT(brush.0 as isize)
            } else {
                def()
            }
        }
        WM_PAINT => {
            on_paint(host, hwnd);
            LRESULT(0)
        }
        WM_SETCURSOR => {
            let mut cursor = POINT::default();
            unsafe {
                let _ = GetCursorPos(&mut cursor);
            }
            let pt = map_point(
                None,
                Some(hwnd),
                Point {
                    x: cursor.x,
                    y: cursor.y,
                },
            );
            // handle mouse message
            handle_mouse(host, hwnd, hi_word(lparam.0 as usize), pt);
            LRESULT(1)
        }
        WM_MOUSELEAVE => {
            host.set_tracking_mouse(false);
            if host.has_scroll_bar() {
                host.sb_set_candidate_mouse_in(false);
            }
            LRESULT(0)
        }
        WM_MOUSEMOVE | WM_LBUTTONDOWN | WM_MBUTTONDOWN | WM_RBUTTONDOWN | WM_LBUTTONUP | WM_MBUTTONUP
        | WM_RBUTTONUP => {
            // POINTSTOPOINT(MAKEPOINTS(lParam))
            let pt = Point {
                x: (lparam.0 as u32 & 0xFFFF) as u16 as i16 as i32,
                y: ((lparam.0 as u32) >> 16) as u16 as i16 as i32,
            };
            handle_mouse(host, hwnd, msg, pt);
            LRESULT(0)
        }
        WM_MOUSEACTIVATE => {
            let mouse = hi_word(lparam.0 as usize);
            if mouse == WM_LBUTTONDOWN || mouse == WM_RBUTTONDOWN || mouse == WM_MBUTTONDOWN {
                LRESULT(MA_NOACTIVATE as isize)
            } else {
                def()
            }
        }
        WM_POINTERACTIVATE => LRESULT(PA_NOACTIVATE),
        WM_MOUSEWHEEL => {
            let delta = hi_word(wparam.0) as u16 as i16;
            let prev = host.selection();
            if delta > 0 {
                host.set_selection_offset(-1);
            } else if delta < 0 {
                host.set_selection_offset(1);
            }
            let now = host.selection();
            if now != prev {
                if host.has_scroll_bar() {
                    host.sb_on_scroll_activity();
                    host.sb_shift_position(now);
                }
                host.invalidate();
            }
            LRESULT(0)
        }
        WM_VSCROLL => {
            on_vscroll(host, lo_word(wparam.0), hi_word(wparam.0));
            LRESULT(0)
        }
        WM_DPICHANGED => {
            // Recreate font at the new monitor DPI
            host.dpi_changed(hwnd);
            // Use the suggested new window rect from Windows
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

/// `WM_CREATE`: the initial metrics from the extent of `_wndWidth` test characters.
fn on_create(host: &mut dyn CandHost, hwnd: HWND) {
    unsafe {
        let dc = GetDC(Some(hwnd));
        if dc.is_invalid() {
            return;
        }
        let old = SelectObject(dc, host.font().into());
        let wnd_width = host.wnd_width();
        let test: Vec<u16> = vec![TEST_CHAR; wnd_width as usize];
        let mut size = windows::Win32::Foundation::SIZE::default();
        // don't trust the TextMetrics. Measurement the font height and width directly.
        let _ = GetTextExtentPoint32W(dc, &test, &mut size);
        let (cx_title, cy_row, padding) = candwnd::initial_metrics(size.cx, size.cy, wnd_width);
        host.set_initial_metrics(cx_title, cy_row, padding);
        SelectObject(dc, old);
        ReleaseDC(Some(hwnd), dc);
    }
}

/// `WM_PAINT`: compose the frame off-screen, then blit once.
fn on_paint(host: &mut dyn CandHost, hwnd: HWND) {
    unsafe {
        let mut ps = PAINTSTRUCT::default();
        let dc = BeginPaint(hwnd, &mut ps);
        let client = client_rect(hwnd);
        let (w, h) = (client.width(), client.height());
        let mem = CreateCompatibleDC(Some(dc));
        let bmp = CreateCompatibleBitmap(dc, w, h);
        let old = SelectObject(mem, bmp.into());

        let mut ps_mem = ps;
        ps_mem.rcPaint = RECT {
            left: client.left,
            top: client.top,
            right: client.right,
            bottom: client.bottom,
        };
        host.paint(mem, &mut ps_mem);

        let _ = BitBlt(dc, 0, 0, w, h, Some(mem), 0, 0, SRCCOPY);

        SelectObject(mem, old);
        let _ = DeleteObject(bmp.into());
        let _ = DeleteDC(mem);
        // No custom border: CShadowWindow provides the visual separation.
        let _ = EndPaint(hwnd, &ps);
    }
}

/// `_HandleMouseMsg`.
fn handle_mouse(host: &mut dyn CandHost, hwnd: HWND, msg: u32, pt: Point) {
    match msg {
        WM_MOUSEMOVE => on_mouse_move(host, hwnd, pt),
        WM_LBUTTONDOWN => on_lbutton_down(host, hwnd, pt),
        WM_LBUTTONUP => on_lbutton_up(host, hwnd, pt),
        _ => {}
    }
}

/// `_OnLButtonDown`: select the clicked row, forward to the scroll bar, or cancel.
fn on_lbutton_down(host: &mut dyn CandHost, hwnd: HWND, pt: Point) {
    // Safety check: ensure window is still valid
    if !unsafe { IsWindow(Some(hwnd)) }.as_bool() {
        return;
    }
    let Some(rows) = host.rows_in_page() else {
        return;
    };
    let client = client_rect(hwnd);
    let Some(page) = host.current_page() else {
        return;
    };
    // Hit test on list items
    let start = host.page_start(page);
    let (padding, sb_width, cy_row, count) = (host.padding(), host.sb_width(), host.cy_row(), host.count());
    if let Some(row) = candwnd::hit_row(&client, padding, sb_width, cy_row, rows, start, count, pt) {
        if host.has_callback() {
            set_cursor(IDC_HAND);
            host.set_selection_raw(start.wrapping_add(row));
            host.invoke(action::ITEM_SELECT);
            return;
        }
    }

    set_cursor(IDC_ARROW);

    if host.has_scroll_bar() && host.sb_is_enabled() {
        let rc = host.sb_client_rect();
        let sb = host.sb_hwnd();
        let p = map_point(Some(hwnd), Some(sb), pt);
        if candwnd::contains(&rc, p) {
            host.sb_mouse(WM_LBUTTONDOWN, p);
            return;
        }
    }
    host.invoke(action::CANCEL);
}

/// `_OnLButtonUp`: the scroll bar finishes a drag or a button press.
fn on_lbutton_up(host: &mut dyn CandHost, hwnd: HWND, pt: Point) {
    if !host.has_scroll_bar() {
        return;
    }
    let rc = host.sb_client_rect();
    let sb = host.sb_hwnd();
    let p = map_point(Some(hwnd), Some(sb), pt);
    if host.is_capture() || candwnd::contains(&rc, p) {
        host.sb_mouse(WM_LBUTTONUP, p);
    }
}

/// `_OnMouseMove`: cursor, scroll bar hover and drag.
fn on_mouse_move(host: &mut dyn CandHost, hwnd: HWND, pt: Point) {
    if !host.is_capture() {
        unsafe {
            SetCapture(hwnd);
        }
    }
    let client = client_rect(hwnd);
    let scroll_width = host.sb_width();
    // Mouse in scrollbar column → full scrollbar; outside → thin line after 2s delay (handled in scrollbar).
    if host.has_scroll_bar() {
        let col = candwnd::scroll_column(&client, scroll_width);
        host.sb_set_candidate_mouse_in(candwnd::contains(&col, pt));
    }

    if !host.tracking_mouse() {
        let mut tme = TRACKMOUSEEVENT {
            cbSize: std::mem::size_of::<TRACKMOUSEEVENT>() as u32,
            dwFlags: TME_LEAVE,
            hwndTrack: hwnd,
            dwHoverTime: 0,
        };
        unsafe {
            let _ = TrackMouseEvent(&mut tme);
        }
        host.set_tracking_mouse(true);
    }

    if candwnd::contains(&candwnd::list_area(&client, scroll_width), pt) {
        set_cursor(IDC_HAND);
    } else {
        set_cursor(IDC_ARROW);
        if host.has_scroll_bar() {
            let rc = host.sb_client_rect();
            let sb = host.sb_hwnd();
            let p = map_point(Some(hwnd), Some(sb), pt);
            if candwnd::contains(&rc, p) {
                host.sb_mouse(WM_MOUSEMOVE, p);
            }
        }
    }
}

/// `_OnVScroll`: scroll bar events (stop at the ends; the keyboard wraps).
fn on_vscroll(host: &mut dyn CandHost, code: u32, pos: u32) {
    let prev = host.selection();
    match code as i32 {
        c if c == SB_LINEDOWN.0 => host.set_selection_offset(1),
        c if c == SB_LINEUP.0 => host.set_selection_offset(-1),
        c if c == SB_PAGEDOWN.0 => {
            if let Some(page) = host.current_page() {
                if page < (host.page_count() as i32).wrapping_sub(1) {
                    host.move_page(1);
                }
            }
        }
        c if c == SB_PAGEUP.0 => {
            if let Some(page) = host.current_page() {
                if page > 0 {
                    host.move_page(-1);
                }
            }
        }
        c if c == SB_THUMBPOSITION.0 || c == SB_THUMBTRACK.0 => host.set_selection(pos as i32),
        _ => {}
    }
    // Update scrollbar thumb and repaint only if selection actually changed
    let now = host.selection();
    if now != prev {
        if host.has_scroll_bar() {
            host.sb_shift_position(now);
        }
        host.invalidate();
    }
}

/// `_FireMessageToLightDismiss`: tell LightDismiss about the IME window.
fn fire_light_dismiss(hwnd: HWND, wp: &WINDOWPOS) {
    let f = wp.flags.0;
    let show = (f & SWP_SHOWWINDOW.0) != 0;
    let hide = (f & SWP_HIDEWINDOW.0) != 0;
    let resize = (f & SWP_NOSIZE.0) == 0;
    let moved = (f & SWP_NOMOVE.0) == 0;
    let redraw = (f & SWP_NOREDRAW.0) == 0;
    unsafe {
        if show {
            NotifyWinEvent(EVENT_OBJECT_IME_SHOW, hwnd, OBJID_CLIENT.0, CHILDID_SELF as i32);
        } else if hide {
            NotifyWinEvent(EVENT_OBJECT_IME_HIDE, hwnd, OBJID_CLIENT.0, CHILDID_SELF as i32);
        } else if (resize || moved || redraw) && IsWindowVisible(hwnd).as_bool() {
            NotifyWinEvent(EVENT_OBJECT_IME_CHANGE, hwnd, OBJID_CLIENT.0, CHILDID_SELF as i32);
        }
    }
}
