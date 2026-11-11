//! Phase 4d: language-bar decisions. Vtable order matches `DimeLangHostVtbl`
//! in `include/dime_ffi.h` and `CLangBarHost::Vtbl` in `LanguageBar.cpp`.

use std::panic::{catch_unwind, AssertUnwindSafe};

use dime_core::langbar::{self, LangHost};

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

#[repr(C)]
pub struct DimeLangHost {
    vtbl: *const DimeLangHostVtbl,
    ctx: *mut std::ffi::c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DimeLangHostVtbl {
    has_conversion: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    get_conversion: Option<unsafe extern "C" fn(*mut DimeLangHost, *mut u32) -> i32>,
    set_conversion: Option<unsafe extern "C" fn(*mut DimeLangHost, u32)>,
    get_bool: Option<unsafe extern "C" fn(*mut DimeLangHost, i32, *mut i32) -> i32>,
    set_bool: Option<unsafe extern "C" fn(*mut DimeLangHost, i32, i32)>,
    windows8: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    set_chinese: Option<unsafe extern "C" fn(*mut DimeLangHost, i32)>,
    set_full_shape: Option<unsafe extern "C" fn(*mut DimeLangHost, i32)>,
    end_composition_if_any: Option<unsafe extern "C" fn(*mut DimeLangHost)>,
    delete_candidate: Option<unsafe extern "C" fn(*mut DimeLangHost, i32)>,
    width_mode: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    set_width_mode: Option<unsafe extern "C" fn(*mut DimeLangHost, i32)>,
    notify_desktop: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    show_text: Option<unsafe extern "C" fn(*mut DimeLangHost, *const u16, usize, u32)>,
    shift_mode: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    shift_key_only: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    modifiers: Option<unsafe extern "C" fn(*mut DimeLangHost) -> u32>,
    do_han: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
    set_do_han: Option<unsafe extern "C" fn(*mut DimeLangHost, i32)>,
    write_config: Option<unsafe extern "C" fn(*mut DimeLangHost)>,
    setup_han: Option<unsafe extern "C" fn(*mut DimeLangHost)>,
    inject_space: Option<unsafe extern "C" fn(*mut DimeLangHost)>,
    show_config: Option<unsafe extern "C" fn(*mut DimeLangHost)>,
    has_text_service: Option<unsafe extern "C" fn(*mut DimeLangHost) -> i32>,
}

struct FfiHost {
    host: *mut DimeLangHost,
}

impl FfiHost {
    /// Copy the vtable so a re-entrant callback does not borrow `self`.
    fn vt(&self) -> Option<DimeLangHostVtbl> {
        unsafe {
            if self.host.is_null() {
                return None;
            }
            let p = (*self.host).vtbl;
            if p.is_null() { None } else { Some(*p) }
        }
    }
}

fn flag(v: i32) -> bool { v != 0 }

impl LangHost for FfiHost {
    fn has_conversion(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.has_conversion) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn get_conversion(&mut self) -> Option<u32> {
        let f = self.vt().and_then(|v| v.get_conversion)?;
        let mut mode = 0u32;
        unsafe {
            if f(self.host, &mut mode) != 0 { Some(mode) } else { None }
        }
    }

    fn set_conversion(&mut self, mode: u32) {
        let Some(f) = self.vt().and_then(|v| v.set_conversion) else { return };
        unsafe { f(self.host, mode) }
    }

    fn get_bool(&mut self, id: i32) -> Option<bool> {
        let f = self.vt().and_then(|v| v.get_bool)?;
        let mut out = 0i32;
        unsafe {
            if f(self.host, id, &mut out) != 0 { Some(out != 0) } else { None }
        }
    }

    fn set_bool(&mut self, id: i32, value: bool) {
        let Some(f) = self.vt().and_then(|v| v.set_bool) else { return };
        unsafe { f(self.host, id, if value { 1 } else { 0 }) }
    }

    fn windows8(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.windows8) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn set_chinese(&mut self, on: bool) {
        let Some(f) = self.vt().and_then(|v| v.set_chinese) else { return };
        unsafe { f(self.host, if on { 1 } else { 0 }) }
    }

    fn set_full_shape(&mut self, on: bool) {
        let Some(f) = self.vt().and_then(|v| v.set_full_shape) else { return };
        unsafe { f(self.host, if on { 1 } else { 0 }) }
    }

