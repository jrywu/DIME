//! UI of the DIME input method (migration phase 3).
//!
//! Phase 3a: the pure layout calculations of the C++ windows (`CBaseWindow`
//! placement around the composition, the notify-window stack of
//! `CUIPresenter`).  Phase 3b: the candidate window and scroll bar layout
//! (`candwnd`).  Phase 3c-1: the GDI drawing of the candidate window
//! (`candpaint`, Windows only).  Phase 3c-2: its message handling
//! (`candproc`, Windows only).  Phase 3c-3a: the scroll bar drawing
//! (`sbpaint`, Windows only).  Phase 3c-3b: the scroll bar events
//! (`sbproc`, Windows only).  The window objects themselves are still C++.

#[cfg(windows)]
pub mod candpaint;
#[cfg(windows)]
pub mod candproc;
pub mod candwnd;
pub mod layout;
#[cfg(windows)]
pub mod sbpaint;
#[cfg(windows)]
pub mod sbproc;
