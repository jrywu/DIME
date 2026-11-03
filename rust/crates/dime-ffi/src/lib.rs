//! C ABI over `dime-core`.  See `include/dime_ffi.h` for the contract.
//!
//! Rules for every exported function:
//! * never unwind into C/C++: each body runs inside [`guard`], which turns a
//!   panic into the documented "failure" return value;
//! * null pointers are accepted wherever a "nothing" value makes sense;
//! * strings are UTF-16 `(pointer, length)` pairs, never NUL-terminated;
//! * returned objects are owned by the caller and released with the matching
//!   `*_free` function.

#![cfg(windows)]
#![allow(clippy::missing_safety_doc)]

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_core::big5;
use dime_core::sentence::{self, Lexicon, Position};
use dime_core::{CandidateItem, DictionaryType, SearchSection, TableDictionaryEngine, WinTextOps};

pub struct DimeEngine(TableDictionaryEngine<WinTextOps>);
pub struct DimeItems(Vec<CandidateItem>);
pub struct DimeBuffer(Vec<u16>);
pub struct DimeLexicon(Lexicon);

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn slice<'a>(p: *const u16, len: usize) -> &'a [u16] {
    if p.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: caller guarantees `p` points to `len` readable units.
        unsafe { std::slice::from_raw_parts(p, len) }
    }
}

/// Write `s` into `out[..cap]`; returns the full length of `s`.
unsafe fn copy_out(s: &[u16], out: *mut u16, cap: usize) -> usize {
    if !out.is_null() {
        let n = s.len().min(cap);
        // SAFETY: caller guarantees `out` has room for `cap` units.
        unsafe { ptr::copy_nonoverlapping(s.as_ptr(), out, n) };
    }
    s.len()
}

unsafe fn set<T>(p: *mut T, v: T) {
    if !p.is_null() {
        // SAFETY: caller passed a valid out-pointer or null.
        unsafe { *p = v };
    }
}

