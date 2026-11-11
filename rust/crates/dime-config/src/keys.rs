//! The settings key registry used by the CLI (`g_keyRegistry` in
//! `DIMESettings/CLI.cpp`, and `cli.rs` on the Rust CLI path): key types,
//! value ranges, which IME modes a key applies to, and the `--get` / `--set`
//! value formatting and validation.

use dime_core::text::w;

use crate::guid;
use crate::num::{wcstol, wcstoul};
use crate::values::{face_from, face_str, ImeMode, Palette, Settings};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyType {
    Bool,
    Int,
    Color,
    Str,
    Clsid,
}

pub const MASK_DAYI: u32 = 0x1;
pub const MASK_ARRAY: u32 = 0x2;
pub const MASK_PHONETIC: u32 = 0x4;
pub const MASK_GENERIC: u32 = 0x8;
pub const MASK_ALL: u32 = 0xF;
pub const MASK_NOT_ARRAY: u32 = MASK_DAYI | MASK_PHONETIC | MASK_GENERIC;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KeyInfo {
    pub name: &'static str,
    pub ty: KeyType,
    /// Inclusive range for `Bool` / `Int`.
    pub min: i32,
    pub max: i32,
    pub mode_mask: u32,
}

const fn k(name: &'static str, ty: KeyType, min: i32, max: i32, mode_mask: u32) -> KeyInfo {
    KeyInfo { name, ty, min, max, mode_mask }
}

use KeyType::{Bool as B, Clsid as G, Color as C, Int as I, Str as S};

/// In `--get-all` output order.
pub static KEYS: &[KeyInfo] = &[
    k("AutoCompose", B, 0, 1, MASK_ALL),
    k("SpaceAsPageDown", B, 0, 1, MASK_ALL),
    k("SpaceAsFirstCandSelkey", B, 0, 1, MASK_GENERIC),
    k("ArrowKeySWPages", B, 0, 1, MASK_ALL),
    k("ClearOnBeep", B, 0, 1, MASK_ALL),
    k("DoBeep", B, 0, 1, MASK_ALL),
    k("DoBeepNotify", B, 0, 1, MASK_ALL),
    k("DoBeepOnCandi", B, 0, 1, MASK_DAYI),
    k("ActivatedKeyboardMode", B, 0, 1, MASK_ALL),
    k("MakePhrase", B, 0, 1, MASK_ALL),
    k("ShowNotifyDesktop", B, 0, 1, MASK_ALL),
    k("DoHanConvert", B, 0, 1, MASK_ALL),
    k("FontItalic", B, 0, 1, MASK_ALL),
    k("CustomTablePriority", B, 0, 1, MASK_ALL),
    k("DayiArticleMode", B, 0, 1, MASK_DAYI),
    k("SentenceMode", B, 0, 1, MASK_DAYI),
    k("ArrayForceSP", B, 0, 1, MASK_ARRAY),
    k("ArrayNotifySP", B, 0, 1, MASK_ARRAY),
    k("ArraySingleQuoteCustomPhrase", B, 0, 1, MASK_ARRAY),
    k("Big5Filter", B, 0, 1, MASK_NOT_ARRAY),
    k("MaxCodes", I, 1, 20, MASK_ALL),
    k("FontSize", I, 8, 72, MASK_ALL),
    k("FontWeight", I, 100, 900, MASK_ALL),
    k("IMEShiftMode", I, 0, 3, MASK_ALL),
    k("DoubleSingleByteMode", I, 0, 2, MASK_ALL),
    k("ShiftEnglishDigitMode", I, 0, 2, MASK_ALL),
    k("ColorMode", I, 0, 3, MASK_ALL),
    k("NumericPad", I, 0, 2, MASK_ALL),
    k("ArrayScope", I, 0, 5, MASK_ARRAY),
    k("PhoneticKeyboardLayout", I, 0, 1, MASK_PHONETIC),
    k("DayiSelkeyMode", I, 0, 2, MASK_DAYI),
    k("FontFaceName", S, 0, 0, MASK_ALL),
    k("ReverseConversionDescription", S, 0, 0, MASK_ALL),
    k("ReverseConversionCLSID", G, 0, 0, MASK_ALL),
    k("ReverseConversionGUIDProfile", G, 0, 0, MASK_ALL),
    k("ItemColor", C, 0, 0, MASK_ALL),
    k("PhraseColor", C, 0, 0, MASK_ALL),
    k("NumberColor", C, 0, 0, MASK_ALL),
    k("ItemBGColor", C, 0, 0, MASK_ALL),
    k("SelectedItemColor", C, 0, 0, MASK_ALL),
    k("SelectedBGItemColor", C, 0, 0, MASK_ALL),
    k("LightItemColor", C, 0, 0, MASK_ALL),
    k("LightPhraseColor", C, 0, 0, MASK_ALL),
    k("LightNumberColor", C, 0, 0, MASK_ALL),
    k("LightItemBGColor", C, 0, 0, MASK_ALL),
    k("LightSelectedItemColor", C, 0, 0, MASK_ALL),
    k("LightSelectedBGItemColor", C, 0, 0, MASK_ALL),
    k("DarkItemColor", C, 0, 0, MASK_ALL),
    k("DarkPhraseColor", C, 0, 0, MASK_ALL),
    k("DarkNumberColor", C, 0, 0, MASK_ALL),
    k("DarkItemBGColor", C, 0, 0, MASK_ALL),
    k("DarkSelectedItemColor", C, 0, 0, MASK_ALL),
    k("DarkSelectedBGItemColor", C, 0, 0, MASK_ALL),
];

