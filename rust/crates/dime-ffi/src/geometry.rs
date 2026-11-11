//! Phase 5b-3. Settings geometry and theme colors. A null `out` is not written.
//! A null `sys` is treated as all zeros.

use std::panic::{catch_unwind, AssertUnwindSafe};

use dime_config::geometry::{self, SettingsGeometry, SysColors, ThemeColors};

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_geometry(out: *mut SettingsGeometry) {
    guard((), || {
        if !out.is_null() {
            // SAFETY: caller provided the out-pointer, or it is null.
            unsafe { *out = *geometry::defaults() };
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_theme_colors(is_dark: i32, sys: *const SysColors, out: *mut ThemeColors) {
    guard((), || {
        if out.is_null() {
            return;
        }
        let colors = if sys.is_null() {
            SysColors::default()
        } else {
            unsafe { *sys }
        };
        // SAFETY: caller provided `out`, and `sys` is null or a readable `SysColors`.
        unsafe { *out = geometry::theme_colors(is_dark != 0, colors) };
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_blend_color(c1: u32, c2: u32, alpha: i32) -> u32 {
    guard(0, || geometry::blend_color(c1, c2, alpha))
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_scale_for_dpi(value: i32, dpi: i32) -> i32 {
    guard(0, || geometry::scale_for_dpi(value, dpi))
}
