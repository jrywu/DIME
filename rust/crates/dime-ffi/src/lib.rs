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

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use dime_core::big5;
use dime_core::candgen;
use dime_ui::{candpaint, candproc, candwnd, layout, sbpaint, sbproc};
use dime_core::candidates::{self, CandidatePaging};
use dime_core::composition;
use dime_core::keyclass;
use dime_core::keyevent;
use dime_core::sentence::{self, Lexicon, Position, UserModel};
use dime_core::sentence_buffer::SentenceBuffer;
use dime_core::session;
use dime_config::{ini, keys, ConfigValues, Env, ImeMode, Settings};
use dime_core::{CandidateItem, DictionaryType, SearchSection, TableDictionaryEngine, WinTextOps};

pub struct DimeEngine(TableDictionaryEngine<WinTextOps>);
pub struct DimeItems(Vec<CandidateItem>);
pub struct DimeBuffer(Vec<u16>);
pub struct DimeLexicon(Lexicon);
pub struct DimeUserModel(UserModel);

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
pub extern "C" fn dime_user_model_new() -> *mut DimeUserModel {
    guard(ptr::null_mut(), || boxed(DimeUserModel(UserModel::new())))
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_free(model: *mut DimeUserModel) {
    if !model.is_null() {
        guard((), || drop(unsafe { Box::from_raw(model) }));
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_import(model: *mut DimeUserModel, buf: *const u16, len: usize) -> usize {
    guard(0, || match unsafe { model.as_mut() } {
        Some(m) => m.0.import(unsafe { slice(buf, len) }),
        None => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_export(model: *const DimeUserModel) -> *mut DimeBuffer {
    guard(ptr::null_mut(), || match unsafe { model.as_ref() } {
        Some(m) => boxed(DimeBuffer(m.0.export())),
        None => ptr::null_mut(),
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_merge(dst: *mut DimeUserModel, src: *const DimeUserModel) {
    guard((), || {
        if let (Some(d), Some(s)) = (unsafe { dst.as_mut() }, unsafe { src.as_ref() }) {
            if !ptr::eq(d, s) {
                d.0.merge(&s.0);
            }
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_clear(model: *mut DimeUserModel) {
    guard((), || {
        if let Some(m) = unsafe { model.as_mut() } {
            m.0.clear();
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_word_count(model: *const DimeUserModel) -> usize {
    guard(0, || unsafe { model.as_ref() }.map_or(0, |m| m.0.word_count()))
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_choice_count(model: *const DimeUserModel) -> usize {
    guard(0, || unsafe { model.as_ref() }.map_or(0, |m| m.0.choice_count()))
}

/// The arrays shared by convert and learn; see dime_ffi.h.
#[derive(Clone, Copy)]
struct SentenceArgs {
    count: usize,
    codes: *const *const u16,
    code_lens: *const usize,
    cand_start: *const usize,
    cands: *const *const u16,
    cand_lens: *const usize,
    pinned: *const i32,
}

impl SentenceArgs {
    /// Build the positions, or `None` when the arrays are inconsistent.
    unsafe fn positions<'a>(self) -> Option<Vec<Position<'a>>> {
        if self.cand_start.is_null() {
            return None;
        }
        // SAFETY: the caller passes count + 1 offsets.
        let starts = unsafe { std::slice::from_raw_parts(self.cand_start, self.count + 1) };
        let total = starts[self.count];
        if starts[0] != 0
            || starts.windows(2).any(|w| w[0] > w[1])
            || (total > 0 && (self.cands.is_null() || self.cand_lens.is_null()))
            || (self.codes.is_null() != self.code_lens.is_null())
        {
            return None;
        }
        let (ptrs, lens): (&[*const u16], &[usize]) = if total == 0 {
            (&[], &[])
        } else {
            // SAFETY: the caller passes `total` candidate pointers and lengths.
            unsafe {
                (
                    std::slice::from_raw_parts(self.cands, total),
                    std::slice::from_raw_parts(self.cand_lens, total),
                )
            }
        };
        Some(
            (0..self.count)
                .map(|i| {
                    // SAFETY: optional arrays, when not null, have `count` entries.
                    let pin = if self.pinned.is_null() {
                        -1
                    } else {
                        unsafe { *self.pinned.add(i) }
                    };
                    let code = if self.codes.is_null() {
                        &[][..]
                    } else {
                        unsafe { slice(*self.codes.add(i), *self.code_lens.add(i)) }
                    };
                    Position {
                        code,
                        candidates: (starts[i]..starts[i + 1])
                            .map(|k| unsafe { slice(ptrs[k], lens[k]) })
                            .collect(),
                        pinned: usize::try_from(pin).ok(),
                    }
                })
                .collect(),
        )
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_convert(
    lex: *const DimeLexicon,
    user: *const DimeUserModel,
    position_count: usize,
    codes: *const *const u16,
    code_lens: *const usize,
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
        if out_chosen.is_null() {
            return 0;
        }
        let args = SentenceArgs {
            count: position_count,
            codes,
            code_lens,
            cand_start,
            cands,
            cand_lens,
            pinned,
        };
        let Some(positions) = (unsafe { args.positions() }) else {
            return 0;
        };
        let empty = Lexicon::new();
        let lex = unsafe { lex.as_ref() }.map_or(&empty, |l| &l.0);
        let user = unsafe { user.as_ref() }.map(|u| &u.0);
        let chosen = sentence::convert_with_user(lex, user, &positions);
        for (i, k) in chosen.into_iter().enumerate() {
            // SAFETY: `out_chosen` has position_count entries.
            unsafe { *out_chosen.add(i) = k as i32 };
        }
        1
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_user_model_learn(
    model: *mut DimeUserModel,
    position_count: usize,
    codes: *const *const u16,
    code_lens: *const usize,
    cand_start: *const usize,
    cands: *const *const u16,
    cand_lens: *const usize,
    pinned: *const i32,
    chosen: *const i32,
) -> i32 {
    guard(0, || {
        let Some(m) = (unsafe { model.as_mut() }) else { return 0 };
        if position_count == 0 || chosen.is_null() {
            return 0;
        }
        let args = SentenceArgs {
            count: position_count,
            codes,
            code_lens,
            cand_start,
            cands,
            cand_lens,
            pinned,
        };
        let Some(positions) = (unsafe { args.positions() }) else {
            return 0;
        };
        // SAFETY: `chosen` has position_count entries.
        let chosen: Vec<usize> = unsafe { std::slice::from_raw_parts(chosen, position_count) }
            .iter()
            .map(|&k| usize::try_from(k).unwrap_or(0))
            .collect();
        i32::from(m.0.learn(&positions, &chosen))
    })
}

// ---------------------------------------------------------------------------
// Settings (dime_config, port of CConfig)
// ---------------------------------------------------------------------------

/// A settings record plus the host values its defaults depend on.
pub struct DimeSettings {
    settings: Settings,
    env: Env,
    /// NUL-terminated copy of the description for `dime_settings_description_cstr`.
    description_c: Vec<u16>,
}

impl DimeSettings {
    const fn empty() -> DimeSettings {
        DimeSettings {
            settings: Settings { v: ConfigValues::ZERO, reverse_conversion_description: None },
            env: Env { sys_3d_highlight: 0, windows_1809_or_later: 0 },
            description_c: Vec::new(),
        }
    }

    /// Re-derive `description_c`; called after every operation that may change it.
    fn refresh(&mut self) {
        self.description_c.clear();
        if let Some(d) = &self.settings.reverse_conversion_description {
            self.description_c.extend(d.iter().copied().take_while(|&c| c != 0));
            self.description_c.push(0);
        }
    }
}

// The C header declares the same struct; keep the layouts in lock-step.
const _: () = assert!(std::mem::size_of::<ConfigValues>() == 308);
const _: () = assert!(std::mem::size_of::<Env>() == 8);

/// The process-wide settings used by `CConfig` when it is built with
/// `DIME_USE_RUST_CONFIG`: its static members become references into
/// `settings.v`, so the address must never change (it is a constant-initialised
/// static).  Like the C++ statics it replaces, access is not synchronised: the
/// host serialises writes, exactly as it did before.
struct GlobalSettings(std::cell::UnsafeCell<DimeSettings>);
// SAFETY: see above; this mirrors the unsynchronised C++ static members.
unsafe impl Sync for GlobalSettings {}
static GLOBAL_SETTINGS: GlobalSettings = GlobalSettings(std::cell::UnsafeCell::new(DimeSettings::empty()));

fn global_settings() -> *mut DimeSettings {
    GLOBAL_SETTINGS.0.get()
}

/// The process-wide settings object.  Never free it (dime_settings_free ignores it).
#[no_mangle]
pub extern "C" fn dime_settings_global() -> *mut DimeSettings {
    global_settings()
}

/// Reset the process-wide settings to the static initialisers for `env`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_global_init(env: *const Env) {
    guard((), || {
        if let Some(env) = unsafe { env.as_ref() } {
            let g = unsafe { &mut *global_settings() };
            g.settings = Settings::initial(env);
            g.env = *env;
            g.refresh();
        }
    })
}

/// Update the host facts (system colour, Windows version) used by later defaults.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_set_env(s: *mut DimeSettings, env: *const Env) {
    guard((), || {
        if let (Some(s), Some(env)) = (unsafe { s.as_mut() }, unsafe { env.as_ref() }) {
            s.env = *env;
        }
    })
}

/// Direct pointer to the values; valid for the lifetime of `s`.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_values_ptr(s: *mut DimeSettings) -> *mut ConfigValues {
    guard(ptr::null_mut(), || match unsafe { s.as_mut() } {
        Some(s) => &mut s.settings.v,
        None => ptr::null_mut(),
    })
}

/// NUL-terminated description, or NULL when none is set.  Valid until the
/// description of `s` next changes.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_description_cstr(s: *const DimeSettings) -> *const u16 {
    guard(ptr::null(), || match unsafe { s.as_ref() } {
        Some(s) if !s.description_c.is_empty() => s.description_c.as_ptr(),
        _ => ptr::null(),
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_new(values: *const ConfigValues, env: *const Env) -> *mut DimeSettings {
    guard(ptr::null_mut(), || {
        let Some(env) = (unsafe { env.as_ref() }).copied() else {
            return ptr::null_mut();
        };
        let mut settings = Settings::initial(&env);
        if let Some(v) = unsafe { values.as_ref() } {
            settings.v = *v;
        }
        boxed(DimeSettings { settings, env, description_c: Vec::new() })
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_free(s: *mut DimeSettings) {
    if !s.is_null() && s != global_settings() {
        guard((), || drop(unsafe { Box::from_raw(s) }));
    }
}

/// Returns 1 on success.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_get_values(s: *const DimeSettings, out: *mut ConfigValues) -> i32 {
    guard(0, || match unsafe { s.as_ref() } {
        Some(s) if !out.is_null() => {
            unsafe { *out = s.settings.v };
            1
        }
        _ => 0,
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_set_values(s: *mut DimeSettings, values: *const ConfigValues) {
    guard((), || {
        if let (Some(s), Some(v)) = (unsafe { s.as_mut() }, unsafe { values.as_ref() }) {
            s.settings.v = *v;
        }
    })
}

/// Returns 1 and the text when a description is set, 0 when it is NULL.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_get_description(
    s: *const DimeSettings,
    text: *mut *const u16,
    len: *mut usize,
) -> i32 {
    guard(0, || {
        let d = unsafe { s.as_ref() }.and_then(|s| s.settings.reverse_conversion_description.as_ref());
        unsafe {
            set(text, d.map_or(ptr::null(), |d| d.as_ptr()));
            set(len, d.map_or(0, |d| d.len()));
        }
        i32::from(d.is_some())
    })
}

/// present == 0 stores NULL.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_set_description(s: *mut DimeSettings, text: *const u16, len: usize, present: i32) {
    guard((), || {
        if let Some(s) = unsafe { s.as_mut() } {
            s.settings.reverse_conversion_description = (present != 0).then(|| unsafe { slice(text, len) }.to_vec());
            s.refresh();
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_apply_entry(
    s: *mut DimeSettings,
    lcid: u32,
    key: *const u16,
    key_len: usize,
    value: *const u16,
    value_len: usize,
) {
    guard((), || {
        if let Some(s) = unsafe { s.as_mut() } {
            let ops = WinTextOps { lcid };
            ini::apply_entry(&ops, &mut s.settings, unsafe { slice(key, key_len) }, unsafe { slice(value, value_len) });
            s.refresh();
        }
    })
}

fn with_settings(s: *mut DimeSettings, f: impl FnOnce(&mut DimeSettings)) {
    guard((), || {
        if let Some(s) = unsafe { s.as_mut() } {
            f(s);
            s.refresh();
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_begin_load(s: *mut DimeSettings, mode: i32) {
    with_settings(s, |s| ini::begin_load(&mut s.settings, ImeMode::from_i32(mode)));
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_finish_load(s: *mut DimeSettings) {
    with_settings(s, |s| ini::finish_load(&mut s.settings, &s.env));
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_load_missing(s: *mut DimeSettings, mode: i32) {
    with_settings(s, |s| ini::load_missing(&mut s.settings, ImeMode::from_i32(mode), &s.env));
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_set_mode_defaults(s: *mut DimeSettings, mode: i32) {
    with_settings(s, |s| s.settings.v.set_mode_defaults(ImeMode::from_i32(mode)));
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_reset_all_defaults(s: *mut DimeSettings, mode: i32) {
    with_settings(s, |s| s.settings.reset_all_defaults(ImeMode::from_i32(mode), &s.env));
}

/// The INI file text (UTF-16 with BOM, CRLF) to write verbatim.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_serialize_ini(s: *const DimeSettings, mode: i32) -> *mut DimeBuffer {
    guard(ptr::null_mut(), || match unsafe { s.as_ref() } {
        Some(s) => boxed(DimeBuffer(ini::serialize(&s.settings, ImeMode::from_i32(mode)))),
        None => ptr::null_mut(),
    })
}

// ---------------------------------------------------------------------------
// Settings key registry (dime_config::keys, the CLI's g_keyRegistry)
// ---------------------------------------------------------------------------

fn key_names() -> &'static [Vec<u16>] {
    static NAMES: std::sync::OnceLock<Vec<Vec<u16>>> = std::sync::OnceLock::new();
    NAMES.get_or_init(|| keys::KEYS.iter().map(|k| k.name.encode_utf16().collect()).collect())
}

fn key_at(index: i32) -> Option<&'static keys::KeyInfo> {
    usize::try_from(index).ok().and_then(|i| keys::KEYS.get(i))
}

#[no_mangle]
pub extern "C" fn dime_key_count() -> i32 {
    keys::KEYS.len() as i32
}

/// Returns 1 if `index` is valid.  `name` stays valid for the process lifetime.
#[no_mangle]
pub unsafe extern "C" fn dime_key_info(
    index: i32,
    name: *mut *const u16,
    name_len: *mut usize,
    key_type: *mut i32,
    min: *mut i32,
    max: *mut i32,
    mode_mask: *mut u32,
) -> i32 {
    guard(0, || {
        let Some(k) = key_at(index) else { return 0 };
        let n = &key_names()[index as usize];
        let ty = match k.ty {
            keys::KeyType::Bool => 0,
            keys::KeyType::Int => 1,
            keys::KeyType::Color => 2,
            keys::KeyType::Str => 3,
            keys::KeyType::Clsid => 4,
        };
        unsafe {
            set(name, n.as_ptr());
            set(name_len, n.len());
            set(key_type, ty);
            set(min, k.min);
            set(max, k.max);
            set(mode_mask, k.mode_mask);
        }
        1
    })
}

/// Case-insensitive (ASCII) lookup; returns the index or -1.
#[no_mangle]
pub unsafe extern "C" fn dime_key_find(name: *const u16, len: usize) -> i32 {
    guard(-1, || {
        let Some(k) = keys::find_key(unsafe { slice(name, len) }) else { return -1 };
        keys::KEYS.iter().position(|x| std::ptr::eq(x, k)).map_or(-1, |i| i as i32)
    })
}

#[no_mangle]
pub extern "C" fn dime_key_applicable(index: i32, ime_mode: i32) -> i32 {
    guard(0, || key_at(index).map_or(0, |k| i32::from(keys::applicable(k, ImeMode::from_i32(ime_mode)))))
}

/// The `--get` text of a key; NULL for an invalid handle or index.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_get_key(s: *const DimeSettings, index: i32) -> *mut DimeBuffer {
    guard(ptr::null_mut(), || match (unsafe { s.as_ref() }, key_at(index)) {
        (Some(s), Some(k)) => boxed(DimeBuffer(keys::get_value(&s.settings, k))),
        _ => ptr::null_mut(),
    })
}

/// Validate and store a `--set` value.  Returns 0 on success, 1 when the
/// value is rejected (then `*error`, if non-NULL, receives the message
/// without a trailing newline; free it with dime_buffer_free), 2 on an
/// invalid handle or index.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_set_key(
    s: *mut DimeSettings,
    index: i32,
    value: *const u16,
    len: usize,
    error: *mut *mut DimeBuffer,
) -> i32 {
    guard(2, || {
        unsafe { set(error, ptr::null_mut()) };
        let (Some(s), Some(k)) = (unsafe { s.as_mut() }, key_at(index)) else { return 2 };
        match keys::set_value(&mut s.settings, k, unsafe { slice(value, len) }) {
            Ok(()) => {
                s.refresh();
                0
            }
            Err(e) => {
                unsafe { set(error, boxed(DimeBuffer(e.to_string().encode_utf16().collect()))) };
                1
            }
        }
    })
}

// ---------------------------------------------------------------------------
// Candidate selection / paging (dime_core::candidates, port of CCandidateListModel)
// ---------------------------------------------------------------------------

pub struct DimeCandidates(CandidatePaging);

fn with_cand<R>(c: *mut DimeCandidates, fallback: R, f: impl FnOnce(&mut CandidatePaging) -> R) -> R {
    guard(fallback, move || match unsafe { c.as_mut() } {
        Some(c) => f(&mut c.0),
        None => panic!("null candidates handle"),
    })
}

#[no_mangle]
pub extern "C" fn dime_candidates_new() -> *mut DimeCandidates {
    guard(ptr::null_mut(), || boxed(DimeCandidates(CandidatePaging::new())))
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_free(c: *mut DimeCandidates) {
    if !c.is_null() {
        guard((), || drop(unsafe { Box::from_raw(c) }));
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_push_item(c: *mut DimeCandidates) {
    with_cand(c, (), |c| c.push_item());
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_clear(c: *mut DimeCandidates) {
    with_cand(c, (), |c| c.clear());
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_selection(c: *mut DimeCandidates) -> i32 {
    with_cand(c, 0, |c| c.selection())
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_set_selection_raw(c: *mut DimeCandidates, index: i32) {
    with_cand(c, (), |c| c.set_selection_raw(index));
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_page_count(c: *mut DimeCandidates) -> u32 {
    with_cand(c, 0, |c| c.pages().len() as u32)
}

/// Page start; 0 for an invalid page.
#[no_mangle]
pub unsafe extern "C" fn dime_candidates_page_start(c: *mut DimeCandidates, page: u32) -> u32 {
    with_cand(c, 0, |c| c.pages().get(page as usize).copied().unwrap_or(0))
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_set_page_index(c: *mut DimeCandidates, pages: *const u32, count: u32) {
    with_cand(c, (), |c| {
        let p = if pages.is_null() || count == 0 { &[][..] } else { unsafe { std::slice::from_raw_parts(pages, count as usize) } };
        c.set_page_index(p)
    });
}

/// HRESULT; *page = current page (0 on failure).
#[no_mangle]
pub unsafe extern "C" fn dime_candidates_current_page(c: *mut DimeCandidates, page: *mut u32) -> i32 {
    with_cand(c, candidates::E_FAIL, |c| {
        let r = c.current_page();
        unsafe { set(page, r.unwrap_or(0)) };
        r.map_or_else(|e| e, |_| candidates::S_OK)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_set_selection_in_page(c: *mut DimeCandidates, pos: i32) -> i32 {
    with_cand(c, 0, |c| i32::from(c.set_selection_in_page(pos)))
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_move_selection(c: *mut DimeCandidates, offset: i32) -> i32 {
    with_cand(c, 0, |c| i32::from(c.move_selection(offset)))
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_set_selection(c: *mut DimeCandidates, page_size: u32, index: i32, applied: *mut i32) -> i32 {
    with_cand(c, 0, |c| {
        let (ok, a) = c.set_selection(page_size, index);
        unsafe { set(applied, i32::from(a)) };
        i32::from(ok)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_move_page(c: *mut DimeCandidates, page_size: u32, offset: i32, wrapped: *mut i32) -> i32 {
    with_cand(c, 0, |c| {
        let (ok, w) = c.move_page(page_size, offset);
        unsafe { set(wrapped, i32::from(w)) };
        i32::from(ok)
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_set_selection_offset(c: *mut DimeCandidates, page_size: u32, offset: i32) -> i32 {
    with_cand(c, 0, |c| i32::from(c.set_selection_offset(page_size, offset)))
}

#[no_mangle]
pub unsafe extern "C" fn dime_candidates_adjust_page_index_for_selection(c: *mut DimeCandidates, page_size: u32) -> i32 {
    with_cand(c, 0, |c| i32::from(c.adjust_page_index_for_selection(page_size)))
}

/// HRESULT (E_FAIL without page size, S_FALSE without a current page, else S_OK).
#[no_mangle]
pub unsafe extern "C" fn dime_candidates_current_page_has_empty_items(c: *mut DimeCandidates, page_size: u32, has_empty: *mut i32) -> i32 {
    with_cand(c, candidates::E_FAIL, |c| match c.current_page_has_empty_items(page_size) {
        Ok(v) => {
            unsafe { set(has_empty, i32::from(v)) };
            candidates::S_OK
        }
        Err(e) => e,
    })
}

/// HRESULT; updates *current_page and writes *current_page_index.
#[no_mangle]
pub unsafe extern "C" fn dime_candidates_adjust_page_index(
    c: *mut DimeCandidates,
    page_size: u32,
    current_page: *mut u32,
    current_page_index: *mut u32,
) -> i32 {
    with_cand(c, candidates::E_FAIL, |c| {
        let page = if current_page.is_null() { 0 } else { unsafe { *current_page } };
        let (hr, p, idx) = c.adjust_page_index(page_size, page);
        unsafe {
            set(current_page, p);
            set(current_page_index, idx);
        }
        hr
    })
}

// ---------------------------------------------------------------------------
// Key classification (CCompositionProcessorEngine::IsVirtualKeyNeed)
// ---------------------------------------------------------------------------

/// One select key of the active candidate range (`_KEYSTROKE` subset).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DimeSelkey {
    pub printable: u16,
    pub index: u32,
    pub modifiers: u32,
}

/// Snapshot of everything `IsVirtualKeyNeed` reads; see `keyclass::KeyContext`.
/// Booleans are `BOOL`-style `int32_t` (non-zero = true).
#[repr(C)]
pub struct DimeKeyInput {
    pub code: u32,
    pub wch: u16,
    pub base_char: u16,
    pub wch_printable: i32,
    pub composing: i32,
    pub candidate_mode: u32,
    pub candi_count: u32,
    pub candi_selection: i32,
    pub has_key_state: i32,
    pub modifiers: u32,
    pub caps_on: i32,
    pub ime_mode: u32,
    pub dictionary_available: i32,
    pub array_scope: u32,
    pub numeric_pad: u32,
    pub space_as_page_down: i32,
    pub shift_english_digit_mode: u32,
    pub phonetic_eten: i32,
    pub candidate_page_size: u32,
    pub has_wildcard_in_buffer: i32,
    pub buffer_null: i32,
    pub buffer: *const u16,
    pub buffer_len: u32,
    pub keystroke_functions: *const u32,
    pub keystroke_count: u32,
    /// `_pEndkey`; null when there is none.
    pub end_keys: *const u16,
    pub end_keys_len: u32,
    pub selkeys: *const DimeSelkey,
    pub selkey_count: u32,
}

/// Selkeys of a `DimeKeyInput` (kept alive alongside the context).
unsafe fn key_selkeys(i: &DimeKeyInput) -> Vec<keyclass::Selkey> {
    let raw: &[DimeSelkey] = if i.selkeys.is_null() || i.selkey_count == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(i.selkeys, i.selkey_count as usize) }
    };
    raw.iter()
        .map(|k| keyclass::Selkey {
            printable: k.printable,
            index: k.index,
            modifiers: k.modifiers,
        })
        .collect()
}

/// The `KeyContext` described by a `DimeKeyInput`.
unsafe fn key_context<'a>(i: &'a DimeKeyInput, selkeys: &'a [keyclass::Selkey]) -> keyclass::KeyContext<'a> {
    let functions: &[u32] = if i.keystroke_functions.is_null() || i.keystroke_count == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(i.keystroke_functions, i.keystroke_count as usize) }
    };
    let end_keys = if i.end_keys.is_null() {
        None
    } else {
        Some(unsafe { slice(i.end_keys, i.end_keys_len as usize) })
    };
    keyclass::KeyContext {
        code: i.code,
        wch: i.wch,
        base_char: i.base_char,
        wch_printable: i.wch_printable != 0,
        composing: i.composing != 0,
        candidate_mode: i.candidate_mode,
        candi_count: i.candi_count,
        candi_selection: i.candi_selection,
        has_key_state: i.has_key_state != 0,
        modifiers: i.modifiers,
        caps_on: i.caps_on != 0,
        ime_mode: i.ime_mode,
        dictionary_available: i.dictionary_available != 0,
        array_scope: i.array_scope,
        numeric_pad: i.numeric_pad,
        space_as_page_down: i.space_as_page_down != 0,
        shift_english_digit_mode: i.shift_english_digit_mode,
        phonetic_eten: i.phonetic_eten != 0,
        candidate_page_size: i.candidate_page_size,
        has_wildcard_in_buffer: i.has_wildcard_in_buffer != 0,
        buffer_null: i.buffer_null != 0,
        buffer: unsafe { slice(i.buffer, i.buffer_len as usize) },
        keystroke_functions: functions,
        end_keys,
        selkeys,
    }
}

/// Classify a key.  Returns 1 if the IME eats it, 0 if not; `category` and
/// `function` (may be null) receive the resulting key state.
///
/// # Safety
/// `input` must be valid; its pointers must reference the given lengths.
#[no_mangle]
pub unsafe extern "C" fn dime_classify_key(input: *const DimeKeyInput, category: *mut u32, function: *mut u32) -> i32 {
    guard(0, || {
        let Some(i) = (unsafe { input.as_ref() }) else {
            return 0;
        };
        let selkeys = unsafe { key_selkeys(i) };
        let ctx = unsafe { key_context(i, &selkeys) };
        let (eaten, state) = keyclass::classify_key(&ctx);
        unsafe {
            set(category, state.category);
            set(function, state.function);
        }
        i32::from(eaten)
    })
}

// ---------------------------------------------------------------------------
// Key events (KeyEventSink): _DecideKeyEaten and the Shift flags
// ---------------------------------------------------------------------------

/// The facts `_DecideKeyEaten` reads; `key` is filled as for
/// `dime_classify_key` (only when there is an engine).
#[repr(C)]
pub struct DimeDecideInput {
    pub keyboard_disabled: i32,
    pub is_open: i32,
    pub is_double_single_byte: i32,
    pub has_engine: i32,
    pub is_composing: i32,
    pub candi_count: u32,
    pub virtual_key_length: usize,
    pub key: DimeKeyInput,
}

#[repr(C)]
pub struct DimeDecideOutput {
    pub eaten: i32,
    pub wch: u16,
    pub category: u32,
    pub function: u32,
    /// Reset `_candidateMode` to NONE and `_isCandidateWithWildcard` to FALSE.
    pub reset_candidate_mode: i32,
}

/// `_DecideKeyEaten`.
///
/// # Safety
/// `session`, `input` and `output` must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_decide_key_eaten(
    session: *const DimeSession,
    input: *const DimeDecideInput,
    output: *mut DimeDecideOutput,
) {
    guard((), || {
        let (Some(s), Some(i), Some(o)) = (unsafe { session.as_ref() }, unsafe { input.as_ref() }, unsafe {
            output.as_mut()
        }) else {
            return;
        };
        let selkeys = unsafe { key_selkeys(&i.key) };
        let d = keyevent::DecideInput {
            keyboard_disabled: i.keyboard_disabled != 0,
            is_open: i.is_open != 0,
            is_double_single_byte: i.is_double_single_byte != 0,
            has_engine: i.has_engine != 0,
            is_composing: i.is_composing != 0,
            candi_count: i.candi_count,
            virtual_key_length: i.virtual_key_length,
            key: unsafe { key_context(&i.key, &selkeys) },
        };
        let r = keyevent::decide_key_eaten(&s.0, &d);
        o.eaten = i32::from(r.eaten);
        o.wch = r.wch;
        o.category = r.state.category;
        o.function = r.state.function;
        o.reset_candidate_mode = i32::from(r.reset_candidate_mode);
    })
}

/// `OnTestKeyDown`: a fresh Shift press clears the Shift flags.
///
/// # Safety
/// `session` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_on_test_key_down(session: *mut DimeSession, code: u32, lparam: isize) {
    guard((), || {
        if let Some(s) = unsafe { session.as_mut() } {
            keyevent::on_test_key_down(&mut s.0, code, lparam);
        }
    })
}

/// `OnKeyUp`: Shift released.
///
/// # Safety
/// `session` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_on_key_up(session: *mut DimeSession, code: u32) {
    guard((), || {
        if let Some(s) = unsafe { session.as_mut() } {
            keyevent::on_key_up(&mut s.0, code);
        }
    })
}

/// End of `OnKeyDown`: update the Shift English / pending wildcard flags.
///
/// # Safety
/// `session` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_after_key_down(
    session: *mut DimeSession,
    function: u32,
    code: u32,
    wch: u16,
    modifiers: u32,
) {
    guard((), || {
        if let Some(s) = unsafe { session.as_mut() } {
            keyevent::after_key_down(&mut s.0, function, code, wch, modifiers);
        }
    })
}

/// The Shift English flag (for logging).
///
/// # Safety
/// `session` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_shifted_english(session: *const DimeSession) -> i32 {
    guard(0, || unsafe { session.as_ref() }.map_or(0, |s| i32::from(s.0.shifted_english)))
}

/// The pending wildcard flag (for logging).
///
/// # Safety
/// `session` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_pending_wildcard(session: *const DimeSession) -> i32 {
    guard(0, || unsafe { session.as_ref() }.map_or(0, |s| i32::from(s.0.pending_wildcard)))
}

/// `OnTestKeyDown`: 1 and the key state to invoke the handler with, or 0.
///
/// # Safety
/// `category` and `function` must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_test_key_down_invoke(eaten: i32, category: *mut u32, function: *mut u32) -> i32 {
    guard(0, || {
        let (Some(c), Some(f)) = (unsafe { category.as_mut() }, unsafe { function.as_mut() }) else {
            return 0;
        };
        match keyevent::test_key_down_invoke(eaten != 0, keyclass::KeyState { category: *c, function: *f }) {
            Some(k) => {
                *c = k.category;
                *f = k.function;
                1
            }
            None => 0,
        }
    })
}

/// `OnKeyDown`: 1 and the key state to invoke the handler with, or 0.
///
/// # Safety
/// `category` and `function` must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_key_down_invoke(
    eaten: i32,
    category: *mut u32,
    function: *mut u32,
    code: u32,
    wch: u16,
    candidate_mode: u32,
) -> i32 {
    guard(0, || {
        let (Some(c), Some(f)) = (unsafe { category.as_mut() }, unsafe { function.as_mut() }) else {
            return 0;
        };
        let state = keyclass::KeyState { category: *c, function: *f };
        match keyevent::key_down_invoke(eaten != 0, state, code, wch, candidate_mode) {
            Some(k) => {
                *c = k.category;
                *f = k.function;
                1
            }
            None => 0,
        }
    })
}

// ---------------------------------------------------------------------------
// Keystroke buffer (AddVirtualKey / RemoveVirtualKey / GetReadingString, phonetic syllable)
// ---------------------------------------------------------------------------

/// `IsEscapeInput`.  `has_first`/`first_raw`: `*_keystrokeBuffer.Get()` when the
/// buffer is not null (read even at length zero).
///
/// # Safety
/// `keys` must point to `len` units (or be null with `len == 0`).
#[no_mangle]
pub unsafe extern "C" fn dime_is_escape_input(
    keys: *const u16,
    len: usize,
    has_first: i32,
    first_raw: u16,
    ime_mode: u32,
    array_scope: u32,
) -> i32 {
    guard(0, || {
        let keys = unsafe { slice(keys, len) };
        let first = (has_first != 0).then_some(first_raw);
        i32::from(composition::is_escape_input(keys, first, ime_mode, array_scope))
    })
}

#[no_mangle]
pub extern "C" fn dime_phonetic_add_key(syllable: u32, wch: u16, eten: i32) -> u32 {
    guard(syllable, || composition::phonetic_add_key(syllable, wch, eten != 0))
}

#[no_mangle]
pub extern "C" fn dime_phonetic_remove_last(syllable: u32) -> u32 {
    guard(0, || composition::phonetic_remove_last(syllable))
}

/// Writes the syllable's keys and a terminator to `out` (at least 5 units).
/// Returns the number of keys.
///
/// # Safety
/// `out` must have room for 5 units.
#[no_mangle]
pub unsafe extern "C" fn dime_phonetic_keystrokes(syllable: u32, out: *mut u16) -> u32 {
    guard(0, || {
        if out.is_null() {
            return 0;
        }
        let keys = composition::phonetic_keystrokes(syllable);
        unsafe {
            ptr::copy_nonoverlapping(keys.as_ptr(), out, keys.len());
            *out.add(keys.len()) = 0;
        }
        keys.len() as u32
    })
}

#[repr(C)]
pub struct DimeAddKeyInput {
    pub keys: *const u16,
    pub len: usize,
    pub null: i32,
    pub ime_mode: u32,
    pub array_scope: u32,
    pub phonetic_eten: i32,
    pub max_codes: u32,
    pub syllable: u32,
}

#[repr(C)]
pub struct DimeAddKeyResult {
    pub accepted: i32,
    /// `BEEP_TYPE`, or -1 for none.
    pub beep: i32,
    pub syllable: u32,
    /// 0 = buffer unchanged, 1 = append `append`, 2 = replace with `replace`.
    pub action: i32,
    pub append: u16,
    pub replace: [u16; 4],
    pub replace_len: u32,
}

/// `AddVirtualKey`: what to do with the buffer.
///
/// # Safety
/// `input` and `out` must be valid; `input.keys` must point to `input.len` units.
#[no_mangle]
pub unsafe extern "C" fn dime_add_key(input: *const DimeAddKeyInput, wch: u16, out: *mut DimeAddKeyResult) {
    guard((), || {
        let (Some(i), Some(o)) = (unsafe { input.as_ref() }, unsafe { out.as_mut() }) else {
            return;
        };
        let ctx = composition::AddKeyContext {
            keys: unsafe { slice(i.keys, i.len) },
            null: i.null != 0,
            ime_mode: i.ime_mode,
            array_scope: i.array_scope,
            phonetic_eten: i.phonetic_eten != 0,
            max_codes: i.max_codes,
            syllable: i.syllable,
        };
        let r = composition::add_key(&ctx, wch);
        o.accepted = i32::from(r.accepted);
        o.beep = r.beep.map_or(-1, |b| b as i32);
        o.syllable = r.syllable;
        o.append = 0;
        o.replace = [0; 4];
        o.replace_len = 0;
        match r.action {
            composition::AddAction::None => o.action = 0,
            composition::AddAction::Append(c) => {
                o.action = 1;
                o.append = c;
            }
            composition::AddAction::Replace(keys) => {
                o.action = 2;
                let n = keys.len().min(4);
                o.replace[..n].copy_from_slice(&keys[..n]);
                o.replace_len = n as u32;
            }
        }
    })
}

/// `RemoveVirtualKey` on the buffer in place; returns the new length.
///
/// # Safety
/// `keys` must point to `len` writable units.
#[no_mangle]
pub unsafe extern "C" fn dime_remove_key(keys: *mut u16, len: usize, index: usize) -> usize {
    guard(len.saturating_sub(1), || {
        if keys.is_null() || len == 0 {
            return 0;
        }
        let buf = unsafe { std::slice::from_raw_parts_mut(keys, len) };
        composition::remove_key(buf, index)
    })
}

/// One entry of the radical map (`_T_RadicalMap`).
#[repr(C)]
pub struct DimeRadical {
    pub key: u16,
    pub value: *const u16,
    pub value_len: usize,
}

/// `GetReadingString` for a non-empty key code.  Writes the reading string and
/// a terminator to `out` (`MAX_READINGSTRING` units); returns its length.
///
/// # Safety
/// Pointers must reference the given lengths; `out` must hold 64 units.
#[no_mangle]
pub unsafe extern "C" fn dime_reading_string(
    keys: *const u16,
    len: usize,
    escape_input: i32,
    radicals: *const DimeRadical,
    radical_count: usize,
    out: *mut u16,
    has_wildcard: *mut i32,
) -> usize {
    guard(0, || {
        if out.is_null() {
            return 0;
        }
        let raw: &[DimeRadical] = if radicals.is_null() || radical_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(radicals, radical_count) }
        };
        let map: Vec<(u16, &[u16])> =
            raw.iter().map(|r| (r.key, unsafe { slice(r.value, r.value_len) })).collect();
        let (text, wildcard) = composition::reading_string(unsafe { slice(keys, len) }, escape_input != 0, &map);
        unsafe {
            ptr::copy_nonoverlapping(text.as_ptr(), out, text.len());
            *out.add(text.len()) = 0;
            set(has_wildcard, i32::from(wildcard));
        }
        text.len()
    })
}

// ---------------------------------------------------------------------------
// Candidate generation plan (CCompositionProcessorEngine::GetCandidateList)
// ---------------------------------------------------------------------------

/// Inputs of `GetCandidateList`'s decisions; see `candgen::SearchInput`.
#[repr(C)]
pub struct DimeCandidateSearchInput {
    pub ime_mode: u32,
    pub dictionary_available: i32,
    pub buffer: *const u16,
    pub buffer_len: usize,
    pub buffer_null: i32,
    pub incremental: i32,
    pub wildcard_search: i32,
    pub array_phrase_ending: i32,
    pub array_scope: u32,
    pub big5_filter: i32,
    pub custom_table_priority: i32,
    pub array_single_quote_custom_phrase: i32,
    pub escape_input: i32,
    pub table_is_tts: i32,
    pub big5_is_tts: i32,
    pub has_big5: i32,
    pub has_custom: i32,
    pub has_array_short_code: i32,
    pub has_array_phrase: i32,
    pub has_ext_b: i32,
    pub has_ext_cd: i32,
    pub has_ext_e: i32,
    pub has_freq_table: i32,
    pub keystroke_sort: i32,
    pub wildcard_freq_sort: i32,
    pub phonetic_syllable: u32,
}

/// One step of the plan.  `op` is the `DIME_CANDGEN_*` code.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DimeCandidateStep {
    pub op: u32,
    pub engine: u32,
    pub key: u32,
    /// Section for SET_SECTION, "with frequency engine" for COLLECT_WILDCARD.
    pub arg: u32,
}

fn encode_step(st: candgen::Step) -> DimeCandidateStep {
    use candgen::Step::*;
    let (op, engine, key, arg) = match st {
        SingleWildcardItem => (0, 0, 0, 0),
        ResetActiveRange => (1, 0, 0, 0),
        SetSection { engine, section } => (2, engine, 0, section),
        Collect { engine, key } => (3, engine, key, 0),
        CollectWildcard { engine, key, with_freq } => (4, engine, key, u32::from(with_freq)),
        ReturnIfEmpty => (5, 0, 0, 0),
        SortByFindKeyCode { engine } => (6, engine, 0, 0),
        TrimFindKeyCodes => (7, 0, 0, 0),
        SetupFreqTable => (8, 0, 0, 0),
        SortByWordFrequency { engine } => (9, engine, 0, 0),
        UpdateWidth => (10, 0, 0, 0),
        ClearWildcardFlag => (11, 0, 0, 0),
    };
    DimeCandidateStep { op, engine, key, arg }
}

/// Plan `GetCandidateList`.  Writes up to `cap` steps; returns the total count
/// (larger than `cap` means the buffer was too small).
///
/// # Safety
/// `input` must be valid; `steps` must have room for `cap` entries.
#[no_mangle]
pub unsafe extern "C" fn dime_plan_candidate_search(
    input: *const DimeCandidateSearchInput,
    steps: *mut DimeCandidateStep,
    cap: usize,
) -> usize {
    guard(0, || {
        let Some(i) = (unsafe { input.as_ref() }) else {
            return 0;
        };
        let b = |v: i32| v != 0;
        let si = candgen::SearchInput {
            ime_mode: i.ime_mode,
            dictionary_available: b(i.dictionary_available),
            buffer: unsafe { slice(i.buffer, i.buffer_len) },
            buffer_null: b(i.buffer_null),
            incremental: b(i.incremental),
            wildcard_search: b(i.wildcard_search),
            array_phrase_ending: b(i.array_phrase_ending),
            array_scope: i.array_scope,
            big5_filter: b(i.big5_filter),
            custom_table_priority: b(i.custom_table_priority),
            array_single_quote_custom_phrase: b(i.array_single_quote_custom_phrase),
            escape_input: b(i.escape_input),
            table_is_tts: b(i.table_is_tts),
            big5_is_tts: b(i.big5_is_tts),
            has_big5: b(i.has_big5),
            has_custom: b(i.has_custom),
            has_array_short_code: b(i.has_array_short_code),
            has_array_phrase: b(i.has_array_phrase),
            has_ext_b: b(i.has_ext_b),
            has_ext_cd: b(i.has_ext_cd),
            has_ext_e: b(i.has_ext_e),
            has_freq_table: b(i.has_freq_table),
            keystroke_sort: b(i.keystroke_sort),
            wildcard_freq_sort: b(i.wildcard_freq_sort),
            phonetic_syllable: i.phonetic_syllable,
        };
        let plan = candgen::plan(&si);
        if !steps.is_null() {
            for (n, st) in plan.iter().take(cap).enumerate() {
                unsafe { *steps.add(n) = encode_step(*st) };
            }
        }
        plan.len()
    })
}

// ---------------------------------------------------------------------------
// Key handler flow (session): C++ implements the host through a vtable
// ---------------------------------------------------------------------------

/// The C++ text service as seen by `dime_core::session` (see `session::Host`;
/// strings returned through `out` stay valid until the next callback).
/// Every function receives `ctx`.  Booleans are `int32_t`.  The field order
/// must match `s_vtbl` in `src/RustSessionHost.cpp` and `dime_ffi.h`.
#[repr(C)]
pub struct DimeHostVtbl {
    pub is_composing: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_full_shape: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub virtual_key_length: unsafe extern "C" fn(ctx: *mut c_void) -> usize,
    pub caps_lock_on: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub shift_english_digit_mode: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub dayi_article_mode: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_alpha: unsafe extern "C" fn(ctx: *mut c_void, c: u16) -> i32,
    pub layout_char: unsafe extern "C" fn(ctx: *mut c_void, c: u16, caps_lock_on: i32, out: *mut u16) -> i32,
    pub add_char_and_finalize: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize) -> i32,
    pub remove_dummy_composition: unsafe extern "C" fn(ctx: *mut c_void),
    pub delete_candidate_list: unsafe extern "C" fn(ctx: *mut c_void, force: i32),
    pub terminate_composition: unsafe extern "C" fn(ctx: *mut c_void),
    pub has_ui_presenter: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_sentence_mode: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub ime_mode: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub array_scope: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub clear_on_beep: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub array_force_sp: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub array_notify_sp: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub do_han_convert: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub make_phrase: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_cmd_shell: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_ui_less_mode: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub reverse_conversion_active: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_escape_input_leading: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub is_array_short_code: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub selected_candidate: unsafe extern "C" fn(ctx: *mut c_void, out: *mut *const u16) -> usize,
    pub candidate_selection: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub selkey_index: unsafe extern "C" fn(ctx: *mut c_void, code: u32, wch: u16) -> i32,
    pub set_candidate_selection_in_page: unsafe extern "C" fn(ctx: *mut c_void, index: i32) -> i32,
    pub advise_ui_changed_by_arrow_key: unsafe extern "C" fn(ctx: *mut c_void, function: u32),
    pub array_special_code_matches: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub array_special_code_for: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize, out: *mut *const u16, out_len: *mut usize) -> i32,
    pub sc_from_tc: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize, out: *mut *const u16) -> usize,
    pub show_notify: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize, kind: u32),
    pub notify_selected_key_code: unsafe extern "C" fn(ctx: *mut c_void),
    pub show_associated_phrases: unsafe extern "C" fn(ctx: *mut c_void, last_char: u16) -> i32,
    pub start_composition: unsafe extern "C" fn(ctx: *mut c_void),
    pub add_composing_and_char: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize) -> i32,
    pub set_commit_string: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize),
    pub async_reverse_conversion: unsafe extern "C" fn(ctx: *mut c_void),
    pub beep: unsafe extern "C" fn(ctx: *mut c_void, kind: u32),
    pub selection: unsafe extern "C" fn(ctx: *mut c_void, strict: i32) -> i32,
    pub add_virtual_key: unsafe extern "C" fn(ctx: *mut c_void, wch: u16) -> i32,
    pub remove_virtual_key: unsafe extern "C" fn(ctx: *mut c_void, index: usize),
    pub is_escape_input: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub reading_string: unsafe extern "C" fn(ctx: *mut c_void, out: *mut *const u16, wildcard: *mut i32) -> usize,
    pub auto_compose: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub do_beep_on_candi: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub fetch_candidates: unsafe extern "C" fn(ctx: *mut c_void, incremental: i32, wildcard: i32, array_phrase_ending: i32) -> u32,
    pub set_candidate_text: unsafe extern "C" fn(ctx: *mut c_void),
    pub clear_candidate_list: unsafe extern "C" fn(ctx: *mut c_void),
    pub hide_candidate_window: unsafe extern "C" fn(ctx: *mut c_void),
    pub create_and_start_candidate: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub composition_arrow_key: unsafe extern "C" fn(ctx: *mut c_void, function: u32),
    pub sentence_write: unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize, caret: usize, with_attributes: i32) -> i32,
    pub sentence_models: unsafe extern "C" fn(ctx: *mut c_void, lexicon: *mut *const DimeLexicon, user: *mut *const DimeUserModel),
    pub sentence_learn: unsafe extern "C" fn(ctx: *mut c_void),
    pub sentence_clear_display: unsafe extern "C" fn(ctx: *mut c_void),
    pub keystroke_code: unsafe extern "C" fn(ctx: *mut c_void, out: *mut *const u16) -> usize,
    pub fetched_count: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub fetched_string: unsafe extern "C" fn(ctx: *mut c_void, index: u32, out: *mut *const u16) -> usize,
    pub show_sentence_candidates: unsafe extern "C" fn(ctx: *mut c_void, candidates: *const *const u16, lens: *const usize, count: usize, code: *const u16, code_len: usize, selection: i32),
}

/// IME state owned by Rust, kept by `CDIME` across edit sessions.
pub struct DimeSession(session::SessionState);

#[repr(C)]
pub struct DimeHost {
    pub vtbl: *const DimeHostVtbl,
    pub ctx: *mut c_void,
    pub session: *mut DimeSession,
}

struct FfiHost<'a> {
    v: &'a DimeHostVtbl,
    ctx: *mut c_void,
    /// Raw so that C++ callbacks may read the session (through
    /// `dime_session_*`) while a handler runs; Rust never keeps a borrow of it
    /// across a callback (every access goes through `state()` / `state_ref()`).
    state: *mut session::SessionState,
}

/// Copy a host string (`ptr`, `len`), null meaning empty.
fn host_string(ptr: *const u16, len: usize) -> Vec<u16> {
    unsafe { slice(ptr, len) }.to_vec()
}

macro_rules! host_bool {
    ($($name:ident),*) => {$(
        fn $name(&self) -> bool {
            unsafe { (self.v.$name)(self.ctx) != 0 }
        }
    )*};
}

macro_rules! host_u32 {
    ($($name:ident),*) => {$(
        fn $name(&self) -> u32 {
            unsafe { (self.v.$name)(self.ctx) }
        }
    )*};
}

macro_rules! host_unit {
    ($($name:ident),*) => {$(
        fn $name(&mut self) {
            unsafe { (self.v.$name)(self.ctx) }
        }
    )*};
}

impl session::Host for FfiHost<'_> {
    host_u32!(shift_english_digit_mode, ime_mode, array_scope);
    host_bool!(
        is_composing,
        is_full_shape,
        caps_lock_on,
        dayi_article_mode,
        has_ui_presenter,
        is_sentence_mode,
        clear_on_beep,
        array_force_sp,
        array_notify_sp,
        do_han_convert,
        make_phrase,
        is_cmd_shell,
        is_ui_less_mode,
        reverse_conversion_active,
        is_escape_input_leading,
        is_array_short_code,
        is_escape_input,
        auto_compose,
        do_beep_on_candi
    );
    host_unit!(
        remove_dummy_composition,
        terminate_composition,
        notify_selected_key_code,
        start_composition,
        async_reverse_conversion,
        set_candidate_text,
        clear_candidate_list,
        hide_candidate_window,
        sentence_learn,
        sentence_clear_display
    );

    fn virtual_key_length(&self) -> usize {
        unsafe { (self.v.virtual_key_length)(self.ctx) }
    }
    fn is_alpha(&self, c: u16) -> bool {
        unsafe { (self.v.is_alpha)(self.ctx, c) != 0 }
    }
    fn layout_char(&self, c: u16, caps_lock_on: bool) -> Option<u16> {
        let mut out = 0u16;
        let found = unsafe { (self.v.layout_char)(self.ctx, c, i32::from(caps_lock_on), &mut out) };
        (found != 0).then_some(out)
    }
    fn add_char_and_finalize(&mut self, text: &[u16]) -> i32 {
        unsafe { (self.v.add_char_and_finalize)(self.ctx, text.as_ptr(), text.len()) }
    }
    fn delete_candidate_list(&mut self, force: bool) {
        unsafe { (self.v.delete_candidate_list)(self.ctx, i32::from(force)) }
    }
    fn selected_candidate(&self) -> Vec<u16> {
        let mut p: *const u16 = ptr::null();
        let len = unsafe { (self.v.selected_candidate)(self.ctx, &mut p) };
        host_string(p, len)
    }
    fn candidate_selection(&self) -> i32 {
        unsafe { (self.v.candidate_selection)(self.ctx) }
    }
    fn selkey_index(&self, code: u32, wch: u16) -> i32 {
        unsafe { (self.v.selkey_index)(self.ctx, code, wch) }
    }
    fn set_candidate_selection_in_page(&mut self, index: i32) -> bool {
        unsafe { (self.v.set_candidate_selection_in_page)(self.ctx, index) != 0 }
    }
    fn advise_ui_changed_by_arrow_key(&mut self, function: u32) {
        unsafe { (self.v.advise_ui_changed_by_arrow_key)(self.ctx, function) }
    }
    fn array_special_code_matches(&mut self) -> bool {
        unsafe { (self.v.array_special_code_matches)(self.ctx) != 0 }
    }
    fn array_special_code_for(&mut self, text: &[u16]) -> Option<Vec<u16>> {
        let mut p: *const u16 = ptr::null();
        let mut len = 0usize;
        let found = unsafe { (self.v.array_special_code_for)(self.ctx, text.as_ptr(), text.len(), &mut p, &mut len) };
        (found != 0).then(|| host_string(p, len))
    }
    fn sc_from_tc(&mut self, text: &[u16]) -> Vec<u16> {
        let mut p: *const u16 = ptr::null();
        let len = unsafe { (self.v.sc_from_tc)(self.ctx, text.as_ptr(), text.len(), &mut p) };
        host_string(p, len)
    }
    fn show_notify(&mut self, text: &[u16], kind: u32) {
        unsafe { (self.v.show_notify)(self.ctx, text.as_ptr(), text.len(), kind) }
    }
    fn show_associated_phrases(&mut self, last_char: u16) -> bool {
        unsafe { (self.v.show_associated_phrases)(self.ctx, last_char) != 0 }
    }
    fn add_composing_and_char(&mut self, text: Option<&[u16]>) -> i32 {
        let (p, len) = text.map_or((ptr::null(), 0), |t| (t.as_ptr(), t.len()));
        unsafe { (self.v.add_composing_and_char)(self.ctx, p, len) }
    }
    fn set_commit_string(&mut self, text: &[u16]) {
        unsafe { (self.v.set_commit_string)(self.ctx, text.as_ptr(), text.len()) }
    }
    fn beep(&mut self, kind: u32) {
        unsafe { (self.v.beep)(self.ctx, kind) }
    }
    fn selection(&mut self, strict: bool) -> session::Selection {
        match unsafe { (self.v.selection)(self.ctx, i32::from(strict)) } {
            -1 => session::Selection::Unavailable,
            0 => session::Selection::Outside,
            _ => session::Selection::Covered,
        }
    }
    fn add_virtual_key(&mut self, wch: u16) -> bool {
        unsafe { (self.v.add_virtual_key)(self.ctx, wch) != 0 }
    }
    fn remove_virtual_key(&mut self, index: usize) {
        unsafe { (self.v.remove_virtual_key)(self.ctx, index) }
    }
    fn reading_string(&mut self) -> (Option<Vec<u16>>, Option<bool>) {
        let mut p: *const u16 = ptr::null();
        let mut wildcard: i32 = -1;
        let len = unsafe { (self.v.reading_string)(self.ctx, &mut p, &mut wildcard) };
        let text = (!p.is_null()).then(|| host_string(p, len));
        let wildcard = (wildcard >= 0).then_some(wildcard != 0);
        (text, wildcard)
    }
    fn fetch_candidates(&mut self, incremental: bool, wildcard: bool, array_phrase_ending: bool) -> u32 {
        unsafe {
            (self.v.fetch_candidates)(
                self.ctx,
                i32::from(incremental),
                i32::from(wildcard),
                i32::from(array_phrase_ending),
            )
        }
    }
    fn create_and_start_candidate(&mut self) -> i32 {
        unsafe { (self.v.create_and_start_candidate)(self.ctx) }
    }
    fn composition_arrow_key(&mut self, function: u32) {
        unsafe { (self.v.composition_arrow_key)(self.ctx, function) }
    }

    fn state(&mut self) -> &mut session::SessionState {
        // SAFETY: `with_host` checked the pointer; see the field comment.
        unsafe { &mut *self.state }
    }
    fn state_ref(&self) -> &session::SessionState {
        unsafe { &*self.state }
    }
    fn sentence_write(&mut self, text: &[u16], caret: usize, with_attributes: bool) -> i32 {
        unsafe { (self.v.sentence_write)(self.ctx, text.as_ptr(), text.len(), caret, i32::from(with_attributes)) }
    }
    fn convert_sentence(&mut self) {
        let mut lex: *const DimeLexicon = ptr::null();
        let mut user: *const DimeUserModel = ptr::null();
        unsafe { (self.v.sentence_models)(self.ctx, &mut lex, &mut user) };
        let (lex, user) = unsafe { (lex.as_ref().map(|l| &l.0), user.as_ref().map(|u| &u.0)) };
        self.state().sentence.convert(lex, user);
    }
    fn keystroke_code(&self) -> Vec<u16> {
        let mut p: *const u16 = ptr::null();
        let len = unsafe { (self.v.keystroke_code)(self.ctx, &mut p) };
        host_string(p, len)
    }
    fn fetched_strings(&self) -> Vec<Vec<u16>> {
        let count = unsafe { (self.v.fetched_count)(self.ctx) };
        (0..count)
            .map(|i| {
                let mut p: *const u16 = ptr::null();
                let len = unsafe { (self.v.fetched_string)(self.ctx, i, &mut p) };
                host_string(p, len)
            })
            .collect()
    }
    fn show_sentence_candidates(&mut self, pos: usize) {
        // The window keeps these pointers: they point into the session's
        // sentence, which does not change while the window is open.
        let position = unsafe { (*self.state).sentence.at(pos) };
        let ptrs: Vec<*const u16> = position.candidates.iter().map(|c| c.as_ptr()).collect();
        let lens: Vec<usize> = position.candidates.iter().map(Vec::len).collect();
        unsafe {
            (self.v.show_sentence_candidates)(
                self.ctx,
                ptrs.as_ptr(),
                lens.as_ptr(),
                ptrs.len(),
                position.code.as_ptr(),
                position.code.len(),
                position.chosen,
            )
        }
    }
}

/// `E_UNEXPECTED`, returned when a handler panics or gets a null host.
const E_UNEXPECTED_HR: i32 = 0x8000_FFFF_u32 as i32;

/// Run a session handler with the C++ host.  A panic (in Rust code; the C++
/// callbacks must not throw) is turned into `E_UNEXPECTED`.
unsafe fn with_host(host: *const DimeHost, f: impl FnOnce(&mut FfiHost<'_>) -> i32) -> i32 {
    guard(E_UNEXPECTED_HR, || {
        let Some(h) = (unsafe { host.as_ref() }) else {
            return E_UNEXPECTED_HR;
        };
        let Some(v) = (unsafe { h.vtbl.as_ref() }) else {
            return E_UNEXPECTED_HR;
        };
        if h.session.is_null() {
            return E_UNEXPECTED_HR;
        }
        // No `&mut` is created here: C++ reads and writes fields of the session
        // through references of its own (`CDIME::_candidateMode`) during callbacks.
        let mut fh = FfiHost {
            v,
            ctx: h.ctx,
            state: unsafe { ptr::addr_of_mut!((*h.session).0) },
        };
        f(&mut fh)
    })
}

/// `_HandleComplete`.
///
/// # Safety
/// `host` must be a valid host whose callbacks stay valid for the call.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_complete(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_complete(h)) }
}

/// `_HandleCancel`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_cancel(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_cancel(h)) }
}

/// `_HandleCompositionDoubleSingleByte`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_double_single_byte(host: *const DimeHost, wch: u16) -> i32 {
    unsafe { with_host(host, |h| session::handle_double_single_byte(h, wch)) }
}

/// `_HandleCompositionShiftEnglishInput`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_shift_english_input(host: *const DimeHost, code: u32, wch: u16) -> i32 {
    unsafe { with_host(host, |h| session::handle_shift_english_input(h, code, wch)) }
}

/// `_HandleCompositionAddressChar`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_address_char(host: *const DimeHost, wch: u16) -> i32 {
    unsafe { with_host(host, |h| session::handle_address_char(h, wch)) }
}

/// `_HandleCandidateFinalize` / `_HandleCandidateConvert` / `_HandleCandidateWorker`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_candidate_finalize(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_candidate_finalize(h)) }
}

/// `_HandleCandidateArrowKey` / `_HandlePhraseArrowKey`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_candidate_arrow_key(host: *const DimeHost, function: u32) -> i32 {
    unsafe { with_host(host, |h| session::handle_candidate_arrow_key(h, function)) }
}

/// `_HandleCandidateSelectByNumber`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_candidate_select_by_number(
    host: *const DimeHost,
    code: u32,
    wch: u16,
) -> i32 {
    unsafe { with_host(host, |h| session::handle_candidate_select_by_number(h, code, wch)) }
}

/// `_HandlePhraseFinalize`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_phrase_finalize(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_phrase_finalize(h)) }
}

/// `_HandlePhraseSelectByNumber`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_phrase_select_by_number(host: *const DimeHost, code: u32, wch: u16) -> i32 {
    unsafe { with_host(host, |h| session::handle_phrase_select_by_number(h, code, wch)) }
}

/// `_HandleCompositionInput`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_input(host: *const DimeHost, wch: u16) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_input(h, wch)) }
}

/// `_HandleCompositionInputWorker`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_input_worker(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_input_worker(h)) }
}

/// `_HandleCompositionFinalize`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_finalize(host: *const DimeHost, is_candidate_list: i32) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_finalize(h, is_candidate_list != 0)) }
}

/// `_HandleCompositionConvert`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_convert(
    host: *const DimeHost,
    wildcard: i32,
    array_phrase_ending: i32,
) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_convert(h, wildcard != 0, array_phrase_ending != 0)) }
}

/// `_HandleCompositionBackspace`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_backspace(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_backspace(h)) }
}

/// `_HandleCompositionArrowKey`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_composition_arrow_key(host: *const DimeHost, function: u32) -> i32 {
    unsafe { with_host(host, |h| session::handle_composition_arrow_key(h, function)) }
}

/// `_HandleSentenceCommit`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_sentence_commit(host: *const DimeHost) -> i32 {
    unsafe { with_host(host, |h| session::handle_sentence_commit(h)) }
}

/// `_HandleSentenceBackspace` (`code`: `VK_BACK` or `VK_DELETE`).
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_sentence_backspace(host: *const DimeHost, code: u32) -> i32 {
    unsafe { with_host(host, |h| session::handle_sentence_backspace(h, code)) }
}

/// `_HandleSentenceArrowKey`.
///
/// # Safety
/// See `dime_session_handle_complete`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_handle_sentence_arrow_key(host: *const DimeHost, function: u32) -> i32 {
    unsafe { with_host(host, |h| session::handle_sentence_arrow_key(h, function)) }
}

/// The Rust session state a `CDIME` keeps.
#[no_mangle]
pub extern "C" fn dime_session_new() -> *mut DimeSession {
    guard(ptr::null_mut(), || boxed(DimeSession(session::SessionState::new())))
}

/// # Safety
/// `s` must come from `dime_session_new` (or be null).
#[no_mangle]
pub unsafe extern "C" fn dime_session_free(s: *mut DimeSession) {
    if !s.is_null() {
        guard((), || drop(unsafe { Box::from_raw(s) }));
    }
}

/// A sentence is pending (`_IsSentencePending`).
///
/// # Safety
/// `s` must be a valid session or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_sentence_pending(s: *const DimeSession) -> i32 {
    guard(0, || unsafe { s.as_ref() }.map_or(0, |s| i32::from(!s.0.sentence.is_empty())))
}

/// Learn the pending sentence into `user` (`CSentenceUserModel::Learn`).
///
/// # Safety
/// `s` and `user` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_sentence_learn(s: *const DimeSession, user: *mut DimeUserModel) -> i32 {
    guard(0, || match (unsafe { s.as_ref() }, unsafe { user.as_mut() }) {
        (Some(s), Some(u)) => i32::from(s.0.sentence.learn_into(&mut u.0)),
        _ => 0,
    })
}

/// The converted sentence text, freed with `dime_buffer_free`.
///
/// # Safety
/// `s` must be a valid session or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_sentence_text(s: *const DimeSession) -> *mut DimeBuffer {
    guard(ptr::null_mut(), || match unsafe { s.as_ref() } {
        Some(s) => boxed(DimeBuffer(s.0.sentence.text())),
        None => ptr::null_mut(),
    })
}

/// Address of the session's candidate mode (`CANDIDATE_MODE`, 32 bits);
/// `CDIME::_candidateMode` is a reference to it.
///
/// # Safety
/// `s` must be a valid session; the pointer lives as long as the session.
#[no_mangle]
pub unsafe extern "C" fn dime_session_candidate_mode_ptr(s: *mut DimeSession) -> *mut u32 {
    if s.is_null() {
        return ptr::null_mut();
    }
    unsafe { ptr::addr_of_mut!((*s).0.candidate_mode) }
}

/// Address of the session's "candidates came from a wildcard" flag (`BOOL`);
/// `CDIME::_isCandidateWithWildcard` is a reference to it.
///
/// # Safety
/// As `dime_session_candidate_mode_ptr`.
#[no_mangle]
pub unsafe extern "C" fn dime_session_candidate_with_wildcard_ptr(s: *mut DimeSession) -> *mut i32 {
    if s.is_null() {
        return ptr::null_mut();
    }
    unsafe { ptr::addr_of_mut!((*s).0.candidate_with_wildcard) }
}

/// The Rust part of `_SentenceReset`.
///
/// # Safety
/// `s` must be a valid session or null.
#[no_mangle]
pub unsafe extern "C" fn dime_session_sentence_reset(s: *mut DimeSession) {
    guard((), || {
        if let Some(s) = unsafe { s.as_mut() } {
            s.0.reset_sentence();
        }
    })
}

// ---------------------------------------------------------------------------
// Sentence buffer (CSentenceBuffer)
// ---------------------------------------------------------------------------

pub struct DimeSentenceBuffer(SentenceBuffer);

/// Run `f` on the buffer, or return `fallback` for a null handle or a panic.
unsafe fn with_sb<R: Copy>(b: *mut DimeSentenceBuffer, fallback: R, f: impl FnOnce(&mut SentenceBuffer) -> R) -> R {
    guard(fallback, || match unsafe { b.as_mut() } {
        Some(b) => f(&mut b.0),
        None => fallback,
    })
}

#[no_mangle]
pub extern "C" fn dime_sentence_buffer_new() -> *mut DimeSentenceBuffer {
    guard(ptr::null_mut(), || boxed(DimeSentenceBuffer(SentenceBuffer::new())))
}

/// # Safety
/// `b` must come from `dime_sentence_buffer_new` (or be null).
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_free(b: *mut DimeSentenceBuffer) {
    if !b.is_null() {
        guard((), || drop(unsafe { Box::from_raw(b) }));
    }
}

/// # Safety
/// `b` must be a valid handle or null (all `dime_sentence_buffer_*`).
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_count(b: *mut DimeSentenceBuffer) -> usize {
    unsafe { with_sb(b, 0, |b| b.count()) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_cursor(b: *mut DimeSentenceBuffer) -> usize {
    unsafe { with_sb(b, 0, |b| b.cursor()) }
}

/// `op`: 0 MoveLeft, 1 MoveRight, 2 Home, 3 End.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_move(b: *mut DimeSentenceBuffer, op: u32) {
    unsafe {
        with_sb(b, (), |b| match op {
            0 => b.move_left(),
            1 => b.move_right(),
            2 => b.home(),
            3 => b.end(),
            _ => {}
        })
    }
}

/// `Insert`.  `cands`/`cand_lens` hold `count` candidates.
///
/// # Safety
/// The pointers must reference the given lengths.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_insert(
    b: *mut DimeSentenceBuffer,
    code: *const u16,
    code_len: usize,
    cands: *const *const u16,
    cand_lens: *const usize,
    count: usize,
    pinned: i32,
) -> i32 {
    unsafe {
        with_sb(b, 0, |b| {
            let candidates: Vec<Vec<u16>> = if count == 0 || cands.is_null() || cand_lens.is_null() {
                Vec::new()
            } else {
                let ptrs = std::slice::from_raw_parts(cands, count);
                let lens = std::slice::from_raw_parts(cand_lens, count);
                ptrs.iter().zip(lens).map(|(&p, &l)| slice(p, l).to_vec()).collect()
            };
            i32::from(b.insert(slice(code, code_len), candidates, pinned))
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_remove_before_cursor(b: *mut DimeSentenceBuffer) -> i32 {
    unsafe { with_sb(b, 0, |b| i32::from(b.remove_before_cursor())) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_remove_at_cursor(b: *mut DimeSentenceBuffer) -> i32 {
    unsafe { with_sb(b, 0, |b| i32::from(b.remove_at_cursor())) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_clear(b: *mut DimeSentenceBuffer) {
    unsafe { with_sb(b, (), |b| b.clear()) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_edit_target(b: *mut DimeSentenceBuffer) -> usize {
    unsafe { with_sb(b, 0, |b| b.edit_target()) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_pin(b: *mut DimeSentenceBuffer, pos: usize, index: i32) -> i32 {
    unsafe { with_sb(b, 0, |b| i32::from(b.pin(pos, index))) }
}

/// `Convert` (lexicon / user model may be null).
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_convert(
    b: *mut DimeSentenceBuffer,
    lex: *const DimeLexicon,
    user: *const DimeUserModel,
) {
    unsafe {
        with_sb(b, (), |b| {
            b.convert(lex.as_ref().map(|l| &l.0), user.as_ref().map(|u| &u.0));
        })
    }
}

/// `CSentenceUserModel::Learn(buffer)`.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_learn(b: *mut DimeSentenceBuffer, user: *mut DimeUserModel) -> i32 {
    unsafe {
        with_sb(b, 0, |b| match user.as_mut() {
            Some(u) => i32::from(b.learn_into(&mut u.0)),
            None => 0,
        })
    }
}

/// The code of position `pos` (null when out of range); valid until the buffer changes.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_position_code(
    b: *mut DimeSentenceBuffer,
    pos: usize,
    len: *mut usize,
) -> *const u16 {
    unsafe {
        with_sb(b, ptr::null(), |b| match b.positions().get(pos) {
            Some(p) => {
                set(len, p.code.len());
                p.code.as_ptr()
            }
            None => ptr::null(),
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_candidate_count(b: *mut DimeSentenceBuffer, pos: usize) -> usize {
    unsafe { with_sb(b, 0, |b| b.positions().get(pos).map_or(0, |p| p.candidates.len())) }
}

/// Candidate `i` of position `pos` (null when out of range); valid until the buffer changes.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_candidate(
    b: *mut DimeSentenceBuffer,
    pos: usize,
    i: usize,
    len: *mut usize,
) -> *const u16 {
    unsafe {
        with_sb(b, ptr::null(), |b| match b.positions().get(pos).and_then(|p| p.candidates.get(i)) {
            Some(c) => {
                set(len, c.len());
                c.as_ptr()
            }
            None => ptr::null(),
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_pinned(b: *mut DimeSentenceBuffer, pos: usize) -> i32 {
    unsafe { with_sb(b, -1, |b| b.positions().get(pos).map_or(-1, |p| p.pinned)) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_chosen(b: *mut DimeSentenceBuffer, pos: usize) -> i32 {
    unsafe { with_sb(b, 0, |b| b.positions().get(pos).map_or(0, |p| p.chosen)) }
}

/// `Text`, freed with `dime_buffer_free`.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_text(b: *mut DimeSentenceBuffer) -> *mut DimeBuffer {
    unsafe { with_sb(b, ptr::null_mut(), |b| boxed(DimeBuffer(b.text()))) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_text_length(b: *mut DimeSentenceBuffer, n: usize) -> usize {
    unsafe { with_sb(b, 0, |b| b.text_length(n)) }
}

/// `Display(reading)`, freed with `dime_buffer_free`.
#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_display(
    b: *mut DimeSentenceBuffer,
    reading: *const u16,
    len: usize,
) -> *mut DimeBuffer {
    unsafe { with_sb(b, ptr::null_mut(), |b| boxed(DimeBuffer(b.display(slice(reading, len))))) }
}

#[no_mangle]
pub unsafe extern "C" fn dime_sentence_buffer_display_caret(
    b: *mut DimeSentenceBuffer,
    reading: *const u16,
    len: usize,
) -> usize {
    unsafe { with_sb(b, 0, |b| b.display_caret(slice(reading, len))) }
}

/// `GetCandidateStringInConverted`: the lookup key for associated phrases.
/// Writes it to `out` (room for `len + 2` units, terminated) and returns its length.
///
/// # Safety
/// `text` must point to `len` units and `out` to `len + 2`.
#[no_mangle]
pub unsafe extern "C" fn dime_associated_phrase_key(
    text: *const u16,
    len: usize,
    phrase_table_is_tts: i32,
    has_phrase_section: i32,
    has_cin_phrase_section: i32,
    out: *mut u16,
) -> usize {
    guard(0, || {
        if out.is_null() {
            return 0;
        }
        let key = candgen::associated_phrase_key(
            unsafe { slice(text, len) },
            phrase_table_is_tts != 0,
            has_phrase_section != 0,
            has_cin_phrase_section != 0,
        );
        unsafe {
            ptr::copy_nonoverlapping(key.as_ptr(), out, key.len());
            *out.add(key.len()) = 0;
        }
        key.len()
    })
}

// ---------------------------------------------------------------------------
// UI layout (dime-ui): window placement and the notify-window stack
// ---------------------------------------------------------------------------

/// `RectInRect`.  `RECT` and `DimeRect` have the same layout.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_rect_in_rect(limit: *const layout::Rect, target: *const layout::Rect) -> u32 {
    guard(0, || match (unsafe { limit.as_ref() }, unsafe { target.as_ref() }) {
        (Some(l), Some(t)) => layout::rect_in_rect(l, t),
        _ => 0,
    })
}

/// The point whose monitor work area `_GetWindowExtent` uses.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_work_area_probe(
    text: *const layout::Rect,
    window: *const layout::Rect,
    out: *mut layout::Point,
) {
    guard((), || {
        if let (Some(t), Some(w)) = (unsafe { text.as_ref() }, unsafe { window.as_ref() }) {
            unsafe { set(out, layout::work_area_probe(t, w)) };
        }
    })
}

/// `CalcFitPointAroundTextExtent`.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_fit_point(
    text: *const layout::Rect,
    work: *const layout::Rect,
    window: *const layout::Rect,
    out: *mut layout::Point,
) {
    guard((), || {
        if let (Some(t), Some(a), Some(w)) = (unsafe { text.as_ref() }, unsafe { work.as_ref() }, unsafe {
            window.as_ref()
        }) {
            unsafe { set(out, layout::fit_point_around_text_extent(t, a, w)) };
        }
    })
}

unsafe fn notify_slots<'a>(slots: *const layout::NotifySlot, count: usize) -> &'a [layout::NotifySlot] {
    if slots.is_null() || count == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(slots, count) }
    }
}

/// `_DecideStackGrowUp`.
///
/// # Safety
/// `slots` must point to `count` entries.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_stack_grow_up(
    has_candidate: i32,
    candidate_top: i32,
    notify_y: i32,
    slots: *const layout::NotifySlot,
    count: usize,
    screen_bottom: i32,
) -> i32 {
    guard(0, || {
        let top = (has_candidate != 0).then_some(candidate_top);
        i32::from(layout::stack_grow_up(top, notify_y, unsafe { notify_slots(slots, count) }, screen_bottom))
    })
}

/// `_ComputeStackPosForNewSlot`.
///
/// # Safety
/// `slots` must point to `count` entries; `out` must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_stack_pos_for_new_slot(
    notify: layout::Point,
    slots: *const layout::NotifySlot,
    count: usize,
    new_slot: usize,
    grow_up: i32,
    out: *mut layout::Point,
) {
    guard((), || {
        let p = layout::stack_pos_for_new_slot(notify, unsafe { notify_slots(slots, count) }, new_slot, grow_up != 0);
        unsafe { set(out, p) };
    })
}

/// `_RestackVisibleSlots`: `positions[i]` and `placed[i]` (1 for visible slots).
///
/// # Safety
/// `slots`, `positions` and `placed` must have `count` entries.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_restack(
    has_candidate: i32,
    candidate_rect: layout::Rect,
    candidate_location: layout::Point,
    notify: layout::Point,
    slots: *const layout::NotifySlot,
    count: usize,
    grow_up: i32,
    positions: *mut layout::Point,
    placed: *mut i32,
) {
    guard((), || {
        if positions.is_null() || placed.is_null() {
            return;
        }
        let candidate = (has_candidate != 0).then_some((candidate_rect, candidate_location));
        let out = layout::restack(candidate, notify, unsafe { notify_slots(slots, count) }, grow_up != 0);
        for (i, p) in out.into_iter().enumerate() {
            unsafe {
                *placed.add(i) = i32::from(p.is_some());
                *positions.add(i) = p.unwrap_or_default();
            }
        }
    })
}

// ---------------------------------------------------------------------------
// UI layout (dime-ui): candidate window and scroll bar (phase 3b)
// ---------------------------------------------------------------------------

/// Win32 `MulDiv`.
#[no_mangle]
pub extern "C" fn dime_ui_mul_div(a: i32, b: i32, c: i32) -> i32 {
    guard(-1, || candwnd::mul_div(a, b, c))
}

/// `WM_CREATE` metrics of the candidate window.
///
/// # Safety
/// The out-pointers must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_initial_metrics(
    cx: i32,
    cy: i32,
    wnd_width: u32,
    cx_title: *mut i32,
    cy_row: *mut i32,
    padding: *mut i32,
) {
    guard((), || {
        let (t, r, p) = candwnd::initial_metrics(cx, cy, wnd_width);
        unsafe {
            set(cx_title, t);
            set(cy_row, r);
            set(padding, p);
        }
    })
}

/// `_DrawList` metrics.
///
/// # Safety
/// `out` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_metrics(
    cx: i32,
    cy: i32,
    ave_char_width: i32,
    wnd_width: u32,
    out: *mut candwnd::CandMetrics,
) {
    guard((), || unsafe { set(out, candwnd::metrics(cx, cy, ave_char_width, wnd_width)) })
}

/// Candidate window size.
///
/// # Safety
/// The out-pointers must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_window_size(
    cx_title: i32,
    cy_row: i32,
    padding: i32,
    rows: i32,
    sb_width: i32,
    width: *mut i32,
    height: *mut i32,
) {
    guard((), || {
        let (w, h) = candwnd::window_size(cx_title, cy_row, padding, rows, sb_width);
        unsafe {
            set(width, w);
            set(height, h);
        }
    })
}

/// The scroll bar inside the candidate window.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_scrollbar_rect(
    client: *const layout::Rect,
    sb_width: i32,
    dpi: u32,
    corner_radius_base: i32,
    out: *mut layout::Rect,
) {
    guard((), || {
        if let Some(c) = unsafe { client.as_ref() } {
            unsafe { set(out, candwnd::scrollbar_rect(c, sb_width, dpi, corner_radius_base)) };
        }
    })
}

/// One row of the candidate list.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_row(
    paint: *const layout::Rect,
    index: i32,
    metrics: *const candwnd::CandMetrics,
    sb_width: i32,
    dpi: u32,
    out: *mut candwnd::CandRow,
) {
    guard((), || {
        if let (Some(p), Some(m)) = (unsafe { paint.as_ref() }, unsafe { metrics.as_ref() }) {
            unsafe { set(out, candwnd::row(p, index, m, sb_width, dpi)) };
        }
    })
}

/// Item truncation: returns the shown length; `*tail_offset` is where the
/// tail starts, or -1 when the item is not truncated.
///
/// # Safety
/// `tail_offset` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_truncate(length: i32, tail_offset: *mut i32) -> i32 {
    guard(length, || {
        let (shown, tail) = candwnd::truncate_item(length);
        unsafe { set(tail_offset, tail.unwrap_or(-1)) };
        shown
    })
}

/// Whether an item is measured to widen the window.
#[no_mangle]
pub extern "C" fn dime_ui_cand_item_needs_measure(display_length: i32, wnd_width: u32) -> i32 {
    guard(0, || i32::from(candwnd::item_needs_measure(display_length, wnd_width)))
}

/// Widen for a measured item: returns 1 and updates `*cx_title` / `*wnd_width`
/// when it widens.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_widen(
    display_length: i32,
    measured_cx: i32,
    text_left: i32,
    cx_title: *mut i32,
    wnd_width: *mut u32,
) -> i32 {
    guard(0, || {
        if cx_title.is_null() || wnd_width.is_null() {
            return 0;
        }
        match candwnd::widen_for_item(display_length, measured_cx, text_left, unsafe { *cx_title }) {
            Some((t, w)) => {
                unsafe {
                    *cx_title = t;
                    *wnd_width = w;
                }
                1
            }
            None => 0,
        }
    })
}

/// Whether the candidate window is resized after drawing.
///
/// # Safety
/// `paint` must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_needs_resize(cx_title: i32, paint: *const layout::Rect, sb_width: i32) -> i32 {
    guard(0, || match unsafe { paint.as_ref() } {
        Some(p) => i32::from(candwnd::needs_resize(cx_title, p, sb_width)),
        None => 0,
    })
}

/// The row under `pt`, or -1.
///
/// # Safety
/// `client` must be valid.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn dime_ui_cand_hit_row(
    client: *const layout::Rect,
    padding: i32,
    sb_width: i32,
    cy_row: i32,
    rows_in_page: u32,
    page_start: u32,
    count: u32,
    pt: layout::Point,
) -> i32 {
    guard(-1, || match unsafe { client.as_ref() } {
        Some(c) => candwnd::hit_row(c, padding, sb_width, cy_row, rows_in_page, page_start, count, pt)
            .map_or(-1, |r| r as i32),
        None => -1,
    })
}

/// The scroll bar column (`which` 0) or the list area (`which` 1).
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_mouse_area(
    client: *const layout::Rect,
    sb_width: i32,
    which: i32,
    out: *mut layout::Rect,
) {
    guard((), || {
        if let Some(c) = unsafe { client.as_ref() } {
            let r = if which == 0 {
                candwnd::scroll_column(c, sb_width)
            } else {
                candwnd::list_area(c, sb_width)
            };
            unsafe { set(out, r) };
        }
    })
}

/// The page indicator rectangle.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_page_indicator(
    client: *const layout::Rect,
    cy_row: i32,
    rows: i32,
    sb_width: i32,
    dpi: u32,
    out: *mut layout::Rect,
) {
    guard((), || {
        if let Some(c) = unsafe { client.as_ref() } {
            unsafe { set(out, candwnd::page_indicator(c, cy_row, rows, sb_width, dpi)) };
        }
    })
}

/// The scroll bar parts.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_parts(
    client: *const layout::Rect,
    dpi: u32,
    si: *const candwnd::ScrollInfo,
    out: *mut candwnd::ScrollParts,
) {
    guard((), || {
        if let (Some(c), Some(s)) = (unsafe { client.as_ref() }, unsafe { si.as_ref() }) {
            unsafe { set(out, candwnd::parts(c, dpi, s)) };
        }
    })
}

/// `SCROLLBAR_HITTEST` of `pt`.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_hit_test(
    client: *const layout::Rect,
    dpi: u32,
    si: *const candwnd::ScrollInfo,
    pt: layout::Point,
) -> i32 {
    guard(candwnd::hit::NONE, || match (unsafe { client.as_ref() }, unsafe { si.as_ref() }) {
        (Some(c), Some(s)) => candwnd::hit_test(c, dpi, s, pt),
        _ => candwnd::hit::NONE,
    })
}

