//! Which child controls a settings page creates (phase 5b-8f).
//!
//! Port of `SettingsControls::Cpp::BuildSpecs` (`src/DIMESettings/
//! SettingsControls.cpp`, formerly `CreateRowControl` /
//! `CreateColorGridControls` / `CreateRichEditorControl`): one spec per
//! child window, in creation order.

use dime_config::geometry::defaults;
use dime_config::page_layout::{
    LayoutCard, ACT_EXPORT, ACT_FONT, ACT_IMPORT, ACT_LOAD, ACT_RESET, TY_BUTTON, TY_CLICKABLE, TY_COMBO, TY_EDIT,
    TY_TOGGLE,
};

use crate::layout::{
    card_rows, ctrl_right, plan_controls, rich_editor_rects, row_control_rect, scale, swatch_rect, CtrlRect, PlanInput,
};

pub const CLS_BUTTON: i32 = 0;
pub const CLS_COMBOBOX: i32 = 1;
pub const CLS_EDIT: i32 = 2;
pub const CLS_STATIC: i32 = 3;
pub const CLS_RICHEDIT: i32 = 4;

pub const ROLE_PLAIN: i32 = 0;
pub const ROLE_RICH_EDITOR: i32 = 1;
pub const ROLE_SAVE_BUTTON: i32 = 2;

pub const DARK_NONE: i32 = 0;
pub const DARK_CFD: i32 = 1;
pub const DARK_EXPLORER: i32 = 2;

const WS_CHILD: u32 = 0x4000_0000;
const WS_VISIBLE: u32 = 0x1000_0000;
const WS_DISABLED: u32 = 0x0800_0000;
const WS_VSCROLL: u32 = 0x0020_0000;
const WS_EX_CLIENTEDGE: u32 = 0x0000_0200;
const BS_AUTOCHECKBOX: u32 = 0x3;
const BS_OWNERDRAW: u32 = 0xB;
const CBS_DROPDOWNLIST: u32 = 0x3;
const ES_MULTILINE: u32 = 0x4;
const ES_AUTOVSCROLL: u32 = 0x40;
const ES_AUTOHSCROLL: u32 = 0x80;
const ES_WANTRETURN: u32 = 0x1000;
const SS_OWNERDRAW: u32 = 0xD;
const SS_NOTIFY: u32 = 0x100;

const CTRL_COLOR_FR: i32 = 5;
const CTRL_CUSTOM_TABLE_SAVE: i32 = 201;

/// `SettingsControls::ControlSpec`. Rectangles are x, y, w, h.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ControlSpec {
    pub ctrl_id: i32,
    pub menu_id: i32,
    pub role: i32,
    pub control_class: i32,
    pub ex_style: u32,
    pub style: u32,
    /// `None`: a null caption.
    pub text: Option<Vec<u16>>,
    pub create: [i32; 4],
    pub add: [i32; 4],
    pub vis_h: i32,
    pub set_font: bool,
    pub dark_theme: i32,
}

fn rect4(r: &CtrlRect) -> [i32; 4] {
    [r.x, r.y, r.w, r.h]
}

#[allow(clippy::too_many_arguments)]
fn spec(
    ctrl_id: i32,
    menu_id: i32,
    role: i32,
    class: i32,
    ex_style: u32,
    style: u32,
    text: Option<Vec<u16>>,
    create: [i32; 4],
    add: [i32; 4],
    vis_h: i32,
    set_font: bool,
    dark: i32,
) -> ControlSpec {
    ControlSpec {
        ctrl_id,
        menu_id,
        role,
        control_class: class,
        ex_style,
        style,
        text,
        create,
        add,
        vis_h,
        set_font,
        dark_theme: dark,
    }
}

/// A NUL-terminated table string, or `None` for null.
fn label(p: *const u16) -> Option<Vec<u16>> {
    if p.is_null() {
        return None;
    }
    let mut v = Vec::new();
    // SAFETY: the layout tables hold NUL-terminated static strings.
    unsafe {
        let mut i = 0;
        while *p.add(i) != 0 {
            v.push(*p.add(i));
            i += 1;
        }
    }
    Some(v)
}

fn w(s: &str) -> Option<Vec<u16>> {
    Some(s.encode_utf16().collect())
}

