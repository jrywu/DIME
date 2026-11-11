//! Card and row geometry of the settings window (phase 5b-6).
//!
//! Line-by-line port of `SettingsWindowLayout::Cpp` (`src/DIMESettings/
//! SettingsWindowLayout.cpp`), which was lifted out of `SettingsWindow.cpp`.
//! `dpi` is the `UINT` the window code passes to `ScaleForDpi(int, UINT)`,
//! which always calls `MulDiv` (no `dpi <= 0` shortcut).

use dime_config::geometry::{defaults, mul_div};
use dime_config::page_layout::{
    row_visible, LayoutCard, LayoutRow, ACT_EXPAND, ACT_NONE, CTRL_COLOR_MODE, CTRL_NONE, TY_BUTTON, TY_CLICKABLE,
    TY_COLOR_GRID, TY_COMBO, TY_EDIT, TY_PREVIEW, TY_RICH_EDITOR, TY_TOGGLE,
};

/// Number of `cardExpanded` slots in `SettingsWindow::WindowData`.
pub const EXPANDED_SLOTS: usize = 16;

/// `ScaleForDpi(int, UINT)` in `BaseStructure.h`.
pub fn scale(value: i32, dpi: u32) -> i32 {
    mul_div(value, dpi as i32, 96)
}

/// The rows of a card. A null pointer or a non-positive count is empty.
pub fn card_rows(card: &LayoutCard) -> &[LayoutRow] {
    if card.rows.is_null() || card.row_count <= 0 {
        return &[];
    }
    // SAFETY: the tables keep `row_count` rows alive behind `rows`.
    unsafe { std::slice::from_raw_parts(card.rows, card.row_count as usize) }
}

fn expanded_at(expanded: &[bool], index: i32) -> bool {
    usize::try_from(index)
        .ok()
        .and_then(|i| expanded.get(i))
        .copied()
        .unwrap_or(false)
}

/// `CalcCardW`. `client_right` is the main window's client width and
/// `vscroll_w` is `GetSystemMetrics(SM_CXVSCROLL)`.
pub fn card_width(client_right: i32, dpi: u32, sidebar_takes_space: bool, vscroll_w: i32) -> i32 {
    let g = defaults();
    let sidebar = if sidebar_takes_space {
        scale(g.sidebar_width, dpi)
    } else {
        0
    };
    let margin = scale(g.content_margin, dpi);
    client_right - sidebar - 2 * margin - vscroll_w
}

/// `CalcCtrlRight`: right edge of the row controls, left of the chevron column.
pub fn ctrl_right(margin: i32, card_w: i32, dpi: u32, has_chevron: bool) -> i32 {
    let g = defaults();
    let pad = scale(g.card_pad, dpi);
    let ctrl_rm = scale(g.control_right_margin, dpi);
    let chev = if has_chevron { scale(g.chevron_w + 4, dpi) } else { 0 };
    margin + card_w - pad - ctrl_rm - chev
}

/// `CalcRowHeight`. `preview_h` is the measured `CalcCandidatePreviewH`,
/// used only by the candidate preview row.
pub fn row_height(row: &LayoutRow, dpi: u32, preview_h: i32) -> i32 {
    let g = defaults();
    match row.row_type {
        TY_PREVIEW => preview_h,
        TY_COLOR_GRID => 2 * (scale(g.swatch_size, dpi) + scale(g.swatch_cell_gap, dpi)),
        TY_RICH_EDITOR => scale(g.editor_card_h, dpi),
        _ if !row.description.is_null() => scale(g.row_height, dpi),
        _ => scale(g.row_height_single, dpi),
    }
}

/// Sub-page state that decides which rows show and how tall they are.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct RowContext {
    pub dpi: u32,
    pub scroll_pos: i32,
    pub mode_mask: i32,
    pub color_mode: i32,
    pub grid_expanded: i32,
    pub preview_h: i32,
}

/// The `isShown` lambda: visible in the mode, and the color swatches only in
/// custom color mode (3) with the grid expanded.
pub fn row_shown(row: &LayoutRow, mode_mask: i32, color_mode: i32, grid_expanded: bool) -> bool {
    if !row_visible(row.visible_modes, mode_mask) {
        return false;
    }
    !(row.expand_parent != CTRL_NONE && row.expand_parent == CTRL_COLOR_MODE && (color_mode != 3 || !grid_expanded))
}

fn shown(row: &LayoutRow, ctx: &RowContext) -> bool {
    row_shown(row, ctx.mode_mask, ctx.color_mode, ctx.grid_expanded != 0)
}

