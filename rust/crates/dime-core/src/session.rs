//! Key handler flow of `CDIME` (`KeyHandler.cpp`, `CandidateHandler.cpp`, ...).
//!
//! The handlers are ported here one group at a time.  They drive the IME
//! through [`Host`], which the C++ side implements with the TSF / UI
//! primitives it still owns (composition ranges, the candidate window, the
//! sentence buffer) and with the handlers not ported yet.  The order of the
//! host calls is the C++ order; the golden key-flow tests
//! (`src/tests/KeyFlowTest.cpp`) check the result end to end.

/// `HRESULT`.
pub type HResult = i32;
pub const S_OK: HResult = 0;
pub const S_FALSE: HResult = 1;

pub fn failed(hr: HResult) -> bool {
    hr < 0
}

/// `CANDIDATE_MODE`.
pub mod candidate_mode {
    pub const NONE: u32 = 0;
    pub const ORIGINAL: u32 = 1;
    pub const PHRASE: u32 = 2;
    pub const INCREMENTAL: u32 = 3;
}

/// `IME_MODE`.
pub mod ime_mode {
    pub const DAYI: u32 = 0;
    pub const ARRAY: u32 = 1;
    pub const PHONETIC: u32 = 2;
}

/// `ARRAY_SCOPE::ARRAY40_BIG5`.
pub const ARRAY40_BIG5: u32 = 4;

/// `BEEP_TYPE`.
pub mod beep {
    pub const COMPOSITION_ERROR: u32 = 0;
    pub const WARNING: u32 = 1;
    pub const ON_CANDI: u32 = 2;
}

/// Where the insertion point is relative to the composition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Selection {
    /// No selection could be read.
    Unavailable,
    /// Outside the composition (the key is ignored).
    Outside,
    /// Inside the composition, or there is no composition range.
    Covered,
}

/// `NOTIFY_TYPE`.
pub mod notify {
    pub const SPECIAL_CODE: u32 = 4;
}

/// An empty position in the Array short-code table (U+2394).
const ARRAY_SHORT_CODE_EMPTY: u16 = 0x2394;

/// `IME_SHIFT_ENGLISH_DIGIT_MODE`.
pub mod shift_english_digit {
    pub const CAPS_OFF_DIGIT: u32 = 1;
    pub const ALWAYS_SYMBOL: u32 = 2;
}

/// What the handlers need from the C++ text service.
pub trait Host {
    // --- state ---
    fn candidate_mode(&self) -> u32;
    fn is_composing(&self) -> bool;
    fn is_full_shape(&self) -> bool;
    fn virtual_key_length(&self) -> usize;
    fn is_sentence_pending(&self) -> bool;
    fn caps_lock_on(&self) -> bool;
    /// `CConfig::GetShiftEnglishDigitMode()`.
    fn shift_english_digit_mode(&self) -> u32;
    /// `CConfig::getDayiArticleMode()`.
    fn dayi_article_mode(&self) -> bool;
    /// `iswalpha`.
    fn is_alpha(&self, c: u16) -> bool;
    /// The base (CapsLock off) or shifted (CapsLock on) character on the key
    /// that produces `c` in the current keyboard layout, if any.
    fn layout_char(&self, c: u16, caps_lock_on: bool) -> Option<u16>;

    // --- TSF / UI primitives ---
    fn add_char_and_finalize(&mut self, text: &[u16]) -> HResult;
    fn remove_dummy_composition(&mut self);
    fn delete_candidate_list(&mut self, force: bool);
    fn terminate_composition(&mut self);

    // --- state (candidate handling) ---
    fn has_ui_presenter(&self) -> bool;
    fn is_candidate_with_wildcard(&self) -> bool;
    fn sentence_pin_next(&self) -> bool;
    fn set_sentence_pin_next(&mut self, pin: bool);
    fn is_sentence_mode(&self) -> bool;
    /// `Global::imeMode`.
    fn ime_mode(&self) -> u32;
    fn array_scope(&self) -> u32;
    fn clear_on_beep(&self) -> bool;
    fn array_force_sp(&self) -> bool;
    fn array_notify_sp(&self) -> bool;
    fn do_han_convert(&self) -> bool;
    fn make_phrase(&self) -> bool;
    fn is_cmd_shell(&self) -> bool;
    fn is_ui_less_mode(&self) -> bool;
    /// A reverse-conversion provider is loaded and configured for this mode.
    fn reverse_conversion_active(&self) -> bool;
    fn is_escape_input_leading(&self) -> bool;
    fn is_array_short_code(&self) -> bool;

