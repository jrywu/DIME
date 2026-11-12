//! The Win32 side of the settings command host (phase 5b-8k).
//!
//! Port of `SettingsWinHost::Cpp` (`src/DIMESettings/SettingsWinHost.cpp`,
//! formerly the window-facing methods of the `WindowHost` class in
//! `SettingsWindow::HandleCommand`): the font, colour, open and save
//! dialogs, writing a UTF-16 file with a BOM, the Documents folder, the
//! screen dpi, and the custom table editor's text, line index, and caret.
//! `CConfig`, the table parser, and the viewport validation stay with the
//! caller.

// HWND / HDC are opaque handles passed to Win32, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::{null, null_mut};
use std::sync::Mutex;

use crate::command::LogFont;
use crate::paint::Handle;

pub const MAX_PATH: usize = 260;

#[repr(C)]
struct ChooseFontW {
    struct_size: u32,
    owner: Handle,
    hdc: Handle,
    log_font: *mut LogFont,
    point_size: i32,
    flags: u32,
    rgb_colors: u32,
    cust_data: isize,
    hook: *const c_void,
    template_name: *const u16,
    instance: Handle,
    style: *mut u16,
    font_type: u16,
    missing_alignment: u16,
    size_min: i32,
    size_max: i32,
}

#[repr(C)]
struct ChooseColorW {
    struct_size: u32,
    owner: Handle,
    instance: Handle,
    rgb_result: u32,
    cust_colors: *mut u32,
    flags: u32,
    cust_data: isize,
    hook: *const c_void,
    template_name: *const u16,
}

#[repr(C)]
struct OpenFileNameW {
    struct_size: u32,
    owner: Handle,
    instance: Handle,
    filter: *const u16,
    custom_filter: *mut u16,
    max_cust_filter: u32,
    filter_index: u32,
    file: *mut u16,
    max_file: u32,
    file_title: *mut u16,
    max_file_title: u32,
    initial_dir: *const u16,
    title: *const u16,
    flags: u32,
    file_offset: u16,
    file_extension: u16,
    def_ext: *const u16,
    cust_data: isize,
    hook: *const c_void,
    template_name: *const u16,
    reserved_ptr: *mut c_void,
    reserved: u32,
    flags_ex: u32,
}

