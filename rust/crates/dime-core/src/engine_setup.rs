//! Dictionary setup policy from `SetupCompositionProcesseorEngine.cpp`.
//!
//! Rust decides which file to copy, open, reload, or alias. The host performs
//! the filesystem and `CFile` / engine operations. Quirks kept on purpose:
//! program-files timestamps stick when a later file is missing, a newer
//! program-files table overwrites AppData, generic mode does not drop a
//! dictionary whose path changed, Array special-code and Ext-E replace the
//! engine without deleting the old one, and a phrase table that was the TTS
//! main table is deleted when `Phrase.cin` appears. `StringCchPrintf` width
//! is `MAX_PATH` (259 characters plus NUL).

/// `DICTIONARY_TYPE` (`dime_ffi.h`).
pub const DICT_TTS: i32 = 0;
pub const DICT_CIN: i32 = 2;

pub const MODE_DAYI: i32 = 0;
pub const MODE_ARRAY: i32 = 1;
pub const MODE_PHONETIC: i32 = 2;
pub const MODE_GENERIC: i32 = 3;

/// `ARRAY_SCOPE::ARRAY40_BIG5`.
pub const ARRAY40_BIG5: i32 = 4;

pub const KIND_MAIN: i32 = 0;
pub const KIND_CUSTOM: i32 = 1;
pub const KIND_PHRASE: i32 = 2;
pub const KIND_EXT_B: i32 = 3;
pub const KIND_EXT_CD: i32 = 4;
pub const KIND_EXT_E: i32 = 5;
pub const KIND_ARRAY_PHRASE: i32 = 6;
pub const KIND_SPECIAL: i32 = 7;
pub const KIND_SHORT: i32 = 8;
pub const KIND_TCSC: i32 = 9;
pub const KIND_TCFREQ: i32 = 10;

pub const MAX_RADICAL: usize = 96;
pub const MAX_PATH_CHARS: usize = 259;

const VK_SHIFT: u32 = 0x10;
const VK_SPACE: u32 = 0x20;
const VK_OEM_1: u32 = 0xBA;
const VK_OEM_PLUS: u32 = 0xBB;
const VK_OEM_COMMA: u32 = 0xBC;
const VK_OEM_MINUS: u32 = 0xBD;
const VK_OEM_PERIOD: u32 = 0xBE;
const VK_OEM_2: u32 = 0xBF;
const VK_OEM_3: u32 = 0xC0;
const VK_OEM_4: u32 = 0xDB;
const VK_OEM_5: u32 = 0xDC;
const VK_OEM_6: u32 = 0xDD;
const VK_OEM_7: u32 = 0xDE;
const TF_MOD_CONTROL: u32 = 0x0002;
const TF_MOD_SHIFT: u32 = 0x0004;
/// `_TF_MOD_ON_KEYUP_SHIFT_ONLY`.
const TF_MOD_ON_KEYUP_SHIFT_ONLY: u32 = 0x0001_0000 | 0x0200;

const FN_NONE: u32 = 0;
const FN_INPUT: u32 = 1;
const FN_INPUT_AND_CONVERT: u32 = 2;
const FN_CONVERT: u32 = 9;

/// `kind * 16 + slot`. Slot is 0 for tables that are not per input mode.
pub fn setup_id(kind: i32, slot: i32) -> i32 {
    kind * 16 + slot
}

pub trait SetupHost {
    fn path_exists(&mut self, path: &[u16]) -> bool;
    /// `Some` when `_wstat` succeeds. `None` leaves the caller's timestamp as it was.
    fn file_mtime(&mut self, path: &[u16]) -> Option<i64>;
    fn copy_file(&mut self, src: &[u16], dst: &[u16], fail_if_exists: bool);
    fn create_dir(&mut self, path: &[u16]);
    fn path_equal(&mut self, locale: u32, a: &[u16], b: &[u16]) -> bool;

    fn has_file(&mut self, id: i32) -> bool;
    fn has_engine(&mut self, id: i32) -> bool;
    /// -1 when there is no engine.
    fn dict_type(&mut self, id: i32) -> i32;
    fn file_updated(&mut self, id: i32) -> bool;
    fn file_name(&mut self, id: i32) -> Vec<u16>;
    fn drop_file(&mut self, id: i32);
    /// Deletes the engine object and stores null. Does not delete an aliased slot.
    fn drop_engine(&mut self, id: i32);
    /// `new CFile`. On failure the slot is null (a previous object is leaked, as in C++).
    fn new_file(&mut self, id: i32) -> bool;
    fn create_file(&mut self, id: i32, path: &[u16], share_mode: u32) -> bool;
    /// Assigns a new engine without deleting the previous one.
    fn create_engine(&mut self, id: i32, locale: u32, dict_type: i32) -> bool;
    fn parse_config(&mut self, id: i32, mode: i32);
    fn alias_phrase_to_main(&mut self, slot: i32);

    fn setup_big5(&mut self, mode: i32, locale: u32);
    fn setup_keystroke(&mut self, mode: i32);
    fn load_config(&mut self, mode: i32);
    fn setup_configuration(&mut self, mode: i32);
    fn setup_candidate_range(&mut self, mode: i32);
    fn has_text_service(&mut self) -> bool;
    fn array_scope(&mut self) -> i32;
    fn active_mode(&mut self) -> i32;
    fn do_han_convert(&mut self) -> bool;
}