    // --- candidate window ---
    /// The highlighted candidate (`_GetSelectedCandidateString`).
    fn selected_candidate(&self) -> Vec<u16>;
    fn candidate_selection(&self) -> i32;
    /// `GetCandidateListIndexRange()->GetIndex(code, wch, modifiers, candidateMode)`.
    fn selkey_index(&self, code: u32, wch: u16) -> i32;
    fn set_candidate_selection_in_page(&mut self, index: i32) -> bool;
    fn advise_ui_changed_by_arrow_key(&mut self, function: u32);
    /// Array: the 2-key buffer is the special code of the highlighted candidate
    /// (`CollectWordFromArraySpeicalCode`).
    fn array_special_code_matches(&mut self) -> bool;
    /// `GetArraySpeicalCodeFromConvertedText`: the special code's key names.
    fn array_special_code_for(&mut self, text: &[u16]) -> Option<Vec<u16>>;
    /// `GetSCFromTC` (the text itself when there is no conversion).
    fn sc_from_tc(&mut self, text: &[u16]) -> Vec<u16>;
    fn show_notify(&mut self, text: &[u16], kind: u32);
    /// Show the highlighted wildcard candidate's key code as radicals.
    fn notify_selected_key_code(&mut self);
    /// Look up associated phrases for `last_char`; if any, show them as a
    /// phrase candidate list (mode `CANDIDATE_PHRASE`) and return true.
    fn show_associated_phrases(&mut self, last_char: u16) -> bool;

    // --- more TSF primitives ---
    fn start_composition(&mut self);
    /// `_AddComposingAndChar`; `None` passes a null string (an unavailable
    /// dictionary leaves the reading string null).
    fn add_composing_and_char(&mut self, text: Option<&[u16]>) -> HResult;
    /// `_commitString` (read by the reverse conversion).
    fn set_commit_string(&mut self, text: &[u16]);
    fn async_reverse_conversion(&mut self);
    fn beep(&mut self, kind: u32);

    // --- composing ---
    /// The default selection and whether the composition covers it.  `strict`
    /// treats any result but `S_OK` as unavailable (`_HandleCompositionInput`);
    /// otherwise only failures (`_HandleCompositionBackspace`).
    fn selection(&mut self, strict: bool) -> Selection;
    fn add_virtual_key(&mut self, wch: u16) -> bool;
    fn remove_virtual_key(&mut self, index: usize);
    fn is_escape_input(&self) -> bool;
    /// `GetReadingString`: the reading string (`None` when it stays null) and
    /// the wildcard flag (`None` when it is left untouched).
    fn reading_string(&mut self) -> (Option<Vec<u16>>, Option<bool>);
    fn auto_compose(&self) -> bool;
    fn do_beep_on_candi(&self) -> bool;
    fn set_candidate_mode(&mut self, mode: u32);
    fn set_candidate_with_wildcard(&mut self, wildcard: bool);
    /// `GetCandidateList` into the host's list; returns the count.
    fn fetch_candidates(&mut self, incremental: bool, wildcard: bool, array_phrase_ending: bool) -> u32;
    /// `_SetCandidateText` with the fetched list.
    fn set_candidate_text(&mut self);
    fn clear_candidate_list(&mut self);
    fn hide_candidate_window(&mut self);
    fn create_and_start_candidate(&mut self) -> HResult;
    /// Arrow keys inside a composition: the incremental candidate list moves
    /// and the selection is put back.
    fn composition_arrow_key(&mut self, function: u32);

    // --- handlers still in C++ ---
    fn sentence_select_candidate(&mut self, text: &[u16], explicit: bool) -> HResult;
    fn sentence_render(&mut self, reading: &[u16]) -> HResult;
    /// `_SentenceAppendFromCandidateList` with the fetched list.
    fn sentence_append_fetched(&mut self, index: i32) -> HResult;
    fn sentence_commit_before_output(&mut self);
    fn sentence_close_reading(&mut self) -> HResult;
    fn sentence_reset(&mut self);
}

