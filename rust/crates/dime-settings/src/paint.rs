//! GDI / GDI+ painting of the settings window (phase 5b-7, Windows only).
//!
//! Call-by-call port of `SettingsWindowPaint::Cpp` (`src/DIMESettings/
//! SettingsWindowPaint.cpp`). The order of GDI calls, the objects selected
//! into the DC, and the GDI+ object lifetimes follow the C++ so the pixels
//! match; `SettingsWindowPaintParityTest` compares them. Raw `extern
//! "system"` bindings keep each call identical to the C++ one.

#![allow(clippy::too_many_arguments)]
// `Handle` is an opaque GDI handle (HDC, HFONT, ...) that is only passed
// back to the system, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::{null, null_mut};

use dime_config::geometry::{blend_color, defaults, mul_div, ThemeColors};
use dime_config::page_layout::{
    ime_cards, load_cards, row_visible, sidebar_items, LayoutCard, LayoutRow, ACT_EXPAND, ACT_FONT, ACT_NAVIGATE,
    CTRL_NONE, MODE_ALL, TY_CLICKABLE, TY_COLOR_GRID, TY_COMBO, TY_PREVIEW, TY_RICH_EDITOR, TY_TOGGLE,
};
use dime_config::settings_model::{mode_bitmask, SettingsSnapshot};
use dime_config::ImeMode;

use crate::layout::{card_rows, ctrl_right, row_height, row_shown, row_spans, scale, RowContext, EXPANDED_SLOTS};
use crate::msgbox::{first_button_x, Layout as MsgBoxLayout};

pub type Handle = *mut c_void;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub const fn new(left: i32, top: i32, right: i32, bottom: i32) -> Rect {
        Rect {
            left,
            top,
            right,
            bottom,
        }
    }
}

#[repr(C)]
#[derive(Default)]
struct Size {
    cx: i32,
    cy: i32,
}

#[repr(C)]
#[derive(Default)]
struct TextMetric {
    tm_height: i32,
    tm_ascent: i32,
    tm_descent: i32,
    tm_internal_leading: i32,
    tm_external_leading: i32,
    tm_ave_char_width: i32,
    tm_max_char_width: i32,
    tm_weight: i32,
    tm_overhang: i32,
    tm_digitized_aspect_x: i32,
    tm_digitized_aspect_y: i32,
    tm_first_char: u16,
    tm_last_char: u16,
    tm_default_char: u16,
    tm_break_char: u16,
    tm_italic: u8,
    tm_underlined: u8,
    tm_struck_out: u8,
    tm_pitch_and_family: u8,
    tm_char_set: u8,
}

#[repr(C)]
#[derive(Default)]
struct BitmapInfo {
    bi_size: u32,
    bi_width: i32,
    bi_height: i32,
    bi_planes: u16,
    bi_bit_count: u16,
    bi_compression: u32,
    bi_size_image: u32,
    bi_x_pels_per_meter: i32,
    bi_y_pels_per_meter: i32,
    bi_clr_used: u32,
    bi_clr_important: u32,
    colors: u32,
}

#[repr(C)]
struct BlendFunction {
    op: u8,
    flags: u8,
    constant_alpha: u8,
    alpha_format: u8,
}