/// The idle thin-line thumb: returns 0 when there is none.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_idle_thumb(
    client: *const layout::Rect,
    dpi: u32,
    si: *const candwnd::ScrollInfo,
    out: *mut layout::Rect,
) -> i32 {
    guard(0, || match (unsafe { client.as_ref() }, unsafe { si.as_ref() }) {
        (Some(c), Some(s)) => match candwnd::idle_thumb(c, dpi, s) {
            Some(r) => {
                unsafe { set(out, r) };
                1
            }
            None => 0,
        },
        _ => 0,
    })
}

/// The arrows and pill thumb of the hovered scroll bar.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_arrows(client: *const layout::Rect, dpi: u32, out: *mut candwnd::ScrollArrows) {
    guard((), || {
        if let Some(c) = unsafe { client.as_ref() } {
            unsafe { set(out, candwnd::arrows(c, dpi)) };
        }
    })
}

/// The position while dragging the thumb: returns 0 when the thumb cannot
/// slide.
///
/// # Safety
/// The pointers must be valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_drag_pos(
    client: *const layout::Rect,
    dpi: u32,
    si: *const candwnd::ScrollInfo,
    start_pos: i32,
    delta_y: i32,
    out: *mut i32,
) -> i32 {
    guard(0, || match (unsafe { client.as_ref() }, unsafe { si.as_ref() }) {
        (Some(c), Some(s)) => match candwnd::drag_position(c, dpi, s, start_pos, delta_y) {
            Some(p) => {
                unsafe { set(out, p) };
                1
            }
            None => 0,
        },
        _ => 0,
    })
}

