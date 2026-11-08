//! Window infrastructure owned by Rust (phase 3c-5a; design in
//! `docs/RUST_WINDOW_OWNERSHIP.md`): window class registration, creation
//! with the DPI-awareness switch of `CBaseWindow::_Create`, and the message
//! dispatch of `CBaseWindow::_WindowProc`.
//!
//! A window is a [`Window`] (`Rc`) holding its [`Handler`].  `GWLP_USERDATA`
//! points to a boxed `Rc<Window>` from `WM_CREATE` (as `CBaseWindow` sets it)
//! until `WM_NCDESTROY`, where it is released.  Every message call holds its
//! own `Rc`, so a handler that destroys its window (or whose owner drops the
//! last outside reference) during a message keeps running on a live object.
//!
//! Dispatch, as `CBaseWindow::_WindowProc`:
//! - before `WM_CREATE` (no object yet): `DefWindowProc`;
//! - `WM_TIMER`: always to [`Handler::timer`], result 0, never to
//!   [`Handler::message`];
//! - a handler that does not dispatch (the shadow window, whose `_pUIWnd` is
//!   never set): `S_FALSE` for every other message, without `DefWindowProc`;
//! - otherwise [`Handler::message`], `None` meaning `DefWindowProc`.

use std::cell::Cell;
use std::rc::Rc;
pub use windows::core::PCWSTR;
use windows::core::{s, w};
pub use windows::Win32::Foundation::HINSTANCE;
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::{GetStockObject, HBRUSH, LTGRAY_BRUSH};
use windows::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, DestroyWindow, IsWindow, LoadCursorW, RegisterClassExW, UnregisterClassW,
    CREATESTRUCTW, CS_DBLCLKS, CS_HREDRAW, CS_IME, CS_VREDRAW, GWLP_USERDATA, HMENU, IDC_ARROW, WINDOW_EX_STYLE,
    WINDOW_STYLE, WM_CREATE, WM_NCDESTROY, WM_TIMER, WNDCLASSEXW,
};

/// `S_FALSE`: what `CBaseWindow::_WindowProc` returns for an object without a UI window.
pub const S_FALSE_RESULT: LRESULT = LRESULT(1);

/// A window's message handling.
pub trait Handler {
    /// A message after `WM_CREATE` other than `WM_TIMER`; `None`: `DefWindowProc`.
    fn message(&self, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> Option<LRESULT>;
    /// `WM_TIMER` (`CBaseWindow` routes it to the timer object, never to the window procedure).
    fn timer(&self, _hwnd: HWND, _id: usize) {}
    /// `false` for a window without a UI window (`_pUIWnd` never set): every
    /// message after `WM_CREATE` gets `S_FALSE`.
    fn dispatches(&self) -> bool {
        true
    }
}

/// A window owned by Rust.
pub struct Window {
    hwnd: Cell<HWND>,
    /// The window procedure took the cell at `WM_CREATE` (and frees it at `WM_NCDESTROY`).
    adopted: Cell<bool>,
    handler: Box<dyn Handler>,
}

impl Window {
    /// The window, or a null `HWND` once it was destroyed (or never created).
    pub fn hwnd(&self) -> HWND {
        self.hwnd.get()
    }

    pub fn is_alive(&self) -> bool {
        !self.hwnd.get().is_invalid()
    }

    pub fn handler(&self) -> &dyn Handler {
        self.handler.as_ref()
    }

