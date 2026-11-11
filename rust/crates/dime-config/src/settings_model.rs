//! Window-free `SettingsModel` (phase 5b-1).
//!
//! These functions match `src/DIMESettings/SettingsController.cpp`.
//! `LoadFromConfig` / `ApplyToConfig` copy the plain fields. The six effective
//! colors are passed in (`colors[]`, not the stored palettes) and are not
//! written back. `reverseConversionIndex` is always 0. The composition engine
//! and custom-table paths stay in C++. Decoding an already-read text file
//! lives in `text_file`. Page layout tables live in `page_layout`. Geometry
//! and theme colors live in `geometry`. Quirks kept on purpose:
//! `CTRL_LOAD_ARRAY_EXT_E_TO_J` maps to `Array-Ext-EF.cin` (not EFG),
//! `CTRL_LOAD_ARRAY_EXPAND` has no suffix, an unknown sidebar index is Dayi,
//! and the no-engine character check is `towupper(c)` in `32..=128`.

use crate::values::{face_from, ConfigValues, ImeMode};

/// `SM_MODE_*` in `SettingsPageLayout.h`.
pub const MODE_DAYI: i32 = 0x1;
pub const MODE_ARRAY: i32 = 0x2;
pub const MODE_PHONETIC: i32 = 0x4;
pub const MODE_GENERIC: i32 = 0x8;

/// `MAX_KEY_LENGTH`, `MAX_TABLE_LINE_LENGTH`, `MAX_RADICAL` in `Define.h`.
const MAX_KEY_LENGTH: usize = 64;
const MAX_TABLE_LINE_LENGTH: usize = 1024;
const MAX_RADICAL: u16 = 96;

/// `SettingsControlId` values that these functions switch on.
pub const CTRL_SHOW_NOTIFY: i32 = 11;
pub const CTRL_CHARSET_SCOPE: i32 = 14;
pub const CTRL_NUMERIC_PAD: i32 = 15;
pub const CTRL_SHIFT_ENGLISH_DIGIT_MODE: i32 = 16;
pub const CTRL_PHONETIC_KB: i32 = 17;
pub const CTRL_MAX_CODES: i32 = 18;
pub const CTRL_ARROW_KEY_SW_PAGES: i32 = 19;
pub const CTRL_SPACE_AS_PAGEDOWN: i32 = 20;
pub const CTRL_SPACE_AS_FIRST_CAND: i32 = 21;
pub const CTRL_AUTO_COMPOSE: i32 = 22;
pub const CTRL_CLEAR_ON_BEEP: i32 = 23;
pub const CTRL_MAKE_PHRASE: i32 = 24;
pub const CTRL_CUSTOM_TABLE_PRIORITY: i32 = 25;
pub const CTRL_DO_BEEP: i32 = 26;
pub const CTRL_DO_BEEP_NOTIFY: i32 = 27;
pub const CTRL_DO_BEEP_CANDI: i32 = 28;
pub const CTRL_IME_SHIFT_MODE: i32 = 29;
pub const CTRL_DOUBLE_SINGLE_BYTE: i32 = 30;
pub const CTRL_KEYBOARD_OPEN_CLOSE: i32 = 31;
pub const CTRL_OUTPUT_CHT_CHS: i32 = 32;
pub const CTRL_DAYI_ARTICLE: i32 = 34;
pub const CTRL_SENTENCE_MODE: i32 = 35;
pub const CTRL_DAYI_SELKEY_MODE: i32 = 36;
pub const CTRL_ARRAY_FORCE_SP: i32 = 37;
pub const CTRL_ARRAY_NOTIFY_SP: i32 = 38;
pub const CTRL_ARRAY_SINGLE_QUOTE: i32 = 39;

pub const CTRL_LOAD_DAYI_MAIN: i32 = 300;
pub const CTRL_LOAD_ARRAY_MAIN: i32 = 301;
pub const CTRL_LOAD_ARRAY_EXPAND: i32 = 302;
pub const CTRL_LOAD_ARRAY_SP: i32 = 303;
pub const CTRL_LOAD_ARRAY_SC: i32 = 304;
pub const CTRL_LOAD_ARRAY_EXT_B: i32 = 305;
pub const CTRL_LOAD_ARRAY_EXT_CD: i32 = 306;
pub const CTRL_LOAD_ARRAY_EXT_E_TO_J: i32 = 307;
pub const CTRL_LOAD_ARRAY40: i32 = 308;
pub const CTRL_LOAD_ARRAY_PHRASE: i32 = 309;
pub const CTRL_LOAD_PHONETIC_MAIN: i32 = 310;
pub const CTRL_LOAD_GENERIC_MAIN: i32 = 311;
pub const CTRL_LOAD_ASSOC_PHRASE: i32 = 312;