/// `Global::FullWidthCharTable[c - 0x20]` for printable ASCII.
pub fn full_width(c: u16) -> u16 {
    match c {
        0x20 => 0x3000,
        0x21..=0x7E => 0xFF00 + (c - 0x20),
        _ => c,
    }
}

/// `Global::dayiAddressCharTable` / `dayiArticleCharTable`: key, address
/// character, article-mode punctuation.
const DAYI_ADDRESS_TABLE: [(u16, u16, u16); 6] = [
    (b'`' as u16, 0x5DF7, 0xFF1A),  // 巷 ：
    (b'\'' as u16, 0x865F, 0xFF0C), // 號 ，
    (b'[' as u16, 0x8DEF, 0x3002),  // 路 。
    (b']' as u16, 0x8857, 0xFF1F),  // 街 ？
    (b'-' as u16, 0x9109, 0x3001),  // 鄉 、
    (b'\\' as u16, 0x93AE, 0xFF1B), // 鎮 ；
];

/// `CCompositionProcessorEngine::GetDayiAddressChar`.
pub fn dayi_address_char(c: u16, article_mode: bool) -> u16 {
    DAYI_ADDRESS_TABLE
        .iter()
        .find(|(k, _, _)| *k == c)
        .map_or(0, |&(_, address, article)| if article_mode { article } else { address })
}

/// `_HandleComplete`.
pub fn handle_complete(h: &mut impl Host) -> HResult {
    h.delete_candidate_list(false);
    h.terminate_composition();
    S_OK
}

/// `_HandleCancel`.  In sentence mode, cancelling a reading or a candidate
/// window keeps the sentence; cancelling with nothing else pending discards it.
pub fn handle_cancel(h: &mut impl Host) -> HResult {
    if h.is_sentence_pending() {
        if h.virtual_key_length() > 0 || h.candidate_mode() != candidate_mode::NONE {
            return h.sentence_close_reading();
        }
        h.sentence_reset();
    }
    h.remove_dummy_composition();
    h.delete_candidate_list(true);
    h.terminate_composition();
    S_OK
}

/// Output one character, then end the composition (the tail shared by the
/// direct-output handlers).
fn output_char(h: &mut impl Host, c: u16) -> HResult {
    let hr = h.add_char_and_finalize(&[c]);
    if failed(hr) {
        return hr;
    }
    handle_cancel(h);
    S_OK
}

/// `_HandleCompositionDoubleSingleByte`: full-width output of printable ASCII.
pub fn handle_double_single_byte(h: &mut impl Host, wch: u16) -> HResult {
    h.sentence_commit_before_output();
    output_char(h, full_width(wch))
}

/// `_HandleCompositionShiftEnglishInput`: Shift + printable key outside a
/// composition.  CapsLock off gives the base character (a, 1, -), on gives the
/// shifted one (A, !, _); the digit row follows `ShiftEnglishDigitMode`.
pub fn handle_shift_english_input(h: &mut impl Host, code: u32, wch: u16) -> HResult {
    let was_phrase_candidate = h.candidate_mode() == candidate_mode::PHRASE;
    if h.candidate_mode() != candidate_mode::NONE {
        handle_candidate_finalize(h);
    }
    h.sentence_commit_before_output();

    let caps = h.caps_lock_on();
    let mut out = wch;
    if h.is_alpha(wch) {
        // C-locale towupper / towlower: ASCII letters only.
        out = if caps {
            if (u16::from(b'a')..=u16::from(b'z')).contains(&wch) {
                wch - 32
            } else {
                wch
            }
        } else if (u16::from(b'A')..=u16::from(b'Z')).contains(&wch) {
            wch + 32
        } else {
            wch
        };
    } else if (u32::from(b'0')..=u32::from(b'9')).contains(&code) {
        let symbol = wch;
        let digit = code as u16;
        out = match h.shift_english_digit_mode() {
            shift_english_digit::CAPS_OFF_DIGIT => {
                if caps {
                    symbol
                } else {
                    digit
                }
            }
            shift_english_digit::ALWAYS_SYMBOL => symbol,
            _ => {
                if caps {
                    digit
                } else {
                    symbol
                }
            }
        };
    } else if !was_phrase_candidate {
        if let Some(c) = h.layout_char(wch, caps) {
            out = c;
        }
    }

    if h.is_full_shape() && (0x20..=0x7E).contains(&out) {
        out = full_width(out);
    }
    let hr = h.add_char_and_finalize(&[out]);
    if failed(hr) {
        return hr;
    }
    handle_cancel(h);
    hr
}

