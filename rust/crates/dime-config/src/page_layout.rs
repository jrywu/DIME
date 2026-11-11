//! Settings page layout (phase 5b-2).
//!
//! The card and row tables match `src/DIMESettings/SettingsPageLayout.cpp`.
//! Strings are NUL-terminated UTF-16. `iconColor` is 0 in every row the C++
//! table ships. Geometry and theme colors stay in C++.

use std::ptr;
use std::sync::OnceLock;

use crate::settings_model::{
    CTRL_ARRAY_FORCE_SP, CTRL_ARRAY_NOTIFY_SP, CTRL_ARRAY_SINGLE_QUOTE, CTRL_ARROW_KEY_SW_PAGES, CTRL_AUTO_COMPOSE,
    CTRL_CHARSET_SCOPE, CTRL_CLEAR_ON_BEEP, CTRL_CUSTOM_TABLE_PRIORITY, CTRL_DAYI_ARTICLE, CTRL_DAYI_SELKEY_MODE,
    CTRL_DOUBLE_SINGLE_BYTE, CTRL_DO_BEEP, CTRL_DO_BEEP_CANDI, CTRL_DO_BEEP_NOTIFY, CTRL_IME_SHIFT_MODE,
    CTRL_KEYBOARD_OPEN_CLOSE, CTRL_LOAD_ARRAY40, CTRL_LOAD_ARRAY_EXPAND, CTRL_LOAD_ARRAY_EXT_B, CTRL_LOAD_ARRAY_EXT_CD,
    CTRL_LOAD_ARRAY_EXT_E_TO_J, CTRL_LOAD_ARRAY_MAIN, CTRL_LOAD_ARRAY_PHRASE, CTRL_LOAD_ARRAY_SC, CTRL_LOAD_ARRAY_SP,
    CTRL_LOAD_ASSOC_PHRASE, CTRL_LOAD_DAYI_MAIN, CTRL_LOAD_GENERIC_MAIN, CTRL_LOAD_PHONETIC_MAIN, CTRL_MAKE_PHRASE,
    CTRL_MAX_CODES, CTRL_NUMERIC_PAD, CTRL_OUTPUT_CHT_CHS, CTRL_PHONETIC_KB, CTRL_SENTENCE_MODE,
    CTRL_SHIFT_ENGLISH_DIGIT_MODE, CTRL_SHOW_NOTIFY, CTRL_SPACE_AS_FIRST_CAND, CTRL_SPACE_AS_PAGEDOWN, MODE_ARRAY,
    MODE_DAYI, MODE_GENERIC, MODE_PHONETIC,
};

pub const CTRL_NONE: i32 = 0;
pub const CTRL_FONT_NAME: i32 = 1;
pub const CTRL_COLOR_MODE: i32 = 4;
pub const CTRL_RESTORE_DEFAULT: i32 = 12;
pub const CTRL_ARRAY_SCOPE: i32 = 13;
pub const CTRL_REVERSE_CONVERSION: i32 = 33;
pub const CTRL_COLOR_GRID: i32 = 41;
pub const CTRL_CANDIDATE_PREVIEW: i32 = 42;
pub const CTRL_CUSTOM_TABLE_EDITOR: i32 = 200;
pub const CTRL_CUSTOM_TABLE_EXPORT: i32 = 202;
pub const CTRL_CUSTOM_TABLE_IMPORT: i32 = 203;

pub const MODE_ALL: i32 = MODE_DAYI | MODE_ARRAY | MODE_PHONETIC | MODE_GENERIC;
const MODE_NON_ARRAY: i32 = MODE_DAYI | MODE_PHONETIC | MODE_GENERIC;

/// `RowType` in `SettingsPageLayout.h`.
pub const TY_TOGGLE: i32 = 0;
pub const TY_COMBO: i32 = 1;
pub const TY_EDIT: i32 = 2;
pub const TY_COLOR_GRID: i32 = 3;
pub const TY_CLICKABLE: i32 = 4;
pub const TY_BUTTON: i32 = 5;
pub const TY_RICH_EDITOR: i32 = 6;
pub const TY_PREVIEW: i32 = 7;