fn boxed<T>(v: T) -> *mut T {
    Box::into_raw(Box::new(v))
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn dime_engine_new(lcid: u32, buf: *const u16, len: usize, dict_type: i32) -> *mut DimeEngine {
    guard(ptr::null_mut(), || {
        let Some(t) = DictionaryType::from_i32(dict_type) else {
            return ptr::null_mut();
        };
        let data = unsafe { slice(buf, len) }.to_vec();
        boxed(DimeEngine(TableDictionaryEngine::new(WinTextOps { lcid }, data, t)))
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_free(e: *mut DimeEngine) {
    if !e.is_null() {
        guard((), || drop(unsafe { Box::from_raw(e) }));
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_set_buffer(e: *mut DimeEngine, buf: *const u16, len: usize) {
    guard((), || {
        if let Some(e) = unsafe { e.as_mut() } {
            e.0.set_buffer(unsafe { slice(buf, len) }.to_vec());
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_set_search_section(e: *mut DimeEngine, section: i32) {
    guard((), || {
        let s = match section {
            1 => SearchSection::Symbol,
            2 => SearchSection::Phrase,
            3 => SearchSection::PhraseFromKeystroke,
            _ => SearchSection::Text,
        };
        if let Some(e) = unsafe { e.as_mut() } {
            e.0.set_search_section(s);
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_set_has_cin_phrase_section(e: *mut DimeEngine, v: i32) {
    guard((), || {
        if let Some(e) = unsafe { e.as_mut() } {
            e.0.has_cin_phrase_section = v != 0;
        }
    })
}

/// Returns 1 on success, 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn dime_engine_parse_config(e: *mut DimeEngine) -> i32 {
    guard(0, || match unsafe { e.as_mut() } {
        Some(e) => {
            e.0.parse_config();
            1
        }
        None => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_is_sorted_cin(e: *const DimeEngine) -> i32 {
    guard(0, || unsafe { e.as_ref() }.is_some_and(|e| e.0.is_sorted_cin()) as i32)
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_has_phrase_section(e: *const DimeEngine) -> i32 {
    guard(0, || {
        unsafe { e.as_ref() }.is_some_and(|e| e.0.config().has_phrase_section) as i32
    })
}

/// -1 = `%autoCompose` not present, 0/1 = its value.
#[no_mangle]
pub unsafe extern "C" fn dime_engine_auto_compose(e: *const DimeEngine) -> i32 {
    guard(-1, || {
        unsafe { e.as_ref() }
            .and_then(|e| e.0.config().auto_compose)
            .map_or(-1, i32::from)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_get_selkey(e: *const DimeEngine, out: *mut u16, cap: usize) -> usize {
    guard(0, || {
        unsafe { e.as_ref() }.map_or(0, |e| unsafe { copy_out(&e.0.config().selkey, out, cap) })
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_get_endkey(e: *const DimeEngine, out: *mut u16, cap: usize) -> usize {
    guard(0, || {
        unsafe { e.as_ref() }.map_or(0, |e| unsafe { copy_out(&e.0.config().endkey, out, cap) })
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_radical_count(e: *const DimeEngine) -> usize {
    guard(0, || {
        unsafe { e.as_ref() }.map_or(0, |e| e.0.config().radical_map.len())
    })
}

/// Entries are ordered by key.  Returns 1 if `i` is valid.  `*value` stays
/// valid until the engine is modified or freed.
#[no_mangle]
pub unsafe extern "C" fn dime_engine_radical_at(
    e: *const DimeEngine,
    i: usize,
    key: *mut u16,
    value: *mut *const u16,
    value_len: *mut usize,
) -> i32 {
    guard(0, || {
        let Some((k, v)) = unsafe { e.as_ref() }.and_then(|e| e.0.config().radical_map.iter().nth(i)) else {
            return 0;
        };
        unsafe {
            set(key, *k);
            set(value, v.as_ptr());
            set(value_len, v.len());
        }
        1
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_radical_index_count(e: *const DimeEngine) -> usize {
    guard(0, || {
        unsafe { e.as_ref() }
            .and_then(|e| e.0.config().radical_index.as_ref())
            .map_or(0, |m| m.len())
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_radical_index_at(
    e: *const DimeEngine,
    i: usize,
    key: *mut u16,
    offset: *mut usize,
) -> i32 {
    guard(0, || {
        let Some((k, off)) = unsafe { e.as_ref() }
            .and_then(|e| e.0.config().radical_index.as_ref())
            .and_then(|m| m.iter().nth(i))
        else {
            return 0;
        };
        unsafe {
            set(key, *k);
            set(offset, *off);
        }
        1
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_config_entry_count(e: *const DimeEngine) -> usize {
    guard(0, || {
        unsafe { e.as_ref() }.map_or(0, |e| e.0.config().config_entries.len())
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_config_entry_at(
    e: *const DimeEngine,
    i: usize,
    key: *mut *const u16,
    key_len: *mut usize,
    value: *mut *const u16,
    value_len: *mut usize,
) -> i32 {
    guard(0, || {
        let Some((k, v)) = unsafe { e.as_ref() }.and_then(|e| e.0.config().config_entries.get(i)) else {
            return 0;
        };
        unsafe {
            set(key, k.as_ptr());
            set(key_len, k.len());
            set(value, v.as_ptr());
            set(value_len, v.len());
        }
        1
    })
}

// ---------------------------------------------------------------------------
// Searches
// ---------------------------------------------------------------------------

unsafe fn collect(
    e: *const DimeEngine,
    key: *const u16,
    len: usize,
    f: impl FnOnce(&TableDictionaryEngine<WinTextOps>, &[u16]) -> Vec<CandidateItem>,
) -> *mut DimeItems {
    guard(ptr::null_mut(), || match unsafe { e.as_ref() } {
        Some(e) => boxed(DimeItems(f(&e.0, unsafe { slice(key, len) }))),
        None => ptr::null_mut(),
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_collect_word(e: *const DimeEngine, key: *const u16, len: usize) -> *mut DimeItems {
    unsafe { collect(e, key, len, |e, k| e.collect_word(k)) }
}

/// `CollectWord(key, CDIMEArray<CStringRange>*)`: phrases only (no sorted
/// index); `find_key` of each item is empty.
#[no_mangle]
pub unsafe extern "C" fn dime_engine_collect_word_strings(
    e: *const DimeEngine,
    key: *const u16,
    len: usize,
) -> *mut DimeItems {
    unsafe {
        collect(e, key, len, |e, k| {
            e.collect_word_strings(k)
                .into_iter()
                .map(|item| CandidateItem {
                    item,
                    ..CandidateItem::default()
                })
                .collect()
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_collect_word_for_wildcard(
    e: *const DimeEngine,
    key: *const u16,
    len: usize,
    word_freq: *const DimeEngine,
    existing: *const *const u16,
    existing_lens: *const usize,
    existing_count: usize,
) -> *mut DimeItems {
    let freq = unsafe { word_freq.as_ref() }.map(|f| &f.0);
    let prior: Vec<&[u16]> = if existing.is_null() || existing_lens.is_null() {
        Vec::new()
    } else {
        // SAFETY: caller passes `existing_count` (pointer, length) pairs.
        (0..existing_count)
            .map(|i| unsafe { slice(*existing.add(i), *existing_lens.add(i)) })
            .collect()
    };
    unsafe { collect(e, key, len, |e, k| e.collect_word_for_wildcard_after(k, freq, &prior)) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_collect_word_from_converted_string(
    e: *const DimeEngine,
    s: *const u16,
    len: usize,
) -> *mut DimeItems {
    unsafe { collect(e, s, len, |e, k| e.collect_word_from_converted_string(k)) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_collect_word_from_converted_string_for_wildcard(
    e: *const DimeEngine,
    s: *const u16,
    len: usize,
) -> *mut DimeItems {
    unsafe { collect(e, s, len, |e, k| e.collect_word_from_converted_string_for_wildcard(k)) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_sort_by_find_key(e: *const DimeEngine, items: *mut DimeItems) {
    guard((), || {
        if let (Some(e), Some(items)) = (unsafe { e.as_ref() }, unsafe { items.as_mut() }) {
            e.0.sort_by_find_key(&mut items.0);
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_engine_sort_by_word_frequency(e: *const DimeEngine, items: *mut DimeItems) {
    guard((), || {
        if let (Some(e), Some(items)) = (unsafe { e.as_ref() }, unsafe { items.as_mut() }) {
            e.0.sort_by_word_frequency(&mut items.0);
        }
    })
}

// ---------------------------------------------------------------------------
// Item lists
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn dime_items_count(items: *const DimeItems) -> usize {
    guard(0, || unsafe { items.as_ref() }.map_or(0, |i| i.0.len()))
}

unsafe fn item_field(
    items: *const DimeItems,
    i: usize,
    len: *mut usize,
    f: fn(&CandidateItem) -> &[u16],
) -> *const u16 {
    guard(ptr::null(), || {
        let Some(it) = unsafe { items.as_ref() }.and_then(|v| v.0.get(i)) else {
            unsafe { set(len, 0) };
            return ptr::null();
        };
        let s = f(it);
        unsafe { set(len, s.len()) };
        s.as_ptr()
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_items_item(items: *const DimeItems, i: usize, len: *mut usize) -> *const u16 {
    unsafe { item_field(items, i, len, |it| &it.item) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_items_find_key(items: *const DimeItems, i: usize, len: *mut usize) -> *const u16 {
    unsafe { item_field(items, i, len, |it| &it.find_key) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_items_word_frequency(items: *const DimeItems, i: usize) -> i32 {
    guard(0, || {
        unsafe { items.as_ref() }
            .and_then(|v| v.0.get(i))
            .map_or(0, |it| it.word_frequency)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_items_free(items: *mut DimeItems) {
    if !items.is_null() {
        guard((), || drop(unsafe { Box::from_raw(items) }));
    }
}

// ---------------------------------------------------------------------------
// Big5 filter
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn dime_big5_filter(lcid: u32, src: *const u16, len: usize) -> *mut DimeBuffer {
    guard(ptr::null_mut(), || {
        let ops = WinTextOps { lcid };
        boxed(DimeBuffer(big5::filter_buffer(&ops, unsafe { slice(src, len) })))
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_buffer_data(b: *const DimeBuffer, len: *mut usize) -> *const u16 {
    guard(ptr::null(), || match unsafe { b.as_ref() } {
        Some(b) => {
            unsafe { set(len, b.0.len()) };
            b.0.as_ptr()
        }
        None => {
            unsafe { set(len, 0) };
            ptr::null()
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_buffer_free(b: *mut DimeBuffer) {
    if !b.is_null() {
        guard((), || drop(unsafe { Box::from_raw(b) }));
    }
}

// ---------------------------------------------------------------------------
// Sentence conversion
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn dime_lexicon_new() -> *mut DimeLexicon {
    guard(ptr::null_mut(), || boxed(DimeLexicon(Lexicon::new())))
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_free(lex: *mut DimeLexicon) {
    if !lex.is_null() {
        guard((), || drop(unsafe { Box::from_raw(lex) }));
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_load_cin_values(lex: *mut DimeLexicon, buf: *const u16, len: usize) -> usize {
    guard(0, || match unsafe { lex.as_mut() } {
        Some(l) => l.0.load_cin_values(unsafe { slice(buf, len) }),
        None => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_load_freq_table(lex: *mut DimeLexicon, buf: *const u16, len: usize) -> usize {
    guard(0, || match unsafe { lex.as_mut() } {
        Some(l) => l.0.load_freq_table(unsafe { slice(buf, len) }),
        None => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_add_word(lex: *mut DimeLexicon, word: *const u16, len: usize, freq: u32) -> i32 {
    guard(0, || match unsafe { lex.as_mut() } {
        Some(l) => i32::from(l.0.add_word(unsafe { slice(word, len) }, freq)),
        None => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_word_count(lex: *const DimeLexicon) -> usize {
    guard(0, || unsafe { lex.as_ref() }.map_or(0, |l| l.0.word_count()))
}

#[no_mangle]
pub unsafe extern "C" fn dime_lexicon_char_count(lex: *const DimeLexicon) -> usize {
    guard(0, || unsafe { lex.as_ref() }.map_or(0, |l| l.0.char_count()))
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_convert(
    lex: *const DimeLexicon,
    position_count: usize,
    cand_start: *const usize,
    cands: *const *const u16,
    cand_lens: *const usize,
    pinned: *const i32,
    out_chosen: *mut i32,
) -> i32 {
    guard(0, || {
        if position_count == 0 {
            return 1;
        }
        if cand_start.is_null() || out_chosen.is_null() {
            return 0;
        }
        let empty = Lexicon::new();
        let lex = unsafe { lex.as_ref() }.map_or(&empty, |l| &l.0);
        // SAFETY: the caller passes position_count + 1 offsets.
        let starts = unsafe { std::slice::from_raw_parts(cand_start, position_count + 1) };
        let total = starts[position_count];
        if starts[0] != 0
            || starts.windows(2).any(|w| w[0] > w[1])
            || (total > 0 && (cands.is_null() || cand_lens.is_null()))
        {
            return 0;
        }
        let (ptrs, lens) = if total == 0 {
            (&[][..], &[][..])
        } else {
            // SAFETY: the caller passes `total` candidate pointers and lengths.
            unsafe {
                (
                    std::slice::from_raw_parts(cands, total),
                    std::slice::from_raw_parts(cand_lens, total),
                )
            }
        };
        let positions: Vec<Position> = (0..position_count)
            .map(|i| {
                // SAFETY: `pinned`, when not null, has position_count entries.
                let pin = if pinned.is_null() {
                    -1
                } else {
                    unsafe { *pinned.add(i) }
                };
                Position {
                    candidates: (starts[i]..starts[i + 1])
                        .map(|k| unsafe { slice(ptrs[k], lens[k]) })
                        .collect(),
                    pinned: usize::try_from(pin).ok(),
                }
            })
            .collect();
        let chosen = sentence::convert(lex, &positions);
        for (i, k) in chosen.into_iter().enumerate() {
            // SAFETY: `out_chosen` has position_count entries.
            unsafe { *out_chosen.add(i) = k as i32 };
        }
        1
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn round_trip_through_c_abi() {
        let src = w("%chardef begin\na\t中\na\t央\n%chardef end\n");
        let key = w("a");
        unsafe {
            let e = dime_engine_new(0x0404, src.as_ptr(), src.len(), 2);
            assert!(!e.is_null());
            assert_eq!(dime_engine_parse_config(e), 1);
            let items = dime_engine_collect_word(e, key.as_ptr(), key.len());
            assert_eq!(dime_items_count(items), 2);
            let mut n = 0;
            let p = dime_items_item(items, 1, &mut n);
            assert_eq!(std::slice::from_raw_parts(p, n), &w("央")[..]);
            assert!(dime_items_item(items, 9, &mut n).is_null());
            assert_eq!(n, 0);
            dime_items_free(items);
            dime_engine_free(e);
        }
    }

    #[test]
    fn null_inputs_are_safe() {
        unsafe {
            assert!(dime_engine_new(0x0404, ptr::null(), 0, 99).is_null());
            assert_eq!(dime_engine_parse_config(ptr::null_mut()), 0);
            assert!(dime_engine_collect_word(ptr::null(), ptr::null(), 0).is_null());
            assert_eq!(dime_items_count(ptr::null()), 0);
            dime_items_free(ptr::null_mut());
            dime_engine_free(ptr::null_mut());
        }
    }

    #[test]
    fn sentence_through_c_abi() {
        let freq = w("%chardef begin\n\u{73fe}\t500\n\u{5728}\t9000\n\u{4ee3}\t800\n%chardef end\n");
        let words = w("%chardef begin\nab\t\u{73fe}\u{4ee3}\n%chardef end\n");
        let c = [w("\u{73fe}"), w("\u{5728}"), w("\u{4ee3}")];
        let ptrs: Vec<*const u16> = c.iter().map(|v| v.as_ptr()).collect();
        let lens: Vec<usize> = c.iter().map(|v| v.len()).collect();
        let starts = [0usize, 1, 3];
        let mut chosen = [-1i32; 2];
        unsafe {
            let lex = dime_lexicon_new();
            assert_eq!(dime_lexicon_load_freq_table(lex, freq.as_ptr(), freq.len()), 3);
            assert_eq!(dime_lexicon_load_cin_values(lex, words.as_ptr(), words.len()), 1);
            assert_eq!(dime_lexicon_word_count(lex), 1);
            assert_eq!(dime_lexicon_char_count(lex), 3);
            let pinned = [-1i32, -1];
            let ok = dime_sentence_convert(
                lex,
                2,
                starts.as_ptr(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                pinned.as_ptr(),
                chosen.as_mut_ptr(),
            );
            assert_eq!(ok, 1);
            assert_eq!(chosen, [0, 1]);
            let pinned = [-1i32, 0];
            dime_sentence_convert(
                lex,
                2,
                starts.as_ptr(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                pinned.as_ptr(),
                chosen.as_mut_ptr(),
            );
            assert_eq!(chosen, [0, 0]);
            // A null lexicon still converts (first candidates).
            dime_sentence_convert(
                ptr::null(),
                2,
                starts.as_ptr(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                ptr::null(),
                chosen.as_mut_ptr(),
            );
            assert_eq!(chosen, [0, 0]);
            let bad = dime_sentence_convert(
                lex,
                2,
                ptr::null(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                ptr::null(),
                chosen.as_mut_ptr(),
            );
            assert_eq!(bad, 0);
            dime_lexicon_free(lex);
            dime_lexicon_free(ptr::null_mut());
        }
    }
}
