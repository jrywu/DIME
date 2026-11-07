//! Key classification: port of `CCompositionProcessorEngine::IsVirtualKeyNeed`
//! and the helpers it calls (`IsEscapeInputChar`, `IsEscapeInputLeading`,
//! `IsDayiAddressChar`, `isEndComposingKey`, `IsVirtualKeyKeystrokeComposition`,
//! `IsKeystrokeRange` / `CCandidateRange::IsRange`).
//!
//! Decides whether the IME wants a key and how it will be handled (category +
//! function), from a snapshot of the engine state.  Everything the C++ code
//! reads from globals, `CConfig` or Win32 (`MapVirtualKey`, `GetKeyState`,
//! `iswprint`) is passed in [`KeyContext`].  The order of the checks and every
//! write to the key state are kept, since the final state is the output.

// --- C++ enum values -------------------------------------------------------

pub mod category {
    pub const NONE: u32 = 0;
    pub const COMPOSING: u32 = 1;
    pub const CANDIDATE: u32 = 2;
    pub const INVOKE_COMPOSITION_EDIT_SESSION: u32 = 5;
}

pub mod function {
    pub const NONE: u32 = 0;
    pub const INPUT: u32 = 1;
    pub const INPUT_AND_CONVERT: u32 = 2;
    pub const INPUT_AND_CONVERT_WILDCARD: u32 = 3;
    pub const CANCEL: u32 = 4;
    pub const FINALIZE_TEXTSTORE: u32 = 5;
    pub const FINALIZE_CANDIDATELIST_AND_INPUT: u32 = 8;
    pub const CONVERT: u32 = 9;
    pub const CONVERT_WILDCARD: u32 = 10;
    pub const CONVERT_ARRAY_PHRASE: u32 = 11;
    pub const CONVERT_ARRAY_PHRASE_WILDCARD: u32 = 12;
    pub const SELECT_BY_NUMBER: u32 = 13;
    pub const BACKSPACE: u32 = 14;
    pub const MOVE_LEFT: u32 = 15;
    pub const MOVE_RIGHT: u32 = 16;
    pub const MOVE_UP: u32 = 17;
    pub const MOVE_DOWN: u32 = 18;
    pub const MOVE_PAGE_UP: u32 = 19;
    pub const MOVE_PAGE_DOWN: u32 = 20;
    pub const MOVE_PAGE_TOP: u32 = 21;
    pub const MOVE_PAGE_BOTTOM: u32 = 22;
    pub const ADDRESS_DIRECT_INPUT: u32 = 24;
    pub const SHIFT_ENGLISH_INPUT: u32 = 25;
}

pub mod candidate_mode {
    pub const NONE: u32 = 0;
    pub const ORIGINAL: u32 = 1;
    pub const PHRASE: u32 = 2;
    pub const INCREMENTAL: u32 = 3;
}

pub mod ime_mode {
    pub const DAYI: u32 = 0;
    pub const ARRAY: u32 = 1;
    pub const PHONETIC: u32 = 2;
}

/// `ARRAY_SCOPE::ARRAY40_BIG5`.
pub const ARRAY40_BIG5: u32 = 4;

pub mod numeric_pad {
    pub const NUMERIC: u32 = 0;
    pub const COMPOSITION_ONLY: u32 = 2;
}

pub mod shift_english_digit {
    pub const CAPS_OFF_DIGIT: u32 = 1;
    pub const ALWAYS_SYMBOL: u32 = 2;
}

pub mod vk {
    pub const BACK: u32 = 0x08;
    pub const RETURN: u32 = 0x0D;
    pub const SHIFT: u32 = 0x10;
    pub const ESCAPE: u32 = 0x1B;
    pub const SPACE: u32 = 0x20;
    pub const PRIOR: u32 = 0x21;
    pub const NEXT: u32 = 0x22;
    pub const END: u32 = 0x23;
    pub const HOME: u32 = 0x24;
    pub const LEFT: u32 = 0x25;
    pub const UP: u32 = 0x26;
    pub const RIGHT: u32 = 0x27;
    pub const DOWN: u32 = 0x28;
    pub const INSERT: u32 = 0x2D;
    pub const DELETE: u32 = 0x2E;
    pub const NUMPAD0: u32 = 0x60;
    pub const NUMPAD9: u32 = 0x69;
    pub const DIVIDE: u32 = 0x6F;
    pub const OEM_7: u32 = 0xDE;
}

