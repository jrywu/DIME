//! `[Config]` INI handling: applying entries (`ApplyIniConfigEntry` in
//! `DictionarySearch.cpp`), the load sequence of `CConfig::LoadConfig`, and
//! the file written by `CConfig::WriteConfig`.

use dime_core::text::{w, TextOps};

use crate::guid;
use crate::num::{wcstol, wcstoul, wtoi};
use crate::values::{color_mode, colors, face_from, face_str, ConfigValues, Env, ImeMode, Settings, MAX_CODES};

/// Apply one `[Config]` key/value pair.  Keys are matched with
/// `CompareString(NORM_IGNORECASE)` and booleans are true only when the value
/// compares equal to `"1"`; numbers go through `_wtoi` / `wcstol` / `wcstoul`
/// exactly like the C++ code.  Unknown keys are ignored.
pub fn apply_entry(ops: &dyn TextOps, s: &mut Settings, key: &[u16], value: &[u16]) {
    let is = |name: &str| ops.eq(key, &w(name));
    let truthy = || i32::from(ops.eq(value, &w("1")));
    let v = &mut s.v;

    if is("AutoCompose") {
        v.auto_compose = truthy();
    } else if is("ArrayForceSP") {
        v.array_force_sp = truthy();
    } else if is("ArrayNotifySP") {
        v.array_notify_sp = truthy();
    } else if is("ArraySingleQuoteCustomPhrase") {
        v.array_single_quote_custom_phrase = truthy();
    } else if is("DayiArticleMode") {
        v.dayi_article_mode = truthy();
    } else if is("DayiSelkeyMode") {
        let m = wtoi(value);
        v.dayi_selkey_mode = if (0..=2).contains(&m) { m } else { 0 };
    } else if is("SpaceAsPageDown") {
        v.space_as_page_down = truthy();
    } else if is("SpaceAsFirstCandSelkey") {
        v.space_as_first_cand_selkey = truthy();
    } else if is("ArrowKeySWPages") {
        v.arrow_key_sw_pages = truthy();
    } else if is("ClearOnBeep") {
        v.clear_on_beep = truthy();
    } else if is("DoBeep") {
        v.do_beep = truthy();
    } else if is("DoBeepNotify") {
        v.do_beep_notify = truthy();
    } else if is("DoBeepOnCandi") {
        v.do_beep_on_candi = truthy();
    } else if is("SentenceMode") {
        v.sentence_mode = truthy();
    } else if is("FontSize") {
        v.font_size = wtoi(value) as u32;
    } else if is("FontWeight") {
        v.font_weight = wtoi(value) as u32;
    } else if is("FontItalic") {
        v.font_italic = truthy();
    } else if is("DoubleSingleByteMode") {
        v.double_single_byte_mode = wtoi(value);
    } else if is("ShiftEnglishDigitMode") {
        v.shift_english_digit_mode = wtoi(value);
    } else if is("IMEShiftMode") {
        v.ime_shift_mode = wtoi(value);
    } else if is("MaxCodes") {
        let m = wtoi(value);
        if m > 0 && m as u32 <= MAX_CODES {
            v.max_codes = m as u32;
        }
    } else if is("ActivatedKeyboardMode") {
        v.activated_keyboard_mode = truthy();
    } else if is("MakePhrase") {
        v.make_phrase = truthy();
    } else if is("DoHanConvert") {
        v.do_han_convert = truthy();
    } else if is("PhoneticKeyboardLayout") {
        v.phonetic_keyboard_layout = wtoi(value);
    } else if is("ArrayUnicodeScope") || is("ArrayScope") {
        v.array_scope = wtoi(value);
    } else if is("Big5Filter") {
        v.big5_filter = truthy();
    } else if is("NumericPad") {
        v.numeric_pad = wtoi(value);
    } else if is("ReloadReverseConversion") {
        v.reload_reverse_conversion = truthy();
    } else if is("ReverseConversionCLSID") {
        if let Some(g) = guid::parse(value) {
            v.reverse_conversion_clsid = g;
        }
    } else if is("ReverseConversionGUIDProfile") {
        if let Some(g) = guid::parse(value) {
            v.reverse_conversion_guid_profile = g;
        }
    } else if is("ReverseConversionDescription") {
        s.reverse_conversion_description = Some(value.to_vec());
    } else if is("ShowNotifyDesktop") {
        v.show_notify_desktop = truthy();
    } else if is("AppPermissionSet") {
        v.app_permission_set = truthy();
    } else if is("ColorMode") {
        v.color_mode = wcstol(value, 0).0;
        v.color_mode_key_found = 1;
    } else if let Some(slot) = color_slot(v, &is) {
        *slot = wcstoul(value, 0).0;
    } else if is("CustomTablePriority") {
        v.custom_table_priority = truthy();
    } else if is("FontFaceName") {
        v.font_face_name = face_from(value);
    } else if is("LoadTableMode") {
        v.load_table_mode = truthy();
    }
}

