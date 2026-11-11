//! Settings geometry and theme colors (phase 5b-3).
//!
//! Field order matches `SettingsGeometry` and `ThemeColors` in
//! `src/DIMESettings/SettingsGeometry.h`. `GetSysColor` stays in C++ and is
//! passed in as [`SysColors`]. `SettingsLayout.ini` is not read.
//!
//! `scale_for_dpi` uses the same `MulDiv` as `dime-ui::candwnd::mul_div`.
//! x86 and x64 differ at `i32::MIN`. `dpi <= 0` returns `value` and does
//! not call `MulDiv`. That is the `(int, int)` overload. The `(int, UINT)`
//! overload in `BaseStructure.h` always calls `MulDiv`.

/// `DARK_DIALOG_BG` in `Define.h`: `RGB(32, 32, 32)`.
const DARK_DIALOG_BG: u32 = rgb(32, 32, 32);
/// `DARK_CONTROL_BG` in `Define.h`: `RGB(45, 45, 48)`.
const DARK_CONTROL_BG: u32 = rgb(45, 45, 48);

const fn rgb(r: u32, g: u32, b: u32) -> u32 {
    r | (g << 8) | (b << 16)
}

/// `SettingsGeometry`. Every field is an `int` at 96 dpi.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SettingsGeometry {
    pub sidebar_width: i32,
    pub sidebar_item_h: i32,
    pub sidebar_item_r: i32,
    pub sidebar_icon_size: i32,
    pub sidebar_icon_gap: i32,
    pub sidebar_item_pad_left: i32,
    pub sidebar_accent_bar_w: i32,
    pub sidebar_accent_bar_h: i32,
    pub sidebar_accent_bar_r: i32,
    pub sidebar_collapse_break: i32,
    pub hamburger_size: i32,
    pub content_margin: i32,
    pub top_margin: i32,
    pub breadcrumb_h: i32,
    pub card_radius: i32,
    pub card_pad: i32,
    pub card_gap: i32,
    pub row_height: i32,
    pub row_height_single: i32,
    pub category_row_h: i32,
    pub child_row_h: i32,
    pub row_icon_size: i32,
    pub row_icon_pad_left: i32,
    pub row_icon_gap: i32,
    pub row_sep_indent: i32,
    pub toggle_w: i32,
    pub toggle_h: i32,
    pub toggle_knob: i32,
    pub toggle_knob_border_w: i32,
    pub toggle_label_gap: i32,
    pub combo_width: i32,
    pub edit_width: i32,
    pub button_width: i32,
    pub control_right_margin: i32,
    pub swatch_size: i32,
    pub swatch_radius: i32,
    pub font_title_pt: i32,
    pub font_card_header_pt: i32,
    pub font_body_pt: i32,
    pub font_desc_pt: i32,
    pub editor_card_h: i32,
    pub editor_header_h: i32,
    pub top_pad: i32,
    pub sidebar_start_y: i32,
    pub title_area_h: i32,
    pub row_label_top_off: i32,
    pub row_label_mid_off: i32,
    pub row_desc_bot_off: i32,
    pub preview_gap: i32,
    pub ctrl_height: i32,
    pub button_height: i32,
    pub action_btn_w: i32,
    pub toggle_label_w: i32,
    pub toggle_label_gap_r: i32,
    pub chevron_w: i32,
    pub bc_mode_name_w: i32,
    pub bc_sep_w: i32,
    pub bc_cat_name_w: i32,
    pub combo_drop_h: i32,
    pub swatch_label_h: i32,
    pub swatch_cell_gap: i32,
    pub window_min_w: i32,
    pub window_min_h: i32,
    pub window_default_w: i32,
    pub window_default_h: i32,
}

