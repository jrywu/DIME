//! Phase 5a: headless CLI (`ParseCLIArgs` / `RunCLI` in `DIMESettings/CLI.cpp`).
//!
//! Argument splitting stays in C++ (`CommandLineToArgvW`) so quoting matches.
//! This module walks that argv, formats every message, and decides the exit
//! code. File IO, CIN import, and `CConfig` load/save stay behind [`CliHost`]
//! because `DIMESettings` still uses the C++ dictionary engine.
//!
//! Host callbacks must not throw and must not call back into [`parse`] or
//! [`run`]. Pointers passed into a callback are valid only for that call.
//! `HELP` is the text of `HandleHelp`; the escapes keep it independent of
//! the source file's newline style.

use dime_core::text::w;

use crate::keys::{self, KeyInfo};
use crate::values::ImeMode;

/// `CLI_COMMAND` in `CLI.h`.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    None = 0,
    Get = 1,
    GetAll = 2,
    Set = 3,
    Reset = 4,
    ImportCustom = 5,
    ExportCustom = 6,
    LoadMain = 7,
    LoadPhrase = 8,
    LoadArray = 9,
    ListModes = 10,
    ListKeys = 11,
    Help = 12,
}

const MAX_SET: usize = 32;
const KEY_CHARS: usize = 63;
const VALUE_CHARS: usize = 255;
const PATH_CHARS: usize = 259;

const TABLE_SP: i32 = 0;
const TABLE_SC: i32 = 1;
const TABLE_EXT_B: i32 = 2;
const TABLE_EXT_CD: i32 = 3;
const TABLE_EXT_EFG: i32 = 4;
const TABLE_ARRAY40: i32 = 5;
const TABLE_PHRASE: i32 = 6;

/// `StringCchCopyW` into a fixed buffer: keep at most `max_chars` units.
fn trunc(s: &[u16], max_chars: usize) -> Vec<u16> {
    s.iter().copied().take(max_chars).collect()
}

fn fill_z(dst: &mut [u16], src: &[u16]) {
    let n = src.len().min(dst.len().saturating_sub(1));
    dst[..n].copy_from_slice(&src[..n]);
    if let Some(slot) = dst.get_mut(n) {
        *slot = 0;
    }
}

/// `_wcsicmp` for ASCII command tokens: only A–Z fold.
fn eq_ci(a: &[u16], b: &str) -> bool {
    let fold = |c: u16| if (0x41..=0x5A).contains(&c) { c + 32 } else { c };
    let mut chars = b.encode_utf16();
    a.iter().all(|&x| chars.next().is_some_and(|y| fold(x) == fold(y))) && chars.next().is_none()
}

fn err_wrap(prefix: &str, mid: &[u16], suffix: &str) -> Vec<u16> {
    let mut v = w(prefix);
    v.extend_from_slice(mid);
    v.extend(suffix.encode_utf16());
    v
}

fn mode_name(mode: i32) -> &'static str {
    ImeMode::from_i32(mode).cli_name()
}

fn index_of(name: &[u16]) -> Option<usize> {
    let found = keys::find_key(name)?;
    keys::KEYS.iter().position(|k| std::ptr::eq(k, found))
}

/// Parsed command line. Stored strings are already truncated the way
/// `StringCchCopyW` truncates the C++ `CLIArgs` buffers.
#[derive(Clone, Debug)]
pub struct Parsed {
    pub command: Command,
    pub mode: i32,
    pub has_mode: bool,
    pub json: bool,
    pub silent: bool,
    pub no_validate: bool,
    pub get_key: Vec<u16>,
    pub sets: Vec<(Vec<u16>, Vec<u16>)>,
    pub file_path: Vec<u16>,
    pub array_table: i32,
}

impl Default for Parsed {
    fn default() -> Self {
        Self {
            command: Command::None,
            mode: ImeMode::None as i32,
            has_mode: false,
            json: false,
            silent: false,
            no_validate: false,
            get_key: Vec::new(),
            sets: Vec::new(),
            file_path: Vec::new(),
            array_table: -1,
        }
    }
}

/// C layout of `DimeCliArgs` in `dime_ffi.h`. Fixed buffers are NUL-terminated.
#[repr(C)]
pub struct DimeCliArgs {
    pub command: i32,
    pub mode: i32,
    pub has_mode: i32,
    pub json_output: i32,
    pub silent: i32,
    pub no_validate: i32,
    pub get_key: [u16; 64],
    pub set_keys: [[u16; 64]; 32],
    pub set_values: [[u16; 256]; 32],
    pub set_count: i32,
    pub file_path: [u16; 260],
    pub array_table: i32,
}

pub fn to_ffi(p: &Parsed) -> DimeCliArgs {
    let mut a = DimeCliArgs {
        command: p.command as i32,
        mode: p.mode,
        has_mode: i32::from(p.has_mode),
        json_output: i32::from(p.json),
        silent: i32::from(p.silent),
        no_validate: i32::from(p.no_validate),
        get_key: [0; 64],
        set_keys: [[0; 64]; 32],
        set_values: [[0; 256]; 32],
        set_count: p.sets.len() as i32,
        file_path: [0; 260],
        array_table: p.array_table,
    };
    fill_z(&mut a.get_key, &p.get_key);
    for (i, (k, v)) in p.sets.iter().enumerate().take(MAX_SET) {
        fill_z(&mut a.set_keys[i], k);
        fill_z(&mut a.set_values[i], v);
    }
    fill_z(&mut a.file_path, &p.file_path);
    a
}