fn color_slot<'a>(v: &'a mut ConfigValues, is: &dyn Fn(&str) -> bool) -> Option<&'a mut u32> {
    let names: [(&str, &str); 6] = [
        ("ItemColor", "item"),
        ("PhraseColor", "phrase"),
        ("NumberColor", "number"),
        ("ItemBGColor", "item_bg"),
        ("SelectedItemColor", "selected"),
        ("SelectedBGItemColor", "selected_bg"),
    ];
    for (prefix, palette) in [("", 0), ("Light", 1), ("Dark", 2)] {
        for (name, field) in names {
            if is(&format!("{prefix}{name}")) {
                let p = match palette {
                    0 => &mut v.custom,
                    1 => &mut v.light,
                    _ => &mut v.dark,
                };
                return Some(match field {
                    "item" => &mut p.item,
                    "phrase" => &mut p.phrase,
                    "number" => &mut p.number,
                    "item_bg" => &mut p.item_bg,
                    "selected" => &mut p.selected,
                    _ => &mut p.selected_bg,
                });
            }
        }
    }
    None
}

/// First half of the "file exists" branch of `CConfig::LoadConfig`, run
/// before the entries are applied.
pub fn begin_load(s: &mut Settings, mode: ImeMode) {
    s.v.set_mode_defaults(mode);
    s.v.load_table_mode = 1;
    s.v.color_mode_key_found = 0;
    s.v.color_mode = color_mode::SYSTEM;
}

/// Second half: migrate INI files written before `ColorMode` existed.  If the
/// legacy six colours are still factory defaults the user gets SYSTEM (LIGHT
/// before Windows 10 1809), otherwise CUSTOM so their colours are kept.
pub fn finish_load(s: &mut Settings, env: &Env) {
    if s.v.color_mode_key_found != 0 {
        return;
    }
    let c = &s.v.custom;
    let is_default = c.item == colors::ITEM
        && c.selected == colors::SELECTED_ITEM
        && c.selected_bg == colors::SELECTED_BK
        && c.phrase == colors::PHRASE
        && c.number == colors::NUMBER
        && c.item_bg == env.sys_3d_highlight;
    s.v.color_mode = if is_default { ConfigValues::default_color_mode(env) } else { color_mode::CUSTOM };
}

/// The "config file does not exist" branch of `CConfig::LoadConfig`.
pub fn load_missing(s: &mut Settings, mode: ImeMode, env: &Env) {
    s.v.set_mode_defaults(mode);
    s.v.color_mode = ConfigValues::default_color_mode(env);
}

/// Load from parsed `[Config]` entries (the whole "file exists" branch).
pub fn load_entries<'a>(
    ops: &dyn TextOps,
    s: &mut Settings,
    mode: ImeMode,
    env: &Env,
    entries: impl IntoIterator<Item = (&'a [u16], &'a [u16])>,
) {
    begin_load(s, mode);
    for (k, v) in entries {
        apply_entry(ops, s, k, v);
    }
    finish_load(s, env);
}

