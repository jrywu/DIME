//! The DIMESettings window (phase 5b, `docs/RUST_MIGRATION_PLAN.md`).
//!
//! 5b-6 moves the window-free computations of `SettingsWindow.cpp`
//! (`src/DIMESettings/SettingsWindowLayout.*`): card and row geometry, hit
//! tests, control placement, the custom-table buffer validator, and the
//! themed message box layout. 5b-7 moves the GDI / GDI+ painting
//! (`paint.rs`, `SettingsWindowPaint.*`). 5b-8 moves the windows, starting
//! with the themed message box (`msgboxwin.rs`, `SettingsMsgBox.*`) and the
//! navigation / hover / scroll / size reducer (`nav.rs`, `SettingsNav.*`).
//! The command handlers (`command.rs`, `SettingsCommand.*`) run against a
//! host interface. The custom table editor's live validation is `editor.rs`
//! (`SettingsEditor.*`). Launch arguments, page defaults, and scroll
//! placement are `lifecycle.rs` (`SettingsLifecycle.*`); window creation,
//! theme and dpi changes are `frame.rs` (`SettingsFrame.*`); owner-drawn
//! controls and dark control colours are `owner.rs` (`SettingsOwnerDraw.*`);
//! the dialogs, file writes, and editor calls of the command host are
//! `winhost.rs` (`SettingsWinHost.*`).
//! The binary comes in a later step.

pub mod command;
pub mod controls;
#[cfg(windows)]
pub mod editor;
pub mod frame;
pub mod layout;
pub mod lifecycle;
pub mod msgbox;
#[cfg(windows)]
pub mod msgboxwin;
pub mod nav;
#[cfg(windows)]
pub mod owner;
#[cfg(windows)]
pub mod paint;
pub mod populate;
pub mod validate;
#[cfg(windows)]
pub mod winhost;
