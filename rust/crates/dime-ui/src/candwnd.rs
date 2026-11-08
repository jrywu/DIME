//! Candidate window and scroll bar layout (port of the calculations in
//! `CCandidateWindow` and `CScrollBarWindow`): row metrics from the measured
//! font, window size, row / highlight / number / text rectangles, item text
//! truncation and widening, mouse hit tests, the page indicator, and the
//! scroll bar buttons, track, thumb, arrows and dragging.  Pure: font
//! measurement (`GetTextExtentPoint32`, `GetTextMetrics`), the client
//! rectangles and the DPI come from the caller.
//!
//! Integer arithmetic follows the C++ exactly, including `MulDiv` rounding and
//! the unsigned divisions that come from mixing `LONG` and `UINT`; all
//! operations wrap instead of panicking.

use crate::layout::{Point, Rect};

/// `USER_DEFAULT_SCREEN_DPI`.
pub const DEFAULT_DPI: i32 = 96;
/// `CANDWND_BORDER_WIDTH` (`Define.h`).
pub const BORDER_WIDTH: i32 = 1;
/// `MAX_CAND_ITEM_LENGTH` (`Define.h`): longer items show 6 + "…" + 6.
pub const MAX_ITEM_LENGTH: i32 = 13;
/// Characters kept at each end of a truncated item.
pub const TRUNCATED_KEEP: i32 = 6;
/// `StringPosition` (`CandidateWindow.cpp`).
const STRING_POSITION: u32 = 2;

/// `ScrollBarWindow.h` constants (pixels at 96 DPI).
pub const SCROLLBAR_THIN_WIDTH: i32 = 6;
pub const SCROLLBAR_HOVER_WIDTH: i32 = 16;
pub const SCROLLBAR_THUMB_MIN_H: i32 = 12;
pub const SCROLLBAR_BTN_HEIGHT: i32 = 14;
pub const SCROLLBAR_IDLE_THUMB_W: i32 = 2;

/// `SCROLLBAR_HITTEST`.
pub mod hit {
    pub const NONE: i32 = 0;
    pub const BTN_UP: i32 = 1;
    pub const BTN_DOWN: i32 = 2;
    pub const TRACK_ABOVE: i32 = 3;
    pub const TRACK_BELOW: i32 = 4;
    pub const THUMB: i32 = 5;
}

/// Win32 `MulDiv` of the process's own `kernel32` (x86 and x64 differ at
/// `i32::MIN`): `a * b / c` in 64 bits, rounded half away from zero; -1 when
/// `c` is 0 or the result does not fit (`i32::MIN` included).  The `i32::MIN`
/// edge cases were measured on Windows; `UILayoutParityTest::MulDivMatchesWin32`
/// checks them on both architectures.
pub fn mul_div(a: i32, b: i32, c: i32) -> i32 {
    if cfg!(target_arch = "x86") {
        mul_div_x86(a, b, c)
    } else {
        mul_div_x64(a, b, c)
    }
}

/// 64-bit `MulDiv`: a factor of `i32::MIN` gives -1 (0 when the other one
/// is 0).
pub fn mul_div_x64(a: i32, b: i32, c: i32) -> i32 {
    if c == 0 {
        return -1;
    }
    if a == 0 || b == 0 {
        return 0;
    }
    if a == i32::MIN || b == i32::MIN {
        return -1;
    }
    round_div(a, b, c)
}

/// 32-bit `MulDiv`: factors of `i32::MIN` are ordinary; a divisor of
/// `i32::MIN` gives `(|a * b| + 3 * 2^30) / 2^31` with the sign of
/// `a ^ b ^ c` (0 counts as positive).
pub fn mul_div_x86(a: i32, b: i32, c: i32) -> i32 {
    if c == 0 {
        return -1;
    }
    if c == i32::MIN {
        let q = ((i64::from(a) * i64::from(b)).unsigned_abs() + (3u64 << 30)) >> 31;
        if q > i32::MAX as u64 {
            return -1;
        }
        let negative = !((a < 0) ^ (b < 0));
        return if negative { -(q as i32) } else { q as i32 };
    }
    round_div(a, b, c)
}

/// `a * b / c` rounded half away from zero; -1 when it does not fit.
fn round_div(a: i32, b: i32, c: i32) -> i32 {
    let (mut a, mut c) = (i64::from(a), i64::from(c));
    if c < 0 {
        a = -a;
        c = -c;
    }
    let b = i64::from(b);
    let p = a * b;
    let r = if (a < 0) == (b < 0) {
        (p + c / 2) / c
    } else {
        (p - c / 2) / c
    };
    if !(-2_147_483_647..=2_147_483_647).contains(&r) {
        -1
    } else {
        r as i32
    }
}

