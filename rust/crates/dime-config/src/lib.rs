//! DIME settings (phase 1 of the Rust migration, see
//! `docs/RUST_MIGRATION_PLAN.md`): a port of `CConfig`'s data, defaults, INI
//! reading/writing and the CLI key registry.
//!
//! Behaviour matches the C++ code, quirks included (number parsing through
//! `_wtoi`/`wcstoul`, `"1"`-only booleans, the legacy colour migration, the
//! exact `WriteConfig` text); parity is checked against the C++ code by
//! `src/tests/RustConfigParityTest.cpp`.

pub mod cli;
pub mod geometry;
pub mod guid;
pub mod ini;
pub mod keys;
pub mod num;
pub mod page_layout;
pub mod settings_model;
pub mod text_file;
pub mod values;

pub use values::{ConfigValues, Env, ImeMode, Palette, Settings};
