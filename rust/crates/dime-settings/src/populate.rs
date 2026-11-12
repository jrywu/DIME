//! Fill the settings controls from the snapshot (phase 5b-8e).
//!
//! Port of `SettingsPopulate::Cpp::Populate` (`src/DIMESettings/
//! SettingsPopulate.cpp`, formerly `PopulateControls`) as a list of control
//! operations the window applies in order. Controls that are not on the page
//! are skipped by the window, as the C++ skips a null `FindControl`.

use dime_config::settings_model::{
    SettingsSnapshot, CTRL_ARRAY_FORCE_SP, CTRL_ARRAY_NOTIFY_SP, CTRL_ARRAY_SINGLE_QUOTE, CTRL_ARROW_KEY_SW_PAGES,
    CTRL_AUTO_COMPOSE, CTRL_CHARSET_SCOPE, CTRL_CLEAR_ON_BEEP, CTRL_CUSTOM_TABLE_PRIORITY, CTRL_DAYI_ARTICLE,
    CTRL_DAYI_SELKEY_MODE, CTRL_DOUBLE_SINGLE_BYTE, CTRL_DO_BEEP, CTRL_DO_BEEP_CANDI, CTRL_DO_BEEP_NOTIFY,
    CTRL_IME_SHIFT_MODE, CTRL_KEYBOARD_OPEN_CLOSE, CTRL_MAKE_PHRASE, CTRL_MAX_CODES, CTRL_NUMERIC_PAD,
    CTRL_OUTPUT_CHT_CHS, CTRL_PHONETIC_KB, CTRL_SENTENCE_MODE, CTRL_SHIFT_ENGLISH_DIGIT_MODE, CTRL_SHOW_NOTIFY,
    CTRL_SPACE_AS_FIRST_CAND, CTRL_SPACE_AS_PAGEDOWN,
};

const CTRL_COLOR_MODE: i32 = 4;
const CTRL_ARRAY_SCOPE: i32 = 13;
const CTRL_REVERSE_CONVERSION: i32 = 33;

pub const OP_RESET: i32 = 1;
pub const OP_ADD: i32 = 2;
pub const OP_SELECT: i32 = 3;
pub const OP_CHECK: i32 = 4;
pub const OP_TEXT: i32 = 5;

/// One control operation. `text` is used by `OP_ADD` and `OP_TEXT`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Op {
    pub kind: i32,
    pub ctrl_id: i32,
    /// `OP_SELECT`: the index; `OP_CHECK`: 0 / 1.
    pub value: i32,
    /// `OP_ADD`: item data to set on the new item.
    pub data: Option<i32>,
    pub text: Vec<u16>,
}

/// One entry of the reverse-conversion combo.
pub struct ReverseEntry<'a> {
    pub description: &'a [u16],
    pub selected: bool,
}

struct Ops(Vec<Op>);

impl Ops {
    fn push(&mut self, kind: i32, id: i32, value: i32, data: Option<i32>, text: &str) {
        self.0.push(Op {
            kind,
            ctrl_id: id,
            value,
            data,
            text: text.encode_utf16().collect(),
        });
    }
    fn reset(&mut self, id: i32) {
        self.push(OP_RESET, id, 0, None, "");
    }
    fn add(&mut self, id: i32, text: &str, data: Option<i32>) {
        self.push(OP_ADD, id, 0, data, text);
    }
    fn select(&mut self, id: i32, index: i32) {
        self.push(OP_SELECT, id, index, None, "");
    }
    fn check(&mut self, id: i32, on: i32) {
        self.push(OP_CHECK, id, i32::from(on != 0), None, "");
    }
    /// `ComboAddAndSelect`: reset, add, select when `sel` is in range.
    fn combo(&mut self, id: i32, items: &[&str], sel: i32) {
        self.reset(id);
        for item in items {
            self.add(id, item, None);
        }
        if sel >= 0 && (sel as usize) < items.len() {
            self.select(id, sel);
        }
    }
    /// Items with item data, then select the first item whose data matches.
    fn combo_by_data(&mut self, id: i32, items: &[(&str, i32)], value: i32) {
        self.reset(id);
        for (text, data) in items {
            self.add(id, text, Some(*data));
        }
        if let Some(i) = items.iter().position(|(_, d)| *d == value) {
            self.select(id, i as i32);
        }
    }
}