/// `MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI)`.
pub fn scale(value: i32, dpi: u32) -> i32 {
    mul_div(value, dpi as i32, DEFAULT_DPI)
}

/// `PtInRect`.
pub fn contains(r: &Rect, p: Point) -> bool {
    p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom
}

fn rect(left: i32, top: i32, right: i32, bottom: i32) -> Rect {
    Rect {
        left,
        top,
        right,
        bottom,
    }
}

// ---------------------------------------------------------------------------
// Candidate window
// ---------------------------------------------------------------------------

/// The row metrics `_DrawList` derives from the font.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct CandMetrics {
    /// Width of the number column.
    pub cx_line: i32,
    /// Row height.
    pub cy_line: i32,
    /// Blank above the first row.
    pub first_line_offset: i32,
    pub left_padding: i32,
    /// Content width (without the scroll bar and the border).
    pub cx_title: i32,
    /// Bottom padding.
    pub padding: i32,
    /// Text baseline offset inside a row, plus the first-row offset.
    pub cy_offset: i32,
}

/// `WM_CREATE`: `(cx_title, cy_row, padding)` from the extent of `wnd_width`
/// "國" characters.  (`cx / wnd_width` is an unsigned division in C++.)
pub fn initial_metrics(cx: i32, cy: i32, wnd_width: u32) -> (i32, i32, i32) {
    let per_char = (cx as u32).checked_div(wnd_width).unwrap_or(0);
    let cx_title = (cx as u32).wrapping_add(STRING_POSITION.wrapping_mul(per_char)) as i32;
    let cy_row = cy.wrapping_mul(5) / 4;
    (cx_title, cy_row, cy_row / 2)
}

/// `_DrawList`: the metrics from the extent of `wnd_width` "國" characters and
/// `tmAveCharWidth`.
pub fn metrics(cx: i32, cy: i32, ave_char_width: i32, wnd_width: u32) -> CandMetrics {
    let per_char = cx.checked_div(wnd_width as i32).unwrap_or(0);
    let cx_line = ave_char_width.max(per_char);
    let cy_line = cy.wrapping_mul(5) / 4;
    let first_line_offset = cy_line / 3;
    let left_padding = cy_line / 2;
    CandMetrics {
        cx_line,
        cy_line,
        first_line_offset,
        left_padding,
        cx_title: cx.wrapping_add(left_padding.wrapping_add(cx_line)),
        padding: cy_line / 2,
        cy_offset: (cy / 8).wrapping_add(first_line_offset),
    }
}

/// `_ResizeWindow` / `_GetWidth`: the window size `(width, height)`.
pub fn window_size(cx_title: i32, cy_row: i32, padding: i32, rows: i32, sb_width: i32) -> (i32, i32) {
    let w = cx_title.wrapping_add(sb_width).wrapping_add(BORDER_WIDTH * 2);
    let h = (cy_row / 3)
        .wrapping_add(cy_row.wrapping_mul(rows))
        .wrapping_add(padding)
        .wrapping_add(BORDER_WIDTH * 2);
    (w, h)
}

/// `_ResizeWindow`: the scroll bar inside the candidate client area, kept
/// clear of the rounded corners.
pub fn scrollbar_rect(client: &Rect, sb_width: i32, dpi: u32, corner_radius_base: i32) -> Rect {
    let arc = scale(corner_radius_base / 2, dpi);
    let left = client.right.wrapping_sub(sb_width);
    let top = client.top.wrapping_add(arc);
    let height = client.height().wrapping_sub(arc.wrapping_mul(2));
    rect(left, top, left.wrapping_add(sb_width), top.wrapping_add(height))
}

/// One row of `_DrawList`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct CandRow {
    /// The whole row (filled before the selection highlight).
    pub row: Rect,
    /// The rounded selection highlight.
    pub highlight: Rect,
    /// The number column (also the filler of an empty row).
    pub number: Rect,
    /// The candidate text clip rectangle.
    pub text: Rect,
    pub corner_radius: i32,
    /// `ExtTextOut` origins.
    pub number_x: i32,
    pub text_x: i32,
    pub text_y: i32,
}