const DEFAULTS: SettingsGeometry = SettingsGeometry {
    sidebar_width: 280,
    sidebar_item_h: 40,
    sidebar_item_r: 4,
    sidebar_icon_size: 20,
    sidebar_icon_gap: 10,
    sidebar_item_pad_left: 14,
    sidebar_accent_bar_w: 3,
    sidebar_accent_bar_h: 16,
    sidebar_accent_bar_r: 2,
    sidebar_collapse_break: 800,
    hamburger_size: 36,
    content_margin: 36,
    top_margin: 56,
    breadcrumb_h: 44,
    card_radius: 8,
    card_pad: 10,
    card_gap: 4,
    row_height: 49,
    row_height_single: 38,
    category_row_h: 49,
    child_row_h: 50,
    row_icon_size: 20,
    row_icon_pad_left: 20,
    row_icon_gap: 14,
    row_sep_indent: 54,
    toggle_w: 40,
    toggle_h: 20,
    toggle_knob: 16,
    toggle_knob_border_w: 2,
    toggle_label_gap: 8,
    combo_width: 180,
    edit_width: 120,
    button_width: 120,
    control_right_margin: 16,
    swatch_size: 28,
    swatch_radius: 4,
    font_title_pt: 20,
    font_card_header_pt: 10,
    font_body_pt: 10,
    font_desc_pt: 9,
    editor_card_h: 400,
    editor_header_h: 52,
    top_pad: 8,
    sidebar_start_y: 56,
    title_area_h: 40,
    row_label_top_off: 0,
    row_label_mid_off: 5,
    row_desc_bot_off: 0,
    preview_gap: 28,
    ctrl_height: 24,
    button_height: 28,
    action_btn_w: 80,
    toggle_label_w: 50,
    toggle_label_gap_r: 6,
    chevron_w: 20,
    bc_mode_name_w: 200,
    bc_sep_w: 30,
    bc_cat_name_w: 300,
    combo_drop_h: 200,
    swatch_label_h: 14,
    swatch_cell_gap: 2,
    window_min_w: 520,
    window_min_h: 450,
    window_default_w: 900,
    window_default_h: 650,
};

/// `ThemeColors`. Each field is a `COLORREF`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ThemeColors {
    pub content_bg: u32,
    pub card_bg: u32,
    pub card_border: u32,
    pub text_primary: u32,
    pub text_secondary: u32,
    pub row_separator: u32,
    pub row_hover: u32,
    pub sidebar_bg: u32,
    pub sidebar_hover: u32,
    pub sidebar_select: u32,
    pub accent: u32,
    pub toggle_on_track: u32,
    pub toggle_off_border: u32,
    pub toggle_on_knob: u32,
    pub toggle_off_knob: u32,
    pub button_fill: u32,
    pub button_fill_hover: u32,
    pub button_fill_pressed: u32,
    pub button_fill_disabled: u32,
    pub button_border: u32,
    pub button_text: u32,
    pub button_text_disabled: u32,
}

/// System colors `ComputeThemeColors` reads. C++ fills this with `GetSysColor`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SysColors {
    pub hotlight: u32,
    pub face: u32,
    pub window: u32,
    pub window_text: u32,
    pub gray_text: u32,
    pub highlight_text: u32,
}

pub fn defaults() -> &'static SettingsGeometry {
    &DEFAULTS
}

/// `BlendColor`. `alpha <= 0` returns `c1` unchanged, `alpha >= 255` returns
/// `c2` unchanged. The blended result clears the high byte, matching `RGB()`.
pub fn blend_color(c1: u32, c2: u32, alpha: i32) -> u32 {
    if alpha <= 0 {
        return c1;
    }
    if alpha >= 255 {
        return c2;
    }
    let mix = |shift: u32| -> u32 {
        let a = ((c1 >> shift) & 0xff) as i32;
        let b = ((c2 >> shift) & 0xff) as i32;
        ((a * (255 - alpha) + b * alpha) / 255) as u32
    };
    mix(0) | (mix(8) << 8) | (mix(16) << 16)
}

/// `(int, int)` `ScaleForDpi`.
pub fn scale_for_dpi(value: i32, dpi: i32) -> i32 {
    if dpi > 0 {
        mul_div(value, dpi, 96)
    } else {
        value
    }
}