windows_link::link!("gdi32.dll" "system" fn CreatePen(style: i32, width: i32, color: u32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn CreateSolidBrush(color: u32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn SelectObject(hdc: Handle, obj: Handle) -> Handle);
windows_link::link!("gdi32.dll" "system" fn DeleteObject(obj: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn RoundRect(hdc: Handle, l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn CreateRoundRectRgn(l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn FillRgn(hdc: Handle, rgn: Handle, brush: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn MoveToEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32);
windows_link::link!("gdi32.dll" "system" fn LineTo(hdc: Handle, x: i32, y: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn SetBkMode(hdc: Handle, mode: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn SetTextColor(hdc: Handle, color: u32) -> u32);
windows_link::link!("gdi32.dll" "system" fn SetBkColor(hdc: Handle, color: u32) -> u32);
windows_link::link!("gdi32.dll" "system" fn CreateFontW(h: i32, w: i32, esc: i32, orient: i32, weight: i32, italic: u32, underline: u32, strike: u32, charset: u32, out_precision: u32, clip_precision: u32, quality: u32, pitch_and_family: u32, face: *const u16) -> Handle);
windows_link::link!("gdi32.dll" "system" fn GetTextExtentPoint32W(hdc: Handle, s: *const u16, c: i32, size: *mut Size) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetTextMetricsW(hdc: Handle, tm: *mut TextMetric) -> i32);
windows_link::link!("gdi32.dll" "system" fn ExtTextOutW(hdc: Handle, x: i32, y: i32, options: u32, rc: *const Rect, s: *const u16, c: u32, dx: *const i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetStockObject(i: i32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn CreateCompatibleDC(hdc: Handle) -> Handle);
windows_link::link!("gdi32.dll" "system" fn CreateDIBSection(hdc: Handle, bmi: *const BitmapInfo, usage: u32, bits: *mut *mut c_void, section: Handle, offset: u32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn DeleteDC(hdc: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetTextFaceW(hdc: Handle, c: i32, name: *mut u16) -> i32);

windows_link::link!("user32.dll" "system" fn FillRect(hdc: Handle, rc: *const Rect, brush: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn DrawTextW(hdc: Handle, s: *const u16, c: i32, rc: *mut Rect, format: u32) -> i32);
windows_link::link!("user32.dll" "system" fn LoadImageW(inst: Handle, name: *const u16, kind: u32, cx: i32, cy: i32, load: u32) -> Handle);
windows_link::link!("user32.dll" "system" fn DrawIconEx(hdc: Handle, x: i32, y: i32, icon: Handle, cx: i32, cy: i32, step: u32, brush: Handle, flags: u32) -> i32);
windows_link::link!("user32.dll" "system" fn DestroyIcon(icon: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn GetDC(wnd: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(wnd: Handle, hdc: Handle) -> i32);

windows_link::link!("msimg32.dll" "system" fn AlphaBlend(dst: Handle, x: i32, y: i32, w: i32, h: i32, src: Handle, sx: i32, sy: i32, sw: i32, sh: i32, blend: BlendFunction) -> i32);

windows_link::link!("gdiplus.dll" "system" fn GdipCreateFromHDC(hdc: Handle, graphics: *mut Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipDeleteGraphics(graphics: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipSetSmoothingMode(graphics: Handle, mode: i32) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipCreatePath(fill_mode: i32, path: *mut Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipDeletePath(path: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipAddPathArc(path: Handle, x: f32, y: f32, w: f32, h: f32, start: f32, sweep: f32) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipClosePathFigure(path: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipCreateSolidFill(color: u32, brush: *mut Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipDeleteBrush(brush: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipFillPath(graphics: Handle, brush: Handle, path: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipCreatePen1(color: u32, width: f32, unit: i32, pen: *mut Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipDeletePen(pen: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipDrawPath(graphics: Handle, pen: Handle, path: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdipFillEllipse(graphics: Handle, brush: Handle, x: f32, y: f32, w: f32, h: f32) -> i32);

const PS_SOLID: i32 = 0;
const TRANSPARENT: i32 = 1;
const OPAQUE: i32 = 2;
const ETO_OPAQUE: u32 = 2;
const NULL_PEN: i32 = 8;
const DEFAULT_GUI_FONT: i32 = 17;
const DT_LEFT: u32 = 0;
const DT_CENTER: u32 = 1;
const DT_RIGHT: u32 = 2;
const DT_VCENTER: u32 = 4;
const DT_WORDBREAK: u32 = 0x10;
const DT_SINGLELINE: u32 = 0x20;
const DT_CALCRECT: u32 = 0x400;
const IMAGE_ICON: u32 = 1;
const DI_NORMAL: u32 = 3;
const DEFAULT_CHARSET: u32 = 1;
const CLEARTYPE_QUALITY: u32 = 5;
const FF_SWISS: u32 = 0x20;
const FW_NORMAL: i32 = 400;
const FW_SEMIBOLD: i32 = 600;
const FW_BOLD: i32 = 700;
const SMOOTHING_NONE: i32 = 3;
const SMOOTHING_ANTIALIAS: i32 = 4;
const FILL_MODE_ALTERNATE: i32 = 0;
const UNIT_WORLD: i32 = 0;
const LF_FACESIZE: usize = 32;

/// Everything the page painters read (`SettingsWindowPaint::PaintContext`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PaintContext {
    pub theme: ThemeColors,
    pub snapshot: SettingsSnapshot,
    pub font_title: Handle,
    pub font_card_header: Handle,
    pub font_body: Handle,
    pub font_desc: Handle,
    pub font_mdl2: Handle,
    pub font_mdl2_icon: Handle,
    pub brush_sidebar_bg: Handle,
    pub brush_content_bg: Handle,
    pub icon_module: Handle,
    pub dpi: u32,
    pub has_mdl2: i32,
    pub client_w: i32,
    pub client_h: i32,
    pub card_w: i32,
    pub sys_face: u32,
    pub selected_mode_index: i32,
    pub hover_sidebar_index: i32,
    pub nav_level: i32,
    pub current_card_index: i32,
    pub hover_row_index: i32,
    pub current_mode: i32,
    pub scroll_pos: i32,
    pub sidebar_narrow_mode: i32,
    pub sidebar_collapsed: i32,
    pub color_grid_expanded: i32,
    pub card_expanded: [u8; EXPANDED_SLOTS],
    pub toggle_on: [u8; 64],
}

/// `SettingsWindowPaint::Fonts`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Fonts {
    pub title: Handle,
    pub card_header: Handle,
    pub body: Handle,
    pub desc: Handle,
    pub mdl2: Handle,
    pub mdl2_icon: Handle,
    pub has_mdl2: i32,
}

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

fn wz(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// A NUL-terminated UTF-16 string without the NUL, or empty for null.
unsafe fn cstr<'a>(p: *const u16) -> &'a [u16] {
    if p.is_null() {
        return &[];
    }
    let mut n = 0;
    // SAFETY: caller passes a NUL-terminated string.
    while unsafe { *p.add(n) } != 0 {
        n += 1;
    }
    unsafe { std::slice::from_raw_parts(p, n) }
}

fn r_of(c: u32) -> u32 {
    c & 0xff
}
fn g_of(c: u32) -> u32 {
    (c >> 8) & 0xff
}
fn b_of(c: u32) -> u32 {
    (c >> 16) & 0xff
}

/// `Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c))`.
fn argb(c: u32, a: u32) -> u32 {
    (a << 24) | (r_of(c) << 16) | (g_of(c) << 8) | b_of(c)
}

fn lum(c: u32) -> i32 {
    ((299 * r_of(c) + 587 * g_of(c) + 114 * b_of(c)) / 1000) as i32
}

/// `DrawTextW` with an explicit length (the C++ passes -1 on the same text).
/// A readable pointer for `text`: an empty slice's pointer is dangling, and
/// the C++ painters pass `L""` there.
fn text_ptr(text: &[u16]) -> *const u16 {
    static EMPTY: [u16; 1] = [0];
    if text.is_empty() {
        EMPTY.as_ptr()
    } else {
        text.as_ptr()
    }
}

unsafe fn draw_text(hdc: Handle, text: &[u16], rc: &mut Rect, format: u32) -> i32 {
    unsafe { DrawTextW(hdc, text_ptr(text), text.len() as i32, rc, format) }
}

fn text_extent(hdc: Handle, text: &[u16]) -> Size {
    let mut sz = Size::default();
    // SAFETY: valid DC from the caller; text is a live slice.
    unsafe { GetTextExtentPoint32W(hdc, text_ptr(text), text.len() as i32, &mut sz) };
    sz
}

/// RAII for a GDI+ object so lifetimes mirror the C++ wrappers' scopes.
struct Gp {
    handle: Handle,
    delete: unsafe extern "system" fn(Handle) -> i32,
}

impl Drop for Gp {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            // SAFETY: the handle was created by the matching GdipCreate*.
            unsafe { (self.delete)(self.handle) };
        }
    }
}

fn graphics(hdc: Handle) -> Gp {
    let mut h = null_mut();
    // SAFETY: valid DC from the caller.
    unsafe { GdipCreateFromHDC(hdc, &mut h) };
    Gp {
        handle: h,
        delete: GdipDeleteGraphics,
    }
}

fn path() -> Gp {
    let mut h = null_mut();
    // SAFETY: out-pointer is a local.
    unsafe { GdipCreatePath(FILL_MODE_ALTERNATE, &mut h) };
    Gp {
        handle: h,
        delete: GdipDeletePath,
    }
}

fn solid_brush(color: u32) -> Gp {
    let mut h = null_mut();
    // SAFETY: out-pointer is a local.
    unsafe { GdipCreateSolidFill(color, &mut h) };
    Gp {
        handle: h,
        delete: GdipDeleteBrush,
    }
}

fn pen(color: u32, width: f32) -> Gp {
    let mut h = null_mut();
    // SAFETY: out-pointer is a local.
    unsafe { GdipCreatePen1(color, width, UNIT_WORLD, &mut h) };
    Gp {
        handle: h,
        delete: GdipDeletePen,
    }
}

fn s(v: i32, dpi: u32) -> i32 {
    scale(v, dpi)
}

/// `CreateFonts`: the window's six fonts and whether MDL2 resolved.
pub fn create_fonts(dpi_u: u32) -> Fonts {
    let g = defaults();
    let dpi = dpi_u as i32;
    let jh = wz("Microsoft JhengHei UI");
    let mdl2_face = wz("Segoe MDL2 Assets");
    // SAFETY: plain GDI calls with local strings.
    unsafe {
        let make = |pt: i32, weight: i32| {
            CreateFontW(
                -mul_div(pt, dpi, 72),
                0,
                0,
                0,
                weight,
                0,
                0,
                0,
                DEFAULT_CHARSET,
                0,
                0,
                CLEARTYPE_QUALITY,
                FF_SWISS,
                jh.as_ptr(),
            )
        };
        let title = make(g.font_title_pt, FW_SEMIBOLD);
        let card_header = make(g.font_card_header_pt, FW_NORMAL);
        let body = make(g.font_body_pt, FW_NORMAL);
        let desc = make(g.font_desc_pt, FW_NORMAL);
        let mdl2 = CreateFontW(
            -mul_div(g.font_body_pt - 2, dpi, 72),
            0,
            0,
            0,
            FW_BOLD,
            0,
            0,
            0,
            DEFAULT_CHARSET,
            0,
            0,
            CLEARTYPE_QUALITY,
            0,
            mdl2_face.as_ptr(),
        );
        let mdl2_icon = CreateFontW(
            -s(g.row_icon_size, dpi_u),
            0,
            0,
            0,
            FW_NORMAL,
            0,
            0,
            0,
            DEFAULT_CHARSET,
            0,
            0,
            CLEARTYPE_QUALITY,
            0,
            mdl2_face.as_ptr(),
        );
        let mut has_mdl2 = 0;
        if !mdl2.is_null() {
            let dc = GetDC(null_mut());
            let old = SelectObject(dc, mdl2);
            let mut face = [0u16; LF_FACESIZE];
            GetTextFaceW(dc, LF_FACESIZE as i32, face.as_mut_ptr());
            let n = face.iter().position(|&c| c == 0).unwrap_or(LF_FACESIZE);
            has_mdl2 = i32::from(face[..n] == mdl2_face[..mdl2_face.len() - 1]);
            SelectObject(dc, old);
            ReleaseDC(null_mut(), dc);
        }
        Fonts {
            title,
            card_header,
            body,
            desc,
            mdl2,
            mdl2_icon,
            has_mdl2,
        }
    }
}

/// `DrawCardWithShadow`.
pub fn draw_card(
    hdc: Handle,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    corner_r: i32,
    card_bg: u32,
    card_border: u32,
    content_bg: u32,
    is_hover: bool,
    hover_color: u32,
) {
    let bg_is_light = lum(content_bg) > 128;
    // SAFETY: plain GDI / GDI+ calls on the caller's DC.
    unsafe {
        if bg_is_light {
            let g = graphics(hdc);
            GdipSetSmoothingMode(g.handle, SMOOTHING_NONE);
            let r = corner_r as f32;
            let expand = 0;
            let y_off = 1;
            let sp = path();
            let (sx, sy) = ((x - expand) as f32, (y - expand + y_off) as f32);
            let (sw, sh) = ((w + 2 * expand) as f32, (h + 2 * expand) as f32);
            GdipAddPathArc(sp.handle, sx, sy, r, r, 180.0, 90.0);
            GdipAddPathArc(sp.handle, sx + sw - r, sy, r, r, 270.0, 90.0);
            GdipAddPathArc(sp.handle, sx + sw - r, sy + sh - r, r, r, 0.0, 90.0);
            GdipAddPathArc(sp.handle, sx, sy + sh - r, r, r, 90.0, 90.0);
            GdipClosePathFigure(sp.handle);
            let sb = solid_brush(20 << 24);
            GdipFillPath(g.handle, sb.handle, sp.handle);
            drop(sb);
            drop(sp);
            drop(g);
        }

        let h_pen = CreatePen(PS_SOLID, 1, card_border);
        let h_br = CreateSolidBrush(card_bg);
        SelectObject(hdc, h_pen);
        SelectObject(hdc, h_br);
        RoundRect(hdc, x, y, x + w, y + h, corner_r, corner_r);
        DeleteObject(h_br);
        DeleteObject(h_pen);

        if is_hover {
            let h_hov = CreateSolidBrush(hover_color);
            let h_hov_pen = CreatePen(PS_SOLID, 1, hover_color);
            SelectObject(hdc, h_hov);
            SelectObject(hdc, h_hov_pen);
            RoundRect(hdc, x, y, x + w, y + h, corner_r, corner_r);
            DeleteObject(h_hov);
            DeleteObject(h_hov_pen);
        }
    }
}

/// `PaintToggleSwitch`. `is_hover` is unused, as in C++.
pub fn paint_toggle(hdc: Handle, rc: Rect, is_on: bool, tc: &ThemeColors) {
    // SAFETY: plain GDI+ calls on the caller's DC.
    unsafe {
        let g = graphics(hdc);
        GdipSetSmoothingMode(g.handle, SMOOTHING_ANTIALIAS);
        let track_w = rc.right - rc.left;
        let track_h = rc.bottom - rc.top;
        let knob = track_h - 8;
        let inset = 4;
        let radius = track_h as f32;
        let p = path();
        let (tx, ty, tw) = (rc.left as f32, rc.top as f32, track_w as f32);
        GdipAddPathArc(p.handle, tx, ty, radius, radius, 90.0, 180.0);
        GdipAddPathArc(p.handle, tx + tw - radius, ty, radius, radius, 270.0, 180.0);
        GdipClosePathFigure(p.handle);
        if is_on {
            let track = solid_brush(argb(tc.toggle_on_track, 255));
            GdipFillPath(g.handle, track.handle, p.handle);
            let knob_brush = solid_brush(argb(tc.toggle_on_knob, 255));
            GdipFillEllipse(
                g.handle,
                knob_brush.handle,
                (rc.right - knob - inset - 1) as f32,
                (rc.top + inset) as f32,
                knob as f32,
                knob as f32,
            );
        } else {
            let track = pen(argb(tc.toggle_off_border, 255), 1.5);
            GdipDrawPath(g.handle, track.handle, p.handle);
            let knob_brush = solid_brush(argb(tc.toggle_off_knob, 255));
            GdipFillEllipse(
                g.handle,
                knob_brush.handle,
                (rc.left + inset + 1) as f32,
                (rc.top + inset) as f32,
                knob as f32,
                knob as f32,
            );
        }
    }
}

unsafe fn candidate_font(s: &SettingsSnapshot, dpi: u32) -> Handle {
    let mut face = [0u16; LF_FACESIZE];
    let n = s.font_face_name.iter().position(|&c| c == 0).unwrap_or(LF_FACESIZE);
    // CreateFontW reads at most LF_FACESIZE units including the NUL.
    let n = n.min(LF_FACESIZE - 1);
    face[..n].copy_from_slice(&s.font_face_name[..n]);
    unsafe {
        CreateFontW(
            -mul_div(s.font_size as i32, dpi as i32, 72),
            0,
            0,
            0,
            s.font_weight as i32,
            s.font_italic as u32,
            0,
            0,
            DEFAULT_CHARSET,
            0,
            0,
            CLEARTYPE_QUALITY,
            0,
            face.as_ptr(),
        )
    }
}

/// `CandidatePreviewHeight` (`CalcCandidatePreviewH`).
pub fn preview_height(hdc: Handle, s_: &SettingsSnapshot, dpi: u32) -> i32 {
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        let f = candidate_font(s_, dpi);
        if f.is_null() {
            return s(172, dpi);
        }
        let old = SelectObject(hdc, f);
        let sz = text_extent(hdc, &w("國國國國"));
        let cy_line = sz.cy * 5 / 4;
        let first = cy_line / 3;
        let cand_h = 6 * cy_line + first + cy_line / 3;
        SelectObject(hdc, old);
        DeleteObject(f);
        cand_h + s(defaults().preview_gap, dpi)
    }
}

/// SDF shadow (as `ShadowWindow.cpp`) around a `w` x `h` box at `x`, `y`.
unsafe fn preview_shadow(
    hdc: Handle,
    snap: &SettingsSnapshot,
    x: i32,
    y: i32,
    w_: i32,
    h_: i32,
    corner: i32,
    dpi: u32,
    sys_face: u32,
) {
    let sp = s(14, dpi);
    let shadow_w = w_ + sp * 2;
    let shadow_h = h_ + sp * 2;
    let ime_is_dark = lum(snap.item_bg_color) <= 128;
    let bg_is_light = lum(sys_face) > 128;
    let mut shade: u32 = if ime_is_dark { 200 } else { 0 };
    let mut max_alpha: u8 = 35;
    if ime_is_dark && bg_is_light {
        shade = 0;
        max_alpha = 45;
    } else if !ime_is_dark && !bg_is_light {
        shade = 200;
    }
    let r = f64::from(corner) * 0.5;
    let half_w = f64::from(w_) * 0.5;
    let half_h = f64::from(h_) * 0.5;
    let inner_rx = half_w - r;
    let inner_ry = half_h - r;

    // SAFETY: plain GDI calls; the DIB is written inside its bounds.
    unsafe {
        let mem = CreateCompatibleDC(hdc);
        let bi = BitmapInfo {
            bi_size: 40,
            bi_width: shadow_w,
            bi_height: -shadow_h,
            bi_planes: 1,
            bi_bit_count: 32,
            ..Default::default()
        };
        let mut bits: *mut c_void = null_mut();
        let bmp = CreateDIBSection(hdc, &bi, 0, &mut bits, null_mut(), 0);
        if !bmp.is_null() && !bits.is_null() {
            let len = shadow_w as usize * shadow_h as usize;
            let px = std::slice::from_raw_parts_mut(bits as *mut u8, len * 4);
            px.fill(0);
            let old = SelectObject(mem, bmp);
            let fmax = |a: f64, b: f64| if a > b { a } else { b };
            let fmin = |a: f64, b: f64| if a < b { a } else { b };
            for yy in 0..shadow_h {
                for xx in 0..shadow_w {
                    let ox = f64::from(xx - sp);
                    let oy = f64::from(yy - sp);
                    let qx = (ox - half_w).abs() - inner_rx;
                    let qy = (oy - half_h).abs() - inner_ry;
                    let len = (fmax(qx, 0.0) * fmax(qx, 0.0) + fmax(qy, 0.0) * fmax(qy, 0.0)).sqrt();
                    let sdf = len + fmin(fmax(qx, qy), 0.0) - r;
                    if sdf <= -1.0 {
                        continue;
                    }
                    let dist = fmax(0.0, sdf) / f64::from(sp);
                    if dist > 1.0 {
                        continue;
                    }
                    let f = 1.0 - dist;
                    let alpha = (f64::from(max_alpha) * f * f) as u8;
                    if alpha == 0 {
                        continue;
                    }
                    let i = (yy as usize * shadow_w as usize + xx as usize) * 4;
                    let c = (shade * u32::from(alpha) / 255) as u8;
                    px[i] = c;
                    px[i + 1] = c;
                    px[i + 2] = c;
                    px[i + 3] = alpha;
                }
            }
            let bf = BlendFunction {
                op: 0,
                flags: 0,
                constant_alpha: 255,
                alpha_format: 1,
            };
            AlphaBlend(
                hdc,
                x - sp,
                y - sp,
                shadow_w,
                shadow_h,
                mem,
                0,
                0,
                shadow_w,
                shadow_h,
                bf,
            );
            SelectObject(mem, old);
            DeleteObject(bmp);
        }
        DeleteDC(mem);
    }
}

/// `PaintCandidatePreview`: candidate window and notify window samples.
pub fn paint_preview(hdc: Handle, snap: &SettingsSnapshot, margin: i32, cur_y: i32, dpi: u32, sys_face: u32) {
    let g = defaults();
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        let prev_top = cur_y;
        let pad_left = s(g.row_icon_pad_left, dpi);
        let mut font = candidate_font(snap, dpi);
        if font.is_null() {
            font = GetStockObject(DEFAULT_GUI_FONT);
        }
        let old_f = SelectObject(hdc, font);

        let wnd_width = 4;
        let cand_size = text_extent(hdc, &w("國國國國"));
        let mut tm = TextMetric::default();
        GetTextMetricsW(hdc, &mut tm);
        let cx_line = tm.tm_ave_char_width.max(cand_size.cx / wnd_width);
        let cy_line = cand_size.cy * 5 / 4;
        let first = cy_line / 3;
        let left_padding = cy_line / 2;
        let cx_title = cand_size.cx + left_padding + cx_line;

        let nums = ["1", "2", "3", "4", "5", "6"];
        let texts = ["國", "過", "鍋", "裹", "鴿", "擱"];
        let cand_count = 6;
        let cand_h = cand_count * cy_line + first + cy_line / 3;
        let corner = mul_div(8, dpi as i32, 96);
        let prev_x = margin + pad_left;

        preview_shadow(hdc, snap, prev_x, prev_top, cx_title, cand_h, corner, dpi, sys_face);

        let bk_br = CreateSolidBrush(snap.item_bg_color);
        let bk_pen = CreatePen(PS_SOLID, 1, snap.item_bg_color);
        SelectObject(hdc, bk_br);
        SelectObject(hdc, bk_pen);
        RoundRect(
            hdc,
            prev_x,
            prev_top,
            prev_x + cx_title,
            prev_top + cand_h,
            corner,
            corner,
        );
        DeleteObject(bk_br);
        DeleteObject(bk_pen);

        let cy_offset = cand_size.cy / 8 + first;
        for pi in 0..cand_count {
            let mut rc = Rect::default();
            rc.top = prev_top + pi * cy_line + first;
            rc.bottom = rc.top + cy_line;
            let selected = pi == 0;
            let num = w(nums[pi as usize]);
            let text = w(texts[pi as usize]);
            let ty = prev_top + pi * cy_line + cy_offset;
            if selected {
                let full = Rect::new(prev_x, rc.top, prev_x + cx_title, rc.bottom);
                let fr = CreateSolidBrush(snap.item_bg_color);
                FillRect(hdc, &full, fr);
                DeleteObject(fr);

                let v_inset = mul_div(1, dpi as i32, 96);
                let sel_r = mul_div(4, dpi as i32, 96);
                let hl = Rect::new(
                    prev_x + left_padding,
                    rc.top + v_inset,
                    prev_x + cx_title - left_padding,
                    rc.bottom - v_inset,
                );
                let sel_br = CreateSolidBrush(snap.selected_bg_color);
                SelectObject(hdc, sel_br);
                SelectObject(hdc, GetStockObject(NULL_PEN));
                RoundRect(hdc, hl.left, hl.top, hl.right, hl.bottom, sel_r, sel_r);
                DeleteObject(sel_br);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, snap.selected_color);
                rc.left = prev_x + left_padding;
                rc.right = prev_x + left_padding + cx_line;
                ExtTextOutW(hdc, prev_x + left_padding, ty, 0, &rc, num.as_ptr(), 1, null());
                rc.left = prev_x + left_padding + cx_line;
                rc.right = prev_x + cx_title;
                ExtTextOutW(
                    hdc,
                    prev_x + left_padding + cx_line,
                    ty,
                    0,
                    &rc,
                    text.as_ptr(),
                    text.len() as u32,
                    null(),
                );
            } else {
                rc.left = prev_x + left_padding;
                rc.right = prev_x + left_padding + cx_line;
                SetTextColor(hdc, snap.number_color);
                SetBkColor(hdc, snap.item_bg_color);
                SetBkMode(hdc, OPAQUE);
                ExtTextOutW(hdc, prev_x + left_padding, ty, ETO_OPAQUE, &rc, num.as_ptr(), 1, null());
                rc.left = prev_x + left_padding + cx_line;
                rc.right = prev_x + cx_title;
                SetTextColor(hdc, snap.item_color);
                SetBkColor(hdc, snap.item_bg_color);
                ExtTextOutW(
                    hdc,
                    prev_x + left_padding + cx_line,
                    ty,
                    ETO_OPAQUE,
                    &rc,
                    text.as_ptr(),
                    text.len() as u32,
                    null(),
                );
            }
            if selected {
                SetBkMode(hdc, OPAQUE);
            }
        }

        let notify = w("中文");
        let n_len = notify.len() as i32;
        let n_size = text_extent(hdc, &notify);
        let notify_w = n_size.cx + n_size.cx / n_len;
        let notify_h = n_size.cy * 3 / 2;
        let notify_x = prev_x + cx_title + s(20, dpi);
        let notify_y = prev_top + (cand_h - notify_h) / 2;
        let n_corner = mul_div(8, dpi as i32, 96);

        preview_shadow(
            hdc, snap, notify_x, notify_y, notify_w, notify_h, n_corner, dpi, sys_face,
        );

        let nb = CreateSolidBrush(snap.item_bg_color);
        let np = CreatePen(PS_SOLID, 1, snap.item_bg_color);
        SelectObject(hdc, nb);
        SelectObject(hdc, np);
        RoundRect(
            hdc,
            notify_x,
            notify_y,
            notify_x + notify_w,
            notify_y + notify_h,
            n_corner,
            n_corner,
        );
        DeleteObject(nb);
        DeleteObject(np);

        SetTextColor(hdc, snap.item_color);
        SetBkMode(hdc, TRANSPARENT);
        ExtTextOutW(
            hdc,
            notify_x + n_size.cx / n_len / 2,
            notify_y + notify_h / 5,
            0,
            null(),
            notify.as_ptr(),
            n_len as u32,
            null(),
        );

        SelectObject(hdc, old_f);
        if font != GetStockObject(DEFAULT_GUI_FONT) {
            DeleteObject(font);
        }
    }
}

/// `PaintSidebar`.
pub fn paint_sidebar(hdc: Handle, ctx: &PaintContext) {
    if ctx.sidebar_collapsed != 0 {
        return;
    }
    let g = defaults();
    let dpi = ctx.dpi;
    let tc = &ctx.theme;
    let sidebar_w = s(g.sidebar_width, dpi);
    let item_h = s(g.sidebar_item_h, dpi);
    let item_r = s(g.sidebar_item_r, dpi);
    let icon_size = s(g.sidebar_icon_size, dpi);
    let icon_gap = s(g.sidebar_icon_gap, dpi);
    let pad_l = s(g.sidebar_item_pad_left, dpi);
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        FillRect(hdc, &Rect::new(0, 0, sidebar_w, ctx.client_h), ctx.brush_sidebar_bg);
        SetBkMode(hdc, TRANSPARENT);
        let old_font = SelectObject(hdc, ctx.font_body);
        let start_y = s(g.sidebar_start_y, dpi);
        for (i, item) in sidebar_items().iter().enumerate() {
            let i = i as i32;
            if item.icon_resource_id == -1 {
                let sep_y = start_y + i * item_h + item_h / 2;
                let sep = CreatePen(PS_SOLID, 1, tc.card_border);
                let old_pen = SelectObject(hdc, sep);
                MoveToEx(hdc, pad_l, sep_y, null_mut());
                LineTo(hdc, sidebar_w - pad_l, sep_y);
                SelectObject(hdc, old_pen);
                DeleteObject(sep);
                continue;
            }
            let item_pad_x = s(g.sidebar_item_r, dpi);
            let rc_item = Rect::new(
                item_pad_x,
                start_y + i * item_h,
                sidebar_w - item_pad_x,
                start_y + (i + 1) * item_h,
            );
            let selected = i == ctx.selected_mode_index;
            if selected || i == ctx.hover_sidebar_index {
                let hl = if selected { tc.sidebar_select } else { tc.sidebar_hover };
                let br = CreateSolidBrush(hl);
                let pn = CreatePen(PS_SOLID, 1, hl);
                SelectObject(hdc, br);
                SelectObject(hdc, pn);
                RoundRect(
                    hdc,
                    rc_item.left,
                    rc_item.top,
                    rc_item.right,
                    rc_item.bottom,
                    item_r * 2,
                    item_r * 2,
                );
                DeleteObject(br);
                DeleteObject(pn);
            }
            if selected {
                let bar_w = s(g.sidebar_accent_bar_w, dpi);
                let bar_h = s(g.sidebar_accent_bar_h, dpi);
                let bar_x = rc_item.left + s(g.sidebar_item_r, dpi);
                let bar_y = rc_item.top + (item_h - bar_h) / 2;
                let bar_r = s(g.sidebar_accent_bar_r, dpi);
                let acc = CreateSolidBrush(tc.accent);
                let rgn = CreateRoundRectRgn(bar_x, bar_y, bar_x + bar_w + 1, bar_y + bar_h + 1, bar_r, bar_r);
                FillRgn(hdc, rgn, acc);
                DeleteObject(rgn);
                DeleteObject(acc);
            }
            let icon_x = rc_item.left + pad_l;
            let icon_y = rc_item.top + (item_h - icon_size) / 2;
            if item.icon_resource_id > 0 {
                let icon = LoadImageW(
                    ctx.icon_module,
                    item.icon_resource_id as usize as *const u16,
                    IMAGE_ICON,
                    icon_size,
                    icon_size,
                    0,
                );
                if !icon.is_null() {
                    DrawIconEx(
                        hdc,
                        icon_x,
                        icon_y,
                        icon,
                        icon_size,
                        icon_size,
                        0,
                        null_mut(),
                        DI_NORMAL,
                    );
                    DestroyIcon(icon);
                }
            }
            SetTextColor(hdc, tc.text_primary);
            let mut rc_text = rc_item;
            rc_text.left = icon_x + icon_size + icon_gap;
            draw_text(
                hdc,
                cstr(item.label),
                &mut rc_text,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT,
            );
        }
        SelectObject(hdc, old_font);
    }
}

fn chevron(expanded: bool, has_mdl2: bool) -> Vec<u16> {
    match (expanded, has_mdl2) {
        (true, true) => vec![0xE70E],
        (true, false) => vec![0x2227],
        (false, true) => vec![0xE70D],
        (false, false) => vec![0x2228],
    }
}

/// `PaintRow`: one row of any type on any page.
unsafe fn paint_row(
    hdc: Handle,
    ctx: &PaintContext,
    lr: &LayoutRow,
    margin: i32,
    cur_y: i32,
    card_w: i32,
    this_h: i32,
    ctrl_right_edge: i32,
    dpi: u32,
    is_child: bool,
) {
    let g = defaults();
    let tc = &ctx.theme;
    let pad = s(g.card_pad, dpi);
    let icon_sz = s(g.row_icon_size, dpi);
    let icon_pad_l = s(g.row_icon_pad_left, dpi);
    let icon_gap = s(g.row_icon_gap, dpi);
    let sep_indent = s(g.row_sep_indent, dpi);
    let has_mdl2 = ctx.has_mdl2 != 0;

    if lr.row_type == TY_PREVIEW {
        paint_preview(hdc, &ctx.snapshot, margin, cur_y, dpi, ctx.sys_face);
        return;
    }

    // SAFETY: plain GDI calls on the caller's DC; strings come from the tables.
    unsafe {
        let has_icon = lr.icon_glyph != 0 || lr.icon_color != 0;
        let child_icon_start = margin + icon_pad_l + icon_sz + icon_gap;
        if lr.row_type != TY_COLOR_GRID && lr.row_type != TY_RICH_EDITOR && has_icon {
            let ix = if is_child {
                child_icon_start
            } else {
                margin + icon_pad_l
            };
            let iy = cur_y + (this_h - icon_sz) / 2;
            if lr.icon_glyph != 0 && !ctx.font_mdl2_icon.is_null() && has_mdl2 {
                let old = SelectObject(hdc, ctx.font_mdl2_icon);
                SetTextColor(hdc, tc.text_primary);
                let mut rc = Rect::new(ix, iy, ix + icon_sz, iy + icon_sz);
                draw_text(hdc, &[lr.icon_glyph], &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(hdc, old);
            } else if lr.icon_color != 0 {
                let br = CreateSolidBrush(lr.icon_color);
                let pn = CreatePen(PS_SOLID, 1, lr.icon_color);
                SelectObject(hdc, br);
                SelectObject(hdc, pn);
                RoundRect(hdc, ix, iy, ix + icon_sz, iy + icon_sz, icon_sz / 4, icon_sz / 4);
                DeleteObject(br);
                DeleteObject(pn);
            }
        }

        let label_left = if is_child {
            if has_icon {
                child_icon_start + icon_sz + icon_gap
            } else {
                margin + sep_indent
            }
        } else {
            margin + icon_pad_l + icon_sz + icon_gap
        };

        if lr.row_type == TY_RICH_EDITOR {
            let hdr_h = s(g.editor_header_h / 2, dpi);
            SelectObject(hdc, ctx.font_card_header);
            SetTextColor(hdc, tc.text_primary);
            let mut rh = Rect::new(margin + pad, cur_y + pad / 2, margin + card_w - pad, cur_y + hdr_h);
            draw_text(hdc, cstr(lr.label), &mut rh, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            if !lr.description.is_null() {
                SelectObject(hdc, ctx.font_desc);
                SetTextColor(hdc, tc.text_secondary);
                let mut ri = Rect::new(
                    margin + pad,
                    cur_y + hdr_h,
                    margin + card_w - pad,
                    cur_y + s(g.editor_header_h, dpi),
                );
                draw_text(hdc, cstr(lr.description), &mut ri, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            }
            return;
        }

        if lr.row_type == TY_COLOR_GRID {
            let labels = [
                "候選字文字",
                "選取文字",
                "候選字背景",
                "聯想詞文字",
                "編號文字",
                "選取背景",
            ];
            let sw = s(g.swatch_size, dpi);
            let sw_gap = s(g.swatch_cell_gap, dpi);
            let cell_h = sw + sw_gap;
            SelectObject(hdc, ctx.font_desc);
            let first = w(labels[0]);
            let max_lbl_w = text_extent(hdc, &first[..5]).cx;
            let gap = max_lbl_w / 5;
            let col_w = (ctrl_right_edge - margin) / 3;
            for (si, label) in labels.iter().enumerate() {
                let si = si as i32;
                let (col, row) = (si % 3, si / 3);
                let col_right = margin + (col + 1) * col_w;
                let label_x = col_right - sw - gap - max_lbl_w;
                let sy = cur_y + row * cell_h;
                SetTextColor(hdc, tc.text_secondary);
                let mut rc = Rect::new(label_x, sy, label_x + max_lbl_w, sy + sw);
                draw_text(hdc, &w(label), &mut rc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            }
            return;
        }

        SelectObject(hdc, if is_child { ctx.font_body } else { ctx.font_card_header });
        SetTextColor(hdc, tc.text_primary);
        let has_desc = !lr.description.is_null();
        let lt = cur_y + s(if has_desc { g.row_label_top_off } else { 0 }, dpi);
        let lb = if has_desc {
            cur_y + this_h / 2 + s(g.row_label_mid_off, dpi)
        } else {
            cur_y + this_h
        };
        let mut rc_l = Rect::new(label_left, lt, ctrl_right_edge, lb);
        if lr.row_type == TY_CLICKABLE && lr.action == ACT_FONT {
            let face_n = ctx
                .snapshot
                .font_face_name
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(LF_FACESIZE);
            let mut text: Vec<u16> = cstr(lr.label).to_vec();
            text.extend(w("  —  "));
            text.extend_from_slice(&ctx.snapshot.font_face_name[..face_n]);
            text.extend(w(&format!(", {}pt", ctx.snapshot.font_size)));
            text.truncate(127); // StringCchPrintfW into WCHAR[128]
            draw_text(hdc, &text, &mut rc_l, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        } else {
            draw_text(hdc, cstr(lr.label), &mut rc_l, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        if has_desc {
            SelectObject(hdc, ctx.font_desc);
            SetTextColor(hdc, tc.text_secondary);
            let mut rc_d = Rect::new(
                label_left,
                cur_y + this_h / 2 - s(g.row_label_mid_off, dpi),
                rc_l.right,
                cur_y + this_h - s(g.row_desc_bot_off, dpi),
            );
            draw_text(
                hdc,
                cstr(lr.description),
                &mut rc_d,
                DT_SINGLELINE | DT_LEFT | DT_VCENTER,
            );
        }

        if lr.action == ACT_EXPAND && lr.row_type == TY_COMBO && ctx.snapshot.color_mode == 3 {
            let chev_area = s(g.chevron_w + 4, dpi);
            if has_mdl2 && !ctx.font_mdl2.is_null() {
                SelectObject(hdc, ctx.font_mdl2);
            } else {
                SelectObject(hdc, ctx.font_body);
            }
            SetTextColor(hdc, tc.text_primary);
            let mut rc = Rect::new(
                ctrl_right_edge + s(4, dpi),
                cur_y,
                ctrl_right_edge + chev_area,
                cur_y + this_h,
            );
            draw_text(
                hdc,
                &chevron(ctx.color_grid_expanded != 0, has_mdl2),
                &mut rc,
                DT_SINGLELINE | DT_VCENTER | DT_CENTER,
            );
        }

        if lr.row_type == TY_TOGGLE {
            let tw = s(g.toggle_w, dpi);
            let th = s(g.toggle_h, dpi);
            let tx = ctrl_right_edge - tw;
            let ty = cur_y + (this_h - th) / 2;
            let is_on = usize::try_from(lr.id)
                .ok()
                .and_then(|i| ctx.toggle_on.get(i))
                .is_some_and(|&b| b != 0);
            paint_toggle(hdc, Rect::new(tx, ty, tx + tw, ty + th), is_on, tc);
            SetTextColor(hdc, tc.text_primary);
            SelectObject(hdc, ctx.font_body);
            let mut rc = Rect::new(
                tx - s(g.toggle_label_w, dpi),
                ty,
                tx - s(g.toggle_label_gap_r, dpi),
                ty + th,
            );
            draw_text(
                hdc,
                &w(if is_on { "開啟" } else { "關閉" }),
                &mut rc,
                DT_SINGLELINE | DT_RIGHT | DT_VCENTER,
            );
        }
    }
}

fn card_visible(card: &LayoutCard, mask: i32) -> bool {
    card.row_count == 0 || card_rows(card).iter().any(|r| row_visible(r.visible_modes, mask))
}

fn expanded(ctx: &PaintContext, idx: i32) -> bool {
    usize::try_from(idx)
        .ok()
        .and_then(|i| ctx.card_expanded.get(i))
        .is_some_and(|&b| b != 0)
}

unsafe fn paint_card_list(hdc: Handle, ctx: &PaintContext, title: &[u16], cards: &[LayoutCard], mask: i32, base: i32) {
    let g = defaults();
    let dpi = ctx.dpi;
    let tc = &ctx.theme;
    let margin = s(g.content_margin, dpi);
    let pad = s(g.card_pad, dpi);
    let cat_row_h = s(g.category_row_h, dpi);
    let corner_r = s(g.card_radius, dpi);
    let icon_sz = s(g.row_icon_size, dpi);
    let icon_pad_l = s(g.row_icon_pad_left, dpi);
    let icon_gap = s(g.row_icon_gap, dpi);
    let narrow = ctx.sidebar_narrow_mode != 0;
    let has_mdl2 = ctx.has_mdl2 != 0;

    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        SetBkMode(hdc, TRANSPARENT);
        let old_font = SelectObject(hdc, ctx.font_title);
        SetTextColor(hdc, tc.text_primary);
        let title_left = if narrow { s(g.hamburger_size, dpi) } else { margin };
        let (title_y, title_b) = if narrow {
            (-ctx.scroll_pos, s(g.hamburger_size, dpi) - ctx.scroll_pos)
        } else {
            let y = s(g.top_pad, dpi) - ctx.scroll_pos;
            (y, y + s(g.title_area_h, dpi))
        };
        let mut rc_title = Rect::new(title_left, title_y, ctx.client_w - margin, title_b);
        draw_text(hdc, title, &mut rc_title, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        let card_w = ctx.card_w;
        let gap = s(g.card_gap, dpi);
        let mut cur_y = s(g.top_margin, dpi) - ctx.scroll_pos;

        for (i, card) in cards.iter().enumerate() {
            if !card_visible(card, mask) {
                continue;
            }
            let c_row_h = s(g.child_row_h, dpi);
            let exp_idx = base + i as i32;
            let is_open = card.action == ACT_EXPAND && expanded(ctx, exp_idx) && !card.rows.is_null();
            let num_children = if is_open {
                card_rows(card)
                    .iter()
                    .filter(|r| row_visible(r.visible_modes, mask))
                    .count() as i32
            } else {
                0
            };
            let card_h = 2 * pad
                + cat_row_h
                + if num_children > 0 {
                    num_children * (c_row_h + 1)
                } else {
                    0
                };

            let is_hover = i as i32 == ctx.hover_row_index;
            if num_children > 0 {
                draw_card(
                    hdc,
                    margin,
                    cur_y,
                    card_w,
                    card_h,
                    corner_r,
                    tc.card_bg,
                    tc.card_border,
                    tc.content_bg,
                    false,
                    tc.row_hover,
                );
                if is_hover {
                    let br = CreateSolidBrush(tc.row_hover);
                    let pn = CreatePen(PS_SOLID, 1, tc.row_hover);
                    SelectObject(hdc, br);
                    SelectObject(hdc, pn);
                    RoundRect(
                        hdc,
                        margin,
                        cur_y,
                        margin + card_w,
                        cur_y + 2 * pad + cat_row_h,
                        corner_r,
                        corner_r,
                    );
                    DeleteObject(br);
                    DeleteObject(pn);
                }
            } else {
                draw_card(
                    hdc,
                    margin,
                    cur_y,
                    card_w,
                    card_h,
                    corner_r,
                    tc.card_bg,
                    tc.card_border,
                    tc.content_bg,
                    is_hover,
                    tc.row_hover,
                );
            }

            let row_top = cur_y + pad;
            let label_left = margin + icon_pad_l + icon_sz + icon_gap;
            {
                let (ix, iy) = (margin + icon_pad_l, row_top + (cat_row_h - icon_sz) / 2);
                if card.icon_glyph != 0 && !ctx.font_mdl2_icon.is_null() && has_mdl2 {
                    let old = SelectObject(hdc, ctx.font_mdl2_icon);
                    SetTextColor(hdc, tc.text_primary);
                    let mut rc = Rect::new(ix, iy, ix + icon_sz, iy + icon_sz);
                    draw_text(hdc, &[card.icon_glyph], &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    SelectObject(hdc, old);
                } else if card.icon_color != 0 {
                    let br = CreateSolidBrush(card.icon_color);
                    let pn = CreatePen(PS_SOLID, 1, card.icon_color);
                    SelectObject(hdc, br);
                    SelectObject(hdc, pn);
                    RoundRect(hdc, ix, iy, ix + icon_sz, iy + icon_sz, icon_sz / 4, icon_sz / 4);
                    DeleteObject(br);
                    DeleteObject(pn);
                }
            }

            SelectObject(hdc, ctx.font_card_header);
            SetTextColor(hdc, tc.text_primary);
            let label_right = margin + card_w - pad - s(g.chevron_w + g.row_label_mid_off, dpi);
            let mut rc_l = Rect::new(
                label_left,
                row_top + s(g.row_label_top_off, dpi),
                label_right,
                row_top + cat_row_h / 2 + s(g.row_label_mid_off, dpi),
            );
            draw_text(hdc, cstr(card.title), &mut rc_l, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            if !card.description.is_null() {
                SelectObject(hdc, ctx.font_desc);
                SetTextColor(hdc, tc.text_secondary);
                let mut rc_d = Rect::new(
                    label_left,
                    row_top + cat_row_h / 2 - s(g.row_label_mid_off, dpi),
                    label_right,
                    row_top + cat_row_h - s(g.row_desc_bot_off, dpi),
                );
                draw_text(
                    hdc,
                    cstr(card.description),
                    &mut rc_d,
                    DT_SINGLELINE | DT_LEFT | DT_VCENTER,
                );
            }

            if has_mdl2 && !ctx.font_mdl2.is_null() {
                SelectObject(hdc, ctx.font_mdl2);
            } else {
                SelectObject(hdc, ctx.font_body);
            }
            SetTextColor(hdc, tc.text_primary);
            let mut rc_chev = Rect::new(
                margin + card_w - pad - s(g.chevron_w, dpi),
                row_top,
                margin + card_w - pad,
                row_top + cat_row_h,
            );
            if card.action == ACT_NAVIGATE {
                let glyph = if has_mdl2 { vec![0xE76C] } else { w(">") };
                draw_text(hdc, &glyph, &mut rc_chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            } else if card.action == ACT_EXPAND {
                draw_text(
                    hdc,
                    &chevron(expanded(ctx, exp_idx), has_mdl2),
                    &mut rc_chev,
                    DT_SINGLELINE | DT_VCENTER | DT_CENTER,
                );
            }

            if is_open {
                let mut child_y = row_top + cat_row_h + pad;
                for lr in card_rows(card) {
                    if !row_visible(lr.visible_modes, mask) {
                        continue;
                    }
                    let sep = CreatePen(PS_SOLID, 1, blend_color(tc.card_bg, tc.text_secondary, 40));
                    SelectObject(hdc, sep);
                    MoveToEx(hdc, margin + 1, child_y, null_mut());
                    LineTo(hdc, margin + card_w - 1, child_y);
                    DeleteObject(sep);
                    child_y += 1;
                    let child_ctrl_right = ctrl_right(margin, card_w, dpi, false);
                    paint_row(
                        hdc,
                        ctx,
                        lr,
                        margin,
                        child_y,
                        card_w,
                        c_row_h,
                        child_ctrl_right,
                        dpi,
                        true,
                    );
                    child_y += c_row_h;
                }
            }
            cur_y += card_h + gap;
        }

        let cover = Rect::new(0, 0, ctx.client_w, s(g.top_margin, dpi));
        FillRect(hdc, &cover, ctx.brush_content_bg);
        SelectObject(hdc, ctx.font_title);
        SetTextColor(hdc, tc.text_primary);
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, ctx.font_title);
        SetTextColor(hdc, tc.text_primary);
        let mut rc_title2 = if narrow {
            Rect::new(title_left, 0, ctx.client_w - margin, s(g.hamburger_size, dpi))
        } else {
            Rect::new(
                title_left,
                s(g.top_pad, dpi),
                ctx.client_w - margin,
                s(g.top_pad + g.title_area_h, dpi),
            )
        };
        draw_text(hdc, title, &mut rc_title2, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        SelectObject(hdc, old_font);
    }
}

unsafe fn paint_settings_detail(hdc: Handle, ctx: &PaintContext) {
    let g = defaults();
    let dpi = ctx.dpi;
    let tc = &ctx.theme;
    let margin = s(g.content_margin, dpi);
    let pad = s(g.card_pad, dpi);
    let corner_r = s(g.card_radius, dpi);
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        SetBkMode(hdc, TRANSPARENT);
        let cards = ime_cards();
        let Some(card) = usize::try_from(ctx.current_card_index).ok().and_then(|i| cards.get(i)) else {
            return;
        };
        let mask = mode_bitmask(ImeMode::from_i32(ctx.current_mode));
        let card_w = ctx.card_w;
        let ctrl_right_x = ctrl_right(margin, card_w, dpi, true);
        let row_ctx = RowContext {
            dpi,
            scroll_pos: ctx.scroll_pos,
            mode_mask: mask,
            color_mode: ctx.snapshot.color_mode,
            grid_expanded: i32::from(ctx.color_grid_expanded != 0),
            preview_h: preview_height(hdc, &ctx.snapshot, dpi),
        };
        let spans = row_spans(card, &row_ctx);
        let rows = card_rows(card);

        SetBkMode(hdc, TRANSPARENT);
        let old_font = SelectObject(hdc, ctx.font_body);
        let gap = s(g.card_gap, dpi);
        let mut card_top = s(g.top_pad, dpi) - ctx.scroll_pos;
        for span in spans {
            let start = span.start_row as usize;
            let is_preview = start < rows.len() && rows[start].row_type == TY_PREVIEW;
            if !is_preview {
                let hover = span.start_row == ctx.hover_row_index;
                draw_card(
                    hdc,
                    margin,
                    card_top,
                    card_w,
                    span.height,
                    corner_r,
                    tc.card_bg,
                    tc.card_border,
                    tc.content_bg,
                    hover,
                    tc.row_hover,
                );
            }
            let mut cur_y = card_top + pad;
            for lr in &rows[start..span.end_row as usize] {
                if !row_shown(lr, mask, row_ctx.color_mode, row_ctx.grid_expanded != 0) {
                    continue;
                }
                let h = row_height(lr, dpi, row_ctx.preview_h);
                paint_row(
                    hdc,
                    ctx,
                    lr,
                    margin,
                    cur_y,
                    card_w,
                    h,
                    ctrl_right_x,
                    dpi,
                    lr.expand_parent != CTRL_NONE,
                );
                cur_y += h;
            }
            card_top += span.height + gap;
        }
        SelectObject(hdc, old_font);
    }
}

fn page_title(ctx: &PaintContext) -> Vec<u16> {
    let items = sidebar_items();
    match usize::try_from(ctx.selected_mode_index).ok().and_then(|i| items.get(i)) {
        // SAFETY: table strings are static.
        Some(item) if !item.label.is_null() => unsafe { cstr(item.label) }.to_vec(),
        _ => w("DIME"),
    }
}

/// `PaintContent`: background plus the card list or the sub-page.
pub fn paint_content(hdc: Handle, ctx: &PaintContext) {
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        FillRect(hdc, &Rect::new(0, 0, ctx.client_w, ctx.client_h), ctx.brush_content_bg);
        if ctx.selected_mode_index == 5 {
            paint_card_list(hdc, ctx, &w("載入碼表"), load_cards(), MODE_ALL, 4);
        } else if ctx.nav_level == 0 {
            let mask = mode_bitmask(ImeMode::from_i32(ctx.current_mode));
            paint_card_list(hdc, ctx, &page_title(ctx), ime_cards(), mask, 0);
        } else {
            paint_settings_detail(hdc, ctx);
        }
    }
}

/// `PaintBreadcrumb`: mode name › category name.
pub fn paint_breadcrumb(hdc: Handle, ctx: &PaintContext) {
    let g = defaults();
    let dpi = ctx.dpi;
    let margin = s(g.content_margin, dpi);
    let bc_h = s(g.breadcrumb_h, dpi);
    let tc = &ctx.theme;
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        SetBkMode(hdc, TRANSPARENT);
        let old_font = SelectObject(hdc, ctx.font_title);
        SetTextColor(hdc, tc.text_primary);
        let mode_name = page_title(ctx);
        let bc_left = if ctx.sidebar_narrow_mode != 0 {
            s(g.hamburger_size, dpi)
        } else {
            margin
        };
        let mut rc_m = Rect::new(bc_left, 0, bc_left + s(g.bc_mode_name_w, dpi), bc_h);
        draw_text(
            hdc,
            &mode_name,
            &mut rc_m,
            DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT,
        );
        let mode_right = rc_m.right;
        let mut rc_m = Rect::new(bc_left, 0, mode_right, bc_h);
        draw_text(hdc, &mode_name, &mut rc_m, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        SetTextColor(hdc, tc.text_secondary);
        let sep = w("  \u{203A}  ");
        let mut rc_s = Rect::new(mode_right, 0, mode_right + s(g.bc_sep_w, dpi), bc_h);
        draw_text(hdc, &sep, &mut rc_s, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT);
        let sep_right = rc_s.right;
        let mut rc_s = Rect::new(mode_right, 0, sep_right, bc_h);
        draw_text(hdc, &sep, &mut rc_s, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        SetTextColor(hdc, tc.text_primary);
        let cards = ime_cards();
        let cat: &[u16] = match usize::try_from(ctx.current_card_index).ok().and_then(|i| cards.get(i)) {
            Some(c) => cstr(c.title),
            None => &[],
        };
        let mut rc_c = Rect::new(sep_right, 0, sep_right + s(g.bc_cat_name_w, dpi), bc_h);
        draw_text(hdc, cat, &mut rc_c, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        SelectObject(hdc, old_font);
    }
}

/// `PaintHamburger`: background and the ☰ glyph.
pub fn paint_hamburger(hdc: Handle, ctx: &PaintContext, rc: Rect, bg: Handle) {
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        FillRect(hdc, &rc, bg);
        SetBkMode(hdc, TRANSPARENT);
        let mut rc = rc;
        if ctx.has_mdl2 != 0 && !ctx.font_mdl2_icon.is_null() {
            SelectObject(hdc, ctx.font_mdl2_icon);
            SetTextColor(hdc, ctx.theme.text_primary);
            draw_text(hdc, &[0xE700], &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            SelectObject(hdc, ctx.font_card_header);
            SetTextColor(hdc, ctx.theme.text_primary);
            draw_text(hdc, &[0x2261], &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
    }
}

/// `PaintOwnerButton`: Win11 rounded button. `text` without the NUL.
pub fn paint_button(
    hdc: Handle,
    rc: Rect,
    text: &[u16],
    pressed: bool,
    disabled: bool,
    tc: &ThemeColors,
    font: Handle,
    dpi: u32,
) {
    let r = s(4, dpi) as f32;
    let fill = if disabled {
        tc.button_fill_disabled
    } else if pressed {
        tc.button_fill_pressed
    } else {
        tc.button_fill
    };
    let text_c = if disabled {
        tc.button_text_disabled
    } else {
        tc.button_text
    };
    // SAFETY: plain GDI / GDI+ calls on the caller's DC.
    unsafe {
        let bg = CreateSolidBrush(tc.card_bg);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        let g = graphics(hdc);
        GdipSetSmoothingMode(g.handle, SMOOTHING_ANTIALIAS);
        let p = path();
        let (x, y) = (rc.left as f32, rc.top as f32);
        let (ww, hh) = ((rc.right - rc.left - 1) as f32, (rc.bottom - rc.top - 1) as f32);
        let d = r * 2.0;
        GdipAddPathArc(p.handle, x, y, d, d, 180.0, 90.0);
        GdipAddPathArc(p.handle, x + ww - d, y, d, d, 270.0, 90.0);
        GdipAddPathArc(p.handle, x + ww - d, y + hh - d, d, d, 0.0, 90.0);
        GdipAddPathArc(p.handle, x, y + hh - d, d, d, 90.0, 90.0);
        GdipClosePathFigure(p.handle);
        let fill_brush = solid_brush(argb(fill, 255));
        GdipFillPath(g.handle, fill_brush.handle, p.handle);
        let border_pen = pen(argb(tc.button_border, 255), 1.0);
        GdipDrawPath(g.handle, border_pen.handle, p.handle);

        if !text.is_empty() {
            let old = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, text_c);
            let mut rc = rc;
            draw_text(hdc, text, &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            SelectObject(hdc, old);
        }
        // C++ destroys the GDI+ objects here, after the text.
        drop(border_pen);
        drop(fill_brush);
        drop(p);
        drop(g);
    }
}

/// `PaintSwatch`: one color swatch.
pub fn paint_swatch(hdc: Handle, rc: Rect, color: u32, border: u32, radius: i32) {
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        let br = CreateSolidBrush(color);
        let pn = CreatePen(PS_SOLID, 1, border);
        SelectObject(hdc, br);
        SelectObject(hdc, pn);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        DeleteObject(br);
        DeleteObject(pn);
    }
}

/// `PaintMsgBox`: the themed message box. `labels` are without NULs.
pub fn paint_msgbox(
    hdc: Handle,
    l: &MsgBoxLayout,
    text: &[u16],
    labels: &[Vec<u16>],
    hover_btn: i32,
    default_btn: i32,
    tc: &ThemeColors,
    font: Handle,
    icon: Handle,
    dpi: u32,
) {
    let btn_count = labels.len() as i32;
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        let client = Rect::new(l.client[0], l.client[1], l.client[2], l.client[3]);
        let bg = CreateSolidBrush(tc.content_bg);
        FillRect(hdc, &client, bg);
        DeleteObject(bg);
        if !icon.is_null() {
            DrawIconEx(
                hdc,
                l.icon_x,
                l.icon_y,
                icon,
                l.icon_size,
                l.icon_size,
                0,
                null_mut(),
                DI_NORMAL,
            );
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, tc.text_primary);
        let old_f = SelectObject(hdc, font);
        let mut rc_text = Rect::new(l.text_x, l.text_y, l.text_x + l.text_w, l.text_y + l.text_h);
        draw_text(hdc, text, &mut rc_text, DT_WORDBREAK | DT_LEFT);

        let mut btn_x = first_button_x(l, btn_count);
        for (i, label) in labels.iter().enumerate() {
            let i = i as i32;
            let mut rc = Rect::new(btn_x, l.btn_y, btn_x + l.btn_w, l.btn_y + l.btn_h);
            let fill = if i == hover_btn {
                tc.button_fill_hover
            } else {
                tc.button_fill
            };
            let border = if i == default_btn { tc.accent } else { tc.button_border };
            let br = CreateSolidBrush(fill);
            let pn = CreatePen(PS_SOLID, 1, border);
            let old_br = SelectObject(hdc, br);
            let old_pn = SelectObject(hdc, pn);
            let r = s(4, dpi);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
            SelectObject(hdc, old_br);
            SelectObject(hdc, old_pn);
            DeleteObject(br);
            DeleteObject(pn);
            SetTextColor(hdc, tc.button_text);
            draw_text(hdc, label, &mut rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            btn_x += l.btn_w + l.btn_gap;
        }
        SelectObject(hdc, old_f);
    }
}

windows_link::link!("gdi32.dll" "system" fn CreateRectRgn(l: i32, t: i32, r: i32, b: i32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn SaveDC(hdc: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn RestoreDC(hdc: Handle, saved: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn ExtSelectClipRgn(hdc: Handle, rgn: Handle, mode: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn SetViewportOrgEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32);

const RGN_AND: i32 = 1;

/// `PaintMainFrame` (phase 5b-8g): the sidebar, then on a sub-page the
/// breadcrumb strip, or with the narrow sidebar open the sidebar over the
/// strip and the hamburger.
pub fn paint_main_frame(hdc: Handle, main: &PaintContext, has_content: bool) {
    let g = defaults();
    paint_sidebar(hdc, main);
    if !(main.nav_level == 1 && main.selected_mode_index < 4 && has_content) {
        return;
    }
    let dpi = main.dpi;
    let narrow = main.sidebar_narrow_mode != 0;
    let collapsed = main.sidebar_collapsed != 0;
    let sidebar_w = if !narrow && !collapsed {
        s(g.sidebar_width, dpi)
    } else {
        0
    };
    let bc_h = s(g.breadcrumb_h, dpi);
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        if narrow && !collapsed {
            let ov = s(g.sidebar_width, dpi);
            let clip = CreateRectRgn(0, 0, ov, bc_h);
            let saved = SaveDC(hdc);
            ExtSelectClipRgn(hdc, clip, RGN_AND);
            paint_sidebar(hdc, main);
            RestoreDC(hdc, saved);
            DeleteObject(clip);
            FillRect(hdc, &Rect::new(ov, 0, 32767, bc_h), main.brush_content_bg);
            let hb = s(g.hamburger_size, dpi);
            paint_hamburger(hdc, main, Rect::new(0, 0, hb, hb), main.brush_sidebar_bg);
        } else {
            FillRect(hdc, &Rect::new(sidebar_w, 0, 32767, bc_h), main.brush_content_bg);
            let saved = SaveDC(hdc);
            SetViewportOrgEx(hdc, sidebar_w, 0, null_mut());
            paint_breadcrumb(hdc, main);
            RestoreDC(hdc, saved);
            if narrow {
                let hb = s(g.hamburger_size, dpi);
                paint_hamburger(
                    hdc,
                    main,
                    Rect::new(sidebar_w, 0, sidebar_w + hb, hb),
                    main.brush_content_bg,
                );
            }
        }
    }
}

/// `PaintContentFrame` (phase 5b-8g): the page, the narrow-mode sidebar
/// overlay, and the Level 0 hamburger.
pub fn paint_content_frame(hdc: Handle, content: &PaintContext, main: &PaintContext) {
    let g = defaults();
    paint_content(hdc, content);
    let narrow = main.sidebar_narrow_mode != 0;
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        if narrow && main.sidebar_collapsed == 0 {
            let ov = s(g.sidebar_width, main.dpi);
            let has_bc = main.nav_level == 1 && main.selected_mode_index < 4;
            let bc_off = if has_bc { s(g.breadcrumb_h, main.dpi) } else { 0 };
            let clip = CreateRectRgn(0, 0, ov, 32767);
            let saved = SaveDC(hdc);
            ExtSelectClipRgn(hdc, clip, RGN_AND);
            if bc_off != 0 {
                SetViewportOrgEx(hdc, 0, -bc_off, null_mut());
            }
            paint_sidebar(hdc, main);
            RestoreDC(hdc, saved);
            DeleteObject(clip);
        }
    }
    if narrow && main.nav_level == 0 {
        let hb = s(g.hamburger_size, main.dpi);
        paint_hamburger(hdc, main, Rect::new(0, 0, hb, hb), main.brush_content_bg);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{offset_of, size_of};

    #[test]
    fn context_layout_matches_msvc() {
        if cfg!(target_pointer_width = "64") {
            assert_eq!(size_of::<PaintContext>(), 520);
            assert_eq!(offset_of!(PaintContext, dpi), 376);
            assert_eq!(size_of::<Fonts>(), 56);
        } else {
            assert_eq!(size_of::<PaintContext>(), 484);
            assert_eq!(offset_of!(PaintContext, dpi), 340);
            assert_eq!(size_of::<Fonts>(), 28);
        }
        assert_eq!(offset_of!(PaintContext, snapshot), 88);
        assert_eq!(offset_of!(PaintContext, font_title), 304);
        assert_eq!(
            offset_of!(PaintContext, toggle_on),
            offset_of!(PaintContext, card_expanded) + 16
        );
        assert_eq!(size_of::<TextMetric>(), 60);
        assert_eq!(size_of::<BitmapInfo>(), 44);
    }

    #[test]
    fn colors() {
        assert_eq!(argb(0x00332211, 255), 0xFF112233);
        assert_eq!(lum(0x00FFFFFF), 255);
        assert_eq!(chevron(true, false), vec![0x2227]);
    }
}
