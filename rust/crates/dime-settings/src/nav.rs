//! Navigation, hover, scrolling, and sizing of the settings window
//! (phase 5b-8b).
//!
//! Port of `SettingsNav::Cpp::Handle` (`src/DIMESettings/SettingsNav.cpp`):
//! a reducer over [`NavState`] that turns one window event into state
//! changes plus an ordered list of [`NavEffect`]s for the window procedure
//! to carry out.

use dime_config::geometry::{defaults, mul_div};
use dime_config::page_layout::{
    ime_cards, load_cards, sidebar_items, LayoutCard, ACT_EXPAND, ACT_NAVIGATE, MODE_ALL, TY_COMBO, TY_TOGGLE,
};
use dime_config::settings_model::{clamp_scroll_pos, index_to_mode, mode_bitmask, sidebar_hit_test};
use dime_config::ImeMode;

use crate::layout::{card_list_click, card_list_hit_test, card_rows, row_cards_hit, scale, RowContext, EXPANDED_SLOTS};

/// `SettingsNav::NavState`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct NavState {
    pub selected_mode_index: i32,
    pub hover_sidebar_index: i32,
    pub nav_level: i32,
    pub current_card_index: i32,
    pub hover_row_index: i32,
    pub current_mode: i32,
    pub scroll_pos: i32,
    pub scroll_max: i32,
    pub total_content_height: i32,
    pub sidebar_collapsed: i32,
    pub sidebar_narrow_mode: i32,
    pub color_grid_expanded: i32,
    /// `bool[16]`; non-zero is expanded. Written back as 0 / 1.
    pub card_expanded: [u8; EXPANDED_SLOTS],
}

pub const EV_MAIN_SIZE: i32 = 1;
pub const EV_MAIN_WHEEL: i32 = 2;
pub const EV_MAIN_LBUTTON: i32 = 3;
pub const EV_MAIN_MOUSEMOVE: i32 = 4;
pub const EV_MAIN_MOUSELEAVE: i32 = 5;
pub const EV_CONTENT_VSCROLL: i32 = 6;
pub const EV_CONTENT_WHEEL: i32 = 7;
pub const EV_CONTENT_MOUSEMOVE: i32 = 8;
pub const EV_CONTENT_MOUSELEAVE: i32 = 9;
pub const EV_CONTENT_LBUTTON: i32 = 10;
pub const EV_BACK: i32 = 11;

/// `SettingsNav::NavEvent`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct NavEvent {
    pub kind: i32,
    pub x: i32,
    pub y: i32,
    pub delta: i32,
    pub thumb: i32,
    pub dpi: u32,
    pub preview_h: i32,
    pub color_mode: i32,
    pub custom_table_dirty: i32,
}

pub const FX_INVALIDATE_MAIN: i32 = 1;
pub const FX_INVALIDATE_CONTENT: i32 = 2;
pub const FX_TRACK_LEAVE_MAIN: i32 = 3;
pub const FX_TRACK_LEAVE_CONTENT: i32 = 4;
pub const FX_SCROLL_CHANGED: i32 = 5;
pub const FX_SHOW_ALL_CONTROLS: i32 = 6;
pub const FX_HIDE_ALL_CONTROLS: i32 = 7;
pub const FX_SWITCH_MODE: i32 = 8;
pub const FX_REBUILD: i32 = 9;
pub const FX_TOGGLE_CONTROL: i32 = 10;
pub const FX_ASK_SAVE_THEN_BACK: i32 = 11;
pub const FX_MOVE_CONTENT: i32 = 12;
pub const FX_SET_FOCUS_CONTENT: i32 = 13;
pub const FX_UPDATE_CONTENT: i32 = 14;
pub const FX_UPDATE_SCROLL_INFO: i32 = 15;

/// `SettingsNav::NavEffect`. `rc` is left, top, right, bottom (for
/// `FX_MOVE_CONTENT`: x, y, w, h).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct NavEffect {
    pub kind: i32,
    pub arg: i32,
    pub erase: i32,
    pub has_rect: i32,
    pub rc: [i32; 4],
}