pub const TF_MOD_SHIFT: u32 = 0x0004;
pub const TF_MOD_RSHIFT: u32 = 0x0020;
pub const TF_MOD_LSHIFT: u32 = 0x0100;
const ANY_SHIFT: u32 = TF_MOD_LSHIFT | TF_MOD_RSHIFT | TF_MOD_SHIFT;

/// `MAX_RADICAL`: radicals are the printable ASCII range 0x20..=0x80.
pub const MAX_RADICAL: u32 = 96;

/// `vpEtenKeyToStandardKeyTable` (`PhoneticComposition.h`), indexed by `c - 32`.
#[rustfmt::skip]
const ETEN_TO_STANDARD: [u8; MAX_RADICAL as usize + 1] = [
    0,
    0, 0, 0, 0, 0, 0, b'H', 0,
    0, 0, 0, b'5', b'/', b'T', b'G', b';',
    b'7', b'6', b'3', b'4', 0, 0, b'F', b'0',
    b'P', 0, b'Y', 0, b'-', 0, b'?', 0,
    b'8', b'1', b'V', b'2', b'U', b'Z', b'R', b'C',
    b'9', b'B', b'D', b'X', b'A', b'S', b'I', b'Q',
    b'O', b'K', b'N', b'W', b'M', b'E', b',', b'J',
    b'.', b'L', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
];

/// `Global::dayiAddressCharTable` codes.
const DAYI_ADDRESS_CHARS: [u16; 6] = [
    b'`' as u16,
    b'\'' as u16,
    b'[' as u16,
    b']' as u16,
    b'-' as u16,
    b'\\' as u16,
];

/// One select key of the active `CCandidateRange`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Selkey {
    pub printable: u16,
    pub index: u32,
    pub modifiers: u32,
}

/// Everything `IsVirtualKeyNeed` reads.
#[derive(Debug, Clone, Default)]
pub struct KeyContext<'a> {
    pub code: u32,
    pub wch: u16,
    /// `(WCHAR)MapVirtualKey(code, MAPVK_VK_TO_CHAR)`.
    pub base_char: u16,
    /// `iswprint(wch)`.
    pub wch_printable: bool,
    pub composing: bool,
    pub candidate_mode: u32,
    pub candi_count: u32,
    pub candi_selection: i32,
    /// Whether the caller passed a key state (`pKeyState != nullptr`).
    pub has_key_state: bool,
    /// `Global::ModifiersValue`.
    pub modifiers: u32,
    /// `GetKeyState(VK_CAPITAL) & 1`.
    pub caps_on: bool,
    /// `Global::imeMode`.
    pub ime_mode: u32,
    /// `IsDictionaryAvailable(_imeMode)` (the engine's own mode).
    pub dictionary_available: bool,
    pub array_scope: u32,
    pub numeric_pad: u32,
    pub space_as_page_down: bool,
    pub shift_english_digit_mode: u32,
    pub phonetic_eten: bool,
    pub candidate_page_size: u32,
    pub has_wildcard_in_buffer: bool,
    /// `_keystrokeBuffer.Get() == nullptr` (distinct from an empty buffer).
    pub buffer_null: bool,
    pub buffer: &'a [u16],
    /// `Function` of each `_KeystrokeComposition` entry.
    pub keystroke_functions: &'a [u32],
    /// `_pEndkey` up to its terminator, `None` when null.
    pub end_keys: Option<&'a [u16]>,
    pub selkeys: &'a [Selkey],
}

/// `_KEYSTROKE_STATE`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct KeyState {
    pub category: u32,
    pub function: u32,
}

/// `towupper` in the C locale: ASCII letters only.
fn upper(c: u16) -> u16 {
    if (b'a' as u16..=b'z' as u16).contains(&c) {
        c - 32
    } else {
        c
    }
}