/// `_DrawList`: the rectangles of row `index` (0-based in the page) of the
/// paint rectangle `paint`.
pub fn row(paint: &Rect, index: i32, m: &CandMetrics, sb_width: i32, dpi: u32) -> CandRow {
    let top = paint
        .top
        .wrapping_add(index.wrapping_mul(m.cy_line))
        .wrapping_add(m.first_line_offset);
    let bottom = top.wrapping_add(m.cy_line);
    let v_inset = scale(1, dpi);
    let text_x = m.left_padding.wrapping_add(m.cx_line);
    CandRow {
        row: rect(paint.left, top, paint.right, bottom),
        highlight: rect(
            paint.left.wrapping_add(m.left_padding),
            top.wrapping_add(v_inset),
            paint.right.wrapping_sub(m.left_padding),
            bottom.wrapping_sub(v_inset),
        ),
        number: rect(
            paint.left.wrapping_add(m.left_padding),
            top,
            paint.left.wrapping_add(text_x),
            bottom,
        ),
        text: rect(
            paint.left.wrapping_add(text_x),
            top,
            paint.right.wrapping_sub(sb_width),
            bottom,
        ),
        corner_radius: scale(4, dpi),
        number_x: m.left_padding,
        text_x,
        text_y: index.wrapping_mul(m.cy_line).wrapping_add(m.cy_offset),
    }
}

/// `_DrawList`: an item of `length` characters is shown as the first 6, "…"
/// and the last 6 when longer than `MAX_CAND_ITEM_LENGTH`.  Returns the shown
/// length and, when truncated, where the tail starts.
pub fn truncate_item(length: i32) -> (i32, Option<i32>) {
    if length > MAX_ITEM_LENGTH {
        (MAX_ITEM_LENGTH, Some(length.wrapping_sub(TRUNCATED_KEEP)))
    } else {
        (length, None)
    }
}

/// `_DrawList`: whether an item of `display_length` shown characters is
/// measured to widen the window.
pub fn item_needs_measure(display_length: i32, wnd_width: u32) -> bool {
    display_length > wnd_width as i32
}

/// `_DrawList`: widen for a measured item (`measured_cx` is the extent of
/// the whole, untruncated item).  Returns the new `(cx_title, wnd_width)`
/// when it widens.
pub fn widen_for_item(display_length: i32, measured_cx: i32, text_left: i32, cx_title: i32) -> Option<(i32, u32)> {
    let needed = measured_cx.wrapping_add(text_left);
    (cx_title < needed).then_some((needed, display_length as u32))
}

/// `_DrawList`: the window is resized when the content width changed.
pub fn needs_resize(cx_title: i32, paint: &Rect, sb_width: i32) -> bool {
    cx_title != paint.width().wrapping_sub(sb_width)
}

/// `_OnLButtonDown`: the row (0-based in the page) under `pt`, if any.
/// Rows exist for `page_start..count`, at most `rows_in_page` of them.
#[allow(clippy::too_many_arguments)]
pub fn hit_row(
    client: &Rect,
    padding: i32,
    sb_width: i32,
    cy_row: i32,
    rows_in_page: u32,
    page_start: u32,
    count: u32,
    pt: Point,
) -> Option<u32> {
    let left = client.left.wrapping_add(padding);
    let right = client.right.wrapping_sub(sb_width).wrapping_sub(BORDER_WIDTH);
    let first = client.top.wrapping_add(cy_row / 3);
    let mut index = page_start;
    let mut n = 0u32;
    while index < count && n < rows_in_page {
        let top = first.wrapping_add((n as i32).wrapping_mul(cy_row));
        let bottom = first.wrapping_add(((n as i32).wrapping_add(1)).wrapping_mul(cy_row));
        if contains(&rect(left, top, right, bottom), pt) {
            return Some(n);
        }
        index += 1;
        n += 1;
    }
    None
}

/// `_OnMouseMove`: the scroll bar column (mouse in it shows the full bar).
pub fn scroll_column(client: &Rect, sb_width: i32) -> Rect {
    rect(
        client.right.wrapping_sub(sb_width).wrapping_sub(BORDER_WIDTH),
        client.top,
        client.right,
        client.bottom,
    )
}

/// `_OnMouseMove`: the list area (hand cursor).
pub fn list_area(client: &Rect, sb_width: i32) -> Rect {
    rect(
        client.left,
        client.top,
        client.right.wrapping_sub(sb_width).wrapping_sub(BORDER_WIDTH),
        client.bottom,
    )
}