/// A card visible in the mode: no rows, or any visible row.
pub fn card_visible(card: &LayoutCard, mode_mask: i32) -> bool {
    card.row_count == 0 || card_rows(card).iter().any(|r| row_visible(r.visible_modes, mode_mask))
}

fn expanded_children(card: &LayoutCard, mode_mask: i32, is_expanded: bool) -> i32 {
    if card.action == ACT_EXPAND && is_expanded && !card.rows.is_null() {
        card_rows(card)
            .iter()
            .filter(|r| row_visible(r.visible_modes, mode_mask))
            .count() as i32
    } else {
        0
    }
}

/// `SettingsWindow::HitTestCardList`: the card under `y` on a card-list
/// page, or -1. A card's box includes its padding.
pub fn card_list_hit_test(
    y: i32,
    cards: &[LayoutCard],
    mode_mask: i32,
    expanded: &[bool],
    expand_base: i32,
    dpi: u32,
    scroll_pos: i32,
) -> i32 {
    let g = defaults();
    let pad = scale(g.card_pad, dpi);
    let cat_row_h = scale(g.category_row_h, dpi);
    let card_gap = scale(g.card_gap, dpi);
    let c_row_h = scale(g.child_row_h, dpi);
    let mut cur = scale(g.top_margin, dpi) + pad - scroll_pos;
    for (i, card) in cards.iter().enumerate() {
        if !card_visible(card, mode_mask) {
            continue;
        }
        let num = expanded_children(card, mode_mask, expanded_at(expanded, expand_base + i as i32));
        let card_h = 2 * pad + cat_row_h + if num > 0 { num * (c_row_h + 1) } else { 0 };
        if y >= cur - pad && y < cur - pad + card_h {
            return i as i32;
        }
        cur += card_h - 2 * pad + 2 * pad + card_gap;
    }
    -1
}

/// What a click on a card-list page lands on.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CardListClick {
    /// 0 = nothing, 1 = a card's category row, 2 = an expanded child row.
    pub kind: i32,
    pub card: i32,
    /// Index into the card's rows (kind 2), otherwise -1.
    pub row: i32,
}

/// The target finder of `SettingsWindow::HandleCardListClick`. The caller
/// navigates, expands, or toggles. Padding and separators hit nothing.
pub fn card_list_click(
    y: i32,
    cards: &[LayoutCard],
    mode_mask: i32,
    expanded: &[bool],
    expand_base: i32,
    dpi: u32,
    scroll_pos: i32,
) -> CardListClick {
    let g = defaults();
    let pad = scale(g.card_pad, dpi);
    let cat_row_h = scale(g.category_row_h, dpi);
    let card_gap = scale(g.card_gap, dpi);
    let c_row_h = scale(g.child_row_h, dpi);
    let mut cur = scale(g.top_margin, dpi) + pad - scroll_pos;
    for (i, card) in cards.iter().enumerate() {
        if !card_visible(card, mode_mask) {
            continue;
        }
        if y >= cur && y < cur + cat_row_h {
            return CardListClick {
                kind: 1,
                card: i as i32,
                row: -1,
            };
        }
        let is_expanded = expanded_at(expanded, expand_base + i as i32);
        if card.action == ACT_EXPAND && is_expanded && !card.rows.is_null() {
            let mut child_y = cur + cat_row_h + pad;
            let mut num = 0;
            for (r, row) in card_rows(card).iter().enumerate() {
                if !row_visible(row.visible_modes, mode_mask) {
                    continue;
                }
                child_y += 1;
                if y >= child_y && y < child_y + c_row_h {
                    return CardListClick {
                        kind: 2,
                        card: i as i32,
                        row: r as i32,
                    };
                }
                child_y += c_row_h;
                num += 1;
            }
            cur += 2 * pad + cat_row_h + num * (c_row_h + 1) + card_gap;
        } else {
            cur += cat_row_h + 2 * pad + card_gap;
        }
    }
    CardListClick {
        kind: 0,
        card: -1,
        row: -1,
    }
}

/// One card on a sub-page: a top-level row and the child rows after it.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RowSpan {
    pub start_row: i32,
    pub end_row: i32,
    /// Shown rows plus `2 * cardPad`.
    pub height: i32,
}

