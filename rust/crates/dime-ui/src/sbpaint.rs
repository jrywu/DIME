//! GDI drawing of the candidate window's scroll bar (port of
//! `CScrollBarWindow::_OnPaint`): the background (the magenta color key on a
//! layered window, else the candidate's brush from `WM_CTLCOLORSCROLLBAR`),
//! then either the hovered bar (arrow triangles and a pill thumb) or the idle
//! thin line, then the candidate's selection highlight on a non-layered bar
//! (RC26).  The same GDI calls in the same order as the C++ original
//! (`ScrollBarPaintMatchesCpp` in `CandidatePaintParityTest`).

use crate::candwnd::{self, ScrollInfo};
use crate::layout::Rect;
use windows::Win32::Foundation::{COLORREF, HWND, LPARAM, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    CreateSolidBrush, DeleteObject, FillRect, GetStockObject, Polygon, RoundRect, SelectObject, BLACK_BRUSH, HBRUSH,
    HDC, NULL_PEN,
};
use windows::Win32::UI::WindowsAndMessaging::{
    GetParent, GetWindowLongW, SendMessageW, GWL_EXSTYLE, WM_CTLCOLORSCROLLBAR, WS_EX_LAYERED,
};

/// `SCROLLBAR_COLORKEY` (magenta: transparent pixels of the layered bar).
pub const COLORKEY: u32 = 0x00FF_00FF;

const fn rgb(r: u8, g: u8, b: u8) -> u32 {
    r as u32 | (g as u32) << 8 | (b as u32) << 16
}

/// The input of [`paint`].
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub hwnd: HWND,
    /// `pps->rcPaint` (`None`: no `PAINTSTRUCT`, no background).
    pub paint: Option<Rect>,
    pub client: Rect,
    pub dpi: u32,
    pub si: ScrollInfo,
    /// `CConfig::GetEffectiveDarkMode()`.
    pub dark: bool,
    /// Hovered, or Windows' "always show scrollbars": arrows and pill thumb.
    pub full: bool,
    /// The candidate's selection highlight in scroll bar coordinates; the
    /// radius is -1 for none.
    pub highlight: Rect,
    pub highlight_radius: i32,
    pub highlight_color: u32,
}

fn rect(r: &Rect) -> RECT {
    RECT {
        left: r.left,
        top: r.top,
        right: r.right,
        bottom: r.bottom,
    }
}

/// A rounded rectangle with a solid brush and no pen.
unsafe fn round_rect(dc: HDC, r: &Rect, color: u32, rx: i32, ry: i32) {
    unsafe {
        let brush = CreateSolidBrush(COLORREF(color));
        let pen = GetStockObject(NULL_PEN);
        let old_br = SelectObject(dc, brush.into());
        let old_pen = SelectObject(dc, pen);
        let _ = RoundRect(dc, r.left, r.top, r.right, r.bottom, rx, ry);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        let _ = DeleteObject(brush.into());
    }
}

/// `CScrollBarWindow::_OnPaint`.
///
/// # Safety
/// `dc` must be a valid device context and `input.hwnd` the scroll bar.
pub unsafe fn paint(dc: HDC, input: &Input) {
    let layered = unsafe { GetWindowLongW(input.hwnd, GWL_EXSTYLE) } as u32 & WS_EX_LAYERED.0 != 0;
    unsafe {
        if let Some(p) = &input.paint {
            if layered {
                // layered windows use the magenta color key (becomes transparent)
                let bg = CreateSolidBrush(COLORREF(COLORKEY));
                FillRect(dc, &rect(p), bg);
                let _ = DeleteObject(bg.into());
            } else {
                // non-layered (legacy apps where WS_EX_LAYERED failed): the parent's background brush
                let mut bg = match GetParent(input.hwnd) {
                    Ok(parent) if !parent.is_invalid() => HBRUSH(
                        SendMessageW(
                            parent,
                            WM_CTLCOLORSCROLLBAR,
                            Some(WPARAM(dc.0 as usize)),
                            Some(LPARAM(input.hwnd.0 as isize)),
                        )
                        .0 as *mut _,
                    ),
                    _ => HBRUSH::default(),
                };
                if bg.is_invalid() {
                    bg = HBRUSH(GetStockObject(BLACK_BRUSH).0);
                }
                FillRect(dc, &rect(p), bg);
            }
        }
    }

    if input.si.max <= input.si.page {
        return;
    }

    if input.full {
        // Hovered or always-show: full scrollbar — triangles + pill thumb
        let a = candwnd::arrows(&input.client, input.dpi);
        let arrow = if input.dark {
            rgb(160, 160, 160)
        } else {
            rgb(100, 100, 100)
        };
        unsafe {
            let brush = CreateSolidBrush(COLORREF(arrow));
            let pen = GetStockObject(NULL_PEN);
            let old_br = SelectObject(dc, brush.into());
            let old_pen = SelectObject(dc, pen);
            let up: Vec<_> =
                a.up.iter()
                    .map(|p| windows::Win32::Foundation::POINT { x: p.x, y: p.y })
                    .collect();
            let down: Vec<_> = a
                .down
                .iter()
                .map(|p| windows::Win32::Foundation::POINT { x: p.x, y: p.y })
                .collect();
            let _ = Polygon(dc, &up);
            let _ = Polygon(dc, &down);
            SelectObject(dc, old_br);
            SelectObject(dc, old_pen);
            let _ = DeleteObject(brush.into());
        }
        // Pill thumb — single-point top aligned with the triangle apex.
        let mut thumb = candwnd::thumb(&input.client, input.dpi, &input.si);
        thumb.left = a.pill_left;
        thumb.right = a.pill_right;
        let color = if input.dark {
            rgb(180, 180, 180)
        } else {
            rgb(128, 128, 128)
        };
        unsafe { round_rect(dc, &thumb, color, a.pill_radius, a.pill_radius) };
    } else {
        // Idle/hiding: thin centered line only, no arrows, full-height track.
        let Some(line) = candwnd::idle_thumb(&input.client, input.dpi, &input.si) else {
            return;
        };
        let radius = line.width() / 2;
        unsafe { round_rect(dc, &line, rgb(160, 160, 160), radius, radius) };
    }

    // RC26: the candidate's rounded selection highlight over the bar (non-layered only).
    if input.highlight_radius >= 0 && !layered {
        unsafe {
            round_rect(
                dc,
                &input.highlight,
                input.highlight_color,
                input.highlight_radius,
                input.highlight_radius,
            )
        };
    }
}