/// `_DrawPageIndicator`: the "n/m" text rectangle (right aligned).
pub fn page_indicator(client: &Rect, cy_row: i32, rows: i32, sb_width: i32, dpi: u32) -> Rect {
    let h_pad = scale(4, dpi);
    rect(
        client.left,
        (cy_row / 3).wrapping_add(rows.wrapping_mul(cy_row)),
        client.right.wrapping_sub(sb_width.wrapping_add(h_pad)),
        client.bottom,
    )
}

/// `_DrawPageIndicator`: the font height (70 %).
pub fn page_indicator_font_height(lf_height: i32) -> i32 {
    mul_div(lf_height, 70, 100)
}

// ---------------------------------------------------------------------------
// Scroll bar
// ---------------------------------------------------------------------------

/// `CScrollInfo`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ScrollInfo {
    pub max: i32,
    pub page: i32,
    pub pos: i32,
}

/// The parts of the scroll bar (client coordinates).
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ScrollParts {
    pub up: Rect,
    pub down: Rect,
    pub track: Rect,
    pub thumb: Rect,
}

fn button_height(client: &Rect, dpi: u32) -> i32 {
    scale(SCROLLBAR_BTN_HEIGHT, dpi).min(client.height() / 3)
}

/// `_GetBtnUpRect`.
pub fn button_up(client: &Rect, dpi: u32) -> Rect {
    rect(
        client.left,
        client.top,
        client.right,
        client.top.wrapping_add(button_height(client, dpi)),
    )
}

/// `_GetBtnDnRect`.
pub fn button_down(client: &Rect, dpi: u32) -> Rect {
    rect(
        client.left,
        client.bottom.wrapping_sub(button_height(client, dpi)),
        client.right,
        client.bottom,
    )
}

/// `_GetTrackRect`.
pub fn track(client: &Rect, dpi: u32) -> Rect {
    rect(
        client.left,
        button_up(client, dpi).bottom,
        client.right,
        button_down(client, dpi).top,
    )
}

/// Thumb height and offset in a track of `track_h` (`trackH > 0`,
/// `max > page` and `max > 0` checked by the caller where C++ does).
fn thumb_span(track_h: i32, dpi: u32, si: &ScrollInfo) -> (i32, i32) {
    let min_h = scale(SCROLLBAR_THUMB_MIN_H, dpi);
    let thumb_h = mul_div(track_h, si.page, si.max).max(min_h).min(track_h);
    let pos_range = si.max.wrapping_sub(si.page);
    let y = if pos_range > 0 {
        mul_div(track_h.wrapping_sub(thumb_h), si.pos, pos_range)
    } else {
        0
    };
    (thumb_h, y.max(0).min(track_h.wrapping_sub(thumb_h)))
}

/// `_GetThumbRect`.
pub fn thumb(client: &Rect, dpi: u32, si: &ScrollInfo) -> Rect {
    let t = track(client, dpi);
    let track_h = t.height();
    if track_h <= 0 || si.max <= 0 || si.max <= si.page {
        return t;
    }
    let (h, y) = thumb_span(track_h, dpi, si);
    rect(
        t.left,
        t.top.wrapping_add(y),
        t.right,
        t.top.wrapping_add(y).wrapping_add(h),
    )
}

/// All parts at once.
pub fn parts(client: &Rect, dpi: u32, si: &ScrollInfo) -> ScrollParts {
    ScrollParts {
        up: button_up(client, dpi),
        down: button_down(client, dpi),
        track: track(client, dpi),
        thumb: thumb(client, dpi, si),
    }
}

/// `_HitTest`: a `hit::*` value.
pub fn hit_test(client: &Rect, dpi: u32, si: &ScrollInfo, pt: Point) -> i32 {
    let p = parts(client, dpi, si);
    if contains(&p.up, pt) {
        hit::BTN_UP
    } else if contains(&p.down, pt) {
        hit::BTN_DOWN
    } else if contains(&p.thumb, pt) {
        hit::THUMB
    } else if contains(&p.track, pt) {
        if pt.y < p.thumb.top {
            hit::TRACK_ABOVE
        } else {
            hit::TRACK_BELOW
        }
    } else {
        hit::NONE
    }
}

/// `_OnPaint` (idle): the thin line thumb over the whole client height, or
/// `None` for an empty client.  The corner radius is `width / 2`.
pub fn idle_thumb(client: &Rect, dpi: u32, si: &ScrollInfo) -> Option<Rect> {
    let track_h = client.height();
    if track_h <= 0 {
        return None;
    }
    let (h, y) = thumb_span(track_h, dpi, si);
    let line_w = scale(SCROLLBAR_IDLE_THUMB_W, dpi).max(2);
    let cx = client.left.wrapping_add(client.right) / 2;
    let left = cx.wrapping_sub(line_w / 2);
    let top = client.top.wrapping_add(y);
    Some(rect(left, top, left.wrapping_add(line_w), top.wrapping_add(h)))
}