/// The card grouping of `PaintSettingsDetail`, `HitTestRowCards`, and
/// `HandleRowCardClick`: a shown top-level row (or `cardBreakBefore`) opens
/// a card that runs until the next top-level row or break.
pub fn row_spans(card: &LayoutCard, ctx: &RowContext) -> Vec<RowSpan> {
    let pad = scale(defaults().card_pad, ctx.dpi);
    let rows = card_rows(card);
    let mut spans = Vec::new();
    let mut r = 0;
    while r < rows.len() {
        let row = &rows[r];
        if shown(row, ctx) && (row.expand_parent == CTRL_NONE || row.card_break_before) {
            let mut h = row_height(row, ctx.dpi, ctx.preview_h);
            let mut end = r + 1;
            while end < rows.len() && rows[end].expand_parent != CTRL_NONE && !rows[end].card_break_before {
                if shown(&rows[end], ctx) {
                    h += row_height(&rows[end], ctx.dpi, ctx.preview_h);
                }
                end += 1;
            }
            spans.push(RowSpan {
                start_row: r as i32,
                end_row: end as i32,
                height: h + 2 * pad,
            });
            r = end;
        } else {
            r += 1;
        }
    }
    spans
}

/// What a point on a sub-page lands on.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RowCardHit {
    /// First row of the card under the point (`HitTestRowCards`), or -1.
    pub span_start: i32,
    /// Shown row under the point inside that card, or -1 for padding.
    pub row: i32,
}

/// `HitTestRowCards` and the target finder of `HandleRowCardClick`.
pub fn row_cards_hit(y: i32, card: &LayoutCard, ctx: &RowContext) -> RowCardHit {
    let g = defaults();
    let pad = scale(g.card_pad, ctx.dpi);
    let card_gap = scale(g.card_gap, ctx.dpi);
    let rows = card_rows(card);
    let mut top = scale(g.top_pad, ctx.dpi) - ctx.scroll_pos;
    for span in row_spans(card, ctx) {
        if y >= top && y < top + span.height {
            let mut cur = top + pad;
            for ri in span.start_row..span.end_row {
                let row = &rows[ri as usize];
                if !shown(row, ctx) {
                    continue;
                }
                let h = row_height(row, ctx.dpi, ctx.preview_h);
                if y >= cur && y < cur + h {
                    return RowCardHit {
                        span_start: span.start_row,
                        row: ri,
                    };
                }
                cur += h;
            }
            return RowCardHit {
                span_start: span.start_row,
                row: -1,
            };
        }
        top += span.height + card_gap;
    }
    RowCardHit {
        span_start: -1,
        row: -1,
    }
}

/// Which page `RebuildContentArea` lays out, and its state.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PlanInput {
    pub dpi: u32,
    /// Sidebar index: 0-3 an IME mode, 5 the 載入碼表 page.
    pub selected_mode_index: i32,
    pub nav_level: i32,
    pub current_card_index: i32,
    pub mode_mask: i32,
    pub color_mode: i32,
    pub grid_expanded: i32,
    pub preview_h: i32,
    /// `cardExpanded` as bytes; non-zero is expanded.
    pub expanded: [u8; EXPANDED_SLOTS],
}

/// A control to create (`RebuildContentArea`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Placement {
    /// 0 = `CreateRowControl` at `y` (the control top `cy`),
    /// 1 = `CreateRichEditorControl` (`y` = row top, `h` = row height),
    /// 2 = `CreateColorGridControls` (`y` = row top).
    pub kind: i32,
    pub card: i32,
    pub row: i32,
    pub y: i32,
    pub h: i32,
}

/// Result flags of [`plan_controls`].
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PlanResult {
    /// 1 when `totalContentHeight` is set to `total_height`.
    pub set_total: i32,
    pub total_height: i32,
    /// 1 when `PopulateControls` runs afterwards.
    pub populate: i32,
}