    /// `CBaseWindow::_Destroy`: destroy the window if it still exists.
    pub fn destroy(&self) {
        let h = self.hwnd.get();
        if !h.is_invalid() {
            unsafe {
                let _ = DestroyWindow(h);
            }
            self.hwnd.set(HWND::default());
        }
    }
}

/// `CBaseWindow::_Show` for a real window: show or hide without moving,
/// sizing, activating or changing the z-order.
pub fn base_show(hwnd: HWND, on: bool) {
    use windows::Win32::UI::WindowsAndMessaging::{
        SetWindowPos, SWP_HIDEWINDOW, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE, SWP_NOZORDER, SWP_SHOWWINDOW,
    };
    if hwnd.is_invalid() {
        return;
    }
    let flags =
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | if on { SWP_SHOWWINDOW } else { SWP_HIDEWINDOW };
    unsafe {
        let _ = SetWindowPos(hwnd, None, 0, 0, 0, 0, flags);
    }
}

/// The class styles of `CBaseWindow::_InitWindowClass` (child / shadow
/// windows: with `CS_IME`) and `_InitPopupWindowClass` (candidate / notify:
/// without `CS_IME`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClassKind {
    Child,
    Popup,
}

/// Registers a window class (`name` NUL-terminated) with the Rust window
/// procedure; 0 on failure (e.g. already registered).
///
/// # Safety
/// `name` must be a NUL-terminated UTF-16 string; `hinstance` the module.
pub unsafe fn register_class(hinstance: HINSTANCE, name: PCWSTR, kind: ClassKind) -> u16 {
    let style = match kind {
        ClassKind::Child => CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW | CS_IME,
        ClassKind::Popup => CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
    };
    let wc = WNDCLASSEXW {
        cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
        style,
        lpfnWndProc: Some(wnd_proc),
        cbClsExtra: 0,
        cbWndExtra: 0,
        hInstance: hinstance,
        hCursor: unsafe { LoadCursorW(None, IDC_ARROW) }.unwrap_or_default(),
        hbrBackground: HBRUSH(unsafe { GetStockObject(LTGRAY_BRUSH) }.0),
        lpszClassName: name,
        ..Default::default()
    };
    unsafe { RegisterClassExW(&wc) }
}

/// `UnregisterClass` (`CBaseWindow::_UninitWindowClass`).
///
/// # Safety
/// `hinstance` must be the module that registered `atom`.
pub unsafe fn unregister_class(hinstance: HINSTANCE, atom: u16) -> bool {
    if atom == 0 {
        return false;
    }
    unsafe { UnregisterClassW(PCWSTR(atom as usize as *const u16), Some(hinstance)) }.is_ok()
}

type SetThreadDpiAwarenessContext = unsafe extern "system" fn(isize) -> isize;
type GetWindowDpiAwarenessContext = unsafe extern "system" fn(HWND) -> isize;
type AreDpiAwarenessContextsEqual = unsafe extern "system" fn(isize, isize) -> i32;
/// `DPI_AWARENESS_CONTEXT_UNAWARE` / `_PER_MONITOR_AWARE_V2`.
const DPI_CONTEXT_UNAWARE: isize = -1;
const DPI_CONTEXT_PER_MONITOR_AWARE_V2: isize = -4;

/// The Windows 10 1607+ DPI functions, loaded dynamically as `CBaseWindow` does.
fn dpi_functions() -> (
    Option<SetThreadDpiAwarenessContext>,
    Option<GetWindowDpiAwarenessContext>,
    Option<AreDpiAwarenessContextsEqual>,
) {
    unsafe {
        let Ok(user32) = GetModuleHandleW(w!("user32.dll")) else {
            return (None, None, None);
        };
        let set = GetProcAddress(user32, s!("SetThreadDpiAwarenessContext"));
        let get = GetProcAddress(user32, s!("GetWindowDpiAwarenessContext"));
        let eq = GetProcAddress(user32, s!("AreDpiAwarenessContextsEqual"));
        (
            set.map(|f| std::mem::transmute::<_, SetThreadDpiAwarenessContext>(f)),
            get.map(|f| std::mem::transmute::<_, GetWindowDpiAwarenessContext>(f)),
            eq.map(|f| std::mem::transmute::<_, AreDpiAwarenessContextsEqual>(f)),
        )
    }
}

