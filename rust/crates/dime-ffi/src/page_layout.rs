//! Phase 5b-2. Settings page layout tables. Pointers stay valid for the
//! process lifetime. A null `count` is not written.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use dime_config::page_layout;

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn write_count(count: *mut i32, n: usize) {
    if !count.is_null() {
        // SAFETY: caller provided the out-pointer, or it is null.
        unsafe { *count = n as i32 };
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_ime_page_layout(count: *mut i32) -> *const c_void {
    guard(std::ptr::null(), || {
        let cards = page_layout::ime_cards();
        unsafe { write_count(count, cards.len()) };
        cards.as_ptr().cast()
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_load_table_page_layout(count: *mut i32) -> *const c_void {
    guard(std::ptr::null(), || {
        let cards = page_layout::load_cards();
        unsafe { write_count(count, cards.len()) };
        cards.as_ptr().cast()
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_sidebar_items(count: *mut i32) -> *const c_void {
    guard(std::ptr::null(), || {
        let items = page_layout::sidebar_items();
        unsafe { write_count(count, items.len()) };
        items.as_ptr().cast()
    })
}