/// `RowAction` in `SettingsPageLayout.h`.
pub const ACT_NONE: i32 = 0;
pub const ACT_TOGGLE: i32 = 1;
pub const ACT_FONT: i32 = 2;
pub const ACT_COLOR: i32 = 3;
pub const ACT_EXPAND: i32 = 4;
pub const ACT_NAVIGATE: i32 = 5;
pub const ACT_LOAD: i32 = 6;
pub const ACT_EXPORT: i32 = 8;
pub const ACT_IMPORT: i32 = 9;
pub const ACT_RESET: i32 = 10;

/// `IDI_*` in `resource.h`.
const IDI_DIMESETTINGS: i32 = 10;
const IDI_DAYI: i32 = 11;
const IDI_ARRAY: i32 = 12;
const IDI_PHONETIC: i32 = 13;
const IDI_GENERIC: i32 = 14;

/// `LayoutRow`. Pointer fields are NUL-terminated UTF-16, or null.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LayoutRow {
    pub id: i32,
    pub row_type: i32,
    pub action: i32,
    pub expand_parent: i32,
    pub icon_color: u32,
    pub label: *const u16,
    pub description: *const u16,
    pub visible_modes: i32,
    pub card_break_before: bool,
    pub icon_glyph: u16,
}

/// `LayoutCard`. `rows` points at `row_count` [`LayoutRow`]s owned by the table.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LayoutCard {
    pub title: *const u16,
    pub description: *const u16,
    pub action: i32,
    pub rows: *const LayoutRow,
    pub row_count: i32,
    pub icon_color: u32,
    pub icon_glyph: u16,
    pub default_expanded: bool,
}

/// `SidebarItemDef`. A null label with icon `-1` is the separator.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SidebarItem {
    pub label: *const u16,
    pub icon_resource_id: i32,
}

// Row vectors are not named after `build`. Their storage keeps the
// pointers inside `ime` and `load` valid.
#[allow(dead_code)]
struct Tables {
    _strings: Vec<Vec<u16>>,
    appearance: Vec<LayoutRow>,
    sound: Vec<LayoutRow>,
    input: Vec<LayoutRow>,
    custom: Vec<LayoutRow>,
    ime: Vec<LayoutCard>,
    array_expand: Vec<LayoutRow>,
    load_dayi: Vec<LayoutRow>,
    load_array: Vec<LayoutRow>,
    load_phonetic: Vec<LayoutRow>,
    load_generic: Vec<LayoutRow>,
    load_assoc: Vec<LayoutRow>,
    load: Vec<LayoutCard>,
    sidebar: Vec<SidebarItem>,
}

fn intern(strings: &mut Vec<Vec<u16>>, text: &str) -> *const u16 {
    let mut units: Vec<u16> = text.encode_utf16().collect();
    units.push(0);
    let p = units.as_ptr();
    strings.push(units);
    p
}

fn maybe(strings: &mut Vec<Vec<u16>>, text: Option<&str>) -> *const u16 {
    match text {
        Some(text) => intern(strings, text),
        None => ptr::null(),
    }
}

#[allow(clippy::too_many_arguments)]
fn row(
    strings: &mut Vec<Vec<u16>>,
    id: i32,
    row_type: i32,
    action: i32,
    expand_parent: i32,
    label: &str,
    description: Option<&str>,
    visible_modes: i32,
    card_break_before: bool,
    icon_glyph: u16,
) -> LayoutRow {
    LayoutRow {
        id,
        row_type,
        action,
        expand_parent,
        icon_color: 0,
        label: intern(strings, label),
        description: maybe(strings, description),
        visible_modes,
        card_break_before,
        icon_glyph,
    }
}

