//! UI of the DIME input method (migration phase 3).
//!
//! Phase 3a: the pure layout calculations of the C++ windows (`CBaseWindow`
//! placement around the composition, the notify-window stack of
//! `CUIPresenter`).  Phase 3b: the candidate window and scroll bar layout
//! (`candwnd`).  Phase 3c-1: the GDI drawing of the candidate window
//! (`candpaint`, Windows only).  Phase 3c-2: its message handling
//! (`candproc`, Windows only).  Phase 3c-3a: the scroll bar drawing
//! (`sbpaint`, Windows only).  Phase 3c-3b: the scroll bar events
//! (`sbproc`, Windows only).  Phase 3c-4: the drop shadow (`shadow`).
//! Phase 3c-6: the notify window (`notify`, Windows only).  Phase 3c-5a:
//! the window infrastructure (`win`, Windows only): classes, creation and
//! dispatch for windows owned by Rust.  Phase 4b-1: the placement decisions
//! of `CUIPresenter` (`presenter`).  Phase 4b-2a: the presenter's state
//! and notify-window stack owned by Rust (`presenterwin`, Windows only).

#[cfg(windows)]
pub mod candpaint;
#[cfg(windows)]
pub mod candproc;
#[cfg(windows)]
pub mod candwin;
pub mod candwnd;
pub mod layout;
#[cfg(windows)]
pub mod notify;
#[cfg(windows)]
pub mod notifywin;
pub mod presenter;
#[cfg(windows)]
pub mod presenterwin;
#[cfg(windows)]
pub mod sbpaint;
#[cfg(windows)]
pub mod sbproc;
#[cfg(windows)]
pub mod sbwin;
pub mod shadow;
#[cfg(windows)]
pub mod win;
