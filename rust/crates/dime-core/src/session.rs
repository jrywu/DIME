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
    pub const PHRASE: u32 = 2;
}

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

    // --- handlers still in C++ ---
    fn handle_candidate_finalize(&mut self) -> HResult;
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
        h.handle_candidate_finalize();
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
        fn handle_candidate_finalize(&mut self) -> HResult {
            self.calls.push("candidate_finalize".into());
            self.candidate_mode = candidate_mode::NONE;
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
    fn address_and_full_width_tables() {
        assert_eq!(dayi_address_char(u16::from(b'\''), false), 0x865F);
        assert_eq!(dayi_address_char(u16::from(b'['), true), 0x3002);
        assert_eq!(dayi_address_char(u16::from(b'a'), false), 0);
        assert_eq!(full_width(u16::from(b' ')), 0x3000);
        assert_eq!(full_width(u16::from(b'~')), 0xFF5E);
    }
}
