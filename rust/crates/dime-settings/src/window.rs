//! The Rust-owned settings window (phase 5b-9e, Windows only).
//!
//! Port of `SettingsWindow` (`src/DIMESettings/SettingsWindow.cpp`): the
//! window data, the two window procedures, and every host the earlier steps
//! defined (`wndproc::ProcHost`, `driver::DriverHost`, `command::Host`,
//! `children::ChildHost`, the paint source), carried out with Win32 here.
//! Whatever must stay in C++ (CConfig, the composition engine's key check,
//! the table parser, custom-table paths, the reverse-conversion list, the
//! themed message box) comes through [`App`].
//!
//! The data lives in a `Box<WindowData>` behind `GWLP_USERDATA` of both the
//! main window and the content area. Window procedures re-enter (a host call
//! sends a message that reaches a window procedure again), so hosts hold a
//! raw pointer and every access is a short-lived borrow; nothing keeps a
//! reference to the data across a host or Win32 call.

#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::cell::RefCell;
use std::ffi::c_void;
use std::ptr::{addr_of_mut, null, null_mut};

use dime_config::geometry::{theme_colors, SysColors, ThemeColors};
use dime_config::page_layout::{ime_cards, load_cards};
use dime_config::settings_model::{sync_toggle, SettingsSnapshot};
use dime_config::ImeMode;

use crate::children::{self, ChildHost};
use crate::command::{self, CommandEvent, LogFont};
use crate::controls;
use crate::driver::{self, ControlInfo, DriverHost, DriverState};
use crate::editor;
use crate::frame::{self, FrameEvent};
use crate::layout::{card_width, PlanInput};
use crate::nav::NavEvent;
use crate::owner::{self, DrawItem};
use crate::paint::{self, Fonts, PaintContext};
use crate::populate::{self, ReverseEntry};
use crate::shell::{self, PaintSource, RunParams};
use crate::validate;
use crate::winhost;
use crate::wndproc::{self, ProcHost};

pub type Handle = *mut c_void;

