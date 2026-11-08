//! The drop shadow around the candidate and notify windows (port of
//! `CShadowWindow`): the shadow image (a rounded-rectangle signed distance
//! field with a quadratic falloff, written into a 32-bpp premultiplied-alpha
//! bitmap for `UpdateLayeredWindow`), its color for the light / dark themes,
//! and the window behavior (follow the owner, show, settings change,
//! `WM_PAINT`).  The state stays in the C++ object ([`ShadowHost`]).
//!
//! The pixel computation repeats the C++ double-precision expressions in the
//! same order, so the bytes are identical (`ShadowParityTest`).

use crate::candwnd;
use crate::layout::{Rect, SHADOW_SPREAD};
#[cfg(windows)]
use windows::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, POINT, RECT, SIZE, WPARAM};
#[cfg(windows)]
use windows::Win32::Graphics::Gdi::{
    BeginPaint, CreateCompatibleDC, CreateDIBSection, CreateSolidBrush, DeleteDC, DeleteObject, EndPaint, FillRect,
    GetDC, GetDeviceCaps, ReleaseDC, SelectObject, AC_SRC_ALPHA, AC_SRC_OVER, BITMAPINFO, BITMAPINFOHEADER, BITSPIXEL,
    BI_RGB, BLENDFUNCTION, DIB_RGB_COLORS, PAINTSTRUCT,
};
#[cfg(windows)]
use windows::Win32::UI::WindowsAndMessaging::{
    DefWindowProcW, GetWindowLongW, GetWindowRect, IsWindow, SetWindowLongW, SetWindowPos, UpdateLayeredWindow,
    GWL_EXSTYLE, SWP_NOACTIVATE, SWP_NOOWNERZORDER, ULW_ALPHA, WM_APP, WM_PAINT, WM_SETTINGCHANGE, WS_EX_LAYERED,
};

/// `SHADOW_MAX_ALPHA`: peak alpha of a light shadow on a dark background.
pub const MAX_ALPHA: u8 = 35;
/// `SHADOW_MAX_ALPHA_DARK`: peak alpha of a dark shadow on a light background.
pub const MAX_ALPHA_DARK: u8 = 45;

/// The shadow's gray level (R = G = B) and peak alpha for the theme and the
/// system window color (`GetSysColor(COLOR_WINDOW)`).
pub fn color(dark: bool, sys_window_color: u32) -> (u8, u8) {
    let (r, g, b) = (
        sys_window_color & 0xFF,
        (sys_window_color >> 8) & 0xFF,
        (sys_window_color >> 16) & 0xFF,
    );
    let lum = (299 * r + 587 * g + 114 * b) / 1000;
    let bg_is_light = lum > 128;
    if dark && bg_is_light {
        (0, MAX_ALPHA_DARK) // dark theme on a light system background: dark shadow
    } else if dark || !bg_is_light {
        (200, MAX_ALPHA) // light shadow
    } else {
        (0, MAX_ALPHA)
    }
}

/// The shadow image of a `cx` x `cy` window around an owner inset by
/// `spread` on each side with corner radius `owner_radius` (pixels): one
/// `0xAARRGGBB` premultiplied pixel per entry, rows in bitmap memory order.
pub fn pixels(cx: i32, cy: i32, spread: i32, owner_radius: i32, gray: u8, max_alpha: u8) -> Vec<u32> {
    let (w, h) = (cx.max(0) as usize, cy.max(0) as usize);
    let mut out = vec![0u32; w * h];
    let sp = spread;
    let (il, it, ir, ib) = (sp, sp, cx - sp, cy - sp);
    let owner_w = ir - il;
    let owner_h = ib - it;
    let r = f64::from(owner_radius) * 0.5; // arc radius (CreateRoundRectRgn takes ellipse size)
    let half_w = f64::from(owner_w) * 0.5;
    let half_h = f64::from(owner_h) * 0.5;
    let inner_rx = half_w - r;
    let inner_ry = half_h - r;
    let band_t = it + 2; // first interior row (no shadow)
    let band_b = ib - 2; // last interior row (no shadow)
    let band_l = il + 2; // first interior column
    let band_r = ir - 2; // last interior column

    // One pixel; `clamp` keeps the outer rows' falloff (dist capped at 1).
    let mut put = |x: i32, y: i32, clamp: bool| {
        let ox = f64::from(x - il);
        let oy = f64::from(y - it);
        let qx = (ox - half_w).abs() - inner_rx;
        let qy = (oy - half_h).abs() - inner_ry;
        let mx = if qx > 0.0 { qx } else { 0.0 };
        let my = if qy > 0.0 { qy } else { 0.0 };
        let len = (mx * mx + my * my).sqrt();
        let inner = if qx > qy { qx } else { qy };
        let sdf = len + (if inner < 0.0 { inner } else { 0.0 }) - r;
        if sdf <= -1.0 {
            return; // extend shadow 1px under candidate edge to close boundary gap
        }
        let mut dist = (if 0.0 > sdf { 0.0 } else { sdf }) / f64::from(sp);
        if dist > 1.0 {
            if !clamp {
                return;
            }
            dist = 1.0;
        }
        let f = 1.0 - dist;
        let alpha = (f64::from(max_alpha) * f * f) as u8;
        if alpha == 0 {
            return;
        }
        let c = u32::from(gray) * u32::from(alpha) / 255;
        out[y as usize * w + x as usize] = u32::from(alpha) << 24 | c << 16 | c << 8 | c;
    };

    for y in 0..cy {
        if y >= band_t && y <= band_b {
            for x in 0..band_l.min(cx) {
                put(x, y, false);
            }
            for x in band_r.max(band_l).max(0)..cx {
                put(x, y, false);
            }
            continue;
        }
        for x in 0..cx {
            put(x, y, true);
        }
    }
    out
}