/// `SettingsSnapshot`. Field order and widths match the C++ struct (`BOOL` and
/// `COLORREF` are 4 bytes, `fontFaceName` is `wchar_t[32]`).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SettingsSnapshot {
    pub font_size: u32,
    pub font_weight: u32,
    pub font_italic: i32,
    pub font_face_name: [u16; 32],
    pub color_mode: i32,
    pub item_color: u32,
    pub phrase_color: u32,
    pub number_color: u32,
    pub item_bg_color: u32,
    pub selected_color: u32,
    pub selected_bg_color: u32,
    pub auto_compose: i32,
    pub clear_on_beep: i32,
    pub do_beep: i32,
    pub do_beep_notify: i32,
    pub do_beep_on_candi: i32,
    pub make_phrase: i32,
    pub custom_table_priority: i32,
    pub arrow_key_sw_pages: i32,
    pub space_as_page_down: i32,
    pub space_as_first_cand_selkey: i32,
    pub show_notify_desktop: i32,
    pub array_notify_sp: i32,
    pub array_force_sp: i32,
    pub array_single_quote_custom_phrase: i32,
    pub dayi_article_mode: i32,
    pub sentence_mode: i32,
    pub do_han_convert: i32,
    pub activated_keyboard_mode: i32,
    pub big5_filter: i32,
    pub max_codes: u32,
    pub array_scope: i32,
    pub numeric_pad: i32,
    pub phonetic_keyboard_layout: i32,
    pub dayi_selkey_mode: i32,
    pub ime_shift_mode: i32,
    pub double_single_byte_mode: i32,
    pub shift_english_digit_mode: i32,
    pub reverse_conversion_index: i32,
}

/// `SettingsModel::LineError` codes written across the FFI. `None` (0) is not
/// stored in an entry.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum LineError {
    Format = 1,
    KeyTooLong = 2,
    InvalidChar = 3,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LineErrorEntry {
    pub error: LineError,
    pub start: i32,
    pub len: i32,
}

const fn ascii16<const N: usize>(bytes: &[u8; N]) -> [u16; N] {
    let mut out = [0u16; N];
    let mut i = 0;
    while i < N {
        out[i] = bytes[i] as u16;
        i += 1;
    }
    out
}

static DAYI_NAME: [u16; 5] = ascii16(b"dayi\0");
static ARRAY_NAME: [u16; 6] = ascii16(b"array\0");
static PHONETIC_NAME: [u16; 9] = ascii16(b"phonetic\0");
static GENERIC_NAME: [u16; 8] = ascii16(b"generic\0");

static SUF_DAYI: [u16; 15] = ascii16(b"\\DIME\\Dayi.cin\0");
static SUF_ARRAY: [u16; 16] = ascii16(b"\\DIME\\Array.cin\0");
static SUF_PHONE: [u16; 16] = ascii16(b"\\DIME\\Phone.cin\0");
static SUF_GENERIC: [u16; 18] = ascii16(b"\\DIME\\Generic.cin\0");
static SUF_PHRASE: [u16; 17] = ascii16(b"\\DIME\\Phrase.cin\0");
static SUF_ARRAY_SP: [u16; 24] = ascii16(b"\\DIME\\Array-special.cin\0");
static SUF_ARRAY_SC: [u16; 26] = ascii16(b"\\DIME\\Array-shortcode.cin\0");
static SUF_EXT_B: [u16; 22] = ascii16(b"\\DIME\\Array-Ext-B.cin\0");
static SUF_EXT_CD: [u16; 23] = ascii16(b"\\DIME\\Array-Ext-CD.cin\0");
static SUF_EXT_EF: [u16; 23] = ascii16(b"\\DIME\\Array-Ext-EF.cin\0");
static SUF_ARRAY40: [u16; 18] = ascii16(b"\\DIME\\Array40.cin\0");
static SUF_ARRAY_PHRASE: [u16; 23] = ascii16(b"\\DIME\\Array-Phrase.cin\0");

// `iswspace` / `towupper`. On Windows these are the CRT (same calls as the
// C++ validator). Elsewhere they follow the C-locale tables already used by
// the config parser, so the crate still builds off Windows.
#[cfg(windows)]
extern "C" {
    fn iswspace(c: u16) -> i32;
    fn towupper(c: u16) -> u16;
}

#[cfg(windows)]
fn is_wspace(c: u16) -> bool {
    // SAFETY: `iswspace` takes a `wint_t` (unsigned short on MSVC).
    unsafe { iswspace(c) != 0 }
}

#[cfg(not(windows))]
fn is_wspace(c: u16) -> bool {
    crate::num::is_space(c)
}

#[cfg(windows)]
fn tow_upper(c: u16) -> u16 {
    // SAFETY: `towupper` takes a `wint_t` and returns one.
    unsafe { towupper(c) }
}

#[cfg(not(windows))]
fn tow_upper(c: u16) -> u16 {
    dime_core::text::towupper(c)
}

fn fold_eq(a: &[u16], b: &[u16]) -> bool {
    a.len() == b.len() && a.iter().zip(b.iter()).all(|(&x, &y)| tow_upper(x) == tow_upper(y))
}

/// Returned slices are NUL-terminated, so the pointer is a C string.
pub fn ime_mode_to_string(mode: ImeMode) -> Option<&'static [u16]> {
    match mode {
        ImeMode::Dayi => Some(&DAYI_NAME),
        ImeMode::Array => Some(&ARRAY_NAME),
        ImeMode::Phonetic => Some(&PHONETIC_NAME),
        ImeMode::Generic => Some(&GENERIC_NAME),
        ImeMode::None => None,
    }
}

