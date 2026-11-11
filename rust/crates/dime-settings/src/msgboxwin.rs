//! `ThemedMessageBox` as a Rust-owned window (phase 5b-8a, Windows only).
//!
//! Port of `SettingsMsgBox::Cpp::Show` (`src/DIMESettings/SettingsMsgBox.cpp`):
//! class registration, the window procedure, and the modal loop. Layout,
//! hit tests, and keys are `msgbox.rs`; painting is `paint::paint_msgbox`.
//! The dpi comes from the host (`CConfig::GetDpiForHwnd`) so both sides use
//! the same rule.

#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use std::cell::Cell;
use std::ffi::c_void;
use std::ptr::{null, null_mut};
use std::sync::Mutex;

use dime_config::geometry::{mul_div, ThemeColors};

use crate::msgbox::{self, Buttons, Layout};
use crate::paint::{self, Handle, Rect};

/// `UINT (*)(HWND)`: the host's dpi for a window.
pub type DpiFn = unsafe extern "C" fn(Handle) -> u32;

type WndProc = unsafe extern "system" fn(Handle, u32, usize, isize) -> isize;

#[repr(C)]
struct WndClassEx {
    cb_size: u32,
    style: u32,
    wnd_proc: Option<WndProc>,
    cls_extra: i32,
    wnd_extra: i32,
    instance: Handle,
    icon: Handle,
    cursor: Handle,
    background: Handle,
    menu_name: *const u16,
    class_name: *const u16,
    icon_sm: Handle,
}

#[repr(C)]
struct PaintStruct {
    hdc: Handle,
    erase: i32,
    rc_paint: Rect,
    restore: i32,
    inc_update: i32,
    reserved: [u8; 32],
}

#[repr(C)]
struct Msg {
    hwnd: Handle,
    message: u32,
    wparam: usize,
    lparam: isize,
    time: u32,
    pt: [i32; 2],
    private: u32,
}

#[repr(C)]
struct TrackMouseEvent {
    cb_size: u32,
    flags: u32,
    track: Handle,
    hover_time: u32,
}