/// `_SetCurPos` clamp.
#[no_mangle]
pub extern "C" fn dime_ui_sb_clamp_pos(pos: i32, max: i32) -> i32 {
    guard(0, || candwnd::clamp_pos(pos, max))
}

/// The scroll bar relative to its parent.
///
/// # Safety
/// `out` must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_attached_rect(parent_w: i32, parent_h: i32, sb_width: i32, out: *mut layout::Rect) {
    guard((), || unsafe { set(out, candwnd::attached_rect(parent_w, parent_h, sb_width)) })
}

// ---------------------------------------------------------------------------
// Candidate window drawing (dime-ui::candpaint, phase 3c-1)
// ---------------------------------------------------------------------------

/// One row of the current page (`DimeCandPaintRow`).
#[repr(C)]
pub struct DimeCandPaintRow {
    pub text: *const u16,
    pub len: u32,
    pub index_char: u16,
    /// 0 when the model has no item there.
    pub has_text: u16,
}

/// `DimeCandPaintList`.
#[repr(C)]
pub struct DimeCandPaintList {
    pub font: *mut c_void,
    pub bk_brush: *mut c_void,
    pub paint: layout::Rect,
    pub draw_list: i32,
    pub wnd_width: u32,
    pub sb_width: i32,
    pub dpi: u32,
    pub rows_in_page: i32,
    /// -1: no selected row on this page.
    pub selected_row: i32,
    pub rows: *const DimeCandPaintRow,
    pub row_count: u32,
    pub colors: candpaint::Colors,
    pub has_sb_origin: i32,
    pub sb_origin: layout::Point,
}

