//! Phase 5b-6. Window-free computations of the settings window
//! (`dime-settings`). Card and row pointers are C++ `LayoutCard` /
//! `LayoutRow` (layout checked in `dime-config::page_layout`). `expanded`
//! points at the 16 `bool`s of `WindowData::cardExpanded`, or is null for
//! all collapsed. Null out-pointers are not written. A panic returns the
//! documented fallback and writes nothing.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use dime_config::page_layout::{LayoutCard, LayoutRow};
use dime_config::ImeMode;
use dime_settings::layout::{
    self, CardListClick, CtrlRect, Placement, PlanInput, PlanResult, RowCardHit, RowContext, RowSpan, EXPANDED_SLOTS,
};
use dime_settings::{msgbox, validate};

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn cards<'a>(p: *const LayoutCard, count: i32) -> &'a [LayoutCard] {
    if p.is_null() || count <= 0 {
        &[]
    } else {
        // SAFETY: caller passes `count` cards.
        unsafe { std::slice::from_raw_parts(p, count as usize) }
    }
}

unsafe fn expanded(p: *const u8) -> [bool; EXPANDED_SLOTS] {
    let mut out = [false; EXPANDED_SLOTS];
    if !p.is_null() {
        for (i, e) in out.iter_mut().enumerate() {
            // SAFETY: caller passes 16 one-byte bools.
            *e = unsafe { *p.add(i) } != 0;
        }
    }
    out
}

unsafe fn write<T>(out: *mut T, v: T) {
    if !out.is_null() {
        // SAFETY: caller provided the out-pointer.
        unsafe { *out = v };
    }
}

unsafe fn write_list<T: Copy>(items: &[T], out: *mut T, cap: usize, out_count: *mut usize) {
    if !out.is_null() {
        for (i, item) in items.iter().take(cap).enumerate() {
            // SAFETY: caller passes `cap` writable entries.
            unsafe { *out.add(i) = *item };
        }
    }
    unsafe { write(out_count, items.len()) };
}

#[no_mangle]
pub extern "C" fn dime_settings_card_width(
    client_right: i32,
    dpi: u32,
    sidebar_takes_space: i32,
    vscroll_w: i32,
) -> i32 {
    guard(0, || {
        layout::card_width(client_right, dpi, sidebar_takes_space != 0, vscroll_w)
    })
}