/// `ComputeThemeColors`. `is_dark` selects the constant dark palette.
pub fn theme_colors(is_dark: bool, sys: SysColors) -> ThemeColors {
    let accent = sys.hotlight;
    if is_dark {
        let white = rgb(255, 255, 255);
        let card_border = blend_color(DARK_CONTROL_BG, rgb(0, 0, 0), 40);
        let text_secondary = blend_color(DARK_DIALOG_BG, white, 157);
        ThemeColors {
            content_bg: DARK_DIALOG_BG,
            card_bg: DARK_CONTROL_BG,
            card_border,
            text_primary: blend_color(DARK_DIALOG_BG, white, 235),
            text_secondary,
            row_separator: card_border,
            row_hover: blend_color(DARK_CONTROL_BG, white, 15),
            sidebar_bg: DARK_DIALOG_BG,
            sidebar_hover: blend_color(DARK_DIALOG_BG, white, 15),
            sidebar_select: blend_color(DARK_DIALOG_BG, white, 18),
            accent,
            toggle_on_track: accent,
            toggle_off_border: blend_color(DARK_DIALOG_BG, white, 140),
            toggle_on_knob: DARK_DIALOG_BG,
            toggle_off_knob: blend_color(DARK_DIALOG_BG, white, 180),
            button_fill: accent,
            button_fill_hover: blend_color(accent, white, 30),
            button_fill_pressed: blend_color(accent, DARK_DIALOG_BG, 55),
            button_fill_disabled: blend_color(accent, DARK_DIALOG_BG, 160),
            button_border: blend_color(accent, DARK_DIALOG_BG, 30),
            button_text: blend_color(DARK_DIALOG_BG, white, 255),
            button_text_disabled: text_secondary,
        }
    } else {
        let face = sys.face;
        let window = sys.window;
        let black = rgb(0, 0, 0);
        let hotlight = sys.hotlight;
        let card_border = blend_color(face, black, 3);
        ThemeColors {
            content_bg: face,
            card_bg: blend_color(window, face, 30),
            card_border,
            text_primary: sys.window_text,
            text_secondary: sys.gray_text,
            row_separator: card_border,
            row_hover: blend_color(window, black, 10),
            sidebar_bg: face,
            sidebar_hover: blend_color(face, black, 20),
            sidebar_select: blend_color(face, black, 25),
            accent,
            toggle_on_track: accent,
            toggle_off_border: sys.gray_text,
            toggle_on_knob: sys.highlight_text,
            toggle_off_knob: blend_color(sys.window_text, window, 170),
            button_fill: hotlight,
            button_fill_hover: blend_color(hotlight, black, 30),
            button_fill_pressed: blend_color(hotlight, black, 55),
            button_fill_disabled: blend_color(hotlight, face, 160),
            button_border: blend_color(hotlight, black, 30),
            button_text: sys.highlight_text,
            button_text_disabled: sys.gray_text,
        }
    }
}

/// Win32 `MulDiv` of the process (`dime-ui::candwnd::mul_div`).
/// Win32 `MulDiv` for the target architecture (x86 and x64 differ on `i32::MIN`).
pub fn mul_div(a: i32, b: i32, c: i32) -> i32 {
    if cfg!(target_arch = "x86") {
        mul_div_x86(a, b, c)
    } else {
        mul_div_x64(a, b, c)
    }
}