/// `SettingsControls::Cpp::BuildSpecs`.
pub fn build_specs(input: &PlanInput, card_w: i32, ime: &[LayoutCard], load: &[LayoutCard]) -> Vec<ControlSpec> {
    let g = defaults();
    let dpi = input.dpi;
    let margin = scale(g.content_margin, dpi);
    let pad = scale(g.card_pad, dpi);
    let btn_w = scale(g.button_width, dpi);
    let ctrl_h = scale(g.ctrl_height, dpi);
    let btn_h = scale(g.button_height, dpi);
    let is_level1 = input.nav_level == 1 && input.selected_mode_index < 4;
    let right = ctrl_right(margin, card_w, dpi, is_level1);
    let (placements, _) = plan_controls(input, ime, load);
    let cards = if input.selected_mode_index == 5 { load } else { ime };

    let mut out = Vec::new();
    for pl in placements {
        let row = &card_rows(&cards[pl.card as usize])[pl.row as usize];
        match pl.kind {
            0 => {
                let Some(rc) = row_control_rect(row.row_type, right, pl.y, ctrl_h, btn_h, dpi) else {
                    continue;
                };
                let menu = 3000 + row.id;
                let r = rect4(&rc);
                match row.row_type {
                    TY_TOGGLE => out.push(spec(
                        row.id,
                        menu,
                        ROLE_PLAIN,
                        CLS_BUTTON,
                        0,
                        WS_CHILD | BS_AUTOCHECKBOX,
                        label(row.label),
                        [0; 4],
                        r,
                        -1,
                        false,
                        DARK_NONE,
                    )),
                    TY_COMBO => out.push(spec(
                        row.id,
                        menu,
                        ROLE_PLAIN,
                        CLS_COMBOBOX,
                        0,
                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                        None,
                        r,
                        r,
                        rc.vis_h,
                        true,
                        DARK_CFD,
                    )),
                    TY_EDIT => out.push(spec(
                        row.id,
                        menu,
                        ROLE_PLAIN,
                        CLS_EDIT,
                        WS_EX_CLIENTEDGE,
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                        None,
                        r,
                        r,
                        -1,
                        true,
                        DARK_CFD,
                    )),
                    TY_BUTTON | TY_CLICKABLE => {
                        let text = match row.action {
                            ACT_LOAD => w("載入"),
                            ACT_FONT => w("變更字型"),
                            ACT_EXPORT => w("匯出"),
                            ACT_IMPORT => w("匯入"),
                            ACT_RESET => w("還原預設值"),
                            _ => label(row.label),
                        };
                        out.push(spec(
                            row.id,
                            menu,
                            ROLE_PLAIN,
                            CLS_BUTTON,
                            0,
                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                            text,
                            r,
                            r,
                            -1,
                            true,
                            DARK_NONE,
                        ));
                    }
                    _ => {}
                }
            }
            1 => {
                let (re, save) = rich_editor_rects(margin, card_w, pad, pl.y, pl.h, right, btn_w, btn_h, dpi);
                out.push(spec(
                    row.id,
                    3000 + row.id,
                    ROLE_RICH_EDITOR,
                    CLS_RICHEDIT,
                    WS_EX_CLIENTEDGE,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                    None,
                    rect4(&re),
                    rect4(&re),
                    -1,
                    true,
                    DARK_EXPLORER,
                ));
                out.push(spec(
                    CTRL_CUSTOM_TABLE_SAVE,
                    3000 + CTRL_CUSTOM_TABLE_SAVE,
                    ROLE_SAVE_BUTTON,
                    CLS_BUTTON,
                    0,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
                    w("儲存"),
                    rect4(&save),
                    rect4(&save),
                    -1,
                    true,
                    DARK_NONE,
                ));
            }
            2 => {
                for si in 0..6 {
                    let rc = swatch_rect(si, margin, pl.y, right, dpi);
                    out.push(spec(
                        CTRL_COLOR_FR + si,
                        4000 + si,
                        ROLE_PLAIN,
                        CLS_STATIC,
                        0,
                        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
                        None,
                        rect4(&rc),
                        rect4(&rc),
                        -1,
                        false,
                        DARK_NONE,
                    ));
                }
            }
            _ => {}
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layout::EXPANDED_SLOTS;
    use dime_config::page_layout::{ime_cards, load_cards};

    #[test]
    fn load_page_buttons_say_load() {
        let input = PlanInput {
            dpi: 96,
            selected_mode_index: 5,
            nav_level: 0,
            current_card_index: 0,
            mode_mask: 0xF,
            color_mode: 0,
            grid_expanded: 0,
            preview_h: 0,
            expanded: [0; EXPANDED_SLOTS],
        };
        let specs = build_specs(&input, 600, ime_cards(), load_cards());
        assert!(!specs.is_empty());
        let load: Vec<u16> = "載入".encode_utf16().collect();
        assert!(specs.iter().all(|s| s.text.as_deref() == Some(load.as_slice())));
    }
}