#[no_mangle]
pub extern "C" fn dime_settings_ctrl_right(margin: i32, card_w: i32, dpi: u32, has_chevron: i32) -> i32 {
    guard(0, || layout::ctrl_right(margin, card_w, dpi, has_chevron != 0))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_row_height(row: *const LayoutRow, dpi: u32, preview_h: i32) -> i32 {
    guard(0, || {
        if row.is_null() {
            return 0;
        }
        // SAFETY: a LayoutRow from the C++ or Rust table.
        layout::row_height(unsafe { &*row }, dpi, preview_h)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_row_shown(
    row: *const LayoutRow,
    mode_mask: i32,
    color_mode: i32,
    grid_expanded: i32,
) -> i32 {
    guard(0, || {
        if row.is_null() {
            return 0;
        }
        // SAFETY: a LayoutRow from the C++ or Rust table.
        i32::from(layout::row_shown(
            unsafe { &*row },
            mode_mask,
            color_mode,
            grid_expanded != 0,
        ))
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_card_list_hit_test(
    y: i32,
    card_ptr: *const LayoutCard,
    count: i32,
    mode_mask: i32,
    expanded_ptr: *const u8,
    expand_base: i32,
    dpi: u32,
    scroll_pos: i32,
) -> i32 {
    guard(-1, || unsafe {
        layout::card_list_hit_test(
            y,
            cards(card_ptr, count),
            mode_mask,
            &expanded(expanded_ptr),
            expand_base,
            dpi,
            scroll_pos,
        )
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_card_list_click(
    y: i32,
    card_ptr: *const LayoutCard,
    count: i32,
    mode_mask: i32,
    expanded_ptr: *const u8,
    expand_base: i32,
    dpi: u32,
    scroll_pos: i32,
    out: *mut CardListClick,
) {
    guard((), || unsafe {
        let c = layout::card_list_click(
            y,
            cards(card_ptr, count),
            mode_mask,
            &expanded(expanded_ptr),
            expand_base,
            dpi,
            scroll_pos,
        );
        write(out, c);
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_row_spans(
    card: *const LayoutCard,
    ctx: *const RowContext,
    out: *mut RowSpan,
    cap: usize,
    out_count: *mut usize,
) {
    guard((), || unsafe {
        if card.is_null() || ctx.is_null() {
            write(out_count, 0);
            return;
        }
        let spans = layout::row_spans(&*card, &*ctx);
        write_list(&spans, out, cap, out_count);
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_row_cards_hit(
    y: i32,
    card: *const LayoutCard,
    ctx: *const RowContext,
    out: *mut RowCardHit,
) {
    guard((), || unsafe {
        let hit = if card.is_null() || ctx.is_null() {
            RowCardHit {
                span_start: -1,
                row: -1,
            }
        } else {
            layout::row_cards_hit(y, &*card, &*ctx)
        };
        write(out, hit);
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_plan_controls(
    input: *const PlanInput,
    ime: *const LayoutCard,
    ime_count: i32,
    load: *const LayoutCard,
    load_count: i32,
    out: *mut Placement,
    cap: usize,
    out_count: *mut usize,
    result: *mut PlanResult,
) {
    guard((), || unsafe {
        if input.is_null() {
            write(out_count, 0);
            write(result, PlanResult::default());
            return;
        }
        let (placed, res) = layout::plan_controls(&*input, cards(ime, ime_count), cards(load, load_count));
        write_list(&placed, out, cap, out_count);
        write(result, res);
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_row_control_rect(
    row_type: i32,
    ctrl_right: i32,
    cy: i32,
    ctrl_h: i32,
    btn_h: i32,
    dpi: u32,
    out: *mut CtrlRect,
) -> i32 {
    guard(0, || {
        match layout::row_control_rect(row_type, ctrl_right, cy, ctrl_h, btn_h, dpi) {
            Some(r) => {
                unsafe { write(out, r) };
                1
            }
            None => 0,
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_swatch_rect(
    index: i32,
    margin: i32,
    cur_y: i32,
    ctrl_right: i32,
    dpi: u32,
    out: *mut CtrlRect,
) {
    guard((), || unsafe {
        write(out, layout::swatch_rect(index, margin, cur_y, ctrl_right, dpi))
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_rich_editor_rects(
    margin: i32,
    card_w: i32,
    pad: i32,
    cur_y: i32,
    this_h: i32,
    ctrl_right: i32,
    btn_w: i32,
    btn_h: i32,
    dpi: u32,
    out_editor: *mut CtrlRect,
    out_save: *mut CtrlRect,
) {
    guard((), || unsafe {
        let (re, save) = layout::rich_editor_rects(margin, card_w, pad, cur_y, this_h, ctrl_right, btn_w, btn_h, dpi);
        write(out_editor, re);
        write(out_save, save);
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_content_area_rect(
    client_w: i32,
    client_h: i32,
    dpi: u32,
    sidebar_takes_space: i32,
    has_breadcrumb: i32,
    out: *mut CtrlRect,
) {
    guard((), || unsafe {
        write(
            out,
            layout::content_area_rect(client_w, client_h, dpi, sidebar_takes_space != 0, has_breadcrumb != 0),
        )
    })
}

/// `ValidateCustomTableBuffer`. Returns the invalid line count. The summary
/// is written like `StringCchCatW` into `summary_cch` units (truncated, NUL
/// terminated); `summary` and `first_err_line` are cleared first as in C++.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_validate_buffer(
    text: *const u16,
    len: usize,
    mode: i32,
    max_codes: u32,
    char_ok: Option<unsafe extern "C" fn(u16, *mut c_void) -> i32>,
    ctx: *mut c_void,
    max_report: i32,
    first_err_line: *mut i32,
    summary: *mut u16,
    summary_cch: usize,
) -> i32 {
    guard(0, || unsafe {
        write(first_err_line, 0);
        if !summary.is_null() && summary_cch > 0 {
            *summary = 0;
        }
        if text.is_null() || len == 0 {
            return 0;
        }
        let units = std::slice::from_raw_parts(text, len);
        let report = if let Some(f) = char_ok {
            let mut check = |c: u16| f(c, ctx) != 0;
            validate::validate_buffer(units, ImeMode::from_i32(mode), max_codes, Some(&mut check), max_report)
        } else {
            validate::validate_buffer(units, ImeMode::from_i32(mode), max_codes, None, max_report)
        };
        if report.error_count > 0 {
            write(first_err_line, report.first_error_line);
        }
        if !summary.is_null() && summary_cch > 0 {
            let out = validate::truncate_for_buffer(&report.summary, summary_cch);
            std::ptr::copy_nonoverlapping(out.as_ptr(), summary, out.len());
        }
        report.error_count
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_buttons(mb_type: u32, out: *mut msgbox::Buttons) {
    guard((), || unsafe { write(out, msgbox::buttons(mb_type)) })
}

#[no_mangle]
pub extern "C" fn dime_settings_msgbox_icon(mb_type: u32) -> i32 {
    guard(0, || msgbox::icon(mb_type))
}

#[no_mangle]
pub extern "C" fn dime_settings_msgbox_max_text_width(dpi: u32) -> i32 {
    guard(0, || msgbox::max_text_width(dpi))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_layout(
    has_icon: i32,
    btn_count: i32,
    text_w: i32,
    text_h: i32,
    dpi: u32,
    out: *mut msgbox::Layout,
) {
    guard((), || unsafe {
        write(out, msgbox::layout(has_icon != 0, btn_count, text_w, text_h, dpi))
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_first_button_x(l: *const msgbox::Layout, btn_count: i32) -> i32 {
    guard(0, || {
        if l.is_null() {
            0
        } else {
            msgbox::first_button_x(unsafe { &*l }, btn_count)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_button_at(
    l: *const msgbox::Layout,
    btn_count: i32,
    x: i32,
    y: i32,
) -> i32 {
    guard(-1, || {
        if l.is_null() {
            -1
        } else {
            msgbox::button_at(unsafe { &*l }, btn_count, x, y)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_key_result(b: *const msgbox::Buttons, default_btn: i32, vk: u32) -> i32 {
    guard(-1, || {
        if b.is_null() {
            -1
        } else {
            msgbox::key_result(unsafe { &*b }, default_btn, vk)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_msgbox_close_result(b: *const msgbox::Buttons) -> i32 {
    guard(-1, || {
        if b.is_null() {
            -1
        } else {
            msgbox::close_result(unsafe { &*b })
        }
    })
}

// ---- Phase 5b-7: painting (dime-settings::paint) -------------------------

use dime_config::geometry::ThemeColors;
use dime_config::settings_model::SettingsSnapshot;
use dime_settings::paint::{self, Fonts, Handle, PaintContext, Rect};

/// A NUL-terminated UTF-16 string without the NUL; null is empty.
unsafe fn cstr_vec(p: *const u16) -> Vec<u16> {
    let mut out = Vec::new();
    if p.is_null() {
        return out;
    }
    let mut i = 0;
    // SAFETY: caller passes a NUL-terminated string.
    while unsafe { *p.add(i) } != 0 {
        out.push(unsafe { *p.add(i) });
        i += 1;
    }
    out
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_create_fonts(dpi: u32, out: *mut Fonts) {
    guard((), || unsafe { write(out, paint::create_fonts(dpi)) })
}

#[no_mangle]
pub extern "C" fn dime_settings_draw_card(
    hdc: Handle,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    corner_r: i32,
    card_bg: u32,
    card_border: u32,
    content_bg: u32,
    is_hover: i32,
    hover_color: u32,
) {
    guard((), || {
        paint::draw_card(
            hdc,
            x,
            y,
            w,
            h,
            corner_r,
            card_bg,
            card_border,
            content_bg,
            is_hover != 0,
            hover_color,
        )
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_toggle(
    hdc: Handle,
    rc: *const Rect,
    is_on: i32,
    _is_hover: i32,
    tc: *const ThemeColors,
) {
    guard((), || unsafe {
        if rc.is_null() || tc.is_null() {
            return;
        }
        paint::paint_toggle(hdc, *rc, is_on != 0, &*tc)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_preview_height(hdc: Handle, snap: *const SettingsSnapshot, dpi: u32) -> i32 {
    guard(0, || {
        if snap.is_null() {
            0
        } else {
            paint::preview_height(hdc, unsafe { &*snap }, dpi)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_preview(
    hdc: Handle,
    snap: *const SettingsSnapshot,
    margin: i32,
    cur_y: i32,
    _card_w: i32,
    _total_h: i32,
    dpi: u32,
    sys_face: u32,
) {
    guard((), || {
        if !snap.is_null() {
            paint::paint_preview(hdc, unsafe { &*snap }, margin, cur_y, dpi, sys_face)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_sidebar(hdc: Handle, ctx: *const PaintContext) {
    guard((), || {
        if !ctx.is_null() {
            paint::paint_sidebar(hdc, unsafe { &*ctx })
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_content(hdc: Handle, ctx: *const PaintContext) {
    guard((), || {
        if !ctx.is_null() {
            paint::paint_content(hdc, unsafe { &*ctx })
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_breadcrumb(hdc: Handle, ctx: *const PaintContext) {
    guard((), || {
        if !ctx.is_null() {
            paint::paint_breadcrumb(hdc, unsafe { &*ctx })
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_hamburger(
    hdc: Handle,
    ctx: *const PaintContext,
    rc: *const Rect,
    bg: Handle,
) {
    guard((), || unsafe {
        if !ctx.is_null() && !rc.is_null() {
            paint::paint_hamburger(hdc, &*ctx, *rc, bg)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_button(
    hdc: Handle,
    rc: *const Rect,
    text: *const u16,
    pressed: i32,
    disabled: i32,
    tc: *const ThemeColors,
    font: Handle,
    dpi: u32,
) {
    guard((), || unsafe {
        if rc.is_null() || tc.is_null() {
            return;
        }
        paint::paint_button(hdc, *rc, &cstr_vec(text), pressed != 0, disabled != 0, &*tc, font, dpi)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_swatch(
    hdc: Handle,
    rc: *const Rect,
    color: u32,
    border: u32,
    radius: i32,
) {
    guard((), || unsafe {
        if !rc.is_null() {
            paint::paint_swatch(hdc, *rc, color, border, radius)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_msgbox(
    hdc: Handle,
    layout: *const msgbox::Layout,
    text: *const u16,
    labels: *const *const u16,
    btn_count: i32,
    hover_btn: i32,
    default_btn: i32,
    tc: *const ThemeColors,
    font: Handle,
    icon: Handle,
    dpi: u32,
) {
    guard((), || unsafe {
        if layout.is_null() || tc.is_null() {
            return;
        }
        let n = if labels.is_null() {
            0
        } else {
            btn_count.clamp(0, 3) as usize
        };
        let labels: Vec<Vec<u16>> = (0..n).map(|i| cstr_vec(*labels.add(i))).collect();
        paint::paint_msgbox(
            hdc,
            &*layout,
            &cstr_vec(text),
            &labels,
            hover_btn,
            default_btn,
            &*tc,
            font,
            icon,
            dpi,
        )
    })
}

// ---- Phase 5b-8a: the themed message box window (dime-settings::msgboxwin)

/// `SettingsMsgBox::Show` in Rust: runs the modal box and returns the button
/// id. A panic returns IDCANCEL (2).
#[no_mangle]
pub unsafe extern "C" fn dime_settings_message_box(
    parent: Handle,
    text: *const u16,
    caption: *const u16,
    mb_type: u32,
    class_name: *const u16,
    is_dark: i32,
    tc: *const ThemeColors,
    dpi_for: Option<dime_settings::msgboxwin::DpiFn>,
) -> i32 {
    guard(2, || unsafe {
        let (Some(dpi_for), false) = (dpi_for, tc.is_null()) else {
            return 2;
        };
        dime_settings::msgboxwin::show(
            parent,
            &cstr_vec(text),
            &cstr_vec(caption),
            mb_type,
            &cstr_vec(class_name),
            is_dark != 0,
            &*tc,
            dpi_for,
        )
    })
}

// ---- Phase 5b-8b: navigation reducer (dime-settings::nav) ------------------

use dime_settings::nav::{self, NavEffect, NavEvent, NavState};

/// `SettingsNav::Handle`: update `state` from `ev`, write up to `cap` effects
/// and the total count. A panic leaves `state` as the reducer left it and
/// writes no effects.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_nav_handle(
    state: *mut NavState,
    ev: *const NavEvent,
    out: *mut NavEffect,
    cap: usize,
    out_count: *mut usize,
) {
    guard((), || unsafe {
        write(out_count, 0);
        if state.is_null() || ev.is_null() {
            return;
        }
        let effects = nav::handle(&mut *state, &*ev);
        write_list(&effects, out, cap, out_count);
    })
}

/// `SettingsNav::MinTrackSize`: `out` is a POINT.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_nav_min_track_size(screen_dpi: u32, out: *mut [i32; 2]) {
    guard((), || unsafe {
        let (x, y) = nav::min_track_size(screen_dpi);
        write(out, [x, y]);
    })
}

// ---- Phase 5b-8c: WM_COMMAND (dime-settings::command) ------------------------

use dime_settings::command::{self, CommandEvent, LogFont};

type Ctx = *mut c_void;

/// `DimeSettingsCmdHostVtbl`: the order must match dime_ffi.h and
/// SettingsCommand.cpp (`kVtbl`). Returned strings stay valid until the next
/// call that returns a string.
#[repr(C)]
pub struct CmdHostVtbl {
    log_pixels_y: unsafe extern "C" fn(Ctx) -> i32,
    choose_font: unsafe extern "C" fn(Ctx, *mut LogFont, *mut i32) -> i32,
    choose_color: unsafe extern "C" fn(Ctx, *mut u32) -> i32,
    get_open_file: unsafe extern "C" fn(Ctx, i32, *mut i32, *mut usize) -> *const u16,
    get_save_file: unsafe extern "C" fn(Ctx, *const u16, usize, *mut i32, *mut usize) -> *const u16,
    message_box: unsafe extern "C" fn(Ctx, *const u16, usize, *const u16, u32) -> i32,
    app_data_path: unsafe extern "C" fn(Ctx, *mut usize) -> *const u16,
    documents_path: unsafe extern "C" fn(Ctx, *mut usize) -> *const u16,
    custom_table_txt_path: unsafe extern "C" fn(Ctx, i32, *mut usize) -> *const u16,
    custom_table_cin_path: unsafe extern "C" fn(Ctx, i32, *mut usize) -> *const u16,
    write_bom_file: unsafe extern "C" fn(Ctx, *const u16, usize, *const u16, usize) -> i32,
    load_text_file: unsafe extern "C" fn(Ctx, *const u16, usize, *mut i32, *mut usize) -> *const u16,
    parse_cin: unsafe extern "C" fn(Ctx, *const u16, usize, *const u16, usize, i32) -> i32,
    reset_all_defaults: unsafe extern "C" fn(Ctx, i32),
    write_config: unsafe extern "C" fn(Ctx, i32),
    load_config: unsafe extern "C" fn(Ctx, i32),
    load_snapshot: unsafe extern "C" fn(Ctx, *mut SettingsSnapshot),
    apply_and_save: unsafe extern "C" fn(Ctx),
    set_color_mode: unsafe extern "C" fn(Ctx, i32),
    load_colors_for_mode: unsafe extern "C" fn(Ctx, i32),
    get_colors: unsafe extern "C" fn(Ctx, *mut u32),
    set_color: unsafe extern "C" fn(Ctx, i32, u32),
    save_colors_for_mode: unsafe extern "C" fn(Ctx, i32),
    set_reverse_conversion_selection: unsafe extern "C" fn(Ctx, i32),
    rebuild: unsafe extern "C" fn(Ctx),
    invalidate_content: unsafe extern "C" fn(Ctx, i32),
    update_content: unsafe extern "C" fn(Ctx),
    invalidate_swatch: unsafe extern "C" fn(Ctx, i32),
    editor_exists: unsafe extern "C" fn(Ctx) -> i32,
    editor_text: unsafe extern "C" fn(Ctx, *mut usize) -> *const u16,
    set_editor_text_muted: unsafe extern "C" fn(Ctx, *const u16, usize),
    editor_line_index: unsafe extern "C" fn(Ctx, i32) -> i32,
    editor_focus_at: unsafe extern "C" fn(Ctx, i32),
    validate_viewport: unsafe extern "C" fn(Ctx),
    cancel_bg_scan: unsafe extern "C" fn(Ctx),
    enable_save: unsafe extern "C" fn(Ctx, i32),
    update_line_count: unsafe extern "C" fn(Ctx),
    validate_buffer: unsafe extern "C" fn(Ctx, *const u16, usize, i32, *mut i32, *mut i32, *mut usize) -> *const u16,
}

struct VtblHost<'a> {
    v: &'a CmdHostVtbl,
    ctx: Ctx,
}

unsafe fn copy_out(p: *const u16, len: usize) -> Vec<u16> {
    if p.is_null() || len == 0 {
        Vec::new()
    } else {
        // SAFETY: the host keeps `len` units alive until its next string call.
        unsafe { std::slice::from_raw_parts(p, len) }.to_vec()
    }
}

// SAFETY (every method): the vtable functions are the host thunks; they take
// `ctx` and never unwind.
impl command::Host for VtblHost<'_> {
    fn log_pixels_y(&mut self) -> i32 {
        unsafe { (self.v.log_pixels_y)(self.ctx) }
    }
    fn choose_font(&mut self, lf: &mut LogFont, point_size10: &mut i32) -> bool {
        unsafe { (self.v.choose_font)(self.ctx, lf, point_size10) != 0 }
    }
    fn choose_color(&mut self, rgb: &mut u32) -> bool {
        unsafe { (self.v.choose_color)(self.ctx, rgb) != 0 }
    }
    fn get_open_file(&mut self, filter: i32) -> Option<Vec<u16>> {
        let (mut ok, mut len) = (0, 0);
        let p = unsafe { (self.v.get_open_file)(self.ctx, filter, &mut ok, &mut len) };
        (ok != 0).then(|| unsafe { copy_out(p, len) })
    }
    fn get_save_file(&mut self, default_path: &[u16]) -> Option<Vec<u16>> {
        let (mut ok, mut len) = (0, 0);
        let p =
            unsafe { (self.v.get_save_file)(self.ctx, default_path.as_ptr(), default_path.len(), &mut ok, &mut len) };
        (ok != 0).then(|| unsafe { copy_out(p, len) })
    }
    fn message_box(&mut self, text: &[u16], caption: &str, mb_type: u32) -> i32 {
        let cap: Vec<u16> = caption.encode_utf16().chain(std::iter::once(0)).collect();
        unsafe { (self.v.message_box)(self.ctx, text.as_ptr(), text.len(), cap.as_ptr(), mb_type) }
    }
    fn app_data_path(&mut self) -> Vec<u16> {
        let mut len = 0;
        unsafe { copy_out((self.v.app_data_path)(self.ctx, &mut len), len) }
    }
    fn documents_path(&mut self) -> Vec<u16> {
        let mut len = 0;
        unsafe { copy_out((self.v.documents_path)(self.ctx, &mut len), len) }
    }
    fn custom_table_txt_path(&mut self, mode: i32) -> Vec<u16> {
        let mut len = 0;
        unsafe { copy_out((self.v.custom_table_txt_path)(self.ctx, mode, &mut len), len) }
    }
    fn custom_table_cin_path(&mut self, mode: i32) -> Vec<u16> {
        let mut len = 0;
        unsafe { copy_out((self.v.custom_table_cin_path)(self.ctx, mode, &mut len), len) }
    }
    fn write_bom_file(&mut self, path: &[u16], text: &[u16]) -> bool {
        unsafe { (self.v.write_bom_file)(self.ctx, path.as_ptr(), path.len(), text.as_ptr(), text.len()) != 0 }
    }
    fn load_text_file(&mut self, path: &[u16]) -> Option<Vec<u16>> {
        let (mut ok, mut len) = (0, 0);
        let p = unsafe { (self.v.load_text_file)(self.ctx, path.as_ptr(), path.len(), &mut ok, &mut len) };
        (ok != 0).then(|| unsafe { copy_out(p, len) })
    }
    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom_table: bool) -> bool {
        unsafe {
            (self.v.parse_cin)(
                self.ctx,
                src.as_ptr(),
                src.len(),
                dst.as_ptr(),
                dst.len(),
                i32::from(custom_table),
            ) != 0
        }
    }
    fn reset_all_defaults(&mut self, mode: i32) {
        unsafe { (self.v.reset_all_defaults)(self.ctx, mode) }
    }
    fn write_config(&mut self, mode: i32) {
        unsafe { (self.v.write_config)(self.ctx, mode) }
    }
    fn load_config(&mut self, mode: i32) {
        unsafe { (self.v.load_config)(self.ctx, mode) }
    }
    fn load_snapshot(&mut self) -> SettingsSnapshot {
        let mut s = SettingsSnapshot::default();
        unsafe { (self.v.load_snapshot)(self.ctx, &mut s) };
        s
    }
    fn apply_and_save(&mut self) {
        unsafe { (self.v.apply_and_save)(self.ctx) }
    }
    fn set_color_mode(&mut self, color_mode: i32) {
        unsafe { (self.v.set_color_mode)(self.ctx, color_mode) }
    }
    fn load_colors_for_mode(&mut self, color_mode: i32) {
        unsafe { (self.v.load_colors_for_mode)(self.ctx, color_mode) }
    }
    fn get_colors(&mut self) -> [u32; 6] {
        let mut c = [0u32; 6];
        unsafe { (self.v.get_colors)(self.ctx, c.as_mut_ptr()) };
        c
    }
    fn set_color(&mut self, index: i32, color: u32) {
        unsafe { (self.v.set_color)(self.ctx, index, color) }
    }
    fn save_colors_for_mode(&mut self, color_mode: i32) {
        unsafe { (self.v.save_colors_for_mode)(self.ctx, color_mode) }
    }
    fn set_reverse_conversion_selection(&mut self, sel: i32) {
        unsafe { (self.v.set_reverse_conversion_selection)(self.ctx, sel) }
    }
    fn rebuild(&mut self) {
        unsafe { (self.v.rebuild)(self.ctx) }
    }
    fn invalidate_content(&mut self, erase: bool) {
        unsafe { (self.v.invalidate_content)(self.ctx, i32::from(erase)) }
    }
    fn update_content(&mut self) {
        unsafe { (self.v.update_content)(self.ctx) }
    }
    fn invalidate_swatch(&mut self, index: i32) {
        unsafe { (self.v.invalidate_swatch)(self.ctx, index) }
    }
    fn editor_exists(&mut self) -> bool {
        unsafe { (self.v.editor_exists)(self.ctx) != 0 }
    }
    fn editor_text(&mut self) -> Vec<u16> {
        let mut len = 0;
        unsafe { copy_out((self.v.editor_text)(self.ctx, &mut len), len) }
    }
    fn set_editor_text_muted(&mut self, text: &[u16]) {
        unsafe { (self.v.set_editor_text_muted)(self.ctx, text.as_ptr(), text.len()) }
    }
    fn editor_line_index(&mut self, line: i32) -> i32 {
        unsafe { (self.v.editor_line_index)(self.ctx, line) }
    }
    fn editor_focus_at(&mut self, cp: i32) {
        unsafe { (self.v.editor_focus_at)(self.ctx, cp) }
    }
    fn validate_viewport(&mut self) {
        unsafe { (self.v.validate_viewport)(self.ctx) }
    }
    fn cancel_bg_scan(&mut self) {
        unsafe { (self.v.cancel_bg_scan)(self.ctx) }
    }
    fn enable_save(&mut self, enable: bool) {
        unsafe { (self.v.enable_save)(self.ctx, i32::from(enable)) }
    }
    fn update_line_count(&mut self) {
        unsafe { (self.v.update_line_count)(self.ctx) }
    }
    fn validate_buffer(&mut self, text: &[u16], max_report: i32) -> (i32, i32, Vec<u16>) {
        let (mut count, mut first, mut len) = (0, 0, 0);
        let p = unsafe {
            (self.v.validate_buffer)(
                self.ctx,
                text.as_ptr(),
                text.len(),
                max_report,
                &mut count,
                &mut first,
                &mut len,
            )
        };
        (count, first, unsafe { copy_out(p, len) })
    }
}

/// `SettingsCommand::Handle` in Rust. `snapshot` and `grid_expanded` are the
/// window's and are updated in place. A panic stops the handler where it was.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_command_handle(
    vtbl: *const CmdHostVtbl,
    ctx: Ctx,
    snapshot: *mut SettingsSnapshot,
    grid_expanded: *mut i32,
    ev: *const CommandEvent,
) {
    guard((), || unsafe {
        if vtbl.is_null() || snapshot.is_null() || grid_expanded.is_null() || ev.is_null() {
            return;
        }
        let mut host = VtblHost { v: &*vtbl, ctx };
        // Written in place: the host's rebuild reads it during the call.
        let mut set_grid = |on: bool| *grid_expanded = i32::from(on);
        command::handle(&mut host, snapshot, &mut set_grid, &*ev);
    })
}

// ---- Phase 5b-8d: custom table editor (dime-settings::editor) --------------

use dime_settings::editor;

/// `SettingsEditor::ViewportLineRange`. Null outs are not written.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_editor_viewport(hre: Handle, first: *mut i32, last: *mut i32) {
    guard((), || unsafe {
        let (f, l) = editor::viewport_line_range(hre);
        write(first, f);
        write(last, l);
    })
}

/// `SettingsEditor::ValidateViewport`. `char_ok` / `char_ctx` is the
/// engine's per-character key check, or null for the fallback range.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_editor_validate_viewport(
    hre: Handle,
    mode: i32,
    max_codes: u32,
    is_dark: i32,
    char_ok: Option<unsafe extern "C" fn(u16, *mut c_void) -> i32>,
    char_ctx: *mut c_void,
) {
    guard((), || {
        let mut check = |c: u16| unsafe { char_ok.is_some_and(|f| f(c, char_ctx) != 0) };
        let mut ctx = editor::EditorContext {
            mode: ImeMode::from_i32(mode),
            max_codes,
            is_dark: is_dark != 0,
            char_ok: if char_ok.is_some() { Some(&mut check) } else { None },
        };
        editor::validate_viewport(hre, &mut ctx);
    })
}

/// The editor's `SUBCLASSPROC` (`SettingsEditor::SubclassProc`).
#[no_mangle]
pub extern "C" fn dime_settings_editor_subclass_proc() -> *const c_void {
    editor::subclass_proc as *const c_void
}

// ---- Phase 5b-8e: populate controls (dime-settings::populate) ------------

use dime_settings::populate;

/// `DimePopulateOp` (28 bytes). Text is `text[text_offset..+text_len]`.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PopulateOp {
    kind: i32,
    ctrl_id: i32,
    value: i32,
    has_data: i32,
    data: i32,
    text_offset: i32,
    text_len: i32,
}

/// `SettingsPopulate` operations for `snapshot`. `reverse_descs` are
/// NUL-terminated, `reverse_selected` 0 / 1, both `reverse_count` long.
/// Writes up to `cap` ops and up to `text_cap` text units; the full counts go
/// to `out_count` / `text_len` (call once with zero caps to size).
#[no_mangle]
pub unsafe extern "C" fn dime_settings_populate_ops(
    snapshot: *const SettingsSnapshot,
    windows1809: i32,
    reverse_is_null: i32,
    reverse_descs: *const *const u16,
    reverse_selected: *const i32,
    reverse_count: i32,
    out: *mut PopulateOp,
    cap: usize,
    out_count: *mut usize,
    text: *mut u16,
    text_cap: usize,
    text_len: *mut usize,
) {
    guard((), || unsafe {
        write(out_count, 0);
        write(text_len, 0);
        if snapshot.is_null() {
            return;
        }
        let n = if reverse_descs.is_null() || reverse_selected.is_null() {
            0
        } else {
            reverse_count.max(0) as usize
        };
        let descs: Vec<Vec<u16>> = (0..n).map(|i| cstr_vec(*reverse_descs.add(i))).collect();
        let entries: Vec<populate::ReverseEntry> = (0..n)
            .map(|i| populate::ReverseEntry {
                description: &descs[i],
                selected: *reverse_selected.add(i) != 0,
            })
            .collect();
        let ops = populate::populate(&*snapshot, windows1809 != 0, reverse_is_null != 0, &entries);
        let mut pool: Vec<u16> = Vec::new();
        let flat: Vec<PopulateOp> = ops
            .iter()
            .map(|op| {
                let offset = pool.len() as i32;
                pool.extend_from_slice(&op.text);
                PopulateOp {
                    kind: op.kind,
                    ctrl_id: op.ctrl_id,
                    value: op.value,
                    has_data: i32::from(op.data.is_some()),
                    data: op.data.unwrap_or(0),
                    text_offset: offset,
                    text_len: op.text.len() as i32,
                }
            })
            .collect();
        write_list(&flat, out, cap, out_count);
        write_list(&pool, text, text_cap, text_len);
    })
}

// ---- Phase 5b-8f: control specs (dime-settings::controls) -----------------

use dime_settings::controls;
use dime_settings::layout::PlanInput as ControlsPlanInput;

/// `DimeControlSpec` (80 bytes). Text is `text[text_offset..+text_len]`.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ControlSpecC {
    ctrl_id: i32,
    menu_id: i32,
    role: i32,
    control_class: i32,
    ex_style: u32,
    style: u32,
    has_text: i32,
    text_offset: i32,
    text_len: i32,
    create: [i32; 4],
    add: [i32; 4],
    vis_h: i32,
    set_font: i32,
    dark_theme: i32,
}

/// `SettingsControls::BuildSpecs`. Writes up to `cap` specs and `text_cap`
/// text units; the full counts go to `out_count` / `text_len`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_control_specs(
    input: *const ControlsPlanInput,
    card_w: i32,
    ime: *const LayoutCard,
    ime_count: i32,
    load: *const LayoutCard,
    load_count: i32,
    out: *mut ControlSpecC,
    cap: usize,
    out_count: *mut usize,
    text: *mut u16,
    text_cap: usize,
    text_len: *mut usize,
) {
    guard((), || unsafe {
        write(out_count, 0);
        write(text_len, 0);
        if input.is_null() {
            return;
        }
        let specs = controls::build_specs(&*input, card_w, cards(ime, ime_count), cards(load, load_count));
        let mut pool: Vec<u16> = Vec::new();
        let flat: Vec<ControlSpecC> = specs
            .iter()
            .map(|s| {
                let offset = pool.len() as i32;
                let len = s.text.as_ref().map_or(0, |t| t.len()) as i32;
                if let Some(t) = &s.text {
                    pool.extend_from_slice(t);
                }
                ControlSpecC {
                    ctrl_id: s.ctrl_id,
                    menu_id: s.menu_id,
                    role: s.role,
                    control_class: s.control_class,
                    ex_style: s.ex_style,
                    style: s.style,
                    has_text: i32::from(s.text.is_some()),
                    text_offset: offset,
                    text_len: len,
                    create: s.create,
                    add: s.add,
                    vis_h: s.vis_h,
                    set_font: i32::from(s.set_font),
                    dark_theme: s.dark_theme,
                }
            })
            .collect();
        write_list(&flat, out, cap, out_count);
        write_list(&pool, text, text_cap, text_len);
    })
}

// ---- Phase 5b-8g: window frames (dime-settings::paint) ---------------------

/// `SettingsWindowPaint::PaintMainFrame`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_main_frame(hdc: Handle, main: *const PaintContext, has_content: i32) {
    guard((), || {
        if !main.is_null() {
            paint::paint_main_frame(hdc, unsafe { &*main }, has_content != 0)
        }
    })
}

/// `SettingsWindowPaint::PaintContentFrame`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_paint_content_frame(
    hdc: Handle,
    content: *const PaintContext,
    main: *const PaintContext,
) {
    guard((), || {
        if !content.is_null() && !main.is_null() {
            paint::paint_content_frame(hdc, unsafe { &*content }, unsafe { &*main })
        }
    })
}

// ---- Phase 5b-8h: launch, defaults, scroll (dime-settings::lifecycle) -------

use dime_settings::lifecycle::{self, LaunchArgs, ScrollAction, ScrollItem};

/// A NUL-terminated command line (no length cap, like `wcsstr`).
unsafe fn cmd_units<'a>(p: *const u16) -> &'a [u16] {
    if p.is_null() {
        return &[];
    }
    let mut n = 0;
    // SAFETY: caller passes a NUL-terminated string.
    unsafe {
        while *p.add(n) != 0 {
            n += 1;
        }
        std::slice::from_raw_parts(p, n)
    }
}

/// `SettingsLifecycle::ParseLaunch`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_parse_launch(cmd: *const u16, out: *mut LaunchArgs) {
    guard((), || unsafe { write(out, lifecycle::parse_launch(cmd_units(cmd))) })
}

/// `SettingsLifecycle::CopyDataMode`: an IME_MODE, 4 (none) for no switch.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_copy_data_mode(cmd: *const u16) -> i32 {
    guard(ImeMode::None as i32, || unsafe {
        lifecycle::copy_data_mode(cmd_units(cmd)) as i32
    })
}

/// `SettingsLifecycle::DefaultExpanded`: writes 16 one-byte bools.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_default_expanded(
    ime: *const LayoutCard,
    ime_count: i32,
    load: *const LayoutCard,
    load_count: i32,
    out: *mut u8,
) {
    guard((), || unsafe {
        if out.is_null() {
            return;
        }
        let e = lifecycle::default_expanded(cards(ime, ime_count), cards(load, load_count));
        for (i, v) in e.iter().enumerate() {
            *out.add(i) = u8::from(*v);
        }
    })
}

/// `SettingsLifecycle::PlanScroll`: `out` has `count` entries.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_plan_scroll(
    items: *const ScrollItem,
    count: usize,
    scroll_pos: i32,
    dpi: u32,
    out: *mut ScrollAction,
) {
    guard((), || unsafe {
        if items.is_null() || out.is_null() || count == 0 {
            return;
        }
        let acts = lifecycle::plan_scroll(std::slice::from_raw_parts(items, count), scroll_pos, dpi);
        for (i, a) in acts.iter().enumerate() {
            *out.add(i) = *a;
        }
    })
}

// ---- Phase 5b-8i: create / theme / dpi (dime-settings::frame) ---------------

use dime_settings::frame::{self, FrameEffect, FrameEvent, FrameState};

/// `SettingsFrame::Handle`: updates `state`, writes up to `cap` effects; the
/// full count goes to `out_count`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_frame_handle(
    state: *mut FrameState,
    ev: *const FrameEvent,
    out: *mut FrameEffect,
    cap: usize,
    out_count: *mut usize,
) {
    guard((), || unsafe {
        write(out_count, 0);
        if state.is_null() || ev.is_null() {
            return;
        }
        let effects = frame::handle(&mut *state, &*ev);
        write_list(&effects, out, cap, out_count);
    })
}

/// `SettingsFrame::InitialSize`: `out` is a SIZE.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_frame_initial_size(dpi: u32, out: *mut [i32; 2]) {
    guard((), || unsafe {
        let (w, h) = frame::initial_size(dpi);
        write(out, [w, h]);
    })
}

// ---- Phase 5b-8j: WM_DRAWITEM / WM_CTLCOLOR* (dime-settings::owner) ---------

use dime_settings::owner::{self, Context as OwnerContext, DrawItem};

/// `SettingsOwnerDraw::DrawItem`: 1 when the item was painted.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_draw_item(dis: *const DrawItem, ctx: *const OwnerContext) -> i32 {
    guard(0, || unsafe {
        if dis.is_null() || ctx.is_null() {
            return 0;
        }
        i32::from(owner::draw_item(&*dis, &*ctx))
    })
}

/// `SettingsOwnerDraw::CtlColor`: 1 when handled; the brush is in `*brush`
/// (created when null).
#[no_mangle]
pub unsafe extern "C" fn dime_settings_ctl_color(
    hdc: Handle,
    is_dark: i32,
    tc: *const ThemeColors,
    brush: *mut Handle,
) -> i32 {
    guard(0, || unsafe {
        if tc.is_null() || brush.is_null() {
            return 0;
        }
        i32::from(owner::ctl_color(hdc, is_dark != 0, &*tc, &mut *brush))
    })
}

// ---- Phase 5b-8k: command host Win32 calls (dime-settings::winhost) ---------

use dime_settings::winhost;
use std::ptr::null_mut;

/// A NUL-terminated string as units (no cap).
unsafe fn wstr<'a>(p: *const u16) -> &'a [u16] {
    unsafe { cmd_units(p) }
}

/// Copies `s` plus a NUL into `buf` (`cap` units); the full length goes to
/// `len`. Writes nothing past `cap`.
unsafe fn out_str(s: &[u16], buf: *mut u16, cap: usize, len: *mut usize) {
    unsafe {
        write(len, s.len());
        if buf.is_null() || cap == 0 {
            return;
        }
        let n = s.len().min(cap - 1);
        std::ptr::copy_nonoverlapping(s.as_ptr(), buf, n);
        *buf.add(n) = 0;
    }
}

/// `SettingsWinHost::PickFont`: `lf` is a LOGFONTW.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_pick_font(owner: Handle, lf: *mut LogFont, point_size10: *mut i32) -> i32 {
    guard(0, || unsafe {
        if lf.is_null() || point_size10.is_null() {
            return 0;
        }
        i32::from(winhost::pick_font(owner, &mut *lf, &mut *point_size10))
    })
}

/// `SettingsWinHost::PickColor`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_pick_color(owner: Handle, rgb: *mut u32) -> i32 {
    guard(0, || unsafe {
        if rgb.is_null() {
            return 0;
        }
        i32::from(winhost::pick_color(owner, &mut *rgb))
    })
}

/// `SettingsWinHost::OpenFileDialog`: `path` has `MAX_PATH` units.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_open_file(owner: Handle, filter: i32, path: *mut u16) -> i32 {
    guard(0, || unsafe {
        match winhost::open_file_dialog(owner, filter) {
            Some(p) => {
                out_str(&p, path, winhost::MAX_PATH, null_mut());
                1
            }
            None => 0,
        }
    })
}

/// `SettingsWinHost::SaveFileDialog`: `path` (`MAX_PATH` units) holds the
/// initial name and receives the chosen one.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_save_file(owner: Handle, path: *mut u16) -> i32 {
    guard(0, || unsafe {
        if path.is_null() {
            return 0;
        }
        let initial = wstr(path).to_vec();
        match winhost::save_file_dialog(owner, &initial) {
            Some(p) => {
                out_str(&p, path, winhost::MAX_PATH, null_mut());
                1
            }
            None => 0,
        }
    })
}

/// `SettingsWinHost::WriteBomFile`: `text` has `len` units.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_write_bom_file(path: *const u16, text: *const u16, len: usize) -> i32 {
    guard(0, || unsafe {
        if path.is_null() || (text.is_null() && len > 0) {
            return 0;
        }
        let t = if len == 0 {
            &[][..]
        } else {
            std::slice::from_raw_parts(text, len)
        };
        i32::from(winhost::write_bom_file(wstr(path), t))
    })
}

/// `SettingsWinHost::DocumentsPath` into `buf` (`MAX_PATH` units).
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_documents_path(buf: *mut u16) {
    guard((), || unsafe {
        out_str(&winhost::documents_path(), buf, winhost::MAX_PATH, null_mut())
    })
}

#[no_mangle]
pub extern "C" fn dime_settings_win_log_pixels_y(wnd: Handle) -> i32 {
    guard(96, || winhost::log_pixels_y(wnd))
}

/// `SettingsWinHost::EditorText`: call with `cap` 0 for the length, then
/// with `len + 1` units.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_editor_text(re: Handle, buf: *mut u16, cap: usize, len: *mut usize) {
    guard((), || unsafe { out_str(&winhost::editor_text(re), buf, cap, len) })
}

/// `SettingsWinHost::SetEditorTextMuted`: returns the new line count.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_win_set_editor_text_muted(re: Handle, text: *const u16) -> i32 {
    guard(0, || unsafe { winhost::set_editor_text_muted(re, wstr(text)) })
}

#[no_mangle]
pub extern "C" fn dime_settings_win_editor_line_index(re: Handle, line: i32) -> i32 {
    guard(-1, || winhost::editor_line_index(re, line))
}

#[no_mangle]
pub extern "C" fn dime_settings_win_editor_focus_at(re: Handle, cp: i32) {
    guard((), || winhost::editor_focus_at(re, cp))
}

#[no_mangle]
pub extern "C" fn dime_settings_win_editor_line_count(re: Handle) -> i32 {
    guard(0, || winhost::editor_line_count(re))
}

// ---- Phase 5b-9a: window procedures (dime-settings::wndproc) ---------------

use dime_settings::wndproc::{self, ProcHost};

/// `DimeSettingsProcHostVtbl`: the order must match dime_ffi.h and
/// SettingsProc.cpp (`kVtbl`).
#[repr(C)]
pub struct ProcHostVtbl {
    has_data: unsafe extern "C" fn(Ctx) -> i32,
    has_content: unsafe extern "C" fn(Ctx) -> i32,
    create: unsafe extern "C" fn(Ctx, isize),
    nav: unsafe extern "C" fn(Ctx, i32, i32, i32, i32, i32),
    theme_changed: unsafe extern "C" fn(Ctx),
    setting_changed: unsafe extern "C" fn(Ctx),
    dpi_changed: unsafe extern "C" fn(Ctx, u32, *const [i32; 4]),
    paint_main: unsafe extern "C" fn(Ctx),
    paint_content: unsafe extern "C" fn(Ctx),
    screen_dpi: unsafe extern "C" fn(Ctx) -> u32,
    switch_mode: unsafe extern "C" fn(Ctx, i32),
    set_foreground: unsafe extern "C" fn(Ctx),
    destroy: unsafe extern "C" fn(Ctx),
    quit: unsafe extern "C" fn(Ctx),
    command: unsafe extern "C" fn(Ctx, usize, isize),
    editor_revalidate: unsafe extern "C" fn(Ctx),
    editor_viewport: unsafe extern "C" fn(Ctx),
    draw_item: unsafe extern "C" fn(Ctx, isize) -> i32,
    ctl_color: unsafe extern "C" fn(Ctx, isize, *mut isize) -> i32,
    default: unsafe extern "C" fn(Ctx, u32, usize, isize) -> isize,
}

struct ProcVtblHost<'a> {
    v: &'a ProcHostVtbl,
    ctx: Ctx,
}

// SAFETY (every method): the vtable functions are the host thunks; they take
// `ctx` and never unwind.
impl ProcHost for ProcVtblHost<'_> {
    fn has_data(&mut self) -> bool {
        unsafe { (self.v.has_data)(self.ctx) != 0 }
    }
    fn has_content(&mut self) -> bool {
        unsafe { (self.v.has_content)(self.ctx) != 0 }
    }
    fn create(&mut self, instance: isize) {
        unsafe { (self.v.create)(self.ctx, instance) }
    }
    fn nav(&mut self, kind: i32, x: i32, y: i32, delta: i32, thumb: i32) {
        unsafe { (self.v.nav)(self.ctx, kind, x, y, delta, thumb) }
    }
    fn theme_changed(&mut self) {
        unsafe { (self.v.theme_changed)(self.ctx) }
    }
    fn setting_changed(&mut self) {
        unsafe { (self.v.setting_changed)(self.ctx) }
    }
    fn dpi_changed(&mut self, dpi: u32, rect: Option<[i32; 4]>) {
        let p = rect.as_ref().map_or(std::ptr::null(), |r| r as *const [i32; 4]);
        unsafe { (self.v.dpi_changed)(self.ctx, dpi, p) }
    }
    fn paint_main(&mut self) {
        unsafe { (self.v.paint_main)(self.ctx) }
    }
    fn paint_content(&mut self) {
        unsafe { (self.v.paint_content)(self.ctx) }
    }
    fn screen_dpi(&mut self) -> u32 {
        unsafe { (self.v.screen_dpi)(self.ctx) }
    }
    fn switch_mode(&mut self, mode: i32) {
        unsafe { (self.v.switch_mode)(self.ctx, mode) }
    }
    fn set_foreground(&mut self) {
        unsafe { (self.v.set_foreground)(self.ctx) }
    }
    fn destroy(&mut self) {
        unsafe { (self.v.destroy)(self.ctx) }
    }
    fn quit(&mut self) {
        unsafe { (self.v.quit)(self.ctx) }
    }
    fn command(&mut self, wparam: usize, lparam: isize) {
        unsafe { (self.v.command)(self.ctx, wparam, lparam) }
    }
    fn editor_revalidate(&mut self) {
        unsafe { (self.v.editor_revalidate)(self.ctx) }
    }
    fn editor_viewport(&mut self) {
        unsafe { (self.v.editor_viewport)(self.ctx) }
    }
    fn draw_item(&mut self, dis: isize) -> bool {
        unsafe { (self.v.draw_item)(self.ctx, dis) != 0 }
    }
    fn ctl_color(&mut self, hdc: isize) -> Option<isize> {
        let mut brush = 0isize;
        let handled = unsafe { (self.v.ctl_color)(self.ctx, hdc, &mut brush) } != 0;
        handled.then_some(brush)
    }
    fn default(&mut self, msg: u32, wparam: usize, lparam: isize) -> isize {
        unsafe { (self.v.default)(self.ctx, msg, wparam, lparam) }
    }
}

/// `SettingsProc::Main`. `lparam` must be what Windows passes with `msg`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_main_proc(
    vtbl: *const ProcHostVtbl,
    ctx: Ctx,
    msg: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    guard(0, || unsafe {
        if vtbl.is_null() {
            return 0;
        }
        let mut host = ProcVtblHost { v: &*vtbl, ctx };
        wndproc::main_proc(&mut host, msg, wparam, lparam)
    })
}

/// `SettingsProc::Content`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_content_proc(
    vtbl: *const ProcHostVtbl,
    ctx: Ctx,
    msg: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    guard(0, || unsafe {
        if vtbl.is_null() {
            return 0;
        }
        let mut host = ProcVtblHost { v: &*vtbl, ctx };
        wndproc::content_proc(&mut host, msg, wparam, lparam)
    })
}

// ---- Phase 5b-9b: window driver (dime-settings::driver) --------------------

use dime_settings::driver::{self, ControlInfo, DriverHost, DriverState};
use dime_settings::frame::FrameEvent as DriverFrameEvent;
use dime_settings::layout::PlanInput as DriverPlanInput;
use dime_settings::nav::NavEvent as DriverNavEvent;

/// `DimeSettingsDriverHostVtbl`: the order must match dime_ffi.h and
/// SettingsDriver.cpp (`kVtbl`).
#[repr(C)]
pub struct DriverHostVtbl {
    main_dpi: unsafe extern "C" fn(Ctx) -> u32,
    content_dpi: unsafe extern "C" fn(Ctx) -> u32,
    has_content: unsafe extern "C" fn(Ctx) -> i32,
    main_client: unsafe extern "C" fn(Ctx, *mut i32, *mut i32),
    content_client_height: unsafe extern "C" fn(Ctx) -> i32,
    vscroll_width: unsafe extern "C" fn(Ctx) -> i32,
    system_dark: unsafe extern "C" fn(Ctx) -> i32,
    color_mode: unsafe extern "C" fn(Ctx) -> i32,
    save_enabled: unsafe extern "C" fn(Ctx) -> i32,
    preview_height: unsafe extern "C" fn(Ctx, u32) -> i32,
    control_count: unsafe extern "C" fn(Ctx) -> i32,
    control: unsafe extern "C" fn(Ctx, i32, *mut ControlInfo),
    move_control: unsafe extern "C" fn(Ctx, i32, i32, i32, i32, i32),
    show_control: unsafe extern "C" fn(Ctx, i32, i32),
    reset_control_region: unsafe extern "C" fn(Ctx, i32),
    control_checked: unsafe extern "C" fn(Ctx, i32) -> i32,
    set_control_checked: unsafe extern "C" fn(Ctx, i32, i32),
    destroy_controls: unsafe extern "C" fn(Ctx),
    create_controls: unsafe extern "C" fn(Ctx, *const DriverPlanInput, i32),
    populate: unsafe extern "C" fn(Ctx),
    invalidate_main: unsafe extern "C" fn(Ctx, *const [i32; 4], i32),
    invalidate_content: unsafe extern "C" fn(Ctx, i32),
    track_leave: unsafe extern "C" fn(Ctx, i32),
    set_scroll_info: unsafe extern "C" fn(Ctx, i32, i32, i32),
    redraw_content: unsafe extern "C" fn(Ctx),
    update_content: unsafe extern "C" fn(Ctx),
    move_content: unsafe extern "C" fn(Ctx, i32, i32, i32, i32),
    focus_content: unsafe extern "C" fn(Ctx),
    create_content: unsafe extern "C" fn(Ctx, i32, i32, i32, i32),
    content_theme: unsafe extern "C" fn(Ctx, i32),
    set_main_pos: unsafe extern "C" fn(Ctx, i32, i32, i32, i32),
    set_dark_title: unsafe extern "C" fn(Ctx, i32),
    set_caption_color: unsafe extern "C" fn(Ctx, i32),
    theme_brushes: unsafe extern "C" fn(Ctx),
    create_fonts: unsafe extern "C" fn(Ctx, i32),
    destroy_fonts: unsafe extern "C" fn(Ctx),
    ask_save_before_back: unsafe extern "C" fn(Ctx) -> i32,
    click_save: unsafe extern "C" fn(Ctx),
    toggle_snapshot: unsafe extern "C" fn(Ctx, i32, i32),
    apply_and_save: unsafe extern "C" fn(Ctx),
    enumerate_reverse_providers: unsafe extern "C" fn(Ctx),
    load_config: unsafe extern "C" fn(Ctx, i32),
    load_colors: unsafe extern "C" fn(Ctx),
    load_snapshot: unsafe extern "C" fn(Ctx),
    recreate_engine: unsafe extern "C" fn(Ctx, i32),
}

struct DriverVtblHost<'a> {
    v: &'a DriverHostVtbl,
    ctx: Ctx,
}

// SAFETY (every method): the vtable functions are the host thunks; they take
// `ctx` and never unwind.
impl DriverHost for DriverVtblHost<'_> {
    fn main_dpi(&mut self) -> u32 {
        unsafe { (self.v.main_dpi)(self.ctx) }
    }
    fn content_dpi(&mut self) -> u32 {
        unsafe { (self.v.content_dpi)(self.ctx) }
    }
    fn has_content(&mut self) -> bool {
        unsafe { (self.v.has_content)(self.ctx) != 0 }
    }
    fn main_client(&mut self) -> (i32, i32) {
        let (mut w, mut h) = (0, 0);
        unsafe { (self.v.main_client)(self.ctx, &mut w, &mut h) };
        (w, h)
    }
    fn content_client_height(&mut self) -> i32 {
        unsafe { (self.v.content_client_height)(self.ctx) }
    }
    fn vscroll_width(&mut self) -> i32 {
        unsafe { (self.v.vscroll_width)(self.ctx) }
    }
    fn system_dark(&mut self) -> bool {
        unsafe { (self.v.system_dark)(self.ctx) != 0 }
    }
    fn color_mode(&mut self) -> i32 {
        unsafe { (self.v.color_mode)(self.ctx) }
    }
    fn save_enabled(&mut self) -> bool {
        unsafe { (self.v.save_enabled)(self.ctx) != 0 }
    }
    fn preview_height(&mut self, dpi: u32) -> i32 {
        unsafe { (self.v.preview_height)(self.ctx, dpi) }
    }
    fn control_count(&mut self) -> i32 {
        unsafe { (self.v.control_count)(self.ctx) }
    }
    fn control(&mut self, index: i32) -> ControlInfo {
        let mut c = ControlInfo::default();
        unsafe { (self.v.control)(self.ctx, index, &mut c) };
        c
    }
    fn move_control(&mut self, index: i32, x: i32, y: i32, w: i32, h: i32) {
        unsafe { (self.v.move_control)(self.ctx, index, x, y, w, h) }
    }
    fn show_control(&mut self, index: i32, show: bool) {
        unsafe { (self.v.show_control)(self.ctx, index, i32::from(show)) }
    }
    fn reset_control_region(&mut self, index: i32) {
        unsafe { (self.v.reset_control_region)(self.ctx, index) }
    }
    fn control_checked(&mut self, index: i32) -> bool {
        unsafe { (self.v.control_checked)(self.ctx, index) != 0 }
    }
    fn set_control_checked(&mut self, index: i32, on: bool) {
        unsafe { (self.v.set_control_checked)(self.ctx, index, i32::from(on)) }
    }
    fn destroy_controls(&mut self) {
        unsafe { (self.v.destroy_controls)(self.ctx) }
    }
    fn create_controls(&mut self, input: &DriverPlanInput, card_w: i32) {
        unsafe { (self.v.create_controls)(self.ctx, input, card_w) }
    }
    fn populate(&mut self) {
        unsafe { (self.v.populate)(self.ctx) }
    }
    fn invalidate_main(&mut self, rc: Option<[i32; 4]>, erase: bool) {
        let p = rc.as_ref().map_or(std::ptr::null(), |r| r as *const [i32; 4]);
        unsafe { (self.v.invalidate_main)(self.ctx, p, i32::from(erase)) }
    }
    fn invalidate_content(&mut self, erase: bool) {
        unsafe { (self.v.invalidate_content)(self.ctx, i32::from(erase)) }
    }
    fn track_leave(&mut self, content: bool) {
        unsafe { (self.v.track_leave)(self.ctx, i32::from(content)) }
    }
    fn set_scroll_info(&mut self, total: i32, page: i32, pos: i32) {
        unsafe { (self.v.set_scroll_info)(self.ctx, total, page, pos) }
    }
    fn redraw_content(&mut self) {
        unsafe { (self.v.redraw_content)(self.ctx) }
    }
    fn update_content(&mut self) {
        unsafe { (self.v.update_content)(self.ctx) }
    }
    fn move_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
        unsafe { (self.v.move_content)(self.ctx, x, y, w, h) }
    }
    fn focus_content(&mut self) {
        unsafe { (self.v.focus_content)(self.ctx) }
    }
    fn create_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
        unsafe { (self.v.create_content)(self.ctx, x, y, w, h) }
    }
    fn content_theme(&mut self, dark: bool) {
        unsafe { (self.v.content_theme)(self.ctx, i32::from(dark)) }
    }
    fn set_main_pos(&mut self, x: i32, y: i32, w: i32, h: i32) {
        unsafe { (self.v.set_main_pos)(self.ctx, x, y, w, h) }
    }
    fn set_dark_title(&mut self, dark: bool) {
        unsafe { (self.v.set_dark_title)(self.ctx, i32::from(dark)) }
    }
    fn set_caption_color(&mut self, attribute: i32) {
        unsafe { (self.v.set_caption_color)(self.ctx, attribute) }
    }
    fn theme_brushes(&mut self) {
        unsafe { (self.v.theme_brushes)(self.ctx) }
    }
    fn create_fonts(&mut self, dpi: i32) {
        unsafe { (self.v.create_fonts)(self.ctx, dpi) }
    }
    fn destroy_fonts(&mut self) {
        unsafe { (self.v.destroy_fonts)(self.ctx) }
    }
    fn ask_save_before_back(&mut self) -> i32 {
        unsafe { (self.v.ask_save_before_back)(self.ctx) }
    }
    fn click_save(&mut self) {
        unsafe { (self.v.click_save)(self.ctx) }
    }
    fn toggle_snapshot(&mut self, id: i32, on: bool) {
        unsafe { (self.v.toggle_snapshot)(self.ctx, id, i32::from(on)) }
    }
    fn apply_and_save(&mut self) {
        unsafe { (self.v.apply_and_save)(self.ctx) }
    }
    fn enumerate_reverse_providers(&mut self) {
        unsafe { (self.v.enumerate_reverse_providers)(self.ctx) }
    }
    fn load_config(&mut self, mode: i32) {
        unsafe { (self.v.load_config)(self.ctx, mode) }
    }
    fn load_colors(&mut self) {
        unsafe { (self.v.load_colors)(self.ctx) }
    }
    fn load_snapshot(&mut self) {
        unsafe { (self.v.load_snapshot)(self.ctx) }
    }
    fn recreate_engine(&mut self, mode: i32) {
        unsafe { (self.v.recreate_engine)(self.ctx, mode) }
    }
}

/// `SettingsDriver::Nav`. `state` is read and written in place.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_driver_nav(
    vtbl: *const DriverHostVtbl,
    ctx: Ctx,
    state: *mut DriverState,
    ev: *const DriverNavEvent,
) {
    guard((), || unsafe {
        if vtbl.is_null() || state.is_null() || ev.is_null() {
            return;
        }
        let mut host = DriverVtblHost { v: &*vtbl, ctx };
        driver::nav(&mut host, state, *ev);
    })
}

/// `SettingsDriver::Frame`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_driver_frame(
    vtbl: *const DriverHostVtbl,
    ctx: Ctx,
    state: *mut DriverState,
    ev: *const DriverFrameEvent,
) {
    guard((), || unsafe {
        if vtbl.is_null() || state.is_null() || ev.is_null() {
            return;
        }
        let mut host = DriverVtblHost { v: &*vtbl, ctx };
        driver::frame(&mut host, state, *ev);
    })
}

/// `SettingsDriver::SwitchMode`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_driver_switch_mode(
    vtbl: *const DriverHostVtbl,
    ctx: Ctx,
    state: *mut DriverState,
    mode: i32,
) {
    guard((), || unsafe {
        if vtbl.is_null() || state.is_null() {
            return;
        }
        let mut host = DriverVtblHost { v: &*vtbl, ctx };
        driver::switch_mode(&mut host, state, mode);
    })
}

/// `SettingsDriver::Rebuild`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_driver_rebuild(vtbl: *const DriverHostVtbl, ctx: Ctx, state: *mut DriverState) {
    guard((), || unsafe {
        if vtbl.is_null() || state.is_null() {
            return;
        }
        let mut host = DriverVtblHost { v: &*vtbl, ctx };
        driver::rebuild(&mut host, state);
    })
}

// ---- Phase 5b-9c: child windows (dime-settings::children) -----------------

use dime_settings::children::{self, ChildHost, Context as ChildContext};
use dime_settings::controls::ControlSpec;

/// `DimeSettingsChildHostVtbl`: the order must match dime_ffi.h and
/// SettingsChildren.cpp (kVtbl).
#[repr(C)]
pub struct ChildHostVtbl {
    added: unsafe extern "C" fn(Ctx, i32, Handle, i32, i32, i32, i32, i32),
    editor_path: unsafe extern "C" fn(Ctx, *mut u16, usize) -> usize,
    editor_ready: unsafe extern "C" fn(Ctx, Handle, i32),
}

struct ChildVtblHost<'a> {
    v: &'a ChildHostVtbl,
    ctx: Ctx,
}

impl ChildHost for ChildVtblHost<'_> {
    fn added(&mut self, ctrl_id: i32, h: Handle, add: [i32; 4], vis_h: i32) {
        // SAFETY: the C++ host outlives this call; thunks do not throw.
        unsafe { (self.v.added)(self.ctx, ctrl_id, h, add[0], add[1], add[2], add[3], vis_h) }
    }
    fn editor_path(&mut self) -> Vec<u16> {
        // One call (the host's EditorPath runs once, like the C++ original):
        // the longest Win32 path fits.
        const CAP: usize = 32_768;
        let mut buf = vec![0u16; CAP];
        // SAFETY: `buf` holds CAP units; the thunk writes at most CAP - 1 plus a NUL.
        let len = unsafe { (self.v.editor_path)(self.ctx, buf.as_mut_ptr(), CAP) };
        buf.truncate(len.min(CAP - 1));
        buf
    }
    fn editor_ready(&mut self, hre: Handle, line_count: i32) {
        // SAFETY: as above.
        unsafe { (self.v.editor_ready)(self.ctx, hre, line_count) }
    }
}

/// `SettingsChildren::Create`. `specs[i]` text is
/// `text[text_offset..+text_len]`; an out-of-range slice is an empty caption.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_create_children(
    vtbl: *const ChildHostVtbl,
    ctx: Ctx,
    ctx_in: *const ChildContext,
    specs: *const ControlSpecC,
    count: usize,
    text: *const u16,
    text_len: usize,
) {
    guard((), || unsafe {
        if vtbl.is_null() || ctx_in.is_null() || (specs.is_null() && count > 0) {
            return;
        }
        let raw = if count == 0 {
            &[][..]
        } else {
            std::slice::from_raw_parts(specs, count)
        };
        let pool = if text.is_null() || text_len == 0 {
            &[][..]
        } else {
            std::slice::from_raw_parts(text, text_len)
        };
        let list: Vec<ControlSpec> = raw
            .iter()
            .map(|r| {
                let caption = (r.has_text != 0).then(|| {
                    let start = r.text_offset.max(0) as usize;
                    let end = start.saturating_add(r.text_len.max(0) as usize);
                    pool.get(start..end).map_or_else(Vec::new, <[u16]>::to_vec)
                });
                ControlSpec {
                    ctrl_id: r.ctrl_id,
                    menu_id: r.menu_id,
                    role: r.role,
                    control_class: r.control_class,
                    ex_style: r.ex_style,
                    style: r.style,
                    text: caption,
                    create: r.create,
                    add: r.add,
                    vis_h: r.vis_h,
                    set_font: r.set_font != 0,
                    dark_theme: r.dark_theme,
                }
            })
            .collect();
        let mut host = ChildVtblHost { v: &*vtbl, ctx };
        children::create(&mut host, &*ctx_in, &list);
    })
}

// ---- Phase 5b-9d: window shell (dime-settings::shell) ----------------------

use dime_settings::shell::{self, PaintSource, RunParams};

/// `SettingsShell::SavePlacement`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_save_placement(hwnd: Handle, key: *const u16, value: *const u16) -> i32 {
    guard(0, || {
        if key.is_null() || value.is_null() {
            return 0;
        }
        i32::from(shell::save_placement(hwnd, key, value))
    })
}

/// `SettingsShell::RestorePlacement`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_restore_placement(hwnd: Handle, key: *const u16, value: *const u16) -> i32 {
    guard(0, || {
        if key.is_null() || value.is_null() {
            return 0;
        }
        i32::from(shell::restore_placement(hwnd, key, value))
    })
}

/// `SettingsShell::DeletePlacement`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_delete_placement(key: *const u16, value: *const u16) {
    guard((), || {
        if !key.is_null() && !value.is_null() {
            shell::delete_placement(key, value)
        }
    })
}

/// `SettingsShell::CreateBrushes`: `brushes` is HBRUSH[3].
#[no_mangle]
pub unsafe extern "C" fn dime_settings_create_brushes(theme: *const ThemeColors, brushes: *mut [Handle; 3]) {
    guard((), || unsafe {
        if !theme.is_null() && !brushes.is_null() {
            *brushes = shell::create_brushes(&*theme);
        }
    })
}

/// `SettingsShell::DestroyBrushes`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_destroy_brushes(brushes: *mut [Handle; 3]) {
    guard((), || unsafe {
        if !brushes.is_null() {
            shell::destroy_brushes(&mut *brushes);
        }
    })
}

/// `SettingsShell::PaintMain`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_shell_paint_main(hwnd: Handle, src: *const PaintSource) {
    guard((), || unsafe {
        if !src.is_null() {
            shell::paint_main(hwnd, &*src);
        }
    })
}

/// `SettingsShell::PaintContent`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_shell_paint_content(hwnd: Handle, src: *const PaintSource) {
    guard((), || unsafe {
        if !src.is_null() {
            shell::paint_content(hwnd, &*src);
        }
    })
}

/// `SettingsShell::PaintMainInto`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_shell_paint_main_into(hdc: Handle, w: i32, h: i32, src: *const PaintSource) {
    guard((), || unsafe {
        if !src.is_null() {
            shell::paint_main_into(hdc, w, h, &*src);
        }
    })
}

/// `SettingsShell::PaintContentInto`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_shell_paint_content_into(hdc: Handle, w: i32, h: i32, src: *const PaintSource) {
    guard((), || unsafe {
        if !src.is_null() {
            shell::paint_content_into(hdc, w, h, &*src);
        }
    })
}

/// `SettingsShell::RegisterMainClass`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_register_main_class(
    instance: Handle,
    name: *const u16,
    proc_: *const c_void,
    icon_id: i32,
) -> i32 {
    guard(0, || {
        if name.is_null() {
            return 0;
        }
        i32::from(shell::register_main_class(instance, name, proc_, icon_id))
    })
}

/// `SettingsShell::RegisterContentClass`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_register_content_class(
    instance: Handle,
    name: *const u16,
    proc_: *const c_void,
) {
    guard((), || {
        if !name.is_null() {
            shell::register_content_class(instance, name, proc_)
        }
    })
}

/// `SettingsShell::Run`. -1 for a null `params`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_shell_run(params: *const RunParams) -> i32 {
    guard(-1, || unsafe {
        if params.is_null() {
            return -1;
        }
        shell::run(&*params)
    })
}

// ---- Phase 5b-9e: the Rust-owned window (dime-settings::window) ------------

use dime_settings::window::{self, App};

/// `DimeSettingsAppVtbl`: the order must match dime_ffi.h and
/// SettingsApp.cpp (kVtbl).
#[repr(C)]
pub struct AppVtbl {
    dpi_for_window: unsafe extern "C" fn(Ctx, Handle) -> u32,
    system_dark: unsafe extern "C" fn(Ctx) -> i32,
    show_message: unsafe extern "C" fn(Ctx, Handle, *const u16, *const u16, u32) -> i32,
    load_config: unsafe extern "C" fn(Ctx, i32),
    write_config: unsafe extern "C" fn(Ctx, i32),
    reset_all_defaults: unsafe extern "C" fn(Ctx, i32),
    color_mode: unsafe extern "C" fn(Ctx) -> i32,
    set_color_mode: unsafe extern "C" fn(Ctx, i32),
    load_colors_for_mode: unsafe extern "C" fn(Ctx, i32),
    save_colors_for_mode: unsafe extern "C" fn(Ctx, i32),
    get_colors: unsafe extern "C" fn(Ctx, *mut u32),
    set_color: unsafe extern "C" fn(Ctx, i32, u32),
    set_reverse_selection: unsafe extern "C" fn(Ctx, i32),
    enumerate_reverse_providers: unsafe extern "C" fn(Ctx),
    reverse_count: unsafe extern "C" fn(Ctx) -> i32,
    reverse_description: unsafe extern "C" fn(Ctx, i32, *mut u16, usize) -> usize,
    reverse_is_current: unsafe extern "C" fn(Ctx, i32) -> i32,
    reverse_is_null: unsafe extern "C" fn(Ctx) -> i32,
    load_snapshot: unsafe extern "C" fn(Ctx, *mut SettingsSnapshot),
    apply_snapshot: unsafe extern "C" fn(Ctx, *const SettingsSnapshot),
    parse_cin: unsafe extern "C" fn(Ctx, *const u16, *const u16, i32) -> i32,
    recreate_engine: unsafe extern "C" fn(Ctx, i32),
    destroy_engine: unsafe extern "C" fn(Ctx),
    has_engine: unsafe extern "C" fn(Ctx) -> i32,
    engine_char_ok: unsafe extern "C" fn(Ctx, u16) -> i32,
    custom_table_txt_path: unsafe extern "C" fn(Ctx, i32, *mut u16, usize) -> usize,
    custom_table_cin_path: unsafe extern "C" fn(Ctx, i32, *mut u16, usize) -> usize,
    app_data_path: unsafe extern "C" fn(Ctx, *mut u16, usize) -> usize,
    windows_1809_or_later: unsafe extern "C" fn(Ctx) -> i32,
}

/// A `SettingsApp::Host` through its vtable. The C++ host outlives the window.
struct VtblApp {
    v: &'static AppVtbl,
    ctx: Ctx,
}

/// One call with room for the longest Win32 path (the thunks write at most
/// `cap - 1` units plus a NUL and return the full length).
fn take_string(f: impl FnOnce(*mut u16, usize) -> usize) -> Vec<u16> {
    const CAP: usize = 32_768;
    let mut buf = vec![0u16; CAP];
    let len = f(buf.as_mut_ptr(), CAP);
    buf.truncate(len.min(CAP - 1));
    buf
}

fn z(s: &[u16]) -> Vec<u16> {
    let mut v = s.to_vec();
    v.push(0);
    v
}

// SAFETY (every unsafe block below): the vtable and ctx come from C++ and
// outlive the window; thunks do not throw; strings passed are NUL-terminated
// copies that outlive the call.
impl App for VtblApp {
    fn dpi_for_window(&self, hwnd: Handle) -> u32 {
        unsafe { (self.v.dpi_for_window)(self.ctx, hwnd) }
    }
    fn system_dark(&self) -> bool {
        unsafe { (self.v.system_dark)(self.ctx) != 0 }
    }
    fn show_message(&self, parent: Handle, text: &[u16], caption: &[u16], mb_type: u32) -> i32 {
        let (t, c) = (z(text), z(caption));
        unsafe { (self.v.show_message)(self.ctx, parent, t.as_ptr(), c.as_ptr(), mb_type) }
    }
    fn load_config(&self, mode: i32) {
        unsafe { (self.v.load_config)(self.ctx, mode) }
    }
    fn write_config(&self, mode: i32) {
        unsafe { (self.v.write_config)(self.ctx, mode) }
    }
    fn reset_all_defaults(&self, mode: i32) {
        unsafe { (self.v.reset_all_defaults)(self.ctx, mode) }
    }
    fn color_mode(&self) -> i32 {
        unsafe { (self.v.color_mode)(self.ctx) }
    }
    fn set_color_mode(&self, color_mode: i32) {
        unsafe { (self.v.set_color_mode)(self.ctx, color_mode) }
    }
    fn load_colors_for_mode(&self, color_mode: i32) {
        unsafe { (self.v.load_colors_for_mode)(self.ctx, color_mode) }
    }
    fn save_colors_for_mode(&self, color_mode: i32) {
        unsafe { (self.v.save_colors_for_mode)(self.ctx, color_mode) }
    }
    fn get_colors(&self) -> [u32; 6] {
        let mut out = [0u32; 6];
        unsafe { (self.v.get_colors)(self.ctx, out.as_mut_ptr()) };
        out
    }
    fn set_color(&self, index: i32, color: u32) {
        unsafe { (self.v.set_color)(self.ctx, index, color) }
    }
    fn set_reverse_selection(&self, sel: i32) {
        unsafe { (self.v.set_reverse_selection)(self.ctx, sel) }
    }
    fn enumerate_reverse_providers(&self) {
        unsafe { (self.v.enumerate_reverse_providers)(self.ctx) }
    }
    fn reverse_count(&self) -> i32 {
        unsafe { (self.v.reverse_count)(self.ctx) }
    }
    fn reverse_description(&self, index: i32) -> Vec<u16> {
        take_string(|b, c| unsafe { (self.v.reverse_description)(self.ctx, index, b, c) })
    }
    fn reverse_is_current(&self, index: i32) -> bool {
        unsafe { (self.v.reverse_is_current)(self.ctx, index) != 0 }
    }
    fn reverse_is_null(&self) -> bool {
        unsafe { (self.v.reverse_is_null)(self.ctx) != 0 }
    }
    fn load_snapshot(&self) -> SettingsSnapshot {
        let mut s = SettingsSnapshot::default();
        unsafe { (self.v.load_snapshot)(self.ctx, &mut s) };
        s
    }
    fn apply_snapshot(&self, s: &SettingsSnapshot) {
        unsafe { (self.v.apply_snapshot)(self.ctx, s) }
    }
    fn parse_cin(&self, src: &[u16], dst: &[u16], custom_table: bool) -> bool {
        let (s, d) = (z(src), z(dst));
        unsafe { (self.v.parse_cin)(self.ctx, s.as_ptr(), d.as_ptr(), i32::from(custom_table)) != 0 }
    }
    fn recreate_engine(&self, mode: i32) {
        unsafe { (self.v.recreate_engine)(self.ctx, mode) }
    }
    fn destroy_engine(&self) {
        unsafe { (self.v.destroy_engine)(self.ctx) }
    }
    fn has_engine(&self) -> bool {
        unsafe { (self.v.has_engine)(self.ctx) != 0 }
    }
    fn engine_char_ok(&self, ch: u16) -> bool {
        unsafe { (self.v.engine_char_ok)(self.ctx, ch) != 0 }
    }
    fn custom_table_txt_path(&self, mode: i32) -> Vec<u16> {
        take_string(|b, c| unsafe { (self.v.custom_table_txt_path)(self.ctx, mode, b, c) })
    }
    fn custom_table_cin_path(&self, mode: i32) -> Vec<u16> {
        take_string(|b, c| unsafe { (self.v.custom_table_cin_path)(self.ctx, mode, b, c) })
    }
    fn app_data_path(&self) -> Vec<u16> {
        take_string(|b, c| unsafe { (self.v.app_data_path)(self.ctx, b, c) })
    }
    fn windows_1809_or_later(&self) -> bool {
        unsafe { (self.v.windows_1809_or_later)(self.ctx) != 0 }
    }
}

/// `SettingsWindow::Run` (Rust window). -1 for a null vtable.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_window_run(
    vtbl: *const AppVtbl,
    app: Ctx,
    instance: Handle,
    n_cmd_show: i32,
    mode: i32,
    caption: *const u16,
    icon_id: i32,
) -> i32 {
    guard(-1, || unsafe {
        if vtbl.is_null() {
            return -1;
        }
        let a = Box::new(VtblApp { v: &*vtbl, ctx: app });
        window::run(a, instance, n_cmd_show, mode, wstr(caption), icon_id)
    })
}

/// `SettingsWindow::CreateForTest` (Rust window). Null for a null vtable.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_window_create(
    vtbl: *const AppVtbl,
    app: Ctx,
    instance: Handle,
    mode: i32,
    caption: *const u16,
    main_class: *const u16,
    content_class: *const u16,
    placement_key: *const u16,
    placement_value: *const u16,
    icon_id: i32,
) -> Handle {
    guard(null_mut(), || unsafe {
        if vtbl.is_null() || main_class.is_null() || content_class.is_null() {
            return null_mut();
        }
        let a = Box::new(VtblApp { v: &*vtbl, ctx: app });
        window::create(
            a,
            instance,
            mode,
            wstr(caption),
            wstr(main_class),
            wstr(content_class),
            wstr(placement_key),
            wstr(placement_value),
            icon_id,
        )
    })
}

/// `SettingsWindow::StateForTest`: 0 when the window has no data.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_window_state(
    hwnd: Handle,
    state: *mut DriverState,
    snapshot: *mut SettingsSnapshot,
) -> i32 {
    guard(0, || unsafe {
        match window::state(hwnd) {
            Some((st, sn)) => {
                write(state, st);
                write(snapshot, sn);
                1
            }
            None => 0,
        }
    })
}

/// `SettingsWindow::NavigateForTest`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_window_navigate(
    hwnd: Handle,
    mode: i32,
    selected_mode_index: i32,
    nav_level: i32,
    card_index: i32,
) {
    guard((), || {
        window::navigate(hwnd, mode, selected_mode_index, nav_level, card_index)
    })
}

/// `SettingsWindow::PaintForTest`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_window_paint(hwnd: Handle, hdc: Handle, w: i32, h: i32, content: i32) {
    guard((), || window::paint_into(hwnd, hdc, w, h, content != 0))
}