const HELP: &str = concat!(
    "DIMESettings.exe [--mode <mode>] <command> [options]\n",
    "\n",
    "GUI launch:\n",
    "  (no arguments)                    Open settings UI (default: dayi tab)\n",
    "  --mode <mode>                     Open settings UI to specific IME tab\n",
    "  --reset-ui                        Reset window position/size to defaults\n",
    "\n",
    "Modes:\n",
    "  dayi, array, phonetic, generic\n",
    "\n",
    "CLI commands:\n",
    "  --help, -h                        Show this help message\n",
    "  --list-modes                      List available IME modes\n",
    "  --list-keys                       List all keys for the selected mode\n",
    "  --get <key>                       Get the value of a setting key\n",
    "  --get-all                         Get all settings for the selected mode\n",
    "  --set <key> <value> [--set ...]   Set one or more settings\n",
    "  --reset                           Reset all settings to defaults\n",
    "  --import-custom <file>            Import a custom phrase table\n",
    "                                    (pre-flights through the same format\n",
    "                                     checker the GUI uses; see --no-validate)\n",
    "  --export-custom <file>            Export the custom phrase table\n",
    "  --load-main <file>                Load a main dictionary (.cin)\n",
    "  --load-phrase <file>              Load a phrase dictionary (.cin)\n",
    "  --load-array <table> <file>       Load an Array supplementary table\n",
    "                                    tables: sp, sc, ext-b, ext-cd,\n",
    "                                            ext-efg, array40, phrase\n",
    "\n",
    "Options:\n",
    "  --json                            Output in JSON format\n",
    "  --silent                          Suppress all output\n",
    "  --no-validate                     Skip pre-flight format check on\n",
    "                                    --import-custom (use with care)\n",
    "\n",
    "Exit codes: 0=success, 1=invalid argument, 2=I/O error,\n",
    "            3=semantic error (key not applicable, or\n",
    "              --import-custom format validation failure)\n",
    "\n",
    "Examples:\n",
    "  DIMESettings.exe                                    (open UI, dayi tab)\n",
    "  DIMESettings.exe --mode array                       (open UI, array tab)\n",
    "  DIMESettings.exe --reset-ui --mode phonetic         (reset window, phonetic tab)\n",
    "  DIMESettings.exe --mode dayi --get FontSize\n",
    "  DIMESettings.exe --mode array --set ColorMode 2 --set FontSize 16\n",
    "  DIMESettings.exe --mode phonetic --get-all --json\n",
    "  DIMESettings.exe --mode dayi --reset\n",
);

/// Why `CreateFileW` / `WriteFile` failed while persisting an import.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WriteUtf16 {
    Ok,
    OpenFailed(u32),
    WriteFailed,
}

/// Side effects that stay in C++. See the module comment.
pub trait CliHost {
    fn write_out(&mut self, text: &[u16]);
    fn write_err(&mut self, text: &[u16]);
    fn load_config(&mut self, mode: i32);
    fn write_config(&mut self, mode: i32);
    fn reset_defaults(&mut self, mode: i32);
    /// `None` skips the key, matching `GetKeyValue` returning false.
    fn get_value(&mut self, index: i32) -> Option<Vec<u16>>;
    /// `(rc, message)`. `rc` is 0 / 1 / 2. The message has no trailing newline;
    /// empty means print nothing.
    fn set_value(&mut self, index: i32, value: &[u16]) -> (i32, Vec<u16>);
    fn file_exists(&mut self, path: &[u16]) -> bool;
    fn load_text(&mut self, path: &[u16]) -> Option<Vec<u16>>;
    /// Creates the composition engine, loads config, and returns max codes.
    fn begin_validate(&mut self, mode: i32) -> u32;
    /// 0 valid, 1 format, 2 key too long, 3 invalid char.
    fn validate_line(&mut self, line: &[u16], mode: i32, max_codes: u32) -> i32;
    fn end_validate(&mut self);
    fn custom_txt_path(&mut self, mode: i32) -> Vec<u16>;
    fn custom_cin_path(&mut self, mode: i32) -> Vec<u16>;
    fn write_utf16_bom(&mut self, path: &[u16], text: &[u16]) -> WriteUtf16;
    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom: bool) -> bool;
    fn export_src_path(&mut self, mode: i32) -> Vec<u16>;
    /// `None` on success, otherwise `GetLastError`.
    fn copy_file(&mut self, src: &[u16], dst: &[u16]) -> Option<u32>;
    /// Empty when `mode` is not an IME mode (`--load-main` then exits 1).
    fn main_dest(&mut self, mode: i32) -> Vec<u16>;
    fn phrase_dest(&mut self) -> Vec<u16>;
    /// Empty when `table` is not a known array table.
    fn array_dest(&mut self, table: i32) -> Vec<u16>;
}