/// Control placement of `RebuildContentArea` for the IME pages (`ime`) and
/// the 載入碼表 page (`load`). Positions are unscrolled content coordinates.
pub fn plan_controls(input: &PlanInput, ime: &[LayoutCard], load: &[LayoutCard]) -> (Vec<Placement>, PlanResult) {
    let g = defaults();
    let dpi = input.dpi;
    let pad = scale(g.card_pad, dpi);
    let cat_row_h = scale(g.category_row_h, dpi);
    let ctrl_h = scale(g.ctrl_height, dpi);
    let btn_h = scale(g.button_height, dpi);
    let card_gap = scale(g.card_gap, dpi);
    let c_row_h = scale(g.child_row_h, dpi);
    let expanded = input.expanded.map(|b| b != 0);
    let mut out = Vec::new();

    if input.selected_mode_index == 5 {
        let mut cur = scale(g.top_margin, dpi) + pad;
        for (i, card) in load.iter().enumerate() {
            let rows = card_rows(card);
            if card.action == ACT_NONE && !card.rows.is_null() && card.row_count > 0 {
                out.push(Placement {
                    kind: 0,
                    card: i as i32,
                    row: 0,
                    y: cur + (cat_row_h - btn_h) / 2,
                    h: 0,
                });
                cur += cat_row_h + 2 * pad + card_gap;
            } else if card.action == ACT_EXPAND && expanded_at(&expanded, 4 + i as i32) && !card.rows.is_null() {
                let mut child_y = cur + cat_row_h + pad;
                for r in 0..rows.len() {
                    child_y += 1;
                    out.push(Placement {
                        kind: 0,
                        card: i as i32,
                        row: r as i32,
                        y: child_y + (c_row_h - btn_h) / 2,
                        h: 0,
                    });
                    child_y += c_row_h;
                }
                cur += 2 * pad + cat_row_h + card.row_count * (c_row_h + 1) + card_gap;
            } else {
                cur += cat_row_h + 2 * pad + card_gap;
            }
        }
        return (
            out,
            PlanResult {
                set_total: 1,
                total_height: cur,
                populate: 0,
            },
        );
    }

    if input.nav_level == 0 && input.selected_mode_index < 4 {
        let mut cur = scale(g.top_margin, dpi) + pad;
        for (i, card) in ime.iter().enumerate() {
            if !card_visible(card, input.mode_mask) {
                continue;
            }
            if card.action == ACT_EXPAND && expanded_at(&expanded, i as i32) && !card.rows.is_null() {
                let mut child_y = cur + cat_row_h + pad;
                let mut num = 0;
                for (r, row) in card_rows(card).iter().enumerate() {
                    if !row_visible(row.visible_modes, input.mode_mask) {
                        continue;
                    }
                    child_y += 1;
                    out.push(Placement {
                        kind: 0,
                        card: i as i32,
                        row: r as i32,
                        y: child_y + (c_row_h - ctrl_h) / 2,
                        h: 0,
                    });
                    child_y += c_row_h;
                    num += 1;
                }
                cur += 2 * pad + cat_row_h + num * (c_row_h + 1) + card_gap;
            } else {
                cur += cat_row_h + 2 * pad + card_gap;
            }
        }
        return (
            out,
            PlanResult {
                set_total: 1,
                total_height: cur,
                populate: 1,
            },
        );
    }

    if input.nav_level == 1 && input.selected_mode_index < 4 {
        let index = input.current_card_index;
        if index < 0 || index as usize >= ime.len() {
            return (
                out,
                PlanResult {
                    set_total: 1,
                    total_height: 0,
                    populate: 0,
                },
            );
        }
        let card = &ime[index as usize];
        let ctx = RowContext {
            dpi,
            scroll_pos: 0,
            mode_mask: input.mode_mask,
            color_mode: input.color_mode,
            grid_expanded: input.grid_expanded,
            preview_h: input.preview_h,
        };
        let mut cur = scale(g.top_pad, dpi) + pad;
        let mut first_top_level = true;
        for (r, row) in card_rows(card).iter().enumerate() {
            if !shown(row, &ctx) {
                continue;
            }
            if row.expand_parent == CTRL_NONE || row.card_break_before {
                if !first_top_level {
                    cur += pad + card_gap + pad;
                }
                first_top_level = false;
            }
            let h = row_height(row, dpi, input.preview_h);
            let (kind, y) = match row.row_type {
                TY_RICH_EDITOR => (1, cur),
                TY_TOGGLE | TY_COMBO | TY_EDIT | TY_CLICKABLE | TY_BUTTON => (0, cur + (h - ctrl_h) / 2),
                TY_COLOR_GRID => (2, cur),
                _ => (-1, 0),
            };
            if kind >= 0 {
                out.push(Placement {
                    kind,
                    card: index,
                    row: r as i32,
                    y,
                    h,
                });
            }
            cur += h;
        }
        let total = cur + pad + scale(g.card_gap, dpi);
        return (
            out,
            PlanResult {
                set_total: 1,
                total_height: total,
                populate: 1,
            },
        );
    }

    (out, PlanResult::default())
}

/// A rectangle as `x, y, w, h` plus the painted height of a combo box.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CtrlRect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
    /// `ControlHandle::visH`: the closed height for a combo box, else `h`.
    pub vis_h: i32,
}

