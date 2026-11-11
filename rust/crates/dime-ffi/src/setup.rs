//! Phase 4c: dictionary setup, keystroke table, candidate range, preserved keys.
//!
//! The vtable order matches `DimeSetupHostVtbl` in `include/dime_ffi.h` and
//! `CEngineSetupHost::Vtbl` in `SetupCompositionProcesseorEngine.cpp`.

#![allow(clippy::too_many_arguments)]

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_core::engine_setup::{self, Roots, SetupHost};

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn slice<'a>(p: *const u16, len: usize) -> &'a [u16] {
    if p.is_null() || len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(p, len) }
    }
}

/// Host argument is `*mut c_void` so this struct does not name itself through
/// the vtable. The ABI matches `DimeSetupHost*` (one pointer).
#[repr(C)]
pub struct DimeSetupHost {
    vtbl: *const DimeSetupHostVtbl,
    ctx: *mut std::ffi::c_void,
}

#[repr(C)]
pub struct DimeSetupHostVtbl {
    path_exists: Option<unsafe extern "C" fn(*mut DimeSetupHost, *const u16, usize) -> i32>,
    file_mtime: Option<unsafe extern "C" fn(*mut DimeSetupHost, *const u16, usize, *mut i64) -> i32>,
    copy_file: Option<unsafe extern "C" fn(*mut DimeSetupHost, *const u16, usize, *const u16, usize, i32)>,
    create_dir: Option<unsafe extern "C" fn(*mut DimeSetupHost, *const u16, usize)>,
    path_equal: Option<unsafe extern "C" fn(*mut DimeSetupHost, u32, *const u16, usize, *const u16, usize) -> i32>,
    has_file: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32) -> i32>,
    has_engine: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32) -> i32>,
    dict_type: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32) -> i32>,
    file_updated: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32) -> i32>,
    file_name: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32, *mut u16, usize) -> usize>,
    drop_file: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    drop_engine: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    new_file: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32) -> i32>,
    create_file: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32, *const u16, usize, u32) -> i32>,
    create_engine: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32, u32, i32) -> i32>,
    parse_config: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32, i32)>,
    alias_phrase_to_main: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    setup_big5: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32, u32)>,
    setup_keystroke: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    load_config: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    setup_configuration: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    setup_candidate_range: Option<unsafe extern "C" fn(*mut DimeSetupHost, i32)>,
    has_text_service: Option<unsafe extern "C" fn(*mut DimeSetupHost) -> i32>,
    array_scope: Option<unsafe extern "C" fn(*mut DimeSetupHost) -> i32>,
    active_mode: Option<unsafe extern "C" fn(*mut DimeSetupHost) -> i32>,
    do_han_convert: Option<unsafe extern "C" fn(*mut DimeSetupHost) -> i32>,
}

#[repr(C)]
pub struct DimeKeystrokeOut {
    index: u32,
    printable: u16,
    cand_index: u16,
    virtual_key: u32,
    modifiers: u32,
    function: u32,
}

#[repr(C)]
pub struct DimePreservedKey {
    id: u32,
    vkey: u32,
    modifiers: u32,
    desc_len: u32,
}

const _: () = assert!(std::mem::size_of::<DimeKeystrokeOut>() == 20);
const _: () = assert!(std::mem::size_of::<DimePreservedKey>() == 16);

struct FfiHost {
    host: *mut DimeSetupHost,
}

fn raw(s: &[u16]) -> (*const u16, usize) {
    if s.is_empty() { (ptr::null(), 0) } else { (s.as_ptr(), s.len()) }
}

impl FfiHost {
    fn vt(&self) -> Option<&DimeSetupHostVtbl> {
        unsafe {
            if self.host.is_null() {
                return None;
            }
            let v = (*self.host).vtbl;
            if v.is_null() { None } else { Some(&*v) }
        }
    }
}

impl SetupHost for FfiHost {
    fn path_exists(&mut self, path: &[u16]) -> bool {
        let Some(f) = self.vt().and_then(|v| v.path_exists) else { return false };
        let (p, n) = raw(path);
        unsafe { f(self.host, p, n) != 0 }
    }

    fn file_mtime(&mut self, path: &[u16]) -> Option<i64> {
        let f = self.vt().and_then(|v| v.file_mtime)?;
        let (p, n) = raw(path);
        let mut t = 0i64;
        unsafe {
            if f(self.host, p, n, &mut t) != 0 { Some(t) } else { None }
        }
    }

