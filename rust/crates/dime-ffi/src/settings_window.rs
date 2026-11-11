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
