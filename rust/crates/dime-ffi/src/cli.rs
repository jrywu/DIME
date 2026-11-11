//! Phase 5a CLI. Vtable order matches `DimeCliHostVtbl` in `include/dime_ffi.h`
//! and `kCliVtbl` in `DIMESettings/CLI.cpp`.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_config::cli::{self, CliHost, WriteUtf16};

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

#[repr(C)]
pub struct DimeCliHost {
    vtbl: *const DimeCliHostVtbl,
    ctx: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct DimeCliHostVtbl {
    write_out: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize)>,
    write_err: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize)>,
    load_config: Option<unsafe extern "C" fn(*mut DimeCliHost, i32)>,
    write_config: Option<unsafe extern "C" fn(*mut DimeCliHost, i32)>,
    reset_defaults: Option<unsafe extern "C" fn(*mut DimeCliHost, i32)>,
    get_value: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut *mut super::DimeBuffer)>,
    set_value: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *const u16, usize, *mut *mut super::DimeBuffer) -> i32>,
    file_exists: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize) -> i32>,
    load_text: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize, *mut *mut u16, *mut usize) -> i32>,
    free_text: Option<unsafe extern "C" fn(*mut DimeCliHost, *mut u16)>,
    begin_validate: Option<unsafe extern "C" fn(*mut DimeCliHost, i32) -> u32>,
    validate_line: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize, i32, u32) -> i32>,
    end_validate: Option<unsafe extern "C" fn(*mut DimeCliHost)>,
    custom_txt_path: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>,
    custom_cin_path: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>,
    write_utf16_bom: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize, *const u16, usize, *mut u32) -> i32>,
    parse_cin: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize, *const u16, usize, i32) -> i32>,
    export_src_path: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>,
    copy_file: Option<unsafe extern "C" fn(*mut DimeCliHost, *const u16, usize, *const u16, usize, *mut u32) -> i32>,
    main_dest: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>,
    phrase_dest: Option<unsafe extern "C" fn(*mut DimeCliHost, *mut u16, usize) -> usize>,
    array_dest: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>,
}

struct FfiHost {
    host: *mut DimeCliHost,
}

impl FfiHost {
    fn vt(&self) -> Option<DimeCliHostVtbl> {
        unsafe {
            if self.host.is_null() {
                return None;
            }
            let p = (*self.host).vtbl;
            if p.is_null() { None } else { Some(*p) }
        }
    }
}

fn read_path(buf: &[u16], n: usize) -> Vec<u16> {
    let n = n.min(buf.len().saturating_sub(1));
    let n = buf[..n].iter().position(|&c| c == 0).unwrap_or(n);
    buf[..n].to_vec()
}

unsafe fn take_buffer(b: *mut super::DimeBuffer) -> Option<Vec<u16>> {
    if b.is_null() {
        return None;
    }
    let mut len = 0usize;
    let p = unsafe { super::dime_buffer_data(b, &mut len) };
    let v = unsafe { slice(p, len) }.to_vec();
    unsafe { super::dime_buffer_free(b) };
    Some(v)
}

impl CliHost for FfiHost {
    fn write_out(&mut self, text: &[u16]) {
        let Some(f) = self.vt().and_then(|v| v.write_out) else { return };
        if text.is_empty() { return; }
        unsafe { f(self.host, text.as_ptr(), text.len()) };
    }

    fn write_err(&mut self, text: &[u16]) {
        let Some(f) = self.vt().and_then(|v| v.write_err) else { return };
        if text.is_empty() { return; }
        unsafe { f(self.host, text.as_ptr(), text.len()) };
    }

    fn load_config(&mut self, mode: i32) {
        if let Some(f) = self.vt().and_then(|v| v.load_config) {
            unsafe { f(self.host, mode) };
        }
    }

    fn write_config(&mut self, mode: i32) {
        if let Some(f) = self.vt().and_then(|v| v.write_config) {
            unsafe { f(self.host, mode) };
        }
    }

    fn reset_defaults(&mut self, mode: i32) {
        if let Some(f) = self.vt().and_then(|v| v.reset_defaults) {
            unsafe { f(self.host, mode) };
        }
    }

    fn get_value(&mut self, index: i32) -> Option<Vec<u16>> {
        let f = self.vt().and_then(|v| v.get_value)?;
        let mut b = ptr::null_mut();
        unsafe { f(self.host, index, &mut b) };
        unsafe { take_buffer(b) }
    }

    fn set_value(&mut self, index: i32, value: &[u16]) -> (i32, Vec<u16>) {
        let Some(f) = self.vt().and_then(|v| v.set_value) else { return (2, Vec::new()) };
        let mut err = ptr::null_mut();
        let rc = unsafe { f(self.host, index, value.as_ptr(), value.len(), &mut err) };
        let msg = unsafe { take_buffer(err) }.unwrap_or_default();
        (rc, msg)
    }

    fn file_exists(&mut self, path: &[u16]) -> bool {
        let Some(f) = self.vt().and_then(|v| v.file_exists) else { return false };
        unsafe { f(self.host, path.as_ptr(), path.len()) != 0 }
    }