/// `DimeCandPaintListOut`.
#[repr(C)]
pub struct DimeCandPaintListOut {
    pub old_font: *mut c_void,
    pub drew_list: i32,
    pub cx_title: i32,
    pub cy_row: i32,
    pub padding: i32,
    pub wnd_width: u32,
    pub needs_resize: i32,
    /// 0: leave the scroll bar highlight; 1: reset it; 2: reset, then set `highlight`.
    pub highlight_state: i32,
    pub highlight: layout::Rect,
    pub highlight_radius: i32,
    pub highlight_color: u32,
}

/// `DimeCandPaintIndicator`.
#[repr(C)]
pub struct DimeCandPaintIndicator {
    pub font: *mut c_void,
    pub client: layout::Rect,
    pub cy_row: i32,
    pub rows_in_page: i32,
    pub sb_width: i32,
    pub dpi: u32,
    pub current_page: u32,
    pub page_count: u32,
    pub number_color: u32,
}

/// `_OnPaint` up to the page indicator (background and `_DrawList`).
///
/// # Safety
/// `dc` must be a valid device context; `input` valid with `row_count` rows
/// whose `text` points to `len` characters; `out` valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_paint_list(
    dc: *mut c_void,
    input: *const DimeCandPaintList,
    out: *mut DimeCandPaintListOut,
) {
    guard((), || {
        let (Some(i), Some(o)) = (unsafe { input.as_ref() }, unsafe { out.as_mut() }) else {
            return;
        };
        let raw: &[DimeCandPaintRow] = if i.rows.is_null() || i.row_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(i.rows, i.row_count as usize) }
        };
        let rows: Vec<candpaint::Row> = raw
            .iter()
            .map(|r| candpaint::Row {
                index_char: r.index_char,
                text: (r.has_text != 0).then(|| unsafe { slice(r.text, r.len as usize) }),
            })
            .collect();
        let li = candpaint::ListInput {
            font: candpaint::HFONT(i.font),
            bk_brush: candpaint::HBRUSH(i.bk_brush),
            paint: i.paint,
            draw_list: i.draw_list != 0,
            wnd_width: i.wnd_width,
            sb_width: i.sb_width,
            dpi: i.dpi,
            rows_in_page: i.rows_in_page,
            rows: &rows,
            selected: usize::try_from(i.selected_row).ok(),
            colors: i.colors,
            sb_origin: (i.has_sb_origin != 0).then_some((i.sb_origin.x, i.sb_origin.y)),
        };
        let r = unsafe { candpaint::paint_list(candpaint::HDC(dc), &li) };
        o.old_font = r.old_font.0;
        o.drew_list = 0;
        o.highlight_state = 0;
        if let Some(m) = r.metrics {
            o.drew_list = 1;
            o.cx_title = m.cx_title;
            o.cy_row = m.cy_row;
            o.padding = m.padding;
            o.wnd_width = m.wnd_width;
            o.needs_resize = i32::from(m.needs_resize);
        }
        match r.highlight {
            None => {}
            Some(None) => o.highlight_state = 1,
            Some(Some(h)) => {
                o.highlight_state = 2;
                o.highlight = h.rect;
                o.highlight_radius = h.radius;
                o.highlight_color = h.color;
            }
        }
    })
}