fn say(host: &mut dyn CliHost, silent: bool, text: &[u16]) {
    if !silent {
        host.write_out(text);
    }
}

/// `ParseCLIArgs` given an argv already split by `CommandLineToArgvW`.
pub fn parse(argv: &[&[u16]]) -> Result<Parsed, Vec<u16>> {
    let mut p = Parsed::default();
    let mut i = 0;
    while i < argv.len() {
        let arg = argv[i];
        if eq_ci(arg, "--mode") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --mode requires an argument.\n"));
            }
            i += 1;
            let m = argv[i];
            p.mode = if eq_ci(m, "dayi") {
                ImeMode::Dayi as i32
            } else if eq_ci(m, "array") {
                ImeMode::Array as i32
            } else if eq_ci(m, "phonetic") {
                ImeMode::Phonetic as i32
            } else if eq_ci(m, "generic") {
                ImeMode::Generic as i32
            } else {
                return Err(err_wrap("Error: unknown mode '", m, "'.\n"));
            };
            p.has_mode = true;
        } else if eq_ci(arg, "--json") {
            p.json = true;
        } else if eq_ci(arg, "--silent") {
            p.silent = true;
        } else if eq_ci(arg, "--no-validate") {
            p.no_validate = true;
        } else if eq_ci(arg, "--list-modes") {
            p.command = Command::ListModes;
        } else if eq_ci(arg, "--list-keys") {
            p.command = Command::ListKeys;
        } else if eq_ci(arg, "--help") || eq_ci(arg, "-h") {
            p.command = Command::Help;
        } else if eq_ci(arg, "--get-all") {
            p.command = Command::GetAll;
        } else if eq_ci(arg, "--reset") {
            p.command = Command::Reset;
        } else if eq_ci(arg, "--get") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --get requires a key name.\n"));
            }
            i += 1;
            p.command = Command::Get;
            p.get_key = trunc(argv[i], KEY_CHARS);
        } else if eq_ci(arg, "--set") {
            if i + 2 >= argv.len() {
                return Err(w("Error: --set requires a key and a value.\n"));
            }
            if p.sets.len() >= MAX_SET {
                return Err(w(&format!("Error: too many --set pairs (max {MAX_SET}).\n")));
            }
            i += 1;
            let key = trunc(argv[i], KEY_CHARS);
            i += 1;
            let value = trunc(argv[i], VALUE_CHARS);
            p.sets.push((key, value));
            p.command = Command::Set;
        } else if eq_ci(arg, "--import-custom") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --import-custom requires a file path.\n"));
            }
            i += 1;
            p.command = Command::ImportCustom;
            p.file_path = trunc(argv[i], PATH_CHARS);
        } else if eq_ci(arg, "--export-custom") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --export-custom requires a file path.\n"));
            }
            i += 1;
            p.command = Command::ExportCustom;
            p.file_path = trunc(argv[i], PATH_CHARS);
        } else if eq_ci(arg, "--load-main") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --load-main requires a file path.\n"));
            }
            i += 1;
            p.command = Command::LoadMain;
            p.file_path = trunc(argv[i], PATH_CHARS);
        } else if eq_ci(arg, "--load-phrase") {
            if i + 1 >= argv.len() {
                return Err(w("Error: --load-phrase requires a file path.\n"));
            }
            i += 1;
            p.command = Command::LoadPhrase;
            p.file_path = trunc(argv[i], PATH_CHARS);
        } else if eq_ci(arg, "--load-array") {
            if i + 2 >= argv.len() {
                return Err(w("Error: --load-array requires a table name and a file path.\n"));
            }
            i += 1;
            let tbl = argv[i];
            p.array_table = if eq_ci(tbl, "sp") {
                TABLE_SP
            } else if eq_ci(tbl, "sc") {
                TABLE_SC
            } else if eq_ci(tbl, "ext-b") {
                TABLE_EXT_B
            } else if eq_ci(tbl, "ext-cd") {
                TABLE_EXT_CD
            } else if eq_ci(tbl, "ext-efg") {
                TABLE_EXT_EFG
            } else if eq_ci(tbl, "array40") {
                TABLE_ARRAY40
            } else if eq_ci(tbl, "phrase") {
                TABLE_PHRASE
            } else {
                return Err(err_wrap("Error: unknown array table '", tbl, "'.\n"));
            };
            i += 1;
            p.command = Command::LoadArray;
            p.file_path = trunc(argv[i], PATH_CHARS);
        } else {
            return Err(err_wrap("Error: unknown argument '", arg, "'.\n"));
        }
        i += 1;
    }
    Ok(p)
}

/// `RunCLI`. Exit codes: 0 success, 1 bad argument, 2 I/O, 3 semantic.
pub fn run(argv: &[&[u16]], host: &mut dyn CliHost) -> i32 {
    match parse(argv) {
        Err(e) => {
            host.write_err(&e);
            1
        }
        Ok(p) => dispatch(p, host),
    }
}