/// The child-window rectangle `CreateRowControl` uses. Toggles are hidden
/// checkboxes at 0,0,0,0. Returns `None` for row types without a control.
pub fn row_control_rect(
    row_type: i32,
    ctrl_right: i32,
    cy: i32,
    ctrl_h: i32,
    btn_h: i32,
    dpi: u32,
) -> Option<CtrlRect> {
    let g = defaults();
    let rect = |x, y, w, h, vis_h| CtrlRect { x, y, w, h, vis_h };
    match row_type {
        TY_TOGGLE => Some(rect(0, 0, 0, 0, 0)),
        TY_COMBO => {
            let w = scale(g.combo_width, dpi);
            Some(rect(
                ctrl_right - w,
                cy,
                w,
                scale(g.combo_drop_h, dpi),
                scale(g.ctrl_height, dpi),
            ))
        }
        TY_EDIT => {
            let w = scale(g.edit_width, dpi);
            Some(rect(ctrl_right - w, cy, w, ctrl_h, ctrl_h))
        }
        TY_BUTTON | TY_CLICKABLE => {
            let w = scale(g.button_width, dpi);
            Some(rect(ctrl_right - w, cy, w, btn_h, btn_h))
        }
        _ => None,
    }
}

/// Swatch `index` (0..6, three columns) of `CreateColorGridControls`.
pub fn swatch_rect(index: i32, margin: i32, cur_y: i32, ctrl_right: i32, dpi: u32) -> CtrlRect {
    let g = defaults();
    let sw = scale(g.swatch_size, dpi);
    let gap = scale(g.swatch_cell_gap, dpi);
    let cell_h = sw + gap;
    let col_w = (ctrl_right - margin) / 3;
    let (col, row) = (index % 3, index / 3);
    let col_right = margin + (col + 1) * col_w;
    CtrlRect {
        x: col_right - sw,
        y: cur_y + row * cell_h,
        w: sw,
        h: sw,
        vis_h: sw,
    }
}

/// The RichEdit and the 儲存 button of `CreateRichEditorControl`.
#[allow(clippy::too_many_arguments)]
pub fn rich_editor_rects(
    margin: i32,
    card_w: i32,
    pad: i32,
    cur_y: i32,
    this_h: i32,
    ctrl_right: i32,
    btn_w: i32,
    btn_h: i32,
    dpi: u32,
) -> (CtrlRect, CtrlRect) {
    let g = defaults();
    let x = margin + pad;
    let y = cur_y + scale(g.editor_header_h, dpi);
    let w = card_w - 2 * pad;
    let h = this_h - scale(g.editor_header_h + g.row_height_single, dpi);
    let save_y = y + h + scale(g.top_pad, dpi);
    (
        CtrlRect { x, y, w, h, vis_h: h },
        CtrlRect {
            x: ctrl_right - btn_w,
            y: save_y,
            w: btn_w,
            h: btn_h,
            vis_h: btn_h,
        },
    )
}