/// `_HandleCompositionAddressChar`: Dayi address keys (or article-mode punctuation).
pub fn handle_address_char(h: &mut impl Host, wch: u16) -> HResult {
    h.sentence_commit_before_output();
    if h.is_composing() {
        handle_cancel(h);
    }
    output_char(h, dayi_address_char(wch, h.dayi_article_mode()))
}

/// `_HandleCandidateFinalize` / `_HandleCandidateConvert` (both are `_HandleCandidateWorker`).
pub fn handle_candidate_finalize(h: &mut impl Host) -> HResult {
    handle_candidate_worker(h)
}

/// Beep for a key that has no mapping; Phonetic only warns, the others may
/// also clear the composition (`ClearOnBeep`).
fn beep_no_mapping(h: &mut impl Host) {
    if h.ime_mode() == ime_mode::PHONETIC {
        h.beep(beep::WARNING);
    } else {
        if h.clear_on_beep() {
            handle_cancel(h);
        }
        h.beep(beep::COMPOSITION_ERROR);
    }
}

/// `_HandleCandidateWorker`: commit the highlighted candidate (or add it to
/// the sentence), with the reverse-lookup / special-code notifications, the
/// TC to SC conversion and the associated phrases that follow.
pub fn handle_candidate_worker(h: &mut impl Host) -> HResult {
    if !h.has_ui_presenter() {
        return S_OK;
    }
    if !h.is_composing() {
        h.start_composition();
    }
    let candidate = h.selected_candidate();

    let array30 = h.ime_mode() == ime_mode::ARRAY && h.array_scope() != ARRAY40_BIG5;
    let using_special_code = array30 && h.array_special_code_matches();

    if candidate.is_empty() {
        if h.candidate_mode() == candidate_mode::PHRASE {
            handle_cancel(h);
            return S_OK;
        }
        handle_cancel(h);
        h.beep(beep::COMPOSITION_ERROR);
        return S_FALSE;
    }

    // Sentence mode: the candidate joins (or edits) the pending sentence; it is
    // pinned when the user picked it explicitly.
    if h.is_sentence_mode() && h.candidate_mode() != candidate_mode::PHRASE {
        let explicit = h.sentence_pin_next()
            || h.is_candidate_with_wildcard()
            || h.candidate_selection() > 0
            || h.is_escape_input_leading();
        h.set_sentence_pin_next(false);
        return h.sentence_select_candidate(&candidate, explicit);
    }

    if h.is_array_short_code() && candidate.len() == 1 && candidate[0] == ARRAY_SHORT_CODE_EMPTY {
        beep_no_mapping(h);
        return S_FALSE;
    }

    h.set_commit_string(&candidate);
    let last_char = *candidate.last().unwrap_or(&0);

    let ui_less = h.is_ui_less_mode();
    if !ui_less {
        if h.reverse_conversion_active() {
            h.async_reverse_conversion();
        } else if h.is_candidate_with_wildcard() {
            h.notify_selected_key_code();
        }
    }

    let mut special_code_found = false;
    if array30 && !ui_less && !using_special_code && (h.array_force_sp() || h.array_notify_sp()) {
        if let Some(code) = h.array_special_code_for(&candidate) {
            special_code_found = true;
            h.show_notify(&code, notify::SPECIAL_CODE);
        }
    }

    let converted = if h.do_han_convert() {
        h.sc_from_tc(&candidate)
    } else {
        candidate.clone()
    };

    if h.ime_mode() == ime_mode::ARRAY && !ui_less && !using_special_code && h.array_force_sp() && special_code_found {
        handle_cancel(h);
        h.beep(beep::WARNING);
        return S_OK;
    }
    let hr = h.add_composing_and_char(Some(&converted));
    if failed(hr) {
        return hr;
    }
    handle_complete(h);

    // Associated phrases (off in the console: issue #27).
    if h.make_phrase() && !h.is_cmd_shell() {
        if !h.show_associated_phrases(last_char) {
            handle_cancel(h);
        }
    } else {
        h.delete_candidate_list(true);
    }
    hr
}