const fn w(c: u8) -> u16 {
    c as u16
}

struct Classifier<'a, 'b> {
    ctx: &'b KeyContext<'a>,
    state: KeyState,
}

impl Classifier<'_, '_> {
    fn set(&mut self, category: u32, function: u32) {
        if self.ctx.has_key_state {
            self.state = KeyState { category, function };
        }
    }

    fn is_numpad_numeric(&self) -> bool {
        let c = self.ctx;
        c.numeric_pad == numeric_pad::NUMERIC && (vk::NUMPAD0..=vk::DIVIDE).contains(&c.code)
    }

    fn array_big5(&self) -> bool {
        self.ctx.array_scope == ARRAY40_BIG5
    }

    fn is_wildcard_char(wch: u16) -> bool {
        wch == w(b'?') || wch == w(b'*')
    }

    fn is_double_single_byte(wch: u16) -> bool {
        (w(b' ')..=w(b'~')).contains(&wch)
    }

    /// `IsEscapeInputLeading`.
    fn is_escape_input_leading(&self) -> bool {
        let c = self.ctx;
        if c.buffer_null || c.buffer.is_empty() {
            return false;
        }
        let first = c.buffer[0];
        (c.ime_mode == ime_mode::DAYI && first == w(b'='))
            || (c.ime_mode == ime_mode::ARRAY
                && ((!self.array_big5() && upper(first) == w(b'W'))
                    || (self.array_big5() && (upper(first) == w(b'H') || first == w(b'8')))))
            || (c.ime_mode == ime_mode::PHONETIC && first == w(b'\\'))
    }

    /// `IsEscapeInputChar`.
    fn is_escape_input_char(&self, wch: u16) -> bool {
        let c = self.ctx;
        let len = c.buffer.len();
        if c.ime_mode == ime_mode::PHONETIC {
            if len == 0 && wch == w(b'\\') {
                return true;
            }
            if len >= 1
                && !c.buffer_null
                && c.buffer[0] == w(b'\\')
                && Self::is_double_single_byte(wch)
                && wch != w(b' ')
            {
                return true;
            }
        }
        if c.buffer_null || len > 2 {
            return false;
        }
        let big5 = self.array_big5();
        match len {
            0 => {
                (c.ime_mode == ime_mode::DAYI && wch == w(b'='))
                    || (c.ime_mode == ime_mode::ARRAY
                        && ((!big5 && upper(wch) == w(b'W')) || (big5 && (upper(wch) == w(b'H') || wch == w(b'8')))))
            }
            1 => {
                let first = c.buffer[0];
                if c.ime_mode == ime_mode::DAYI && upper(first) == w(b'=') {
                    true
                } else if c.ime_mode == ime_mode::ARRAY {
                    (!big5 && upper(first) == w(b'W') && (w(b'0')..=w(b'9')).contains(&wch))
                        || (big5
                            && (upper(first) == w(b'H') || first == w(b'8'))
                            && b"'[]-=".iter().any(|&k| wch == w(k)))
                } else {
                    false
                }
            }
            _ => {
                c.ime_mode == ime_mode::ARRAY
                    && !big5
                    && upper(c.buffer[0]) == w(b'H')
                    && upper(c.buffer[1]) == w(b'G')
                    && (w(b'0')..=w(b'9')).contains(&wch)
            }
        }
    }

    /// `IsDayiAddressChar`.
    fn is_dayi_address_char(&self, wch: u16) -> bool {
        self.ctx.ime_mode == ime_mode::DAYI && DAYI_ADDRESS_CHARS.contains(&wch)
    }

    /// `isEndComposingKey`.
    fn is_end_composing_key(&self, wch: u16) -> bool {
        let c = self.ctx;
        match c.end_keys {
            Some(keys) if !c.buffer_null && !c.buffer.is_empty() => keys.contains(&wch),
            _ => false,
        }
    }