fn mul_div_x64(a: i32, b: i32, c: i32) -> i32 {
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

fn mul_div_x86(a: i32, b: i32, c: i32) -> i32 {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn geometry_layout_and_defaults() {
        use std::mem::{align_of, offset_of, size_of};
        assert_eq!(size_of::<SettingsGeometry>(), 260);
        assert_eq!(align_of::<SettingsGeometry>(), 4);
        assert_eq!(offset_of!(SettingsGeometry, sidebar_width), 0);
        assert_eq!(offset_of!(SettingsGeometry, window_default_h), 256);
        assert_eq!(size_of::<ThemeColors>(), 88);
        assert_eq!(align_of::<ThemeColors>(), 4);
        assert_eq!(offset_of!(ThemeColors, accent), 40);
        assert_eq!(offset_of!(ThemeColors, button_text_disabled), 84);
        assert_eq!(size_of::<SysColors>(), 24);

        let g = defaults();
        assert_eq!(g.sidebar_width, 280);
        assert_eq!(g.sidebar_collapse_break, 800);
        assert_eq!(g.hamburger_size, 36);
        assert_eq!(g.content_margin, 36);
        assert_eq!(g.card_pad, 10);
        assert_eq!(g.row_height, 49);
        assert_eq!(g.row_height_single, 38);
        assert_eq!(g.child_row_h, 50);
        assert_eq!(g.toggle_w, 40);
        assert_eq!(g.combo_width, 180);
        assert_eq!(g.font_title_pt, 20);
        assert_eq!(g.font_desc_pt, 9);
        assert_eq!(g.editor_card_h, 400);
        assert_eq!(g.editor_header_h, 52);
        assert_eq!(g.row_label_mid_off, 5);
        assert_eq!(g.row_label_top_off, 0);
        assert_eq!(g.preview_gap, 28);
        assert_eq!(g.ctrl_height, 24);
        assert_eq!(g.action_btn_w, 80);
        assert_eq!(g.chevron_w, 20);
        assert_eq!(g.bc_cat_name_w, 300);
        assert_eq!(g.combo_drop_h, 200);
        assert_eq!(g.swatch_label_h, 14);
        assert_eq!(g.window_min_w, 520);
        assert_eq!(g.window_default_w, 900);
        assert_eq!(g.window_default_h, 650);
    }

    #[test]
    fn blend_and_scale_match_cpp() {
        assert_eq!(blend_color(0xAA01_0203, 0xBB04_0506, 0), 0xAA01_0203);
        assert_eq!(blend_color(0xAA01_0203, 0xBB04_0506, -1), 0xAA01_0203);
        assert_eq!(blend_color(0xAA01_0203, 0xBB04_0506, 255), 0xBB04_0506);
        assert_eq!(blend_color(0xAA01_0203, 0xBB04_0506, 256), 0xBB04_0506);
        assert_eq!(blend_color(0xAA01_0203, 0xBB04_0506, 128), rgb(4, 3, 2));
        assert_eq!(blend_color(DARK_CONTROL_BG, 0, 40), rgb(37, 37, 40));
        assert_eq!(blend_color(DARK_DIALOG_BG, rgb(255, 255, 255), 235), rgb(237, 237, 237));

        assert_eq!(scale_for_dpi(16, 0), 16);
        assert_eq!(scale_for_dpi(16, -1), 16);
        assert_eq!(scale_for_dpi(16, 96), 16);
        assert_eq!(scale_for_dpi(16, 120), 20);
        assert_eq!(scale_for_dpi(16, 144), 24);
        assert_eq!(scale_for_dpi(16, 192), 32);
        assert_eq!(scale_for_dpi(5, 144), mul_div(5, 144, 96));
        assert_eq!(scale_for_dpi(7, 120), mul_div(7, 120, 96));
        assert_eq!(scale_for_dpi(i32::MIN, 96), mul_div(i32::MIN, 96, 96));
        // x64 rejects a factor of i32::MIN. x86 rounds it like any other factor.
        let edge = mul_div(i32::MIN, 1, 2);
        if cfg!(target_arch = "x86") {
            assert_eq!(edge, -1_073_741_824);
        } else {
            assert_eq!(edge, -1);
        }
    }

    #[test]
    fn theme_matches_cpp_cases() {
        let sys = SysColors {
            hotlight: rgb(0, 120, 215),
            face: rgb(240, 240, 240),
            window: rgb(255, 255, 255),
            window_text: rgb(0, 0, 0),
            gray_text: rgb(128, 128, 128),
            highlight_text: rgb(255, 255, 255),
        };
        let dark = theme_colors(true, sys);
        assert_eq!(dark.content_bg, DARK_DIALOG_BG);
        assert_eq!(dark.card_bg, DARK_CONTROL_BG);
        assert_eq!(dark.card_border, rgb(37, 37, 40));
        assert_eq!(dark.text_primary, rgb(237, 237, 237));
        assert_eq!(dark.row_separator, dark.card_border);
        assert_eq!(dark.accent, sys.hotlight);
        assert_eq!(dark.toggle_on_track, sys.hotlight);
        assert_eq!(dark.toggle_on_knob, DARK_DIALOG_BG);
        assert_eq!(dark.button_fill, sys.hotlight);
        assert_eq!(dark.button_text, rgb(255, 255, 255));
        assert_eq!(dark.button_text_disabled, dark.text_secondary);
        assert_eq!(dark.sidebar_bg, DARK_DIALOG_BG);

        let light = theme_colors(false, sys);
        assert_eq!(light.content_bg, sys.face);
        assert_eq!(light.card_bg, rgb(253, 253, 253));
        assert_eq!(light.card_border, rgb(237, 237, 237));
        assert_eq!(light.text_primary, sys.window_text);
        assert_eq!(light.text_secondary, sys.gray_text);
        assert_eq!(light.row_separator, light.card_border);
        assert_eq!(light.toggle_off_border, sys.gray_text);
        assert_eq!(light.toggle_on_knob, sys.highlight_text);
        assert_eq!(light.button_fill, sys.hotlight);
        assert_eq!(light.button_text, sys.highlight_text);
        assert_eq!(light.button_text_disabled, sys.gray_text);
        assert_eq!(light.sidebar_bg, sys.face);
    }
}