const SB_LINEUP: i32 = 0;
const SB_LINEDOWN: i32 = 1;
const SB_PAGEUP: i32 = 2;
const SB_PAGEDOWN: i32 = 3;
const SB_THUMBTRACK: i32 = 5;
const WHEEL_DELTA: i32 = 120;

struct Out(Vec<NavEffect>);

impl Out {
    fn add(&mut self, kind: i32) {
        self.add_arg(kind, 0);
    }
    fn add_arg(&mut self, kind: i32, arg: i32) {
        self.0.push(NavEffect {
            kind,
            arg,
            ..Default::default()
        });
    }
    fn invalidate_main(&mut self, erase: bool) {
        self.0.push(NavEffect {
            kind: FX_INVALIDATE_MAIN,
            erase: i32::from(erase),
            ..Default::default()
        });
    }
    fn invalidate_main_rect(&mut self, erase: bool, rc: [i32; 4]) {
        self.0.push(NavEffect {
            kind: FX_INVALIDATE_MAIN,
            erase: i32::from(erase),
            has_rect: 1,
            rc,
            arg: 0,
        });
    }
    fn invalidate_content(&mut self, erase: bool) {
        self.0.push(NavEffect {
            kind: FX_INVALIDATE_CONTENT,
            erase: i32::from(erase),
            ..Default::default()
        });
    }
}

fn s(v: i32, dpi: u32) -> i32 {
    scale(v, dpi)
}

fn expanded_bools(st: &NavState) -> [bool; EXPANDED_SLOTS] {
    st.card_expanded.map(|b| b != 0)
}

fn scroll_to(st: &mut NavState, pos: i32, o: &mut Out) {
    let old = st.scroll_pos;
    st.scroll_pos = clamp_scroll_pos(pos, st.scroll_max);
    if st.scroll_pos != old {
        o.add(FX_SCROLL_CHANGED);
    }
}

fn wheel(st: &mut NavState, ev: &NavEvent, o: &mut Out) {
    let step = mul_div(ev.delta, s(defaults().row_height * 3, ev.dpi), WHEEL_DELTA);
    scroll_to(st, st.scroll_pos - step, o);
}

fn navigate_back(st: &mut NavState, o: &mut Out) {
    st.nav_level = 0;
    st.scroll_pos = 0;
    o.add(FX_REBUILD);
    o.invalidate_main(true);
    o.invalidate_content(true);
}

fn navigate_to_category(st: &mut NavState, card: i32, o: &mut Out) {
    st.nav_level = 1;
    st.current_card_index = card;
    st.scroll_pos = 0;
    o.add(FX_REBUILD);
    o.invalidate_main(true);
    o.invalidate_content(true);
}

fn show_load_table_page(st: &mut NavState, o: &mut Out) {
    st.selected_mode_index = 5;
    st.nav_level = 0;
    st.scroll_pos = 0;
    for (li, card) in load_cards().iter().enumerate() {
        if 4 + li >= EXPANDED_SLOTS {
            break;
        }
        st.card_expanded[4 + li] = u8::from(card.default_expanded);
    }
    o.add(FX_REBUILD);
    o.invalidate_main(true);
    o.invalidate_content(true);
}

fn select_sidebar_index(st: &mut NavState, idx: i32, o: &mut Out) {
    if idx < 4 {
        let mode = index_to_mode(idx) as i32;
        if mode != st.current_mode || st.nav_level == 1 || st.selected_mode_index != idx {
            o.add_arg(FX_SWITCH_MODE, mode);
        }
    } else if idx == 5 {
        show_load_table_page(st, o);
    }
}

fn toggle_sidebar(st: &mut NavState, o: &mut Out) {
    st.sidebar_collapsed = i32::from(st.sidebar_collapsed == 0);
    o.add(if st.sidebar_collapsed != 0 {
        FX_SHOW_ALL_CONTROLS
    } else {
        FX_HIDE_ALL_CONTROLS
    });
}