/// `_wcsicmp` in the "C" locale: only ASCII letters fold.
fn wcsicmp_eq(a: &[u16], b: &str) -> bool {
    let fold = |c: u16| if (0x41..=0x5A).contains(&c) { c + 32 } else { c };
    let b = w(b);
    a.len() == b.len() && a.iter().zip(&b).all(|(&x, &y)| fold(x) == fold(y))
}

/// `CLI_FindKey`: case-insensitive lookup.
pub fn find_key(name: &[u16]) -> Option<&'static KeyInfo> {
    KEYS.iter().find(|ki| wcsicmp_eq(name, ki.name))
}

/// Whether the key applies to `mode` (the CLI exits with code 3 otherwise).
pub fn applicable(ki: &KeyInfo, mode: ImeMode) -> bool {
    let bit = match mode {
        ImeMode::Dayi => MASK_DAYI,
        ImeMode::Array => MASK_ARRAY,
        ImeMode::Phonetic => MASK_PHONETIC,
        ImeMode::Generic => MASK_GENERIC,
        ImeMode::None => return false,
    };
    ki.mode_mask & bit != 0
}

fn palette_mut<'a>(s: &'a mut Settings, name: &str) -> Option<(&'a mut Palette, &'static str)> {
    let (p, rest) = if let Some(r) = name.strip_prefix("Light") {
        (&mut s.v.light, r)
    } else if let Some(r) = name.strip_prefix("Dark") {
        (&mut s.v.dark, r)
    } else {
        (&mut s.v.custom, name)
    };
    let field = match rest {
        "ItemColor" => "item",
        "PhraseColor" => "phrase",
        "NumberColor" => "number",
        "ItemBGColor" => "item_bg",
        "SelectedItemColor" => "selected",
        "SelectedBGItemColor" => "selected_bg",
        _ => return None,
    };
    Some((p, field))
}

fn palette_slot<'a>(p: &'a mut Palette, field: &str) -> &'a mut u32 {
    match field {
        "item" => &mut p.item,
        "phrase" => &mut p.phrase,
        "number" => &mut p.number,
        "item_bg" => &mut p.item_bg,
        "selected" => &mut p.selected,
        _ => &mut p.selected_bg,
    }
}

