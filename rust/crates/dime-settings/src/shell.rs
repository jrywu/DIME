//! The settings window's shell (phase 5b-9d, Windows only).
//!
//! Port of `SettingsShell::Cpp` (`src/DIMESettings/SettingsShell.cpp`,
//! formerly in `SettingsWindow.cpp`): the window placement kept in HKCU, the
//! three theme brushes, the `BeginPaint` / memory DC / `BitBlt` halves of the
//! two `WM_PAINT` handlers, class registration, and `Run` (GDI+, class,
//! window, placement or `nCmdShow`, message loop). The window still builds
//! the paint contexts ([`PaintSource`]) and sets up its data once the window
//! exists ([`RunParams::started`]).

// HWND / HDC / HBRUSH are opaque handles passed to Win32, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::{null, null_mut};

use dime_config::geometry::ThemeColors;

use crate::frame;
use crate::paint::{self, PaintContext};

pub type Handle = *mut c_void;

#[repr(C)]
#[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
struct Point {
    x: i32,
    y: i32,
}

/// `WINDOWPLACEMENT` (44 bytes).
#[repr(C)]
#[derive(Default, Clone, Copy)]
struct WindowPlacement {
    length: u32,
    flags: u32,
    show_cmd: u32,
    min_position: Point,
    max_position: Point,
    normal_position: Rect,
}

const _: () = assert!(std::mem::size_of::<WindowPlacement>() == 44);

/// `PAINTSTRUCT`.
#[repr(C)]
struct PaintStruct {
    hdc: Handle,
    erase: i32,
    rc_paint: Rect,
    restore: i32,
    inc_update: i32,
    reserved: [u8; 32],
}