fn row_ctx(st: &NavState, ev: &NavEvent) -> RowContext {
    RowContext {
        dpi: ev.dpi,
        scroll_pos: st.scroll_pos,
        mode_mask: mode_bitmask(ImeMode::from_i32(st.current_mode)),
        color_mode: ev.color_mode,
        grid_expanded: i32::from(st.color_grid_expanded != 0),
        preview_h: ev.preview_h,
    }
}

/// Sidebar item under main-window `y`, if it is a selectable item.
fn sidebar_item_at(y: i32, dpi: u32) -> Option<i32> {
    let g = defaults();
    let items = sidebar_items();
    let idx = sidebar_hit_test(
        y - s(g.sidebar_start_y, dpi),
        s(g.sidebar_item_h, dpi),
        items.len() as i32,
    );
    let item = usize::try_from(idx).ok().and_then(|i| items.get(i))?;
    (item.icon_resource_id != -1).then_some(idx)
}

fn sidebar_hover_at(y: i32, dpi: u32) -> i32 {
    let g = defaults();
    sidebar_hit_test(
        y - s(g.sidebar_start_y, dpi),
        s(g.sidebar_item_h, dpi),
        sidebar_items().len() as i32,
    )
}

fn main_lbutton(st: &mut NavState, ev: &NavEvent, o: &mut Out) {
    let g = defaults();
    let dpi = ev.dpi;
    let (x, y) = (ev.x, ev.y);
    let narrow = st.sidebar_narrow_mode != 0;
    let collapsed = st.sidebar_collapsed != 0;
    let sidebar_w = if !narrow && !collapsed {
        s(g.sidebar_width, dpi)
    } else {
        0
    };

    if narrow && st.nav_level == 1 {
        let hb = s(g.hamburger_size, dpi);
        if x >= sidebar_w && x < sidebar_w + hb && y < hb {
            toggle_sidebar(st, o);
            o.invalidate_content(true);
            o.invalidate_main(true);
            return;
        }
    }

    if x >= sidebar_w && st.nav_level == 1 && st.selected_mode_index < 4 {
        if y < s(g.breadcrumb_h, dpi) {
            let margin = s(g.content_margin, dpi);
            let rel_x = x - sidebar_w;
            if rel_x >= margin && rel_x < margin + s(g.bc_mode_name_w, dpi) {
                if ev.custom_table_dirty != 0 {
                    o.add(FX_ASK_SAVE_THEN_BACK);
                } else {
                    navigate_back(st, o);
                }
            }
        }
        return;
    }

    if collapsed || x >= sidebar_w {
        return;
    }
    let Some(idx) = sidebar_item_at(y, dpi) else {
        return;
    };
    select_sidebar_index(st, idx, o);
    if narrow {
        st.sidebar_collapsed = 1;
        o.add(FX_SHOW_ALL_CONTROLS);
        o.invalidate_content(true);
        o.invalidate_main(true);
    }
}

fn main_mouse_move(st: &mut NavState, ev: &NavEvent, o: &mut Out) {
    let sidebar_w = s(defaults().sidebar_width, ev.dpi);
    let hover = if ev.x < sidebar_w && st.sidebar_collapsed == 0 {
        sidebar_hover_at(ev.y, ev.dpi)
    } else {
        -1
    };
    if hover != st.hover_sidebar_index {
        st.hover_sidebar_index = hover;
        o.invalidate_main_rect(false, [0, 0, sidebar_w, 32767]);
        o.add(FX_TRACK_LEAVE_MAIN);
    }
}

fn bc_offset(st: &NavState, dpi: u32) -> i32 {
    if st.nav_level == 1 && st.selected_mode_index < 4 {
        s(defaults().breadcrumb_h, dpi)
    } else {
        0
    }
}