fn join(base: &[u16], rel: &str) -> Vec<u16> {
    let mut v = base.to_vec();
    v.extend(rel.encode_utf16());
    if v.len() > MAX_PATH_CHARS {
        v.truncate(MAX_PATH_CHARS);
    }
    v
}

const SHARE_READ: u32 = 1;
const SHARE_READ_WRITE: u32 = 1 | 2;

/// Copy the program-files table over AppData when AppData is missing or older.
/// `pf_mtime` / `app_mtime` persist across tables: a missing program-files path
/// keeps the previous timestamp (`_wstat` is not called).
fn copy_if_shipped<H: SetupHost>(
    host: &mut H,
    pf: &[u16],
    app: &[u16],
    pf_mtime: &mut i64,
    app_mtime: &mut i64,
) {
    let mut wstat_failed = true;
    if host.path_exists(pf) {
        if let Some(t) = host.file_mtime(pf) {
            *pf_mtime = t;
        }
    }
    if host.path_exists(app) {
        match host.file_mtime(app) {
            Some(t) => {
                *app_mtime = t;
                wstat_failed = false;
            }
            None => wstat_failed = true,
        }
    }
    let updated = *pf_mtime > *app_mtime;
    if !host.path_exists(app) || (!wstat_failed && updated) {
        host.copy_file(pf, app, false);
    }
}