fn dispatch(p: Parsed, host: &mut dyn CliHost) -> i32 {
    if p.command == Command::None {
        host.write_err(&w("Error: no command specified. Use --help for usage.\n"));
        return 1;
    }
    if p.command != Command::ListModes && p.command != Command::Help && !p.has_mode {
        host.write_err(&w("Error: --mode is required for this command.\n"));
        return 1;
    }
    match p.command {
        Command::Help => {
            say(host, p.silent, &w(HELP));
            0
        }
        Command::ListModes => handle_list_modes(&p, host),
        Command::ListKeys => handle_list(&p, host, true),
        Command::GetAll => handle_list(&p, host, false),
        Command::Get => handle_get(&p, host),
        Command::Set => handle_set(&p, host),
        Command::Reset => {
            host.load_config(p.mode);
            host.reset_defaults(p.mode);
            host.write_config(p.mode);
            0
        }
        Command::ImportCustom => handle_import(&p, host),
        Command::ExportCustom => handle_export(&p, host),
        Command::LoadMain => handle_load_main(&p, host),
        Command::LoadPhrase => handle_load_phrase(&p, host),
        Command::LoadArray => handle_load_array(&p, host),
        Command::None => 1,
    }
}

fn handle_list_modes(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    let text = if p.json {
        "[\"dayi\",\"array\",\"phonetic\",\"generic\"]\n"
    } else {
        "dayi\narray\nphonetic\ngeneric\n"
    };
    say(host, p.silent, &w(text));
    0
}

fn each_value(mode: i32, host: &mut dyn CliHost, mut f: impl FnMut(&KeyInfo, &[u16])) -> bool {
    if ImeMode::from_i32(mode) == ImeMode::None {
        host.write_err(&w("Error: invalid mode.\n"));
        return false;
    }
    for (idx, ki) in keys::KEYS.iter().enumerate() {
        if !keys::applicable(ki, ImeMode::from_i32(mode)) {
            continue;
        }
        let Some(val) = host.get_value(idx as i32) else { continue };
        f(ki, &val);
    }
    true
}

/// `list_keys` selects the `--list-keys` JSON shape; otherwise `--get-all`.
fn handle_list(p: &Parsed, host: &mut dyn CliHost, list_keys: bool) -> i32 {
    host.load_config(p.mode);
    let mut out = Vec::new();
    let name = mode_name(p.mode);
    if p.json && list_keys {
        out.extend(format!("{{\n  \"mode\": \"{name}\",\n  \"keys\": [\n").encode_utf16());
    } else if p.json {
        out.extend(format!("{{\n  \"mode\": \"{name}\"").encode_utf16());
    }
    let mut first = true;
    let ok = each_value(p.mode, host, |ki, val| {
        if p.json && list_keys {
            if !first {
                out.extend(",\n".encode_utf16());
            }
            first = false;
            out.extend(format!("    {{\"key\":\"{}\",\"value\":\"", ki.name).encode_utf16());
            out.extend_from_slice(val);
            out.extend("\"}".encode_utf16());
        } else if p.json {
            out.extend(format!(",\n  \"{}\": \"", ki.name).encode_utf16());
            out.extend_from_slice(val);
            out.extend("\"".encode_utf16());
        } else {
            out.extend(ki.name.encode_utf16());
            out.push(u16::from(b'='));
            out.extend_from_slice(val);
            out.push(u16::from(b'\n'));
        }
    });
    if !ok {
        return 1;
    }
    if p.json && list_keys {
        out.extend("\n  ]\n}\n".encode_utf16());
    } else if p.json {
        out.extend("\n}\n".encode_utf16());
    }
    say(host, p.silent, &out);
    0
}

fn handle_get(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    host.load_config(p.mode);
    let Some(idx) = index_of(&p.get_key) else {
        host.write_err(&err_wrap(
            "Error: unknown key '",
            &p.get_key,
            "'. Use --list-keys to see valid keys.\n",
        ));
        return 1;
    };
    let ki = &keys::KEYS[idx];
    if !keys::applicable(ki, ImeMode::from_i32(p.mode)) {
        let mut m = err_wrap("Error: key '", &p.get_key, "' is not applicable to mode '");
        m.extend(mode_name(p.mode).encode_utf16());
        m.extend("'.\n".encode_utf16());
        host.write_err(&m);
        return 3;
    }
    let Some(val) = host.get_value(idx as i32) else {
        host.write_err(&err_wrap("Error: failed to read key '", &p.get_key, "'.\n"));
        return 2;
    };
    let mut out = Vec::new();
    if p.json {
        out.extend(format!("{{\"{}\":\"", ki.name).encode_utf16());
        out.extend_from_slice(&val);
        out.extend("\"}\n".encode_utf16());
    } else {
        out.extend(ki.name.encode_utf16());
        out.push(u16::from(b'='));
        out.extend_from_slice(&val);
        out.push(u16::from(b'\n'));
    }
    say(host, p.silent, &out);
    0
}