fn content_mouse_move(st: &mut NavState, ev: &NavEvent, o: &mut Out) {
    let dpi = ev.dpi;
    let y = ev.y;
    if st.sidebar_narrow_mode != 0 && st.sidebar_collapsed == 0 {
        if ev.x < s(defaults().sidebar_width, dpi) {
            let hover = sidebar_hover_at(y + bc_offset(st, dpi), dpi);
            if hover != st.hover_sidebar_index {
                st.hover_sidebar_index = hover;
                o.invalidate_content(false);
                o.add(FX_TRACK_LEAVE_CONTENT);
            }
            return;
        } else if st.hover_sidebar_index != -1 {
            st.hover_sidebar_index = -1;
            o.invalidate_content(false);
        }
    }

    let expanded = expanded_bools(st);
    let mask = mode_bitmask(ImeMode::from_i32(st.current_mode));
    let hover = if st.nav_level == 0 && st.selected_mode_index < 4 {
        card_list_hit_test(y, ime_cards(), mask, &expanded, 0, dpi, st.scroll_pos)
    } else if st.selected_mode_index == 5 {
        card_list_hit_test(y, load_cards(), MODE_ALL, &expanded, 4, dpi, st.scroll_pos)
    } else if st.nav_level == 1 && st.selected_mode_index < 4 {
        match usize::try_from(st.current_card_index)
            .ok()
            .and_then(|i| ime_cards().get(i))
        {
            Some(card) => row_cards_hit(y, card, &row_ctx(st, ev)).span_start,
            None => -1,
        }
    } else {
        -1
    };
    if hover != st.hover_row_index {
        st.hover_row_index = hover;
        o.invalidate_content(false);
        o.add(FX_TRACK_LEAVE_CONTENT);
    }
}

fn card_list_click_action(st: &mut NavState, ev: &NavEvent, cards: &[LayoutCard], mask: i32, base: i32, o: &mut Out) {
    let expanded = expanded_bools(st);
    let c = card_list_click(ev.y, cards, mask, &expanded, base, ev.dpi, st.scroll_pos);
    match c.kind {
        1 => {
            let card = &cards[c.card as usize];
            if card.action == ACT_NAVIGATE {
                navigate_to_category(st, c.card, o);
            } else if card.action == ACT_EXPAND {
                let i = (base + c.card) as usize;
                if let Some(e) = st.card_expanded.get_mut(i) {
                    *e = u8::from(*e == 0);
                }
                st.scroll_pos = 0;
                o.add(FX_REBUILD);
                o.add(FX_UPDATE_SCROLL_INFO);
                o.invalidate_content(false);
                o.add(FX_UPDATE_CONTENT);
            }
        }
        2 => {
            let row = &card_rows(&cards[c.card as usize])[c.row as usize];
            if row.row_type == TY_TOGGLE {
                o.add_arg(FX_TOGGLE_CONTROL, row.id);
            }
        }
        _ => {}
    }
}

fn row_card_click(st: &mut NavState, ev: &NavEvent, card: &LayoutCard, o: &mut Out) {
    let hit = row_cards_hit(ev.y, card, &row_ctx(st, ev));
    if hit.span_start < 0 || hit.row < 0 {
        return;
    }
    let row = &card_rows(card)[hit.row as usize];
    if row.action == ACT_EXPAND && row.row_type == TY_COMBO && ev.color_mode == 3 {
        st.color_grid_expanded = i32::from(st.color_grid_expanded == 0);
        o.add(FX_REBUILD);
        o.invalidate_content(false);
        o.add(FX_UPDATE_CONTENT);
    } else if row.row_type == TY_TOGGLE {
        o.add_arg(FX_TOGGLE_CONTROL, row.id);
    }
}

