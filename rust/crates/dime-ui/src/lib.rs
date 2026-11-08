//! UI of the DIME input method (migration phase 3).
//!
//! Phase 3a: the pure layout calculations of the C++ windows (`CBaseWindow`
//! placement around the composition, the notify-window stack of
//! `CUIPresenter`).  Phase 3b: the candidate window and scroll bar layout
//! (`candwnd`).  Phase 3c-1: the GDI drawing of the candidate window
//! (`candpaint`, Windows only).  The windows themselves are still C++.

#[cfg(windows)]
pub mod candpaint;
pub mod candwnd;
pub mod layout;