    fn load_text(&mut self, path: &[u16]) -> Option<Vec<u16>> {
        let f = self.vt().and_then(|v| v.load_text)?;
        let mut p = ptr::null_mut();
        let mut n = 0usize;
        let ok = unsafe { f(self.host, path.as_ptr(), path.len(), &mut p, &mut n) };
        if ok == 0 {
            return None;
        }
        let v = if p.is_null() || n == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(p, n) }.to_vec()
        };
        if !p.is_null() {
            if let Some(free) = self.vt().and_then(|vt| vt.free_text) {
                unsafe { free(self.host, p) };
            }
        }
        Some(v)
    }

    fn begin_validate(&mut self, mode: i32) -> u32 {
        let Some(f) = self.vt().and_then(|v| v.begin_validate) else { return 0 };
        unsafe { f(self.host, mode) }
    }

    fn validate_line(&mut self, line: &[u16], mode: i32, max_codes: u32) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.validate_line) else { return 1 };
        let p = if line.is_empty() { ptr::null() } else { line.as_ptr() };
        unsafe { f(self.host, p, line.len(), mode, max_codes) }
    }

    fn end_validate(&mut self) {
        if let Some(f) = self.vt().and_then(|v| v.end_validate) {
            unsafe { f(self.host) };
        }
    }

    fn custom_txt_path(&mut self, mode: i32) -> Vec<u16> {
        self.path_mode(mode, self.vt().and_then(|v| v.custom_txt_path))
    }

    fn custom_cin_path(&mut self, mode: i32) -> Vec<u16> {
        self.path_mode(mode, self.vt().and_then(|v| v.custom_cin_path))
    }

    fn write_utf16_bom(&mut self, path: &[u16], text: &[u16]) -> WriteUtf16 {
        let Some(f) = self.vt().and_then(|v| v.write_utf16_bom) else { return WriteUtf16::WriteFailed };
        let mut gle = 0u32;
        match unsafe { f(self.host, path.as_ptr(), path.len(), text.as_ptr(), text.len(), &mut gle) } {
            0 => WriteUtf16::Ok,
            1 => WriteUtf16::OpenFailed(gle),
            _ => WriteUtf16::WriteFailed,
        }
    }

    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom: bool) -> bool {
        let Some(f) = self.vt().and_then(|v| v.parse_cin) else { return false };
        unsafe { f(self.host, src.as_ptr(), src.len(), dst.as_ptr(), dst.len(), i32::from(custom)) != 0 }
    }

    fn export_src_path(&mut self, mode: i32) -> Vec<u16> {
        self.path_mode(mode, self.vt().and_then(|v| v.export_src_path))
    }

    fn copy_file(&mut self, src: &[u16], dst: &[u16]) -> Option<u32> {
        let Some(f) = self.vt().and_then(|v| v.copy_file) else { return Some(0) };
        let mut gle = 0u32;
        let ok = unsafe { f(self.host, src.as_ptr(), src.len(), dst.as_ptr(), dst.len(), &mut gle) };
        if ok != 0 { None } else { Some(gle) }
    }

    fn main_dest(&mut self, mode: i32) -> Vec<u16> {
        self.path_mode(mode, self.vt().and_then(|v| v.main_dest))
    }

    fn phrase_dest(&mut self) -> Vec<u16> {
        let Some(f) = self.vt().and_then(|v| v.phrase_dest) else { return Vec::new() };
        let mut buf = [0u16; 260];
        let n = unsafe { f(self.host, buf.as_mut_ptr(), buf.len()) };
        read_path(&buf, n)
    }

    fn array_dest(&mut self, table: i32) -> Vec<u16> {
        self.path_mode(table, self.vt().and_then(|v| v.array_dest))
    }
}

impl FfiHost {
    fn path_mode(&mut self, mode: i32, f: Option<unsafe extern "C" fn(*mut DimeCliHost, i32, *mut u16, usize) -> usize>) -> Vec<u16> {
        let Some(f) = f else { return Vec::new() };
        let mut buf = [0u16; 260];
        let n = unsafe { f(self.host, mode, buf.as_mut_ptr(), buf.len()) };
        read_path(&buf, n)
    }
}

unsafe fn collect<'a>(argv: *const *const u16, lens: *const usize, argc: usize) -> Vec<&'a [u16]> {
    if argv.is_null() || lens.is_null() || argc == 0 {
        return Vec::new();
    }
    let ptrs = unsafe { std::slice::from_raw_parts(argv, argc) };
    let lens = unsafe { std::slice::from_raw_parts(lens, argc) };
    ptrs.iter().zip(lens).map(|(&p, &n)| unsafe { slice(p, n) }).collect()
}

/// 1 on success. On failure `*error` (if non-NULL) receives the message,
/// including its trailing newline; free it with `dime_buffer_free`.
#[no_mangle]
pub unsafe extern "C" fn dime_cli_parse(
    argv: *const *const u16,
    lens: *const usize,
    argc: usize,
    out: *mut cli::DimeCliArgs,
    error: *mut *mut super::DimeBuffer,
) -> i32 {
    guard(0, || {
        if !error.is_null() {
            unsafe { *error = ptr::null_mut() };
        }
        let Some(out) = (unsafe { out.as_mut() }) else { return 0 };
        let args = unsafe { collect(argv, lens, argc) };
        match cli::parse(&args) {
            Ok(p) => {
                *out = cli::to_ffi(&p);
                1
            }
            Err(e) => {
                if !error.is_null() {
                    unsafe { *error = Box::into_raw(Box::new(super::DimeBuffer(e))) };
                }
                0
            }
        }
    })
}

/// Exit code 0 / 1 / 2 / 3. A null host returns 1.
#[no_mangle]
pub unsafe extern "C" fn dime_cli_run(
    argv: *const *const u16,
    lens: *const usize,
    argc: usize,
    host: *mut DimeCliHost,
) -> i32 {
    guard(1, || {
        if host.is_null() {
            return 1;
        }
        let args = unsafe { collect(argv, lens, argc) };
        let mut ffi = FfiHost { host };
        cli::run(&args, &mut ffi)
    })
}
