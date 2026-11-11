//! Phase 5b-1. Window-free `SettingsModel` functions. The snapshot pointer is
//! a C++ `SettingsSnapshot` (216 bytes, checked on both sides).

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_config::settings_model::{self, LineErrorEntry, SettingsSnapshot};
use dime_config::ImeMode;

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn slice<'a>(p: *const u16, len: usize) -> &'a [u16] {
    if p.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: caller guarantees `p` points at `len` readable units.
        unsafe { std::slice::from_raw_parts(p, len) }
    }
}

/// Copy a NUL-terminated UTF-16 string, stopping at `cap` units.
unsafe fn cstr_units(p: *const u16, cap: usize) -> Vec<u16> {
    let mut out = Vec::new();
    if p.is_null() {
        return out;
    }
    // SAFETY: caller passed a C string, or we stop after `cap` units.
    unsafe {
        let mut i = 0;
        while i < cap {
            let c = *p.add(i);
            if c == 0 {
                break;
            }
            out.push(c);
            i += 1;
        }
    }
    out
}

#[repr(C)]
pub struct DimeLineError {
    code: i32,
    start: i32,
    len: i32,
}

fn write_errors(errors: &[LineErrorEntry], out: *mut DimeLineError, cap: usize, out_count: *mut usize) {
    let n = errors.len().min(cap);
    if !out.is_null() {
        for (i, e) in errors.iter().take(n).enumerate() {
            // SAFETY: caller provided `cap` slots, or `out` is null.
            unsafe {
                *out.add(i) = DimeLineError {
                    code: e.error as i32,
                    start: e.start,
                    len: e.len,
                };
            }
        }
    }
    if !out_count.is_null() {
        // SAFETY: caller provided the out-pointer, or it is null.
        unsafe { *out_count = n };
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_mode_bitmask(mode: i32) -> i32 {
    guard(0, || settings_model::mode_bitmask(ImeMode::from_i32(mode)))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_sidebar_hit_test(y: i32, item_height: i32, item_count: i32) -> i32 {
    guard(-1, || settings_model::sidebar_hit_test(y, item_height, item_count))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_scroll_range(content_height: i32, viewport_height: i32) -> i32 {
    guard(0, || {
        settings_model::compute_scroll_range(content_height, viewport_height)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_clamp_scroll(pos: i32, max_scroll: i32) -> i32 {
    guard(0, || settings_model::clamp_scroll_pos(pos, max_scroll))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_ime_mode_to_string(mode: i32) -> *const u16 {
    guard(ptr::null(), || {
        match settings_model::ime_mode_to_string(ImeMode::from_i32(mode)) {
            Some(s) => s.as_ptr(),
            None => ptr::null(),
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_string_to_ime_mode(text: *const u16) -> i32 {
    guard(ImeMode::None as i32, || {
        if text.is_null() {
            return ImeMode::None as i32;
        }
        // SAFETY: `text` is a C string; the scan stops at NUL or 64 units.
        let units = unsafe { cstr_units(text, 64) };
        settings_model::string_to_ime_mode(Some(&units)) as i32
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_index_to_mode(index: i32) -> i32 {
    guard(ImeMode::Dayi as i32, || settings_model::index_to_mode(index) as i32)
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_mode_to_index(mode: i32) -> i32 {
    guard(0, || settings_model::mode_to_index(ImeMode::from_i32(mode)))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_sync_toggle(snapshot: *mut c_void, id: i32, is_on: i32) {
    guard((), || {
        if snapshot.is_null() {
            return;
        }
        // SAFETY: `snapshot` is a `SettingsSnapshot` for the life of the call.
        let snap = unsafe { &mut *(snapshot as *mut SettingsSnapshot) };
        settings_model::sync_toggle(snap, id, is_on != 0);
    });
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_sync_combo(snapshot: *mut c_void, id: i32, sel: i32) {
    guard((), || {
        if snapshot.is_null() {
            return;
        }
        // SAFETY: `snapshot` is a `SettingsSnapshot` for the life of the call.
        let snap = unsafe { &mut *(snapshot as *mut SettingsSnapshot) };
        settings_model::sync_combo(snap, id, sel);
    });
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_parse_max_codes(text: *const u16, out_val: *mut u32) -> i32 {
    guard(0, || {
        if text.is_null() {
            return 0;
        }
        // SAFETY: `text` is a C string; the scan stops at NUL or 256 units.
        let units = unsafe { cstr_units(text, 256) };
        match settings_model::parse_max_codes(Some(&units)) {
            Some(v) => {
                if !out_val.is_null() {
                    // SAFETY: caller provided the out-pointer, or it is null.
                    unsafe { *out_val = v };
                }
                1
            }
            None => 0,
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_load_table_suffix(id: i32) -> *const u16 {
    guard(ptr::null(), || match settings_model::load_table_suffix(id) {
        Some(s) => s.as_ptr(),
        None => ptr::null(),
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_validate_line(
    line: *const u16,
    len: usize,
    mode: i32,
    max_codes: u32,
    char_ok: Option<unsafe extern "C" fn(u16, *mut c_void) -> i32>,
    ctx: *mut c_void,
    out: *mut DimeLineError,
    cap: usize,
    out_count: *mut usize,
) -> i32 {
    guard(0, || {
        // SAFETY: null `line` is an empty line; otherwise `len` units are readable.
        let units = unsafe { slice(line, len) };
        let errors = if let Some(f) = char_ok {
            let mut check = |c: u16| unsafe { f(c, ctx) != 0 };
            settings_model::validate_line(units, ImeMode::from_i32(mode), max_codes, Some(&mut check))
        } else {
            settings_model::validate_line(units, ImeMode::from_i32(mode), max_codes, None)
        };
        let valid = errors.is_empty();
        write_errors(&errors, out, cap, out_count);
        if valid {
            1
        } else {
            0
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_load_snapshot(out: *mut SettingsSnapshot, colors: *const u32) {
    guard((), || {
        if out.is_null() {
            return;
        }
        let effective = if colors.is_null() {
            [0u32; 6]
        } else {
            // SAFETY: caller passed six COLORREFs, or the pointer is null.
            let slice = unsafe { std::slice::from_raw_parts(colors, 6) };
            [slice[0], slice[1], slice[2], slice[3], slice[4], slice[5]]
        };
        // SAFETY: `out` is a SettingsSnapshot, and the global values live for the process.
        let values = unsafe { &*crate::global_config_values() };
        let snap = settings_model::load_snapshot(values, &effective);
        unsafe { *out = snap };
    });
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_apply_snapshot(snap: *const SettingsSnapshot) {
    guard((), || {
        if snap.is_null() {
            return;
        }
        // SAFETY: `snap` is a SettingsSnapshot. The global values live for the process.
        let snap = unsafe { &*snap };
        let values = unsafe { &mut *crate::global_config_values() };
        settings_model::apply_snapshot(values, snap);
    });
}