/// `_HandleCandidateArrowKey` / `_HandlePhraseArrowKey`.
pub fn handle_candidate_arrow_key(h: &mut impl Host, function: u32) -> HResult {
    h.advise_ui_changed_by_arrow_key(function);
    S_OK
}

/// `_HandleCandidateSelectByNumber`: a select key picks (and in sentence
/// mode pins) a candidate.
pub fn handle_candidate_select_by_number(h: &mut impl Host, code: u32, wch: u16) -> HResult {
    let index = h.selkey_index(code, wch);
    if index == -1 {
        return S_FALSE;
    }
    if h.has_ui_presenter() && h.set_candidate_selection_in_page(index) {
        h.set_sentence_pin_next(true);
        let hr = handle_candidate_finalize(h);
        h.set_sentence_pin_next(false);
        return hr;
    }
    S_FALSE
}

/// `_HandlePhraseFinalize`: commit the highlighted associated phrase.
pub fn handle_phrase_finalize(h: &mut impl Host) -> HResult {
    let phrase = h.selected_candidate();
    if !phrase.is_empty() {
        let hr = h.add_composing_and_char(Some(&phrase));
        if hr != S_OK {
            return hr;
        }
    }
    handle_complete(h);
    S_OK
}

/// `_HandlePhraseSelectByNumber`.
pub fn handle_phrase_select_by_number(h: &mut impl Host, code: u32, wch: u16) -> HResult {
    let index = h.selkey_index(code, wch);
    if index == -1 {
        return S_FALSE;
    }
    if h.has_ui_presenter() && h.set_candidate_selection_in_page(index) {
        return handle_phrase_finalize(h);
    }
    S_FALSE
}

/// `_HandleCompositionInput`: a radical key.
pub fn handle_composition_input(h: &mut impl Host, wch: u16) -> HResult {
    let mode = h.candidate_mode();
    if h.has_ui_presenter() && mode != candidate_mode::INCREMENTAL && mode != candidate_mode::NONE {
        handle_composition_finalize(h, true);
    }
    if !h.is_composing() {
        h.start_composition();
    }
    match h.selection(true) {
        Selection::Unavailable => return S_FALSE,
        Selection::Outside => return S_OK,
        Selection::Covered => {}
    }
    if h.add_virtual_key(wch) {
        handle_composition_input_worker(h);
    } else if h.ime_mode() == ime_mode::PHONETIC {
        h.beep(beep::WARNING);
    } else {
        // Over the code length limit or the like.
        if h.clear_on_beep() {
            handle_cancel(h);
        }
        h.beep(beep::COMPOSITION_ERROR);
    }
    S_OK
}

/// `_HandleCompositionInputWorker`: show the reading string and, with
/// AutoCompose, the incremental candidates.
pub fn handle_composition_input_worker(h: &mut impl Host) -> HResult {
    let (reading, wildcard) = h.reading_string();
    let wildcard = wildcard.unwrap_or(true);
    let hr = if h.is_sentence_pending() {
        h.sentence_render(reading.as_deref().unwrap_or(&[]))
    } else {
        h.add_composing_and_char(reading.as_deref())
    };
    if failed(hr) {
        return hr;
    }
    let mut hr = hr;
    if !h.auto_compose() {
        return hr;
    }
    if h.fetch_candidates(true, wildcard, false) > 0 {
        hr = h.create_and_start_candidate();
        if !failed(hr) {
            h.clear_candidate_list();
            h.set_candidate_text();
            h.set_candidate_mode(candidate_mode::INCREMENTAL);
            h.set_candidate_with_wildcard(false);
        }
    } else if h.has_ui_presenter() {
        // No candidates: hide the window.
        h.clear_candidate_list();
        h.hide_candidate_window();
    } else if reading.as_ref().is_some_and(|r| !r.is_empty()) && wildcard {
        hr = h.create_and_start_candidate();
        if !failed(hr) {
            h.clear_candidate_list();
            h.set_candidate_mode(candidate_mode::INCREMENTAL);
            h.set_candidate_with_wildcard(false);
        }
    }
    hr
}