/// The file `CConfig::WriteConfig` produces, as UTF-16 code units including
/// the BOM.  `_wfopen_s(..., L"w, ccs=UTF-16LE")` writes a BOM and translates
/// `\n` to `\r\n`, so the result is byte-identical when written as-is.
pub fn serialize(s: &Settings, mode: ImeMode) -> Vec<u16> {
    let v = &s.v;
    let b = |x: i32| i32::from(x != 0);
    let mut t = String::new();
    let mut line = |l: String| {
        t.push_str(&l);
        t.push_str("\r\n");
    };
    line("[Config]".into());
    line(format!("AutoCompose = {}", b(v.auto_compose)));
    line(format!("SpaceAsPageDown = {}", b(v.space_as_page_down)));
    line(format!("SpaceAsFirstCandSelkey = {}", b(v.space_as_first_cand_selkey)));
    line(format!("ArrowKeySWPages = {}", b(v.arrow_key_sw_pages)));
    line(format!("ClearOnBeep = {}", b(v.clear_on_beep)));
    line(format!("DoBeep = {}", b(v.do_beep)));
    line(format!("DoBeepNotify = {}", b(v.do_beep_notify)));
    line(format!("DoBeepOnCandi = {}", b(v.do_beep_on_candi)));
    line(format!("SentenceMode = {}", b(v.sentence_mode)));
    line(format!("ActivatedKeyboardMode = {}", b(v.activated_keyboard_mode)));
    line(format!("MakePhrase = {}", b(v.make_phrase)));
    line(format!("MaxCodes = {}", v.max_codes as i32));
    line(format!("IMEShiftMode  = {}", v.ime_shift_mode));
    line(format!("DoubleSingleByteMode = {}", v.double_single_byte_mode));
    line(format!("ShiftEnglishDigitMode = {}", v.shift_english_digit_mode));
    line(format!("ShowNotifyDesktop = {}", b(v.show_notify_desktop)));
    line(format!("DoHanConvert = {}", b(v.do_han_convert)));
    line(format!("FontSize = {}", v.font_size as i32));
    line(format!("FontItalic = {}", b(v.font_italic)));
    line(format!("FontWeight = {}", v.font_weight as i32));
    let mut out: Vec<u16> = vec![0xFEFF];
    // The face name is UTF-16 and may hold unpaired surrogates: splice it in raw.
    out.extend(t.encode_utf16());
    out.extend(w("FontFaceName = "));
    out.extend_from_slice(face_str(&v.font_face_name));
    out.extend(w("\r\n"));

    let mut t = String::new();
    let mut line = |l: String| {
        t.push_str(&l);
        t.push_str("\r\n");
    };
    line(format!("ColorMode = {}", v.color_mode));
    for (prefix, p) in [("", &v.custom), ("Light", &v.light), ("Dark", &v.dark)] {
        line(format!("{prefix}ItemColor = 0x{:06X}", p.item));
        line(format!("{prefix}PhraseColor = 0x{:06X}", p.phrase));
        line(format!("{prefix}NumberColor = 0x{:06X}", p.number));
        line(format!("{prefix}ItemBGColor = 0x{:06X}", p.item_bg));
        line(format!("{prefix}SelectedItemColor = 0x{:06X}", p.selected));
        line(format!("{prefix}SelectedBGItemColor = 0x{:06X}", p.selected_bg));
    }
    line(format!("CustomTablePriority = {}", b(v.custom_table_priority)));
    line(format!("ReloadReverseConversion = {}", v.reload_reverse_conversion));
    line(format!("ReverseConversionCLSID = {}", guid::to_string(&v.reverse_conversion_clsid)));
    line(format!("ReverseConversionGUIDProfile = {}", guid::to_string(&v.reverse_conversion_guid_profile)));
    out.extend(t.encode_utf16());
    out.extend(w("ReverseConversionDescription = "));
    match &s.reverse_conversion_description {
        // fwprintf("%s", nullptr) prints "(null)"; the text stops at a NUL.
        None => out.extend(w("(null)")),
        Some(d) => out.extend_from_slice(&d[..d.iter().position(|&c| c == 0).unwrap_or(d.len())]),
    }
    out.extend(w("\r\n"));

    let mut t = String::new();
    let mut line = |l: String| {
        t.push_str(&l);
        t.push_str("\r\n");
    };
    line(format!("AppPermissionSet = {}", b(v.app_permission_set)));
    if mode == ImeMode::Dayi {
        line(format!("DayiArticleMode = {}", b(v.dayi_article_mode)));
        line(format!("DayiSelkeyMode = {}", v.dayi_selkey_mode));
    }
    if mode == ImeMode::Array {
        line(format!("ArrayScope = {}", v.array_scope));
        line(format!("ArrayForceSP = {}", b(v.array_force_sp)));
        line(format!("ArrayNotifySP = {}", b(v.array_notify_sp)));
        line(format!("ArraySingleQuoteCustomPhrase = {}", b(v.array_single_quote_custom_phrase)));
    }
    if mode == ImeMode::Phonetic {
        line(format!("PhoneticKeyboardLayout = {}", v.phonetic_keyboard_layout));
    }
    if mode != ImeMode::Array {
        line(format!("Big5Filter = {}", b(v.big5_filter)));
    }
    line(format!("NumericPad = {}", v.numeric_pad));
    if v.load_table_mode != 0 {
        line("LoadTableMode = 1".into());
    }
    out.extend(t.encode_utf16());
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_core::text::SimpleTextOps;

    const ENV: Env = Env { sys_3d_highlight: 0xFFFFFF, windows_1809_or_later: 1 };

    fn apply(s: &mut Settings, k: &str, v: &str) {
        apply_entry(&SimpleTextOps, s, &w(k), &w(v));
    }

    #[test]
    fn booleans_need_exactly_one() {
        let mut s = Settings::initial(&ENV);
        apply(&mut s, "autocompose", "1");
        assert_eq!(s.v.auto_compose, 1);
        apply(&mut s, "AutoCompose", "2");
        assert_eq!(s.v.auto_compose, 0);
        apply(&mut s, "DoBeep", "true");
        assert_eq!(s.v.do_beep, 0);
    }

    #[test]
    fn numbers_and_validation() {
        let mut s = Settings::initial(&ENV);
        apply(&mut s, "MaxCodes", "21");
        assert_eq!(s.v.max_codes, 4);
        apply(&mut s, "MaxCodes", "7");
        assert_eq!(s.v.max_codes, 7);
        apply(&mut s, "DayiSelkeyMode", "3");
        assert_eq!(s.v.dayi_selkey_mode, 0);
        apply(&mut s, "FontSize", "-5");
        assert_eq!(s.v.font_size, (-5i32) as u32);
        apply(&mut s, "ItemColor", "0x123456");
        assert_eq!(s.v.custom.item, 0x123456);
        apply(&mut s, "DarkSelectedBGItemColor", "010");
        assert_eq!(s.v.dark.selected_bg, 8);
        apply(&mut s, "ArrayUnicodeScope", "3");
        assert_eq!(s.v.array_scope, 3);
    }

    #[test]
    fn color_mode_migration() {
        let mut s = Settings::initial(&ENV);
        load_entries(&SimpleTextOps, &mut s, ImeMode::Dayi, &ENV, []);
        assert_eq!(s.v.color_mode, color_mode::SYSTEM);
        let (k, v) = (w("ItemColor"), w("0x0000FF"));
        load_entries(&SimpleTextOps, &mut s, ImeMode::Dayi, &ENV, [(&k[..], &v[..])]);
        assert_eq!(s.v.color_mode, color_mode::CUSTOM);
        let (k, v) = (w("ColorMode"), w("2"));
        load_entries(&SimpleTextOps, &mut s, ImeMode::Dayi, &ENV, [(&k[..], &v[..])]);
        assert_eq!(s.v.color_mode, color_mode::DARK);
    }

    #[test]
    fn serialize_round_trips() {
        let mut s = Settings::initial(&ENV);
        s.v.font_face_name = face_from(&w("Consolas"));
        s.reverse_conversion_description = Some(w("注音"));
        s.v.load_table_mode = 1;
        for mode in [ImeMode::Dayi, ImeMode::Array, ImeMode::Phonetic, ImeMode::Generic] {
            let text = serialize(&s, mode);
            assert_eq!(text[0], 0xFEFF);
            let body = String::from_utf16(&text[1..]).unwrap();
            let mut back = Settings::initial(&ENV);
            let pairs: Vec<(Vec<u16>, Vec<u16>)> = body
                .lines()
                .filter_map(|l| l.split_once(" = ").or_else(|| l.split_once("  = ")))
                .map(|(k, v)| (w(k.trim()), w(v)))
                .collect();
            load_entries(&SimpleTextOps, &mut back, mode, &ENV, pairs.iter().map(|(k, v)| (&k[..], &v[..])));
            assert_eq!(back.v.font_face_name, s.v.font_face_name);
            assert_eq!(back.reverse_conversion_description, s.reverse_conversion_description);
            assert_eq!(serialize(&back, mode), text, "{mode:?}");
        }
    }
}
