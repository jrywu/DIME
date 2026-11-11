//! Live validation of the 自建詞庫 (custom table) RichEdit (phase 5b-8d,
//! Windows only).
//!
//! Port of `SettingsEditor::Cpp` (`src/DIMESettings/SettingsEditor.cpp`):
//! the visible line range, the foreground viewport pass that validates the
//! visible lines and colours them through TOM (`ITextRange::GetFont` /
//! `SetForeColor`, so the caret never moves), and the editor's subclass
//! procedure. Messages use raw bindings; TOM uses the `windows` crate.

#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::null_mut;

use dime_config::settings_model::{validate_line, LineError};
use dime_config::ImeMode;
use windows::core::{IUnknown, Interface};
use windows::Win32::UI::Controls::RichEdit::ITextDocument;

pub type Handle = *mut c_void;

#[repr(C)]
#[derive(Default)]
struct Rect {
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
struct Point {
    x: i32,
    y: i32,
}

#[repr(C)]
#[derive(Default)]
struct CharRange {
    cp_min: i32,
    cp_max: i32,
}

#[repr(C)]
struct TextRange {
    chrg: CharRange,
    text: *mut u16,
}

type SubclassProc = unsafe extern "system" fn(Handle, u32, usize, isize, usize, usize) -> isize;

windows_link::link!("user32.dll" "system" fn SendMessageW(h: Handle, m: u32, w: usize, l: isize) -> isize);
windows_link::link!("user32.dll" "system" fn PostMessageW(h: Handle, m: u32, w: usize, l: isize) -> i32);
windows_link::link!("user32.dll" "system" fn GetClientRect(h: Handle, rc: *mut Rect) -> i32);
windows_link::link!("user32.dll" "system" fn GetWindowRect(h: Handle, rc: *mut Rect) -> i32);
windows_link::link!("user32.dll" "system" fn GetParent(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn GetCursorPos(pt: *mut Point) -> i32);
windows_link::link!("user32.dll" "system" fn PtInRect(rc: *const Rect, pt: Point) -> i32);
windows_link::link!("comctl32.dll" "system" fn DefSubclassProc(h: Handle, m: u32, w: usize, l: isize) -> isize);
windows_link::link!("comctl32.dll" "system" fn RemoveWindowSubclass(h: Handle, proc_: SubclassProc, id: usize) -> i32);

const WM_USER: u32 = 0x0400;
const WM_SETFOCUS: u32 = 0x0007;
const WM_NCDESTROY: u32 = 0x0082;
const WM_KEYDOWN: u32 = 0x0100;
const WM_VSCROLL: u32 = 0x0115;
const WM_MOUSEWHEEL: u32 = 0x020A;
const WM_PASTE: u32 = 0x0302;
const EM_SCROLLCARET: u32 = 0x00B7;
const EM_GETLINECOUNT: u32 = 0x00BA;
const EM_LINEINDEX: u32 = 0x00BB;
const EM_LINELENGTH: u32 = 0x00C1;
const EM_LINEFROMCHAR: u32 = 0x00C9;
const EM_GETFIRSTVISIBLELINE: u32 = 0x00CE;
const EM_CHARFROMPOS: u32 = 0x00D7;
const EM_EXGETSEL: u32 = WM_USER + 52;
const EM_EXSETSEL: u32 = WM_USER + 55;
const EM_GETEVENTMASK: u32 = WM_USER + 59;
const EM_GETOLEINTERFACE: u32 = WM_USER + 60;
const EM_SETEVENTMASK: u32 = WM_USER + 69;
const EM_GETTEXTRANGE: u32 = WM_USER + 75;
const VK_BACK: usize = 0x08;
const VK_RETURN: usize = 0x0D;
const VK_SPACE: usize = 0x20;
const VK_DELETE: usize = 0x2E;

/// `SettingsEditor::kMsgRevalidate` / `kMsgViewport`.
pub const MSG_REVALIDATE: u32 = WM_USER + 1;
pub const MSG_VIEWPORT: u32 = WM_USER + 3;

/// `CUSTOM_TABLE_LARGE_DELETION_THRESHOLD` (Define.h).
const LARGE_DELETION_THRESHOLD: i32 = 10;

const fn rgb(r: u32, g: u32, b: u32) -> u32 {
    r | (g << 8) | (b << 16)
}

/// Define.h `CUSTOM_TABLE_{LIGHT,DARK}_*`: format, length, char, valid.
const LIGHT: [u32; 4] = [rgb(180, 0, 180), rgb(200, 100, 0), rgb(210, 0, 0), rgb(0, 0, 0)];
const DARK: [u32; 4] = [
    rgb(220, 100, 220),
    rgb(255, 180, 0),
    rgb(255, 100, 100),
    rgb(255, 255, 255),
];

/// Per-character key check (`ValidateCompositionKeyCharFull`), or `None`.
pub type CharOk<'a> = Option<&'a mut dyn FnMut(u16) -> bool>;

/// What `ValidateLine` needs for the page.
pub struct EditorContext<'a> {
    pub mode: ImeMode,
    pub max_codes: u32,
    pub is_dark: bool,
    pub char_ok: CharOk<'a>,
}

fn send(h: Handle, m: u32, w: usize, l: isize) -> isize {
    // SAFETY: plain user32 call; `l` points at a live local where the message needs one.
    unsafe { SendMessageW(h, m, w, l) }
}

/// `ViewportLineRange`: inclusive visible lines, 0, 0 for an empty editor.
pub fn viewport_line_range(hre: Handle) -> (i32, i32) {
    let first = send(hre, EM_GETFIRSTVISIBLELINE, 0, 0) as i32;
    let mut rc = Rect::default();
    // SAFETY: plain user32 call into a local.
    unsafe { GetClientRect(hre, &mut rc) };
    let pt = Point {
        x: rc.right - 1,
        y: rc.bottom - 1,
    }; // POINTL
    let last_char = send(hre, EM_CHARFROMPOS, 0, &pt as *const Point as isize);
    let mut last = if last_char >= 0 {
        send(hre, EM_LINEFROMCHAR, last_char as usize, 0) as i32
    } else {
        first
    };
    let line_count = send(hre, EM_GETLINECOUNT, 0, 0) as i32;
    if last >= line_count {
        last = line_count - 1;
    }
    if last < first {
        last = first;
    }
    (first, last)
}

/// `GetRichEditTextDocument`: the editor's TOM document, or `None`.
fn text_document(hre: Handle) -> Option<ITextDocument> {
    let mut unk: *mut c_void = null_mut();
    send(hre, EM_GETOLEINTERFACE, 0, &mut unk as *mut *mut c_void as isize);
    if unk.is_null() {
        return None;
    }
    // SAFETY: EM_GETOLEINTERFACE returned an AddRef'd IUnknown we now own.
    let unk = unsafe { IUnknown::from_raw(unk) };
    unk.cast::<ITextDocument>().ok()
}

/// `PaintRangeColor`: foreground colour over `[cp_min, cp_max)` via TOM.
fn paint_range(doc: &ITextDocument, cp_min: i32, cp_max: i32, color: u32) {
    if cp_max <= cp_min {
        return;
    }
    // SAFETY: COM calls on a live document; failures are ignored like C++.
    unsafe {
        if let Ok(range) = doc.Range(cp_min, cp_max) {
            if let Ok(font) = range.GetFont() {
                let _ = font.SetForeColor(color as i32);
            }
        }
    }
}

/// `ValidateOneLine`: valid colour over the line, then the error colours.
fn validate_one_line(hre: Handle, doc: &ITextDocument, li: i32, ctx: &mut EditorContext) {
    let start = send(hre, EM_LINEINDEX, li as usize, 0) as i32;
    if start < 0 {
        return;
    }
    let len = send(hre, EM_LINELENGTH, start as usize, 0) as i32;
    if len <= 0 {
        return;
    }
    let mut buf = vec![0u16; len as usize + 1];
    let mut tr = TextRange {
        chrg: CharRange {
            cp_min: start,
            cp_max: start + len,
        },
        text: buf.as_mut_ptr(),
    };
    if send(hre, EM_GETTEXTRANGE, 0, &mut tr as *mut TextRange as isize) <= 0 {
        return;
    }
    let check = ctx.char_ok.as_mut().map(|f| &mut **f as &mut dyn FnMut(u16) -> bool);
    let errors = validate_line(&buf[..len as usize], ctx.mode, ctx.max_codes, check);
    let colors = if ctx.is_dark { &DARK } else { &LIGHT };
    paint_range(doc, start, start + len, colors[3]);
    for e in &errors {
        let color = match e.error {
            LineError::Format => colors[0],
            LineError::KeyTooLong => colors[1],
            LineError::InvalidChar => colors[2],
        };
        paint_range(doc, start + e.start, start + e.start + e.len, color);
    }
}

/// `ValidateViewport`: validate and colour the visible lines, with EN_CHANGE
/// muted (CHARFORMAT changes can fire it and recurse).
pub fn validate_viewport(hre: Handle, ctx: &mut EditorContext) {
    if hre.is_null() {
        return;
    }
    let (first, last) = viewport_line_range(hre);
    let line_count = send(hre, EM_GETLINECOUNT, 0, 0) as i32;
    if line_count <= 0 {
        return;
    }
    let Some(doc) = text_document(hre) else {
        return;
    };
    let prev_mask = send(hre, EM_GETEVENTMASK, 0, 0);
    send(hre, EM_SETEVENTMASK, 0, 0);
    let mut li = first;
    while li <= last && li < line_count {
        validate_one_line(hre, &doc, li, ctx);
        li += 1;
    }
    send(hre, EM_SETEVENTMASK, 0, prev_mask);
}

/// `SettingsEditor::Cpp::SubclassProc`.
///
/// # Safety
///
/// Install only with `SetWindowSubclass` on a RichEdit whose parent is the
/// content area; comctl32 calls it with that window's messages.
pub unsafe extern "system" fn subclass_proc(h: Handle, msg: u32, w: usize, l: isize, id: usize, _ref: usize) -> isize {
    // SAFETY: called by comctl32 for the subclassed RichEdit; plain user32 calls.
    unsafe {
        match msg {
            WM_SETFOCUS => {
                let cr = CharRange::default();
                send(h, EM_EXSETSEL, 0, &cr as *const CharRange as isize);
                send(h, EM_SCROLLCARET, 0, 0);
            }
            WM_KEYDOWN => {
                if w == VK_RETURN || w == VK_SPACE {
                    PostMessageW(GetParent(h), MSG_REVALIDATE, 0, 0);
                } else if w == VK_DELETE || w == VK_BACK {
                    let mut sel = CharRange::default();
                    send(h, EM_EXGETSEL, 0, &mut sel as *mut CharRange as isize);
                    if sel.cp_max - sel.cp_min > LARGE_DELETION_THRESHOLD {
                        PostMessageW(GetParent(h), MSG_REVALIDATE, 0, 0);
                    }
                }
            }
            WM_PASTE => {
                PostMessageW(GetParent(h), MSG_REVALIDATE, 0, 0);
            }
            WM_MOUSEWHEEL => {
                let mut pt = Point::default();
                GetCursorPos(&mut pt);
                let mut rc = Rect::default();
                GetWindowRect(h, &mut rc);
                if PtInRect(&rc, pt) == 0 {
                    return SendMessageW(GetParent(h), msg, w, l);
                }
                PostMessageW(GetParent(h), MSG_VIEWPORT, 0, 0);
            }
            WM_VSCROLL => {
                PostMessageW(GetParent(h), MSG_VIEWPORT, 0, 0);
            }
            WM_NCDESTROY => {
                RemoveWindowSubclass(h, subclass_proc, id);
            }
            _ => {}
        }
        DefSubclassProc(h, msg, w, l)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colors_match_define_h() {
        assert_eq!(LIGHT[0], 0x00B4_00B4);
        assert_eq!(DARK[3], 0x00FF_FFFF);
        assert_eq!(
            std::mem::size_of::<TextRange>(),
            if cfg!(target_pointer_width = "64") { 16 } else { 12 }
        );
    }
}