/// `None` (a null pointer) and any unknown name are `ImeMode::None`.
/// Comparison matches `_wcsicmp` for these ASCII names.
pub fn string_to_ime_mode(s: Option<&[u16]>) -> ImeMode {
    let Some(s) = s else { return ImeMode::None };
    if fold_eq(s, &DAYI_NAME[..4]) {
        ImeMode::Dayi
    } else if fold_eq(s, &ARRAY_NAME[..5]) {
        ImeMode::Array
    } else if fold_eq(s, &PHONETIC_NAME[..8]) {
        ImeMode::Phonetic
    } else if fold_eq(s, &GENERIC_NAME[..7]) {
        ImeMode::Generic
    } else {
        ImeMode::None
    }
}

pub fn mode_bitmask(mode: ImeMode) -> i32 {
    match mode {
        ImeMode::Dayi => MODE_DAYI,
        ImeMode::Array => MODE_ARRAY,
        ImeMode::Phonetic => MODE_PHONETIC,
        ImeMode::Generic => MODE_GENERIC,
        ImeMode::None => 0,
    }
}

pub fn sidebar_hit_test(y: i32, item_height: i32, item_count: i32) -> i32 {
    if y < 0 || item_height <= 0 || item_count <= 0 {
        return -1;
    }
    let index = y / item_height;
    if index >= item_count {
        -1
    } else {
        index
    }
}

pub fn compute_scroll_range(content_height: i32, viewport_height: i32) -> i32 {
    if content_height <= viewport_height {
        return 0;
    }
    // MSVC wraps signed overflow; debug Rust would panic on `sub`.
    content_height.wrapping_sub(viewport_height)
}

pub fn clamp_scroll_pos(pos: i32, mut max_scroll: i32) -> i32 {
    if pos < 0 {
        return 0;
    }
    if max_scroll < 0 {
        max_scroll = 0;
    }
    if pos > max_scroll {
        max_scroll
    } else {
        pos
    }
}

/// Unknown index is Dayi, matching the C++ `default`.
pub fn index_to_mode(index: i32) -> ImeMode {
    match index {
        0 => ImeMode::Dayi,
        1 => ImeMode::Array,
        2 => ImeMode::Phonetic,
        3 => ImeMode::Generic,
        _ => ImeMode::Dayi,
    }
}

pub fn mode_to_index(mode: ImeMode) -> i32 {
    match mode {
        ImeMode::Dayi => 0,
        ImeMode::Array => 1,
        ImeMode::Phonetic => 2,
        ImeMode::Generic => 3,
        ImeMode::None => 0,
    }
}

fn set_bool(field: &mut i32, on: bool) {
    *field = if on { 1 } else { 0 };
}

pub fn sync_toggle(s: &mut SettingsSnapshot, id: i32, on: bool) {
    match id {
        CTRL_DO_BEEP => set_bool(&mut s.do_beep, on),
        CTRL_DO_BEEP_NOTIFY => set_bool(&mut s.do_beep_notify, on),
        CTRL_DO_BEEP_CANDI => set_bool(&mut s.do_beep_on_candi, on),
        CTRL_SHOW_NOTIFY => set_bool(&mut s.show_notify_desktop, on),
        CTRL_AUTO_COMPOSE => set_bool(&mut s.auto_compose, on),
        CTRL_CLEAR_ON_BEEP => set_bool(&mut s.clear_on_beep, on),
        CTRL_MAKE_PHRASE => set_bool(&mut s.make_phrase, on),
        CTRL_CUSTOM_TABLE_PRIORITY => set_bool(&mut s.custom_table_priority, on),
        CTRL_ARROW_KEY_SW_PAGES => set_bool(&mut s.arrow_key_sw_pages, on),
        CTRL_SPACE_AS_PAGEDOWN => set_bool(&mut s.space_as_page_down, on),
        CTRL_SPACE_AS_FIRST_CAND => set_bool(&mut s.space_as_first_cand_selkey, on),
        CTRL_KEYBOARD_OPEN_CLOSE => set_bool(&mut s.activated_keyboard_mode, on),
        CTRL_OUTPUT_CHT_CHS => set_bool(&mut s.do_han_convert, on),
        CTRL_DAYI_ARTICLE => set_bool(&mut s.dayi_article_mode, on),
        CTRL_SENTENCE_MODE => set_bool(&mut s.sentence_mode, on),
        CTRL_ARRAY_FORCE_SP => set_bool(&mut s.array_force_sp, on),
        CTRL_ARRAY_NOTIFY_SP => set_bool(&mut s.array_notify_sp, on),
        CTRL_ARRAY_SINGLE_QUOTE => set_bool(&mut s.array_single_quote_custom_phrase, on),
        _ => {}
    }
}

pub fn sync_combo(s: &mut SettingsSnapshot, id: i32, sel: i32) {
    match id {
        CTRL_CHARSET_SCOPE => set_bool(&mut s.big5_filter, sel == 1),
        CTRL_NUMERIC_PAD => s.numeric_pad = sel,
        CTRL_PHONETIC_KB => s.phonetic_keyboard_layout = sel,
        CTRL_DAYI_SELKEY_MODE => s.dayi_selkey_mode = sel,
        CTRL_IME_SHIFT_MODE => s.ime_shift_mode = sel,
        CTRL_DOUBLE_SINGLE_BYTE => s.double_single_byte_mode = sel,
        CTRL_SHIFT_ENGLISH_DIGIT_MODE => s.shift_english_digit_mode = sel,
        CTRL_KEYBOARD_OPEN_CLOSE => set_bool(&mut s.activated_keyboard_mode, sel == 0),
        CTRL_OUTPUT_CHT_CHS => set_bool(&mut s.do_han_convert, sel == 1),
        _ => {}
    }
}