/// `WNDCLASSEXW`.
#[repr(C)]
struct WndClassEx {
    cb_size: u32,
    style: u32,
    wnd_proc: *const c_void,
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

/// `MSG`.
#[repr(C)]
struct Msg {
    hwnd: Handle,
    message: u32,
    wparam: usize,
    lparam: isize,
    time: u32,
    pt: Point,
    private: u32,
}

/// `Gdiplus::GdiplusStartupInput` as its default constructor fills it.
#[repr(C)]
struct GdiplusStartupInput {
    version: u32,
    debug_event_callback: *const c_void,
    suppress_background_thread: i32,
    suppress_external_codecs: i32,
}

windows_link::link!("user32.dll" "system" fn GetWindowPlacement(h: Handle, wp: *mut WindowPlacement) -> i32);
windows_link::link!("user32.dll" "system" fn SetWindowPlacement(h: Handle, wp: *const WindowPlacement) -> i32);
windows_link::link!("user32.dll" "system" fn MonitorFromRect(rc: *const Rect, flags: u32) -> Handle);
windows_link::link!("user32.dll" "system" fn BeginPaint(h: Handle, ps: *mut PaintStruct) -> Handle);
windows_link::link!("user32.dll" "system" fn EndPaint(h: Handle, ps: *const PaintStruct) -> i32);
windows_link::link!("user32.dll" "system" fn GetClientRect(h: Handle, rc: *mut Rect) -> i32);
windows_link::link!("user32.dll" "system" fn GetDC(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(h: Handle, hdc: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn LoadCursorW(inst: Handle, name: *const u16) -> Handle);
windows_link::link!("user32.dll" "system" fn LoadIconW(inst: Handle, name: *const u16) -> Handle);
windows_link::link!("user32.dll" "system" fn RegisterClassExW(wc: *const WndClassEx) -> u16);
windows_link::link!("user32.dll" "system" fn GetClassInfoExW(inst: Handle, name: *const u16, wc: *mut WndClassEx) -> i32);
windows_link::link!("user32.dll" "system" fn CreateWindowExW(ex: u32, class: *const u16, name: *const u16, style: u32, x: i32, y: i32, w: i32, h: i32, parent: Handle, menu: Handle, inst: Handle, param: *mut c_void) -> Handle);
windows_link::link!("user32.dll" "system" fn ShowWindow(h: Handle, cmd: i32) -> i32);
windows_link::link!("user32.dll" "system" fn UpdateWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn GetMessageW(msg: *mut Msg, h: Handle, min: u32, max: u32) -> i32);
windows_link::link!("user32.dll" "system" fn TranslateMessage(msg: *const Msg) -> i32);
windows_link::link!("user32.dll" "system" fn DispatchMessageW(msg: *const Msg) -> isize);
windows_link::link!("gdi32.dll" "system" fn GetDeviceCaps(hdc: Handle, index: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn CreateSolidBrush(color: u32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn DeleteObject(obj: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn CreateCompatibleDC(hdc: Handle) -> Handle);
windows_link::link!("gdi32.dll" "system" fn CreateCompatibleBitmap(hdc: Handle, w: i32, h: i32) -> Handle);
windows_link::link!("gdi32.dll" "system" fn SelectObject(hdc: Handle, obj: Handle) -> Handle);
windows_link::link!("gdi32.dll" "system" fn DeleteDC(hdc: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn BitBlt(dst: Handle, x: i32, y: i32, w: i32, h: i32, src: Handle, sx: i32, sy: i32, rop: u32) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegCreateKeyExW(key: Handle, sub: *const u16, reserved: u32, class: *const u16, options: u32, sam: u32, sa: *const c_void, out: *mut Handle, disposition: *mut u32) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegOpenKeyExW(key: Handle, sub: *const u16, options: u32, sam: u32, out: *mut Handle) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegSetValueExW(key: Handle, name: *const u16, reserved: u32, ty: u32, data: *const u8, len: u32) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegGetValueW(key: Handle, sub: *const u16, name: *const u16, flags: u32, ty: *mut u32, data: *mut c_void, len: *mut u32) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegDeleteValueW(key: Handle, name: *const u16) -> i32);
windows_link::link!("advapi32.dll" "system" fn RegCloseKey(key: Handle) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdiplusStartup(token: *mut usize, input: *const GdiplusStartupInput, output: *mut c_void) -> i32);
windows_link::link!("gdiplus.dll" "system" fn GdiplusShutdown(token: usize));

/// `HKEY_CURRENT_USER` (sign-extended like the SDK's `(HKEY)(ULONG_PTR)(LONG)`).
const HKEY_CURRENT_USER: Handle = 0x8000_0001u32 as i32 as isize as Handle;
const KEY_WRITE: u32 = 0x0002_0006;
const REG_BINARY: u32 = 3;
const RRF_RT_REG_BINARY: u32 = 0x0000_0008;
const ERROR_SUCCESS: i32 = 0;
const MONITOR_DEFAULTTONULL: u32 = 0;
const SRCCOPY: u32 = 0x00CC_0020;
const LOGPIXELSY: i32 = 90;
const CS_VREDRAW: u32 = 0x0001;
const CS_HREDRAW: u32 = 0x0002;
const IDC_ARROW: usize = 32512;
const WS_OVERLAPPEDWINDOW: u32 = 0x00CF_0000;
const WS_CLIPCHILDREN: u32 = 0x0200_0000;
const CW_USEDEFAULT: i32 = 0x8000_0000u32 as i32;

/// `SettingsShell::PaintSource` / `DimeSettingsPaintSource`.
#[repr(C)]
pub struct PaintSource {
    pub user: *mut c_void,
    pub has_data: unsafe extern "C" fn(*mut c_void) -> i32,
    pub prepare: unsafe extern "C" fn(*mut c_void, i32, *mut PaintContext, *mut PaintContext, *mut i32),
}

/// `SettingsShell::RunParams` / `DimeSettingsRunParams`.
#[repr(C)]
pub struct RunParams {
    pub instance: Handle,
    pub n_cmd_show: i32,
    pub main_class: *const u16,
    pub main_proc: *const c_void,
    pub icon_id: i32,
    pub caption: *const u16,
    pub placement_key: *const u16,
    pub placement_value: *const u16,
    pub started: Option<unsafe extern "C" fn(*mut c_void, Handle)>,
    pub user: *mut c_void,
}

/// `SettingsShell::SavePlacement`. `key` / `value` are NUL-terminated.
pub fn save_placement(hwnd: Handle, key: *const u16, value: *const u16) -> bool {
    let mut wp = WindowPlacement {
        length: std::mem::size_of::<WindowPlacement>() as u32,
        ..Default::default()
    };
    // SAFETY: plain Win32 calls; strings are NUL-terminated per the contract.
    unsafe {
        if GetWindowPlacement(hwnd, &mut wp) == 0 {
            return false;
        }
        let mut hkey: Handle = null_mut();
        if RegCreateKeyExW(
            HKEY_CURRENT_USER,
            key,
            0,
            null(),
            0,
            KEY_WRITE,
            null(),
            &mut hkey,
            null_mut(),
        ) != ERROR_SUCCESS
        {
            return false;
        }
        RegSetValueExW(
            hkey,
            value,
            0,
            REG_BINARY,
            &wp as *const WindowPlacement as *const u8,
            std::mem::size_of::<WindowPlacement>() as u32,
        );
        RegCloseKey(hkey);
    }
    true
}

/// `SettingsShell::RestorePlacement`: false when nothing valid is stored or
/// its rectangle is on no monitor.
pub fn restore_placement(hwnd: Handle, key: *const u16, value: *const u16) -> bool {
    let mut wp = WindowPlacement::default();
    let mut cb = std::mem::size_of::<WindowPlacement>() as u32;
    let mut ty = 0u32;
    // SAFETY: `wp` holds `cb` bytes; strings are NUL-terminated per the contract.
    unsafe {
        if RegGetValueW(
            HKEY_CURRENT_USER,
            key,
            value,
            RRF_RT_REG_BINARY,
            &mut ty,
            &mut wp as *mut WindowPlacement as *mut c_void,
            &mut cb,
        ) != ERROR_SUCCESS
        {
            return false;
        }
        let size = std::mem::size_of::<WindowPlacement>() as u32;
        if cb != size || wp.length != size {
            return false;
        }
        if MonitorFromRect(&wp.normal_position, MONITOR_DEFAULTTONULL).is_null() {
            return false;
        }
        SetWindowPlacement(hwnd, &wp);
    }
    true
}

/// `SettingsShell::DeletePlacement`.
pub fn delete_placement(key: *const u16, value: *const u16) {
    let mut hkey: Handle = null_mut();
    // SAFETY: strings are NUL-terminated per the contract.
    unsafe {
        if RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_WRITE, &mut hkey) == ERROR_SUCCESS {
            RegDeleteValueW(hkey, value);
            RegCloseKey(hkey);
        }
    }
}

/// `SettingsShell::CreateBrushes`: sidebar, content, card.
pub fn create_brushes(theme: &ThemeColors) -> [Handle; 3] {
    // SAFETY: plain GDI calls.
    unsafe {
        [
            CreateSolidBrush(theme.sidebar_bg),
            CreateSolidBrush(theme.content_bg),
            CreateSolidBrush(theme.card_bg),
        ]
    }
}

/// `SettingsShell::DestroyBrushes`: deletes and clears each non-null brush.
pub fn destroy_brushes(brushes: &mut [Handle; 3]) {
    for b in brushes.iter_mut() {
        if !b.is_null() {
            // SAFETY: a brush we created.
            unsafe { DeleteObject(*b) };
            *b = null_mut();
        }
    }
}

fn zeroed_context() -> PaintContext {
    // SAFETY: PaintContext is plain data (integers, colours, handles); all
    // zeros is the `= {}` the C++ side starts from.
    unsafe { std::mem::zeroed() }
}

/// `SettingsShell::PaintMainInto`. When the memory DC cannot be created the
/// frame is painted straight into `target`.
pub fn paint_main_into(target: Handle, w: i32, h: i32, src: &PaintSource) {
    // SAFETY: plain GDI calls; the callbacks are the window's.
    unsafe {
        let mut hdc = target;
        let mem = CreateCompatibleDC(target);
        let bmp = CreateCompatibleBitmap(target, w, h);
        let old = SelectObject(mem, bmp);
        if !mem.is_null() {
            hdc = mem;
        }
        if (src.has_data)(src.user) != 0 {
            let mut main = zeroed_context();
            let mut has_content = 0i32;
            (src.prepare)(src.user, 0, &mut main, null_mut(), &mut has_content);
            paint::paint_main_frame(hdc, &main, has_content != 0);
        }
        if !mem.is_null() {
            BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
    }
}

/// `SettingsShell::PaintContentInto`: nothing without window data.
pub fn paint_content_into(target: Handle, w: i32, h: i32, src: &PaintSource) {
    // SAFETY: as above.
    unsafe {
        if (src.has_data)(src.user) == 0 {
            return;
        }
        let mem = CreateCompatibleDC(target);
        let bmp = CreateCompatibleBitmap(target, w, h);
        let old = SelectObject(mem, bmp);
        let mut content = zeroed_context();
        let mut main = zeroed_context();
        let mut unused = 0i32;
        (src.prepare)(src.user, 1, &mut content, &mut main, &mut unused);
        paint::paint_content_frame(mem, &content, &main);
        BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
}

fn paint_window(hwnd: Handle, body: impl FnOnce(Handle, i32, i32)) {
    // SAFETY: BeginPaint / EndPaint pair on the window being painted.
    unsafe {
        let mut ps: PaintStruct = std::mem::zeroed();
        let hdc = BeginPaint(hwnd, &mut ps);
        let mut rc = Rect::default();
        GetClientRect(hwnd, &mut rc);
        body(hdc, rc.right, rc.bottom);
        EndPaint(hwnd, &ps);
    }
}

/// `SettingsShell::PaintMain` (`WM_PAINT` of the main window).
pub fn paint_main(hwnd: Handle, src: &PaintSource) {
    paint_window(hwnd, |hdc, w, h| paint_main_into(hdc, w, h, src));
}

/// `SettingsShell::PaintContent` (`WM_PAINT` of the content area).
pub fn paint_content(hwnd: Handle, src: &PaintSource) {
    paint_window(hwnd, |hdc, w, h| paint_content_into(hdc, w, h, src));
}

fn class(instance: Handle, name: *const u16, proc_: *const c_void, icon: Handle) -> WndClassEx {
    WndClassEx {
        cb_size: std::mem::size_of::<WndClassEx>() as u32,
        style: CS_HREDRAW | CS_VREDRAW,
        wnd_proc: proc_,
        cls_extra: 0,
        wnd_extra: 0,
        instance,
        icon,
        // SAFETY: a system cursor by ordinal.
        cursor: unsafe { LoadCursorW(null_mut(), IDC_ARROW as *const u16) },
        background: null_mut(),
        menu_name: null(),
        class_name: name,
        icon_sm: icon,
    }
}

/// `SettingsShell::RegisterMainClass`. `proc_` is the `WNDPROC`.
pub fn register_main_class(instance: Handle, name: *const u16, proc_: *const c_void, icon_id: i32) -> bool {
    // SAFETY: an icon by resource ordinal (MAKEINTRESOURCE); NUL-terminated name.
    unsafe {
        let icon = LoadIconW(instance, icon_id as u16 as usize as *const u16);
        let wc = class(instance, name, proc_, icon);
        RegisterClassExW(&wc) != 0
    }
}

/// `SettingsShell::RegisterContentClass`: nothing when a class with that
/// name is already registered.
pub fn register_content_class(instance: Handle, name: *const u16, proc_: *const c_void) {
    // SAFETY: NUL-terminated name; `existing` is a WNDCLASSEXW to fill.
    unsafe {
        let mut existing = class(instance, name, proc_, null_mut());
        if GetClassInfoExW(instance, name, &mut existing) != 0 {
            return;
        }
        let wc = class(instance, name, proc_, null_mut());
        RegisterClassExW(&wc);
    }
}

/// `SettingsShell::Run`: -1 when the window cannot be created (GDI+ is then
/// left running, like the C++ original); else `WM_QUIT`'s wParam.
pub fn run(p: &RunParams) -> i32 {
    // SAFETY: plain Win32 / GDI+ calls; strings and callbacks per the contract.
    unsafe {
        let input = GdiplusStartupInput {
            version: 1,
            debug_event_callback: null(),
            suppress_background_thread: 0,
            suppress_external_codecs: 0,
        };
        let mut token = 0usize;
        GdiplusStartup(&mut token, &input, null_mut());

        register_main_class(p.instance, p.main_class, p.main_proc, p.icon_id);
        let mut dpi = 96u32;
        let hdc = GetDC(null_mut());
        if !hdc.is_null() {
            dpi = GetDeviceCaps(hdc, LOGPIXELSY) as u32;
            ReleaseDC(null_mut(), hdc);
        }
        let (cx, cy) = frame::initial_size(dpi);

        let hwnd = CreateWindowExW(
            0,
            p.main_class,
            p.caption,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            cx,
            cy,
            null_mut(),
            null_mut(),
            p.instance,
            null_mut(),
        );
        if hwnd.is_null() {
            return -1;
        }
        if let Some(started) = p.started {
            started(p.user, hwnd);
        }
        if !restore_placement(hwnd, p.placement_key, p.placement_value) {
            ShowWindow(hwnd, p.n_cmd_show);
        }
        UpdateWindow(hwnd);

        let mut msg: Msg = std::mem::zeroed();
        while GetMessageW(&mut msg, null_mut(), 0, 0) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        GdiplusShutdown(token);
        msg.wparam as i32
    }
}