#[cfg(target_pointer_width = "64")]
unsafe fn get_userdata(hwnd: HWND) -> isize {
    unsafe { windows::Win32::UI::WindowsAndMessaging::GetWindowLongPtrW(hwnd, GWLP_USERDATA) }
}
#[cfg(target_pointer_width = "32")]
unsafe fn get_userdata(hwnd: HWND) -> isize {
    unsafe { windows::Win32::UI::WindowsAndMessaging::GetWindowLongW(hwnd, GWLP_USERDATA) as isize }
}
#[cfg(target_pointer_width = "64")]
unsafe fn set_userdata(hwnd: HWND, v: isize) {
    unsafe {
        windows::Win32::UI::WindowsAndMessaging::SetWindowLongPtrW(hwnd, GWLP_USERDATA, v);
    }
}
#[cfg(target_pointer_width = "32")]
unsafe fn set_userdata(hwnd: HWND, v: isize) {
    unsafe {
        windows::Win32::UI::WindowsAndMessaging::SetWindowLongW(hwnd, GWLP_USERDATA, v as i32);
    }
}

/// `CBaseWindow::_Create`: create the window at (0, 0) with the given size.
/// When the thread DPI context can be switched, the window is created
/// per-monitor aware (v2) unless its parent is DPI-unaware (DPI-unaware hosts
/// use virtualized coordinates; their caret positions would not match).
/// `None` when the window could not be created.
///
/// # Safety
/// `class` must be a registered class atom whose window procedure is
/// [`wnd_proc`]; `hinstance` the module that registered it.
#[allow(clippy::too_many_arguments)]
pub unsafe fn create(
    hinstance: HINSTANCE,
    class: u16,
    ex_style: WINDOW_EX_STYLE,
    style: WINDOW_STYLE,
    parent: Option<HWND>,
    width: i32,
    height: i32,
    handler: Box<dyn Handler>,
) -> Option<Rc<Window>> {
    let win = Rc::new(Window {
        hwnd: Cell::new(HWND::default()),
        adopted: Cell::new(false),
        handler,
    });
    // Owned by the window from WM_CREATE until WM_NCDESTROY.
    let cell: *mut Rc<Window> = Box::into_raw(Box::new(Rc::clone(&win)));

    let (set_ctx, get_ctx, eq_ctx) = dpi_functions();
    let mut prev_ctx = 0isize;
    if let Some(set_ctx) = set_ctx {
        let mut skip = false;
        if let (Some(p), Some(get_ctx), Some(eq_ctx)) = (parent, get_ctx, eq_ctx) {
            let parent_ctx = unsafe { get_ctx(p) };
            if parent_ctx != 0 && unsafe { eq_ctx(parent_ctx, DPI_CONTEXT_UNAWARE) } != 0 {
                skip = true;
            }
        }
        if !skip {
            prev_ctx = unsafe { set_ctx(DPI_CONTEXT_PER_MONITOR_AWARE_V2) };
        }
    }
    let hwnd = unsafe {
        CreateWindowExW(
            ex_style,
            PCWSTR(class as usize as *const u16),
            PCWSTR::null(),
            style,
            0,
            0,
            width,
            height,
            parent,
            None::<HMENU>,
            Some(hinstance),
            Some(cell as *const core::ffi::c_void),
        )
    };
    if prev_ctx != 0 {
        if let Some(set_ctx) = set_ctx {
            unsafe { set_ctx(prev_ctx) };
        }
    }
    match hwnd {
        Ok(h) if !h.is_invalid() && unsafe { get_userdata(h) } == cell as isize => {
            win.hwnd.set(h);
            Some(win)
        }
        other => {
            if let Ok(h) = other {
                if !h.is_invalid() && unsafe { IsWindow(Some(h)) }.as_bool() {
                    unsafe {
                        let _ = DestroyWindow(h);
                    }
                }
            }
            // Not adopted (creation failed before WM_CREATE): the cell is ours to free.
            // Adopted cells were freed at WM_NCDESTROY.
            if !win.adopted.get() {
                drop(unsafe { Box::from_raw(cell) });
            }
            None
        }
    }
}

