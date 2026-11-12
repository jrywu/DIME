//! The settings window procedures (phase 5b-9a).
//!
//! Port of `SettingsProc::Cpp::Main` / `Content` (`src/DIMESettings/
//! SettingsProc.cpp`, formerly `SettingsWindow::WndProc` / `ContentWndProc`):
//! which message does what, how its parameters are decoded, and what it
//! returns. Every side effect, including `DefWindowProc`,
//! `PostQuitMessage`, and `SetForegroundWindow`, goes through `ProcHost`.
//!
//! Pointer messages (`WM_CREATE`, `WM_GETMINMAXINFO`, `WM_COPYDATA`,
//! `WM_DPICHANGED`) read `lparam` as Windows passes it; callers must pass
//! the matching structure, as Windows does.

use dime_config::ImeMode;

use crate::lifecycle::copy_data_mode;
use crate::nav;

pub const WM_CREATE: u32 = 0x0001;
pub const WM_DESTROY: u32 = 0x0002;
pub const WM_SIZE: u32 = 0x0005;
pub const WM_PAINT: u32 = 0x000F;
pub const WM_ERASEBKGND: u32 = 0x0014;
pub const WM_SETTINGCHANGE: u32 = 0x001A;
pub const WM_GETMINMAXINFO: u32 = 0x0024;
pub const WM_DRAWITEM: u32 = 0x002B;
pub const WM_COPYDATA: u32 = 0x004A;
pub const WM_COMMAND: u32 = 0x0111;
pub const WM_VSCROLL: u32 = 0x0115;
pub const WM_CTLCOLOREDIT: u32 = 0x0133;
pub const WM_CTLCOLORLISTBOX: u32 = 0x0134;
pub const WM_MOUSEMOVE: u32 = 0x0200;
pub const WM_LBUTTONDOWN: u32 = 0x0201;
pub const WM_MOUSEWHEEL: u32 = 0x020A;
pub const WM_MOUSELEAVE: u32 = 0x02A3;
pub const WM_DPICHANGED: u32 = 0x02E0;
pub const WM_THEMECHANGED: u32 = 0x031A;
/// `SettingsEditor::kMsgRevalidate`.
pub const WM_EDITOR_REVALIDATE: u32 = 0x0400 + 1;
/// `SettingsEditor::kMsgViewport`.
pub const WM_EDITOR_VIEWPORT: u32 = 0x0400 + 3;

/// What the window procedures ask of the window (`SettingsProc::Host`).
pub trait ProcHost {
    /// `WindowData` is attached to the window.
    fn has_data(&mut self) -> bool;
    /// The content area exists.
    fn has_content(&mut self) -> bool;
    /// `WM_CREATE`: create the window data and the content area.
    fn create(&mut self, instance: isize);
    /// `RunNav` with a `SettingsNav` event (dpi and the rest filled by the host).
    fn nav(&mut self, kind: i32, x: i32, y: i32, delta: i32, thumb: i32);
    fn theme_changed(&mut self);
    fn setting_changed(&mut self);
    /// `WM_DPICHANGED`: new dpi, and the suggested rectangle when given.
    fn dpi_changed(&mut self, dpi: u32, rect: Option<[i32; 4]>);
    fn paint_main(&mut self);
    fn paint_content(&mut self);
    /// LOGPIXELSY of the screen DC (96 when it can't be read).
    fn screen_dpi(&mut self) -> u32;
    fn switch_mode(&mut self, mode: i32);
    fn set_foreground(&mut self);
    /// `WM_DESTROY`: save the placement and free the window data.
    fn destroy(&mut self);
    /// `PostQuitMessage(0)`.
    fn quit(&mut self);
    /// `HandleCommand` for the content area's `WM_COMMAND`.
    fn command(&mut self, wparam: usize, lparam: isize);
    /// `kMsgRevalidate`: validate the viewport, enable Save, reset the
    /// keystroke count.
    fn editor_revalidate(&mut self);
    /// `kMsgViewport`: validate the newly visible lines.
    fn editor_viewport(&mut self);
    /// `SettingsOwnerDraw::DrawItem` with a non-null `DRAWITEMSTRUCT*`.
    fn draw_item(&mut self, dis: isize) -> bool;
    /// `SettingsOwnerDraw::CtlColor`: the brush when handled.
    fn ctl_color(&mut self, hdc: isize) -> Option<isize>;
    /// `DefWindowProc`.
    fn default(&mut self, msg: u32, wparam: usize, lparam: isize) -> isize;
}

