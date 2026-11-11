//! The DIMESettings window (phase 5b, `docs/RUST_MIGRATION_PLAN.md`).
//!
//! 5b-6 moves the window-free computations of `SettingsWindow.cpp`
//! (`src/DIMESettings/SettingsWindowLayout.*`): card and row geometry, hit
//! tests, control placement, the custom-table buffer validator, and the
//! themed message box layout. Painting, the window procedures, and the
//! binary come in later steps.

pub mod layout;
pub mod msgbox;
pub mod validate;