fn drop_if_renamed<H: SetupHost>(host: &mut H, id: i32, path: &[u16], locale: u32) {
    if host.path_exists(path) && host.has_file(id) {
        let existing = host.file_name(id);
        if !host.path_equal(locale, path, &existing) {
            host.drop_file(id);
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn open_new<H: SetupHost>(
    host: &mut H,
    id: i32,
    path: &[u16],
    locale: u32,
    mode: i32,
    dict_type: i32,
    share: u32,
    delete_engine: bool,
    parse: bool,
) {
    if !host.has_file(id) {
        if host.new_file(id) && host.create_file(id, path, share) {
            if delete_engine && host.has_engine(id) {
                host.drop_engine(id);
            }
            if host.create_engine(id, locale, dict_type) && parse {
                host.parse_config(id, mode);
            }
        }
    } else if host.has_engine(id) && host.file_updated(id) {
        host.parse_config(id, mode);
    }
}

pub struct Roots<'a> {
    pub app: &'a [u16],
    pub pf: &'a [u16],
    pub locale: u32,
    pub mode: i32,
    pub settings_build: bool,
}

#[allow(clippy::too_many_arguments)]
fn load_shipped_aux<H: SetupHost>(
    host: &mut H,
    roots: &Roots<'_>,
    id: i32,
    file_name: &str,
    pf_mtime: &mut i64,
    app_mtime: &mut i64,
    drop_on_rename: bool,
    delete_engine: bool,
) {
    let app_path = join(roots.app, &format!("\\DIME\\{file_name}"));
    let pf_path = join(roots.pf, &format!("\\DIME\\{file_name}"));
    copy_if_shipped(host, &pf_path, &app_path, pf_mtime, app_mtime);
    let chosen = if host.path_exists(&app_path) {
        if drop_on_rename {
            drop_if_renamed(host, id, &app_path, roots.locale);
        }
        app_path
    } else {
        pf_path
    };
    if host.path_exists(&chosen) {
        open_new(
            host,
            id,
            &chosen,
            roots.locale,
            roots.mode,
            DICT_CIN,
            SHARE_READ,
            delete_engine,
            true,
        );
    }
}

/// `SetupDictionaryFile`. `true` is the C++ `TRUE` success return.
pub fn setup_dictionary<H: SetupHost>(host: &mut H, roots: &Roots<'_>) -> bool {
    let mode = roots.mode;
    let main = setup_id(KIND_MAIN, mode);
    let dime_dir = join(roots.app, "\\DIME");
    if !host.path_exists(&dime_dir) {
        host.create_dir(&dime_dir);
    }

    let tts_name = if mode != MODE_ARRAY {
        "\\DIME\\TableTextServiceDaYi.txt"
    } else {
        "\\DIME\\TableTextServiceArray.txt"
    };
    let tts_app = join(roots.app, tts_name);
    let tts_pf = join(roots.pf, &format!("\\Windows NT\\TableTextService\\{}", &tts_name[6..]));

    let mut pf_mtime = 0i64;
    let mut app_mtime = 0i64;
    let mut cin = join(roots.app, "\\DIME");

    if mode == MODE_DAYI {
        cin = join(roots.app, "\\DIME\\Dayi.cin");
        drop_if_renamed(host, main, &cin, roots.locale);
    } else if mode == MODE_ARRAY {
        let name = if host.array_scope() == ARRAY40_BIG5 { "Array40.cin" } else { "Array.cin" };
        cin = join(roots.app, &format!("\\DIME\\{name}"));
        let pf = join(roots.pf, &format!("\\DIME\\{name}"));
        copy_if_shipped(host, &pf, &cin, &mut pf_mtime, &mut app_mtime);
        if !host.path_exists(&cin) {
            cin = pf;
        }
        drop_if_renamed(host, main, &cin, roots.locale);
    } else if mode == MODE_PHONETIC {
        cin = join(roots.app, "\\DIME\\Phone.cin");
        let pf = join(roots.pf, "\\DIME\\Phone.cin");
        copy_if_shipped(host, &pf, &cin, &mut pf_mtime, &mut app_mtime);
        if !host.path_exists(&cin) {
            cin = pf;
        }
        drop_if_renamed(host, main, &cin, roots.locale);
    } else if mode == MODE_GENERIC {
        cin = join(roots.app, "\\DIME\\Generic.cin");
    }

    let mut big5_rebuilt = false;
    if host.path_exists(&cin) {
        if !host.has_file(main) {
            if !(host.new_file(main) && host.create_file(main, &cin, SHARE_READ)) {
                host.drop_file(main);
                return false;
            }
            if host.has_engine(main) {
                host.drop_engine(main);
            }
            let _ = host.create_engine(main, roots.locale, DICT_CIN);
            host.parse_config(main, mode);
            host.setup_big5(mode, roots.locale);
            big5_rebuilt = true;
        } else if host.has_engine(main) && host.file_updated(main) {
            host.parse_config(main, mode);
            host.setup_big5(mode, roots.locale);
            big5_rebuilt = true;
            let active = host.active_mode();
            host.setup_keystroke(active);
            host.load_config(active);
            host.setup_configuration(active);
            if !roots.settings_build {
                host.setup_candidate_range(active);
            }
        }
    }

    if !host.has_engine(main) && (mode == MODE_DAYI || mode == MODE_ARRAY) {
        if !host.new_file(main) {
            return false;
        }
        if !host.path_exists(&tts_app) {
            let src = if mode != MODE_ARRAY {
                join(roots.pf, "\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt")
            } else {
                join(roots.pf, "\\Windows NT\\TableTextService\\TableTextServiceArray.txt")
            };
            host.copy_file(&src, &tts_app, true);
        }
        if !host.create_file(main, &tts_app, SHARE_READ) {
            return false;
        }
        if !host.create_engine(main, roots.locale, DICT_TTS) {
            return false;
        }
        host.parse_config(main, mode);
        host.setup_big5(mode, roots.locale);
        big5_rebuilt = true;
    }

    if !big5_rebuilt && host.has_engine(main) {
        host.setup_big5(mode, roots.locale);
    }

    let phrase = setup_id(KIND_PHRASE, 0);
    let phrase_path = join(roots.app, "\\DIME\\Phrase.cin");
    if host.path_exists(&phrase_path) {
        let replace = !host.has_file(phrase)
            || (host.has_engine(phrase) && host.dict_type(phrase) == DICT_TTS);
        if replace {
            if host.has_file(phrase) {
                host.drop_file(phrase);
            }
            if host.new_file(phrase) && host.create_file(phrase, &phrase_path, SHARE_READ) {
                if host.has_engine(phrase) {
                    host.drop_engine(phrase);
                }
                if !host.create_engine(phrase, roots.locale, DICT_CIN) {
                    return false;
                }
                host.parse_config(phrase, mode);
            }
        } else if host.file_updated(phrase) {
            host.parse_config(phrase, mode);
        }
    } else if !host.has_engine(phrase)
        && (mode == MODE_DAYI || mode == MODE_ARRAY)
        && host.has_file(main)
        && host.has_engine(main)
        && host.dict_type(main) == DICT_TTS
    {
        host.alias_phrase_to_main(mode);
    } else if !host.has_engine(phrase) {
        if !host.new_file(phrase) {
            return false;
        }
        if !host.path_exists(&tts_app) {
            host.copy_file(&tts_pf, &tts_app, true);
        }
        if !host.create_file(phrase, &tts_app, SHARE_READ) {
            return false;
        }
        if host.has_engine(phrase) {
            host.drop_engine(phrase);
        }
        if !host.create_engine(phrase, roots.locale, DICT_TTS) {
            return false;
        }
        host.parse_config(phrase, mode);
    }

    if mode == MODE_ARRAY && host.array_scope() != ARRAY40_BIG5 {
        let aux = [
            (KIND_EXT_B, "Array-Ext-B.cin", true, true),
            (KIND_EXT_CD, "Array-Ext-CD.cin", true, true),
            (KIND_EXT_E, "Array-Ext-EF.cin", true, false),
            (KIND_ARRAY_PHRASE, "Array-Phrase.cin", false, true),
            (KIND_SPECIAL, "Array-special.cin", true, false),
            (KIND_SHORT, "Array-shortcode.cin", true, true),
        ];
        for (kind, name, drop_on_rename, delete_engine) in aux {
            load_shipped_aux(
                host,
                roots,
                setup_id(kind, 0),
                name,
                &mut pf_mtime,
                &mut app_mtime,
                drop_on_rename,
                delete_engine,
            );
        }
    }

    let custom_rel = if mode == MODE_DAYI {
        "\\DIME\\DAYI-CUSTOM.cin"
    } else {
        match host.active_mode() {
            MODE_ARRAY => "\\DIME\\ARRAY-CUSTOM.cin",
            MODE_PHONETIC => "\\DIME\\PHONETIC-CUSTOM.cin",
            _ => "\\DIME\\GENERIC-CUSTOM.cin",
        }
    };
    let custom_path = join(roots.app, custom_rel);
    let custom = setup_id(KIND_CUSTOM, mode);
    if host.path_exists(&custom_path) {
        if !host.has_engine(custom) {
            if host.new_file(custom) && host.create_file(custom, &custom_path, SHARE_READ_WRITE) {
                if host.has_engine(custom) {
                    host.drop_engine(custom);
                }
                if host.create_engine(custom, roots.locale, DICT_CIN) {
                    host.parse_config(custom, mode);
                }
            }
        } else if host.file_updated(custom) {
            host.parse_config(custom, mode);
        }
    }
    true
}

/// `SetupHanCovertTable`. Returns false when conversion is off or the table is already loaded.
pub fn setup_han_convert<H: SetupHost>(host: &mut H, locale: u32, app: &[u16], pf: &[u16]) -> bool {
    let id = setup_id(KIND_TCSC, 0);
    if !(host.do_han_convert() && !host.has_engine(id)) {
        return false;
    }
    let app_path = join(app, "\\DIME\\TCSC.cin");
    let pf_path = join(pf, "\\DIME\\TCSC.cin");
    if host.path_exists(&pf_path) && !host.path_exists(&app_path) {
        host.copy_file(&pf_path, &app_path, true);
    }
    let chosen = if host.path_exists(&app_path) { app_path } else { pf_path };
    if !host.has_file(id) && host.new_file(id) && host.has_text_service() {
        let _ = host.create_file(id, &chosen, SHARE_READ)
            && host.create_engine(id, locale, DICT_CIN);
    }
    true
}

/// `SetupTCFreqTable`. Returns true unless the C++ path-buffer allocation failed
/// (there is no such allocation here).
pub fn setup_tc_freq<H: SetupHost>(host: &mut H, locale: u32, app: &[u16], pf: &[u16]) -> bool {
    let id = setup_id(KIND_TCFREQ, 0);
    let app_path = join(app, "\\DIME\\TCFreq.cin");
    let pf_path = join(pf, "\\DIME\\TCFreq.cin");
    if host.path_exists(&pf_path) && !host.path_exists(&app_path) {
        host.copy_file(&pf_path, &app_path, true);
    }
    let chosen = if host.path_exists(&app_path) { app_path } else { pf_path };
    if !host.has_file(id) && host.new_file(id) && host.has_text_service() {
        let _ = host.create_file(id, &chosen, SHARE_READ)
            && host.create_engine(id, locale, DICT_CIN);
    }
    true
}

/// One `_KEYSTROKE`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Keystroke {
    pub index: u32,
    pub printable: u16,
    pub cand_index: u16,
    pub virtual_key: u32,
    pub modifiers: u32,
    pub function: u32,
}

impl Keystroke {
    fn blank(index: u32) -> Self {
        Self { index, printable: 0, cand_index: 0, virtual_key: 0, modifiers: 0, function: FN_NONE }
    }
}

/// `GetVKeyFromPrintable`. Letters outside `A`-`Z` keep their own code as the virtual key.
pub fn vkey_from_printable(printable: u16) -> (u32, u32) {
    let c = printable as u32;
    if printable > 127 {
        return (c, 0);
    }
    let ascii = printable as u8;
    let shift = |v: u32| (v, TF_MOD_SHIFT);
    if ascii.is_ascii_digit() || ascii.is_ascii_uppercase() {
        return (c, 0);
    }
    match ascii {
        b'!' => shift(b'1' as u32),
        b'@' => shift(b'2' as u32),
        b'#' => shift(b'3' as u32),
        b'$' => shift(b'4' as u32),
        b'%' => shift(b'5' as u32),
        b'^' => shift(b'6' as u32),
        b'&' => shift(b'7' as u32),
        b'*' => shift(b'8' as u32),
        b'(' => shift(b'9' as u32),
        b')' => shift(b'0' as u32),
        b',' => (VK_OEM_COMMA, 0),
        b'<' => shift(VK_OEM_COMMA),
        b'.' => (VK_OEM_PERIOD, 0),
        b'>' => shift(VK_OEM_PERIOD),
        b';' => (VK_OEM_1, 0),
        b':' => shift(VK_OEM_1),
        b'/' => (VK_OEM_2, 0),
        b'?' => shift(VK_OEM_2),
        b'`' => (VK_OEM_3, 0),
        b'~' => shift(VK_OEM_3),
        b'[' => (VK_OEM_4, 0),
        b'{' => shift(VK_OEM_4),
        b'\\' => (VK_OEM_5, 0),
        b'|' => shift(VK_OEM_5),
        b']' => (VK_OEM_6, 0),
        b'}' => shift(VK_OEM_6),
        b'\'' => (VK_OEM_7, 0),
        b'"' => shift(VK_OEM_7),
        b'-' => (VK_OEM_MINUS, 0),
        b'_' => shift(VK_OEM_MINUS),
        b'=' => (VK_OEM_PLUS, 0),
        b'+' => shift(VK_OEM_PLUS),
        _ => (c, 0),
    }
}

fn wchar_upper(c: u16) -> u16 {
    if c < 128 { (c as u8).to_ascii_uppercase() as u16 } else { c }
}

/// `SetupKeystroke` after the dictionary-available checks. `inserted_equal` is
/// the Dayi prompt key inserted into the radical map.
pub fn build_keystrokes(mode: i32, radicals: &[u16], endkey: &[u16]) -> (Vec<Keystroke>, bool) {
    let mut keys: Vec<u16> = radicals.to_vec();
    let inserted_equal = mode == MODE_DAYI && !keys.contains(&('=' as u16));
    if inserted_equal {
        keys.push('=' as u16);
    }
    keys.sort_unstable();
    keys.dedup();

    let mut table = (0..=MAX_RADICAL).map(|i| Keystroke::blank(i as u32)).collect::<Vec<_>>();
    for key in keys {
        let c = wchar_upper(key);
        if c < 32 || c > (MAX_RADICAL as u16) + 32 {
            continue;
        }
        let slot = (c as usize) - 32;
        if slot >= table.len() {
            break;
        }
        let (vkey, modifiers) = vkey_from_printable(key);
        table[slot].function = FN_INPUT;
        table[slot].printable = key;
        table[slot].virtual_key = vkey;
        table[slot].modifiers = modifiers;
    }
    for &c in endkey {
        if c < 32 || c > (MAX_RADICAL as u16) + 32 {
            continue;
        }
        let slot = (c as usize) - 32;
        if slot >= table.len() {
            break;
        }
        if table[slot].function == FN_INPUT {
            table[slot].function = FN_INPUT_AND_CONVERT;
        } else {
            table[slot].function = FN_CONVERT;
        }
    }
    (table, inserted_equal)
}

const DAYI_SELKEY: &str = "'[]-\\\"{}_";
const DIGIT_SELKEY: &str = "1234567890";
const PHRASE_SELKEY: &str = "!@#$%^&*()";

/// `DAYI_SELKEY_MODE`.
pub const SELKEY_FROM_TABLE: i32 = 0;
pub const SELKEY_NUMERIC: i32 = 1;
pub const SELKEY_DAYI: i32 = 2;

/// `SetupCandidateListRange`. The page-size cap follows the C++ dangling else:
/// a table `%selkey` is capped at 10 only when space is the first select key.
pub fn build_candidate_range(
    mode: i32,
    global_mode: i32,
    dayi_selkey_mode: i32,
    space_as_first: bool,
    array_scope: i32,
    selkey: &[u16],
) -> (u32, Vec<Keystroke>, Vec<Keystroke>) {
    let mut space_first = space_as_first;
    let dayi_mode = if mode == MODE_DAYI { dayi_selkey_mode } else { SELKEY_FROM_TABLE };
    let owned: Vec<u16>;
    let (sel, page) = if dayi_mode == SELKEY_NUMERIC {
        owned = DIGIT_SELKEY.encode_utf16().collect();
        space_first = false;
        (owned.as_slice(), owned.len() as u32)
    } else if dayi_mode == SELKEY_DAYI {
        owned = DAYI_SELKEY.encode_utf16().collect();
        let page = owned.len() as u32 + if space_first { 1 } else { 0 };
        (owned.as_slice(), page)
    } else if selkey.is_empty() {
        owned = if mode == MODE_DAYI {
            DAYI_SELKEY.encode_utf16().collect()
        } else {
            DIGIT_SELKEY.encode_utf16().collect()
        };
        let page = if mode == MODE_PHONETIC { 9 } else { 10 };
        (owned.as_slice(), page)
    } else {
        // Dangling else: the `else if (page > 10)` belongs to the inner `if`.
        let mut page = selkey.len() as u32;
        if space_first && page + 1 > 10 {
            page = 10;
        }
        (selkey, page)
    };

    let phrase_keys: Vec<u16> = PHRASE_SELKEY.encode_utf16().collect();
    let array40 = global_mode == MODE_ARRAY && array_scope == ARRAY40_BIG5;
    let mut cand = Vec::new();
    let mut phrase = Vec::new();
    for i in 0..page {
        let mut ck = Keystroke::blank(0);
        if space_first {
            if i == 0 {
                ck.printable = b' ' as u16;
                ck.cand_index = b' ' as u16;
                ck.virtual_key = VK_SPACE;
            } else {
                let ch = sel[(i as usize) - 1];
                let (vkey, modifiers) = vkey_from_printable(ch);
                ck.printable = ch;
                ck.cand_index = ch;
                ck.virtual_key = vkey;
                ck.modifiers = modifiers;
            }
            ck.index = i;
        } else {
            let ch = sel[i as usize];
            let (vkey, modifiers) = vkey_from_printable(ch);
            ck.printable = ch;
            ck.cand_index = if array40 { (i + 1 + 0x30) as u16 } else { ch };
            ck.virtual_key = vkey;
            ck.modifiers = modifiers;
            ck.index = if i != 9 { i + 1 } else { 0 };
        }
        cand.push(ck);

        let mut pk = Keystroke::blank(0);
        let pch = phrase_keys.get(i as usize).copied().unwrap_or(0);
        let (vkey, _) = vkey_from_printable(pch);
        pk.printable = pch;
        pk.virtual_key = vkey;
        pk.modifiers = TF_MOD_SHIFT;
        if i != 9 {
            pk.index = i + 1;
            pk.cand_index = (i + 1 + 0x30) as u16;
        } else {
            pk.cand_index = b'0' as u16;
            pk.index = 0;
        }
        phrase.push(pk);
    }
    (page, cand, phrase)
}

/// Preserved-key list. `description == None` means the C++ resource string
/// (`ImeModeDescription` / `DoubleSingleByteDescription`).
#[derive(Clone, Copy)]
pub struct PreservedKey {
    pub id: u32,
    pub vkey: u32,
    pub modifiers: u32,
    pub description: Option<&'static str>,
}

pub fn preserved_keys() -> [PreservedKey; 4] {
    [
        PreservedKey { id: 0, vkey: VK_SHIFT, modifiers: TF_MOD_ON_KEYUP_SHIFT_ONLY, description: None },
        PreservedKey { id: 1, vkey: VK_SPACE, modifiers: TF_MOD_SHIFT, description: None },
        PreservedKey { id: 2, vkey: VK_OEM_5, modifiers: TF_MOD_CONTROL, description: Some("Show Config Pages") },
        PreservedKey {
            id: 3,
            vkey: VK_OEM_5,
            modifiers: TF_MOD_CONTROL | TF_MOD_SHIFT,
            description: Some("Toggle Simplified/Traditional"),
        },
    ]
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    struct Slot {
        file: bool,
        name: Vec<u16>,
        engine: bool,
        dict: i32,
        updated: bool,
    }

    struct Mem {
        files: BTreeMap<Vec<u16>, i64>,
        mtime_fail: BTreeMap<Vec<u16>, bool>,
        slots: BTreeMap<i32, Slot>,
        copies: Vec<(Vec<u16>, Vec<u16>, bool)>,
        dirs: Vec<Vec<u16>>,
        calls: Vec<&'static str>,
        scope: i32,
        active: i32,
        han: bool,
        text_service: bool,
        leaked_engines: u32,
    }

    impl Mem {
        fn new() -> Self {
            Self {
                files: BTreeMap::new(),
                mtime_fail: BTreeMap::new(),
                slots: BTreeMap::new(),
                copies: Vec::new(),
                dirs: Vec::new(),
                calls: Vec::new(),
                scope: 0,
                active: MODE_DAYI,
                han: false,
                text_service: true,
                leaked_engines: 0,
            }
        }

        fn slot(&mut self, id: i32) -> &mut Slot {
            self.slots.entry(id).or_insert(Slot {
                file: false,
                name: Vec::new(),
                engine: false,
                dict: -1,
                updated: false,
            })
        }
    }

    impl SetupHost for Mem {
        fn path_exists(&mut self, path: &[u16]) -> bool { self.files.contains_key(path) }
        fn file_mtime(&mut self, path: &[u16]) -> Option<i64> {
            if self.mtime_fail.get(path) == Some(&true) { return None; }
            self.files.get(path).copied()
        }
        fn copy_file(&mut self, src: &[u16], dst: &[u16], fail_if_exists: bool) {
            self.copies.push((src.to_vec(), dst.to_vec(), fail_if_exists));
            // `CopyFile` fails when the source is missing and leaves the destination alone.
            let Some(&t) = self.files.get(src) else { return };
            if fail_if_exists && self.files.contains_key(dst) { return; }
            self.files.insert(dst.to_vec(), t);
        }
        fn create_dir(&mut self, path: &[u16]) { self.dirs.push(path.to_vec()); }
        fn path_equal(&mut self, _locale: u32, a: &[u16], b: &[u16]) -> bool {
            a.len() == b.len()
                && a.iter().zip(b).all(|(x, y)| wchar_upper(*x) == wchar_upper(*y))
        }
        fn has_file(&mut self, id: i32) -> bool { self.slots.get(&id).is_some_and(|s| s.file) }
        fn has_engine(&mut self, id: i32) -> bool { self.slots.get(&id).is_some_and(|s| s.engine) }
        fn dict_type(&mut self, id: i32) -> i32 { self.slots.get(&id).map(|s| s.dict).unwrap_or(-1) }
        fn file_updated(&mut self, id: i32) -> bool { self.slots.get(&id).is_some_and(|s| s.updated) }
        fn file_name(&mut self, id: i32) -> Vec<u16> { self.slots.get(&id).map(|s| s.name.clone()).unwrap_or_default() }
        fn drop_file(&mut self, id: i32) {
            if let Some(s) = self.slots.get_mut(&id) { s.file = false; s.name.clear(); }
        }
        fn drop_engine(&mut self, id: i32) {
            if let Some(s) = self.slots.get_mut(&id) {
                s.engine = false;
                s.dict = -1;
            }
        }
        fn new_file(&mut self, id: i32) -> bool {
            let s = self.slot(id);
            if s.file { /* leak, matching `p = new` */ }
            s.file = true;
            true
        }
        fn create_file(&mut self, id: i32, path: &[u16], _share: u32) -> bool {
            if !self.files.contains_key(path) { return false; }
            let s = self.slot(id);
            s.file = true;
            s.name = path.to_vec();
            true
        }
        fn create_engine(&mut self, id: i32, _locale: u32, dict_type: i32) -> bool {
            let already = self.slots.get(&id).is_some_and(|s| s.engine);
            if already {
                self.leaked_engines += 1;
            }
            let s = self.slot(id);
            s.engine = true;
            s.dict = dict_type;
            true
        }
        fn parse_config(&mut self, _id: i32, _mode: i32) { self.calls.push("parse"); }
        fn alias_phrase_to_main(&mut self, slot: i32) {
            let main = setup_id(KIND_MAIN, slot);
            let dict = self.dict_type(main);
            let s = self.slot(setup_id(KIND_PHRASE, 0));
            s.engine = true;
            s.dict = dict;
            self.calls.push("alias");
        }
        fn setup_big5(&mut self, _mode: i32, _locale: u32) { self.calls.push("big5"); }
        fn setup_keystroke(&mut self, _mode: i32) { self.calls.push("keystroke"); }
        fn load_config(&mut self, _mode: i32) { self.calls.push("load"); }
        fn setup_configuration(&mut self, _mode: i32) { self.calls.push("config"); }
        fn setup_candidate_range(&mut self, _mode: i32) { self.calls.push("range"); }
        fn has_text_service(&mut self) -> bool { self.text_service }
        fn array_scope(&mut self) -> i32 { self.scope }
        fn active_mode(&mut self) -> i32 { self.active }
        fn do_han_convert(&mut self) -> bool { self.han }
    }

    fn roots<'a>(app: &'a [u16], pf: &'a [u16], mode: i32) -> Roots<'a> {
        Roots { app, pf, locale: 0x0404, mode, settings_build: false }
    }

    #[test]
    fn array_copies_when_program_files_is_newer() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Array.cin"), 10);
        m.files.insert(u("C:\\pf\\DIME\\Array.cin"), 20);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        assert!(setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY)));
        assert_eq!(m.files.get(&u("C:\\app\\DIME\\Array.cin")), Some(&20));
        assert!(m.has_engine(setup_id(KIND_MAIN, MODE_ARRAY)));
        assert_eq!(m.dict_type(setup_id(KIND_MAIN, MODE_ARRAY)), DICT_CIN);
        assert!(!m.calls.contains(&"keystroke"));
    }

    #[test]
    fn array_keeps_newer_appdata() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Array.cin"), 50);
        m.files.insert(u("C:\\pf\\DIME\\Array.cin"), 20);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        assert_eq!(m.files.get(&u("C:\\app\\DIME\\Array.cin")), Some(&50));
        assert!(m.copies.is_empty() || m.copies.iter().all(|c| c.1 != u("C:\\app\\DIME\\Array.cin")));
    }

    #[test]
    fn dayi_does_not_copy_program_files() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Dayi.cin"), 1);
        m.files.insert(u("C:\\pf\\DIME\\Dayi.cin"), 99);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        assert!(m.copies.is_empty());
        assert_eq!(m.file_name(setup_id(KIND_MAIN, MODE_DAYI)), u("C:\\app\\DIME\\Dayi.cin"));
    }

    #[test]
    fn missing_cin_falls_back_to_tts_for_dayi() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\pf\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt"), 3);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        assert_eq!(m.dict_type(setup_id(KIND_MAIN, MODE_DAYI)), DICT_TTS);
        assert!(m.calls.contains(&"alias"));
        assert_eq!(m.dict_type(setup_id(KIND_PHRASE, 0)), DICT_TTS);
    }

    #[test]
    fn phrase_cin_replaces_tts_alias() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\pf\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt"), 3);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 4);
        m.calls.clear();
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        assert_eq!(m.dict_type(setup_id(KIND_PHRASE, 0)), DICT_CIN);
        assert!(!m.calls.contains(&"alias"));
    }

    #[test]
    fn generic_loads_tts_phrase_separately() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Generic.cin"), 1);
        m.files.insert(u("C:\\pf\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt"), 2);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_GENERIC));
        assert_eq!(m.dict_type(setup_id(KIND_MAIN, MODE_GENERIC)), DICT_CIN);
        assert_eq!(m.dict_type(setup_id(KIND_PHRASE, 0)), DICT_TTS);
        assert!(!m.calls.contains(&"alias"));
    }

    #[test]
    fn array40_skips_extension_tables() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.scope = ARRAY40_BIG5;
        m.files.insert(u("C:\\pf\\DIME\\Array40.cin"), 5);
        m.files.insert(u("C:\\pf\\DIME\\Array-Ext-B.cin"), 5);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        assert!(m.has_engine(setup_id(KIND_MAIN, MODE_ARRAY)));
        assert!(!m.has_engine(setup_id(KIND_EXT_B, 0)));
        assert_eq!(m.file_name(setup_id(KIND_MAIN, MODE_ARRAY)), u("C:\\app\\DIME\\Array40.cin"));
    }

    #[test]
    fn renamed_table_drops_the_open_file() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.scope = ARRAY40_BIG5;
        m.files.insert(u("C:\\app\\DIME\\Array40.cin"), 5);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        m.scope = 0;
        m.files.insert(u("C:\\app\\DIME\\Array.cin"), 6);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        assert_eq!(m.file_name(setup_id(KIND_MAIN, MODE_ARRAY)), u("C:\\app\\DIME\\Array.cin"));
    }

    #[test]
    fn updated_main_table_reloads_keystroke_and_range() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Dayi.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        m.slot(setup_id(KIND_MAIN, MODE_DAYI)).updated = true;
        m.calls.clear();
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        assert_eq!(m.calls, ["parse", "big5", "keystroke", "load", "config", "range"]);
    }

    #[test]
    fn settings_build_skips_candidate_range_on_reload() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\app\\DIME\\Dayi.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_DAYI));
        m.slot(setup_id(KIND_MAIN, MODE_DAYI)).updated = true;
        m.calls.clear();
        let mut r = roots(&app, &pf, MODE_DAYI);
        r.settings_build = true;
        setup_dictionary(&mut m, &r);
        assert!(!m.calls.contains(&"range"));
        assert!(m.calls.contains(&"keystroke"));
    }

    #[test]
    fn custom_table_path_follows_the_active_mode() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.active = MODE_PHONETIC;
        m.files.insert(u("C:\\app\\DIME\\Generic.cin"), 1);
        m.files.insert(u("C:\\app\\DIME\\PHONETIC-CUSTOM.cin"), 1);
        m.files.insert(u("C:\\pf\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_GENERIC));
        assert!(m.has_engine(setup_id(KIND_CUSTOM, MODE_GENERIC)));
        assert_eq!(m.file_name(setup_id(KIND_CUSTOM, MODE_GENERIC)), u("C:\\app\\DIME\\PHONETIC-CUSTOM.cin"));
    }

    #[test]
    fn special_code_replace_leaks_the_previous_engine() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.files.insert(u("C:\\pf\\DIME\\Array.cin"), 1);
        m.files.insert(u("C:\\pf\\DIME\\Array-special.cin"), 1);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        let id = setup_id(KIND_SPECIAL, 0);
        m.files.insert(u("C:\\app\\DIME\\Array-special.cin"), 1);
        // Force the "file pointer null, engine still there" reload by dropping the file only.
        m.drop_file(id);
        assert!(m.has_engine(id));
        m.leaked_engines = 0;
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        assert_eq!(m.leaked_engines, 1);
    }

    #[test]
    fn stale_program_files_mtime_is_kept() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        // Main program file is newer than a pre-existing ext table, then the ext
        // program file is absent so its timestamp stays at the main file's mtime.
        m.files.insert(u("C:\\pf\\DIME\\Array.cin"), 100);
        m.files.insert(u("C:\\app\\DIME\\Array-Ext-B.cin"), 50);
        m.files.insert(u("C:\\app\\DIME\\Phrase.cin"), 1);
        setup_dictionary(&mut m, &roots(&app, &pf, MODE_ARRAY));
        let ext = u("C:\\app\\DIME\\Array-Ext-B.cin");
        let ext_pf = u("C:\\pf\\DIME\\Array-Ext-B.cin");
        assert!(m.copies.iter().any(|c| c.0 == ext_pf && c.1 == ext), "stale mtime still attempts the copy");
        assert_eq!(m.files.get(&ext), Some(&50), "missing source leaves AppData unchanged");
    }

    #[test]
    fn han_convert_copies_only_when_missing_and_enabled() {
        let app = u("C:\\app");
        let pf = u("C:\\pf");
        let mut m = Mem::new();
        m.han = false;
        m.files.insert(u("C:\\pf\\DIME\\TCSC.cin"), 4);
        assert!(!setup_han_convert(&mut m, 0, &app, &pf));
        assert!(m.copies.is_empty());
        m.han = true;
        assert!(setup_han_convert(&mut m, 0, &app, &pf));
        assert!(m.has_engine(setup_id(KIND_TCSC, 0)));
        assert!(!setup_han_convert(&mut m, 0, &app, &pf), "already loaded returns false");
    }

    #[test]
    fn dayi_keystroke_inserts_equal_and_endkey_converts() {
        // Endkey is indexed by the raw code, not towupper, so 'A' hits the slot of radical 'a'.
        let (table, inserted) = build_keystrokes(MODE_DAYI, &['a' as u16, ' ' as u16], &['A' as u16]);
        assert!(inserted);
        assert_eq!(table.len(), MAX_RADICAL + 1);
        let eq = table[('=' as usize) - 32];
        assert_eq!(eq.function, FN_INPUT);
        assert_eq!(eq.virtual_key, VK_OEM_PLUS);
        let a = table[('A' as usize) - 32];
        assert_eq!(a.function, FN_INPUT_AND_CONVERT);
        assert_eq!(a.printable, 'a' as u16);
        assert_eq!(a.virtual_key, 'a' as u32, "lowercase is not folded into the virtual key");
    }

    #[test]
    fn candidate_range_dangling_else_and_array40_label() {
        let digits = DIGIT_SELKEY.encode_utf16().collect::<Vec<_>>();
        let (page, cand, phrase) = build_candidate_range(MODE_ARRAY, MODE_ARRAY, SELKEY_FROM_TABLE, false, ARRAY40_BIG5, &digits);
        assert_eq!(page, 10);
        assert_eq!(cand[0].cand_index, b'1' as u16);
        assert_eq!(cand[9].cand_index, (10 + 0x30) as u16);
        assert_eq!(cand[9].index, 0);
        assert_eq!(phrase[0].printable, b'!' as u16);
        assert_eq!(phrase[0].modifiers, TF_MOD_SHIFT);
        assert_eq!(phrase[0].virtual_key, b'1' as u32);

        let long: Vec<u16> = "1234567890ab".encode_utf16().collect();
        let (page, _, _) = build_candidate_range(MODE_GENERIC, MODE_GENERIC, 0, false, 0, &long);
        assert_eq!(page, 12, "without space-first the page size is not capped");
        let (page, _, _) = build_candidate_range(MODE_GENERIC, MODE_GENERIC, 0, true, 0, &long);
        assert_eq!(page, 10);
    }

    #[test]
    fn preserved_key_list_matches_the_cpp_constants() {
        let keys = preserved_keys();
        assert_eq!(keys[0].vkey, VK_SHIFT);
        assert_eq!(keys[0].modifiers, 0x0001_0200);
        assert_eq!(keys[1].vkey, VK_SPACE);
        assert_eq!(keys[2].vkey, VK_OEM_5);
        assert_eq!(keys[2].modifiers, TF_MOD_CONTROL);
        assert_eq!(keys[2].description, Some("Show Config Pages"));
        assert_eq!(keys[3].modifiers, TF_MOD_CONTROL | TF_MOD_SHIFT);
    }
}