windows_link::link!("comdlg32.dll" "system" fn ChooseFontW(cf: *mut ChooseFontW) -> i32);
windows_link::link!("comdlg32.dll" "system" fn ChooseColorW(cc: *mut ChooseColorW) -> i32);
windows_link::link!("comdlg32.dll" "system" fn GetOpenFileNameW(ofn: *mut OpenFileNameW) -> i32);
windows_link::link!("comdlg32.dll" "system" fn GetSaveFileNameW(ofn: *mut OpenFileNameW) -> i32);
windows_link::link!("kernel32.dll" "system" fn CreateFileW(name: *const u16, access: u32, share: u32, sa: *const c_void, disposition: u32, flags: u32, template: Handle) -> Handle);
windows_link::link!("kernel32.dll" "system" fn WriteFile(file: Handle, buf: *const c_void, len: u32, written: *mut u32, ov: *mut c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn CloseHandle(h: Handle) -> i32);
windows_link::link!("shell32.dll" "system" fn SHGetSpecialFolderPathW(owner: Handle, path: *mut u16, csidl: i32, create: i32) -> i32);
windows_link::link!("user32.dll" "system" fn GetDC(wnd: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(wnd: Handle, hdc: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetDeviceCaps(hdc: Handle, index: i32) -> i32);
windows_link::link!("user32.dll" "system" fn GetWindowTextLengthW(wnd: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn GetWindowTextW(wnd: Handle, buf: *mut u16, cap: i32) -> i32);
windows_link::link!("user32.dll" "system" fn SetWindowTextW(wnd: Handle, text: *const u16) -> i32);
windows_link::link!("user32.dll" "system" fn SendMessageW(wnd: Handle, msg: u32, w: usize, l: isize) -> isize);
windows_link::link!("user32.dll" "system" fn SetFocus(wnd: Handle) -> Handle);

const CF_INITTOLOGFONTSTRUCT: u32 = 0x40;
const CF_NOVERTFONTS: u32 = 0x0100_0000;
const CC_RGBINIT: u32 = 0x1;
const CC_FULLOPEN: u32 = 0x2;
const OFN_OVERWRITEPROMPT: u32 = 0x2;
const OFN_FILEMUSTEXIST: u32 = 0x1000;
const GENERIC_WRITE: u32 = 0x4000_0000;
const FILE_SHARE_READ: u32 = 0x1;
const CREATE_ALWAYS: u32 = 2;
const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
const INVALID_HANDLE_VALUE: isize = -1;
const CSIDL_PERSONAL: i32 = 5;
const LOGPIXELSY: i32 = 90;
const EM_GETEVENTMASK: u32 = 0x043B;
const EM_SETEVENTMASK: u32 = 0x0445;
const EM_GETLINECOUNT: u32 = 0x00BA;
const EM_LINEINDEX: u32 = 0x00BB;
const EM_SETSEL: u32 = 0x00B1;
const EM_SCROLLCARET: u32 = 0x00B7;

/// `ChooseColor` custom colours, kept for the process like the C++ static.
static CUST_COLORS: Mutex<[u32; 16]> = Mutex::new([0; 16]);

fn until_nul(buf: &[u16]) -> Vec<u16> {
    let n = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    buf[..n].to_vec()
}

/// `PickFont`: the font dialog, starting from `lf`. On OK, `lf` and the
/// size in tenths of a point are updated.
pub fn pick_font(owner: Handle, lf: &mut LogFont, point_size10: &mut i32) -> bool {
    let mut cf = ChooseFontW {
        struct_size: std::mem::size_of::<ChooseFontW>() as u32,
        owner,
        hdc: null_mut(),
        log_font: lf,
        point_size: 0,
        flags: CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS,
        rgb_colors: 0,
        cust_data: 0,
        hook: null(),
        template_name: null(),
        instance: null_mut(),
        style: null_mut(),
        font_type: 0,
        missing_alignment: 0,
        size_min: 0,
        size_max: 0,
    };
    // SAFETY: cf is fully initialised and lf outlives the call.
    if unsafe { ChooseFontW(&mut cf) } != 0 {
        *point_size10 = cf.point_size;
        return true;
    }
    false
}

/// `PickColor`: the colour dialog, starting from `rgb`.
pub fn pick_color(owner: Handle, rgb: &mut u32) -> bool {
    let mut cust = CUST_COLORS.lock().unwrap_or_else(|e| e.into_inner());
    let mut cc = ChooseColorW {
        struct_size: std::mem::size_of::<ChooseColorW>() as u32,
        owner,
        instance: null_mut(),
        rgb_result: *rgb,
        cust_colors: cust.as_mut_ptr(),
        flags: CC_FULLOPEN | CC_RGBINIT,
        cust_data: 0,
        hook: null(),
        template_name: null(),
    };
    // SAFETY: cc is fully initialised; the custom colours live in the lock.
    if unsafe { ChooseColorW(&mut cc) } != 0 {
        *rgb = cc.rgb_result;
        return true;
    }
    false
}

fn ofn(owner: Handle, file: &mut [u16; MAX_PATH], filter: &[u16], flags: u32) -> OpenFileNameW {
    // SAFETY: OPENFILENAMEW is plain data; all-zero is its empty value.
    let mut o: OpenFileNameW = unsafe { std::mem::zeroed() };
    o.struct_size = std::mem::size_of::<OpenFileNameW>() as u32;
    o.owner = owner;
    o.file = file.as_mut_ptr();
    o.max_file = MAX_PATH as u32;
    o.flags = flags;
    o.filter = filter.as_ptr();
    o
}

/// `OpenFileDialog`: filter 0 is CIN / TXT tables, otherwise phrase text files.
pub fn open_file_dialog(owner: Handle, filter: i32) -> Option<Vec<u16>> {
    let f: Vec<u16> = if filter == 0 {
        "CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0".encode_utf16().collect()
    } else {
        "詞庫文字檔(*.txt)\0*.txt\0\0".encode_utf16().collect()
    };
    let mut file = [0u16; MAX_PATH];
    let mut o = ofn(owner, &mut file, &f, OFN_FILEMUSTEXIST);
    // SAFETY: o points at live buffers for the whole call.
    let ok = unsafe { GetOpenFileNameW(&mut o) } != 0;
    ok.then(|| until_nul(&file))
}

/// `SaveFileDialog`: starts from `initial` (cut to `MAX_PATH - 1` units like
/// `StringCchCopyW`).
pub fn save_file_dialog(owner: Handle, initial: &[u16]) -> Option<Vec<u16>> {
    let f: Vec<u16> = "詞庫文字檔(*.txt)\0*.txt\0\0".encode_utf16().collect();
    let mut file = [0u16; MAX_PATH];
    let src = until_nul(initial);
    let n = src.len().min(MAX_PATH - 1);
    file[..n].copy_from_slice(&src[..n]);
    let mut o = ofn(owner, &mut file, &f, OFN_OVERWRITEPROMPT);
    // SAFETY: o points at live buffers for the whole call.
    let ok = unsafe { GetSaveFileNameW(&mut o) } != 0;
    ok.then(|| until_nul(&file))
}

/// `WriteBomFile`: UTF-16 LE with a BOM; false only when the file can't be
/// created (write errors are not checked, like the C++).
pub fn write_bom_file(path: &[u16], text: &[u16]) -> bool {
    let mut p = until_nul(path);
    p.push(0);
    // SAFETY: p is NUL-terminated; the buffers outlive the calls.
    unsafe {
        let h = CreateFileW(
            p.as_ptr(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            null(),
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            null_mut(),
        );
        if h as isize == INVALID_HANDLE_VALUE {
            return false;
        }
        let mut written = 0u32;
        let bom: u16 = 0xFEFF;
        WriteFile(h, (&bom as *const u16).cast(), 2, &mut written, null_mut());
        WriteFile(
            h,
            text.as_ptr().cast(),
            (text.len() * 2) as u32,
            &mut written,
            null_mut(),
        );
        CloseHandle(h);
    }
    true
}

/// `DocumentsPath`: CSIDL_PERSONAL (empty when it fails).
pub fn documents_path() -> Vec<u16> {
    let mut buf = [0u16; MAX_PATH];
    // SAFETY: buf has MAX_PATH units.
    unsafe { SHGetSpecialFolderPathW(null_mut(), buf.as_mut_ptr(), CSIDL_PERSONAL, 0) };
    until_nul(&buf)
}

/// `LogPixelsY` of the window's DC.
pub fn log_pixels_y(wnd: Handle) -> i32 {
    // SAFETY: plain GDI calls.
    unsafe {
        let dc = GetDC(wnd);
        let v = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(wnd, dc);
        v
    }
}

/// `EditorText`: `GetWindowTextLength` units (anything past what
/// `GetWindowText` copied stays NUL, like the C++ `resize`).
pub fn editor_text(re: Handle) -> Vec<u16> {
    // SAFETY: the buffer has len + 1 units.
    unsafe {
        let len = GetWindowTextLengthW(re).max(0) as usize;
        let mut buf = vec![0u16; len + 1];
        GetWindowTextW(re, buf.as_mut_ptr(), (len + 1) as i32);
        buf.truncate(len);
        buf
    }
}

/// `SetEditorTextMuted`: set the text with the event mask off (no
/// EN_CHANGE), then restore it. Returns the new line count.
pub fn set_editor_text_muted(re: Handle, text: &[u16]) -> i32 {
    let mut t = until_nul(text);
    t.push(0);
    // SAFETY: plain messages; t is NUL-terminated.
    unsafe {
        let prev = SendMessageW(re, EM_GETEVENTMASK, 0, 0);
        SendMessageW(re, EM_SETEVENTMASK, 0, 0);
        SetWindowTextW(re, t.as_ptr());
        let lines = SendMessageW(re, EM_GETLINECOUNT, 0, 0) as i32;
        SendMessageW(re, EM_SETEVENTMASK, 0, prev);
        lines
    }
}

/// `EditorLineIndex`: `EM_LINEINDEX` as a 32-bit value.
pub fn editor_line_index(re: Handle, line: i32) -> i32 {
    // SAFETY: plain message.
    unsafe { SendMessageW(re, EM_LINEINDEX, line as isize as usize, 0) as i32 }
}

/// `EditorFocusAt` without the viewport pass: focus first (the subclass's
/// WM_SETFOCUS resets the selection), then the caret, then scroll to it.
pub fn editor_focus_at(re: Handle, cp: i32) {
    // SAFETY: plain messages.
    unsafe {
        SetFocus(re);
        SendMessageW(re, EM_SETSEL, cp as isize as usize, cp as isize);
        SendMessageW(re, EM_SCROLLCARET, 0, 0);
    }
}

/// `EM_GETLINECOUNT`.
pub fn editor_line_count(re: Handle) -> i32 {
    // SAFETY: plain message.
    unsafe { SendMessageW(re, EM_GETLINECOUNT, 0, 0) as i32 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn struct_sizes_match_windows() {
        #[cfg(target_pointer_width = "64")]
        {
            assert_eq!(std::mem::size_of::<ChooseFontW>(), 104);
            assert_eq!(std::mem::size_of::<ChooseColorW>(), 72);
            assert_eq!(std::mem::size_of::<OpenFileNameW>(), 152);
        }
        #[cfg(target_pointer_width = "32")]
        {
            assert_eq!(std::mem::size_of::<ChooseFontW>(), 60);
            assert_eq!(std::mem::size_of::<ChooseColorW>(), 36);
            assert_eq!(std::mem::size_of::<OpenFileNameW>(), 88);
        }
    }
}