/// `_wtoi`, then accept `1..=20`. A null string is `None` and does not write.
/// A negative parse becomes a large `UINT` and is rejected, same as C++.
pub fn parse_max_codes(text: Option<&[u16]>) -> Option<u32> {
    let text = text?;
    let val = crate::num::wtoi(text) as u32;
    if (1..=20).contains(&val) {
        Some(val)
    } else {
        None
    }
}

/// NUL-terminated suffix, or `None` (including `CTRL_LOAD_ARRAY_EXPAND`).
pub fn load_table_suffix(id: i32) -> Option<&'static [u16]> {
    match id {
        CTRL_LOAD_DAYI_MAIN => Some(&SUF_DAYI),
        CTRL_LOAD_ARRAY_MAIN => Some(&SUF_ARRAY),
        CTRL_LOAD_PHONETIC_MAIN => Some(&SUF_PHONE),
        CTRL_LOAD_GENERIC_MAIN => Some(&SUF_GENERIC),
        CTRL_LOAD_ASSOC_PHRASE => Some(&SUF_PHRASE),
        CTRL_LOAD_ARRAY_SP => Some(&SUF_ARRAY_SP),
        CTRL_LOAD_ARRAY_SC => Some(&SUF_ARRAY_SC),
        CTRL_LOAD_ARRAY_EXT_B => Some(&SUF_EXT_B),
        CTRL_LOAD_ARRAY_EXT_CD => Some(&SUF_EXT_CD),
        CTRL_LOAD_ARRAY_EXT_E_TO_J => Some(&SUF_EXT_EF),
        CTRL_LOAD_ARRAY40 => Some(&SUF_ARRAY40),
        CTRL_LOAD_ARRAY_PHRASE => Some(&SUF_ARRAY_PHRASE),
        _ => None,
    }
}

fn entry(error: LineError, start: usize, len: usize) -> LineErrorEntry {
    LineErrorEntry {
        error,
        start: start as i32,
        len: len as i32,
    }
}

/// Three-level custom-table line check. `char_ok == None` is the no-engine
/// fallback (`towupper` in `32..=32+MAX_RADICAL`). An empty result is a valid
/// line. Phonetic keys must be printable ASCII except `*` and `?`.
pub fn validate_line(
    line: &[u16],
    mode: ImeMode,
    max_codes: u32,
    mut char_ok: Option<&mut dyn FnMut(u16) -> bool>,
) -> Vec<LineErrorEntry> {
    if line.is_empty() {
        return Vec::new();
    }
    let len = line.len();
    let mut s = 0;
    while s < len && is_wspace(line[s]) {
        s += 1;
    }
    let mut e = len;
    while e > s && is_wspace(line[e - 1]) {
        e -= 1;
    }
    if s >= e {
        return Vec::new();
    }
    let span = e - s;
    if span > MAX_TABLE_LINE_LENGTH {
        return vec![entry(LineError::Format, s, span)];
    }

    let mut key_end = s;
    while key_end < e && !is_wspace(line[key_end]) {
        key_end += 1;
    }
    if key_end == s || key_end == e {
        return vec![entry(LineError::Format, s, span)];
    }
    let mut val_start = key_end;
    while val_start < e && is_wspace(line[val_start]) {
        val_start += 1;
    }
    if val_start == e {
        return vec![entry(LineError::Format, s, span)];
    }
    let mut val_end = val_start;
    while val_end < e && !is_wspace(line[val_end]) {
        val_end += 1;
    }
    let mut tail = val_end;
    while tail < e && is_wspace(line[tail]) {
        tail += 1;
    }
    if tail != e {
        return vec![entry(LineError::Format, s, span)];
    }

    let key_len = key_end - s;
    let max_key = if mode == ImeMode::Phonetic {
        MAX_KEY_LENGTH
    } else {
        max_codes as usize
    };
    if key_len > max_key {
        return vec![entry(LineError::KeyTooLong, s, key_len)];
    }

    let mut errors = Vec::new();
    if mode == ImeMode::Phonetic {
        for (i, &c) in line[s..key_end].iter().enumerate() {
            let printable = (b'!' as u16..=b'~' as u16).contains(&c);
            if !printable || c == b'*' as u16 || c == b'?' as u16 {
                errors.push(entry(LineError::InvalidChar, s + i, 1));
            }
        }
    } else {
        let all_ascii_letters = line[s..key_end].iter().all(|&c| {
            let cu = tow_upper(c);
            (b'A' as u16..=b'Z' as u16).contains(&cu)
        });
        if !all_ascii_letters {
            for (i, &c) in line[s..key_end].iter().enumerate() {
                let ok = if let Some(check) = char_ok.as_mut() {
                    check(c)
                } else {
                    let cu = u32::from(tow_upper(c));
                    (32..=32 + u32::from(MAX_RADICAL)).contains(&cu)
                };
                if !ok {
                    errors.push(entry(LineError::InvalidChar, s + i, 1));
                }
            }
        }
    }
    errors
}