fn content_lbutton(st: &mut NavState, ev: &NavEvent, o: &mut Out) {
    let g = defaults();
    o.add(FX_SET_FOCUS_CONTENT);
    let dpi = ev.dpi;
    let (x, y) = (ev.x, ev.y);

    if st.sidebar_narrow_mode != 0 {
        let hb = s(g.hamburger_size, dpi);
        if (0..hb).contains(&x) && (0..hb).contains(&y) {
            toggle_sidebar(st, o);
            o.invalidate_content(true);
            o.invalidate_main(true);
            return;
        }
    }

    if st.sidebar_narrow_mode != 0 && st.sidebar_collapsed == 0 {
        if x < s(g.sidebar_width, dpi) {
            if let Some(idx) = sidebar_item_at(y + bc_offset(st, dpi), dpi) {
                st.sidebar_collapsed = 1;
                select_sidebar_index(st, idx, o);
                o.add(FX_SHOW_ALL_CONTROLS);
            }
            return;
        }
        st.sidebar_collapsed = 1;
        o.add(FX_SHOW_ALL_CONTROLS);
        o.invalidate_content(true);
        o.invalidate_main(true);
    }

    if st.nav_level == 0 && st.selected_mode_index < 4 {
        let mask = mode_bitmask(ImeMode::from_i32(st.current_mode));
        card_list_click_action(st, ev, ime_cards(), mask, 0, o);
    } else if st.nav_level == 1 {
        if let Some(card) = usize::try_from(st.current_card_index)
            .ok()
            .and_then(|i| ime_cards().get(i))
        {
            row_card_click(st, ev, card, o);
        }
    } else if st.selected_mode_index == 5 {
        card_list_click_action(st, ev, load_cards(), MODE_ALL, 4, o);
    }
}

/// `SettingsNav::Cpp::Handle`: apply one event, return the effects in order.
pub fn handle(st: &mut NavState, ev: &NavEvent) -> Vec<NavEffect> {
    // C++ stores bools; normalise like a bool round trip.
    for e in st.card_expanded.iter_mut() {
        *e = u8::from(*e != 0);
    }
    let g = defaults();
    let dpi = ev.dpi;
    let mut o = Out(Vec::new());
    match ev.kind {
        EV_MAIN_SIZE => {
            let (w, h) = (ev.x, ev.y);
            let narrow = w < s(g.sidebar_collapse_break, dpi);
            st.sidebar_narrow_mode = i32::from(narrow);
            st.sidebar_collapsed = i32::from(narrow);
            let sidebar_w = if narrow { 0 } else { s(g.sidebar_width, dpi) };
            o.0.push(NavEffect {
                kind: FX_MOVE_CONTENT,
                has_rect: 1,
                rc: [sidebar_w, 0, w - sidebar_w, h],
                ..Default::default()
            });
            o.add(FX_REBUILD);
            o.invalidate_main(true);
        }
        EV_MAIN_WHEEL | EV_CONTENT_WHEEL => wheel(st, ev, &mut o),
        EV_MAIN_LBUTTON => main_lbutton(st, ev, &mut o),
        EV_MAIN_MOUSEMOVE => main_mouse_move(st, ev, &mut o),
        EV_MAIN_MOUSELEAVE => {
            if st.hover_sidebar_index != -1 {
                st.hover_sidebar_index = -1;
                o.invalidate_main_rect(false, [0, 0, s(g.sidebar_width, dpi), 32767]);
            }
        }
        EV_CONTENT_VSCROLL => {
            let pos = match ev.delta {
                SB_LINEUP => st.scroll_pos - s(g.row_height, dpi),
                SB_LINEDOWN => st.scroll_pos + s(g.row_height, dpi),
                SB_PAGEUP => st.scroll_pos - st.total_content_height / 4,
                SB_PAGEDOWN => st.scroll_pos + st.total_content_height / 4,
                SB_THUMBTRACK => ev.thumb,
                _ => st.scroll_pos,
            };
            scroll_to(st, pos, &mut o);
        }
        EV_CONTENT_MOUSEMOVE => content_mouse_move(st, ev, &mut o),
        EV_CONTENT_MOUSELEAVE => {
            if st.hover_row_index != -1 {
                st.hover_row_index = -1;
                o.invalidate_content(false);
            }
        }
        EV_CONTENT_LBUTTON => content_lbutton(st, ev, &mut o),
        EV_BACK => navigate_back(st, &mut o),
        _ => {}
    }
    o.0
}