windows_link::link!("user32.dll" "system" fn RegisterClassExW(wc: *const WndClassEx) -> u16);
windows_link::link!("user32.dll" "system" fn CreateWindowExW(ex: u32, class: *const u16, name: *const u16, style: u32, x: i32, y: i32, w: i32, h: i32, parent: Handle, menu: Handle, inst: Handle, param: *mut c_void) -> Handle);
windows_link::link!("user32.dll" "system" fn DefWindowProcW(h: Handle, m: u32, w: usize, l: isize) -> isize);
#[cfg(target_pointer_width = "64")]
windows_link::link!("user32.dll" "system" fn GetWindowLongPtrW(h: Handle, i: i32) -> isize);
#[cfg(target_pointer_width = "64")]
windows_link::link!("user32.dll" "system" fn SetWindowLongPtrW(h: Handle, i: i32, v: isize) -> isize);
windows_link::link!("user32.dll" "system" fn GetWindowLongW(h: Handle, i: i32) -> i32);
#[cfg(target_pointer_width = "32")]
windows_link::link!("user32.dll" "system" fn SetWindowLongW(h: Handle, i: i32, v: i32) -> i32);
windows_link::link!("user32.dll" "system" fn AdjustWindowRectEx(rc: *mut Rect, style: u32, menu: i32, ex: u32) -> i32);
windows_link::link!("user32.dll" "system" fn GetParent(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn GetWindowRect(h: Handle, rc: *mut Rect) -> i32);
windows_link::link!("user32.dll" "system" fn GetDesktopWindow() -> Handle);
windows_link::link!("user32.dll" "system" fn SetWindowPos(h: Handle, after: Handle, x: i32, y: i32, w: i32, h2: i32, flags: u32) -> i32);
windows_link::link!("user32.dll" "system" fn BeginPaint(h: Handle, ps: *mut PaintStruct) -> Handle);
windows_link::link!("user32.dll" "system" fn EndPaint(h: Handle, ps: *const PaintStruct) -> i32);
windows_link::link!("user32.dll" "system" fn InvalidateRect(h: Handle, rc: *const Rect, erase: i32) -> i32);
windows_link::link!("user32.dll" "system" fn TrackMouseEvent(t: *mut TrackMouseEvent) -> i32);
windows_link::link!("user32.dll" "system" fn DestroyWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn PostQuitMessage(code: i32));
windows_link::link!("user32.dll" "system" fn LoadCursorW(inst: Handle, name: *const u16) -> Handle);
windows_link::link!("user32.dll" "system" fn LoadIconW(inst: Handle, name: *const u16) -> Handle);
windows_link::link!("user32.dll" "system" fn MessageBoxW(h: Handle, text: *const u16, caption: *const u16, kind: u32) -> i32);
windows_link::link!("user32.dll" "system" fn EnableWindow(h: Handle, enable: i32) -> i32);
windows_link::link!("user32.dll" "system" fn ShowWindow(h: Handle, cmd: i32) -> i32);
windows_link::link!("user32.dll" "system" fn UpdateWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn SetFocus(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn GetMessageW(msg: *mut Msg, h: Handle, min: u32, max: u32) -> i32);
windows_link::link!("user32.dll" "system" fn TranslateMessage(msg: *const Msg) -> i32);
windows_link::link!("user32.dll" "system" fn DispatchMessageW(msg: *const Msg) -> isize);
windows_link::link!("user32.dll" "system" fn SetForegroundWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn GetDC(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(h: Handle, dc: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn DrawTextW(dc: Handle, s: *const u16, c: i32, rc: *mut Rect, format: u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetModuleHandleW(name: *const u16) -> Handle);
windows_link::link!("gdi32.dll" "system" fn CreateFontW(h: i32, w: i32, esc: i32, orient: i32, weight: i32, italic: u32, underline: u32, strike: u32, charset: u32, out_precision: u32, clip_precision: u32, quality: u32, pitch_and_family: u32, face: *const u16) -> Handle);
windows_link::link!("gdi32.dll" "system" fn SelectObject(dc: Handle, obj: Handle) -> Handle);
windows_link::link!("gdi32.dll" "system" fn DeleteObject(obj: Handle) -> i32);
windows_link::link!("dwmapi.dll" "system" fn DwmSetWindowAttribute(h: Handle, attr: u32, value: *const c_void, size: u32) -> i32);

const GWLP_USERDATA: i32 = -21;
const GWL_STYLE: i32 = -16;
const GWL_EXSTYLE: i32 = -20;
const WM_CREATE: u32 = 0x0001;
const WM_DESTROY: u32 = 0x0002;
const WM_PAINT: u32 = 0x000F;
const WM_CLOSE: u32 = 0x0010;
const WM_KEYDOWN: u32 = 0x0100;
const WM_MOUSEMOVE: u32 = 0x0200;
const WM_LBUTTONDOWN: u32 = 0x0201;
const WM_MOUSELEAVE: u32 = 0x02A3;
const WS_EX_DLGMODALFRAME: u32 = 0x0000_0001;
const WS_POPUP: u32 = 0x8000_0000;
const WS_CAPTION: u32 = 0x00C0_0000;
const WS_SYSMENU: u32 = 0x0008_0000;
const SWP_NOZORDER: u32 = 0x0004;
const SW_SHOW: i32 = 5;
const TME_LEAVE: u32 = 0x0002;
const DT_LEFT: u32 = 0;
const DT_WORDBREAK: u32 = 0x10;
const DT_CALCRECT: u32 = 0x400;
const DWMWA_USE_IMMERSIVE_DARK_MODE: u32 = 20;
const IDC_ARROW: usize = 32512;
const IDI_ERROR: usize = 32513;
const IDI_QUESTION: usize = 32514;
const IDI_WARNING: usize = 32515;
const IDI_INFORMATION: usize = 32516;

/// The modal box's state (`MsgBoxData`), pointed at by `GWLP_USERDATA`.
struct MsgBox {
    text: Vec<u16>,
    theme: ThemeColors,
    is_dark: bool,
    buttons: Buttons,
    labels: Vec<Vec<u16>>,
    default_btn: i32,
    hover_btn: Cell<i32>,
    icon: Handle,
    font: Cell<Handle>,
    dpi_for: DpiFn,
    result: Cell<i32>,
}

unsafe fn get_data(h: Handle) -> *const MsgBox {
    #[cfg(target_pointer_width = "64")]
    // SAFETY: plain user32 call.
    let v = unsafe { GetWindowLongPtrW(h, GWLP_USERDATA) };
    #[cfg(target_pointer_width = "32")]
    // SAFETY: plain user32 call.
    let v = unsafe { GetWindowLongW(h, GWLP_USERDATA) } as isize;
    v as *const MsgBox
}

unsafe fn set_data(h: Handle, p: *const MsgBox) {
    #[cfg(target_pointer_width = "64")]
    // SAFETY: plain user32 call.
    unsafe {
        SetWindowLongPtrW(h, GWLP_USERDATA, p as isize)
    };
    #[cfg(target_pointer_width = "32")]
    // SAFETY: plain user32 call.
    unsafe {
        SetWindowLongW(h, GWLP_USERDATA, p as i32)
    };
}

fn x_of(l: isize) -> i32 {
    (l & 0xffff) as u16 as i16 as i32
}

fn y_of(l: isize) -> i32 {
    ((l >> 16) & 0xffff) as u16 as i16 as i32
}

/// `CalcLayout`: measure the wrapped text, then `msgbox::layout`.
unsafe fn calc_layout(hwnd: Handle, md: &MsgBox, dpi: u32) -> Layout {
    let max_w = msgbox::max_text_width(dpi);
    // SAFETY: plain GDI calls on the window's DC.
    unsafe {
        let dc = GetDC(hwnd);
        let old = SelectObject(dc, md.font.get());
        let mut rc = Rect::new(0, 0, max_w, 0);
        DrawTextW(
            dc,
            md.text.as_ptr(),
            md.text.len() as i32,
            &mut rc,
            DT_CALCRECT | DT_WORDBREAK | DT_LEFT,
        );
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
        msgbox::layout(!md.icon.is_null(), md.buttons.count, rc.right, rc.bottom, dpi)
    }
}

unsafe extern "system" fn wnd_proc(hwnd: Handle, msg: u32, wparam: usize, lparam: isize) -> isize {
    // SAFETY: GWLP_USERDATA is null or the `MsgBox` that `show` keeps alive
    // until the modal loop ends (after WM_DESTROY).
    unsafe {
        let mut md = get_data(hwnd);
        match msg {
            WM_CREATE => {
                // lparam is the CREATESTRUCTW; lpCreateParams is its first field.
                md = *(lparam as *const *const MsgBox);
                set_data(hwnd, md);
                let md = &*md;
                if md.is_dark {
                    let use_dark: i32 = 1;
                    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, (&use_dark as *const i32).cast(), 4);
                }
                let dpi = (md.dpi_for)(hwnd);
                let l = calc_layout(hwnd, md, dpi);
                let mut rc = Rect::new(l.client[0], l.client[1], l.client[2], l.client[3]);
                AdjustWindowRectEx(
                    &mut rc,
                    GetWindowLongW(hwnd, GWL_STYLE) as u32,
                    0,
                    GetWindowLongW(hwnd, GWL_EXSTYLE) as u32,
                );
                let (w, h) = (rc.right - rc.left, rc.bottom - rc.top);
                let parent = GetParent(hwnd);
                let mut rp = Rect::default();
                GetWindowRect(if parent.is_null() { GetDesktopWindow() } else { parent }, &mut rp);
                SetWindowPos(
                    hwnd,
                    null_mut(),
                    (rp.left + rp.right - w) / 2,
                    (rp.top + rp.bottom - h) / 2,
                    w,
                    h,
                    SWP_NOZORDER,
                );
                return 0;
            }
            WM_PAINT if !md.is_null() => {
                let md = &*md;
                let mut ps: PaintStruct = std::mem::zeroed();
                let dc = BeginPaint(hwnd, &mut ps);
                let dpi = (md.dpi_for)(hwnd);
                let l = calc_layout(hwnd, md, dpi);
                paint::paint_msgbox(
                    dc,
                    &l,
                    &md.text,
                    &md.labels,
                    md.hover_btn.get(),
                    md.default_btn,
                    &md.theme,
                    md.font.get(),
                    md.icon,
                    dpi,
                );
                EndPaint(hwnd, &ps);
                return 0;
            }
            WM_MOUSEMOVE if !md.is_null() => {
                let md = &*md;
                let dpi = (md.dpi_for)(hwnd);
                let l = calc_layout(hwnd, md, dpi);
                let hover = msgbox::button_at(&l, md.buttons.count, x_of(lparam), y_of(lparam));
                if hover != md.hover_btn.get() {
                    md.hover_btn.set(hover);
                    InvalidateRect(hwnd, null(), 0);
                    let mut tme = TrackMouseEvent {
                        cb_size: std::mem::size_of::<TrackMouseEvent>() as u32,
                        flags: TME_LEAVE,
                        track: hwnd,
                        hover_time: 0,
                    };
                    TrackMouseEvent(&mut tme);
                }
                return 0;
            }
            WM_MOUSELEAVE => {
                if !md.is_null() && (*md).hover_btn.get() != -1 {
                    (*md).hover_btn.set(-1);
                    InvalidateRect(hwnd, null(), 0);
                }
                return 0;
            }
            WM_LBUTTONDOWN if !md.is_null() => {
                let md = &*md;
                let dpi = (md.dpi_for)(hwnd);
                let l = calc_layout(hwnd, md, dpi);
                let hit = msgbox::button_at(&l, md.buttons.count, x_of(lparam), y_of(lparam));
                if hit >= 0 {
                    md.result.set(md.buttons.ids[hit as usize]);
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            WM_KEYDOWN if !md.is_null() => {
                let md = &*md;
                let r = msgbox::key_result(&md.buttons, md.default_btn, wparam as u32);
                if r != -1 {
                    md.result.set(r);
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            WM_CLOSE => {
                if !md.is_null() {
                    let r = msgbox::close_result(&(*md).buttons);
                    if r != -1 {
                        (*md).result.set(r);
                    }
                }
                DestroyWindow(hwnd);
                return 0;
            }
            WM_DESTROY => {
                if !md.is_null() {
                    let f = (*md).font.replace(null_mut());
                    if !f.is_null() {
                        DeleteObject(f);
                    }
                }
                PostQuitMessage(0);
                return 0;
            }
            _ => {}
        }
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}

static REGISTERED: Mutex<Vec<Vec<u16>>> = Mutex::new(Vec::new());

fn label(i: i32) -> Vec<u16> {
    match i {
        msgbox::LABEL_YES => "是(Y)",
        msgbox::LABEL_NO => "否(N)",
        msgbox::LABEL_CANCEL => "取消",
        msgbox::LABEL_OK => "確定",
        _ => "",
    }
    .encode_utf16()
    .collect()
}

fn with_nul(s: &[u16]) -> Vec<u16> {
    let mut v = s.to_vec();
    v.push(0);
    v
}

/// `SettingsMsgBox::Cpp::Show`: run the modal box and return the button id.
/// Strings are without NULs. `class_name` is registered once per process.
pub fn show(
    parent: Handle,
    text: &[u16],
    caption: &[u16],
    mb_type: u32,
    class_name: &[u16],
    is_dark: bool,
    theme: &ThemeColors,
    dpi_for: DpiFn,
) -> i32 {
    let class_z = with_nul(class_name);
    // SAFETY: plain user32 / gdi32 calls; `md` outlives the window, which is
    // destroyed before the modal loop returns.
    unsafe {
        let inst = GetModuleHandleW(null());
        {
            let mut reg = REGISTERED.lock().unwrap_or_else(|e| e.into_inner());
            if !reg.iter().any(|n| n.as_slice() == class_name) {
                let wc = WndClassEx {
                    cb_size: std::mem::size_of::<WndClassEx>() as u32,
                    style: 0,
                    wnd_proc: Some(wnd_proc),
                    cls_extra: 0,
                    wnd_extra: 0,
                    instance: inst,
                    icon: null_mut(),
                    cursor: LoadCursorW(null_mut(), IDC_ARROW as *const u16),
                    background: null_mut(),
                    menu_name: null(),
                    class_name: class_z.as_ptr(),
                    icon_sm: null_mut(),
                };
                RegisterClassExW(&wc);
                reg.push(class_name.to_vec());
            }
        }

        let buttons = msgbox::buttons(mb_type);
        let n = buttons.count.clamp(0, 3) as usize;
        let labels = (0..n).map(|i| label(buttons.labels[i])).collect();
        let icon = match msgbox::icon(mb_type) {
            1 => LoadIconW(null_mut(), IDI_WARNING as *const u16),
            2 => LoadIconW(null_mut(), IDI_ERROR as *const u16),
            3 => LoadIconW(null_mut(), IDI_QUESTION as *const u16),
            4 => LoadIconW(null_mut(), IDI_INFORMATION as *const u16),
            _ => null_mut(),
        };
        let dpi = dpi_for(if parent.is_null() { GetDesktopWindow() } else { parent });
        let face: Vec<u16> = "Segoe UI\0".encode_utf16().collect();
        let font = CreateFontW(
            -mul_div(12, dpi as i32, 96),
            0,
            0,
            0,
            400,
            0,
            0,
            0,
            1,
            0,
            0,
            5,
            0,
            face.as_ptr(),
        );

        let md = Box::new(MsgBox {
            text: text.to_vec(),
            theme: *theme,
            is_dark,
            buttons,
            labels,
            default_btn: 0,
            hover_btn: Cell::new(-1),
            icon,
            font: Cell::new(font),
            dpi_for,
            result: Cell::new(0),
        });
        let md_ptr: *const MsgBox = &*md;
        let text_z = with_nul(text);
        let caption_z = with_nul(caption);
        let hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            class_z.as_ptr(),
            caption_z.as_ptr(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0,
            0,
            1,
            1,
            parent,
            null_mut(),
            inst,
            md_ptr as *mut c_void, // CREATESTRUCT::lpCreateParams
        );
        if hwnd.is_null() {
            let f = md.font.replace(null_mut());
            if !f.is_null() {
                DeleteObject(f);
            }
            return MessageBoxW(parent, text_z.as_ptr(), caption_z.as_ptr(), mb_type);
        }

        if !parent.is_null() {
            EnableWindow(parent, 0);
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetFocus(hwnd);

        md.result.set(msgbox::IDCANCEL);
        let mut msg: Msg = std::mem::zeroed();
        while GetMessageW(&mut msg, null_mut(), 0, 0) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if !parent.is_null() {
            EnableWindow(parent, 1);
            SetForegroundWindow(parent);
        }
        md.result.get()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lparam_coordinates_are_signed() {
        assert_eq!(x_of(0x0002_FFFF), -1);
        assert_eq!(y_of(0x0002_FFFF), 2);
        assert_eq!(y_of(0xFFFF_0000u32 as i32 as isize), -1);
    }

    #[test]
    fn struct_sizes_match_win32() {
        use std::mem::size_of;
        if cfg!(target_pointer_width = "64") {
            assert_eq!(size_of::<WndClassEx>(), 80);
            assert_eq!(size_of::<PaintStruct>(), 72);
            assert_eq!(size_of::<Msg>(), 48);
            assert_eq!(size_of::<TrackMouseEvent>(), 24);
        } else {
            assert_eq!(size_of::<WndClassEx>(), 48);
            assert_eq!(size_of::<PaintStruct>(), 64);
            assert_eq!(size_of::<Msg>(), 32);
            assert_eq!(size_of::<TrackMouseEvent>(), 16);
        }
    }
}
