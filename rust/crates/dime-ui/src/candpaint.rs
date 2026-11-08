//! GDI drawing of the candidate window (port of `CCandidateWindow::_OnPaint`,
//! `_DrawList` and `_DrawPageIndicator`).  The caller (C++) gathers the rows
//! (candidate strings, selection keys, the selected row), the colors, the
//! font and background brush, the DPI and the scroll bar width; this module
//! issues the same GDI calls in the same order as the C++ original, so the
//! output is pixel-identical (`CandidatePaintParityTest`).  The layout
//! numbers come from [`crate::candwnd`].
//!
//! The paint is split in two because the C++ original resizes the window
//! between the list and the page indicator (which reads the new client
//! rectangle): [`paint_list`], then the caller resizes when told to, then
//! [`paint_page_indicator`] and [`paint_end`].

use crate::candwnd::{self, CandMetrics};
use crate::layout::Rect;
use windows::core::PCWSTR;
use windows::Win32::Foundation::{COLORREF, RECT, SIZE};
use windows::Win32::Graphics::Gdi::{
    CreateFontIndirectW, CreateSolidBrush, DeleteObject, DrawTextW, ExtTextOutW, FillRect, GetObjectW, GetStockObject,
    GetTextExtentPoint32W, GetTextMetricsW, RoundRect, SelectObject, SetBkColor, SetBkMode, SetTextColor, DT_RIGHT,
    DT_SINGLELINE, DT_VCENTER, ETO_OPAQUE, ETO_OPTIONS, LOGFONTW, NULL_PEN, OPAQUE, TEXTMETRICW, TRANSPARENT,
};
pub use windows::Win32::Graphics::Gdi::{HBRUSH, HDC, HFONT, HGDIOBJ};

/// The character `_DrawList` measures for the width of one candidate.
const TEST_CHAR: u16 = 0x570B; // 國
/// The ellipsis between the head and the tail of a truncated item.
const ELLIPSIS: u16 = 0x2026; // …
/// `itemText[MAX_CAND_ITEM_LENGTH + 3]`.
const ITEM_BUFFER: usize = candwnd::MAX_ITEM_LENGTH as usize + 3;

/// One row of the current page.
#[derive(Debug, Clone, Copy)]
pub struct Row<'a> {
    /// The selection key shown in the number column (`CandIndex`).
    pub index_char: u16,
    /// The candidate, `None` when the model has no item there.
    pub text: Option<&'a [u16]>,
}

/// The colors (`COLORREF`) of `CCandidateWindow`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Colors {
    pub number: u32,
    pub number_bk: u32,
    pub text: u32,
    pub bk: u32,
    pub selected_text: u32,
    pub selected_bk: u32,
}

/// The input of [`paint_list`].
#[derive(Debug, Clone, Copy)]
pub struct ListInput<'a> {
    pub font: HFONT,
    /// `_brshBkColor` (may be null).
    pub bk_brush: HBRUSH,
    /// `pps->rcPaint`.
    pub paint: Rect,
    /// `_DrawList` runs (the index range exists and the current page is known).
    pub draw_list: bool,
    pub wnd_width: u32,
    pub sb_width: i32,
    pub dpi: u32,
    /// `_pIndexRange->Count()`.
    pub rows_in_page: i32,
    /// The rows from the first one of the page, at most `rows_in_page`.
    pub rows: &'a [Row<'a>],
    /// The selected row in `rows`, if it is on this page.
    pub selected: Option<usize>,
    pub colors: Colors,
    /// The non-layered scroll bar's origin in candidate client coordinates:
    /// the selection highlight is reported in its coordinates (RC26).
    pub sb_origin: Option<(i32, i32)>,
}

/// The selection highlight to pass to the scroll bar (RC26).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Highlight {
    pub rect: Rect,
    pub radius: i32,
    pub color: u32,
}