/// Mutable access to an integer-valued (`Bool` / `Int`) setting.
fn int_slot<'a>(s: &'a mut Settings, name: &str) -> Option<&'a mut i32> {
    let v = &mut s.v;
    Some(match name {
        "AutoCompose" => &mut v.auto_compose,
        "SpaceAsPageDown" => &mut v.space_as_page_down,
        "SpaceAsFirstCandSelkey" => &mut v.space_as_first_cand_selkey,
        "ArrowKeySWPages" => &mut v.arrow_key_sw_pages,
        "ClearOnBeep" => &mut v.clear_on_beep,
        "DoBeep" => &mut v.do_beep,
        "DoBeepNotify" => &mut v.do_beep_notify,
        "DoBeepOnCandi" => &mut v.do_beep_on_candi,
        "ActivatedKeyboardMode" => &mut v.activated_keyboard_mode,
        "MakePhrase" => &mut v.make_phrase,
        "ShowNotifyDesktop" => &mut v.show_notify_desktop,
        "DoHanConvert" => &mut v.do_han_convert,
        "FontItalic" => &mut v.font_italic,
        "CustomTablePriority" => &mut v.custom_table_priority,
        "DayiArticleMode" => &mut v.dayi_article_mode,
        "SentenceMode" => &mut v.sentence_mode,
        "ArrayForceSP" => &mut v.array_force_sp,
        "ArrayNotifySP" => &mut v.array_notify_sp,
        "ArraySingleQuoteCustomPhrase" => &mut v.array_single_quote_custom_phrase,
        "Big5Filter" => &mut v.big5_filter,
        "IMEShiftMode" => &mut v.ime_shift_mode,
        "DoubleSingleByteMode" => &mut v.double_single_byte_mode,
        "ShiftEnglishDigitMode" => &mut v.shift_english_digit_mode,
        "ColorMode" => &mut v.color_mode,
        "NumericPad" => &mut v.numeric_pad,
        "ArrayScope" => &mut v.array_scope,
        "PhoneticKeyboardLayout" => &mut v.phonetic_keyboard_layout,
        "DayiSelkeyMode" => &mut v.dayi_selkey_mode,
        _ => return None,
    })
}

fn uint_slot<'a>(s: &'a mut Settings, name: &str) -> Option<&'a mut u32> {
    Some(match name {
        "MaxCodes" => &mut s.v.max_codes,
        "FontSize" => &mut s.v.font_size,
        "FontWeight" => &mut s.v.font_weight,
        _ => return None,
    })
}

/// `GetKeyValue`: the text `--get` prints.
pub fn get_value(s: &Settings, ki: &KeyInfo) -> Vec<u16> {
    let mut s = s.clone();
    let n = ki.name;
    if let Some(u) = uint_slot(&mut s, n) {
        return w(&u.to_string());
    }
    match ki.ty {
        KeyType::Bool => w(&i32::from(*int_slot(&mut s, n).expect("bool key") != 0).to_string()),
        KeyType::Int => w(&int_slot(&mut s, n).expect("int key").to_string()),
        KeyType::Color => {
            let (p, f) = palette_mut(&mut s, n).expect("colour key");
            w(&format!("0x{:06X}", *palette_slot(p, f)))
        }
        KeyType::Str if n == "FontFaceName" => face_str(&s.v.font_face_name).to_vec(),
        KeyType::Str => s.reverse_conversion_description.clone().unwrap_or_default(),
        KeyType::Clsid if n == "ReverseConversionCLSID" => w(&guid::to_string(&s.v.reverse_conversion_clsid)),
        KeyType::Clsid => w(&guid::to_string(&s.v.reverse_conversion_guid_profile)),
    }
}

/// Why `--set` rejected a value; `Display` is the CLI's error line.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SetError {
    NotInteger(String),
    OutOfRange { value: i32, min: i32, max: i32, key: &'static str },
    NotColor(String),
    NotGuid(String),
}

impl std::fmt::Display for SetError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SetError::NotInteger(v) => write!(f, "Error: value '{v}' is not a valid integer."),
            SetError::OutOfRange { value, min, max, key } => {
                write!(f, "Error: value {value} is out of range [{min}, {max}] for key '{key}'.")
            }
            SetError::NotColor(v) => write!(f, "Error: '{v}' is not a valid hex colour (e.g. 0xRRGGBB)."),
            SetError::NotGuid(v) => write!(f, "Error: '{v}' is not a valid GUID."),
        }
    }
}

/// `CLI_ParseColorValue`: `0xRRGGBB` or `RRGGBB` (anything `wcstoul(…, 16)`
/// consumes completely).
pub fn parse_color(value: &[u16]) -> Option<u32> {
    if value.is_empty() {
        return None;
    }
    let p = if value.len() >= 2 && value[0] == b'0' as u16 && (value[1] == b'x' as u16 || value[1] == b'X' as u16) {
        &value[2..]
    } else {
        value
    };
    let (v, used) = wcstoul(p, 16);
    (used != 0 && used == p.len()).then_some(v)
}