#[allow(clippy::too_many_arguments)]
fn card(
    strings: &mut Vec<Vec<u16>>,
    title: &str,
    description: &str,
    action: i32,
    rows: *const LayoutRow,
    row_count: i32,
    icon_glyph: u16,
    default_expanded: bool,
) -> LayoutCard {
    LayoutCard {
        title: intern(strings, title),
        description: intern(strings, description),
        action,
        rows,
        row_count,
        icon_color: 0,
        icon_glyph,
        default_expanded,
    }
}

fn load_button(strings: &mut Vec<Vec<u16>>, id: i32) -> LayoutRow {
    row(
        strings, id, TY_BUTTON, ACT_LOAD, CTRL_NONE, "載入", None, MODE_ALL, false, 0,
    )
}

fn build() -> Tables {
    let mut s = Vec::new();
    let appearance = vec![
        row(
            &mut s,
            CTRL_CANDIDATE_PREVIEW,
            TY_PREVIEW,
            ACT_NONE,
            CTRL_NONE,
            "預覽",
            None,
            MODE_ALL,
            true,
            0,
        ),
        row(
            &mut s,
            CTRL_FONT_NAME,
            TY_CLICKABLE,
            ACT_FONT,
            CTRL_NONE,
            "字型設定",
            Some("候選字視窗顯示字型"),
            MODE_ALL,
            true,
            0xE8D2,
        ),
        row(
            &mut s,
            CTRL_COLOR_MODE,
            TY_COMBO,
            ACT_EXPAND,
            CTRL_NONE,
            "色彩模式",
            Some("設定候選字視窗色彩主題"),
            MODE_ALL,
            false,
            0xE790,
        ),
        row(
            &mut s,
            CTRL_COLOR_GRID,
            TY_COLOR_GRID,
            ACT_COLOR,
            CTRL_COLOR_MODE,
            "色彩",
            None,
            MODE_ALL,
            false,
            0,
        ),
        row(
            &mut s,
            CTRL_RESTORE_DEFAULT,
            TY_BUTTON,
            ACT_RESET,
            CTRL_NONE,
            "還原預設值",
            Some("將所有外觀設定還原為預設值"),
            MODE_ALL,
            false,
            0xE72C,
        ),
    ];
    let sound = vec![
        row(
            &mut s,
            CTRL_DO_BEEP,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "錯誤提示音",
            Some("輸入錯誤字根時發出提示音"),
            MODE_ALL,
            false,
            0xEA8F,
        ),
        row(
            &mut s,
            CTRL_DO_BEEP_NOTIFY,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "錯誤提示音顯示通知",
            Some("提示音發出時同時顯示桌面通知視窗"),
            MODE_ALL,
            false,
            0xE814,
        ),
        row(
            &mut s,
            CTRL_DO_BEEP_CANDI,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "候選字提示音",
            Some("顯示候選字視窗時發出提示音"),
            MODE_DAYI,
            false,
            0xE994,
        ),
    ];
    let input = vec![
        row(
            &mut s,
            CTRL_SHOW_NOTIFY,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "在桌面模式顯示浮動中英切換視窗",
            None,
            MODE_ALL,
            false,
            0xE982,
        ),
        row(
            &mut s,
            CTRL_ARRAY_SCOPE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "字集查詢範圍",
            None,
            MODE_ARRAY,
            false,
            0xE774,
        ),
        row(
            &mut s,
            CTRL_CHARSET_SCOPE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "字集查詢範圍",
            None,
            MODE_NON_ARRAY,
            false,
            0xE774,
        ),
        row(
            &mut s,
            CTRL_NUMERIC_PAD,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "九宮格數字鍵盤",
            None,
            MODE_ALL,
            false,
            0xE75F,
        ),
        row(
            &mut s,
            CTRL_SHIFT_ENGLISH_DIGIT_MODE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "Shift英數模式數字鍵行為",
            None,
            MODE_ALL,
            false,
            0xE75F,
        ),
        row(
            &mut s,
            CTRL_PHONETIC_KB,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "鍵盤對應選擇",
            None,
            MODE_PHONETIC,
            false,
            0xE765,
        ),
        row(
            &mut s,
            CTRL_MAX_CODES,
            TY_EDIT,
            ACT_NONE,
            CTRL_NONE,
            "組字區最大長度",
            None,
            MODE_ALL,
            false,
            0xE8C1,
        ),
        row(
            &mut s,
            CTRL_ARROW_KEY_SW_PAGES,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "以方向鍵換頁",
            None,
            MODE_ALL,
            false,
            0xE76C,
        ),
        row(
            &mut s,
            CTRL_SPACE_AS_PAGEDOWN,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "以空白鍵換頁",
            None,
            MODE_ALL,
            false,
            0xE75D,
        ),
        row(
            &mut s,
            CTRL_SPACE_AS_FIRST_CAND,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "空白鍵為第一選字鍵",
            None,
            MODE_GENERIC,
            false,
            0xE8C4,
        ),
        row(
            &mut s,
            CTRL_AUTO_COMPOSE,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "打字時同步組字",
            None,
            MODE_ALL,
            false,
            0xE8D3,
        ),
        row(
            &mut s,
            CTRL_CLEAR_ON_BEEP,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "錯誤組字時清除字根",
            None,
            MODE_ALL,
            false,
            0xE894,
        ),
        row(
            &mut s,
            CTRL_MAKE_PHRASE,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "提示聯想字詞",
            None,
            MODE_ALL,
            false,
            0xE82F,
        ),
        row(
            &mut s,
            CTRL_CUSTOM_TABLE_PRIORITY,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "自建詞優先",
            None,
            MODE_ALL,
            false,
            0xE82E,
        ),
        row(
            &mut s,
            CTRL_IME_SHIFT_MODE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "中英切換熱鍵",
            None,
            MODE_ALL,
            false,
            0xE8AB,
        ),
        row(
            &mut s,
            CTRL_DOUBLE_SINGLE_BYTE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "全半形輸入模式",
            None,
            MODE_ALL,
            false,
            0xE97F,
        ),
        row(
            &mut s,
            CTRL_KEYBOARD_OPEN_CLOSE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "預設輸入模式",
            None,
            MODE_ALL,
            false,
            0xE983,
        ),
        row(
            &mut s,
            CTRL_OUTPUT_CHT_CHS,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "輸出字元",
            None,
            MODE_ALL,
            false,
            0xE98A,
        ),
        row(
            &mut s,
            CTRL_REVERSE_CONVERSION,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "反查輸入字根",
            None,
            MODE_ALL,
            false,
            0xE721,
        ),
        row(
            &mut s,
            CTRL_DAYI_ARTICLE,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "地址鍵輸入符號",
            None,
            MODE_DAYI,
            false,
            0xE8AC,
        ),
        row(
            &mut s,
            CTRL_DAYI_SELKEY_MODE,
            TY_COMBO,
            ACT_NONE,
            CTRL_NONE,
            "選字鍵",
            None,
            MODE_DAYI,
            false,
            0xE8C4,
        ),
        row(
            &mut s,
            CTRL_SENTENCE_MODE,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "整句輸入",
            Some("連續打字不選字，依上下文自動組成句子，按 Enter 送出"),
            MODE_DAYI,
            false,
            0xE8D2,
        ),
        row(
            &mut s,
            CTRL_ARRAY_FORCE_SP,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "僅接受輸入特別碼",
            None,
            MODE_ARRAY,
            false,
            0xE73E,
        ),
        row(
            &mut s,
            CTRL_ARRAY_NOTIFY_SP,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "特別碼提示",
            None,
            MODE_ARRAY,
            false,
            0xE7E7,
        ),
        row(
            &mut s,
            CTRL_ARRAY_SINGLE_QUOTE,
            TY_TOGGLE,
            ACT_TOGGLE,
            CTRL_NONE,
            "以'鍵查詢自建詞庫",
            None,
            MODE_ARRAY,
            false,
            0xE848,
        ),
    ];
    let custom = vec![
        row(
            &mut s,
            CTRL_CUSTOM_TABLE_EDITOR,
            TY_RICH_EDITOR,
            ACT_NONE,
            CTRL_NONE,
            "自訂碼表",
            Some("每行格式：輸入碼 空白 詞彙"),
            MODE_ALL,
            false,
            0,
        ),
        row(
            &mut s,
            CTRL_CUSTOM_TABLE_EXPORT,
            TY_BUTTON,
            ACT_EXPORT,
            CTRL_NONE,
            "匯出自建詞庫",
            Some("匯出自建詞庫至檔案"),
            MODE_ALL,
            true,
            0xEDE1,
        ),
        row(
            &mut s,
            CTRL_CUSTOM_TABLE_IMPORT,
            TY_BUTTON,
            ACT_IMPORT,
            CTRL_NONE,
            "匯入自建詞庫",
            Some("從檔案匯入（覆蓋現有內容）"),
            MODE_ALL,
            false,
            0xE8B5,
        ),
    ];
    let array_expand = vec![
        row(
            &mut s,
            CTRL_LOAD_ARRAY_SP,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "特別碼碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY_SC,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "簡碼碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY_EXT_B,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "Unicode Ext. B 碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY_EXT_CD,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "Unicode Ext. CD 碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY_EXT_E_TO_J,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "Unicode Ext. E-J 碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY40,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "行列40碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
        row(
            &mut s,
            CTRL_LOAD_ARRAY_PHRASE,
            TY_BUTTON,
            ACT_LOAD,
            CTRL_LOAD_ARRAY_EXPAND,
            "行列詞庫碼表",
            None,
            MODE_ALL,
            false,
            0xE82D,
        ),
    ];
    let load_dayi = vec![load_button(&mut s, CTRL_LOAD_DAYI_MAIN)];
    let load_array = vec![load_button(&mut s, CTRL_LOAD_ARRAY_MAIN)];
    let load_phonetic = vec![load_button(&mut s, CTRL_LOAD_PHONETIC_MAIN)];
    let load_generic = vec![load_button(&mut s, CTRL_LOAD_GENERIC_MAIN)];
    let load_assoc = vec![load_button(&mut s, CTRL_LOAD_ASSOC_PHRASE)];

    // Row pointers are taken after each Vec is in its final allocation.
    let ime = vec![
        card(
            &mut s,
            "外觀設定",
            "字型、色彩、候選字視窗樣式",
            ACT_NAVIGATE,
            appearance.as_ptr(),
            appearance.len() as i32,
            0xE790,
            false,
        ),
        card(
            &mut s,
            "聲音與通知",
            "提示音、錯誤通知",
            ACT_EXPAND,
            sound.as_ptr(),
            sound.len() as i32,
            0xEA8F,
            false,
        ),
        card(
            &mut s,
            "輸入設定",
            "字集、組字、候選字、切換、輸入法專屬選項",
            ACT_EXPAND,
            input.as_ptr(),
            input.len() as i32,
            0xE765,
            true,
        ),
        card(
            &mut s,
            "自建詞庫",
            "自建詞庫編輯、匯入匯出",
            ACT_NAVIGATE,
            custom.as_ptr(),
            custom.len() as i32,
            0xE70F,
            false,
        ),
    ];
    let load = vec![
        card(
            &mut s,
            "大易主碼表",
            "載入或更新大易輸入法主碼表",
            ACT_NONE,
            load_dayi.as_ptr(),
            1,
            0xE82D,
            false,
        ),
        card(
            &mut s,
            "行列主碼表",
            "載入或更新行列輸入法主碼表",
            ACT_NONE,
            load_array.as_ptr(),
            1,
            0xE82D,
            false,
        ),
        card(
            &mut s,
            "行列擴充碼表",
            "特碼、簡碼、ExtB/CD/E~J、行列40、行列詞庫",
            ACT_EXPAND,
            array_expand.as_ptr(),
            array_expand.len() as i32,
            0xE8F1,
            false,
        ),
        card(
            &mut s,
            "注音主碼表",
            "載入或更新注音輸入法主碼表",
            ACT_NONE,
            load_phonetic.as_ptr(),
            1,
            0xE82D,
            false,
        ),
        card(
            &mut s,
            "自建主碼表",
            "載入或更新自建輸入法主碼表",
            ACT_NONE,
            load_generic.as_ptr(),
            1,
            0xE82D,
            false,
        ),
        card(
            &mut s,
            "聯想詞彙表",
            "載入所有輸入法共用的聯想詞彙表",
            ACT_NONE,
            load_assoc.as_ptr(),
            1,
            0xE82D,
            false,
        ),
    ];
    let sidebar = vec![
        SidebarItem {
            label: intern(&mut s, "大易輸入法"),
            icon_resource_id: IDI_DAYI,
        },
        SidebarItem {
            label: intern(&mut s, "行列輸入法"),
            icon_resource_id: IDI_ARRAY,
        },
        SidebarItem {
            label: intern(&mut s, "傳統注音輸入法"),
            icon_resource_id: IDI_PHONETIC,
        },
        SidebarItem {
            label: intern(&mut s, "自建輸入法"),
            icon_resource_id: IDI_GENERIC,
        },
        SidebarItem {
            label: ptr::null(),
            icon_resource_id: -1,
        },
        SidebarItem {
            label: intern(&mut s, "自定碼表"),
            icon_resource_id: IDI_DIMESETTINGS,
        },
    ];
    Tables {
        _strings: s,
        appearance,
        sound,
        input,
        custom,
        ime,
        array_expand,
        load_dayi,
        load_array,
        load_phonetic,
        load_generic,
        load_assoc,
        load,
        sidebar,
    }
}

// The raw pointers address string and row buffers owned by the same `Tables`.
// `build` freezes the value before it is published, and nothing mutates it.
unsafe impl Send for Tables {}
unsafe impl Sync for Tables {}

fn tables() -> &'static Tables {
    static TABLES: OnceLock<Tables> = OnceLock::new();
    TABLES.get_or_init(build)
}