    fn copy_file(&mut self, src: &[u16], dst: &[u16], fail_if_exists: bool) {
        let Some(f) = self.vt().and_then(|v| v.copy_file) else { return };
        let (sp, sn) = raw(src);
        let (dp, dn) = raw(dst);
        unsafe { f(self.host, sp, sn, dp, dn, if fail_if_exists { 1 } else { 0 }) }
    }

    fn create_dir(&mut self, path: &[u16]) {
        let Some(f) = self.vt().and_then(|v| v.create_dir) else { return };
        let (p, n) = raw(path);
        unsafe { f(self.host, p, n) }
    }

    fn path_equal(&mut self, locale: u32, a: &[u16], b: &[u16]) -> bool {
        let Some(f) = self.vt().and_then(|v| v.path_equal) else { return false };
        let (ap, an) = raw(a);
        let (bp, bn) = raw(b);
        unsafe { f(self.host, locale, ap, an, bp, bn) != 0 }
    }

    fn has_file(&mut self, id: i32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.has_file) else { return false };
        unsafe { f(self.host, id) != 0 }
    }

    fn has_engine(&mut self, id: i32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.has_engine) else { return false };
        unsafe { f(self.host, id) != 0 }
    }

    fn dict_type(&mut self, id: i32) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.dict_type) else { return -1 };
        unsafe { f(self.host, id) }
    }

    fn file_updated(&mut self, id: i32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.file_updated) else { return false };
        unsafe { f(self.host, id) != 0 }
    }

    fn file_name(&mut self, id: i32) -> Vec<u16> {
        let Some(f) = self.vt().and_then(|v| v.file_name) else { return Vec::new() };
        let mut buf = vec![0u16; 520];
        let n = unsafe { f(self.host, id, buf.as_mut_ptr(), buf.len()) };
        buf.truncate(n.min(buf.len()));
        buf
    }

    fn drop_file(&mut self, id: i32) {
        let Some(f) = self.vt().and_then(|v| v.drop_file) else { return };
        unsafe { f(self.host, id) }
    }

    fn drop_engine(&mut self, id: i32) {
        let Some(f) = self.vt().and_then(|v| v.drop_engine) else { return };
        unsafe { f(self.host, id) }
    }

    fn new_file(&mut self, id: i32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.new_file) else { return false };
        unsafe { f(self.host, id) != 0 }
    }

    fn create_file(&mut self, id: i32, path: &[u16], share_mode: u32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.create_file) else { return false };
        let (p, n) = raw(path);
        unsafe { f(self.host, id, p, n, share_mode) != 0 }
    }

    fn create_engine(&mut self, id: i32, locale: u32, dict_type: i32) -> bool {
        let Some(f) = self.vt().and_then(|v| v.create_engine) else { return false };
        unsafe { f(self.host, id, locale, dict_type) != 0 }
    }

    fn parse_config(&mut self, id: i32, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.parse_config) else { return };
        unsafe { f(self.host, id, mode) }
    }

    fn alias_phrase_to_main(&mut self, slot: i32) {
        let Some(f) = self.vt().and_then(|v| v.alias_phrase_to_main) else { return };
        unsafe { f(self.host, slot) }
    }

    fn setup_big5(&mut self, mode: i32, locale: u32) {
        let Some(f) = self.vt().and_then(|v| v.setup_big5) else { return };
        unsafe { f(self.host, mode, locale) }
    }

    fn setup_keystroke(&mut self, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.setup_keystroke) else { return };
        unsafe { f(self.host, mode) }
    }

    fn load_config(&mut self, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.load_config) else { return };
        unsafe { f(self.host, mode) }
    }

    fn setup_configuration(&mut self, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.setup_configuration) else { return };
        unsafe { f(self.host, mode) }
    }

    fn setup_candidate_range(&mut self, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.setup_candidate_range) else { return };
        unsafe { f(self.host, mode) }
    }

    fn has_text_service(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.has_text_service) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn array_scope(&mut self) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.array_scope) else { return 0 };
        unsafe { f(self.host) }
    }

    fn active_mode(&mut self) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.active_mode) else { return 0 };
        unsafe { f(self.host) }
    }

    fn do_han_convert(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.do_han_convert) else { return false };
        unsafe { f(self.host) != 0 }
    }
}

fn write_keys(rows: &[engine_setup::Keystroke], out: *mut DimeKeystrokeOut, cap: usize) -> usize {
    if !out.is_null() {
        let n = rows.len().min(cap);
        for (i, row) in rows.iter().take(n).enumerate() {
            unsafe {
                *out.add(i) = DimeKeystrokeOut {
                    index: row.index,
                    printable: row.printable,
                    cand_index: row.cand_index,
                    virtual_key: row.virtual_key,
                    modifiers: row.modifiers,
                    function: row.function,
                };
            }
        }
    }
    rows.len()
}