/// The arrows and the pill thumb of the hovered scroll bar.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ScrollArrows {
    pub up: [Point; 3],
    pub down: [Point; 3],
    /// Horizontal extent of the pill thumb (its vertical extent is the thumb's).
    pub pill_left: i32,
    pub pill_right: i32,
    pub pill_radius: i32,
}

/// `_OnPaint` (hovered): the triangles and the pill thumb width.
pub fn arrows(client: &Rect, dpi: u32) -> ScrollArrows {
    let up = button_up(client, dpi);
    let aw = 2.max(up.width() / 3);
    let ah = scale(2, dpi).max(aw.wrapping_mul(87) / 100);
    let acx = up.left.wrapping_add(up.right) / 2;
    let acy = up.top.wrapping_add(ah).wrapping_add(1);
    let up_tri = [
        Point {
            x: acx,
            y: acy.wrapping_sub(ah),
        },
        Point {
            x: acx.wrapping_sub(aw),
            y: acy.wrapping_add(ah),
        },
        Point {
            x: acx.wrapping_add(aw),
            y: acy.wrapping_add(ah),
        },
    ];
    let dn = button_down(client, dpi);
    let acx = dn.left.wrapping_add(dn.right) / 2;
    let acy = dn.bottom.wrapping_sub(ah).wrapping_sub(1);
    let down_tri = [
        Point {
            x: acx,
            y: acy.wrapping_add(ah),
        },
        Point {
            x: acx.wrapping_sub(aw),
            y: acy.wrapping_sub(ah),
        },
        Point {
            x: acx.wrapping_add(aw),
            y: acy.wrapping_sub(ah),
        },
    ];
    let pw = 2.max(aw.wrapping_sub(1));
    ScrollArrows {
        up: up_tri,
        down: down_tri,
        pill_left: acx.wrapping_sub(pw).wrapping_add(1),
        pill_right: acx.wrapping_add(pw).wrapping_add(1),
        pill_radius: pw.wrapping_mul(2),
    }
}

/// `_OnMouseMove` while dragging the thumb: the new position for a cursor
/// `delta_y` pixels from where the drag started, or `None` when the thumb
/// cannot slide.
pub fn drag_position(client: &Rect, dpi: u32, si: &ScrollInfo, start_pos: i32, delta_y: i32) -> Option<i32> {
    let p = parts(client, dpi, si);
    let slide = p.track.height().wrapping_sub(p.thumb.height());
    if slide <= 0 {
        return None;
    }
    let pos_range = si.max.wrapping_sub(si.page);
    let pos = start_pos.wrapping_add(mul_div(delta_y, pos_range, slide));
    Some(pos.max(0).min(pos_range))
}

/// `_SetCurPos`: the stored position.
pub fn clamp_pos(pos: i32, max: i32) -> i32 {
    pos.min(max.wrapping_sub(1)).max(0)
}

