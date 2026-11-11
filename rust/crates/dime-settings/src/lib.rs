//! The DIMESettings window (phase 5b, `docs/RUST_MIGRATION_PLAN.md`).
//!
//! 5b-6 moves the window-free computations of `SettingsWindow.cpp`
//! (`src/DIMESettings/SettingsWindowLayout.*`): card and row geometry, hit
//! tests, control placement, the custom-table buffer validator, and the
//! themed message box layout. 5b-7 moves the GDI / GDI+ painting
//! (`paint.rs`, `SettingsWindowPaint.*`). 5b-8 moves the windows, starting
//! with the themed message box (`msgboxwin.rs`, `SettingsMsgBox.*`) and the
//! navigation / hover / scroll / size reducer (`nav.rs`, `SettingsNav.*`).
//! The command handlers, the editor, and the binary come in later steps.

pub mod layout;
pub mod msgbox;
#[cfg(windows)]
pub mod msgboxwin;
pub mod nav;
#[cfg(windows)]
pub mod paint;
pub mod validate;