#[no_mangle]
pub unsafe extern "C" fn dime_setup_dictionary(
    host: *mut DimeSetupHost,
    mode: i32,
    locale: u32,
    settings_build: i32,
    app: *const u16,
    app_len: usize,
    pf: *const u16,
    pf_len: usize,
) -> i32 {
    guard(0, || {
        if host.is_null() {
            return 0;
        }
        let app_s = unsafe { slice(app, app_len) };
        let pf_s = unsafe { slice(pf, pf_len) };
        let mut ffi = FfiHost { host };
        let roots = Roots { app: app_s, pf: pf_s, locale, mode, settings_build: settings_build != 0 };
        if engine_setup::setup_dictionary(&mut ffi, &roots) { 1 } else { 0 }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_setup_han_convert(
    host: *mut DimeSetupHost,
    locale: u32,
    app: *const u16,
    app_len: usize,
    pf: *const u16,
    pf_len: usize,
) -> i32 {
    guard(0, || {
        if host.is_null() {
            return 0;
        }
        let mut ffi = FfiHost { host };
        let app_s = unsafe { slice(app, app_len) };
        let pf_s = unsafe { slice(pf, pf_len) };
        if engine_setup::setup_han_convert(&mut ffi, locale, app_s, pf_s) { 1 } else { 0 }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_setup_tc_freq(
    host: *mut DimeSetupHost,
    locale: u32,
    app: *const u16,
    app_len: usize,
    pf: *const u16,
    pf_len: usize,
) -> i32 {
    guard(0, || {
        if host.is_null() {
            return 0;
        }
        let mut ffi = FfiHost { host };
        let app_s = unsafe { slice(app, app_len) };
        let pf_s = unsafe { slice(pf, pf_len) };
        if engine_setup::setup_tc_freq(&mut ffi, locale, app_s, pf_s) { 1 } else { 0 }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_build_keystrokes(
    mode: i32,
    radicals: *const u16,
    radical_count: usize,
    endkey: *const u16,
    endkey_len: usize,
    out: *mut DimeKeystrokeOut,
    cap: usize,
    inserted_equal: *mut i32,
) -> usize {
    guard(0, || {
        let rad = unsafe { slice(radicals, radical_count) };
        let end = unsafe { slice(endkey, endkey_len) };
        let (rows, inserted) = engine_setup::build_keystrokes(mode, rad, end);
        if !inserted_equal.is_null() {
            unsafe { *inserted_equal = if inserted { 1 } else { 0 } };
        }
        write_keys(&rows, out, cap)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_build_candidate_range(
    mode: i32,
    global_mode: i32,
    dayi_selkey_mode: i32,
    space_as_first: i32,
    array_scope: i32,
    selkey: *const u16,
    selkey_len: usize,
    cand: *mut DimeKeystrokeOut,
    phrase: *mut DimeKeystrokeOut,
    cap: usize,
    page_size: *mut u32,
) -> usize {
    guard(0, || {
        let sel = unsafe { slice(selkey, selkey_len) };
        let (page, cand_rows, phrase_rows) = engine_setup::build_candidate_range(
            mode,
            global_mode,
            dayi_selkey_mode,
            space_as_first != 0,
            array_scope,
            sel,
        );
        if !page_size.is_null() {
            unsafe { *page_size = page };
        }
        let n = write_keys(&cand_rows, cand, cap);
        let _ = write_keys(&phrase_rows, phrase, cap);
        n.min(cap)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_preserved_key_count() -> i32 {
    engine_setup::preserved_keys().len() as i32
}

#[no_mangle]
pub unsafe extern "C" fn dime_preserved_key_at(
    index: i32,
    out: *mut DimePreservedKey,
    desc: *mut u16,
    desc_cap: usize,
) -> i32 {
    guard(0, || {
        let keys = engine_setup::preserved_keys();
        if out.is_null() || index < 0 || index as usize >= keys.len() {
            return 0;
        }
        let key = keys[index as usize];
        let text: Vec<u16> = key.description.map(|s| s.encode_utf16().collect()).unwrap_or_default();
        let desc_len = if key.description.is_some() { text.len() as u32 } else { 0 };
        if !desc.is_null() && desc_cap > 0 && !text.is_empty() {
            let n = text.len().min(desc_cap);
            unsafe { ptr::copy_nonoverlapping(text.as_ptr(), desc, n) };
        }
        unsafe {
            *out = DimePreservedKey { id: key.id, vkey: key.vkey, modifiers: key.modifiers, desc_len };
        }
        1
    })
}