/// `_AdjustWindowPos`: the shadow's rectangle around the owner's window
/// rectangle (`shift` is `_sizeShift`).
pub fn rect_around(owner: &Rect, shift: (i32, i32)) -> Rect {
    let left = owner.left + shift.0;
    let top = owner.top + shift.1;
    Rect {
        left,
        top,
        right: left + owner.width() + SHADOW_SPREAD * 2,
        bottom: top + owner.height() + SHADOW_SPREAD * 2,
    }
}

/// What the shadow logic needs from the C++ object.
#[cfg(windows)]
pub trait ShadowHost {
    fn hwnd(&mut self) -> HWND;
    /// The owner window (candidate / notify), if any.
    fn owner_hwnd(&mut self) -> Option<HWND>;
    /// `_pWndOwner->_GetCornerRadiusBase()`.
    fn owner_corner_radius_base(&mut self) -> i32;
    fn is_gradient(&mut self) -> bool;
    fn set_is_gradient(&mut self, on: bool);
    fn color(&mut self) -> u32;
    fn set_color(&mut self, color: u32);
    fn size_shift(&mut self) -> (i32, i32);
    fn set_size_shift(&mut self, shift: (i32, i32));
    fn is_visible(&mut self) -> bool;
    /// `CBaseWindow::_Show`.
    fn base_show(&mut self, on: bool);
    fn dpi(&mut self) -> u32;
    /// `CConfig::GetEffectiveDarkMode()`.
    fn dark(&mut self) -> bool;
    /// `GetSysColor(COLOR_WINDOW)`.
    fn sys_window_color(&mut self) -> u32;
}

/// `_InitSettings` (also `_Initialize`).
#[cfg(windows)]
pub fn init_settings(host: &mut dyn ShadowHost) {
    unsafe {
        let dc = GetDC(None);
        let bits = GetDeviceCaps(Some(dc), BITSPIXEL);
        host.set_is_gradient(bits > 8);
        ReleaseDC(None, dc);
    }
    host.set_color(0);
    host.set_size_shift((-SHADOW_SPREAD, -SHADOW_SPREAD));
}

/// `_AdjustWindowPos`: follow the owner, just below it in z-order.
#[cfg(windows)]
pub fn adjust_window_pos(host: &mut dyn ShadowHost) {
    let hwnd = host.hwnd();
    if !unsafe { IsWindow(Some(hwnd)) }.as_bool() {
        return;
    }
    let Some(owner) = host.owner_hwnd() else {
        return;
    };
    let mut rc = RECT::default();
    unsafe {
        let _ = GetWindowRect(owner, &mut rc);
    }
    let r = rect_around(
        &Rect {
            left: rc.left,
            top: rc.top,
            right: rc.right,
            bottom: rc.bottom,
        },
        host.size_shift(),
    );
    unsafe {
        let _ = SetWindowPos(
            hwnd,
            Some(owner),
            r.left,
            r.top,
            r.width(),
            r.height(),
            SWP_NOOWNERZORDER | SWP_NOACTIVATE,
        );
    }
}

