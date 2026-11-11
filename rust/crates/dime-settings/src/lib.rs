//! The DIMESettings window (phase 5b, `docs/RUST_MIGRATION_PLAN.md`).
//!
//! 5b-6 moves the window-free computations of `SettingsWindow.cpp`
//! (`src/DIMESettings/SettingsWindowLayout.*`): card and row geometry, hit
//! tests, control placement, the custom-table buffer validator, and the
//! themed message box layout. 5b-7 moves the GDI / GDI+ painting
//! (`paint.rs`, `SettingsWindowPaint.*`). The window procedures and the
//! binary come in later steps.

pub mod layout;
pub mod msgbox;
#[cfg(windows)]
pub mod paint;
pub mod validate;
