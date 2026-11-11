//! Phase 5b-5. `LoadTextFileAsUtf16` byte ladder. A null `out_wlen` or `out`
//! is not written. A null `bytes` with a non-zero length is rejected.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_config::text_file;

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

extern "system" {
    fn MultiByteToWideChar(
        code_page: u32,
        flags: u32,
        multi_byte: *const u8,
        cb_multi_byte: i32,
        wide: *mut u16,
        cch_wide: i32,
    ) -> i32;
}

/// `MultiByteToWideChar`. A length that does not fit in a positive `int`
/// fails the call. The C++ cast to `int` would pass a negative length and
/// read a NUL-terminated string past the buffer.
fn win_convert(cp: u32, flags: u32, bytes: &[u8]) -> Option<Vec<u16>> {
    if bytes.len() > i32::MAX as usize {
        return None;
    }
    let cb = bytes.len() as i32;
    let src = if bytes.is_empty() { ptr::null() } else { bytes.as_ptr() };
    // SAFETY: `src` is null only when `cb` is 0, which the API rejects.
    let needed = unsafe { MultiByteToWideChar(cp, flags, src, cb, ptr::null_mut(), 0) };
    if needed <= 0 {
        return None;
    }
    let mut out = vec![0u16; needed as usize];
    // SAFETY: `out` holds `needed` units and `src`/`cb` are unchanged.
    let wrote = unsafe { MultiByteToWideChar(cp, flags, src, cb, out.as_mut_ptr(), needed) };
    if wrote <= 0 {
        // The sizing call succeeded and the fill call failed. C++ still
        // returns a buffer and stores a 0 length.
        return Some(Vec::new());
    }
    out.truncate(wrote as usize);
    Some(out)
}

fn decode(bytes: *const u8, len: u32) -> Option<Vec<u16>> {
    if bytes.is_null() {
        if len != 0 {
            return None;
        }
        return text_file::decode_text_bytes(&[], win_convert);
    }
    // SAFETY: caller guarantees `bytes` points at `len` readable bytes.
    let slice = unsafe { std::slice::from_raw_parts(bytes, len as usize) };
    text_file::decode_text_bytes(slice, win_convert)
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_decode_text_len(bytes: *const u8, len: u32, out_wlen: *mut u32) -> i32 {
    guard(0, || {
        if out_wlen.is_null() {
            return 0;
        }
        match decode(bytes, len) {
            Some(text) => {
                let n = u32::try_from(text.len()).unwrap_or(u32::MAX);
                if n as usize != text.len() {
                    return 0;
                }
                // SAFETY: caller provided `out_wlen`.
                unsafe { *out_wlen = n };
                1
            }
            None => 0,
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_decode_text(bytes: *const u8, len: u32, out: *mut u16, cap: u32) -> i32 {
    guard(0, || {
        if out.is_null() {
            return 0;
        }
        let Some(text) = decode(bytes, len) else {
            return 0;
        };
        if text.len() != cap as usize {
            return 0;
        }
        if cap > 0 {
            // SAFETY: `out` has room for `cap` units, matching `text`.
            unsafe { ptr::copy_nonoverlapping(text.as_ptr(), out, text.len()) };
        }
        1
    })
}