/// `_HandleCompositionFinalize`.
pub fn handle_composition_finalize(h: &mut impl Host, is_candidate_list: bool) -> HResult {
    if h.is_sentence_mode() {
        // A chosen candidate joins the sentence; anything else commits it first.
        if is_candidate_list && h.has_ui_presenter() {
            return handle_candidate_worker(h);
        }
        if !is_candidate_list {
            h.sentence_commit_before_output();
        }
    }
    if is_candidate_list && h.has_ui_presenter() {
        let candidate = h.selected_candidate();
        if !candidate.is_empty() {
            let hr = h.add_char_and_finalize(&candidate);
            if failed(hr) {
                return hr;
            }
        } else {
            handle_cancel(h);
        }
    } else if h.is_composing() {
        handle_cancel(h);
    }
    S_OK
}

/// `_HandleCompositionConvert`: open the candidate window for the reading
/// (or, in sentence mode, add the character to the sentence).
pub fn handle_composition_convert(h: &mut impl Host, wildcard: bool, array_phrase_ending: bool) -> HResult {
    if h.is_sentence_mode()
        && !wildcard
        && h.virtual_key_length() > 0
        && !h.is_escape_input_leading()
        && h.fetch_candidates(false, false, false) > 0
    {
        return h.sentence_append_fetched(-1);
    }
    let count = h.fetch_candidates(false, wildcard, array_phrase_ending);
    if count > 0 {
        if !failed(h.create_and_start_candidate()) {
            h.set_candidate_mode(candidate_mode::ORIGINAL);
            h.set_candidate_with_wildcard(wildcard);
            h.clear_candidate_list();
            h.set_candidate_text();
        }
    } else {
        beep_no_mapping(h);
    }
    if count == 1 {
        // The only candidate is committed without showing the window.
        handle_candidate_finalize(h);
    } else if h.ime_mode() == ime_mode::DAYI && h.do_beep_on_candi() {
        h.beep(beep::ON_CANDI);
    }
    S_OK
}

/// `_HandleCompositionBackspace`.
pub fn handle_composition_backspace(h: &mut impl Host) -> HResult {
    if !h.is_composing() {
        return S_OK;
    }
    match h.selection(false) {
        Selection::Unavailable => return S_FALSE,
        Selection::Outside => return S_OK,
        Selection::Covered => {}
    }
    let len = h.virtual_key_length();
    if len > 0 {
        let symbol_mode = h.is_escape_input();
        h.remove_virtual_key(len - 1);
        let remaining = h.virtual_key_length() > 0 && !symbol_mode;
        if h.is_sentence_pending() {
            // The sentence stays when the reading is emptied.
            if remaining {
                handle_composition_input_worker(h);
            } else {
                h.sentence_close_reading();
            }
        } else if remaining && !(h.ime_mode() == ime_mode::PHONETIC && h.candidate_mode() != candidate_mode::NONE) {
            handle_composition_input_worker(h);
        } else {
            handle_cancel(h);
        }
    }
    S_OK
}