/// `ApplyKeyValue`: validate and store a `--set` value.
pub fn set_value(s: &mut Settings, ki: &KeyInfo, value: &[u16]) -> Result<(), SetError> {
    let text = || String::from_utf16_lossy(value);
    let n = ki.name;
    match ki.ty {
        KeyType::Bool | KeyType::Int => {
            let (v, used) = wcstol(value, 10);
            if used == 0 || used != value.len() {
                return Err(SetError::NotInteger(text()));
            }
            if v < ki.min || v > ki.max {
                return Err(SetError::OutOfRange { value: v, min: ki.min, max: ki.max, key: ki.name });
            }
            if let Some(u) = uint_slot(s, n) {
                *u = v as u32;
            } else {
                *int_slot(s, n).expect("int key") = v;
            }
        }
        KeyType::Color => {
            let c = parse_color(value).ok_or_else(|| SetError::NotColor(text()))?;
            let (p, f) = palette_mut(s, n).expect("colour key");
            *palette_slot(p, f) = c;
        }
        KeyType::Str if n == "FontFaceName" => s.v.font_face_name = face_from(value),
        KeyType::Str => {
            s.reverse_conversion_description = Some(value.to_vec());
            s.v.reload_reverse_conversion = 1;
        }
        KeyType::Clsid => {
            // The CLI maps an empty value to CLSID_NULL (clears the provider).
            let g = if value.is_empty() {
                [0; 16]
            } else {
                guid::parse(value).ok_or_else(|| SetError::NotGuid(text()))?
            };
            if n == "ReverseConversionCLSID" {
                s.v.reverse_conversion_clsid = g;
            } else {
                s.v.reverse_conversion_guid_profile = g;
            }
            s.v.reload_reverse_conversion = 1;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::values::Env;

    const ENV: Env = Env { sys_3d_highlight: 0xFFFFFF, windows_1809_or_later: 1 };

    fn set(s: &mut Settings, k: &str, v: &str) -> Result<(), SetError> {
        set_value(s, find_key(&w(k)).unwrap(), &w(v))
    }
    fn get(s: &Settings, k: &str) -> String {
        String::from_utf16(&get_value(s, find_key(&w(k)).unwrap())).unwrap()
    }

    #[test]
    fn every_key_round_trips() {
        let mut s = Settings::initial(&ENV);
        for ki in KEYS {
            let v = match ki.ty {
                KeyType::Bool | KeyType::Int => ki.max.to_string(),
                KeyType::Color => "0x0A0B0C".into(),
                KeyType::Str => "Font".into(),
                KeyType::Clsid => "{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B}".into(),
            };
            set(&mut s, ki.name, &v).unwrap();
            assert_eq!(get(&s, ki.name), v, "{}", ki.name);
        }
    }

    #[test]
    fn validation_messages() {
        let mut s = Settings::initial(&ENV);
        assert_eq!(set(&mut s, "fontsize", "7").unwrap_err().to_string(),
            "Error: value 7 is out of range [8, 72] for key 'FontSize'.");
        assert_eq!(set(&mut s, "FontSize", "12px").unwrap_err().to_string(),
            "Error: value '12px' is not a valid integer.");
        assert!(set(&mut s, "FontSize", "").is_err());
        assert!(set(&mut s, "ItemColor", "0xZZ").is_err());
        assert!(set(&mut s, "ItemColor", "").is_err());
        assert_eq!(set(&mut s, "ItemColor", "ff00"), Ok(()));
        assert_eq!(get(&s, "ItemColor"), "0x00FF00");
        assert!(set(&mut s, "ReverseConversionCLSID", "nope").is_err());
        assert_eq!(set(&mut s, "ReverseConversionCLSID", ""), Ok(()));
        assert_eq!(get(&s, "ReverseConversionCLSID"), "{00000000-0000-0000-0000-000000000000}");
        assert_eq!(s.v.reload_reverse_conversion, 1);
    }

    #[test]
    fn mode_applicability() {
        let ki = find_key(&w("SentenceMode")).unwrap();
        assert!(applicable(ki, ImeMode::Dayi));
        assert!(!applicable(ki, ImeMode::Array));
        assert!(!applicable(find_key(&w("Big5Filter")).unwrap(), ImeMode::Array));
        assert!(find_key(&w("NoSuchKey")).is_none());
    }
}