/// Where `RebuildContentArea` moves the content child: right of the sidebar
/// and below the breadcrumb strip on a sub-page.
pub fn content_area_rect(
    client_w: i32,
    client_h: i32,
    dpi: u32,
    sidebar_takes_space: bool,
    has_breadcrumb: bool,
) -> CtrlRect {
    let g = defaults();
    let sidebar = if sidebar_takes_space {
        scale(g.sidebar_width, dpi)
    } else {
        0
    };
    let bc = if has_breadcrumb { scale(g.breadcrumb_h, dpi) } else { 0 };
    let h = client_h - bc;
    CtrlRect {
        x: sidebar,
        y: bc,
        w: client_w - sidebar,
        h,
        vis_h: h,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_config::page_layout::{ime_cards, load_cards, ACT_NAVIGATE, MODE_ALL};
    use dime_config::settings_model::MODE_DAYI;

    fn ctx(dpi: u32) -> RowContext {
        RowContext {
            dpi,
            scroll_pos: 0,
            mode_mask: MODE_DAYI,
            color_mode: 3,
            grid_expanded: 1,
            preview_h: 150,
        }
    }

    #[test]
    fn scale_is_muldiv() {
        assert_eq!(scale(10, 144), 15);
        assert_eq!(scale(10, 0), 0);
        assert_eq!(scale(7, 120), 9);
    }

    #[test]
    fn card_and_ctrl_widths() {
        assert_eq!(card_width(1000, 96, true, 17), 1000 - 280 - 72 - 17);
        assert_eq!(card_width(1000, 96, false, 17), 1000 - 72 - 17);
        assert_eq!(ctrl_right(36, 600, 96, false), 36 + 600 - 10 - 16);
        assert_eq!(ctrl_right(36, 600, 96, true), 36 + 600 - 10 - 16 - 24);
    }

    #[test]
    fn first_card_hits_from_its_padding() {
        let cards = ime_cards();
        let exp = [false; EXPANDED_SLOTS];
        // First card box starts at topMargin (56) and is 2*10+49 tall.
        assert_eq!(card_list_hit_test(55, cards, MODE_DAYI, &exp, 0, 96, 0), -1);
        assert_eq!(card_list_hit_test(56, cards, MODE_DAYI, &exp, 0, 96, 0), 0);
        assert_eq!(card_list_hit_test(56 + 69, cards, MODE_DAYI, &exp, 0, 96, 0), -1); // gap
        assert_eq!(card_list_hit_test(56 + 73, cards, MODE_DAYI, &exp, 0, 96, 0), 1);
        // The category row is the inner 49 px.
        assert_eq!(
            card_list_click(66, cards, MODE_DAYI, &exp, 0, 96, 0),
            CardListClick {
                kind: 1,
                card: 0,
                row: -1
            }
        );
        assert_eq!(card_list_click(65, cards, MODE_DAYI, &exp, 0, 96, 0).kind, 0);
    }

    #[test]
    fn expanded_load_card_children() {
        let cards = load_cards();
        let i = cards.iter().position(|c| c.action == ACT_EXPAND).unwrap();
        let mut exp = [false; EXPANDED_SLOTS];
        exp[4 + i] = true;
        let mut ys = Vec::new();
        for y in 0..2000 {
            let c = card_list_click(y, cards, MODE_ALL, &exp, 4, 96, 0);
            if c.kind == 2 {
                ys.push((c.row, y));
            }
        }
        let n = card_rows(&cards[i]).len();
        assert_eq!(ys.iter().map(|p| p.0).max().unwrap() as usize, n - 1);
        assert_eq!(ys.len(), n * 50);
    }

    #[test]
    fn plan_matches_card_list_rows() {
        let mut input = PlanInput {
            dpi: 96,
            selected_mode_index: 0,
            nav_level: 0,
            current_card_index: 0,
            mode_mask: MODE_DAYI,
            color_mode: 0,
            grid_expanded: 0,
            preview_h: 150,
            expanded: [1; EXPANDED_SLOTS],
        };
        let (placed, res) = plan_controls(&input, ime_cards(), load_cards());
        assert_eq!(res.populate, 1);
        for p in &placed {
            // Each control is vertically centred in a child row that the click finder reports.
            let c = card_list_click(p.y, ime_cards(), MODE_DAYI, &[true; EXPANDED_SLOTS], 0, 96, 0);
            assert_eq!((c.kind, c.card, c.row), (2, p.card, p.row));
        }
        input.nav_level = 7;
        assert_eq!(
            plan_controls(&input, ime_cards(), load_cards()).1,
            PlanResult::default()
        );
    }

    #[test]
    fn sub_page_spans_cover_hits() {
        let cards = ime_cards();
        let c = ctx(96);
        for card in cards.iter().filter(|c| c.action == ACT_NAVIGATE) {
            let spans = row_spans(card, &c);
            let mut top = 8;
            for s in &spans {
                assert_eq!(row_cards_hit(top, card, &c).span_start, s.start_row);
                assert_eq!(row_cards_hit(top + s.height - 1, card, &c).span_start, s.start_row);
                top += s.height + 4;
            }
            assert_eq!(row_cards_hit(top, card, &c).span_start, -1);
        }
    }

    #[test]
    fn rich_editor_and_swatches() {
        let (re, save) = rich_editor_rects(36, 600, 10, 100, 400, 600, 120, 28, 96);
        assert_eq!(
            re,
            CtrlRect {
                x: 46,
                y: 152,
                w: 580,
                h: 400 - 52 - 38,
                vis_h: 310
            }
        );
        assert_eq!(save.y, 152 + 310 + 8);
        let s = swatch_rect(4, 36, 10, 600, 96);
        assert_eq!((s.x, s.y), (36 + 2 * 188 - 28, 10 + 30));
        let content = content_area_rect(900, 600, 96, true, true);
        assert_eq!((content.x, content.y, content.w, content.h), (280, 44, 620, 556));
    }
}