fn loword(v: usize) -> u16 {
    (v & 0xFFFF) as u16
}

fn hiword(v: usize) -> u16 {
    ((v >> 16) & 0xFFFF) as u16
}

/// `GET_X_LPARAM` / `GET_Y_LPARAM`.
fn xy(lparam: isize) -> (i32, i32) {
    let v = lparam as usize;
    (i32::from(loword(v) as i16), i32::from(hiword(v) as i16))
}

#[repr(C)]
struct CreateStruct {
    create_params: isize,
    instance: isize,
}

#[repr(C)]
struct CopyData {
    data: usize,
    cb: u32,
    ptr: *const u16,
}

/// `wcsstr` input: the NUL-terminated string at `p`.
///
/// # Safety
/// `p` is null or a NUL-terminated UTF-16 string.
unsafe fn cstr<'a>(p: *const u16) -> &'a [u16] {
    if p.is_null() {
        return &[];
    }
    let mut n = 0;
    // SAFETY: the caller guarantees a NUL terminator.
    unsafe {
        while *p.add(n) != 0 {
            n += 1;
        }
        std::slice::from_raw_parts(p, n)
    }
}

/// `SettingsProc::Cpp::Main`: the main window.
///
/// # Safety
/// For `WM_CREATE`, `WM_GETMINMAXINFO`, `WM_COPYDATA`, and `WM_DPICHANGED`,
/// `lparam` is what Windows passes with that message (a `CREATESTRUCT*`,
/// `MINMAXINFO*`, `COPYDATASTRUCT*` with a NUL-terminated string or null
/// data, or a `RECT*` / null).
pub unsafe fn main_proc(h: &mut dyn ProcHost, msg: u32, wparam: usize, lparam: isize) -> isize {
    let has = h.has_data();
    match msg {
        WM_CREATE => {
            // SAFETY: WM_CREATE carries a CREATESTRUCT*.
            let instance = unsafe { (*(lparam as *const CreateStruct)).instance };
            h.create(instance);
            0
        }
        WM_SIZE => {
            if has {
                h.nav(
                    nav::EV_MAIN_SIZE,
                    i32::from(loword(lparam as usize)),
                    i32::from(hiword(lparam as usize)),
                    0,
                    0,
                );
            }
            0
        }
        WM_PAINT => {
            h.paint_main();
            0
        }
        WM_MOUSEWHEEL => {
            if has && h.has_content() {
                h.nav(nav::EV_MAIN_WHEEL, 0, 0, i32::from(hiword(wparam) as i16), 0);
            }
            0
        }
        WM_LBUTTONDOWN | WM_MOUSEMOVE => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            let kind = if msg == WM_LBUTTONDOWN {
                nav::EV_MAIN_LBUTTON
            } else {
                nav::EV_MAIN_MOUSEMOVE
            };
            let (x, y) = xy(lparam);
            h.nav(kind, x, y, 0, 0);
            0
        }
        WM_MOUSELEAVE => {
            if has {
                h.nav(nav::EV_MAIN_MOUSELEAVE, 0, 0, 0, 0);
            }
            0
        }
        WM_THEMECHANGED => {
            if has {
                h.theme_changed();
            }
            0
        }
        WM_GETMINMAXINFO => {
            let dpi = h.screen_dpi();
            let (x, y) = nav::min_track_size(dpi);
            // SAFETY: WM_GETMINMAXINFO carries a MINMAXINFO*; ptMinTrackSize
            // is its fourth POINT.
            unsafe {
                let p = (lparam as *mut i32).add(6);
                *p = x;
                *p.add(1) = y;
            }
            0
        }
        WM_COPYDATA => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            let cds = lparam as *const CopyData;
            // SAFETY: WM_COPYDATA carries a COPYDATASTRUCT* (or null).
            let data = if cds.is_null() {
                std::ptr::null()
            } else {
                unsafe { (*cds).ptr }
            };
            if !data.is_null() {
                // SAFETY: the sender passes a NUL-terminated command line.
                let mode = copy_data_mode(unsafe { cstr(data) });
                if mode != ImeMode::None {
                    h.switch_mode(mode as i32);
                }
                h.set_foreground();
            }
            1
        }
        WM_SETTINGCHANGE => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            h.setting_changed();
            0
        }
        WM_DPICHANGED => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            let rc = lparam as *const [i32; 4];
            // SAFETY: WM_DPICHANGED carries a RECT* (or null).
            let rect = if rc.is_null() { None } else { Some(unsafe { *rc }) };
            h.dpi_changed(u32::from(hiword(wparam)), rect);
            0
        }
        WM_DESTROY => {
            h.destroy();
            h.quit();
            0
        }
        _ => h.default(msg, wparam, lparam),
    }
}