/// `_HandleCompositionArrowKey`.
pub fn handle_composition_arrow_key(h: &mut impl Host, function: u32) -> HResult {
    h.composition_arrow_key(function);
    S_OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Default)]
    struct Mock {
        calls: Vec<String>,
        candidate_mode: u32,
        composing: bool,
        full_shape: bool,
        caps: bool,
        sentence_pending: bool,
        vk_len: usize,
        selected: Vec<u16>,
        make_phrase: bool,
        phrases: bool,
        fetched: u32,
    }

    impl Host for Mock {
        fn candidate_mode(&self) -> u32 {
            self.candidate_mode
        }
        fn is_composing(&self) -> bool {
            self.composing
        }
        fn is_full_shape(&self) -> bool {
            self.full_shape
        }
        fn virtual_key_length(&self) -> usize {
            self.vk_len
        }
        fn is_sentence_pending(&self) -> bool {
            self.sentence_pending
        }
        fn caps_lock_on(&self) -> bool {
            self.caps
        }
        fn shift_english_digit_mode(&self) -> u32 {
            0
        }
        fn dayi_article_mode(&self) -> bool {
            false
        }
        fn is_alpha(&self, c: u16) -> bool {
            char::from_u32(c.into()).is_some_and(char::is_alphabetic)
        }
        fn layout_char(&self, c: u16, _caps: bool) -> Option<u16> {
            (c == u16::from(b'_')).then_some(u16::from(b'-'))
        }
        fn add_char_and_finalize(&mut self, text: &[u16]) -> HResult {
            self.calls.push(format!("commit {}", String::from_utf16_lossy(text)));
            S_OK
        }
        fn remove_dummy_composition(&mut self) {
            self.calls.push("remove_dummy".into());
        }
        fn delete_candidate_list(&mut self, force: bool) {
            self.calls.push(format!("delete_candidates {force}"));
        }
        fn terminate_composition(&mut self) {
            self.calls.push("terminate".into());
        }
        fn has_ui_presenter(&self) -> bool {
            true
        }
        fn is_candidate_with_wildcard(&self) -> bool {
            false
        }
        fn sentence_pin_next(&self) -> bool {
            false
        }
        fn set_sentence_pin_next(&mut self, _pin: bool) {}
        fn is_sentence_mode(&self) -> bool {
            false
        }
        fn ime_mode(&self) -> u32 {
            0
        }
        fn array_scope(&self) -> u32 {
            0
        }
        fn clear_on_beep(&self) -> bool {
            true
        }
        fn array_force_sp(&self) -> bool {
            false
        }
        fn array_notify_sp(&self) -> bool {
            false
        }
        fn do_han_convert(&self) -> bool {
            false
        }
        fn make_phrase(&self) -> bool {
            self.make_phrase
        }
        fn is_cmd_shell(&self) -> bool {
            false
        }
        fn is_ui_less_mode(&self) -> bool {
            false
        }
        fn reverse_conversion_active(&self) -> bool {
            false
        }
        fn is_escape_input_leading(&self) -> bool {
            false
        }
        fn is_array_short_code(&self) -> bool {
            false
        }
        fn selected_candidate(&self) -> Vec<u16> {
            self.selected.clone()
        }
        fn candidate_selection(&self) -> i32 {
            0
        }
        fn selkey_index(&self, _code: u32, _wch: u16) -> i32 {
            0
        }
        fn set_candidate_selection_in_page(&mut self, _index: i32) -> bool {
            true
        }
        fn advise_ui_changed_by_arrow_key(&mut self, _function: u32) {}
        fn array_special_code_matches(&mut self) -> bool {
            false
        }
        fn array_special_code_for(&mut self, _text: &[u16]) -> Option<Vec<u16>> {
            None
        }
        fn sc_from_tc(&mut self, text: &[u16]) -> Vec<u16> {
            text.to_vec()
        }
        fn show_notify(&mut self, _text: &[u16], _kind: u32) {}
        fn notify_selected_key_code(&mut self) {}
        fn show_associated_phrases(&mut self, last_char: u16) -> bool {
            self.calls
                .push(format!("phrases {}", String::from_utf16_lossy(&[last_char])));
            self.phrases
        }
        fn start_composition(&mut self) {
            self.calls.push("start".into());
        }
        fn add_composing_and_char(&mut self, text: Option<&[u16]>) -> HResult {
            self.calls
                .push(format!("composing {}", String::from_utf16_lossy(text.unwrap_or(&[]))));
            S_OK
        }
        fn selection(&mut self, _strict: bool) -> Selection {
            Selection::Covered
        }
        fn add_virtual_key(&mut self, wch: u16) -> bool {
            self.calls.push(format!("add_key {}", String::from_utf16_lossy(&[wch])));
            true
        }
        fn remove_virtual_key(&mut self, _index: usize) {
            self.vk_len -= 1;
            self.calls.push("remove_key".into());
        }
        fn is_escape_input(&self) -> bool {
            false
        }
        fn reading_string(&mut self) -> (Option<Vec<u16>>, Option<bool>) {
            (Some("r".encode_utf16().collect()), Some(false))
        }
        fn auto_compose(&self) -> bool {
            false
        }
        fn do_beep_on_candi(&self) -> bool {
            false
        }
        fn set_candidate_mode(&mut self, mode: u32) {
            self.candidate_mode = mode;
        }
        fn set_candidate_with_wildcard(&mut self, _wildcard: bool) {}
        fn fetch_candidates(&mut self, _incremental: bool, _wildcard: bool, _ending: bool) -> u32 {
            self.fetched
        }
        fn set_candidate_text(&mut self) {
            self.calls.push("candidate_text".into());
        }
        fn clear_candidate_list(&mut self) {}
        fn hide_candidate_window(&mut self) {}
        fn create_and_start_candidate(&mut self) -> HResult {
            S_OK
        }
        fn composition_arrow_key(&mut self, _function: u32) {}
        fn sentence_render(&mut self, _reading: &[u16]) -> HResult {
            S_OK
        }
        fn sentence_append_fetched(&mut self, _index: i32) -> HResult {
            S_OK
        }
        fn set_commit_string(&mut self, _text: &[u16]) {}
        fn async_reverse_conversion(&mut self) {}
        fn beep(&mut self, kind: u32) {
            self.calls.push(format!("beep {kind}"));
        }
        fn sentence_select_candidate(&mut self, _text: &[u16], _explicit: bool) -> HResult {
            S_OK
        }
        fn sentence_commit_before_output(&mut self) {
            self.calls.push("sentence_commit".into());
        }
        fn sentence_close_reading(&mut self) -> HResult {
            self.calls.push("sentence_close_reading".into());
            S_OK
        }
        fn sentence_reset(&mut self) {
            self.calls.push("sentence_reset".into());
        }
    }

    #[test]
    fn cancel_keeps_sentence_while_reading() {
        let mut m = Mock {
            sentence_pending: true,
            vk_len: 1,
            ..Default::default()
        };
        handle_cancel(&mut m);
        assert_eq!(m.calls, ["sentence_close_reading"]);
    }

    #[test]
    fn shift_english_base_char_and_full_shape() {
        let mut m = Mock {
            full_shape: true,
            ..Default::default()
        };
        handle_shift_english_input(&mut m, 0xBD, u16::from(b'_'));
        assert_eq!(m.calls[1], "commit \u{FF0D}");
        assert_eq!(&m.calls[2..], ["remove_dummy", "delete_candidates true", "terminate"]);
    }

    #[test]
    fn candidate_commit_then_associated_phrases() {
        let mut m = Mock {
            composing: true,
            selected: "東西".encode_utf16().collect(),
            make_phrase: true,
            phrases: true,
            ..Default::default()
        };
        assert_eq!(handle_candidate_worker(&mut m), S_OK);
        assert_eq!(
            m.calls,
            ["composing 東西", "delete_candidates false", "terminate", "phrases 西"]
        );
    }

    #[test]
    fn empty_candidate_cancels_and_beeps() {
        let mut m = Mock {
            composing: true,
            ..Default::default()
        };
        assert_eq!(handle_candidate_worker(&mut m), S_FALSE);
        assert_eq!(m.calls.last().map(String::as_str), Some("beep 0"));
    }

    #[test]
    fn convert_opens_window_or_commits_unique() {
        let mut m = Mock {
            composing: true,
            fetched: 3,
            vk_len: 1,
            ..Default::default()
        };
        handle_composition_convert(&mut m, false, false);
        assert_eq!(m.candidate_mode, candidate_mode::ORIGINAL);
        assert_eq!(m.calls, ["candidate_text"]);
    }

    #[test]
    fn backspace_on_last_key_cancels() {
        let mut m = Mock {
            composing: true,
            vk_len: 1,
            ..Default::default()
        };
        handle_composition_backspace(&mut m);
        assert_eq!(
            m.calls,
            ["remove_key", "remove_dummy", "delete_candidates true", "terminate"]
        );
    }

    #[test]
    fn address_and_full_width_tables() {
        assert_eq!(dayi_address_char(u16::from(b'\''), false), 0x865F);
        assert_eq!(dayi_address_char(u16::from(b'['), true), 0x3002);
        assert_eq!(dayi_address_char(u16::from(b'a'), false), 0);
        assert_eq!(full_width(u16::from(b' ')), 0x3000);
        assert_eq!(full_width(u16::from(b'~')), 0xFF5E);
    }
}