/// `_DrawPageIndicator`.
///
/// # Safety
/// `dc` must be a valid device context; `input` valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_paint_indicator(dc: *mut c_void, input: *const DimeCandPaintIndicator) {
    guard((), || {
        if let Some(i) = unsafe { input.as_ref() } {
            let ii = candpaint::IndicatorInput {
                font: candpaint::HFONT(i.font),
                client: i.client,
                cy_row: i.cy_row,
                rows_in_page: i.rows_in_page,
                sb_width: i.sb_width,
                dpi: i.dpi,
                current_page: i.current_page,
                page_count: i.page_count,
                number_color: i.number_color,
            };
            unsafe { candpaint::paint_page_indicator(candpaint::HDC(dc), &ii) };
        }
    })
}

/// The end of `_OnPaint`: select `old_font` back.
///
/// # Safety
/// `dc` must be a valid device context.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_paint_end(dc: *mut c_void, old_font: *mut c_void) {
    guard((), || unsafe { candpaint::paint_end(candpaint::HDC(dc), candpaint::HGDIOBJ(old_font)) })
}

// ---------------------------------------------------------------------------
// Candidate window message handling (dime-ui::candproc, phase 3c-2)
// ---------------------------------------------------------------------------

/// The C++ side of the candidate window (`CCandidateWindowRustHost` in
/// `src/CandidateWindow.cpp`).  Field order must match `DimeCandHostVtbl` in
/// `dime_ffi.h` and `s_vtbl` there.  Callbacks must not throw.
#[repr(C)]
pub struct DimeCandHostVtbl {
    pub selection: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_selection_raw: unsafe extern "C" fn(ctx: *mut c_void, index: u32),
    pub set_selection_offset: unsafe extern "C" fn(ctx: *mut c_void, offset: i32),
    pub set_selection: unsafe extern "C" fn(ctx: *mut c_void, index: i32),
    pub move_page: unsafe extern "C" fn(ctx: *mut c_void, offset: i32),
    pub current_page: unsafe extern "C" fn(ctx: *mut c_void, page: *mut i32) -> i32,
    pub page_count: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub page_start: unsafe extern "C" fn(ctx: *mut c_void, page: i32) -> u32,
    pub count: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub rows_in_page: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub padding: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub cy_row: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub wnd_width: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub set_initial_metrics: unsafe extern "C" fn(ctx: *mut c_void, cx_title: i32, cy_row: i32, padding: i32),
    pub sb_width: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub font: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub bk_brush: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub has_callback: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub invoke: unsafe extern "C" fn(ctx: *mut c_void, action: i32),
    pub paint: unsafe extern "C" fn(ctx: *mut c_void, dc: *mut c_void, ps: *mut c_void),
    pub invalidate: unsafe extern "C" fn(ctx: *mut c_void),
    pub dpi_changed: unsafe extern "C" fn(ctx: *mut c_void, hwnd: *mut c_void),
    pub is_capture: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub tracking_mouse: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_tracking_mouse: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub has_shadow: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub shadow_hwnd: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub shadow_show: unsafe extern "C" fn(ctx: *mut c_void, show: i32),
    pub shadow_owner_moved: unsafe extern "C" fn(ctx: *mut c_void, resized: i32),
    pub delete_shadow: unsafe extern "C" fn(ctx: *mut c_void),
    pub has_scroll_bar: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub sb_hwnd: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub sb_client_rect: unsafe extern "C" fn(ctx: *mut c_void, out: *mut layout::Rect),
    pub sb_is_enabled: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub sb_show: unsafe extern "C" fn(ctx: *mut c_void, show: i32),
    pub sb_owner_moved: unsafe extern "C" fn(ctx: *mut c_void, resized: i32),
    pub sb_set_candidate_mouse_in: unsafe extern "C" fn(ctx: *mut c_void, inside: i32),
    pub sb_on_scroll_activity: unsafe extern "C" fn(ctx: *mut c_void),
    pub sb_shift_position: unsafe extern "C" fn(ctx: *mut c_void, position: i32),
    pub sb_mouse: unsafe extern "C" fn(ctx: *mut c_void, msg: u32, pt: layout::Point),
}