fn handle_set(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    host.load_config(p.mode);
    let mut indexes = Vec::with_capacity(p.sets.len());
    for (key, _) in &p.sets {
        let Some(idx) = index_of(key) else {
            host.write_err(&err_wrap("Error: unknown key '", key, "'.\n"));
            return 1;
        };
        if !keys::applicable(&keys::KEYS[idx], ImeMode::from_i32(p.mode)) {
            let mut m = err_wrap("Error: key '", key, "' is not applicable to mode '");
            m.extend(mode_name(p.mode).encode_utf16());
            m.extend("'.\n".encode_utf16());
            host.write_err(&m);
            return 3;
        }
        indexes.push(idx);
    }
    for (n, idx) in indexes.iter().copied().enumerate() {
        let (rc, msg) = host.set_value(idx as i32, &p.sets[n].1);
        if rc != 0 {
            if !msg.is_empty() {
                let mut line = msg;
                if !line.ends_with(&[u16::from(b'\n')]) {
                    line.push(u16::from(b'\n'));
                }
                host.write_err(&line);
            }
            return rc;
        }
    }
    host.write_config(p.mode);
    0
}

fn not_found(host: &mut dyn CliHost, path: &[u16]) {
    host.write_err(&err_wrap("Error: file not found: ", path, "\n"));
}

/// Same line split as `HandleImportCustom`: a trailing newline yields one
/// extra empty line, and a CR before LF is not part of the line.
fn for_each_line(text: &[u16], mut f: impl FnMut(i32, &[u16])) {
    let mut line_no = 0i32;
    let mut start = 0usize;
    let mut i = 0usize;
    while i <= text.len() {
        if i == text.len() || text[i] == u16::from(b'\n') {
            line_no = line_no.saturating_add(1);
            let mut len = i - start;
            if len > 0 && text[start + len - 1] == u16::from(b'\r') {
                len -= 1;
            }
            f(line_no, &text[start..start + len]);
            if i == text.len() {
                break;
            }
            start = i + 1;
        }
        i += 1;
    }
}

fn handle_import(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    if !host.file_exists(&p.file_path) {
        not_found(host, &p.file_path);
        return 2;
    }
    let Some(text) = host.load_text(&p.file_path) else {
        host.write_err(&err_wrap("Error: could not read or decode file: ", &p.file_path, "\n"));
        return 2;
    };
    if !p.no_validate {
        let max_codes = host.begin_validate(p.mode);
        let mut err_count = 0i32;
        let mut reported = 0i32;
        for_each_line(&text, |line_no, line| {
            let kind = host.validate_line(line, p.mode, max_codes);
            if kind == 0 {
                return;
            }
            if reported < 20 {
                let name = match kind {
                    2 => "key too long",
                    3 => "invalid char",
                    _ => "format",
                };
                host.write_err(&w(&format!("Line {line_no}: {name} error\n")));
                reported += 1;
            }
            err_count += 1;
        });
        host.end_validate();
        if err_count > 0 {
            if err_count > 20 {
                host.write_err(&w(&format!("  (+ {} more error(s) suppressed)\n", err_count - 20)));
            }
            host.write_err(&w(&format!("\nValidation failed: {err_count} invalid line(s). Import aborted.\n")));
            return 3;
        }
    }
    let txt = host.custom_txt_path(p.mode);
    let cin = host.custom_cin_path(p.mode);
    match host.write_utf16_bom(&txt, &text) {
        WriteUtf16::Ok => {}
        WriteUtf16::OpenFailed(gle) => {
            let mut m = err_wrap("Error: could not open ", &txt, "");
            m.extend(format!(" for writing (error {gle})\n").encode_utf16());
            host.write_err(&m);
            return 2;
        }
        WriteUtf16::WriteFailed => {
            host.write_err(&err_wrap("Error: failed to write ", &txt, "\n"));
            return 2;
        }
    }
    if !host.parse_cin(&txt, &cin, true) {
        let mut m = err_wrap("Error: failed to parse ", &txt, " into ");
        m.extend_from_slice(&cin);
        m.push(u16::from(b'\n'));
        host.write_err(&m);
        return 2;
    }
    0
}

fn handle_export(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    let src = host.export_src_path(p.mode);
    if !host.file_exists(&src) {
        let mut m = w("Error: no custom table found for mode '");
        m.extend(mode_name(p.mode).encode_utf16());
        m.extend("'.\n".encode_utf16());
        host.write_err(&m);
        return 2;
    }
    if let Some(gle) = host.copy_file(&src, &p.file_path) {
        let mut m = err_wrap("Error: could not write to ", &p.file_path, "");
        m.extend(format!(" (error {gle})\n").encode_utf16());
        host.write_err(&m);
        return 2;
    }
    0
}

fn handle_load_main(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    if !host.file_exists(&p.file_path) {
        not_found(host, &p.file_path);
        return 2;
    }
    let dest = host.main_dest(p.mode);
    if dest.is_empty() {
        host.write_err(&w("Error: invalid mode for --load-main.\n"));
        return 1;
    }
    if !host.parse_cin(&p.file_path, &dest, false) {
        host.write_err(&err_wrap("Error: failed to load main table from ", &p.file_path, "\n"));
        return 2;
    }
    0
}

fn handle_load_phrase(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    if !host.file_exists(&p.file_path) {
        not_found(host, &p.file_path);
        return 2;
    }
    let dest = host.phrase_dest();
    if !host.parse_cin(&p.file_path, &dest, false) {
        host.write_err(&err_wrap("Error: failed to load phrase table from ", &p.file_path, "\n"));
        return 2;
    }
    0
}