    /// `IsVirtualKeyKeystrokeComposition(..., FUNCTION_NONE)`.
    fn keystroke_composition(&mut self) -> bool {
        let c = self.ctx;
        if !c.dictionary_available || !c.has_key_state || c.keystroke_functions.is_empty() {
            return false;
        }
        self.state = KeyState::default();

        let mut ch = upper(c.wch) as u32;
        if !(32..=32 + MAX_RADICAL).contains(&ch) {
            return false;
        }
        if c.numeric_pad == numeric_pad::COMPOSITION_ONLY && !(vk::NUMPAD0..=vk::DIVIDE).contains(&c.code) {
            return false;
        }
        if c.ime_mode == ime_mode::PHONETIC && c.phonetic_eten {
            ch = ETEN_TO_STANDARD[(ch - 32) as usize] as u32;
            if ch == 0 {
                ch = 32;
            }
        }
        // C++: GetAt -> vector::at, which throws when out of range.
        let Some(&func) = c.keystroke_functions.get((ch - 32) as usize) else {
            return false;
        };

        if (u32::from(b'A')..=u32::from(b'Z')).contains(&ch)
            && (c.modifiers & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0
            && !(c.ime_mode == ime_mode::PHONETIC && self.is_escape_input_leading())
        {
            return false;
        }

        if func != function::NONE && !self.is_numpad_numeric() {
            self.state = KeyState {
                category: category::COMPOSING,
                function: func,
            };
            return true;
        }
        false
    }

    /// `IsKeystrokeRange` (with `CCandidateRange::IsRange`).
    fn keystroke_range(&mut self, mode: u32) -> bool {
        let c = self.ctx;
        if !c.has_key_state {
            return false;
        }
        self.state = KeyState::default();

        // C++ tests PHRASE and then any other non-NONE mode, with the same result.
        if self.is_range(mode) && mode != candidate_mode::NONE {
            self.state = KeyState {
                category: category::CANDIDATE,
                function: function::SELECT_BY_NUMBER,
            };
            return true;
        }
        false
    }

    fn is_range(&self, mode: u32) -> bool {
        let c = self.ctx;
        if mode == candidate_mode::NONE {
            return false;
        }
        let gate = c.numeric_pad == numeric_pad::COMPOSITION_ONLY;
        for (i, key) in c.selkeys.iter().enumerate() {
            if c.wch == key.printable {
                return !(i == 0 && c.code == vk::SPACE && c.space_as_page_down);
            } else if (vk::NUMPAD0..=vk::NUMPAD9).contains(&c.code) && c.code - vk::NUMPAD0 == key.index {
                if gate && (c.modifiers & key.modifiers) != key.modifiers {
                    continue;
                }
                return true;
            }
        }
        false
    }

    fn classify(&mut self) -> bool {
        let c = self.ctx;
        let code = c.code;
        let wch = c.wch;
        let mode = c.candidate_mode;
        let has = c.has_key_state;

        self.set(category::NONE, function::NONE);

        let mut composing = c.composing;
        if mode == candidate_mode::ORIGINAL || mode == candidate_mode::PHRASE {
            composing = false;
        }

        // Escape input (Dayi '=' symbols, Array w/H/8 symbols, Phonetic '\' phrases).
        if self.is_escape_input_char(wch)
            && code != vk::SHIFT
            && mode != candidate_mode::ORIGINAL
            && !self.is_numpad_numeric()
        {
            let f = if c.ime_mode == ime_mode::PHONETIC {
                function::INPUT
            } else {
                function::INPUT_AND_CONVERT
            };
            self.set(category::COMPOSING, f);
            return true;
        }
        if c.ime_mode == ime_mode::PHONETIC
            && self.is_escape_input_leading()
            && mode != candidate_mode::ORIGINAL
            && (code == vk::SPACE || code == vk::RETURN)
        {
            self.set(category::COMPOSING, function::CONVERT);
            return true;
        }

        // Dayi address characters.
        if self.is_dayi_address_char(wch)
            && (mode == candidate_mode::NONE || mode == candidate_mode::PHRASE)
            && !self.is_numpad_numeric()
        {
            self.set(category::COMPOSING, function::ADDRESS_DIRECT_INPUT);
            return true;
        }

        // Candidate window keys.
        if mode == candidate_mode::ORIGINAL || mode == candidate_mode::PHRASE {
            let nav = match code {
                vk::UP => Some(function::MOVE_UP),
                vk::DOWN => Some(function::MOVE_DOWN),
                vk::PRIOR => Some(function::MOVE_PAGE_UP),
                vk::NEXT => Some(function::MOVE_PAGE_DOWN),
                vk::HOME => Some(function::MOVE_PAGE_TOP),
                vk::END => Some(function::MOVE_PAGE_BOTTOM),
                vk::LEFT => Some(function::MOVE_LEFT),
                vk::RIGHT => Some(function::MOVE_RIGHT),
                vk::BACK | vk::INSERT | vk::DELETE | vk::ESCAPE => Some(function::CANCEL),
                _ => None,
            };
            if let Some(f) = nav {
                self.set(category::CANDIDATE, f);
                return true;
            }
            // Without a key state, VK_RETURN and VK_SPACE fall through the
            // switch to VK_BACK's `return TRUE`.
            if code == vk::RETURN && has {
                if mode == candidate_mode::PHRASE && c.candi_selection == -1 {
                    self.set(category::INVOKE_COMPOSITION_EDIT_SESSION, function::CANCEL);
                    return false;
                }
                self.set(category::CANDIDATE, function::CONVERT);
                return true;
            }
            if code == vk::RETURN || code == vk::SPACE {
                if has {
                    if mode != candidate_mode::PHRASE && c.space_as_page_down && c.candi_count > c.candidate_page_size {
                        self.set(category::CANDIDATE, function::MOVE_PAGE_DOWN);
                    } else if mode == candidate_mode::PHRASE && c.candi_selection == -1 {
                        self.set(category::INVOKE_COMPOSITION_EDIT_SESSION, function::CANCEL);
                        return false;
                    } else {
                        self.set(category::CANDIDATE, function::CONVERT);
                    }
                }
                return true;
            }
            if self.keystroke_range(mode) {
                return true;
            }
            if self.keystroke_composition() {
                if has && mode == candidate_mode::ORIGINAL {
                    self.set(category::CANDIDATE, function::FINALIZE_CANDIDATELIST_AND_INPUT);
                }
                return true;
            }
        }

        // Associated phrases are cancelled by any key except Shift.
        if mode == candidate_mode::PHRASE && code != vk::SHIFT {
            self.set(category::CANDIDATE, function::CANCEL);
            return false;
        }

        // Shift + printable ASCII: English input.
        if !(c.ime_mode == ime_mode::PHONETIC && self.is_escape_input_leading())
            && !composing
            && wch != 0
            && (c.modifiers & ANY_SHIFT) != 0
        {
            let mut digit_row_wants_digit = false;
            if (u32::from(b'0')..=u32::from(b'9')).contains(&code) {
                digit_row_wants_digit = match c.shift_english_digit_mode {
                    shift_english_digit::CAPS_OFF_DIGIT => !c.caps_on,
                    shift_english_digit::ALWAYS_SYMBOL => false,
                    _ => c.caps_on,
                };
            }

            if c.wch_printable
                && wch != w(b' ')
                && (digit_row_wants_digit
                    || !(Self::is_wildcard_char(wch) && !(c.ime_mode == ime_mode::PHONETIC && wch == w(b'*'))))
            {
                if self.keystroke_composition() {
                    return true;
                }
                let base = c.base_char;
                if c.ime_mode == ime_mode::DAYI && self.is_dayi_address_char(base) {
                    // Dayi address char: no radical check.
                } else {
                    let up = upper(base) as u32;
                    if (32..32 + MAX_RADICAL).contains(&up) && ((up - 32) as usize) < c.keystroke_functions.len() {
                        if c.keystroke_functions[(up - 32) as usize] == function::NONE {
                            return false;
                        }
                    } else {
                        return false;
                    }
                }
                self.set(category::COMPOSING, function::SHIFT_ENGLISH_INPUT);
                return true;
            }
        }

        // Composing keys.
        if composing {
            if mode == candidate_mode::NONE {
                let f = match code {
                    vk::LEFT => Some(function::MOVE_LEFT),
                    vk::RIGHT => Some(function::MOVE_RIGHT),
                    vk::INSERT | vk::DELETE | vk::ESCAPE => Some(function::CANCEL),
                    vk::BACK => Some(function::BACKSPACE),
                    vk::UP => Some(function::MOVE_UP),
                    vk::DOWN => Some(function::MOVE_DOWN),
                    vk::PRIOR => Some(function::MOVE_PAGE_UP),
                    vk::NEXT => Some(function::MOVE_PAGE_DOWN),
                    vk::HOME => Some(function::MOVE_PAGE_TOP),
                    vk::END => Some(function::MOVE_PAGE_BOTTOM),
                    vk::RETURN | vk::SPACE => Some(if c.has_wildcard_in_buffer {
                        function::CONVERT_WILDCARD
                    } else {
                        function::CONVERT
                    }),
                    _ => None,
                };
                if let Some(f) = f {
                    self.set(category::COMPOSING, f);
                    return true;
                }
            } else if mode == candidate_mode::INCREMENTAL {
                let array30_plain = c.ime_mode == ime_mode::ARRAY && !self.array_big5() && !c.has_wildcard_in_buffer;
                let r = match code {
                    vk::LEFT => Some((category::COMPOSING, function::MOVE_LEFT)),
                    vk::RIGHT => Some((category::COMPOSING, function::MOVE_RIGHT)),
                    vk::RETURN => Some(if array30_plain {
                        (category::COMPOSING, function::CONVERT)
                    } else {
                        (category::CANDIDATE, function::CONVERT)
                    }),
                    vk::ESCAPE => Some((category::CANDIDATE, function::CANCEL)),
                    vk::BACK => Some((category::COMPOSING, function::BACKSPACE)),
                    vk::UP => Some((category::CANDIDATE, function::MOVE_UP)),
                    vk::DOWN => Some((category::CANDIDATE, function::MOVE_DOWN)),
                    vk::PRIOR => Some((category::CANDIDATE, function::MOVE_PAGE_UP)),
                    vk::NEXT => Some((category::CANDIDATE, function::MOVE_PAGE_DOWN)),
                    vk::HOME => Some((category::CANDIDATE, function::MOVE_PAGE_TOP)),
                    vk::END => Some((category::CANDIDATE, function::MOVE_PAGE_BOTTOM)),
                    vk::SPACE => Some(if array30_plain {
                        (category::COMPOSING, function::CONVERT)
                    } else if c.space_as_page_down && c.candi_count > c.candidate_page_size {
                        (category::CANDIDATE, function::MOVE_PAGE_DOWN)
                    } else {
                        (category::CANDIDATE, function::CONVERT)
                    }),
                    vk::OEM_7 if c.ime_mode == ime_mode::ARRAY && !self.array_big5() => {
                        Some(if c.has_wildcard_in_buffer {
                            (category::COMPOSING, function::CONVERT_ARRAY_PHRASE_WILDCARD)
                        } else {
                            (category::COMPOSING, function::CONVERT_ARRAY_PHRASE)
                        })
                    }
                    _ => None,
                };
                if let Some((cat, f)) = r {
                    self.set(cat, f);
                    return true;
                }
                if self.keystroke_range(mode) {
                    return true;
                }
            }
        }

        if composing || mode == candidate_mode::INCREMENTAL || mode == candidate_mode::NONE {
            // Wildcards (Phonetic: only '?'); numpad keys under NUMERIC go to the app.
            if Self::is_wildcard_char(wch)
                && !(c.ime_mode == ime_mode::PHONETIC && wch == w(b'*'))
                && !self.is_numpad_numeric()
            {
                self.set(category::COMPOSING, function::INPUT);
                return true;
            }

            if self.keystroke_composition() {
                if c.has_wildcard_in_buffer && self.state.function == function::INPUT_AND_CONVERT {
                    self.state.function = function::INPUT_AND_CONVERT_WILDCARD;
                }
                return true;
            }

            if self.is_end_composing_key(wch) {
                let f = if c.has_wildcard_in_buffer {
                    function::CONVERT_WILDCARD
                } else {
                    function::CONVERT
                };
                self.set(category::COMPOSING, f);
                return true;
            }

            if wch != 0 && !self.keystroke_composition() {
                if composing && !c.buffer.is_empty() {
                    self.set(category::COMPOSING, function::NONE);
                    return true;
                }
                self.set(category::INVOKE_COMPOSITION_EDIT_SESSION, function::FINALIZE_TEXTSTORE);
                return false;
            }
        }
        false
    }
}

/// `CCompositionProcessorEngine::IsVirtualKeyNeed`: whether the IME eats the
/// key, and the resulting key state (left `NONE` without a key state).
pub fn classify_key(ctx: &KeyContext<'_>) -> (bool, KeyState) {
    let mut c = Classifier {
        ctx,
        state: KeyState::default(),
    };
    let eaten = c.classify();
    (eaten, c.state)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dayi_functions() -> Vec<u32> {
        let mut f = vec![function::NONE; MAX_RADICAL as usize + 1];
        for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,./;=".bytes() {
            f[(ch - 32) as usize] = function::INPUT;
        }
        f[0] = function::CONVERT; // ' '
        f
    }