/// What the window needs from C++ (`SettingsApp::Host`).
pub trait App {
    fn dpi_for_window(&self, hwnd: Handle) -> u32;
    fn system_dark(&self) -> bool;
    fn show_message(&self, parent: Handle, text: &[u16], caption: &[u16], mb_type: u32) -> i32;
    fn load_config(&self, mode: i32);
    fn write_config(&self, mode: i32);
    fn reset_all_defaults(&self, mode: i32);
    fn color_mode(&self) -> i32;
    fn set_color_mode(&self, color_mode: i32);
    fn load_colors_for_mode(&self, color_mode: i32);
    fn save_colors_for_mode(&self, color_mode: i32);
    fn get_colors(&self) -> [u32; 6];
    fn set_color(&self, index: i32, color: u32);
    fn set_reverse_selection(&self, sel: i32);
    fn enumerate_reverse_providers(&self);
    fn reverse_count(&self) -> i32;
    fn reverse_description(&self, index: i32) -> Vec<u16>;
    fn reverse_is_current(&self, index: i32) -> bool;
    fn reverse_is_null(&self) -> bool;
    fn load_snapshot(&self) -> SettingsSnapshot;
    fn apply_snapshot(&self, s: &SettingsSnapshot);
    fn parse_cin(&self, src: &[u16], dst: &[u16], custom_table: bool) -> bool;
    fn recreate_engine(&self, mode: i32);
    fn destroy_engine(&self);
    fn has_engine(&self) -> bool;
    fn engine_char_ok(&self, ch: u16) -> bool;
    fn custom_table_txt_path(&self, mode: i32) -> Vec<u16>;
    fn custom_table_cin_path(&self, mode: i32) -> Vec<u16>;
    fn app_data_path(&self) -> Vec<u16>;
    fn windows_1809_or_later(&self) -> bool;
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
struct Rect {
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
}

#[repr(C)]
struct TrackMouseEvent {
    cb_size: u32,
    flags: u32,
    hwnd: Handle,
    hover_time: u32,
}

#[repr(C)]
struct ScrollInfo {
    cb_size: u32,
    mask: u32,
    min: i32,
    max: i32,
    page: u32,
    pos: i32,
    track_pos: i32,
}

windows_link::link!("user32.dll" "system" fn DefWindowProcW(h: Handle, m: u32, w: usize, l: isize) -> isize);
#[cfg(target_pointer_width = "64")]
windows_link::link!("user32.dll" "system" fn GetWindowLongPtrW(h: Handle, i: i32) -> isize);
#[cfg(target_pointer_width = "64")]
windows_link::link!("user32.dll" "system" fn SetWindowLongPtrW(h: Handle, i: i32, v: isize) -> isize);
#[cfg(target_pointer_width = "32")]
windows_link::link!("user32.dll" "system" fn GetWindowLongW(h: Handle, i: i32) -> i32);
#[cfg(target_pointer_width = "32")]
windows_link::link!("user32.dll" "system" fn SetWindowLongW(h: Handle, i: i32, v: i32) -> i32);
windows_link::link!("user32.dll" "system" fn GetClientRect(h: Handle, rc: *mut Rect) -> i32);
windows_link::link!("user32.dll" "system" fn GetSystemMetrics(i: i32) -> i32);
windows_link::link!("user32.dll" "system" fn IsWindowEnabled(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn IsWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn IsWindowVisible(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn EnableWindow(h: Handle, enable: i32) -> i32);
windows_link::link!("user32.dll" "system" fn SendMessageW(h: Handle, m: u32, w: usize, l: isize) -> isize);
windows_link::link!("user32.dll" "system" fn SetWindowPos(h: Handle, after: Handle, x: i32, y: i32, w: i32, h2: i32, flags: u32) -> i32);
windows_link::link!("user32.dll" "system" fn ShowWindow(h: Handle, cmd: i32) -> i32);
windows_link::link!("user32.dll" "system" fn SetWindowRgn(h: Handle, rgn: Handle, redraw: i32) -> i32);
windows_link::link!("user32.dll" "system" fn DestroyWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn InvalidateRect(h: Handle, rc: *const Rect, erase: i32) -> i32);
windows_link::link!("user32.dll" "system" fn TrackMouseEvent(tme: *mut TrackMouseEvent) -> i32);
windows_link::link!("user32.dll" "system" fn SetScrollInfo(h: Handle, bar: i32, si: *const ScrollInfo, redraw: i32) -> i32);
windows_link::link!("user32.dll" "system" fn RedrawWindow(h: Handle, rc: *const Rect, rgn: Handle, flags: u32) -> i32);
windows_link::link!("user32.dll" "system" fn UpdateWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn MoveWindow(h: Handle, x: i32, y: i32, w: i32, h2: i32, repaint: i32) -> i32);
windows_link::link!("user32.dll" "system" fn SetFocus(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn CreateWindowExW(ex: u32, class: *const u16, name: *const u16, style: u32, x: i32, y: i32, w: i32, h: i32, parent: Handle, menu: Handle, inst: Handle, param: *mut c_void) -> Handle);
windows_link::link!("user32.dll" "system" fn GetDC(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(h: Handle, hdc: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn GetParent(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn SetForegroundWindow(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn PostQuitMessage(code: i32));
windows_link::link!("user32.dll" "system" fn GetWindowTextW(h: Handle, buf: *mut u16, cap: i32) -> i32);
windows_link::link!("user32.dll" "system" fn GetSysColor(index: i32) -> u32);
windows_link::link!("user32.dll" "system" fn SetWindowTextW(h: Handle, text: *const u16) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetDeviceCaps(hdc: Handle, index: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn DeleteObject(obj: Handle) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetModuleHandleW(name: *const u16) -> Handle);
windows_link::link!("uxtheme.dll" "system" fn SetWindowTheme(h: Handle, app: *const u16, idlist: *const u16) -> i32);
windows_link::link!("dwmapi.dll" "system" fn DwmSetWindowAttribute(h: Handle, attr: u32, value: *const c_void, size: u32) -> i32);

const GWLP_USERDATA: i32 = -21;
const SM_CXVSCROLL: i32 = 2;
const SWP_NOZORDER: u32 = 0x0004;
const SWP_NOACTIVATE: u32 = 0x0010;
const SW_HIDE: i32 = 0;
const SW_SHOW: i32 = 5;
const BM_GETCHECK: u32 = 0x00F0;
const BM_SETCHECK: u32 = 0x00F1;
const BST_CHECKED: isize = 1;
const TME_LEAVE: u32 = 0x0002;
const SIF_RANGE: u32 = 0x1;
const SIF_PAGE: u32 = 0x2;
const SIF_POS: u32 = 0x4;
const SB_VERT: i32 = 1;
const RDW_INVALIDATE: u32 = 0x0001;
const RDW_ALLCHILDREN: u32 = 0x0080;
const RDW_UPDATENOW: u32 = 0x0100;
const WS_CHILD: u32 = 0x4000_0000;
const WS_VISIBLE: u32 = 0x1000_0000;
const WS_VSCROLL: u32 = 0x0020_0000;
const WS_CLIPCHILDREN: u32 = 0x0200_0000;
const WS_OVERLAPPEDWINDOW: u32 = 0x00CF_0000;
const CW_USEDEFAULT: i32 = 0x8000_0000u32 as i32;
const IDC_CONTENT_AREA: usize = 2000;
const DWMWA_USE_IMMERSIVE_DARK_MODE: u32 = 20;
const MB_YESNOCANCEL: u32 = 0x3;
const MB_ICONQUESTION: u32 = 0x20;
const WM_COMMAND: u32 = 0x0111;
const BN_CLICKED: usize = 0;
const CBN_SELCHANGE: i32 = 1;
const EN_CHANGE: i32 = 0x0300;
const CB_ERR: isize = -1;
const CB_GETCURSEL: u32 = 0x0147;
const CB_GETITEMDATA: u32 = 0x0150;
const CB_RESETCONTENT: u32 = 0x014B;
const CB_ADDSTRING: u32 = 0x0143;
const CB_SETITEMDATA: u32 = 0x0151;
const CB_SETCURSEL: u32 = 0x014E;
const LOGPIXELSY: i32 = 90;
const CTRL_MAX_CODES: i32 = 18;
const CTRL_CUSTOM_TABLE_EDITOR: i32 = 200;
const CTRL_CUSTOM_TABLE_SAVE: i32 = 201;
const KEY_TOGGLE_SLOTS: usize = 64;

/// The production class names (`SETTINGS_WND_CLASS`, `CONTENT_WND_CLASS`).
pub const MAIN_CLASS: &str = "DIMESettingsMainClass";
pub const CONTENT_CLASS: &str = "DIMESettingsContentArea";
pub const PLACEMENT_KEY: &str = "SOFTWARE\\DIME\\SettingsUI";
pub const PLACEMENT_VALUE: &str = "WindowPlacement";

/// One child control (`WindowData::ControlHandle`).
#[derive(Clone, Copy)]
struct ControlHandle {
    id: i32,
    hwnd: Handle,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
}

/// `SettingsWindow::WindowData`.
pub struct WindowData {
    state: DriverState,
    instance: Handle,
    theme: ThemeColors,
    snapshot: SettingsSnapshot,
    content: Handle,
    ct_last_line_count: i32,
    ct_last_edited_line: i32,
    ct_keystroke_count: i32,
    /// Sidebar, content, card (the `SettingsShell::Brushes` order).
    brushes: [Handle; 3],
    fonts: Fonts,
    controls: Vec<ControlHandle>,
    app: Box<dyn App>,
    content_class: Vec<u16>,
    placement_key: Vec<u16>,
    placement_value: Vec<u16>,
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn with_nul(s: &[u16]) -> Vec<u16> {
    let mut v = s.to_vec();
    v.push(0);
    v
}

impl WindowData {
    fn new(app: Box<dyn App>, content_class: Vec<u16>, placement_key: Vec<u16>, placement_value: Vec<u16>) -> Self {
        // SAFETY: plain data; all zeros is the C++ `{}` initialisation.
        let mut state: DriverState = unsafe { std::mem::zeroed() };
        state.nav.hover_sidebar_index = -1;
        state.nav.hover_row_index = -1;
        state.nav.current_mode = ImeMode::Dayi as i32;
        state.nav.color_grid_expanded = 1; // colour swatches visible in custom mode
        WindowData {
            state,
            instance: null_mut(),
            // SAFETY: plain data, as above.
            theme: unsafe { std::mem::zeroed() },
            snapshot: SettingsSnapshot::default(),
            content: null_mut(),
            ct_last_line_count: 0,
            ct_last_edited_line: -1,
            ct_keystroke_count: 0,
            brushes: [null_mut(); 3],
            // SAFETY: plain data, as above.
            fonts: unsafe { std::mem::zeroed() },
            controls: Vec::new(),
            app,
            content_class,
            placement_key,
            placement_value,
        }
    }
}

/// What the next window created on this thread uses (WM_CREATE takes it).
struct Pending {
    app: Box<dyn App>,
    content_class: Vec<u16>,
    placement_key: Vec<u16>,
    placement_value: Vec<u16>,
}

thread_local! {
    static PENDING: RefCell<Option<Pending>> = const { RefCell::new(None) };
}

fn get_data(h: Handle) -> *mut WindowData {
    if h.is_null() {
        return null_mut();
    }
    #[cfg(target_pointer_width = "64")]
    // SAFETY: plain user32 call.
    let v = unsafe { GetWindowLongPtrW(h, GWLP_USERDATA) };
    #[cfg(target_pointer_width = "32")]
    // SAFETY: plain user32 call.
    let v = unsafe { GetWindowLongW(h, GWLP_USERDATA) } as isize;
    v as *mut WindowData
}

fn set_data(h: Handle, p: *mut WindowData) {
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

// ---- Short-lived access ----------------------------------------------------
//
// Every helper below borrows the data only inside its own body and never
// across a call that can reach a window procedure.

/// # Safety
/// `wd` is the live window data.
unsafe fn data<'a>(wd: *mut WindowData) -> &'a mut WindowData {
    // SAFETY: per the contract; callers drop the borrow before any call that
    // can re-enter.
    unsafe { &mut *wd }
}

fn mode_of(wd: *mut WindowData) -> i32 {
    // SAFETY: live data, read only.
    unsafe { (*wd).state.nav.current_mode }
}

fn find_control(wd: *mut WindowData, id: i32) -> Handle {
    // SAFETY: live data; no call inside.
    unsafe {
        (*wd)
            .controls
            .iter()
            .find(|c| c.id == id)
            .map_or(null_mut(), |c| c.hwnd)
    }
}

fn app<'a>(wd: *mut WindowData) -> &'a dyn App {
    // SAFETY: the app box lives as long as the data and is never replaced;
    // a shared borrow of it does not overlap the mutable fields.
    unsafe { &*(&*(*wd).app as *const dyn App) }
}

fn theme_for(is_dark: bool) -> ThemeColors {
    // SAFETY: plain user32 calls (`ComputeThemeColors`).
    let sys = unsafe {
        SysColors {
            hotlight: GetSysColor(26),
            face: GetSysColor(15),
            window: GetSysColor(5),
            window_text: GetSysColor(8),
            gray_text: GetSysColor(17),
            highlight_text: GetSysColor(14),
        }
    };
    theme_colors(is_dark, sys)
}

fn client(h: Handle) -> Rect {
    let mut rc = Rect::default();
    // SAFETY: plain user32 call.
    unsafe { GetClientRect(h, &mut rc) };
    rc
}

fn validate_viewport(hre: Handle, wd: *mut WindowData) {
    if hre.is_null() || wd.is_null() {
        return;
    }
    let a = app(wd);
    // SAFETY: live data, copied out.
    let (mode, max_codes, is_dark) = unsafe { (mode_of(wd), (*wd).snapshot.max_codes, (*wd).state.is_dark != 0) };
    let mut check = |c: u16| a.engine_char_ok(c);
    let mut ctx = editor::EditorContext {
        mode: ImeMode::from_i32(mode),
        max_codes,
        is_dark,
        char_ok: if a.has_engine() { Some(&mut check) } else { None },
    };
    editor::validate_viewport(hre, &mut ctx);
}

fn enable_save_button(wd: *mut WindowData, enable: bool) {
    let h = find_control(wd, CTRL_CUSTOM_TABLE_SAVE);
    if !h.is_null() {
        // SAFETY: plain user32 call.
        unsafe { EnableWindow(h, i32::from(enable)) };
    }
}

fn apply_and_save(wd: *mut WindowData) {
    // SAFETY: a copy of the snapshot is handed over.
    let snap = unsafe { (*wd).snapshot };
    app(wd).apply_snapshot(&snap);
    app(wd).write_config(mode_of(wd));
}

fn destroy_brushes(wd: *mut WindowData) {
    // SAFETY: live data; destroy_brushes only deletes GDI objects.
    unsafe { shell::destroy_brushes(&mut (*wd).brushes) };
}

fn create_theme_brushes(wd: *mut WindowData) {
    destroy_brushes(wd);
    // SAFETY: live data; no re-entry inside.
    unsafe {
        let d = data(wd);
        d.theme = theme_for(d.state.is_dark != 0);
        d.brushes = shell::create_brushes(&d.theme);
    }
}

fn destroy_fonts(wd: *mut WindowData) {
    // SAFETY: live data; DeleteObject does not re-enter.
    unsafe {
        let f = &mut data(wd).fonts;
        for h in [
            &mut f.title,
            &mut f.card_header,
            &mut f.body,
            &mut f.desc,
            &mut f.mdl2,
            &mut f.mdl2_icon,
        ] {
            if !h.is_null() {
                DeleteObject(*h);
                *h = null_mut();
            }
        }
    }
}

fn create_fonts(wd: *mut WindowData, dpi: i32) {
    destroy_fonts(wd);
    let f = paint::create_fonts(dpi as u32);
    // SAFETY: live data.
    unsafe { data(wd).fonts = f };
}

fn card_w_for(hmain: Handle, dpi: u32, sidebar_takes_space: bool) -> i32 {
    let rc = client(hmain);
    // SAFETY: plain user32 call.
    card_width(rc.right, dpi, sidebar_takes_space, unsafe {
        GetSystemMetrics(SM_CXVSCROLL)
    })
}

/// `MakePaintContext`: `hpaint` is the window painted into.
fn make_paint_context(hpaint: Handle, hmain: Handle, wd: *mut WindowData) -> PaintContext {
    let dpi = app(wd).dpi_for_window(hmain);
    let rc = client(hpaint);
    // SAFETY: live data copied into the context; the Win32 calls below do not re-enter.
    unsafe {
        let d = data(wd);
        let mut ctx: PaintContext = std::mem::zeroed();
        ctx.theme = d.theme;
        ctx.snapshot = d.snapshot;
        ctx.font_title = d.fonts.title;
        ctx.font_card_header = d.fonts.card_header;
        ctx.font_body = d.fonts.body;
        ctx.font_desc = d.fonts.desc;
        ctx.font_mdl2 = d.fonts.mdl2;
        ctx.font_mdl2_icon = d.fonts.mdl2_icon;
        ctx.brush_sidebar_bg = d.brushes[0];
        ctx.brush_content_bg = d.brushes[1];
        ctx.icon_module = GetModuleHandleW(null());
        ctx.dpi = dpi;
        ctx.has_mdl2 = i32::from(d.fonts.has_mdl2 != 0);
        ctx.client_w = rc.right;
        ctx.client_h = rc.bottom;
        let n = &d.state.nav;
        ctx.card_w = card_w_for(hmain, dpi, n.sidebar_narrow_mode == 0 && n.sidebar_collapsed == 0);
        ctx.sys_face = GetSysColor(15);
        ctx.selected_mode_index = n.selected_mode_index;
        ctx.hover_sidebar_index = n.hover_sidebar_index;
        ctx.nav_level = n.nav_level;
        ctx.current_card_index = n.current_card_index;
        ctx.hover_row_index = n.hover_row_index;
        ctx.current_mode = n.current_mode;
        ctx.scroll_pos = n.scroll_pos;
        ctx.sidebar_narrow_mode = i32::from(n.sidebar_narrow_mode != 0);
        ctx.sidebar_collapsed = i32::from(n.sidebar_collapsed != 0);
        ctx.color_grid_expanded = i32::from(n.color_grid_expanded != 0);
        ctx.card_expanded = n.card_expanded;
        // Toggle state of the first control with each id (FindControl order).
        let mut seen = [false; KEY_TOGGLE_SLOTS];
        for c in d.controls.iter() {
            if c.id < 0 || c.id as usize >= KEY_TOGGLE_SLOTS || seen[c.id as usize] {
                continue;
            }
            seen[c.id as usize] = true;
            ctx.toggle_on[c.id as usize] =
                u8::from(!c.hwnd.is_null() && SendMessageW(c.hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        ctx
    }
}

// ---- Driver host -------------------------------------------------------------

struct DriverHostImpl {
    hmain: Handle,
    wd: *mut WindowData,
}

impl DriverHostImpl {
    fn content(&self) -> Handle {
        // SAFETY: live data, read only.
        unsafe { (*self.wd).content }
    }
    fn control_hwnd(&self, i: i32) -> Handle {
        // SAFETY: live data, read only.
        unsafe {
            let c = &(*self.wd).controls;
            if i >= 0 && (i as usize) < c.len() {
                c[i as usize].hwnd
            } else {
                null_mut()
            }
        }
    }
}

fn switch_mode(hmain: Handle, wd: *mut WindowData, mode: i32) {
    let mut host = DriverHostImpl { hmain, wd };
    // SAFETY: `state` lives as long as the data; the driver only borrows it briefly.
    unsafe { driver::switch_mode(&mut host, addr_of_mut!((*wd).state), mode) };
}

fn rebuild(hmain: Handle, wd: *mut WindowData) {
    let mut host = DriverHostImpl { hmain, wd };
    // SAFETY: as above.
    unsafe { driver::rebuild(&mut host, addr_of_mut!((*wd).state)) };
}

fn run_frame(hmain: Handle, wd: *mut WindowData, ev: FrameEvent) {
    let mut host = DriverHostImpl { hmain, wd };
    // SAFETY: as above.
    unsafe { driver::frame(&mut host, addr_of_mut!((*wd).state), ev) };
}

fn run_nav(hmain: Handle, wd: *mut WindowData, ev: NavEvent) {
    let mut host = DriverHostImpl { hmain, wd };
    // SAFETY: as above.
    unsafe { driver::nav(&mut host, addr_of_mut!((*wd).state), ev) };
}

impl DriverHost for DriverHostImpl {
    fn main_dpi(&mut self) -> u32 {
        app(self.wd).dpi_for_window(self.hmain)
    }
    fn content_dpi(&mut self) -> u32 {
        app(self.wd).dpi_for_window(self.content())
    }
    fn has_content(&mut self) -> bool {
        !self.content().is_null()
    }
    fn main_client(&mut self) -> (i32, i32) {
        let rc = client(self.hmain);
        (rc.right, rc.bottom)
    }
    fn content_client_height(&mut self) -> i32 {
        let c = self.content();
        if c.is_null() {
            0
        } else {
            client(c).bottom
        }
    }
    fn vscroll_width(&mut self) -> i32 {
        // SAFETY: plain user32 call.
        unsafe { GetSystemMetrics(SM_CXVSCROLL) }
    }
    fn system_dark(&mut self) -> bool {
        app(self.wd).system_dark()
    }
    fn color_mode(&mut self) -> i32 {
        // SAFETY: live data, read only.
        unsafe { (*self.wd).snapshot.color_mode }
    }
    fn save_enabled(&mut self) -> bool {
        let h = find_control(self.wd, CTRL_CUSTOM_TABLE_SAVE);
        // SAFETY: plain user32 call.
        !h.is_null() && unsafe { IsWindowEnabled(h) } != 0
    }
    fn preview_height(&mut self, dpi: u32) -> i32 {
        let c = self.content();
        if c.is_null() {
            return 0;
        }
        // SAFETY: plain GDI calls; the snapshot is copied out first.
        unsafe {
            let snap = (*self.wd).snapshot;
            let hdc = GetDC(c);
            let h = paint::preview_height(hdc, &snap, dpi);
            ReleaseDC(c, hdc);
            h
        }
    }

    fn control_count(&mut self) -> i32 {
        // SAFETY: live data, read only.
        unsafe { (*self.wd).controls.len() as i32 }
    }
    fn control(&mut self, index: i32) -> ControlInfo {
        // SAFETY: live data copied out; IsWindow / IsWindowVisible do not re-enter.
        unsafe {
            let c = &(*self.wd).controls;
            if index < 0 || index as usize >= c.len() {
                return ControlInfo::default();
            }
            let ch = c[index as usize];
            let alive = !ch.hwnd.is_null() && IsWindow(ch.hwnd) != 0;
            ControlInfo {
                id: ch.id,
                exists: i32::from(!ch.hwnd.is_null()),
                alive: i32::from(alive),
                visible: i32::from(alive && IsWindowVisible(ch.hwnd) != 0),
                x: ch.x,
                y: ch.y,
                w: ch.w,
                h: ch.h,
            }
        }
    }
    fn move_control(&mut self, index: i32, x: i32, y: i32, w: i32, h: i32) {
        let hc = self.control_hwnd(index);
        if !hc.is_null() {
            // SAFETY: plain user32 call.
            unsafe { SetWindowPos(hc, null_mut(), x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE) };
        }
    }
    fn show_control(&mut self, index: i32, show: bool) {
        let hc = self.control_hwnd(index);
        if !hc.is_null() {
            // SAFETY: plain user32 call.
            unsafe { ShowWindow(hc, if show { SW_SHOW } else { SW_HIDE }) };
        }
    }
    fn reset_control_region(&mut self, index: i32) {
        let hc = self.control_hwnd(index);
        if !hc.is_null() {
            // SAFETY: plain user32 call.
            unsafe { SetWindowRgn(hc, null_mut(), 1) };
        }
    }
    fn control_checked(&mut self, index: i32) -> bool {
        let hc = self.control_hwnd(index);
        // SAFETY: plain user32 call.
        !hc.is_null() && unsafe { SendMessageW(hc, BM_GETCHECK, 0, 0) } == BST_CHECKED
    }
    fn set_control_checked(&mut self, index: i32, on: bool) {
        let hc = self.control_hwnd(index);
        if !hc.is_null() {
            // SAFETY: plain user32 call.
            unsafe { SendMessageW(hc, BM_SETCHECK, usize::from(on), 0) };
        }
    }
    fn destroy_controls(&mut self) {
        // The list is taken first: destroying a child can re-enter.
        // SAFETY: live data.
        let list = unsafe { std::mem::take(&mut (*self.wd).controls) };
        for c in &list {
            if !c.hwnd.is_null() {
                // SAFETY: plain user32 call.
                unsafe { DestroyWindow(c.hwnd) };
            }
        }
    }
    fn create_controls(&mut self, input: &PlanInput, card_w: i32) {
        let specs = controls::build_specs(input, card_w, ime_cards(), load_cards());
        // SAFETY: live data copied out.
        let ctx = unsafe {
            let d = data(self.wd);
            children::Context {
                parent: d.content,
                instance: d.instance,
                font_body: d.fonts.body,
                is_dark: d.state.is_dark,
                card_bg: d.theme.card_bg,
                text_primary: d.theme.text_primary,
            }
        };
        let mut host = ChildHostImpl { wd: self.wd };
        children::create(&mut host, &ctx, &specs);
    }
    fn populate(&mut self) {
        populate_controls(self.wd);
    }

    fn invalidate_main(&mut self, rc: Option<[i32; 4]>, erase: bool) {
        let r = rc.map(|r| Rect {
            left: r[0],
            top: r[1],
            right: r[2],
            bottom: r[3],
        });
        // SAFETY: plain user32 call.
        unsafe {
            InvalidateRect(
                self.hmain,
                r.as_ref().map_or(null(), |r| r as *const Rect),
                i32::from(erase),
            )
        };
    }
    fn invalidate_content(&mut self, erase: bool) {
        let c = self.content();
        if !c.is_null() {
            // SAFETY: plain user32 call.
            unsafe { InvalidateRect(c, null(), i32::from(erase)) };
        }
    }
    fn track_leave(&mut self, content: bool) {
        let target = if content { self.content() } else { self.hmain };
        if target.is_null() {
            return;
        }
        let mut tme = TrackMouseEvent {
            cb_size: std::mem::size_of::<TrackMouseEvent>() as u32,
            flags: TME_LEAVE,
            hwnd: target,
            hover_time: 0,
        };
        // SAFETY: plain user32 call.
        unsafe { TrackMouseEvent(&mut tme) };
    }
    fn set_scroll_info(&mut self, total: i32, page: i32, pos: i32) {
        let c = self.content();
        if c.is_null() {
            return;
        }
        let si = ScrollInfo {
            cb_size: std::mem::size_of::<ScrollInfo>() as u32,
            mask: SIF_RANGE | SIF_PAGE | SIF_POS,
            min: 0,
            max: total,
            page: page as u32,
            pos,
            track_pos: 0,
        };
        // SAFETY: plain user32 call.
        unsafe { SetScrollInfo(c, SB_VERT, &si, 1) };
    }
    fn redraw_content(&mut self) {
        let c = self.content();
        if !c.is_null() {
            // SAFETY: plain user32 call.
            unsafe { RedrawWindow(c, null(), null_mut(), RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN) };
        }
    }
    fn update_content(&mut self) {
        let c = self.content();
        if !c.is_null() {
            // SAFETY: plain user32 call.
            unsafe { UpdateWindow(c) };
        }
    }
    fn move_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
        let c = self.content();
        if !c.is_null() {
            // SAFETY: plain user32 call.
            unsafe { MoveWindow(c, x, y, w, h, 0) };
        }
    }
    fn focus_content(&mut self) {
        let c = self.content();
        if !c.is_null() {
            // SAFETY: plain user32 call.
            unsafe { SetFocus(c) };
        }
    }
    fn create_content(&mut self, x: i32, y: i32, w: i32, h: i32) {
        // SAFETY: live data copied out before CreateWindowEx (which re-enters).
        let (instance, class) = unsafe { ((*self.wd).instance, (*self.wd).content_class.clone()) };
        shell::register_content_class(instance, class.as_ptr(), content_wnd_proc as *const c_void);
        // SAFETY: NUL-terminated class name.
        let hc = unsafe {
            CreateWindowExW(
                0,
                class.as_ptr(),
                null(),
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                x,
                y,
                w,
                h,
                self.hmain,
                IDC_CONTENT_AREA as Handle,
                instance,
                null_mut(),
            )
        };
        // SAFETY: live data.
        unsafe { (*self.wd).content = hc };
        set_data(hc, self.wd);
    }
    fn content_theme(&mut self, dark: bool) {
        let c = self.content();
        if !c.is_null() {
            let name = wide("DarkMode_Explorer");
            // SAFETY: plain uxtheme call.
            unsafe { SetWindowTheme(c, if dark { name.as_ptr() } else { null() }, null()) };
        }
    }
    fn set_main_pos(&mut self, x: i32, y: i32, w: i32, h: i32) {
        // SAFETY: plain user32 call.
        unsafe { SetWindowPos(self.hmain, null_mut(), x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE) };
    }
    fn set_dark_title(&mut self, dark: bool) {
        let v: i32 = i32::from(dark);
        // SAFETY: plain dwmapi call with a BOOL.
        unsafe {
            DwmSetWindowAttribute(
                self.hmain,
                DWMWA_USE_IMMERSIVE_DARK_MODE,
                &v as *const i32 as *const c_void,
                4,
            )
        };
    }
    fn set_caption_color(&mut self, attribute: i32) {
        // SAFETY: live data copied out; plain dwmapi call with a COLORREF.
        unsafe {
            let c: u32 = (*self.wd).theme.content_bg;
            DwmSetWindowAttribute(self.hmain, attribute as u32, &c as *const u32 as *const c_void, 4);
        }
    }
    fn theme_brushes(&mut self) {
        create_theme_brushes(self.wd);
    }
    fn create_fonts(&mut self, dpi: i32) {
        create_fonts(self.wd, dpi);
    }
    fn destroy_fonts(&mut self) {
        destroy_fonts(self.wd);
    }

    fn ask_save_before_back(&mut self) -> i32 {
        let text: Vec<u16> =
            "自建詞庫尚未儲存，是否要儲存後離開？\n\n[是] 儲存並離開\n[否] 不儲存離開\n[取消] 繼續編輯"
                .encode_utf16()
                .collect();
        let caption: Vec<u16> = "自建詞庫未儲存".encode_utf16().collect();
        app(self.wd).show_message(self.hmain, &text, &caption, MB_YESNOCANCEL | MB_ICONQUESTION)
    }
    fn click_save(&mut self) {
        let hsave = find_control(self.wd, CTRL_CUSTOM_TABLE_SAVE);
        let wparam = (3000 + CTRL_CUSTOM_TABLE_SAVE) as usize | (BN_CLICKED << 16);
        // SAFETY: plain user32 call; it re-enters the content procedure.
        unsafe { SendMessageW(self.content(), WM_COMMAND, wparam, hsave as isize) };
    }

    fn toggle_snapshot(&mut self, id: i32, on: bool) {
        // SAFETY: live data; sync_toggle does not call out.
        unsafe { sync_toggle(&mut (*self.wd).snapshot, id, on) };
    }
    fn apply_and_save(&mut self) {
        apply_and_save(self.wd);
    }
    fn enumerate_reverse_providers(&mut self) {
        app(self.wd).enumerate_reverse_providers();
    }
    fn load_config(&mut self, mode: i32) {
        app(self.wd).load_config(mode);
    }
    fn load_colors(&mut self) {
        let a = app(self.wd);
        a.load_colors_for_mode(a.color_mode());
    }
    fn load_snapshot(&mut self) {
        let s = app(self.wd).load_snapshot();
        // SAFETY: live data.
        unsafe { (*self.wd).snapshot = s };
    }
    fn recreate_engine(&mut self, mode: i32) {
        app(self.wd).recreate_engine(mode);
    }
}

// ---- Children and populate ---------------------------------------------------

struct ChildHostImpl {
    wd: *mut WindowData,
}

impl ChildHost for ChildHostImpl {
    fn added(&mut self, ctrl_id: i32, h: Handle, add: [i32; 4], _vis_h: i32) {
        // SAFETY: live data.
        unsafe {
            (*self.wd).controls.push(ControlHandle {
                id: ctrl_id,
                hwnd: h,
                x: add[0],
                y: add[1],
                w: add[2],
                h: add[3],
            })
        };
    }
    fn editor_path(&mut self) -> Vec<u16> {
        app(self.wd).custom_table_txt_path(mode_of(self.wd))
    }
    fn editor_ready(&mut self, hre: Handle, line_count: i32) {
        // SAFETY: live data.
        unsafe {
            let d = data(self.wd);
            d.ct_last_line_count = line_count;
            d.ct_last_edited_line = -1;
            d.ct_keystroke_count = 0;
        }
        // Paint the visible range now; [儲存] stays disabled.
        validate_viewport(hre, self.wd);
    }
}

fn populate_controls(wd: *mut WindowData) {
    let a = app(wd);
    let count = a.reverse_count();
    let names: Vec<Vec<u16>> = (0..count).map(|i| a.reverse_description(i)).collect();
    let entries: Vec<ReverseEntry> = names
        .iter()
        .enumerate()
        .map(|(i, n)| ReverseEntry {
            description: n,
            selected: a.reverse_is_current(i as i32),
        })
        .collect();
    // SAFETY: a copy of the snapshot.
    let snap = unsafe { (*wd).snapshot };
    let ops = populate::populate(&snap, a.windows_1809_or_later(), a.reverse_is_null(), &entries);
    for op in ops {
        let h = find_control(wd, op.ctrl_id);
        if h.is_null() {
            continue;
        }
        let text = with_nul(&op.text);
        // SAFETY: plain messages to our controls; `text` outlives each call.
        unsafe {
            match op.kind {
                populate::OP_RESET => {
                    SendMessageW(h, CB_RESETCONTENT, 0, 0);
                }
                populate::OP_ADD => {
                    let idx = SendMessageW(h, CB_ADDSTRING, 0, text.as_ptr() as isize);
                    if let Some(d) = op.data {
                        SendMessageW(h, CB_SETITEMDATA, idx as usize, d as isize);
                    }
                }
                populate::OP_SELECT => {
                    SendMessageW(h, CB_SETCURSEL, op.value as usize, 0);
                }
                populate::OP_CHECK => {
                    SendMessageW(h, BM_SETCHECK, usize::from(op.value != 0), 0);
                }
                populate::OP_TEXT => {
                    SetWindowTextW(h, text.as_ptr());
                }
                _ => {}
            }
        }
    }
}

// ---- Command host ------------------------------------------------------------

struct CmdHostImpl {
    hwnd: Handle, // content area
    wd: *mut WindowData,
    sender: Handle,
}

impl CmdHostImpl {
    fn editor(&self) -> Handle {
        find_control(self.wd, CTRL_CUSTOM_TABLE_EDITOR)
    }
    fn parent(&self) -> Handle {
        // SAFETY: plain user32 call.
        unsafe { GetParent(self.hwnd) }
    }
}

impl command::Host for CmdHostImpl {
    fn log_pixels_y(&mut self) -> i32 {
        winhost::log_pixels_y(self.hwnd)
    }
    fn choose_font(&mut self, lf: &mut LogFont, point_size10: &mut i32) -> bool {
        winhost::pick_font(self.parent(), lf, point_size10)
    }
    fn choose_color(&mut self, rgb: &mut u32) -> bool {
        winhost::pick_color(self.parent(), rgb)
    }
    fn get_open_file(&mut self, filter: i32) -> Option<Vec<u16>> {
        winhost::open_file_dialog(self.parent(), filter)
    }
    fn get_save_file(&mut self, default_path: &[u16]) -> Option<Vec<u16>> {
        winhost::save_file_dialog(self.parent(), default_path)
    }
    fn message_box(&mut self, text: &[u16], caption: &str, mb_type: u32) -> i32 {
        let caption: Vec<u16> = caption.encode_utf16().collect();
        app(self.wd).show_message(self.parent(), text, &caption, mb_type)
    }
    fn app_data_path(&mut self) -> Vec<u16> {
        app(self.wd).app_data_path()
    }
    fn documents_path(&mut self) -> Vec<u16> {
        winhost::documents_path()
    }
    fn custom_table_txt_path(&mut self, mode: i32) -> Vec<u16> {
        app(self.wd).custom_table_txt_path(mode)
    }
    fn custom_table_cin_path(&mut self, mode: i32) -> Vec<u16> {
        app(self.wd).custom_table_cin_path(mode)
    }
    fn write_bom_file(&mut self, path: &[u16], text: &[u16]) -> bool {
        winhost::write_bom_file(path, text)
    }
    fn load_text_file(&mut self, path: &[u16]) -> Option<Vec<u16>> {
        children::load_text_file(&with_nul(path))
    }
    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom_table: bool) -> bool {
        app(self.wd).parse_cin(src, dst, custom_table)
    }
    fn reset_all_defaults(&mut self, mode: i32) {
        app(self.wd).reset_all_defaults(mode);
    }
    fn write_config(&mut self, mode: i32) {
        app(self.wd).write_config(mode);
    }
    fn load_config(&mut self, mode: i32) {
        app(self.wd).load_config(mode);
    }
    fn load_snapshot(&mut self) -> SettingsSnapshot {
        app(self.wd).load_snapshot()
    }
    fn apply_and_save(&mut self) {
        apply_and_save(self.wd);
    }
    fn set_color_mode(&mut self, color_mode: i32) {
        app(self.wd).set_color_mode(color_mode);
    }
    fn load_colors_for_mode(&mut self, color_mode: i32) {
        app(self.wd).load_colors_for_mode(color_mode);
    }
    fn get_colors(&mut self) -> [u32; 6] {
        app(self.wd).get_colors()
    }
    fn set_color(&mut self, index: i32, color: u32) {
        app(self.wd).set_color(index, color);
    }
    fn save_colors_for_mode(&mut self, color_mode: i32) {
        app(self.wd).save_colors_for_mode(color_mode);
    }
    fn set_reverse_conversion_selection(&mut self, sel: i32) {
        app(self.wd).set_reverse_selection(sel);
    }
    fn rebuild(&mut self) {
        rebuild(self.parent(), self.wd);
    }
    fn invalidate_content(&mut self, erase: bool) {
        // SAFETY: plain user32 call.
        unsafe { InvalidateRect(self.hwnd, null(), i32::from(erase)) };
    }
    fn update_content(&mut self) {
        // SAFETY: plain user32 call.
        unsafe { UpdateWindow(self.hwnd) };
    }
    fn invalidate_swatch(&mut self, _index: i32) {
        // SAFETY: plain user32 call.
        unsafe { InvalidateRect(self.sender, null(), 1) };
    }
    fn editor_exists(&mut self) -> bool {
        !self.editor().is_null()
    }
    fn editor_text(&mut self) -> Vec<u16> {
        let re = self.editor();
        if re.is_null() {
            Vec::new()
        } else {
            winhost::editor_text(re)
        }
    }
    fn set_editor_text_muted(&mut self, text: &[u16]) {
        let re = self.editor();
        if re.is_null() {
            return;
        }
        let lines = winhost::set_editor_text_muted(re, text);
        // SAFETY: live data.
        unsafe {
            let d = data(self.wd);
            d.ct_last_line_count = lines;
            d.ct_last_edited_line = -1;
            d.ct_keystroke_count = 0;
        }
    }
    fn editor_line_index(&mut self, line: i32) -> i32 {
        let re = self.editor();
        if re.is_null() {
            -1
        } else {
            winhost::editor_line_index(re, line)
        }
    }
    fn editor_focus_at(&mut self, cp: i32) {
        let re = self.editor();
        if re.is_null() {
            return;
        }
        winhost::editor_focus_at(re, cp);
        validate_viewport(re, self.wd);
    }
    fn validate_viewport(&mut self) {
        let re = self.editor();
        if !re.is_null() {
            validate_viewport(re, self.wd);
        }
    }
    fn cancel_bg_scan(&mut self) {}
    fn enable_save(&mut self, enable: bool) {
        enable_save_button(self.wd, enable);
    }
    fn update_line_count(&mut self) {
        let re = self.editor();
        if !re.is_null() {
            let n = winhost::editor_line_count(re);
            // SAFETY: live data.
            unsafe { (*self.wd).ct_last_line_count = n };
        }
    }
    fn validate_buffer(&mut self, text: &[u16], max_report: i32) -> (i32, i32, Vec<u16>) {
        let a = app(self.wd);
        // SAFETY: live data copied out.
        let max_codes = unsafe { (*self.wd).snapshot.max_codes };
        let mode = ImeMode::from_i32(mode_of(self.wd));
        let mut check = |c: u16| a.engine_char_ok(c);
        let report = if a.has_engine() {
            validate::validate_buffer(text, mode, max_codes, Some(&mut check), max_report)
        } else {
            validate::validate_buffer(text, mode, max_codes, None, max_report)
        };
        // `wchar_t errSummary[1024]` in the C++ host.
        let mut summary = validate::truncate_for_buffer(&report.summary, 1024);
        summary.pop();
        let first = if report.error_count > 0 {
            report.first_error_line
        } else {
            0
        };
        (report.error_count, first, summary)
    }
}

fn handle_command(hwnd: Handle, wd: *mut WindowData, wparam: usize, lparam: isize) {
    let sender = lparam as Handle;
    let mut ev = CommandEvent {
        notify_code: ((wparam >> 16) & 0xFFFF) as i32,
        ctrl_id: (wparam & 0xFFFF) as i32,
        mode: mode_of(wd),
        combo_sel: 0,
        combo_item_data: 0,
        edit_text: [0; 16],
    };
    if ev.notify_code == CBN_SELCHANGE && !sender.is_null() {
        // SAFETY: plain user32 calls.
        unsafe {
            let sel = SendMessageW(sender, CB_GETCURSEL, 0, 0);
            ev.combo_sel = sel as i32;
            if sel != CB_ERR {
                ev.combo_item_data = SendMessageW(sender, CB_GETITEMDATA, sel as usize, 0) as i32;
            }
        }
    }
    if ev.notify_code == EN_CHANGE && ev.ctrl_id - 3000 == CTRL_MAX_CODES && !sender.is_null() {
        // SAFETY: a 16-unit buffer, as in C++.
        unsafe { GetWindowTextW(sender, ev.edit_text.as_mut_ptr(), 16) };
    }
    let mut host = CmdHostImpl { hwnd, wd, sender };
    // SAFETY: both fields live as long as the data; written in place, and
    // hosts only borrow them briefly.
    let (snap, grid) = unsafe {
        (
            addr_of_mut!((*wd).snapshot),
            addr_of_mut!((*wd).state.nav.color_grid_expanded),
        )
    };
    // SAFETY: as above.
    let mut set_grid = |on: bool| unsafe { *grid = i32::from(on) };
    // SAFETY: as above.
    unsafe { command::handle(&mut host, snap, &mut set_grid, &ev) };
}

// ---- Window procedures -------------------------------------------------------

struct ProcHostImpl {
    hwnd: Handle,
    hmain: Handle,
    wd: *mut WindowData,
}

unsafe extern "C" fn paint_has_data(user: *mut c_void) -> i32 {
    // SAFETY: `user` is the ProcHostImpl of the paint call.
    i32::from(unsafe { !(*(user as *const ProcHostImpl)).wd.is_null() })
}

unsafe extern "C" fn paint_prepare(
    user: *mut c_void,
    content: i32,
    first: *mut PaintContext,
    second: *mut PaintContext,
    has_content: *mut i32,
) {
    // SAFETY: `user` is the ProcHostImpl of the paint call; outputs come from the shell.
    unsafe {
        let h = &*(user as *const ProcHostImpl);
        let wd = h.wd;
        if content == 0 {
            *first = make_paint_context(h.hwnd, h.hwnd, wd);
            *has_content = i32::from(!(*wd).content.is_null());
            return;
        }
        // The open narrow sidebar is painted over the page: hide the child controls.
        let n = &(*wd).state.nav;
        if n.sidebar_narrow_mode != 0 && n.sidebar_collapsed == 0 {
            let list: Vec<Handle> = (*wd).controls.iter().map(|c| c.hwnd).collect();
            for hc in list {
                if !hc.is_null() && IsWindow(hc) != 0 {
                    ShowWindow(hc, SW_HIDE);
                }
            }
        }
        *first = make_paint_context(h.hwnd, h.hmain, wd);
        *second = make_paint_context(h.hmain, h.hmain, wd);
    }
}

impl ProcHostImpl {
    fn source(&self) -> PaintSource {
        PaintSource {
            user: self as *const Self as *mut c_void,
            has_data: paint_has_data,
            prepare: paint_prepare,
        }
    }
    fn frame(&mut self, kind: i32) {
        let ev = FrameEvent {
            kind,
            dpi: app(self.wd).dpi_for_window(self.hwnd),
            ..frame_event()
        };
        run_frame(self.hwnd, self.wd, ev);
    }
}

fn frame_event() -> FrameEvent {
    // SAFETY: plain data; zeros is `= {}`.
    unsafe { std::mem::zeroed() }
}

impl ProcHost for ProcHostImpl {
    fn has_data(&mut self) -> bool {
        !self.wd.is_null()
    }
    fn has_content(&mut self) -> bool {
        // SAFETY: live data when non-null.
        !self.wd.is_null() && unsafe { !(*self.wd).content.is_null() }
    }
    fn create(&mut self, instance: isize) {
        let Some(p) = PENDING.with(|c| c.borrow_mut().take()) else {
            return;
        };
        let wd = Box::into_raw(Box::new(WindowData::new(
            p.app,
            p.content_class,
            p.placement_key,
            p.placement_value,
        )));
        set_data(self.hwnd, wd);
        self.wd = wd;
        // SAFETY: the data was just created.
        unsafe { (*wd).instance = instance as Handle };
        let rc = client(self.hwnd);
        let ev = FrameEvent {
            kind: frame::EV_CREATE,
            dpi: app(wd).dpi_for_window(self.hwnd),
            client_w: rc.right,
            client_h: rc.bottom,
            ..frame_event()
        };
        run_frame(self.hwnd, wd, ev);
    }
    fn nav(&mut self, kind: i32, x: i32, y: i32, delta: i32, thumb: i32) {
        // SAFETY: plain data; zeros is `= {}`.
        let mut ev: NavEvent = unsafe { std::mem::zeroed() };
        ev.kind = kind;
        ev.x = x;
        ev.y = y;
        ev.delta = delta;
        ev.thumb = thumb;
        run_nav(self.hmain, self.wd, ev);
    }
    fn theme_changed(&mut self) {
        self.frame(frame::EV_THEME_CHANGED);
    }
    fn setting_changed(&mut self) {
        self.frame(frame::EV_SETTING_CHANGE);
    }
    fn dpi_changed(&mut self, dpi: u32, rect: Option<[i32; 4]>) {
        let ev = FrameEvent {
            kind: frame::EV_DPI_CHANGED,
            dpi,
            has_rect: i32::from(rect.is_some()),
            rc: rect.unwrap_or_default(),
            ..frame_event()
        };
        run_frame(self.hwnd, self.wd, ev);
    }
    fn paint_main(&mut self) {
        let src = self.source();
        shell::paint_main(self.hwnd, &src);
    }
    fn paint_content(&mut self) {
        let src = self.source();
        shell::paint_content(self.hwnd, &src);
    }
    fn screen_dpi(&mut self) -> u32 {
        let mut dpi = 96;
        // SAFETY: plain GDI calls.
        unsafe {
            let hdc = GetDC(null_mut());
            if !hdc.is_null() {
                dpi = GetDeviceCaps(hdc, LOGPIXELSY) as u32;
                ReleaseDC(null_mut(), hdc);
            }
        }
        dpi
    }
    fn switch_mode(&mut self, mode: i32) {
        switch_mode(self.hwnd, self.wd, mode);
    }
    fn set_foreground(&mut self) {
        // SAFETY: plain user32 call.
        unsafe { SetForegroundWindow(self.hwnd) };
    }
    fn destroy(&mut self) {
        let wd = self.wd;
        if wd.is_null() {
            return;
        }
        // SAFETY: copies of NUL-terminated strings.
        let (key, value) = unsafe { ((*wd).placement_key.clone(), (*wd).placement_value.clone()) };
        shell::save_placement(self.hwnd, key.as_ptr(), value.as_ptr());
        app(wd).destroy_engine();
        destroy_brushes(wd);
        destroy_fonts(wd);
        // SAFETY: live data; the list is copied before DestroyWindow re-enters.
        let list: Vec<Handle> = unsafe { (*wd).controls.iter().map(|c| c.hwnd).collect() };
        for h in list {
            if !h.is_null() {
                // SAFETY: plain user32 call.
                unsafe { DestroyWindow(h) };
            }
        }
        // The content area keeps the pointer until it is destroyed with us:
        // clear it first so its late messages see no data.
        // SAFETY: live data, read once.
        let content = unsafe { (*wd).content };
        if !content.is_null() {
            set_data(content, null_mut());
        }
        set_data(self.hwnd, null_mut());
        self.wd = null_mut();
        // SAFETY: created by Box::into_raw in `create`; no other owner.
        drop(unsafe { Box::from_raw(wd) });
    }
    fn quit(&mut self) {
        // SAFETY: plain user32 call.
        unsafe { PostQuitMessage(0) };
    }
    fn command(&mut self, wparam: usize, lparam: isize) {
        handle_command(self.hwnd, self.wd, wparam, lparam);
    }
    fn editor_revalidate(&mut self) {
        let re = find_control(self.wd, CTRL_CUSTOM_TABLE_EDITOR);
        if !re.is_null() {
            validate_viewport(re, self.wd);
            enable_save_button(self.wd, true);
            // SAFETY: live data.
            unsafe { (*self.wd).ct_keystroke_count = 0 };
        }
    }
    fn editor_viewport(&mut self) {
        let re = find_control(self.wd, CTRL_CUSTOM_TABLE_EDITOR);
        if !re.is_null() {
            validate_viewport(re, self.wd);
        }
    }
    fn draw_item(&mut self, dis: isize) -> bool {
        let dpi = app(self.wd).dpi_for_window(self.hmain);
        // SAFETY: `dis` is the DRAWITEMSTRUCT of WM_DRAWITEM; data copied out.
        unsafe {
            let d = data(self.wd);
            let s = &d.snapshot;
            let ctx = owner::Context {
                theme: d.theme,
                font_body: d.fonts.body,
                dpi,
                swatch: [
                    s.item_color,
                    s.selected_color,
                    s.item_bg_color,
                    s.phrase_color,
                    s.number_color,
                    s.selected_bg_color,
                ],
            };
            owner::draw_item(&*(dis as *const DrawItem), &ctx)
        }
    }
    fn ctl_color(&mut self, hdc: isize) -> Option<isize> {
        // SAFETY: live data; ctl_color may create the card brush in place.
        unsafe {
            let d = data(self.wd);
            let theme = d.theme;
            if !owner::ctl_color(hdc as Handle, d.state.is_dark != 0, &theme, &mut d.brushes[2]) {
                return None;
            }
            Some(d.brushes[2] as isize)
        }
    }
    fn default(&mut self, msg: u32, wparam: usize, lparam: isize) -> isize {
        // SAFETY: plain user32 call.
        unsafe { DefWindowProcW(self.hwnd, msg, wparam, lparam) }
    }
}

/// The main window procedure.
///
/// # Safety
/// Called by user32 for a window of a class registered with it.
pub unsafe extern "system" fn main_wnd_proc(h: Handle, msg: u32, w: usize, l: isize) -> isize {
    let mut host = ProcHostImpl {
        hwnd: h,
        hmain: h,
        wd: get_data(h),
    };
    // SAFETY: called by user32 with this window's message.
    unsafe { wndproc::main_proc(&mut host, msg, w, l) }
}

/// The content area procedure.
///
/// # Safety
/// Called by user32 for a window of a class registered with it.
pub unsafe extern "system" fn content_wnd_proc(h: Handle, msg: u32, w: usize, l: isize) -> isize {
    // SAFETY: plain user32 call.
    let parent = unsafe { GetParent(h) };
    let mut host = ProcHostImpl {
        hwnd: h,
        hmain: parent,
        wd: get_data(h),
    };
    wndproc::content_proc(&mut host, msg, w, l)
}

// ---- Entry points ------------------------------------------------------------

struct Start {
    instance: Handle,
    mode: i32,
}

unsafe extern "C" fn started(user: *mut c_void, hwnd: Handle) {
    // SAFETY: `user` is the Start of `run`.
    let st = unsafe { &*(user as *const Start) };
    let wd = get_data(hwnd);
    if !wd.is_null() {
        // SAFETY: live data.
        unsafe { (*wd).instance = st.instance };
        switch_mode(hwnd, wd, st.mode);
    }
}

fn set_pending(app: Box<dyn App>, content_class: &[u16], key: &[u16], value: &[u16]) {
    PENDING.with(|c| {
        *c.borrow_mut() = Some(Pending {
            app,
            content_class: with_nul(content_class),
            placement_key: with_nul(key),
            placement_value: with_nul(value),
        })
    });
}

/// `SettingsWindow::Run`: the production classes and placement, the message
/// loop. `caption` has no NUL.
pub fn run(app: Box<dyn App>, instance: Handle, n_cmd_show: i32, mode: i32, caption: &[u16], icon_id: i32) -> i32 {
    let key: Vec<u16> = PLACEMENT_KEY.encode_utf16().collect();
    let value: Vec<u16> = PLACEMENT_VALUE.encode_utf16().collect();
    let content: Vec<u16> = CONTENT_CLASS.encode_utf16().collect();
    set_pending(app, &content, &key, &value);
    let main_class = wide(MAIN_CLASS);
    let caption = with_nul(caption);
    let key_z = with_nul(&key);
    let value_z = with_nul(&value);
    let start = Start { instance, mode };
    let p = RunParams {
        instance,
        n_cmd_show,
        main_class: main_class.as_ptr(),
        main_proc: main_wnd_proc as *const c_void,
        icon_id,
        caption: caption.as_ptr(),
        placement_key: key_z.as_ptr(),
        placement_value: value_z.as_ptr(),
        started: Some(started),
        user: &start as *const Start as *mut c_void,
    };
    let r = shell::run(&p);
    PENDING.with(|c| c.borrow_mut().take());
    r
}

/// `SettingsWindow::CreateForTest`: `shell::run` without the placement,
/// `ShowWindow` and the message loop. Strings have no NUL.
#[allow(clippy::too_many_arguments)]
pub fn create(
    app: Box<dyn App>,
    instance: Handle,
    mode: i32,
    caption: &[u16],
    main_class: &[u16],
    content_class: &[u16],
    placement_key: &[u16],
    placement_value: &[u16],
    icon_id: i32,
) -> Handle {
    set_pending(app, content_class, placement_key, placement_value);
    let main_z = with_nul(main_class);
    let caption_z = with_nul(caption);
    shell::register_main_class(instance, main_z.as_ptr(), main_wnd_proc as *const c_void, icon_id);
    let mut dpi = 96u32;
    // SAFETY: plain GDI / user32 calls.
    let hwnd = unsafe {
        let hdc = GetDC(null_mut());
        if !hdc.is_null() {
            dpi = GetDeviceCaps(hdc, LOGPIXELSY) as u32;
            ReleaseDC(null_mut(), hdc);
        }
        let (cx, cy) = frame::initial_size(dpi);
        CreateWindowExW(
            0,
            main_z.as_ptr(),
            caption_z.as_ptr(),
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            cx,
            cy,
            null_mut(),
            null_mut(),
            instance,
            null_mut(),
        )
    };
    PENDING.with(|c| c.borrow_mut().take());
    if hwnd.is_null() {
        return null_mut();
    }
    let start = Start { instance, mode };
    // SAFETY: `start` outlives the call.
    unsafe { started(&start as *const Start as *mut c_void, hwnd) };
    hwnd
}

/// `SettingsWindow::StateForTest`.
pub fn state(hwnd: Handle) -> Option<(DriverState, SettingsSnapshot)> {
    let wd = get_data(hwnd);
    if wd.is_null() {
        return None;
    }
    // SAFETY: live data copied out.
    Some(unsafe { ((*wd).state, (*wd).snapshot) })
}

/// `SettingsWindow::NavigateForTest`.
pub fn navigate(hwnd: Handle, mode: i32, selected_mode_index: i32, nav_level: i32, card_index: i32) {
    let wd = get_data(hwnd);
    if wd.is_null() {
        return;
    }
    switch_mode(hwnd, wd, mode);
    // SAFETY: live data; no call inside.
    unsafe {
        let n = &mut (*wd).state.nav;
        n.selected_mode_index = selected_mode_index;
        n.nav_level = nav_level;
        n.current_card_index = card_index;
        n.scroll_pos = 0;
    }
    rebuild(hwnd, wd);
    // SAFETY: plain user32 call.
    unsafe { InvalidateRect(hwnd, null(), 0) };
}

/// `SettingsWindow::PaintForTest`: the main window, or with `content` the
/// content area `hwnd`, painted into `target`.
pub fn paint_into(hwnd: Handle, target: Handle, w: i32, h: i32, content: bool) {
    let hmain = if content {
        // SAFETY: plain user32 call.
        unsafe { GetParent(hwnd) }
    } else {
        hwnd
    };
    let host = ProcHostImpl {
        hwnd,
        hmain,
        wd: get_data(hwnd),
    };
    let src = host.source();
    if content {
        shell::paint_content_into(target, w, h, &src);
    } else {
        shell::paint_main_into(target, w, h, &src);
    }
}