fn handle_load_array(p: &Parsed, host: &mut dyn CliHost) -> i32 {
    let dest = host.array_dest(p.array_table);
    if dest.is_empty() {
        host.write_err(&w("Error: invalid --load-array table.\n"));
        return 1;
    }
    if !host.file_exists(&p.file_path) {
        not_found(host, &p.file_path);
        return 2;
    }
    if !host.parse_cin(&p.file_path, &dest, false) {
        host.write_err(&err_wrap("Error: failed to load array table from ", &p.file_path, "\n"));
        return 2;
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn argv_of(s: &str) -> Vec<Vec<u16>> {
        s.split(' ').filter(|p| !p.is_empty()).map(|p| p.encode_utf16().collect()).collect()
    }

    fn view(owned: &[Vec<u16>]) -> Vec<&[u16]> {
        owned.iter().map(|v| v.as_slice()).collect()
    }

    fn parse_str(s: &str) -> Result<Parsed, Vec<u16>> {
        let owned = argv_of(s);
        parse(&view(&owned))
    }

    fn run_str(s: &str, h: &mut Rec) -> i32 {
        let owned = argv_of(s);
        run(&view(&owned), h)
    }

    fn utf(s: &[u16]) -> String {
        String::from_utf16_lossy(s)
    }

    struct Rec {
        out: Vec<u16>,
        err: Vec<u16>,
        loads: u32,
        writes: u32,
        resets: u32,
        sets: Vec<(i32, Vec<u16>)>,
        set_fail_on: Option<usize>,
        set_msg: Vec<u16>,
        value: Option<Vec<u16>>,
        exists: bool,
        text: Option<Vec<u16>>,
        began: u32,
        ended: u32,
        max_codes: u32,
        validated: Vec<Vec<u16>>,
        fail_prefix: Vec<u16>,
        write_result: WriteUtf16,
        parsed: bool,
        parse_ok: bool,
        copied: bool,
        copy_gle: Option<u32>,
    }

    impl Default for Rec {
        fn default() -> Self {
            Self {
                out: Vec::new(),
                err: Vec::new(),
                loads: 0,
                writes: 0,
                resets: 0,
                sets: Vec::new(),
                set_fail_on: None,
                set_msg: Vec::new(),
                value: Some(w("1")),
                exists: false,
                text: None,
                began: 0,
                ended: 0,
                max_codes: 4,
                validated: Vec::new(),
                fail_prefix: Vec::new(),
                write_result: WriteUtf16::Ok,
                parsed: false,
                parse_ok: true,
                copied: false,
                copy_gle: None,
            }
        }
    }

    impl CliHost for Rec {
        fn write_out(&mut self, text: &[u16]) { self.out.extend_from_slice(text); }
        fn write_err(&mut self, text: &[u16]) { self.err.extend_from_slice(text); }
        fn load_config(&mut self, _mode: i32) { self.loads += 1; }
        fn write_config(&mut self, _mode: i32) { self.writes += 1; }
        fn reset_defaults(&mut self, _mode: i32) { self.resets += 1; }
        fn get_value(&mut self, _index: i32) -> Option<Vec<u16>> { self.value.clone() }
        fn set_value(&mut self, index: i32, value: &[u16]) -> (i32, Vec<u16>) {
            let n = self.sets.len();
            self.sets.push((index, value.to_vec()));
            if self.set_fail_on == Some(n) { (1, self.set_msg.clone()) } else { (0, Vec::new()) }
        }
        fn file_exists(&mut self, _path: &[u16]) -> bool { self.exists }
        fn load_text(&mut self, _path: &[u16]) -> Option<Vec<u16>> { self.text.clone() }
        fn begin_validate(&mut self, _mode: i32) -> u32 { self.began += 1; self.max_codes }
        fn validate_line(&mut self, line: &[u16], _mode: i32, _max_codes: u32) -> i32 {
            self.validated.push(line.to_vec());
            if !self.fail_prefix.is_empty() && line.starts_with(&self.fail_prefix) { 2 } else { 0 }
        }
        fn end_validate(&mut self) { self.ended += 1; }
        fn custom_txt_path(&mut self, _mode: i32) -> Vec<u16> { w("C:\\DIME\\DAYI-Custom.txt") }
        fn custom_cin_path(&mut self, _mode: i32) -> Vec<u16> { w("C:\\DIME\\DAYI-Custom.cin") }
        fn write_utf16_bom(&mut self, _path: &[u16], _text: &[u16]) -> WriteUtf16 { self.write_result }
        fn parse_cin(&mut self, _src: &[u16], _dst: &[u16], _custom: bool) -> bool {
            self.parsed = true;
            self.parse_ok
        }
        fn export_src_path(&mut self, _mode: i32) -> Vec<u16> { w("C:\\DIME\\GENERIC-CUSTOM.txt") }
        fn copy_file(&mut self, _src: &[u16], _dst: &[u16]) -> Option<u32> {
            if let Some(g) = self.copy_gle { Some(g) } else { self.copied = true; None }
        }
        fn main_dest(&mut self, mode: i32) -> Vec<u16> {
            if (0..=3).contains(&mode) { w("C:\\DIME\\Dayi.cin") } else { Vec::new() }
        }
        fn phrase_dest(&mut self) -> Vec<u16> { w("C:\\DIME\\Phrase.cin") }
        fn array_dest(&mut self, table: i32) -> Vec<u16> {
            if (0..=6).contains(&table) { w("C:\\DIME\\Array-special.cin") } else { Vec::new() }
        }
    }

    #[test]
    fn ffi_args_layout() {
        assert_eq!(std::mem::size_of::<DimeCliArgs>(), 21160);
        assert_eq!(std::mem::align_of::<DimeCliArgs>(), 4);
    }

    #[test]
    fn parse_commands_and_truncation() {
        let p = parse_str("--mode DaYi --get FontSize").unwrap();
        assert_eq!(p.command, Command::Get);
        assert_eq!(p.mode, 0);
        assert!(p.has_mode);
        assert_eq!(utf(&p.get_key), "FontSize");

        let p = parse_str("--MODE ARRAY --get-all --json --silent").unwrap();
        assert_eq!(p.command, Command::GetAll);
        assert_eq!(p.mode, 1);
        assert!(p.json && p.silent);

        let p = parse_str("--mode dayi --set FontSize 14 --set ColorMode 2 --set FontItalic 1").unwrap();
        assert_eq!(p.sets.len(), 3);
        assert_eq!(utf(&p.sets[1].0), "ColorMode");
        assert_eq!(utf(&p.sets[1].1), "2");

        let p = parse_str("--list-modes").unwrap();
        assert_eq!(p.command, Command::ListModes);
        assert!(!p.has_mode);
        assert_eq!(p.mode, 4);

        let p = parse_str("--mode array --load-array EXT-EFG C:\\dummy.cin").unwrap();
        assert_eq!(p.command, Command::LoadArray);
        assert_eq!(p.array_table, 4);
        assert_eq!(utf(&p.file_path), "C:\\dummy.cin");

        let p = parse_str("--get FontSize --list-modes").unwrap();
        assert_eq!(p.command, Command::ListModes);
        assert_eq!(utf(&p.get_key), "FontSize");

        let long = "K".repeat(80);
        let p = parse_str(&format!("--mode dayi --get {long}")).unwrap();
        assert_eq!(p.get_key.len(), 63);

        let a = to_ffi(&parse_str("--mode dayi --get FontSize").unwrap());
        assert_eq!(a.command, Command::Get as i32);
        assert_eq!(a.has_mode, 1);
        assert_eq!(a.get_key[8], 0);
        assert_eq!(a.array_table, -1);

        assert!(parse_str("--mode badmode --list-keys").is_err());
        assert!(parse_str("--mode").is_err());
        assert!(parse_str("--mode dayi --unknown-flag").is_err());
        assert!(parse_str("--mode dayi --get").is_err());
        assert!(parse_str("--mode dayi --set FontSize").is_err());
        assert!(parse_str("--mode array --load-array badtable C:\\a.cin").is_err());
        assert!(parse_str("--mode array --load-array").is_err());

        let mut many = String::from("--mode dayi");
        for i in 0..33 {
            many.push_str(&format!(" --set FontSize {i}"));
        }
        let err = parse_str(&many).unwrap_err();
        assert!(utf(&err).contains("max 32"));
    }

    #[test]
    fn run_listing_get_and_set() {
        let mut h = Rec::default();
        assert_eq!(run_str("", &mut h), 1);
        assert!(utf(&h.err).contains("no command specified"));

        h = Rec::default();
        assert_eq!(run_str("--list-modes", &mut h), 0);
        assert_eq!(utf(&h.out), "dayi\narray\nphonetic\ngeneric\n");

        h = Rec::default();
        assert_eq!(run_str("--list-modes --json --silent", &mut h), 0);
        assert!(h.out.is_empty());

        h = Rec::default();
        assert_eq!(run_str("--get FontSize", &mut h), 1);
        assert!(utf(&h.err).contains("--mode is required"));

        h = Rec::default();
        h.value = Some(w("12"));
        assert_eq!(run_str("--mode dayi --get FontSize", &mut h), 0);
        assert_eq!(utf(&h.out), "FontSize=12\n");
        assert_eq!(h.loads, 1);

        h = Rec::default();
        h.value = Some(w("12"));
        assert_eq!(run_str("--mode dayi --json --get FontSize", &mut h), 0);
        assert_eq!(utf(&h.out), "{\"FontSize\":\"12\"}\n");

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --get Nope", &mut h), 1);
        assert!(utf(&h.err).contains("Use --list-keys"));

        h = Rec::default();
        assert_eq!(run_str("--mode array --get DayiArticleMode", &mut h), 3);
        assert!(utf(&h.err).contains("not applicable to mode 'array'"));

        h = Rec::default();
        assert_eq!(run_str("--mode array --list-keys", &mut h), 0);
        let o = utf(&h.out);
        assert!(o.contains("ArrayScope=1\n"));
        assert!(!o.contains("Big5Filter"));
        assert!(!o.contains("PhoneticKeyboardLayout"));

        h = Rec::default();
        assert_eq!(run_str("--mode phonetic --list-keys --json", &mut h), 0);
        let o = utf(&h.out);
        assert!(o.contains("\"mode\": \"phonetic\""));
        assert!(o.contains("\"keys\": [\n    {\"key\":\"AutoCompose\",\"value\":\"1\"}"));
        assert!(o.contains("PhoneticKeyboardLayout"));
        assert!(!o.contains("ArrayScope"));
        assert!(o.ends_with("\n  ]\n}\n"));

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --get-all --json", &mut h), 0);
        let o = utf(&h.out);
        assert!(o.starts_with("{\n  \"mode\": \"dayi\""));
        assert!(o.contains(",\n  \"FontSize\": \"1\""));
        assert!(o.ends_with("\n}\n"));
        assert!(!o.contains("ArrayScope"));

        h = Rec::default();
        assert_eq!(run_str("--help", &mut h), 0);
        assert!(utf(&h.out).contains("Exit codes: 0=success"));
        assert!(utf(&h.out).starts_with("DIMESettings.exe [--mode <mode>]"));

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --reset --silent", &mut h), 0);
        assert_eq!((h.loads, h.resets, h.writes), (1, 1, 1));
        assert!(h.out.is_empty());

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --set FontSize 18", &mut h), 0);
        assert_eq!(h.writes, 1);
        assert_eq!(utf(&h.sets[0].1), "18");

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --set NoSuch 1", &mut h), 1);
        assert!(h.sets.is_empty() && h.writes == 0);
        assert!(!utf(&h.err).contains("list-keys"));

        h = Rec::default();
        h.set_fail_on = Some(1);
        h.set_msg = w("Error: value 'x' is not a valid integer.");
        assert_eq!(run_str("--mode dayi --set FontSize 18 --set FontSize x", &mut h), 1);
        assert_eq!(h.sets.len(), 2);
        assert_eq!(h.writes, 0);
        assert!(utf(&h.err).ends_with("integer.\n"));
    }

    #[test]
    fn run_import_export_load() {
        let mut h = Rec::default();
        h.exists = true;
        h.text = Some("a b\r\nb c\n".encode_utf16().collect());
        assert_eq!(run_str("--mode generic --import-custom C:\\in.txt", &mut h), 0);
        assert_eq!(h.validated.len(), 3);
        assert_eq!(utf(&h.validated[0]), "a b");
        assert_eq!(utf(&h.validated[1]), "b c");
        assert!(h.validated[2].is_empty());
        assert!(h.parsed && h.began == 1 && h.ended == 1);

        h = Rec::default();
        h.exists = true;
        h.text = Some(w("not a pair"));
        assert_eq!(run_str("--mode dayi --no-validate --import-custom C:\\in.txt", &mut h), 0);
        assert_eq!(h.began, 0);
        assert!(h.parsed);

        h = Rec::default();
        h.exists = true;
        h.fail_prefix = w("BAD");
        h.text = Some("ok x\nBAD y\n".encode_utf16().collect());
        assert_eq!(run_str("--mode dayi --import-custom C:\\in.txt", &mut h), 3);
        assert!(!h.parsed && h.ended == 1);
        assert!(utf(&h.err).contains("Line 2: key too long error\n"));
        assert!(utf(&h.err).contains("Validation failed: 1 invalid line"));

        h = Rec::default();
        assert_eq!(run_str("--mode generic --import-custom C:\\missing.txt", &mut h), 2);
        assert!(utf(&h.err).contains("file not found: C:\\missing.txt"));
        assert_eq!(h.loads, 0);

        h = Rec::default();
        h.exists = true;
        h.text = Some(w("a b\n"));
        h.write_result = WriteUtf16::OpenFailed(32);
        assert_eq!(run_str("--mode dayi --import-custom C:\\in.txt", &mut h), 2);
        assert!(utf(&h.err).contains("could not open C:\\DIME\\DAYI-Custom.txt for writing (error 32)"));
        assert!(!h.parsed);

        h = Rec::default();
        assert_eq!(run_str("--mode generic --export-custom C:\\o.txt", &mut h), 2);
        assert!(utf(&h.err).contains("no custom table found for mode 'generic'"));

        h = Rec::default();
        h.exists = true;
        h.copy_gle = Some(5);
        assert_eq!(run_str("--mode generic --export-custom C:\\o.txt", &mut h), 2);
        assert!(utf(&h.err).contains("could not write to C:\\o.txt (error 5)"));

        h = Rec::default();
        h.exists = true;
        assert_eq!(run_str("--mode generic --export-custom C:\\o.txt", &mut h), 0);
        assert!(h.copied);

        h = Rec::default();
        assert_eq!(run_str("--mode dayi --load-main C:\\t.cin", &mut h), 2);
        h.exists = true;
        h.parse_ok = false;
        assert_eq!(run_str("--mode dayi --load-main C:\\t.cin", &mut h), 2);
        assert!(utf(&h.err).contains("failed to load main table from C:\\t.cin"));

        h = Rec::default();
        assert_eq!(run_str("--mode array --load-array sp C:\\no.cin", &mut h), 2);
        h.exists = true;
        assert_eq!(run_str("--mode generic --load-phrase C:\\p.cin", &mut h), 0);
        assert!(h.parsed);
    }
}