/// `_AdjustWindowPos`: the scroll bar relative to a parent of the given size.
pub fn attached_rect(parent_w: i32, parent_h: i32, sb_width: i32) -> Rect {
    let left = parent_w.wrapping_sub(sb_width).wrapping_sub(BORDER_WIDTH);
    rect(
        left,
        BORDER_WIDTH,
        left.wrapping_add(sb_width),
        BORDER_WIDTH.wrapping_add(parent_h.wrapping_sub(BORDER_WIDTH * 2)),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mul_div_rounds_like_win32() {
        assert_eq!(mul_div(16, 144, 96), 24);
        assert_eq!(mul_div(5, 1, 2), 3);
        assert_eq!(mul_div(-5, 1, 2), -3);
        assert_eq!(mul_div(5, -1, 2), -3);
        assert_eq!(mul_div(5, 1, -2), -3);
        assert_eq!(mul_div(-5, -1, -2), -3);
        assert_eq!(mul_div(1, 1, 0), -1);
        assert_eq!(mul_div(i32::MAX, 2, 1), -1);
        assert_eq!(mul_div(0, -3, 7), 0);
        assert_eq!(mul_div(65536, 32768, 1), -1);
        assert_eq!(mul_div(-65536, 32768, 1), -1);
    }

    #[test]
    fn mul_div_int_min_per_architecture() {
        // Measured with the 64-bit and the 32-bit kernel32.
        assert_eq!(mul_div_x64(1, i32::MIN, 2), -1);
        assert_eq!(mul_div_x64(i32::MIN, 0, 5), 0);
        assert_eq!(mul_div_x64(0, 0, i32::MIN), 0);
        assert_eq!(mul_div_x64(1 << 30, 3, i32::MIN), -2);
        assert_eq!(mul_div_x86(1, i32::MIN, 2), -1_073_741_824);
        assert_eq!(mul_div_x86(i32::MIN, 1073741824, 2147483647), -1_073_741_825);
        assert_eq!(mul_div_x86(0, 0, i32::MIN), -1);
        assert_eq!(mul_div_x86(0, -1, i32::MIN), 1);
        assert_eq!(mul_div_x86(1, i32::MAX, i32::MIN), -2);
        assert_eq!(mul_div_x86(65536, 65536, i32::MIN), -3);
        assert_eq!(mul_div_x86(i32::MAX, i32::MAX, i32::MIN), -i32::MAX);
        assert_eq!(mul_div_x86(i32::MIN, i32::MIN, i32::MIN), -1);
        assert_eq!(mul_div_x86(i32::MIN, 1, i32::MIN), 2);
    }

    #[test]
    fn metrics_at_96_dpi() {
        // 2 x "國" measuring 32 x 20.
        let m = metrics(32, 20, 8, 2);
        assert_eq!(m.cx_line, 16);
        assert_eq!(m.cy_line, 25);
        assert_eq!(m.first_line_offset, 8);
        assert_eq!(m.left_padding, 12);
        assert_eq!(m.cx_title, 32 + 12 + 16);
        assert_eq!(m.cy_offset, 2 + 8);
        assert_eq!(initial_metrics(32, 20, 2), (32 + 32, 25, 12));
        assert_eq!(window_size(60, 25, 12, 9, 16), (60 + 16 + 2, 8 + 225 + 12 + 2));
    }

    #[test]
    fn truncation_and_widening() {
        assert_eq!(truncate_item(13), (13, None));
        assert_eq!(truncate_item(20), (13, Some(14)));
        assert!(item_needs_measure(3, 2));
        assert!(!item_needs_measure(2, 2));
        assert_eq!(widen_for_item(3, 50, 28, 60), Some((78, 3)));
        assert_eq!(widen_for_item(3, 30, 28, 60), None);
    }

    #[test]
    fn hit_rows() {
        let c = rect(0, 0, 100, 300);
        // rows start at 25 / 3 = 8.
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 20, y: 8 }), Some(0));
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 20, y: 33 }), Some(1));
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 20, y: 7 }), None);
        // only 5 items: row 5 does not exist.
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 20, y: 8 + 125 }), None);
        // scroll bar column and left padding are outside.
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 83, y: 10 }), None);
        assert_eq!(hit_row(&c, 12, 16, 25, 9, 0, 5, Point { x: 11, y: 10 }), None);
    }

    #[test]
    fn scroll_bar_parts() {
        let c = rect(0, 0, 16, 200);
        let si = ScrollInfo {
            max: 30,
            page: 10,
            pos: 20,
        };
        let p = parts(&c, 96, &si);
        assert_eq!(p.up, rect(0, 0, 16, 14));
        assert_eq!(p.down, rect(0, 186, 16, 200));
        assert_eq!(p.track, rect(0, 14, 16, 186));
        // thumb 172 / 3 = 57, at the end of the track.
        assert_eq!(p.thumb, rect(0, 186 - 57, 16, 186));
        assert_eq!(hit_test(&c, 96, &si, Point { x: 5, y: 20 }), hit::TRACK_ABOVE);
        assert_eq!(hit_test(&c, 96, &si, Point { x: 5, y: 150 }), hit::THUMB);
        assert_eq!(hit_test(&c, 96, &si, Point { x: 5, y: 199 }), hit::BTN_DOWN);
        assert_eq!(drag_position(&c, 96, &si, 20, -1000), Some(0));
        assert_eq!(drag_position(&c, 96, &si, 0, 1000), Some(20));
        // everything fits: thumb is the whole track, no dragging.
        let all = ScrollInfo {
            max: 5,
            page: 10,
            pos: 0,
        };
        assert_eq!(thumb(&c, 96, &all), p.track);
        assert_eq!(drag_position(&c, 96, &all, 0, 10), None);
        assert_eq!(clamp_pos(40, 30), 29);
        assert_eq!(clamp_pos(-3, 30), 0);
    }
}
