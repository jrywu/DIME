//! `WM_COMMAND` of the settings content area (phase 5b-8c).
//!
//! Port of `SettingsCommand::Cpp::Handle` (`src/DIMESettings/
//! SettingsCommand.cpp`): buttons (font, reset, load table, custom table
//! save / export / import), combo boxes, the max-codes edit, the editor's
//! `EN_CHANGE`, and the color swatches. Every side effect goes through
//! [`Host`]; the calls, their order, and their arguments follow the C++.

use dime_config::geometry::mul_div;
use dime_config::page_layout::{
    ime_cards, load_cards, ACT_EXPORT, ACT_FONT, ACT_IMPORT, ACT_LOAD, ACT_NONE, ACT_RESET, CTRL_CUSTOM_TABLE_EDITOR,
};
use dime_config::settings_model::{load_table_suffix, parse_max_codes, sync_combo, SettingsSnapshot, CTRL_MAX_CODES};

use crate::layout::card_rows;

/// `RowAction::SaveCustomTable`.
pub const ACT_SAVE: i32 = 7;
const CTRL_COLOR_MODE: i32 = 4;
const CTRL_ARRAY_SCOPE: i32 = 13;
const CTRL_REVERSE_CONVERSION: i32 = 33;
const CTRL_CUSTOM_TABLE_SAVE: i32 = 201;

const BN_CLICKED: i32 = 0;
const STN_CLICKED: i32 = 0;
const EN_CHANGE: i32 = 0x0300;
const CBN_SELCHANGE: i32 = 1;
const CB_ERR: i32 = -1;

pub const MB_OK: u32 = 0;
pub const MB_ICONERROR: u32 = 0x10;
pub const MB_ICONWARNING: u32 = 0x30;
pub const MB_ICONINFORMATION: u32 = 0x40;

const MAX_REPORT: i32 = 10;
const MAX_PATH: usize = 260;
const LF_FACESIZE: usize = 32;
const DEFAULT_CHARSET: u8 = 1;

/// `SettingsCommand::CommandEvent`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CommandEvent {
    pub notify_code: i32,
    pub ctrl_id: i32,
    pub mode: i32,
    pub combo_sel: i32,
    pub combo_item_data: i32,
    pub edit_text: [u16; 16],
}

/// `LOGFONTW`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LogFont {
    pub height: i32,
    pub width: i32,
    pub escapement: i32,
    pub orientation: i32,
    pub weight: i32,
    pub italic: u8,
    pub underline: u8,
    pub strike_out: u8,
    pub char_set: u8,
    pub out_precision: u8,
    pub clip_precision: u8,
    pub quality: u8,
    pub pitch_and_family: u8,
    pub face_name: [u16; LF_FACESIZE],
}

/// `SettingsCommand::Host`. Strings are UTF-16 without NULs.
pub trait Host {
    fn log_pixels_y(&mut self) -> i32;
    fn choose_font(&mut self, lf: &mut LogFont, point_size10: &mut i32) -> bool;
    fn choose_color(&mut self, rgb: &mut u32) -> bool;
    /// `filter` 0: .cin / .txt, 1: .txt.
    fn get_open_file(&mut self, filter: i32) -> Option<Vec<u16>>;
    fn get_save_file(&mut self, default_path: &[u16]) -> Option<Vec<u16>>;
    fn message_box(&mut self, text: &[u16], caption: &str, mb_type: u32) -> i32;
    fn app_data_path(&mut self) -> Vec<u16>;
    fn documents_path(&mut self) -> Vec<u16>;
    fn custom_table_txt_path(&mut self, mode: i32) -> Vec<u16>;
    fn custom_table_cin_path(&mut self, mode: i32) -> Vec<u16>;
    fn write_bom_file(&mut self, path: &[u16], text: &[u16]) -> bool;
    fn load_text_file(&mut self, path: &[u16]) -> Option<Vec<u16>>;
    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom_table: bool) -> bool;
    fn reset_all_defaults(&mut self, mode: i32);
    fn write_config(&mut self, mode: i32);
    fn load_config(&mut self, mode: i32);
    fn load_snapshot(&mut self) -> SettingsSnapshot;
    /// Applies and saves the window's snapshot (the one `handle` updates).
    fn apply_and_save(&mut self);
    fn set_color_mode(&mut self, color_mode: i32);
    fn load_colors_for_mode(&mut self, color_mode: i32);
    fn get_colors(&mut self) -> [u32; 6];
    fn set_color(&mut self, index: i32, color: u32);
    fn save_colors_for_mode(&mut self, color_mode: i32);
    fn set_reverse_conversion_selection(&mut self, sel: i32);
    fn rebuild(&mut self);
    fn invalidate_content(&mut self, erase: bool);
    fn update_content(&mut self);
    fn invalidate_swatch(&mut self, index: i32);
    fn editor_exists(&mut self) -> bool;
    fn editor_text(&mut self) -> Vec<u16>;
    fn set_editor_text_muted(&mut self, text: &[u16]);
    fn editor_line_index(&mut self, line: i32) -> i32;
    fn editor_focus_at(&mut self, cp: i32);
    fn validate_viewport(&mut self);
    fn cancel_bg_scan(&mut self);
    fn enable_save(&mut self, enable: bool);
    fn update_line_count(&mut self);
    /// Returns (error count, first error line, summary).
    fn validate_buffer(&mut self, text: &[u16], max_report: i32) -> (i32, i32, Vec<u16>);
}

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