/// `SettingsProc::Cpp::Content`: the content area.
pub fn content_proc(h: &mut dyn ProcHost, msg: u32, wparam: usize, lparam: isize) -> isize {
    let has = h.has_data();
    match msg {
        WM_PAINT => {
            h.paint_content();
            0
        }
        WM_VSCROLL | WM_MOUSEWHEEL | WM_MOUSEMOVE | WM_LBUTTONDOWN => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            match msg {
                WM_VSCROLL => h.nav(
                    nav::EV_CONTENT_VSCROLL,
                    0,
                    0,
                    i32::from(loword(wparam)),
                    i32::from(hiword(wparam)),
                ),
                WM_MOUSEWHEEL => h.nav(nav::EV_CONTENT_WHEEL, 0, 0, i32::from(hiword(wparam) as i16), 0),
                WM_MOUSEMOVE => {
                    let (x, y) = xy(lparam);
                    h.nav(nav::EV_CONTENT_MOUSEMOVE, x, y, 0, 0)
                }
                _ => {
                    let (x, y) = xy(lparam);
                    h.nav(nav::EV_CONTENT_LBUTTON, x, y, 0, 0)
                }
            }
            0
        }
        WM_MOUSELEAVE => {
            if has {
                h.nav(nav::EV_CONTENT_MOUSELEAVE, 0, 0, 0, 0);
            }
            h.default(msg, wparam, lparam)
        }
        WM_COMMAND => {
            if has {
                h.command(wparam, lparam);
            }
            h.default(msg, wparam, lparam)
        }
        WM_EDITOR_REVALIDATE | WM_EDITOR_VIEWPORT => {
            if !has {
                return h.default(msg, wparam, lparam);
            }
            if msg == WM_EDITOR_REVALIDATE {
                h.editor_revalidate();
            } else {
                h.editor_viewport();
            }
            0
        }
        WM_DRAWITEM => {
            if has && lparam != 0 && h.draw_item(lparam) {
                return 1;
            }
            h.default(msg, wparam, lparam)
        }
        WM_CTLCOLOREDIT | WM_CTLCOLORLISTBOX => {
            if has {
                if let Some(brush) = h.ctl_color(wparam as isize) {
                    return brush;
                }
            }
            h.default(msg, wparam, lparam)
        }
        WM_ERASEBKGND => 1,
        _ => h.default(msg, wparam, lparam),
    }
}