/// `SettingsModel::LoadFromConfig`. `effective_colors` is `CConfig::colors[]`
/// in order: item, selected, item background, phrase, number, selected
/// background. Stored palettes are not read. `reverse_conversion_index` is 0.
pub fn load_snapshot(values: &ConfigValues, effective_colors: &[u32; 6]) -> SettingsSnapshot {
    SettingsSnapshot {
        font_size: values.font_size,
        font_weight: values.font_weight,
        font_italic: values.font_italic,
        font_face_name: face_from(&values.font_face_name),
        color_mode: values.color_mode,
        item_color: effective_colors[0],
        phrase_color: effective_colors[3],
        number_color: effective_colors[4],
        item_bg_color: effective_colors[2],
        selected_color: effective_colors[1],
        selected_bg_color: effective_colors[5],
        auto_compose: values.auto_compose,
        clear_on_beep: values.clear_on_beep,
        do_beep: values.do_beep,
        do_beep_notify: values.do_beep_notify,
        do_beep_on_candi: values.do_beep_on_candi,
        make_phrase: values.make_phrase,
        custom_table_priority: values.custom_table_priority,
        arrow_key_sw_pages: values.arrow_key_sw_pages,
        space_as_page_down: values.space_as_page_down,
        space_as_first_cand_selkey: values.space_as_first_cand_selkey,
        show_notify_desktop: values.show_notify_desktop,
        array_notify_sp: values.array_notify_sp,
        array_force_sp: values.array_force_sp,
        array_single_quote_custom_phrase: values.array_single_quote_custom_phrase,
        dayi_article_mode: values.dayi_article_mode,
        sentence_mode: values.sentence_mode,
        do_han_convert: values.do_han_convert,
        activated_keyboard_mode: values.activated_keyboard_mode,
        big5_filter: values.big5_filter,
        max_codes: values.max_codes,
        array_scope: values.array_scope,
        numeric_pad: values.numeric_pad,
        phonetic_keyboard_layout: values.phonetic_keyboard_layout,
        dayi_selkey_mode: values.dayi_selkey_mode,
        ime_shift_mode: values.ime_shift_mode,
        double_single_byte_mode: values.double_single_byte_mode,
        shift_english_digit_mode: values.shift_english_digit_mode,
        reverse_conversion_index: 0,
    }
}

