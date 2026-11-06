//! The settings record (`CConfig`'s static members) and its defaults.

use dime_core::text::w;

/// `LF_FACESIZE`: font face buffer size including the terminating NUL.
pub const LF_FACESIZE: usize = 32;
/// `MAX_CODES` in `Define.h`.
pub const MAX_CODES: u32 = 20;
/// `FW_NORMAL`.
pub const FW_NORMAL: u32 = 400;

/// `IME_MODE` in `BaseStructure.h`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ImeMode {
    Dayi = 0,
    Array = 1,
    Phonetic = 2,
    Generic = 3,
    None = 4,
}

impl ImeMode {
    pub fn from_i32(v: i32) -> ImeMode {
        match v {
            0 => ImeMode::Dayi,
            1 => ImeMode::Array,
            2 => ImeMode::Phonetic,
            3 => ImeMode::Generic,
            _ => ImeMode::None,
        }
    }

    /// Lower-case name used by the CLI (`--mode dayi`).
    pub fn cli_name(self) -> &'static str {
        match self {
            ImeMode::Dayi => "dayi",
            ImeMode::Array => "array",
            ImeMode::Phonetic => "phonetic",
            ImeMode::Generic => "generic",
            ImeMode::None => "(none)",
        }
    }
}

/// `IME_COLOR_MODE` in `Define.h`.
pub mod color_mode {
    pub const SYSTEM: i32 = 0;
    pub const LIGHT: i32 = 1;
    pub const DARK: i32 = 2;
    pub const CUSTOM: i32 = 3;
}

/// `RGB(r, g, b)` (a `COLORREF` is 0x00BBGGRR).
pub const fn rgb(r: u8, g: u8, b: u8) -> u32 {
    (r as u32) | ((g as u32) << 8) | ((b as u32) << 16)
}

/// Candidate-window colour constants from `Define.h`.
pub mod colors {
    use super::rgb;
    pub const ITEM: u32 = rgb(0x00, 0x00, 0x00);
    pub const PHRASE: u32 = rgb(0x00, 0x80, 0x00);
    pub const NUMBER: u32 = rgb(0xB4, 0xB4, 0xB4);
    pub const SELECTED_ITEM: u32 = rgb(0xFF, 0xFF, 0xFF);
    pub const SELECTED_BK: u32 = rgb(0x63, 0xB4, 0xFB);
    pub const DARK_ITEM: u32 = rgb(220, 220, 220);
    pub const DARK_PHRASE: u32 = rgb(144, 238, 144);
    pub const DARK_NUMBER: u32 = rgb(128, 128, 128);
    pub const DARK_ITEM_BG: u32 = rgb(30, 30, 30);
    pub const DARK_SELECTED: u32 = rgb(30, 30, 30);
    pub const DARK_SELECTED_BG: u32 = rgb(0, 120, 215);
}

/// One six-colour palette of the candidate window.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Palette {
    pub item: u32,
    pub phrase: u32,
    pub number: u32,
    pub item_bg: u32,
    pub selected: u32,
    pub selected_bg: u32,
}

/// Values the defaults depend on that only the host can answer.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Env {
    /// `GetSysColor(COLOR_3DHIGHLIGHT)`: default candidate background.
    pub sys_3d_highlight: u32,
    /// `Global::isWindows1809OrLater` (selects SYSTEM vs LIGHT as default colour mode).
    pub windows_1809_or_later: i32,
}

/// All plain-data settings, laid out for the C ABI (`DimeConfigValues` in
/// `dime_ffi.h`).  Field types mirror `CConfig`: `BOOL` and enums are `i32`,
/// `UINT`/`COLORREF` are `u32`, GUIDs are their 16 in-memory bytes.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConfigValues {
    pub auto_compose: i32,
    pub space_as_page_down: i32,
    pub space_as_first_cand_selkey: i32,
    pub arrow_key_sw_pages: i32,
    pub clear_on_beep: i32,
    pub do_beep: i32,
    pub do_beep_notify: i32,
    pub do_beep_on_candi: i32,
    pub sentence_mode: i32,
    pub activated_keyboard_mode: i32,
    pub make_phrase: i32,
    pub show_notify_desktop: i32,
    pub do_han_convert: i32,
    pub font_italic: i32,
    pub custom_table_priority: i32,
    pub dayi_article_mode: i32,
    pub array_force_sp: i32,
    pub array_notify_sp: i32,
    pub array_single_quote_custom_phrase: i32,
    pub big5_filter: i32,
    pub reload_reverse_conversion: i32,
    pub app_permission_set: i32,
    pub load_table_mode: i32,
    pub color_mode_key_found: i32,

    pub max_codes: u32,
    pub font_size: u32,
    pub font_weight: u32,

    pub ime_shift_mode: i32,
    pub double_single_byte_mode: i32,
    pub shift_english_digit_mode: i32,
    pub color_mode: i32,
    pub numeric_pad: i32,
    pub array_scope: i32,
    pub phonetic_keyboard_layout: i32,
    pub dayi_selkey_mode: i32,

    /// `ColorMode = 3` palette (`ItemColor`, `PhraseColor`, ...).
    pub custom: Palette,
    pub light: Palette,
    pub dark: Palette,

    /// NUL-terminated.
    pub font_face_name: [u16; LF_FACESIZE],
    pub reverse_conversion_clsid: [u8; 16],
    pub reverse_conversion_guid_profile: [u8; 16],
}

/// `L"微軟正黑體"`, the default candidate font.
pub fn default_font_face() -> [u16; LF_FACESIZE] {
    face_from(&w("微軟正黑體"))
}