/// The result of [`paint_list`].
#[derive(Debug, Clone, Copy, Default)]
pub struct ListOutput {
    /// The font selected before painting; give it back to [`paint_end`].
    pub old_font: HGDIOBJ,
    /// Only when the list was drawn: the new `_cxTitle`, `_cyRow`, `_padding`,
    /// `_wndWidth` and whether `_ResizeWindow` follows.
    pub metrics: Option<ListMetrics>,
    /// Only when the list was drawn with a non-layered scroll bar: reset the
    /// highlight, then set this one (if a row is selected).
    pub highlight: Option<Option<Highlight>>,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ListMetrics {
    pub cx_title: i32,
    pub cy_row: i32,
    pub padding: i32,
    pub wnd_width: u32,
    pub needs_resize: bool,
}

fn rect(r: &Rect) -> RECT {
    RECT {
        left: r.left,
        top: r.top,
        right: r.right,
        bottom: r.bottom,
    }
}

/// `_OnPaint` up to the page indicator: background, then `_DrawList`.
///
/// # Safety
/// `dc` must be a valid device context; `font` and `bk_brush` valid or null.
pub unsafe fn paint_list(dc: HDC, input: &ListInput) -> ListOutput {
    let mut out = ListOutput::default();
    unsafe {
        SetBkMode(dc, TRANSPARENT);
        out.old_font = SelectObject(dc, input.font.into());
        if !input.bk_brush.is_invalid() {
            FillRect(dc, &rect(&input.paint), input.bk_brush);
        }
        if input.draw_list {
            let (metrics, highlight) = draw_list(dc, input);
            out.metrics = Some(metrics);
            out.highlight = input.sb_origin.map(|_| highlight);
        }
    }
    out
}

/// `_DrawList`.
unsafe fn draw_list(dc: HDC, input: &ListInput) -> (ListMetrics, Option<Highlight>) {
    let mut tm = TEXTMETRICW::default();
    let mut cand_size = SIZE::default();
    let mut wnd_width = input.wnd_width;
    unsafe {
        let _ = GetTextMetricsW(dc, &mut tm);
        // don't trust the TextMetrics. Measurement the font height and width directly.
        let test: Vec<u16> = vec![TEST_CHAR; wnd_width as usize];
        let _ = GetTextExtentPoint32W(dc, &test, &mut cand_size);
    }
    let m: CandMetrics = candwnd::metrics(cand_size.cx, cand_size.cy, tm.tmAveCharWidth, wnd_width);
    let mut cx_title = m.cx_title;
    let mut highlight = None;
    let colors = &input.colors;

    let mut index_in_page = 0i32;
    for (i, row) in input.rows.iter().enumerate() {
        if index_in_page >= input.rows_in_page {
            break;
        }
        let r = candwnd::row(&input.paint, index_in_page, &m, input.sb_width, input.dpi);
        let mut rc = rect(&r.number);
        let num = [row.index_char, 0];
        let is_selected = input.selected == Some(i);
        unsafe {
            if is_selected {
                // Selected row: fill entire row with normal bg, then draw rounded highlight
                if !input.bk_brush.is_invalid() {
                    FillRect(dc, &rect(&r.row), input.bk_brush);
                }
                let sel_brush = CreateSolidBrush(COLORREF(colors.selected_bk));
                let sel_pen = GetStockObject(NULL_PEN);
                let old_br = SelectObject(dc, sel_brush.into());
                let old_pen = SelectObject(dc, sel_pen);
                let h = &r.highlight;
                let _ = RoundRect(dc, h.left, h.top, h.right, h.bottom, r.corner_radius, r.corner_radius);
                SelectObject(dc, old_br);
                SelectObject(dc, old_pen);
                let _ = DeleteObject(sel_brush.into());

                if let Some((x, y)) = input.sb_origin {
                    highlight = Some(Highlight {
                        rect: Rect {
                            left: h.left - x,
                            top: h.top - y,
                            right: h.right - x,
                            bottom: h.bottom - y,
                        },
                        radius: r.corner_radius,
                        color: colors.selected_bk,
                    });
                }

                // Draw number with transparent background
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, COLORREF(colors.selected_text));
                let _ = ExtTextOutW(
                    dc,
                    r.number_x,
                    r.text_y,
                    ETO_OPTIONS(0),
                    Some(&rc),
                    PCWSTR(num.as_ptr()),
                    2,
                    None,
                );

                // Set up for candidate text (transparent bg)
                rc = rect(&r.text);
                SetTextColor(dc, COLORREF(colors.selected_text));
            } else {
                // Non-selected row: standard opaque drawing
                SetTextColor(dc, COLORREF(colors.number));
                SetBkColor(dc, COLORREF(colors.number_bk));
                SetBkMode(dc, OPAQUE);
                let _ = ExtTextOutW(
                    dc,
                    r.number_x,
                    r.text_y,
                    ETO_OPAQUE,
                    Some(&rc),
                    PCWSTR(num.as_ptr()),
                    2,
                    None,
                );

                rc = rect(&r.text);
                SetTextColor(dc, COLORREF(colors.text));
                SetBkColor(dc, COLORREF(colors.bk));
            }
        }

        let Some(text) = row.text else {
            index_in_page += 1;
            continue;
        };
        let length = i32::try_from(text.len()).unwrap_or(i32::MAX);
        let (shown, tail) = candwnd::truncate_item(length);
        let item = item_text(text, shown, tail);

        if candwnd::item_needs_measure(shown, wnd_width) {
            let mut size = SIZE::default();
            unsafe {
                let _ = GetTextExtentPoint32W(dc, text, &mut size);
            }
            if let Some((t, w)) = candwnd::widen_for_item(shown, size.cx, r.text_x, cx_title) {
                cx_title = t;
                wnd_width = w;
            }
        }

        let options = if is_selected { ETO_OPTIONS(0) } else { ETO_OPAQUE };
        let shown_len = shown.clamp(0, ITEM_BUFFER as i32) as u32;
        unsafe {
            let _ = ExtTextOutW(
                dc,
                r.text_x,
                r.text_y,
                options,
                Some(&rc),
                PCWSTR(item.as_ptr()),
                shown_len,
                None,
            );
            if is_selected {
                SetBkMode(dc, OPAQUE); // restore for next row
            }
        }
        index_in_page += 1;
    }
    while index_in_page < input.rows_in_page {
        let rc = rect(&candwnd::row(&input.paint, index_in_page, &m, input.sb_width, input.dpi).number);
        if !input.bk_brush.is_invalid() {
            unsafe {
                FillRect(dc, &rc, input.bk_brush);
            }
        }
        index_in_page += 1;
    }

