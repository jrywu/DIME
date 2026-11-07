//! Key handler flow of `CDIME` (`KeyHandler.cpp`, `CandidateHandler.cpp`, ...).
//!
//! The handlers are ported here one group at a time.  They drive the IME
//! through [`Host`], which the C++ side implements with the TSF / UI
//! primitives it still owns (composition ranges, the candidate window, the
//! sentence buffer) and with the handlers not ported yet.  The order of the
//! host calls is the C++ order; the golden key-flow tests
//! (`src/tests/KeyFlowTest.cpp`) check the result end to end.

use crate::sentence_buffer::{SentenceBuffer, MAX_POSITIONS};

/// IME state owned by Rust (the rest still lives in `CDIME`).
#[derive(Debug, Default)]
pub struct SessionState {
    /// The pending sentence of sentence mode.
    pub sentence: SentenceBuffer,
    /// Position being edited through the candidate window, or -1.
    pub sentence_edit_pos: i32,
    /// The next candidate finalize came from a select key (pins the character).
    pub sentence_pin_next: bool,
}

impl SessionState {
    pub fn new() -> Self {
        Self {
            sentence: SentenceBuffer::new(),
            sentence_edit_pos: -1,
            sentence_pin_next: false,
        }
    }

    /// `_SentenceReset` (the Rust part).
    pub fn reset_sentence(&mut self) {
        self.sentence.clear();
        self.sentence_edit_pos = -1;
        self.sentence_pin_next = false;
    }
}

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
    // --- sentence mode ---
    fn state(&mut self) -> &mut SessionState;
    fn state_ref(&self) -> &SessionState;
    /// Replace the composition text with `text` and put the caret at `caret`
    /// (`_SentenceWrite`).
    fn sentence_write(&mut self, text: &[u16], caret: usize, with_attributes: bool) -> HResult;
    /// Re-convert the sentence with the lexicon and the user model (loaded on first use).
    fn convert_sentence(&mut self);
    /// Learn from the sentence about to be committed and save (`_SentenceLearn`).
    fn sentence_learn(&mut self);
    /// The C++ part of `_SentenceReset`: the displayed text and `lastReadingString`.
    fn sentence_clear_display(&mut self);
    /// The keys typed so far (`_GetKeystrokeCode`).
    fn keystroke_code(&self) -> Vec<u16>;
    /// The item strings of the fetched candidate list.
    fn fetched_strings(&self) -> Vec<Vec<u16>>;
    /// Show the candidates of sentence position `pos` (key code: the
    /// position's code) and highlight the chosen one.  The window keeps
    /// pointers into the position's strings.
    fn show_sentence_candidates(&mut self, pos: usize);
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
    if is_sentence_pending(h) {
        if h.virtual_key_length() > 0 || h.candidate_mode() != candidate_mode::NONE {
            return sentence_close_reading(h);
        }
        sentence_reset(h);
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
    sentence_commit_before_output(h);
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
    sentence_commit_before_output(h);

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
    sentence_commit_before_output(h);
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
        let explicit = h.state_ref().sentence_pin_next
            || h.is_candidate_with_wildcard()
            || h.candidate_selection() > 0
            || h.is_escape_input_leading();
        h.state().sentence_pin_next = false;
        return sentence_select_candidate(h, &candidate, explicit);
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
        h.state().sentence_pin_next = true;
        let hr = handle_candidate_finalize(h);
        h.state().sentence_pin_next = false;
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
    let hr = if is_sentence_pending(h) {
        sentence_render(h, reading.as_deref().unwrap_or(&[]))
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
            sentence_commit_before_output(h);
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
        return sentence_append_fetched(h, -1);
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
        if is_sentence_pending(h) {
            // The sentence stays when the reading is emptied.
            if remaining {
                handle_composition_input_worker(h);
            } else {
                sentence_close_reading(h);
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

// --- sentence mode (SentenceHandler.cpp) ---------------------------------------

/// `VK_DELETE`.
const VK_DELETE: u32 = 0x2E;

/// `_IsSentencePending`.
pub fn is_sentence_pending(h: &impl Host) -> bool {
    !h.state_ref().sentence.is_empty()
}

/// `_SentenceRender`: the sentence with the reading at the cursor.
pub fn sentence_render(h: &mut impl Host, reading: &[u16]) -> HResult {
    let st = h.state_ref();
    let text = st.sentence.display(reading);
    let caret = st.sentence.display_caret(reading);
    h.sentence_write(&text, caret, true)
}

/// `_SentenceReset`.
pub fn sentence_reset(h: &mut impl Host) {
    h.state().reset_sentence();
    h.sentence_clear_display();
}

/// `_SentenceAppend`: add the character for the current reading at the cursor,
/// re-convert and show the sentence.  The reading and any candidate window
/// are cleared.
pub fn sentence_append(h: &mut impl Host, candidates: Vec<Vec<u16>>, pinned: i32) -> HResult {
    let code = h.keystroke_code();
    if !h.is_composing() {
        h.start_composition();
    }
    h.delete_candidate_list(true); // purges the reading too
    h.state().sentence_edit_pos = -1;

    if !h.state().sentence.insert(&code, candidates, pinned) {
        return sentence_render(h, &[]);
    }
    h.convert_sentence();
    if h.state_ref().sentence.count() >= MAX_POSITIONS {
        return handle_sentence_commit(h);
    }
    sentence_render(h, &[])
}

/// `_SentenceAppendFromCandidateList` with the fetched list.
pub fn sentence_append_fetched(h: &mut impl Host, pinned: i32) -> HResult {
    let candidates: Vec<Vec<u16>> = h.fetched_strings().into_iter().filter(|c| !c.is_empty()).collect();
    sentence_append(h, candidates, pinned)
}

/// `_SentenceSelectCandidate`: a candidate was finalized from the window.
/// It fixes the character being edited (Down), or adds the character for the
/// current reading; `explicit` pins it.
pub fn sentence_select_candidate(h: &mut impl Host, selected: &[u16], explicit: bool) -> HResult {
    let edit_pos = h.state_ref().sentence_edit_pos;
    if edit_pos >= 0 {
        let pos = edit_pos as usize;
        h.state().sentence_edit_pos = -1;
        let st = h.state();
        if pos < st.sentence.count() {
            if let Some(i) = st
                .sentence
                .at(pos)
                .candidates
                .iter()
                .position(|c| c.as_slice() == selected)
            {
                st.sentence.pin(pos, i as i32);
            }
        }
        h.delete_candidate_list(true);
        h.convert_sentence();
        return sentence_render(h, &[]);
    }

    let mut candidates: Vec<Vec<u16>> = Vec::new();
    let mut index: i32 = -1;
    if !h.is_candidate_with_wildcard() && !h.is_escape_input_leading() {
        h.fetch_candidates(false, false, false);
        for c in h.fetched_strings() {
            if c.is_empty() {
                continue;
            }
            if index < 0 && c.as_slice() == selected {
                index = candidates.len() as i32;
            }
            candidates.push(c);
        }
    }
    let mut explicit = explicit;
    if index < 0 {
        // Wildcard, symbol, or incremental (prefix) match: keep exactly what was chosen.
        candidates = vec![selected.to_vec()];
        index = 0;
        explicit = true;
    }
    sentence_append(h, candidates, if explicit { index } else { -1 })
}

/// `_SentenceOpenCandidates`: Down shows the candidates of the character under the cursor.
pub fn sentence_open_candidates(h: &mut impl Host) -> HResult {
    if h.state_ref().sentence.is_empty() || !h.has_ui_presenter() {
        return S_OK;
    }
    let pos = h.state_ref().sentence.edit_target();
    if h.state_ref().sentence.at(pos).candidates.len() < 2 {
        h.beep(beep::WARNING);
        return S_OK;
    }
    let hr = h.create_and_start_candidate();
    if !failed(hr) {
        // The window keeps pointers into the sentence's strings, as the C++
        // version did; the sentence does not change while it is open.
        h.show_sentence_candidates(pos);
        h.set_candidate_mode(candidate_mode::ORIGINAL);
        h.set_candidate_with_wildcard(false);
        h.state().sentence_edit_pos = pos as i32;
    }
    hr
}

/// `_SentenceCloseReading`: drop the reading / candidate window but keep the sentence.
pub fn sentence_close_reading(h: &mut impl Host) -> HResult {
    h.delete_candidate_list(true);
    let st = h.state();
    st.sentence_edit_pos = -1;
    st.sentence_pin_next = false;
    sentence_render(h, &[])
}

/// `_HandleSentenceCommit`: write the sentence into the document and end the composition.
pub fn handle_sentence_commit(h: &mut impl Host) -> HResult {
    if h.state_ref().sentence.is_empty() {
        return S_OK;
    }
    h.sentence_learn();
    let mut text = h.state_ref().sentence.text();
    if h.do_han_convert() {
        text = h.sc_from_tc(&text);
    }
    h.sentence_write(&text, text.len(), false);
    sentence_reset(h);
    handle_complete(h)
}

/// `_SentenceCommitBeforeOutput`: keys that output text commit the sentence first.
pub fn sentence_commit_before_output(h: &mut impl Host) -> HResult {
    if !is_sentence_pending(h) {
        return S_OK;
    }
    h.delete_candidate_list(true);
    handle_sentence_commit(h)
}

/// `_HandleSentenceBackspace`: Backspace / Delete remove a character.
pub fn handle_sentence_backspace(h: &mut impl Host, code: u32) -> HResult {
    let st = h.state();
    let removed = if code == VK_DELETE {
        st.sentence.remove_at_cursor()
    } else {
        st.sentence.remove_before_cursor()
    };
    if !removed {
        return S_OK;
    }
    if h.state_ref().sentence.is_empty() {
        return handle_cancel(h); // nothing left: drop the composition
    }
    h.convert_sentence();
    sentence_render(h, &[])
}

/// `KEYSTROKE_FUNCTION` values of the sentence arrow keys.
pub mod sentence_key {
    pub const MOVE_LEFT: u32 = 15;
    pub const MOVE_RIGHT: u32 = 16;
    pub const MOVE_DOWN: u32 = 18;
    pub const MOVE_PAGE_TOP: u32 = 21;
    pub const MOVE_PAGE_BOTTOM: u32 = 22;
}

/// `_HandleSentenceArrowKey`.
pub fn handle_sentence_arrow_key(h: &mut impl Host, function: u32) -> HResult {
    let st = h.state();
    match function {
        sentence_key::MOVE_LEFT => st.sentence.move_left(),
        sentence_key::MOVE_RIGHT => st.sentence.move_right(),
        sentence_key::MOVE_PAGE_TOP => st.sentence.home(),
        sentence_key::MOVE_PAGE_BOTTOM => st.sentence.end(),
        sentence_key::MOVE_DOWN => return sentence_open_candidates(h),
        _ => return S_OK,
    }
    sentence_render(h, &[])
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
        vk_len: usize,
        selected: Vec<u16>,
        make_phrase: bool,
        phrases: bool,
        fetched: u32,
        state: SessionState,
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
        fn state(&mut self) -> &mut SessionState {
            &mut self.state
        }
        fn state_ref(&self) -> &SessionState {
            &self.state
        }
        fn sentence_write(&mut self, text: &[u16], caret: usize, _attrs: bool) -> HResult {
            self.calls
                .push(format!("write {} @{caret}", String::from_utf16_lossy(text)));
            S_OK
        }
        fn convert_sentence(&mut self) {}
        fn sentence_learn(&mut self) {
            self.calls.push("learn".into());
        }
        fn sentence_clear_display(&mut self) {}
        fn keystroke_code(&self) -> Vec<u16> {
            "a".encode_utf16().collect()
        }
        fn fetched_strings(&self) -> Vec<Vec<u16>> {
            Vec::new()
        }
        fn show_sentence_candidates(&mut self, pos: usize) {
            self.calls.push(format!("show {pos}"));
        }
        fn has_ui_presenter(&self) -> bool {
            true
        }
        fn is_candidate_with_wildcard(&self) -> bool {
            false
        }
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
        fn set_commit_string(&mut self, _text: &[u16]) {}
        fn async_reverse_conversion(&mut self) {}
        fn beep(&mut self, kind: u32) {
            self.calls.push(format!("beep {kind}"));
        }
    }

    #[test]
    fn cancel_keeps_sentence_while_reading() {
        let mut m = Mock {
            vk_len: 1,
            ..Default::default()
        };
        m.state.sentence.insert(&[0x61], vec![vec![0x4EBA]], -1);
        handle_cancel(&mut m);
        assert_eq!(m.calls, ["delete_candidates true", "write \u{4EBA} @1"]);
    }

    #[test]
    fn shift_english_base_char_and_full_shape() {
        let mut m = Mock {
            full_shape: true,
            ..Default::default()
        };
        handle_shift_english_input(&mut m, 0xBD, u16::from(b'_'));
        assert_eq!(m.calls[0], "commit \u{FF0D}");
        assert_eq!(&m.calls[1..], ["remove_dummy", "delete_candidates true", "terminate"]);
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
    fn sentence_commit_learns_writes_and_completes() {
        let mut m = Mock::default();
        m.state.sentence.insert(&[0x61], vec![vec![0x4EBA], vec![0x5165]], 1);
        assert_eq!(handle_sentence_commit(&mut m), S_OK);
        assert_eq!(m.calls[..2], ["learn", "write \u{5165} @1"]);
        assert!(m.state.sentence.is_empty());
    }

    #[test]
    fn sentence_down_opens_candidates_of_edit_target() {
        let mut m = Mock::default();
        m.state.sentence.insert(&[0x61], vec![vec![0x4EBA], vec![0x5165]], -1);
        handle_sentence_arrow_key(&mut m, sentence_key::MOVE_DOWN);
        assert_eq!(m.calls, ["show 0"]);
        assert_eq!(m.state.sentence_edit_pos, 0);
        assert_eq!(m.candidate_mode, candidate_mode::ORIGINAL);
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