/// Copy into a face-name buffer, truncating like `StringCchCopyN(dst, LF_FACESIZE, ...)`.
pub fn face_from(s: &[u16]) -> [u16; LF_FACESIZE] {
    let mut out = [0u16; LF_FACESIZE];
    let s = &s[..s.iter().position(|&c| c == 0).unwrap_or(s.len())];
    let n = s.len().min(LF_FACESIZE - 1);
    out[..n].copy_from_slice(&s[..n]);
    out
}

/// The face name without its terminator.
pub fn face_str(face: &[u16; LF_FACESIZE]) -> &[u16] {
    &face[..face.iter().position(|&c| c == 0).unwrap_or(LF_FACESIZE)]
}

impl Palette {
    pub fn light_defaults(env: &Env) -> Palette {
        Palette {
            item: colors::ITEM,
            phrase: colors::PHRASE,
            number: colors::NUMBER,
            item_bg: env.sys_3d_highlight,
            selected: colors::SELECTED_ITEM,
            selected_bg: colors::SELECTED_BK,
        }
    }

    pub fn dark_defaults() -> Palette {
        Palette {
            item: colors::DARK_ITEM,
            phrase: colors::DARK_PHRASE,
            number: colors::DARK_NUMBER,
            item_bg: colors::DARK_ITEM_BG,
            selected: colors::DARK_SELECTED,
            selected_bg: colors::DARK_SELECTED_BG,
        }
    }
}

impl ConfigValues {
    /// The static-member initialisers at the top of `Config.cpp`.
    pub fn initial(env: &Env) -> ConfigValues {
        ConfigValues {
            auto_compose: 0,
            space_as_page_down: 0,
            space_as_first_cand_selkey: 0,
            arrow_key_sw_pages: 1,
            clear_on_beep: 1,
            do_beep: 1,
            do_beep_notify: 1,
            do_beep_on_candi: 0,
            sentence_mode: 0,
            activated_keyboard_mode: 1,
            make_phrase: 1,
            show_notify_desktop: 1,
            do_han_convert: 0,
            font_italic: 0,
            custom_table_priority: 0,
            dayi_article_mode: 0,
            array_force_sp: 0,
            array_notify_sp: 1,
            array_single_quote_custom_phrase: 0,
            big5_filter: 0,
            reload_reverse_conversion: 0,
            app_permission_set: 0,
            load_table_mode: 0,
            color_mode_key_found: 0,
            max_codes: 4,
            font_size: 12,
            font_weight: FW_NORMAL,
            ime_shift_mode: 0,
            double_single_byte_mode: 1,
            shift_english_digit_mode: 0,
            color_mode: color_mode::SYSTEM,
            numeric_pad: 0,
            array_scope: 0,
            phonetic_keyboard_layout: 0,
            dayi_selkey_mode: 0,
            custom: Palette::light_defaults(env),
            light: Palette::light_defaults(env),
            dark: Palette::dark_defaults(),
            font_face_name: default_font_face(),
            reverse_conversion_clsid: [0; 16],
            reverse_conversion_guid_profile: [0; 16],
        }
    }

    /// `CConfig::SetIMEModeDefaults`: only the per-mode fields are touched.
    pub fn set_mode_defaults(&mut self, mode: ImeMode) {
        match mode {
            ImeMode::Array => {
                self.array_scope = 0;
                self.auto_compose = 1;
                self.max_codes = 5;
                self.space_as_page_down = 0;
                self.space_as_first_cand_selkey = 0;
            }
            ImeMode::Phonetic => {
                self.auto_compose = 0;
                self.max_codes = 4;
                self.space_as_page_down = 1;
                self.space_as_first_cand_selkey = 0;
                self.big5_filter = 0;
            }
            ImeMode::Dayi => {
                self.auto_compose = 0;
                self.max_codes = 4;
                self.space_as_page_down = 0;
                self.space_as_first_cand_selkey = 1;
                self.big5_filter = 0;
            }
            ImeMode::Generic | ImeMode::None => {
                self.auto_compose = 0;
                self.max_codes = 4;
                self.space_as_page_down = 0;
                self.space_as_first_cand_selkey = 0;
                self.big5_filter = 0;
            }
        }
    }

    /// The default colour mode for a fresh configuration.
    pub fn default_color_mode(env: &Env) -> i32 {
        if env.windows_1809_or_later != 0 {
            color_mode::SYSTEM
        } else {
            color_mode::LIGHT
        }
    }
}

/// The complete settings state: plain values plus the heap-allocated
/// reverse-conversion description (`nullptr` in C++ is `None`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Settings {
    pub v: ConfigValues,
    pub reverse_conversion_description: Option<Vec<u16>>,
}

impl Settings {
    pub fn initial(env: &Env) -> Settings {
        Settings { v: ConfigValues::initial(env), reverse_conversion_description: None }
    }

    /// `CConfig::ResetAllDefaults`.  Note that, like the C++ code, it leaves
    /// `ReloadReverseConversion`, `AppPermissionSet`, `LoadTableMode` and the
    /// reverse-conversion description untouched.
    pub fn reset_all_defaults(&mut self, mode: ImeMode, env: &Env) {
        let keep = self.v;
        let mut v = ConfigValues::initial(env);
        v.reload_reverse_conversion = keep.reload_reverse_conversion;
        v.app_permission_set = keep.app_permission_set;
        v.load_table_mode = keep.load_table_mode;
        v.color_mode_key_found = keep.color_mode_key_found;
        v.color_mode = ConfigValues::default_color_mode(env);
        v.set_mode_defaults(mode);
        self.v = v;
    }
}