/// `SettingsModel::ApplyToConfig`. Palette colors and the fields Apply does
/// not assign (reverse conversion, reload flags) stay as they were. The font
/// face is truncated like `StringCchCopy` into `LF_FACESIZE`.
pub fn apply_snapshot(values: &mut ConfigValues, snap: &SettingsSnapshot) {
    values.font_size = snap.font_size;
    values.font_weight = snap.font_weight;
    values.font_italic = snap.font_italic;
    values.font_face_name = face_from(&snap.font_face_name);
    values.color_mode = snap.color_mode;
    values.auto_compose = snap.auto_compose;
    values.clear_on_beep = snap.clear_on_beep;
    values.do_beep = snap.do_beep;
    values.do_beep_notify = snap.do_beep_notify;
    values.do_beep_on_candi = snap.do_beep_on_candi;
    values.make_phrase = snap.make_phrase;
    values.custom_table_priority = snap.custom_table_priority;
    values.arrow_key_sw_pages = snap.arrow_key_sw_pages;
    values.space_as_page_down = snap.space_as_page_down;
    values.space_as_first_cand_selkey = snap.space_as_first_cand_selkey;
    values.show_notify_desktop = snap.show_notify_desktop;
    values.array_notify_sp = snap.array_notify_sp;
    values.array_force_sp = snap.array_force_sp;
    values.array_single_quote_custom_phrase = snap.array_single_quote_custom_phrase;
    values.dayi_article_mode = snap.dayi_article_mode;
    values.sentence_mode = snap.sentence_mode;
    values.do_han_convert = snap.do_han_convert;
    values.activated_keyboard_mode = snap.activated_keyboard_mode;
    values.big5_filter = snap.big5_filter;
    values.max_codes = snap.max_codes;
    values.array_scope = snap.array_scope;
    values.numeric_pad = snap.numeric_pad;
    values.phonetic_keyboard_layout = snap.phonetic_keyboard_layout;
    values.dayi_selkey_mode = snap.dayi_selkey_mode;
    values.ime_shift_mode = snap.ime_shift_mode;
    values.double_single_byte_mode = snap.double_single_byte_mode;
    values.shift_english_digit_mode = snap.shift_english_digit_mode;
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_core::text::w;

    fn nt(got: &[u16]) -> String {
        String::from_utf16_lossy(got).trim_end_matches('\0').to_string()
    }

    #[test]
    fn snapshot_layout_matches_cpp() {
        use std::mem::{align_of, offset_of, size_of};
        assert_eq!(size_of::<SettingsSnapshot>(), 216);
        assert_eq!(align_of::<SettingsSnapshot>(), 4);
        assert_eq!(offset_of!(SettingsSnapshot, font_face_name), 12);
        assert_eq!(offset_of!(SettingsSnapshot, color_mode), 76);
        assert_eq!(offset_of!(SettingsSnapshot, auto_compose), 104);
        assert_eq!(offset_of!(SettingsSnapshot, show_notify_desktop), 144);
        assert_eq!(offset_of!(SettingsSnapshot, big5_filter), 176);
        assert_eq!(offset_of!(SettingsSnapshot, max_codes), 180);
        assert_eq!(offset_of!(SettingsSnapshot, reverse_conversion_index), 212);
    }

    #[test]
    fn bitmask_hit_and_scroll() {
        assert_eq!(mode_bitmask(ImeMode::Dayi), 1);
        assert_eq!(mode_bitmask(ImeMode::Array), 2);
        assert_eq!(mode_bitmask(ImeMode::Phonetic), 4);
        assert_eq!(mode_bitmask(ImeMode::Generic), 8);
        assert_eq!(mode_bitmask(ImeMode::None), 0);

        assert_eq!(sidebar_hit_test(0, 40, 8), 0);
        assert_eq!(sidebar_hit_test(39, 40, 8), 0);
        assert_eq!(sidebar_hit_test(40, 40, 8), 1);
        assert_eq!(sidebar_hit_test(280, 40, 8), 7);
        assert_eq!(sidebar_hit_test(-1, 40, 8), -1);
        assert_eq!(sidebar_hit_test(320, 40, 8), -1);
        assert_eq!(sidebar_hit_test(0, 40, 0), -1);
        assert_eq!(sidebar_hit_test(0, 0, 8), -1);
        assert_eq!(sidebar_hit_test(0, -1, 8), -1);

        assert_eq!(compute_scroll_range(1000, 600), 400);
        assert_eq!(compute_scroll_range(500, 600), 0);
        assert_eq!(compute_scroll_range(600, 600), 0);
        assert_eq!(compute_scroll_range(601, 600), 1);

        assert_eq!(clamp_scroll_pos(-10, 400), 0);
        assert_eq!(clamp_scroll_pos(500, 400), 400);
        assert_eq!(clamp_scroll_pos(200, 400), 200);
        assert_eq!(clamp_scroll_pos(100, -5), 0);
    }

    #[test]
    fn mode_strings_and_index() {
        assert_eq!(nt(ime_mode_to_string(ImeMode::Dayi).unwrap()), "dayi");
        assert_eq!(nt(ime_mode_to_string(ImeMode::Array).unwrap()), "array");
        assert_eq!(nt(ime_mode_to_string(ImeMode::Phonetic).unwrap()), "phonetic");
        assert_eq!(nt(ime_mode_to_string(ImeMode::Generic).unwrap()), "generic");
        assert!(ime_mode_to_string(ImeMode::None).is_none());
        assert_eq!(ime_mode_to_string(ImeMode::Dayi).unwrap().last(), Some(&0));

        assert_eq!(string_to_ime_mode(Some(&w("DAYI"))), ImeMode::Dayi);
        assert_eq!(string_to_ime_mode(Some(&w("Array"))), ImeMode::Array);
        assert_eq!(string_to_ime_mode(Some(&w("phonetic"))), ImeMode::Phonetic);
        assert_eq!(string_to_ime_mode(Some(&w("GENERIC"))), ImeMode::Generic);
        assert_eq!(string_to_ime_mode(Some(&w("dayiX"))), ImeMode::None);
        assert_eq!(string_to_ime_mode(Some(&w(""))), ImeMode::None);
        assert_eq!(string_to_ime_mode(None), ImeMode::None);

        assert_eq!(index_to_mode(0), ImeMode::Dayi);
        assert_eq!(index_to_mode(3), ImeMode::Generic);
        assert_eq!(index_to_mode(9), ImeMode::Dayi);
        assert_eq!(index_to_mode(-1), ImeMode::Dayi);
        assert_eq!(mode_to_index(ImeMode::Phonetic), 2);
        assert_eq!(mode_to_index(ImeMode::None), 0);
    }

    #[test]
    fn sync_and_max_codes_and_suffix() {
        let mut s = SettingsSnapshot::default();
        sync_toggle(&mut s, CTRL_DO_BEEP, true);
        assert_eq!(s.do_beep, 1);
        assert_eq!(s.sentence_mode, 0);
        sync_toggle(&mut s, CTRL_DO_BEEP, false);
        assert_eq!(s.do_beep, 0);
        sync_toggle(&mut s, CTRL_KEYBOARD_OPEN_CLOSE, true);
        assert_eq!(s.activated_keyboard_mode, 1);
        sync_toggle(&mut s, CTRL_SENTENCE_MODE, true);
        assert_eq!(s.sentence_mode, 1);
        // CTRL_ARRAY_SCOPE (13) is not a toggle; the snapshot stays put.
        sync_toggle(&mut s, 13, true);
        assert_eq!(
            s,
            SettingsSnapshot {
                activated_keyboard_mode: 1,
                sentence_mode: 1,
                ..SettingsSnapshot::default()
            }
        );

        sync_combo(&mut s, CTRL_CHARSET_SCOPE, 1);
        assert_eq!(s.big5_filter, 1);
        sync_combo(&mut s, CTRL_CHARSET_SCOPE, 0);
        assert_eq!(s.big5_filter, 0);
        sync_combo(&mut s, CTRL_NUMERIC_PAD, 7);
        assert_eq!(s.numeric_pad, 7);
        sync_combo(&mut s, CTRL_KEYBOARD_OPEN_CLOSE, 0);
        assert_eq!(s.activated_keyboard_mode, 1);
        sync_combo(&mut s, CTRL_KEYBOARD_OPEN_CLOSE, 2);
        assert_eq!(s.activated_keyboard_mode, 0);
        sync_combo(&mut s, CTRL_OUTPUT_CHT_CHS, 1);
        assert_eq!(s.do_han_convert, 1);
        sync_combo(&mut s, CTRL_OUTPUT_CHT_CHS, 0);
        assert_eq!(s.do_han_convert, 0);
        sync_combo(&mut s, CTRL_PHONETIC_KB, 3);
        sync_combo(&mut s, CTRL_DAYI_SELKEY_MODE, 4);
        sync_combo(&mut s, CTRL_IME_SHIFT_MODE, 5);
        sync_combo(&mut s, CTRL_DOUBLE_SINGLE_BYTE, 6);
        sync_combo(&mut s, CTRL_SHIFT_ENGLISH_DIGIT_MODE, 2);
        assert_eq!(s.phonetic_keyboard_layout, 3);
        assert_eq!(s.dayi_selkey_mode, 4);
        assert_eq!(s.ime_shift_mode, 5);
        assert_eq!(s.double_single_byte_mode, 6);
        assert_eq!(s.shift_english_digit_mode, 2);

        assert_eq!(parse_max_codes(None), None);
        assert_eq!(parse_max_codes(Some(&w("0"))), None);
        assert_eq!(parse_max_codes(Some(&w("1"))), Some(1));
        assert_eq!(parse_max_codes(Some(&w("20"))), Some(20));
        assert_eq!(parse_max_codes(Some(&w("21"))), None);
        assert_eq!(parse_max_codes(Some(&w("  5"))), Some(5));
        assert_eq!(parse_max_codes(Some(&w("-3"))), None);
        assert_eq!(parse_max_codes(Some(&w("２０"))), Some(20));
        assert_eq!(parse_max_codes(Some(&w("abc"))), None);
        assert_eq!(parse_max_codes(Some(&w("12xyz"))), Some(12));

        assert_eq!(nt(load_table_suffix(CTRL_LOAD_DAYI_MAIN).unwrap()), "\\DIME\\Dayi.cin");
        assert_eq!(
            nt(load_table_suffix(CTRL_LOAD_ARRAY_EXT_E_TO_J).unwrap()),
            "\\DIME\\Array-Ext-EF.cin"
        );
        assert_eq!(
            nt(load_table_suffix(CTRL_LOAD_ARRAY_PHRASE).unwrap()),
            "\\DIME\\Array-Phrase.cin"
        );
        assert_eq!(
            nt(load_table_suffix(CTRL_LOAD_PHONETIC_MAIN).unwrap()),
            "\\DIME\\Phone.cin"
        );
        assert!(load_table_suffix(CTRL_LOAD_ARRAY_EXPAND).is_none());
        assert!(load_table_suffix(0).is_none());
        assert_eq!(load_table_suffix(CTRL_LOAD_ARRAY_SP).unwrap().last(), Some(&0));
    }

    #[test]
    fn validate_line_matches_cpp_cases() {
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&w("roc 語"), ImeMode::Dayi, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let fmt = validate_line(&w("roc 語 extra"), ImeMode::Dayi, 4, none);
        assert_eq!(
            fmt,
            vec![entry(LineError::Format, 0, "roc 語 extra".encode_utf16().count())]
        );

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let long = validate_line(&w("LONGKEY12345 語"), ImeMode::Dayi, 4, none);
        assert_eq!(long, vec![entry(LineError::KeyTooLong, 0, 12)]);

        let huge = vec![b'a' as u16; 1025];
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert_eq!(
            validate_line(&huge, ImeMode::Dayi, 4, none),
            vec![entry(LineError::Format, 0, 1025)]
        );

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert_eq!(
            validate_line(&w("fff"), ImeMode::Dayi, 4, none),
            vec![entry(LineError::Format, 0, 3)]
        );

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let phonetic = validate_line(&w("中 abc"), ImeMode::Phonetic, 64, none);
        assert_eq!(phonetic, vec![entry(LineError::InvalidChar, 0, 1)]);

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&[], ImeMode::Dayi, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&w("   \t"), ImeMode::Dayi, 4, none).is_empty());

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let lead = validate_line(&w("  roc extra x"), ImeMode::Dayi, 4, none);
        assert_eq!(lead[0].error, LineError::Format);
        assert_eq!(lead[0].start, 2);

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&w("ab\t字"), ImeMode::Dayi, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&w("abcd 字"), ImeMode::Dayi, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert_eq!(
            validate_line(&w("abcde 字"), ImeMode::Dayi, 4, none),
            vec![entry(LineError::KeyTooLong, 0, 5)]
        );

        let key64 = format!("{} x", "a".repeat(64));
        let key65 = format!("{} x", "a".repeat(65));
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert!(validate_line(&w(&key64), ImeMode::Phonetic, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert_eq!(
            validate_line(&w(&key65), ImeMode::Phonetic, 99, none),
            vec![entry(LineError::KeyTooLong, 0, 65)]
        );

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let stars = validate_line(&w("*? x"), ImeMode::Phonetic, 64, none);
        assert_eq!(
            stars,
            vec![entry(LineError::InvalidChar, 0, 1), entry(LineError::InvalidChar, 1, 1)]
        );

        let mut calls = 0u32;
        let mut check = |c: u16| {
            calls += 1;
            c != b'1' as u16
        };
        let bad = validate_line(&w("a1 字"), ImeMode::Dayi, 4, Some(&mut check));
        assert_eq!(calls, 2);
        assert_eq!(bad, vec![entry(LineError::InvalidChar, 1, 1)]);

        let mut calls = 0u32;
        let mut check = |_c: u16| {
            calls += 1;
            true
        };
        assert!(validate_line(&w("ab 字"), ImeMode::Dayi, 4, Some(&mut check)).is_empty());
        assert_eq!(calls, 0, "ASCII letters skip the engine");

        let mut check = |_c: u16| true;
        assert!(validate_line(&w("中 字"), ImeMode::Dayi, 4, Some(&mut check)).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        assert_eq!(
            validate_line(&w("中 字"), ImeMode::Dayi, 4, none),
            vec![entry(LineError::InvalidChar, 0, 1)]
        );

        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let mut ok_key = vec![128u16];
        ok_key.extend(w(" x"));
        assert!(validate_line(&ok_key, ImeMode::Dayi, 4, none).is_empty());
        let none: Option<&mut dyn FnMut(u16) -> bool> = None;
        let mut bad_key = vec![129u16];
        bad_key.extend(w(" x"));
        assert_eq!(
            validate_line(&bad_key, ImeMode::Array, 4, none),
            vec![entry(LineError::InvalidChar, 0, 1)]
        );
    }

    #[test]
    fn load_and_apply_match_cpp() {
        use crate::values::face_str;

        let mut values = ConfigValues::ZERO;
        values.font_size = 18;
        values.font_weight = 700;
        values.font_italic = 1;
        values.font_face_name = face_from(&w("Segoe UI"));
        values.color_mode = 3;
        values.auto_compose = 1;
        values.clear_on_beep = 0;
        values.do_beep = 2;
        values.do_beep_notify = 0;
        values.do_beep_on_candi = 1;
        values.make_phrase = 1;
        values.custom_table_priority = 0;
        values.arrow_key_sw_pages = 1;
        values.space_as_page_down = 0;
        values.space_as_first_cand_selkey = 1;
        values.show_notify_desktop = 1;
        values.array_notify_sp = 0;
        values.array_force_sp = 1;
        values.array_single_quote_custom_phrase = 0;
        values.dayi_article_mode = 1;
        values.sentence_mode = 1;
        values.do_han_convert = 0;
        values.activated_keyboard_mode = 1;
        values.big5_filter = 0;
        values.max_codes = 6;
        values.array_scope = 2;
        values.numeric_pad = 1;
        values.phonetic_keyboard_layout = 1;
        values.dayi_selkey_mode = 1;
        values.ime_shift_mode = 2;
        values.double_single_byte_mode = 1;
        values.shift_english_digit_mode = 1;
        values.custom.item = 0x0001_0203;
        values.light.item = 0x0004_0506;
        values.dark.item = 0x0007_0809;
        values.reload_reverse_conversion = 1;
        values.app_permission_set = 1;
        values.load_table_mode = 1;
        values.color_mode_key_found = 1;

        // Distinct from every stored palette. Order matches colors[]:
        // item, selected, item background, phrase, number, selected background.
        let effective = [
            0x0009_0807,
            0x0006_0504,
            0x0003_0201,
            0x000D_0C0B,
            0x0010_0F0E,
            0x0013_1211,
        ];
        let snap = load_snapshot(&values, &effective);
        assert_eq!(snap.font_size, 18);
        assert_eq!(snap.font_weight, 700);
        assert_eq!(snap.font_italic, 1);
        assert_eq!(face_str(&snap.font_face_name), w("Segoe UI").as_slice());
        assert_eq!(snap.color_mode, 3);
        assert_eq!(snap.item_color, effective[0]);
        assert_eq!(snap.selected_color, effective[1]);
        assert_eq!(snap.item_bg_color, effective[2]);
        assert_eq!(snap.phrase_color, effective[3]);
        assert_eq!(snap.number_color, effective[4]);
        assert_eq!(snap.selected_bg_color, effective[5]);
        assert_ne!(snap.item_color, values.custom.item);
        assert_eq!(snap.do_beep, 2);
        assert_eq!(snap.sentence_mode, 1);
        assert_eq!(snap.max_codes, 6);
        assert_eq!(snap.phonetic_keyboard_layout, 1);
        assert_eq!(snap.shift_english_digit_mode, 1);
        assert_eq!(snap.dayi_selkey_mode, 1);
        assert_eq!(snap.reverse_conversion_index, 0);

        let custom = values.custom;
        let light = values.light;
        let dark = values.dark;
        let mut edited = snap;
        edited.item_color = 0x0000_00FF;
        edited.reverse_conversion_index = 4;
        edited.font_size = 22;
        edited.max_codes = 9;
        edited.font_face_name = [u16::from(b'B'); 32];
        apply_snapshot(&mut values, &edited);
        assert_eq!(values.font_size, 22);
        assert_eq!(values.max_codes, 9);
        assert_eq!(values.font_face_name[30], u16::from(b'B'));
        assert_eq!(values.font_face_name[31], 0);
        assert_eq!(values.do_beep, 2);
        assert_eq!(values.custom, custom);
        assert_eq!(values.light, light);
        assert_eq!(values.dark, dark);
        assert_eq!(values.reload_reverse_conversion, 1);
        assert_eq!(values.app_permission_set, 1);
        assert_eq!(values.load_table_mode, 1);
        assert_eq!(values.color_mode_key_found, 1);
    }
}