/// `WM_GETMINMAXINFO`: minimum track size at the screen dpi.
pub fn min_track_size(screen_dpi: u32) -> (i32, i32) {
    let g = defaults();
    (s(g.window_min_w, screen_dpi), s(g.window_min_h, screen_dpi))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::size_of;

    fn ev(kind: i32, x: i32, y: i32) -> NavEvent {
        NavEvent {
            kind,
            x,
            y,
            dpi: 96,
            ..Default::default()
        }
    }

    #[test]
    fn struct_sizes() {
        assert_eq!(size_of::<NavState>(), 64);
        assert_eq!(size_of::<NavEvent>(), 36);
        assert_eq!(size_of::<NavEffect>(), 32);
    }

    #[test]
    fn narrow_resize_collapses() {
        let mut st = NavState::default();
        let fx = handle(&mut st, &ev(EV_MAIN_SIZE, 700, 500));
        assert_eq!((st.sidebar_narrow_mode, st.sidebar_collapsed), (1, 1));
        assert_eq!(fx[0].rc, [0, 0, 700, 500]);
        let fx = handle(&mut st, &ev(EV_MAIN_SIZE, 900, 500));
        assert_eq!((st.sidebar_narrow_mode, st.sidebar_collapsed), (0, 0));
        assert_eq!(fx[0].rc, [280, 0, 620, 500]);
        assert_eq!(
            fx.iter().map(|f| f.kind).collect::<Vec<_>>(),
            vec![FX_MOVE_CONTENT, FX_REBUILD, FX_INVALIDATE_MAIN]
        );
    }

    #[test]
    fn sidebar_click_switches_mode() {
        let mut st = NavState {
            current_mode: 0,
            ..Default::default()
        };
        // Item 1 (行列) at y = 56 + 40 + 5.
        let fx = handle(&mut st, &ev(EV_MAIN_LBUTTON, 50, 101));
        assert_eq!(
            fx,
            vec![NavEffect {
                kind: FX_SWITCH_MODE,
                arg: 1,
                ..Default::default()
            }]
        );
        // Separator (index 4) does nothing.
        assert!(handle(&mut st, &ev(EV_MAIN_LBUTTON, 50, 56 + 4 * 40 + 5)).is_empty());
        // 載入碼表 (index 5).
        let fx = handle(&mut st, &ev(EV_MAIN_LBUTTON, 50, 56 + 5 * 40 + 5));
        assert_eq!(st.selected_mode_index, 5);
        assert_eq!(fx[0].kind, FX_REBUILD);
    }

    #[test]
    fn scrolling_clamps() {
        let mut st = NavState {
            scroll_max: 100,
            total_content_height: 1000,
            ..Default::default()
        };
        let fx = handle(
            &mut st,
            &NavEvent {
                kind: EV_CONTENT_WHEEL,
                delta: -120,
                dpi: 96,
                ..Default::default()
            },
        );
        assert_eq!(st.scroll_pos, 100);
        assert_eq!(fx.len(), 1);
        assert!(handle(
            &mut st,
            &NavEvent {
                kind: EV_CONTENT_VSCROLL,
                delta: SB_PAGEDOWN,
                dpi: 96,
                ..Default::default()
            }
        )
        .is_empty());
        handle(
            &mut st,
            &NavEvent {
                kind: EV_CONTENT_VSCROLL,
                delta: SB_THUMBTRACK,
                thumb: 40,
                dpi: 96,
                ..Default::default()
            },
        );
        assert_eq!(st.scroll_pos, 40);
        assert_eq!(min_track_size(144), (780, 675));
    }
}