/// Units before the NUL (or all of them).
fn until_nul(s: &[u16]) -> &[u16] {
    &s[..s.iter().position(|&c| c == 0).unwrap_or(s.len())]
}

/// `StringCchCopyW(dst, LF_FACESIZE, src)`: the units and a NUL; the rest of
/// `dst` is left as it was.
fn copy_face(dst: &mut [u16; LF_FACESIZE], src: &[u16]) {
    let src = until_nul(src).to_vec();
    let n = src.len().min(LF_FACESIZE - 1);
    dst[..n].copy_from_slice(&src[..n]);
    dst[n] = 0;
}

/// `StringCchPrintfW` into `cch` units: truncated to `cch - 1`.
fn truncate(mut v: Vec<u16>, cch: usize) -> Vec<u16> {
    v.truncate(cch - 1);
    v
}

fn find_action(btn_id: i32) -> i32 {
    for cards in [ime_cards(), load_cards()] {
        for card in cards {
            if let Some(row) = card_rows(card).iter().find(|r| r.id == btn_id) {
                if row.action != ACT_NONE {
                    return row.action;
                }
                // C++ keeps searching after a match whose action is None.
            }
        }
    }
    if btn_id == CTRL_CUSTOM_TABLE_SAVE {
        ACT_SAVE
    } else {
        ACT_NONE
    }
}

fn jump_to_line(host: &mut dyn Host, first_error_line: i32) {
    let cp = host.editor_line_index(first_error_line - 1);
    if cp >= 0 {
        host.editor_focus_at(cp);
    }
}

fn error_message(head: &str, count: i32, first: i32, summary: &[u16], tail: &str) -> Vec<u16> {
    let mut msg = w(&format!(
        "{head}偵測到 {count} 個格式錯誤行，第一個錯誤在第 {first} 行。\n\n"
    ));
    msg.extend_from_slice(summary);
    msg.extend(w(tail));
    truncate(msg, 2048)
}

fn more(count: i32) -> &'static str {
    if count > MAX_REPORT {
        "  ...(更多錯誤已省略)\n"
    } else {
        ""
    }
}

/// A short-lived view of the window's snapshot. Host calls may read it (a
/// rebuild re-reads the snapshot), so no reference outlives a statement.
///
/// # Safety
/// `s` points at a live snapshot; the result is dropped before the next
/// host call.
unsafe fn snap<'a>(s: *mut SettingsSnapshot) -> &'a mut SettingsSnapshot {
    // SAFETY: per the contract above.
    unsafe { &mut *s }
}

unsafe fn font_dialog(host: &mut dyn Host, s: *mut SettingsSnapshot) {
    let lp_y = host.log_pixels_y();
    let mut lf = LogFont::default();
    {
        // SAFETY: `s` is live; no host call while this borrow exists.
        let sn = unsafe { snap(s) };
        copy_face(&mut lf.face_name, &sn.font_face_name);
        lf.height = -mul_div(sn.font_size as i32, lp_y, 72);
        lf.weight = sn.font_weight as i32;
        lf.italic = sn.font_italic as u8;
    }
    lf.char_set = DEFAULT_CHARSET;
    let mut pt10 = 0;
    if host.choose_font(&mut lf, &mut pt10) {
        {
            // SAFETY: as above.
            let sn = unsafe { snap(s) };
            copy_face(&mut sn.font_face_name, &lf.face_name);
            sn.font_size = (pt10 / 10) as u32;
            sn.font_weight = lf.weight as u32;
            sn.font_italic = i32::from(lf.italic);
        }
        host.apply_and_save();
        host.rebuild();
        host.invalidate_content(false);
        host.update_content();
    }
}

unsafe fn reset_defaults(host: &mut dyn Host, s: *mut SettingsSnapshot, mode: i32) {
    host.reset_all_defaults(mode);
    host.write_config(mode);
    host.load_config(mode);
    let fresh = host.load_snapshot();
    // SAFETY: as above.
    unsafe { *snap(s) = fresh };
    host.rebuild();
    host.invalidate_content(false);
    host.update_content();
}