    fn base<'a>(funcs: &'a [u32], buffer: &'a [u16]) -> KeyContext<'a> {
        KeyContext {
            has_key_state: true,
            dictionary_available: true,
            numeric_pad: 1,
            candidate_page_size: 10,
            candi_selection: -1,
            buffer,
            buffer_null: buffer.is_empty(),
            keystroke_functions: funcs,
            ..Default::default()
        }
    }

    #[test]
    fn radical_starts_composition() {
        let f = dayi_functions();
        let mut ctx = base(&f, &[]);
        ctx.code = u32::from(b'A');
        ctx.wch = w(b'a');
        ctx.wch_printable = true;
        let (eaten, st) = classify_key(&ctx);
        assert!(eaten);
        assert_eq!(
            st,
            KeyState {
                category: category::COMPOSING,
                function: function::INPUT
            }
        );
    }

    #[test]
    fn space_while_composing_converts() {
        let f = dayi_functions();
        let buf = [w(b'a')];
        let mut ctx = base(&f, &buf);
        ctx.code = vk::SPACE;
        ctx.wch = w(b' ');
        ctx.composing = true;
        let (eaten, st) = classify_key(&ctx);
        assert!(eaten);
        assert_eq!(st.function, function::CONVERT);
    }

    #[test]
    fn phrase_candidate_cancelled_by_other_key() {
        let f = dayi_functions();
        let mut ctx = base(&f, &[]);
        ctx.candidate_mode = candidate_mode::PHRASE;
        ctx.code = u32::from(b'Q');
        ctx.wch = w(b'q');
        ctx.modifiers = TF_MOD_SHIFT;
        let (eaten, st) = classify_key(&ctx);
        assert!(!eaten);
        assert_eq!(
            st,
            KeyState {
                category: category::CANDIDATE,
                function: function::CANCEL
            }
        );
    }

    #[test]
    fn return_without_key_state_falls_through_to_true() {
        let f = dayi_functions();
        let mut ctx = base(&f, &[]);
        ctx.has_key_state = false;
        ctx.candidate_mode = candidate_mode::PHRASE;
        ctx.code = vk::RETURN;
        assert_eq!(classify_key(&ctx), (true, KeyState::default()));
    }

    #[test]
    fn dayi_equals_is_escape_only_with_non_null_buffer() {
        let f = dayi_functions();
        let mut ctx = base(&f, &[]);
        ctx.code = 0xBB;
        ctx.wch = w(b'=');
        ctx.buffer_null = false;
        assert_eq!(classify_key(&ctx).1.function, function::INPUT_AND_CONVERT);
        ctx.buffer_null = true; // escape check skipped; '=' is still a radical
        assert_eq!(classify_key(&ctx).1.function, function::INPUT);
    }
}