    fn end_composition_if_any(&mut self) {
        let Some(f) = self.vt().and_then(|v| v.end_composition_if_any) else { return };
        unsafe { f(self.host) }
    }

    fn delete_candidate(&mut self, force: bool) {
        let Some(f) = self.vt().and_then(|v| v.delete_candidate) else { return };
        unsafe { f(self.host, if force { 1 } else { 0 }) }
    }

    fn width_mode(&mut self) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.width_mode) else { return 0 };
        unsafe { f(self.host) }
    }

    fn set_width_mode(&mut self, mode: i32) {
        let Some(f) = self.vt().and_then(|v| v.set_width_mode) else { return };
        unsafe { f(self.host, mode) }
    }

    fn notify_desktop(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.notify_desktop) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn show_text(&mut self, text: &[u16], delay: u32) {
        let Some(f) = self.vt().and_then(|v| v.show_text) else { return };
        let (p, n) = if text.is_empty() { (std::ptr::null(), 0) } else { (text.as_ptr(), text.len()) };
        unsafe { f(self.host, p, n, delay) }
    }

    fn shift_mode(&mut self) -> i32 {
        let Some(f) = self.vt().and_then(|v| v.shift_mode) else { return 0 };
        unsafe { f(self.host) }
    }

    fn shift_key_only(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.shift_key_only) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn modifiers(&mut self) -> u16 {
        let Some(f) = self.vt().and_then(|v| v.modifiers) else { return 0 };
        unsafe { f(self.host) as u16 }
    }

    fn do_han(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.do_han) else { return false };
        unsafe { f(self.host) != 0 }
    }

    fn set_do_han(&mut self, on: bool) {
        let Some(f) = self.vt().and_then(|v| v.set_do_han) else { return };
        unsafe { f(self.host, if on { 1 } else { 0 }) }
    }

    fn write_config(&mut self) {
        let Some(f) = self.vt().and_then(|v| v.write_config) else { return };
        unsafe { f(self.host) }
    }

    fn setup_han(&mut self) {
        let Some(f) = self.vt().and_then(|v| v.setup_han) else { return };
        unsafe { f(self.host) }
    }

    fn inject_space(&mut self) {
        let Some(f) = self.vt().and_then(|v| v.inject_space) else { return };
        unsafe { f(self.host) }
    }

    fn show_config(&mut self) {
        let Some(f) = self.vt().and_then(|v| v.show_config) else { return };
        unsafe { f(self.host) }
    }

    fn has_text_service(&mut self) -> bool {
        let Some(f) = self.vt().and_then(|v| v.has_text_service) else { return false };
        unsafe { f(self.host) != 0 }
    }
}

fn run(host: *mut DimeLangHost, body: impl FnOnce(&mut FfiHost)) {
    if host.is_null() {
        return;
    }
    body(&mut FfiHost { host });
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_conversion(host: *mut DimeLangHost, has_override: i32, override_open: i32) {
    guard((), || run(host, |h| {
        let ov = if has_override != 0 { Some(flag(override_open)) } else { None };
        langbar::conversion_updated(h, ov);
    }));
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_private(host: *mut DimeLangHost) {
    guard((), || run(host, langbar::private_updated));
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_keyboard(host: *mut DimeLangHost, which: i32) {
    guard((), || run(host, |h| langbar::keyboard_updated(h, which)));
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_preserved(host: *mut DimeLangHost, id: i32) -> i32 {
    guard(1, || {
        if host.is_null() {
            return 1;
        }
        let mut h = FfiHost { host };
        if langbar::preserved(&mut h, id) { 1 } else { 0 }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_keyboard_side(host: *mut DimeLangHost, open: i32) {
    guard((), || run(host, |h| langbar::keyboard_side(h, flag(open))));
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_shape(host: *mut DimeLangHost, full: i32) {
    guard((), || run(host, |h| langbar::shape_side(h, flag(full))));
}

#[no_mangle]
pub unsafe extern "C" fn dime_langbar_show(host: *mut DimeLangHost, kind: i32, on: i32, delay: u32) {
    guard((), || run(host, |h| langbar::show(h, kind, flag(on), delay)));
}