    let metrics = ListMetrics {
        cx_title,
        cy_row: m.cy_line,
        padding: m.padding,
        wnd_width,
        needs_resize: candwnd::needs_resize(cx_title, &input.paint, input.sb_width),
    };
    (metrics, highlight)
}

/// `itemText`: the first `shown` characters, or head + "…" + tail when
/// truncated (`StringCchCatN` / `StringCchCopyN`: copying stops at a NUL).
fn item_text(text: &[u16], shown: i32, tail: Option<i32>) -> [u16; ITEM_BUFFER] {
    let mut buf = [0u16; ITEM_BUFFER];
    let mut n = 0usize;
    let mut cat = |src: &[u16], max: usize, buf: &mut [u16; ITEM_BUFFER]| {
        for &c in src.iter().take(max) {
            if c == 0 || n >= ITEM_BUFFER - 1 {
                break;
            }
            buf[n] = c;
            n += 1;
        }
    };
    match tail {
        Some(t) => {
            let keep = candwnd::TRUNCATED_KEEP as usize;
            cat(text, keep, &mut buf);
            cat(&[ELLIPSIS], 1, &mut buf);
            cat(text.get(t.max(0) as usize..).unwrap_or(&[]), keep, &mut buf);
        }
        None => cat(text, shown.max(0) as usize, &mut buf),
    }
    buf
}

/// The input of [`paint_page_indicator`].
#[derive(Debug, Clone, Copy)]
pub struct IndicatorInput {
    pub font: HFONT,
    /// The current client rectangle (after any resize).
    pub client: Rect,
    pub cy_row: i32,
    pub rows_in_page: i32,
    pub sb_width: i32,
    pub dpi: u32,
    /// 0-based.
    pub current_page: u32,
    pub page_count: u32,
    pub number_color: u32,
}

/// `_DrawPageIndicator` (the caller checks that there is more than one page).
///
/// # Safety
/// `dc` must be a valid device context; `font` valid or null.
pub unsafe fn paint_page_indicator(dc: HDC, input: &IndicatorInput) {
    let text: Vec<u16> = format!("{}/{}", input.current_page.wrapping_add(1), input.page_count)
        .encode_utf16()
        .collect();
    let mut text = text;
    let mut lf = LOGFONTW::default();
    unsafe {
        if GetObjectW(
            input.font.into(),
            std::mem::size_of::<LOGFONTW>() as i32,
            Some(&mut lf as *mut LOGFONTW as *mut _),
        ) == 0
        {
            return;
        }
        // Create a 60% size font for the page indicator
        lf.lfHeight = candwnd::page_indicator_font_height(lf.lfHeight);
        let small = CreateFontIndirectW(&lf);
        if small.is_invalid() {
            return;
        }
        SelectObject(dc, small.into());

        // Right-aligned, compact, with padding from edges
        let mut rc = rect(&candwnd::page_indicator(
            &input.client,
            input.cy_row,
            input.rows_in_page,
            input.sb_width,
            input.dpi,
        ));
        SetTextColor(dc, COLORREF(input.number_color));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, &mut text, &mut rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetBkMode(dc, OPAQUE);

        SelectObject(dc, input.font.into());
        let _ = DeleteObject(small.into());
    }
}

/// The end of `_OnPaint`: select the font that was there before.
///
/// # Safety
/// `dc` must be a valid device context.
pub unsafe fn paint_end(dc: HDC, old_font: HGDIOBJ) {
    unsafe {
        SelectObject(dc, old_font);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(t: &str) -> Vec<u16> {
        t.encode_utf16().collect()
    }

    #[test]
    fn item_text_truncates_and_stops_at_nul() {
        let long = s("0123456789abcdefghij");
        let b = item_text(&long, 13, Some(14));
        assert_eq!(&b[..13], s("012345…efghij").as_slice());
        assert_eq!(b[13], 0);
        let short = s("abc");
        let b = item_text(&short, 3, None);
        assert_eq!(&b[..4], &[b'a' as u16, b'b' as u16, b'c' as u16, 0]);
        let with_nul = [b'a' as u16, 0, b'c' as u16];
        let b = item_text(&with_nul, 3, None);
        assert_eq!(&b[..3], &[b'a' as u16, 0, 0]);
    }
}