fn load_table(host: &mut dyn Host, btn_id: i32) {
    let Some(suffix) = load_table_suffix(btn_id) else {
        return;
    };
    let Some(path_to_load) = host.get_open_file(0) else {
        return;
    };
    let mut dst = host.app_data_path();
    dst.extend_from_slice(until_nul(suffix));
    let dst = truncate(dst, MAX_PATH);
    if host.parse_cin(&path_to_load, &dst, false) {
        host.message_box(&w("自訂碼表載入完成。"), "DIME 自訂碼表", MB_ICONINFORMATION);
    } else {
        host.message_box(&w("自訂碼表載入發生錯誤 !!"), "DIME 自訂碼表", MB_ICONERROR);
    }
}

fn save_custom_table(host: &mut dyn Host, mode: i32) {
    if !host.editor_exists() {
        return;
    }
    let text = host.editor_text();
    let (count, first, summary) = host.validate_buffer(&text, MAX_REPORT);
    if count > 0 {
        let msg = error_message(
            "儲存失敗：",
            count,
            first,
            &summary,
            &format!("{}按確定後將跳到第一個錯誤行。", more(count)),
        );
        host.message_box(&msg, "自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
        if first > 0 {
            jump_to_line(host, first);
        }
        return;
    }
    let txt_path = host.custom_table_txt_path(mode);
    if host.write_bom_file(&txt_path, &text) {
        let cin_path = host.custom_table_cin_path(mode);
        if !host.parse_cin(&txt_path, &cin_path, true) {
            host.message_box(&w("自建詞庫載入發生錯誤！"), "DIME 自建詞庫", MB_ICONERROR);
        }
    }
    host.enable_save(false);
}

fn export_custom_table(host: &mut dyn Host) {
    if !host.editor_exists() {
        return;
    }
    let text = host.editor_text();
    let (count, first, summary) = host.validate_buffer(&text, MAX_REPORT);
    if count > 0 {
        let msg = error_message("匯出失敗：", count, first, &summary, "請修正後再匯出。");
        host.message_box(&msg, "自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
        return;
    }
    let mut default_path = host.documents_path();
    default_path.extend(w("\\customTable.txt"));
    let default_path = truncate(default_path, MAX_PATH);
    if let Some(path) = host.get_save_file(&default_path) {
        if host.editor_exists() {
            let text = host.editor_text();
            host.write_bom_file(&path, &text);
        }
    }
}

fn import_custom_table(host: &mut dyn Host) {
    let Some(path) = host.get_open_file(1) else {
        return;
    };
    if !host.editor_exists() {
        return;
    }
    let Some(text) = host.load_text_file(&path) else {
        host.message_box(
            &w("無法讀取或解碼檔案，請確認檔案編碼為 UTF-8 / UTF-16 / ANSI(Big5)。"),
            "DIME 自建詞庫",
            MB_ICONERROR,
        );
        return;
    };
    let (count, first, summary) = host.validate_buffer(&text, MAX_REPORT);
    host.set_editor_text_muted(&text);
    host.enable_save(true);
    host.validate_viewport();
    if count > 0 && first > 0 {
        let msg = error_message(
            "匯入完成，但",
            count,
            first,
            &summary,
            &format!("{}按確定後將跳到第一個錯誤行，請修正後再儲存。", more(count)),
        );
        host.message_box(&msg, "DIME 自建詞庫匯入", MB_ICONWARNING);
        jump_to_line(host, first);
    }
}

/// `SettingsCommand::Cpp::Handle`.
/// `set_grid` writes the window's `colorGridExpanded` at once: `host.rebuild()`
/// reads it during the call.
/// `SettingsCommand::Handle`. The snapshot is read and written in place
/// through `s` (host calls may read it, e.g. a rebuild); `set_grid` writes
/// the window's colour grid flag.
///
/// # Safety
/// `s` points at the window's live snapshot for the whole call, and nothing
/// else holds a reference to it across a host call.
pub unsafe fn handle(host: &mut dyn Host, s: *mut SettingsSnapshot, set_grid: &mut dyn FnMut(bool), ev: &CommandEvent) {
    let notify = ev.notify_code;
    let ctrl_id = ev.ctrl_id;

    if notify == BN_CLICKED {
        let btn_id = ctrl_id - 3000;
        match find_action(btn_id) {
            // SAFETY: forwarded contract.
            ACT_FONT => unsafe { font_dialog(host, s) },
            // SAFETY: forwarded contract.
            ACT_RESET => unsafe { reset_defaults(host, s, ev.mode) },
            ACT_LOAD => load_table(host, btn_id),
            ACT_SAVE => save_custom_table(host, ev.mode),
            ACT_EXPORT => export_custom_table(host),
            ACT_IMPORT => import_custom_table(host),
            _ => {}
        }
    }

    if notify == EN_CHANGE {
        let id = ctrl_id - 3000;
        if id == CTRL_CUSTOM_TABLE_EDITOR {
            host.validate_viewport();
            host.cancel_bg_scan();
            host.enable_save(true);
            host.update_line_count();
        } else if id == CTRL_MAX_CODES {
            if let Some(val) = parse_max_codes(Some(until_nul(&ev.edit_text))) {
                // SAFETY: forwarded contract.
                unsafe { snap(s).max_codes = val };
                host.apply_and_save();
            }
        }
    }

    if notify == CBN_SELCHANGE {
        let id = ctrl_id - 3000;
        let sel = ev.combo_sel;
        if sel != CB_ERR {
            // SAFETY (each `snap`): forwarded contract; no host call while borrowed.
            match id {
                CTRL_COLOR_MODE => unsafe { snap(s).color_mode = ev.combo_item_data },
                CTRL_ARRAY_SCOPE => unsafe { snap(s).array_scope = ev.combo_item_data },
                CTRL_REVERSE_CONVERSION => host.set_reverse_conversion_selection(sel),
                _ => sync_combo(unsafe { snap(s) }, id, sel),
            }
            if id == CTRL_COLOR_MODE {
                let color_mode = unsafe { snap(s).color_mode };
                set_grid(color_mode == 3);
                host.set_color_mode(color_mode);
                host.write_config(ev.mode);
                host.load_colors_for_mode(color_mode);
                let c = host.get_colors();
                {
                    let sn = unsafe { snap(s) };
                    sn.item_color = c[0];
                    sn.selected_color = c[1];
                    sn.item_bg_color = c[2];
                    sn.phrase_color = c[3];
                    sn.number_color = c[4];
                    sn.selected_bg_color = c[5];
                }
                host.rebuild();
            } else {
                host.apply_and_save();
            }
            host.invalidate_content(false);
            host.update_content();
        }
    }

    if notify == STN_CLICKED || (notify == BN_CLICKED && (4000..4006).contains(&ctrl_id)) {
        let idx = ctrl_id - 4000;
        if (0..6).contains(&idx) {
            // SAFETY (each `snap`): forwarded contract; no host call while borrowed.
            let current = {
                let sn = unsafe { snap(s) };
                match idx {
                    0 => sn.item_color,
                    1 => sn.selected_color,
                    2 => sn.item_bg_color,
                    3 => sn.phrase_color,
                    4 => sn.number_color,
                    _ => sn.selected_bg_color,
                }
            };
            let mut rgb = current;
            if host.choose_color(&mut rgb) {
                let color_mode = {
                    let sn = unsafe { snap(s) };
                    match idx {
                        0 => sn.item_color = rgb,
                        1 => sn.selected_color = rgb,
                        2 => sn.item_bg_color = rgb,
                        3 => sn.phrase_color = rgb,
                        4 => sn.number_color = rgb,
                        _ => sn.selected_bg_color = rgb,
                    }
                    sn.color_mode
                };
                host.load_colors_for_mode(color_mode);
                host.set_color(idx, rgb);
                host.save_colors_for_mode(color_mode);
                host.apply_and_save();
                host.invalidate_swatch(idx);
                host.invalidate_content(false);
                host.update_content();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::size_of;

    #[test]
    fn struct_sizes() {
        assert_eq!(size_of::<CommandEvent>(), 52);
        assert_eq!(size_of::<LogFont>(), 92);
    }

    #[test]
    fn actions_from_tables() {
        assert_eq!(find_action(CTRL_CUSTOM_TABLE_SAVE), ACT_SAVE);
        assert_eq!(find_action(9999), ACT_NONE);
        assert_eq!(find_action(dime_config::settings_model::CTRL_LOAD_DAYI_MAIN), ACT_LOAD);
    }

    #[test]
    fn messages_truncate_like_strsafe() {
        let long = vec![b'x' as u16; 3000];
        let m = error_message("儲存失敗：", 12, 3, &long, "tail");
        assert_eq!(m.len(), 2047);
        let m = error_message("匯出失敗：", 1, 2, &w("  第 2 行\n"), "請修正後再匯出。");
        assert_eq!(
            String::from_utf16(&m).unwrap(),
            "匯出失敗：偵測到 1 個格式錯誤行，第一個錯誤在第 2 行。\n\n  第 2 行\n請修正後再匯出。"
        );
    }
}