/// `DimeCandHost`.
#[repr(C)]
pub struct DimeCandHost {
    pub vtbl: *const DimeCandHostVtbl,
    pub ctx: *mut c_void,
}

struct CandHostFfi<'a> {
    v: &'a DimeCandHostVtbl,
    ctx: *mut c_void,
}

impl candproc::CandHost for CandHostFfi<'_> {
    fn selection(&mut self) -> i32 {
        unsafe { (self.v.selection)(self.ctx) }
    }
    fn set_selection_raw(&mut self, index: u32) {
        unsafe { (self.v.set_selection_raw)(self.ctx, index) }
    }
    fn set_selection_offset(&mut self, offset: i32) {
        unsafe { (self.v.set_selection_offset)(self.ctx, offset) }
    }
    fn set_selection(&mut self, index: i32) {
        unsafe { (self.v.set_selection)(self.ctx, index) }
    }
    fn move_page(&mut self, offset: i32) {
        unsafe { (self.v.move_page)(self.ctx, offset) }
    }
    fn current_page(&mut self) -> Option<i32> {
        let mut page = 0;
        (unsafe { (self.v.current_page)(self.ctx, &mut page) } != 0).then_some(page)
    }
    fn page_count(&mut self) -> u32 {
        unsafe { (self.v.page_count)(self.ctx) }
    }
    fn page_start(&mut self, page: i32) -> u32 {
        unsafe { (self.v.page_start)(self.ctx, page) }
    }
    fn count(&mut self) -> u32 {
        unsafe { (self.v.count)(self.ctx) }
    }
    fn rows_in_page(&mut self) -> Option<u32> {
        u32::try_from(unsafe { (self.v.rows_in_page)(self.ctx) }).ok()
    }
    fn padding(&mut self) -> i32 {
        unsafe { (self.v.padding)(self.ctx) }
    }
    fn cy_row(&mut self) -> i32 {
        unsafe { (self.v.cy_row)(self.ctx) }
    }
    fn wnd_width(&mut self) -> u32 {
        unsafe { (self.v.wnd_width)(self.ctx) }
    }
    fn set_initial_metrics(&mut self, cx_title: i32, cy_row: i32, padding: i32) {
        unsafe { (self.v.set_initial_metrics)(self.ctx, cx_title, cy_row, padding) }
    }
    fn sb_width(&mut self) -> i32 {
        unsafe { (self.v.sb_width)(self.ctx) }
    }
    fn font(&mut self) -> candpaint::HFONT {
        candpaint::HFONT(unsafe { (self.v.font)(self.ctx) })
    }
    fn bk_brush(&mut self) -> candpaint::HBRUSH {
        candpaint::HBRUSH(unsafe { (self.v.bk_brush)(self.ctx) })
    }
    fn has_callback(&mut self) -> bool {
        unsafe { (self.v.has_callback)(self.ctx) != 0 }
    }
    fn invoke(&mut self, action: i32) {
        unsafe { (self.v.invoke)(self.ctx, action) }
    }
    fn paint(&mut self, dc: candpaint::HDC, ps: &mut candproc::PAINTSTRUCT) {
        unsafe { (self.v.paint)(self.ctx, dc.0, ps as *mut _ as *mut c_void) }
    }
    fn invalidate(&mut self) {
        unsafe { (self.v.invalidate)(self.ctx) }
    }
    fn dpi_changed(&mut self, hwnd: candproc::HWND) {
        unsafe { (self.v.dpi_changed)(self.ctx, hwnd.0) }
    }
    fn is_capture(&mut self) -> bool {
        unsafe { (self.v.is_capture)(self.ctx) != 0 }
    }
    fn tracking_mouse(&mut self) -> bool {
        unsafe { (self.v.tracking_mouse)(self.ctx) != 0 }
    }
    fn set_tracking_mouse(&mut self, on: bool) {
        unsafe { (self.v.set_tracking_mouse)(self.ctx, i32::from(on)) }
    }
    fn has_shadow(&mut self) -> bool {
        unsafe { (self.v.has_shadow)(self.ctx) != 0 }
    }
    fn shadow_hwnd(&mut self) -> candproc::HWND {
        candproc::HWND(unsafe { (self.v.shadow_hwnd)(self.ctx) })
    }
    fn shadow_show(&mut self, show: i32) {
        unsafe { (self.v.shadow_show)(self.ctx, show) }
    }
    fn shadow_owner_moved(&mut self, resized: bool) {
        unsafe { (self.v.shadow_owner_moved)(self.ctx, i32::from(resized)) }
    }
    fn delete_shadow(&mut self) {
        unsafe { (self.v.delete_shadow)(self.ctx) }
    }
    fn has_scroll_bar(&mut self) -> bool {
        unsafe { (self.v.has_scroll_bar)(self.ctx) != 0 }
    }
    fn sb_hwnd(&mut self) -> candproc::HWND {
        candproc::HWND(unsafe { (self.v.sb_hwnd)(self.ctx) })
    }
    fn sb_client_rect(&mut self) -> layout::Rect {
        let mut r = layout::Rect::default();
        unsafe { (self.v.sb_client_rect)(self.ctx, &mut r) };
        r
    }
    fn sb_is_enabled(&mut self) -> bool {
        unsafe { (self.v.sb_is_enabled)(self.ctx) != 0 }
    }
    fn sb_show(&mut self, show: bool) {
        unsafe { (self.v.sb_show)(self.ctx, i32::from(show)) }
    }
    fn sb_owner_moved(&mut self, resized: bool) {
        unsafe { (self.v.sb_owner_moved)(self.ctx, i32::from(resized)) }
    }
    fn sb_set_candidate_mouse_in(&mut self, inside: bool) {
        unsafe { (self.v.sb_set_candidate_mouse_in)(self.ctx, i32::from(inside)) }
    }
    fn sb_on_scroll_activity(&mut self) {
        unsafe { (self.v.sb_on_scroll_activity)(self.ctx) }
    }
    fn sb_shift_position(&mut self, position: i32) {
        unsafe { (self.v.sb_shift_position)(self.ctx, position) }
    }
    fn sb_mouse(&mut self, msg: u32, pt: layout::Point) {
        unsafe { (self.v.sb_mouse)(self.ctx, msg, pt) }
    }
}

