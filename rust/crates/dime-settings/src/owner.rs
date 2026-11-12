//! Owner-drawn controls and dark control colours (phase 5b-8j).
//!
//! Port of `SettingsOwnerDraw::Cpp` (`src/DIMESettings/SettingsOwnerDraw.cpp`,
//! formerly inline in the content area's `WM_DRAWITEM` and
//! `WM_CTLCOLOREDIT` / `WM_CTLCOLORLISTBOX`).

// HWND / HDC / HBRUSH are opaque handles passed to Win32, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use dime_config::geometry::{defaults, ThemeColors};

use crate::layout::scale;
use crate::paint::{paint_button, paint_swatch, Handle, Rect};

windows_link::link!("user32.dll" "system" fn GetWindowTextW(wnd: Handle, buf: *mut u16, cap: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn CreateSolidBrush(color: u32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn SetTextColor(hdc: Handle, color: u32) -> u32);
windows_link::link!("gdi32.dll" "system" fn SetBkColor(hdc: Handle, color: u32) -> u32);

const ODT_BUTTON: u32 = 4;
const ODT_STATIC: u32 = 5;
const ODS_SELECTED: u32 = 0x1;
const ODS_DISABLED: u32 = 0x4;
/// HMENU ids of the six colour swatches.
const SWATCH_FIRST: u32 = 4000;
const SWATCH_COUNT: u32 = 6;

/// `DRAWITEMSTRUCT`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DrawItem {
    pub ctl_type: u32,
    pub ctl_id: u32,
    pub item_id: u32,
    pub item_action: u32,
    pub item_state: u32,
    pub hwnd_item: Handle,
    pub hdc: Handle,
    pub rc_item: Rect,
    pub item_data: usize,
}

/// `SettingsOwnerDraw::Context`: what the owner-drawn controls read.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Context {
    pub theme: ThemeColors,
    pub font_body: Handle,
    /// The main window's dpi.
    pub dpi: u32,
    /// Candidate, selected, candidate background, phrase, number, selected
    /// background (the swatch order).
    pub swatch: [u32; 6],
}

/// `SettingsOwnerDraw::Cpp::DrawItem`: owner-drawn buttons and the colour
/// swatches; false for anything else (the window procedure falls through).
pub fn draw_item(dis: &DrawItem, ctx: &Context) -> bool {
    if dis.ctl_type == ODT_BUTTON {
        let mut buf = [0u16; 128];
        // SAFETY: buf has 128 units; GetWindowTextW writes at most that.
        unsafe { GetWindowTextW(dis.hwnd_item, buf.as_mut_ptr(), 128) };
        let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
        paint_button(
            dis.hdc,
            dis.rc_item,
            &buf[..len],
            dis.item_state & ODS_SELECTED != 0,
            dis.item_state & ODS_DISABLED != 0,
            &ctx.theme,
            ctx.font_body,
            ctx.dpi,
        );
        return true;
    }
    if dis.ctl_type == ODT_STATIC && (SWATCH_FIRST..SWATCH_FIRST + SWATCH_COUNT).contains(&dis.ctl_id) {
        let idx = (dis.ctl_id - SWATCH_FIRST) as usize;
        let r = scale(defaults().swatch_radius, ctx.dpi);
        paint_swatch(dis.hdc, dis.rc_item, ctx.swatch[idx], ctx.theme.card_border, r);
        return true;
    }
    false
}

/// `SettingsOwnerDraw::Cpp::CtlColor`: in the dark theme, edits and combo
/// lists get the card colours and the card brush (created on first use).
/// Returns whether the message is handled; the brush is then `*brush`.
pub fn ctl_color(hdc: Handle, is_dark: bool, theme: &ThemeColors, brush: &mut Handle) -> bool {
    if !is_dark {
        return false;
    }
    // SAFETY: plain GDI calls on the caller's DC.
    unsafe {
        SetTextColor(hdc, theme.text_primary);
        SetBkColor(hdc, theme.card_bg);
        if brush.is_null() {
            *brush = CreateSolidBrush(theme.card_bg);
        }
    }
    true
}