/// `_InitShadow`: render the shadow image into the layered window.
#[cfg(windows)]
pub fn init_shadow(host: &mut dyn ShadowHost) {
    if !host.is_gradient() {
        return;
    }
    let hwnd = host.hwnd();
    unsafe {
        let style = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED.0 as i32);
    }
    let mut rc = RECT::default();
    unsafe {
        let _ = GetWindowRect(hwnd, &mut rc);
    }
    let size = SIZE {
        cx: rc.right - rc.left,
        cy: rc.bottom - rc.top,
    };
    if size.cx <= 0 || size.cy <= 0 {
        return;
    }
    let (gray, max_alpha) = color(host.dark(), host.sys_window_color());
    let owner_radius = candwnd::scale(host.owner_corner_radius_base(), host.dpi());
    let image = pixels(size.cx, size.cy, SHADOW_SPREAD, owner_radius, gray, max_alpha);
    unsafe {
        let screen = GetDC(None);
        if screen.is_invalid() {
            return;
        }
        let mem = CreateCompatibleDC(Some(screen));
        if mem.is_invalid() {
            ReleaseDC(None, screen);
            return;
        }
        let bi = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: size.cx,
                biHeight: size.cy,
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };
        let mut bits: *mut core::ffi::c_void = std::ptr::null_mut();
        let bmp = CreateDIBSection(Some(screen), &bi, DIB_RGB_COLORS, &mut bits, None, 0);
        let Ok(bmp) = bmp else {
            ReleaseDC(None, screen);
            let _ = DeleteDC(mem);
            return;
        };
        if bits.is_null() {
            ReleaseDC(None, screen);
            let _ = DeleteDC(mem);
            return;
        }
        std::ptr::copy_nonoverlapping(image.as_ptr(), bits as *mut u32, image.len());

        let src = POINT { x: 0, y: 0 };
        let blend = BLENDFUNCTION {
            BlendOp: AC_SRC_OVER as u8,
            BlendFlags: 0,
            SourceConstantAlpha: 255,
            AlphaFormat: AC_SRC_ALPHA as u8,
        };
        let old = SelectObject(mem, bmp.into());
        let _ = UpdateLayeredWindow(
            hwnd,
            Some(screen),
            None,
            Some(&size),
            Some(mem),
            Some(&src),
            COLORREF(0),
            Some(&blend),
            ULW_ALPHA,
        );
        SelectObject(mem, old);
        ReleaseDC(None, screen);
        let _ = DeleteDC(mem);
        let _ = DeleteObject(bmp.into());
    }
}

/// `_OnSettingChange`: the display may have changed bit depth.
#[cfg(windows)]
pub fn on_setting_change(host: &mut dyn ShadowHost) {
    init_settings(host);
    let hwnd = host.hwnd();
    unsafe {
        let style = GetWindowLongW(hwnd, GWL_EXSTYLE);
        let layered = WS_EX_LAYERED.0 as i32;
        let style = if host.is_gradient() {
            style | layered
        } else {
            style & !layered
        };
        SetWindowLongW(hwnd, GWL_EXSTYLE, style);
    }
    adjust_window_pos(host);
    init_shadow(host);
}

/// `_OnOwnerWndMoved`.
#[cfg(windows)]
pub fn on_owner_moved(host: &mut dyn ShadowHost, resized: bool) {
    let hwnd = host.hwnd();
    if unsafe { IsWindow(Some(hwnd)) }.as_bool() && host.is_visible() {
        adjust_window_pos(host);
        if resized {
            init_shadow(host);
        }
    }
}

/// `_Show`.
#[cfg(windows)]
pub fn show(host: &mut dyn ShadowHost, on: bool) {
    adjust_window_pos(host);
    host.base_show(on);
    if on {
        init_shadow(host);
    }
}

/// `_WindowProcCallback`.
///
/// # Safety
/// `hwnd` is the shadow window.
#[cfg(windows)]
pub unsafe fn window_proc(host: &mut dyn ShadowHost, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_PAINT => {
            unsafe {
                let mut ps = PAINTSTRUCT::default();
                let dc = BeginPaint(hwnd, &mut ps);
                let brush = CreateSolidBrush(COLORREF(host.color()));
                FillRect(dc, &ps.rcPaint, brush);
                let _ = DeleteObject(brush.into());
                let _ = EndPaint(hwnd, &ps);
            }
            LRESULT(0)
        }
        WM_SETTINGCHANGE => {
            on_setting_change(host);
            unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
        }
        m if m == WM_APP + 1 => {
            init_shadow(host);
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colors() {
        let white = 0x00FF_FFFF;
        let black = 0;
        assert_eq!(color(false, white), (0, MAX_ALPHA));
        assert_eq!(color(true, white), (0, MAX_ALPHA_DARK));
        assert_eq!(color(true, black), (200, MAX_ALPHA));
        assert_eq!(color(false, black), (200, MAX_ALPHA));
    }

    #[test]
    fn shadow_is_clear_inside_and_fades_out() {
        let (cx, cy) = (100, 60);
        let p = pixels(cx, cy, SHADOW_SPREAD, 10, 0, MAX_ALPHA);
        let at = |x: i32, y: i32| p[(y * cx + x) as usize];
        let a = |v: u32| v >> 24;
        // Inside the owner: nothing.
        assert_eq!(at(50, 30), 0);
        // Strongest just outside the owner's edge, fading outwards.
        assert!(a(at(SHADOW_SPREAD - 1, 30)) > a(at(2, 30)));
        assert!(a(at(SHADOW_SPREAD - 1, 30)) <= u32::from(MAX_ALPHA));
        // A black shadow is premultiplied black.
        assert_eq!(at(SHADOW_SPREAD - 1, 30) & 0x00FF_FFFF, 0);
    }
}