/// `CCandidateWindow::_WindowProcCallback`.  A panic returns 0.
///
/// # Safety
/// `host` must be valid with a complete vtable; `hwnd` is the candidate
/// window and `lparam` follows `msg`.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_cand_window_proc(
    host: *const DimeCandHost,
    hwnd: *mut c_void,
    msg: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    guard(0, || {
        let Some(h) = (unsafe { host.as_ref() }) else {
            return 0;
        };
        let Some(v) = (unsafe { h.vtbl.as_ref() }) else {
            return 0;
        };
        let mut ffi = CandHostFfi { v, ctx: h.ctx };
        unsafe {
            candproc::window_proc(&mut ffi, candproc::HWND(hwnd), msg, candproc::WPARAM(wparam), candproc::LPARAM(lparam)).0
        }
    })
}

// ---------------------------------------------------------------------------
// Scroll bar drawing (dime-ui::sbpaint, phase 3c-3a)
// ---------------------------------------------------------------------------

/// `DimeSbPaint`.
#[repr(C)]
pub struct DimeSbPaint {
    pub hwnd: *mut c_void,
    pub has_paint: i32,
    pub paint: layout::Rect,
    pub client: layout::Rect,
    pub dpi: u32,
    pub si: candwnd::ScrollInfo,
    pub dark: i32,
    pub full: i32,
    pub highlight: layout::Rect,
    pub highlight_radius: i32,
    pub highlight_color: u32,
}

/// `CScrollBarWindow::_OnPaint`.
///
/// # Safety
/// `dc` must be a valid device context; `input` valid, its `hwnd` the scroll bar.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_paint(dc: *mut c_void, input: *const DimeSbPaint) {
    guard((), || {
        if let Some(i) = unsafe { input.as_ref() } {
            let ii = sbpaint::Input {
                hwnd: candproc::HWND(i.hwnd),
                paint: (i.has_paint != 0).then_some(i.paint),
                client: i.client,
                dpi: i.dpi,
                si: i.si,
                dark: i.dark != 0,
                full: i.full != 0,
                highlight: i.highlight,
                highlight_radius: i.highlight_radius,
                highlight_color: i.highlight_color,
            };
            unsafe { sbpaint::paint(candpaint::HDC(dc), &ii) };
        }
    })
}

// ---------------------------------------------------------------------------
// Scroll bar events (dime-ui::sbproc, phase 3c-3b)
// ---------------------------------------------------------------------------