pub fn ime_cards() -> &'static [LayoutCard] {
    &tables().ime
}

pub fn load_cards() -> &'static [LayoutCard] {
    &tables().load
}

pub fn sidebar_items() -> &'static [SidebarItem] {
    &tables().sidebar
}

/// `IsRowVisible`: a row shows when any of its mode bits is set.
pub fn row_visible(visible_modes: i32, mode_bitmask: i32) -> bool {
    (visible_modes & mode_bitmask) != 0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn utf16(p: *const u16) -> String {
        if p.is_null() {
            return String::new();
        }
        let mut units = Vec::new();
        unsafe {
            let mut i = 0;
            while *p.add(i) != 0 {
                units.push(*p.add(i));
                i += 1;
            }
        }
        String::from_utf16_lossy(&units)
    }

    unsafe fn rows(card: &LayoutCard) -> &[LayoutRow] {
        unsafe { std::slice::from_raw_parts(card.rows, card.row_count as usize) }
    }

    #[test]
    fn layout_struct_matches_msvc() {
        use std::mem::{align_of, offset_of, size_of};
        assert_eq!(
            align_of::<LayoutRow>(),
            if cfg!(target_pointer_width = "64") { 8 } else { 4 }
        );
        assert_eq!(
            align_of::<LayoutCard>(),
            if cfg!(target_pointer_width = "64") { 8 } else { 4 }
        );
        if cfg!(target_pointer_width = "64") {
            assert_eq!(size_of::<LayoutRow>(), 48);
            assert_eq!(offset_of!(LayoutRow, label), 24);
            assert_eq!(offset_of!(LayoutRow, visible_modes), 40);
            assert_eq!(offset_of!(LayoutRow, icon_glyph), 46);
            assert_eq!(size_of::<LayoutCard>(), 48);
            assert_eq!(offset_of!(LayoutCard, rows), 24);
            assert_eq!(offset_of!(LayoutCard, default_expanded), 42);
            assert_eq!(size_of::<SidebarItem>(), 16);
            assert_eq!(offset_of!(SidebarItem, icon_resource_id), 8);
        } else {
            assert_eq!(size_of::<LayoutRow>(), 36);
            assert_eq!(offset_of!(LayoutRow, label), 20);
            assert_eq!(offset_of!(LayoutRow, visible_modes), 28);
            assert_eq!(offset_of!(LayoutRow, icon_glyph), 34);
            assert_eq!(size_of::<LayoutCard>(), 28);
            assert_eq!(offset_of!(LayoutCard, rows), 12);
            assert_eq!(offset_of!(LayoutCard, default_expanded), 26);
            assert_eq!(size_of::<SidebarItem>(), 8);
            assert_eq!(offset_of!(SidebarItem, icon_resource_id), 4);
        }
    }

    #[test]
    fn ime_page_matches_cpp() {
        let cards = ime_cards();
        assert_eq!(cards.len(), 4);
        assert_eq!(utf16(cards[0].title), "外觀設定");
        assert_eq!(utf16(cards[0].description), "字型、色彩、候選字視窗樣式");
        assert_eq!(cards[0].action, ACT_NAVIGATE);
        assert!(!cards[0].default_expanded);
        assert_eq!(cards[0].row_count, 5);
        assert_eq!(utf16(cards[1].title), "聲音與通知");
        assert_eq!(cards[1].action, ACT_EXPAND);
        assert_eq!(cards[1].row_count, 3);
        assert_eq!(utf16(cards[2].title), "輸入設定");
        assert_eq!(utf16(cards[2].description), "字集、組字、候選字、切換、輸入法專屬選項");
        assert!(cards[2].default_expanded);
        assert_eq!(cards[2].row_count, 25);
        assert_eq!(utf16(cards[3].title), "自建詞庫");
        assert_eq!(cards[3].action, ACT_NAVIGATE);
        assert_eq!(cards[3].row_count, 3);
        assert_eq!(cards[0].icon_glyph, 0xE790);
        assert_eq!(cards[2].icon_glyph, 0xE765);

        let appearance = unsafe { rows(&cards[0]) };
        assert_eq!(appearance[0].id, CTRL_CANDIDATE_PREVIEW);
        assert_eq!(appearance[0].row_type, TY_PREVIEW);
        assert!(appearance[0].card_break_before);
        assert!(appearance[0].description.is_null());
        assert_eq!(appearance[1].id, CTRL_FONT_NAME);
        assert!(appearance[1].card_break_before);
        assert_eq!(appearance[1].icon_glyph, 0xE8D2);
        assert_eq!(appearance[3].expand_parent, CTRL_COLOR_MODE);
        assert_eq!(appearance[3].row_type, TY_COLOR_GRID);

        let input = unsafe { rows(&cards[2]) };
        let article = input.iter().find(|r| r.id == CTRL_DAYI_ARTICLE).unwrap();
        assert!(row_visible(article.visible_modes, MODE_DAYI));
        assert!(!row_visible(article.visible_modes, MODE_ARRAY));
        assert!(!row_visible(article.visible_modes, MODE_PHONETIC));
        assert!(!row_visible(article.visible_modes, MODE_GENERIC));
        let beep = unsafe { rows(&cards[1]) }
            .iter()
            .find(|r| r.id == CTRL_DO_BEEP_CANDI)
            .unwrap();
        assert_eq!(beep.visible_modes, MODE_DAYI);
        let scope = input.iter().find(|r| r.id == CTRL_ARRAY_SCOPE).unwrap();
        assert!(row_visible(scope.visible_modes, MODE_ARRAY));
        assert!(!row_visible(scope.visible_modes, MODE_DAYI));
        let kb = input.iter().find(|r| r.id == CTRL_PHONETIC_KB).unwrap();
        assert_eq!(kb.visible_modes, MODE_PHONETIC);
        let color = appearance.iter().find(|r| r.id == CTRL_COLOR_MODE).unwrap();
        let beep_all = unsafe { rows(&cards[1]) }
            .iter()
            .find(|r| r.id == CTRL_DO_BEEP)
            .unwrap();
        for mode in [MODE_DAYI, MODE_ARRAY, MODE_PHONETIC, MODE_GENERIC] {
            assert!(row_visible(color.visible_modes, mode));
            assert!(row_visible(beep_all.visible_modes, mode));
        }
        let max_codes = input.iter().find(|r| r.id == CTRL_MAX_CODES).unwrap();
        assert_eq!(max_codes.id, 18);
        assert_eq!(max_codes.row_type, TY_EDIT);
        assert_eq!(max_codes.icon_glyph, 0xE8C1);
        let sentence = input.iter().find(|r| r.id == CTRL_SENTENCE_MODE).unwrap();
        assert_eq!(
            utf16(sentence.description),
            "連續打字不選字，依上下文自動組成句子，按 Enter 送出"
        );
        let charset = input.iter().find(|r| r.id == CTRL_CHARSET_SCOPE).unwrap();
        assert_eq!(charset.visible_modes, MODE_NON_ARRAY);
        let custom = unsafe { rows(&cards[3]) };
        assert!(custom[1].card_break_before);
        assert_eq!(custom[1].action, ACT_EXPORT);
        assert_eq!(utf16(custom[0].description), "每行格式：輸入碼 空白 詞彙");
    }

    #[test]
    fn load_page_and_sidebar_match_cpp() {
        let cards = load_cards();
        assert_eq!(cards.len(), 6);
        assert_eq!(utf16(cards[0].title), "大易主碼表");
        assert_eq!(cards[2].action, ACT_EXPAND);
        assert_eq!(cards[2].row_count, 7);
        assert_eq!(utf16(cards[2].description), "特碼、簡碼、ExtB/CD/E~J、行列40、行列詞庫");
        let expand = unsafe { rows(&cards[2]) };
        assert_eq!(expand[4].id, CTRL_LOAD_ARRAY_EXT_E_TO_J);
        assert_eq!(utf16(expand[4].label), "Unicode Ext. E-J 碼表");
        assert_eq!(expand[4].expand_parent, CTRL_LOAD_ARRAY_EXPAND);
        assert_eq!(expand[4].icon_glyph, 0xE82D);
        let dayi = unsafe { rows(&cards[0]) };
        assert_eq!(dayi[0].id, CTRL_LOAD_DAYI_MAIN);
        assert_eq!(utf16(dayi[0].label), "載入");
        assert!(!dayi[0].card_break_before);
        assert_eq!(dayi[0].icon_glyph, 0);

        let items = sidebar_items();
        assert_eq!(items.len(), 6);
        assert_eq!(utf16(items[0].label), "大易輸入法");
        assert_eq!(items[0].icon_resource_id, IDI_DAYI);
        assert_eq!(items[1].icon_resource_id, IDI_ARRAY);
        assert_eq!(items[2].icon_resource_id, IDI_PHONETIC);
        assert_eq!(utf16(items[2].label), "傳統注音輸入法");
        assert_eq!(items[3].icon_resource_id, IDI_GENERIC);
        assert!(items[4].label.is_null());
        assert_eq!(items[4].icon_resource_id, -1);
        assert_eq!(utf16(items[5].label), "自定碼表");
        assert_eq!(items[5].icon_resource_id, IDI_DIMESETTINGS);
    }
}