/// The window procedure of every class registered by [`register_class`].
///
/// # Safety
/// Called by Windows.
pub unsafe extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if msg == WM_CREATE && lparam.0 != 0 {
        let cs = lparam.0 as *const CREATESTRUCTW;
        let cell = unsafe { (*cs).lpCreateParams } as isize;
        if cell != 0 {
            unsafe { set_userdata(hwnd, cell) };
            let adopted: &Rc<Window> = unsafe { &*(cell as *const Rc<Window>) };
            adopted.adopted.set(true);
        }
    }
    let cell = unsafe { get_userdata(hwnd) } as *const Rc<Window>;
    if hwnd.is_invalid() || cell.is_null() {
        return unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) };
    }
    // Our own reference for the duration of the call.
    let win: Rc<Window> = Rc::clone(unsafe { &*cell });
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        dispatch(&win, hwnd, msg, wparam, lparam)
    }))
    .unwrap_or(LRESULT(0));
    if msg == WM_NCDESTROY {
        // The window is gone: release its reference (the cell box).
        unsafe { set_userdata(hwnd, 0) };
        win.hwnd.set(HWND::default());
        drop(unsafe { Box::from_raw(cell as *mut Rc<Window>) });
    }
    result
}

fn dispatch(win: &Window, hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if msg == WM_TIMER {
        win.handler.timer(hwnd, wparam.0);
        return LRESULT(0);
    }
    if !win.handler.dispatches() {
        return S_FALSE_RESULT;
    }
    match win.handler.message(hwnd, msg, wparam, lparam) {
        Some(r) => r,
        None => unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use windows::Win32::UI::WindowsAndMessaging::{SendMessageW, WM_APP, WS_POPUP};

    struct Probe {
        log: Rc<RefCell<Vec<String>>>,
        dispatches: bool,
        destroy_on: Option<u32>,
        dropped: Rc<Cell<bool>>,
    }

    impl Handler for Probe {
        fn message(&self, hwnd: HWND, msg: u32, wparam: WPARAM, _lparam: LPARAM) -> Option<LRESULT> {
            if msg >= WM_APP {
                self.log.borrow_mut().push(format!("msg {} {}", msg - WM_APP, wparam.0));
                if self.destroy_on == Some(msg) {
                    unsafe {
                        let _ = DestroyWindow(hwnd);
                    }
                    self.log.borrow_mut().push("destroyed inside".into());
                }
                return Some(LRESULT(42));
            }
            None
        }
        fn timer(&self, _hwnd: HWND, id: usize) {
            self.log.borrow_mut().push(format!("timer {id}"));
        }
        fn dispatches(&self) -> bool {
            self.dispatches
        }
    }

    impl Drop for Probe {
        fn drop(&mut self) {
            self.dropped.set(true);
        }
    }

    fn class(name: &str, kind: ClassKind) -> (HINSTANCE, u16) {
        let wide: Vec<u16> = name.encode_utf16().chain(Some(0)).collect();
        let hinst: HINSTANCE = unsafe { GetModuleHandleW(None) }.unwrap().into();
        let atom = unsafe { register_class(hinst, PCWSTR(wide.as_ptr()), kind) };
        assert_ne!(atom, 0, "register {name}");
        (hinst, atom)
    }

    type Log = Rc<RefCell<Vec<String>>>;

    fn probe(dispatches: bool, destroy_on: Option<u32>) -> (Box<Probe>, Log, Rc<Cell<bool>>) {
        let log = Rc::new(RefCell::new(Vec::new()));
        let dropped = Rc::new(Cell::new(false));
        (
            Box::new(Probe {
                log: log.clone(),
                dispatches,
                destroy_on,
                dropped: dropped.clone(),
            }),
            log,
            dropped,
        )
    }

    #[test]
    fn dispatch_timer_destroy_and_release() {
        let (hinst, atom) = class("DIME.Test.Win.Dispatch", ClassKind::Popup);
        let (p, log, dropped) = probe(true, None);
        let win = unsafe { create(hinst, atom, WINDOW_EX_STYLE(0), WS_POPUP, None, 10, 10, p) }.expect("create");
        let h = win.hwnd();
        assert!(win.is_alive());
        let r = unsafe { SendMessageW(h, WM_APP + 1, Some(WPARAM(7)), None) };
        assert_eq!(r.0, 42);
        let r = unsafe { SendMessageW(h, WM_TIMER, Some(WPARAM(39775)), None) };
        assert_eq!(r.0, 0);
        assert_eq!(*log.borrow(), vec!["msg 1 7".to_string(), "timer 39775".to_string()]);

        win.destroy();
        assert!(!win.is_alive());
        assert!(!dropped.get(), "the outside reference keeps the object");
        drop(win);
        assert!(
            dropped.get(),
            "released after the window and the last reference are gone"
        );
        assert!(unsafe { unregister_class(hinst, atom) });
    }

    #[test]
    fn non_dispatching_window_answers_s_false() {
        let (hinst, atom) = class("DIME.Test.Win.Shadow", ClassKind::Child);
        let (p, log, _) = probe(false, None);
        let win = unsafe { create(hinst, atom, WINDOW_EX_STYLE(0), WS_POPUP, None, 10, 10, p) }.expect("create");
        let r = unsafe { SendMessageW(win.hwnd(), WM_APP + 1, None, None) };
        assert_eq!(r, S_FALSE_RESULT);
        assert!(log.borrow().is_empty());
        // WM_TIMER is still routed (as CBaseWindow does before the UI-window check).
        unsafe { SendMessageW(win.hwnd(), WM_TIMER, Some(WPARAM(5)), None) };
        assert_eq!(*log.borrow(), vec!["timer 5".to_string()]);
        win.destroy();
        drop(win);
        assert!(unsafe { unregister_class(hinst, atom) });
    }

    #[test]
    fn destroyed_inside_its_own_message() {
        let (hinst, atom) = class("DIME.Test.Win.SelfDestroy", ClassKind::Popup);
        let (p, log, dropped) = probe(true, Some(WM_APP + 3));
        let win = unsafe { create(hinst, atom, WINDOW_EX_STYLE(0), WS_POPUP, None, 10, 10, p) }.expect("create");
        let h = win.hwnd();
        let r = unsafe { SendMessageW(h, WM_APP + 3, None, None) };
        assert_eq!(r.0, 42, "the handler finished on a live object");
        assert_eq!(
            *log.borrow(),
            vec!["msg 3 0".to_string(), "destroyed inside".to_string()]
        );
        assert!(!win.is_alive());
        assert!(!unsafe { IsWindow(Some(h)) }.as_bool());
        // Messages to the dead handle go nowhere; destroy() is a no-op.
        win.destroy();
        drop(win);
        assert!(dropped.get());
        assert!(unsafe { unregister_class(hinst, atom) });
    }

    #[test]
    fn registering_twice_fails_and_unregistering_frees_the_name() {
        let (hinst, atom) = class("DIME.Test.Win.Twice", ClassKind::Popup);
        let wide: Vec<u16> = "DIME.Test.Win.Twice".encode_utf16().chain(Some(0)).collect();
        assert_eq!(
            unsafe { register_class(hinst, PCWSTR(wide.as_ptr()), ClassKind::Popup) },
            0
        );
        assert!(unsafe { unregister_class(hinst, atom) });
        let again = unsafe { register_class(hinst, PCWSTR(wide.as_ptr()), ClassKind::Popup) };
        assert_ne!(again, 0);
        assert!(unsafe { unregister_class(hinst, again) });
    }
}