/// The C++ side of the scroll bar (`CScrollBarWindowRustHost` in
/// `src/ScrollBarWindow.cpp`).  Field order must match `DimeSbHostVtbl` in
/// `dime_ffi.h` and `s_vtbl` there.  Callbacks must not throw.
#[repr(C)]
pub struct DimeSbHostVtbl {
    pub hwnd: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub parent_hwnd: unsafe extern "C" fn(ctx: *mut c_void) -> *mut c_void,
    pub client_rect: unsafe extern "C" fn(ctx: *mut c_void, out: *mut layout::Rect),
    pub dpi: unsafe extern "C" fn(ctx: *mut c_void) -> u32,
    pub hover_width: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub scroll_info: unsafe extern "C" fn(ctx: *mut c_void, out: *mut candwnd::ScrollInfo),
    pub set_scroll_info: unsafe extern "C" fn(ctx: *mut c_void, si: *const candwnd::ScrollInfo),
    pub dragging: unsafe extern "C" fn(ctx: *mut c_void, start_y: *mut i32, start_pos: *mut i32) -> i32,
    pub set_dragging: unsafe extern "C" fn(ctx: *mut c_void, on: i32, start_y: i32, start_pos: i32),
    pub alpha: unsafe extern "C" fn(ctx: *mut c_void) -> u8,
    pub set_alpha: unsafe extern "C" fn(ctx: *mut c_void, alpha: u8),
    pub fading: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_fading: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub hovered: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_hovered: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub tracking_mouse: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_tracking_mouse: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub held_button: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_held_button: unsafe extern "C" fn(ctx: *mut c_void, button: i32),
    pub repeat_started: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub set_repeat_started: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub is_enabled: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub base_enable: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub is_visible: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub base_show: unsafe extern "C" fn(ctx: *mut c_void, on: i32),
    pub invalidate: unsafe extern "C" fn(ctx: *mut c_void),
    pub is_capture: unsafe extern "C" fn(ctx: *mut c_void) -> i32,
    pub start_capture: unsafe extern "C" fn(ctx: *mut c_void),
    pub end_capture: unsafe extern "C" fn(ctx: *mut c_void),
    pub start_timer: unsafe extern "C" fn(ctx: *mut c_void, elapse: u32, id: usize),
    pub end_timer: unsafe extern "C" fn(ctx: *mut c_void, id: usize),
    pub notify_command: unsafe extern "C" fn(ctx: *mut c_void, msg: u32, code: i32, pos: i32),
    pub shift_page: unsafe extern "C" fn(ctx: *mut c_void, pages: i32, notify: i32),
    pub paint: unsafe extern "C" fn(ctx: *mut c_void, dc: *mut c_void, ps: *mut c_void),
}

/// `DimeSbHost`.
#[repr(C)]
pub struct DimeSbHost {
    pub vtbl: *const DimeSbHostVtbl,
    pub ctx: *mut c_void,
}

struct SbHostFfi<'a> {
    v: &'a DimeSbHostVtbl,
    ctx: *mut c_void,
}

impl sbproc::SbHost for SbHostFfi<'_> {
    fn hwnd(&mut self) -> candproc::HWND {
        candproc::HWND(unsafe { (self.v.hwnd)(self.ctx) })
    }
    fn parent_hwnd(&mut self) -> Option<candproc::HWND> {
        let h = unsafe { (self.v.parent_hwnd)(self.ctx) };
        (!h.is_null()).then_some(candproc::HWND(h))
    }
    fn client_rect(&mut self) -> layout::Rect {
        let mut r = layout::Rect::default();
        unsafe { (self.v.client_rect)(self.ctx, &mut r) };
        r
    }
    fn dpi(&mut self) -> u32 {
        unsafe { (self.v.dpi)(self.ctx) }
    }
    fn hover_width(&mut self) -> i32 {
        unsafe { (self.v.hover_width)(self.ctx) }
    }
    fn scroll_info(&mut self) -> candwnd::ScrollInfo {
        let mut si = candwnd::ScrollInfo::default();
        unsafe { (self.v.scroll_info)(self.ctx, &mut si) };
        si
    }
    fn set_scroll_info(&mut self, si: candwnd::ScrollInfo) {
        unsafe { (self.v.set_scroll_info)(self.ctx, &si) }
    }
    fn dragging(&mut self) -> Option<(i32, i32)> {
        let (mut y, mut p) = (0, 0);
        (unsafe { (self.v.dragging)(self.ctx, &mut y, &mut p) } != 0).then_some((y, p))
    }
    fn set_dragging(&mut self, drag: Option<(i32, i32)>) {
        let (on, y, p) = drag.map_or((0, 0, 0), |(y, p)| (1, y, p));
        unsafe { (self.v.set_dragging)(self.ctx, on, y, p) }
    }
    fn alpha(&mut self) -> u8 {
        unsafe { (self.v.alpha)(self.ctx) }
    }
    fn set_alpha(&mut self, alpha: u8) {
        unsafe { (self.v.set_alpha)(self.ctx, alpha) }
    }
    fn fading(&mut self) -> bool {
        unsafe { (self.v.fading)(self.ctx) != 0 }
    }
    fn set_fading(&mut self, on: bool) {
        unsafe { (self.v.set_fading)(self.ctx, i32::from(on)) }
    }
    fn hovered(&mut self) -> bool {
        unsafe { (self.v.hovered)(self.ctx) != 0 }
    }
    fn set_hovered(&mut self, on: bool) {
        unsafe { (self.v.set_hovered)(self.ctx, i32::from(on)) }
    }
    fn tracking_mouse(&mut self) -> bool {
        unsafe { (self.v.tracking_mouse)(self.ctx) != 0 }
    }
    fn set_tracking_mouse(&mut self, on: bool) {
        unsafe { (self.v.set_tracking_mouse)(self.ctx, i32::from(on)) }
    }
    fn held_button(&mut self) -> i32 {
        unsafe { (self.v.held_button)(self.ctx) }
    }
    fn set_held_button(&mut self, button: i32) {
        unsafe { (self.v.set_held_button)(self.ctx, button) }
    }
    fn repeat_started(&mut self) -> bool {
        unsafe { (self.v.repeat_started)(self.ctx) != 0 }
    }
    fn set_repeat_started(&mut self, on: bool) {
        unsafe { (self.v.set_repeat_started)(self.ctx, i32::from(on)) }
    }
    fn is_enabled(&mut self) -> bool {
        unsafe { (self.v.is_enabled)(self.ctx) != 0 }
    }
    fn base_enable(&mut self, on: bool) {
        unsafe { (self.v.base_enable)(self.ctx, i32::from(on)) }
    }
    fn is_visible(&mut self) -> bool {
        unsafe { (self.v.is_visible)(self.ctx) != 0 }
    }
    fn base_show(&mut self, on: bool) {
        unsafe { (self.v.base_show)(self.ctx, i32::from(on)) }
    }
    fn invalidate(&mut self) {
        unsafe { (self.v.invalidate)(self.ctx) }
    }
    fn is_capture(&mut self) -> bool {
        unsafe { (self.v.is_capture)(self.ctx) != 0 }
    }
    fn start_capture(&mut self) {
        unsafe { (self.v.start_capture)(self.ctx) }
    }
    fn end_capture(&mut self) {
        unsafe { (self.v.end_capture)(self.ctx) }
    }
    fn start_timer(&mut self, elapse: u32, id: usize) {
        unsafe { (self.v.start_timer)(self.ctx, elapse, id) }
    }
    fn end_timer(&mut self, id: usize) {
        unsafe { (self.v.end_timer)(self.ctx, id) }
    }
    fn notify_command(&mut self, msg: u32, code: i32, pos: i32) {
        unsafe { (self.v.notify_command)(self.ctx, msg, code, pos) }
    }
    fn shift_page(&mut self, pages: i32, notify: bool) {
        unsafe { (self.v.shift_page)(self.ctx, pages, i32::from(notify)) }
    }
    fn paint(&mut self, dc: candpaint::HDC, ps: &mut candproc::PAINTSTRUCT) {
        unsafe { (self.v.paint)(self.ctx, dc.0, ps as *mut _ as *mut c_void) }
    }
}

/// Runs `f` with the host behind `host` (`fallback` for a null host or vtable, or a panic).
unsafe fn with_sb_host<R: Copy>(host: *const DimeSbHost, fallback: R, f: impl FnOnce(&mut SbHostFfi) -> R) -> R {
    guard(fallback, || {
        let h = unsafe { host.as_ref() };
        match h.and_then(|h| unsafe { h.vtbl.as_ref() }.map(|v| (v, h.ctx))) {
            Some((v, ctx)) => f(&mut SbHostFfi { v, ctx }),
            None => fallback,
        }
    })
}

/// Scroll bar events.  `op` picks the handler; `a` / `b` are its arguments:
/// 0 `_SetCurPos(a, b)`, 1 `_ShiftLine(a, b)`, 2 `_ShiftPage(a, b)`,
/// 3 `_ShiftPosition(a, b)`, 4 `_OnScrollActivity`, 5 `_SetCandidateMouseIn(a)`,
/// 6 `_OnLButtonDown(a, b)`, 7 `_OnLButtonUp`, 8 `_OnMouseMove`,
/// 9 `_OnTimerID(a)`, 10 `_OnOwnerWndMoved`, 11 `_Show(a)`, 12 `_Enable(a)`.
///
/// # Safety
/// `host` must be valid with a complete vtable.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_event(host: *const DimeSbHost, op: u32, a: i32, b: i32) {
    unsafe {
        with_sb_host(host, (), |h| match op {
                0 => sbproc::set_cur_pos(h, a, b),
                1 => sbproc::shift_line(h, a, b != 0),
                2 => sbproc::shift_page(h, a, b != 0),
                3 => sbproc::shift_position(h, a, b != 0),
                4 => sbproc::on_scroll_activity(h),
                5 => sbproc::set_candidate_mouse_in(h, a != 0),
                6 => sbproc::on_lbutton_down(h, layout::Point { x: a, y: b }),
                7 => sbproc::on_lbutton_up(h),
                8 => sbproc::on_mouse_move(h),
                9 => sbproc::on_timer_id(h, a as u32 as usize),
                10 => sbproc::on_owner_moved(h),
                11 => sbproc::show(h, a != 0),
                12 => sbproc::enable(h, a != 0),
                _ => {}
        });
    }
}

/// `_SetScrollInfo`.
///
/// # Safety
/// `host` must be valid with a complete vtable; `si` valid.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_set_scroll_info(host: *const DimeSbHost, si: *const candwnd::ScrollInfo) {
    let Some(si) = (unsafe { si.as_ref() }).copied() else {
        return;
    };
    unsafe { with_sb_host(host, (), |h| sbproc::set_scroll_info(h, si)) }
}

/// `CScrollBarWindow::_WindowProcCallback`.
///
/// # Safety
/// `host` must be valid with a complete vtable; `hwnd` is the scroll bar.
#[no_mangle]
pub unsafe extern "C" fn dime_ui_sb_window_proc(
    host: *const DimeSbHost,
    hwnd: *mut c_void,
    msg: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    unsafe {
        with_sb_host(host, 0, |h| {
            sbproc::window_proc(h, candproc::HWND(hwnd), msg, candproc::WPARAM(wparam), candproc::LPARAM(lparam)).0
        })
    }
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
        let code = [w("a"), w("b")];
        let code_ptrs: Vec<*const u16> = code.iter().map(|v| v.as_ptr()).collect();
        let code_lens: Vec<usize> = code.iter().map(|v| v.len()).collect();
        let starts = [0usize, 1, 3];
        let mut chosen = [-1i32; 2];
        unsafe {
            let lex = dime_lexicon_new();
            assert_eq!(dime_lexicon_load_freq_table(lex, freq.as_ptr(), freq.len()), 3);
            assert_eq!(dime_lexicon_load_cin_values(lex, words.as_ptr(), words.len()), 1);
            assert_eq!(dime_lexicon_word_count(lex), 1);
            assert_eq!(dime_lexicon_char_count(lex), 3);
            let convert = |lex, user, codes: *const *const u16, pinned: *const i32, out: *mut i32| {
                dime_sentence_convert(
                    lex,
                    user,
                    2,
                    codes,
                    if codes.is_null() {
                        ptr::null()
                    } else {
                        code_lens.as_ptr()
                    },
                    starts.as_ptr(),
                    ptrs.as_ptr(),
                    lens.as_ptr(),
                    pinned,
                    out,
                )
            };
            let none = [-1i32, -1];
            assert_eq!(
                convert(lex, ptr::null(), code_ptrs.as_ptr(), none.as_ptr(), chosen.as_mut_ptr()),
                1
            );
            assert_eq!(chosen, [0, 1]);
            let pin = [-1i32, 0];
            convert(lex, ptr::null(), code_ptrs.as_ptr(), pin.as_ptr(), chosen.as_mut_ptr());
            assert_eq!(chosen, [0, 0]);
            // A null lexicon and no codes still convert (first candidates).
            convert(ptr::null(), ptr::null(), ptr::null(), ptr::null(), chosen.as_mut_ptr());
            assert_eq!(chosen, [0, 0]);
            let bad = dime_sentence_convert(
                lex,
                ptr::null(),
                2,
                ptr::null(),
                ptr::null(),
                ptr::null(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                ptr::null(),
                chosen.as_mut_ptr(),
            );
            assert_eq!(bad, 0);

            // Learn the pinned choice (code "b" -> second candidate), export, import.
            let user = dime_user_model_new();
            let learned_from = [0i32, 0];
            let ok = dime_user_model_learn(
                user,
                2,
                code_ptrs.as_ptr(),
                code_lens.as_ptr(),
                starts.as_ptr(),
                ptrs.as_ptr(),
                lens.as_ptr(),
                pin.as_ptr(),
                learned_from.as_ptr(),
            );
            assert_eq!(ok, 1);
            assert_eq!(dime_user_model_choice_count(user), 1);
            assert_eq!(dime_user_model_word_count(user), 1);
            let buf = dime_user_model_export(user);
            let mut n = 0;
            let p = dime_buffer_data(buf, &mut n);
            let copy = dime_user_model_new();
            assert_eq!(dime_user_model_import(copy, p, n), 2);
            dime_buffer_free(buf);
            dime_user_model_merge(copy, user);
            dime_user_model_merge(copy, copy);
            assert_eq!(dime_user_model_choice_count(copy), 1);
            convert(lex, copy, code_ptrs.as_ptr(), none.as_ptr(), chosen.as_mut_ptr());
            assert_eq!(chosen[0], 0);
            dime_user_model_clear(copy);
            assert_eq!(dime_user_model_word_count(copy), 0);
            dime_user_model_free(copy);
            dime_user_model_free(user);
            dime_user_model_free(ptr::null_mut());
            assert!(dime_user_model_export(ptr::null()).is_null());
            dime_lexicon_free(lex);
            dime_lexicon_free(ptr::null_mut());
        }
    }
}
