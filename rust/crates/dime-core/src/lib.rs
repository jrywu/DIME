//! Platform-neutral core of the DIME input method.
//!
//! Phase 2 of the Rust migration: a faithful port of the C++ dictionary
//! stack (`DictionaryParser`, `DictionarySearch`, `TableDictionaryEngine`,
//! and the Big5 filter of `CMemoryFile`).  Behaviour, including the C++
//! quirks, is kept identical; parity with the C++ implementation is checked
//! by `src/tests/RustParityTest.cpp` in the C++ test project.

pub mod big5;
pub mod candgen;
pub mod candidates;
pub mod composition;
pub mod engine;
pub mod engine_setup;
pub mod keyclass;
pub mod keyevent;
pub mod langbar;
pub mod parser;
pub mod search;
pub mod sentence;
pub mod sentence_buffer;
pub mod session;
pub mod text;

pub use engine::{CandidateItem, DictionaryType, TableDictionaryEngine};
pub use search::{ConfigState, SearchSection};
#[cfg(windows)]
pub use text::WinTextOps;
pub use text::{default_ops, TextOps};

/// Decode a dictionary file's bytes into UTF-16 code units without the BOM,
/// matching what `CFile::SetupReadBuffer` produces for UTF-16LE files.
/// Returns `None` if the data is not UTF-16LE with a BOM.
pub fn decode_utf16le_with_bom(bytes: &[u8]) -> Option<Vec<u16>> {
    let body = bytes.strip_prefix(&[0xFF, 0xFE])?;
    let (units, _) = body.as_chunks::<2>();
    Some(units.iter().map(|&u| u16::from_le_bytes(u)).collect())
}