/// `SettingsPopulate::Cpp::Populate` as operations, in the C++ order.
pub fn populate(s: &SettingsSnapshot, windows1809: bool, reverse_is_null: bool, reverse: &[ReverseEntry]) -> Vec<Op> {
    let mut o = Ops(Vec::new());

    let mut color_items: Vec<(&str, i32)> = Vec::new();
    if windows1809 {
        color_items.push(("跟隨系統模式", 0));
    }
    color_items.extend([("淡色模式", 1), ("深色模式", 2), ("自訂", 3)]);
    o.combo_by_data(CTRL_COLOR_MODE, &color_items, s.color_mode);

    o.check(CTRL_SHOW_NOTIFY, s.show_notify_desktop);
    o.combo_by_data(
        CTRL_ARRAY_SCOPE,
        &[
            ("行列30 Big5 (繁體中文)", 5),
            ("行列30 Unicode Ext-A", 0),
            ("行列30 Unicode Ext-AB", 1),
            ("行列30 Unicode Ext-A~D", 2),
            ("行列30 Unicode Ext-A~J", 3),
            ("行列40 Big5", 4),
        ],
        s.array_scope,
    );
    o.combo(
        CTRL_CHARSET_SCOPE,
        &["完整字集", "繁體中文"],
        i32::from(s.big5_filter != 0),
    );
    o.combo(
        CTRL_NUMERIC_PAD,
        &["數字鍵盤輸入數字符號", "數字鍵盤輸入字根", "僅用數字鍵盤輸入字根"],
        s.numeric_pad,
    );
    o.combo(
        CTRL_SHIFT_ENGLISH_DIGIT_MODE,
        &["CapsLock 關輸出符號", "CapsLock 關輸出數字", "永遠輸出符號"],
        s.shift_english_digit_mode,
    );
    o.combo(
        CTRL_DAYI_SELKEY_MODE,
        &["依碼表設定", "數字鍵 1~0", "大易選擇鍵 ' [ ] - \\"],
        s.dayi_selkey_mode,
    );
    o.combo(CTRL_PHONETIC_KB, &["標準鍵盤", "倚天鍵盤"], s.phonetic_keyboard_layout);
    o.push(OP_TEXT, CTRL_MAX_CODES, 0, None, &s.max_codes.to_string());
    o.check(CTRL_ARROW_KEY_SW_PAGES, s.arrow_key_sw_pages);
    o.check(CTRL_SPACE_AS_PAGEDOWN, s.space_as_page_down);
    o.check(CTRL_SPACE_AS_FIRST_CAND, s.space_as_first_cand_selkey);
    o.check(CTRL_AUTO_COMPOSE, s.auto_compose);
    o.check(CTRL_CLEAR_ON_BEEP, s.clear_on_beep);
    o.check(CTRL_MAKE_PHRASE, s.make_phrase);
    o.check(CTRL_CUSTOM_TABLE_PRIORITY, s.custom_table_priority);
    o.check(CTRL_DO_BEEP, s.do_beep);
    o.check(CTRL_DO_BEEP_NOTIFY, s.do_beep_notify);
    o.check(CTRL_DO_BEEP_CANDI, s.do_beep_on_candi);
    o.combo(
        CTRL_IME_SHIFT_MODE,
        &["左右SHIFT鍵", "右SHIFT鍵", "左SHIFT鍵", "無(僅Ctrl-Space鍵)"],
        s.ime_shift_mode,
    );
    o.combo(
        CTRL_DOUBLE_SINGLE_BYTE,
        &["以 Shift-Space 熱鍵切換", "半型", "全型"],
        s.double_single_byte_mode,
    );

    o.reset(CTRL_REVERSE_CONVERSION);
    o.add(CTRL_REVERSE_CONVERSION, "(無)", None);
    if reverse_is_null {
        o.select(CTRL_REVERSE_CONVERSION, 0);
    }
    for (i, entry) in reverse.iter().enumerate() {
        o.0.push(Op {
            kind: OP_ADD,
            ctrl_id: CTRL_REVERSE_CONVERSION,
            value: 0,
            data: None,
            text: entry.description.to_vec(),
        });
        if entry.selected {
            o.select(CTRL_REVERSE_CONVERSION, i as i32 + 1);
        }
    }

    o.reset(CTRL_KEYBOARD_OPEN_CLOSE);
    o.add(CTRL_KEYBOARD_OPEN_CLOSE, "中文模式", None);
    o.add(CTRL_KEYBOARD_OPEN_CLOSE, "英數模式", None);
    o.select(
        CTRL_KEYBOARD_OPEN_CLOSE,
        if s.activated_keyboard_mode != 0 { 0 } else { 1 },
    );
    o.reset(CTRL_OUTPUT_CHT_CHS);
    o.add(CTRL_OUTPUT_CHT_CHS, "不轉換", None);
    o.add(CTRL_OUTPUT_CHT_CHS, "繁轉簡", None);
    o.select(CTRL_OUTPUT_CHT_CHS, i32::from(s.do_han_convert != 0));

    o.check(CTRL_DAYI_ARTICLE, s.dayi_article_mode);
    o.check(CTRL_SENTENCE_MODE, s.sentence_mode);
    o.check(CTRL_ARRAY_FORCE_SP, s.array_force_sp);
    o.check(CTRL_ARRAY_NOTIFY_SP, s.array_notify_sp);
    o.check(CTRL_ARRAY_SINGLE_QUOTE, s.array_single_quote_custom_phrase);
    o.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn color_mode_selects_by_data() {
        let s = SettingsSnapshot {
            color_mode: 3,
            ..Default::default()
        };
        let ops = populate(&s, false, true, &[]);
        let sel: Vec<_> = ops
            .iter()
            .filter(|o| o.ctrl_id == CTRL_COLOR_MODE && o.kind == OP_SELECT)
            .collect();
        assert_eq!(sel[0].value, 2); // no 跟隨系統模式: 淡色, 深色, 自訂
        let ops = populate(&s, true, true, &[]);
        let sel: Vec<_> = ops
            .iter()
            .filter(|o| o.ctrl_id == CTRL_COLOR_MODE && o.kind == OP_SELECT)
            .collect();
        assert_eq!(sel[0].value, 3);
    }

    #[test]
    fn out_of_range_selection_is_skipped() {
        let s = SettingsSnapshot {
            numeric_pad: 7,
            max_codes: 5,
            ..Default::default()
        };
        let ops = populate(&s, true, false, &[]);
        assert!(!ops.iter().any(|o| o.ctrl_id == CTRL_NUMERIC_PAD && o.kind == OP_SELECT));
        let text = ops.iter().find(|o| o.ctrl_id == CTRL_MAX_CODES).unwrap();
        assert_eq!(String::from_utf16(&text.text).unwrap(), "5");
    }
}
