//! Key event decisions of `CDIME` (`KeyEventSink.cpp`, `SentenceHandler.cpp`):
//! `_DecideKeyEaten` (whether the IME wants a key and how it is handled), the
//! sentence-mode keys (`_IsSentenceKeyEaten`), the Shift-English / pending
//! wildcard flags, and when `OnTestKeyDown` / `OnKeyDown` invoke the handler.
//!
//! The facts (`_GatherKeyFacts`: compartments, keyboard state, the candidate
//! window) are read by the C++ sink; everything here is pure.

use crate::keyclass::{self, candidate_mode, category, function, ime_mode, vk, KeyContext, KeyState};
use crate::session::SessionState;

/// Keys the touch keyboard sends for next / previous page.
pub const THIRDPARTY_NEXTPAGE: u16 = 0xF003;
pub const THIRDPARTY_PREVPAGE: u16 = 0xF004;

/// `KEYSTROKE_CATEGORY::CATEGORY_SENTENCE`.
pub const CATEGORY_SENTENCE: u32 = 6;
/// `KEYSTROKE_FUNCTION::FUNCTION_DOUBLE_SINGLE_BYTE`.
pub const FUNCTION_DOUBLE_SINGLE_BYTE: u32 = 23;

const TF_MOD_ALT: u32 = 0x0001;
const TF_MOD_CONTROL: u32 = 0x0002;
const TF_MOD_RALT: u32 = 0x0008;
const TF_MOD_RCONTROL: u32 = 0x0010;
const TF_MOD_LALT: u32 = 0x0040;
const TF_MOD_LCONTROL: u32 = 0x0080;
const ANY_SHIFT: u32 = keyclass::TF_MOD_LSHIFT | keyclass::TF_MOD_RSHIFT | keyclass::TF_MOD_SHIFT;

/// What `_DecideKeyEaten` reads besides the session state.
#[derive(Debug, Clone, Default)]
pub struct DecideInput<'a> {
    pub keyboard_disabled: bool,
    /// The IME-mode compartment (Chinese input on).
    pub is_open: bool,
    /// The double/single-byte compartment (full shape).
    pub is_double_single_byte: bool,
    /// `_pCompositionProcessorEngine != nullptr`.
    pub has_engine: bool,
    pub is_composing: bool,
    pub candi_count: u32,
    /// `GetVirtualKeyLength()`.
    pub virtual_key_length: usize,
    /// For `IsVirtualKeyNeed`: `code` is the key after `VKeyFromVKPacketAndWchar`,
    /// `wch` the character from the keyboard state, `candidate_mode` the current
    /// `_candidateMode`.
    pub key: KeyContext<'a>,
}

/// The result of `_DecideKeyEaten`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct DecideOutput {
    pub eaten: bool,
    /// `*pwch`.
    pub wch: u16,
    /// `*pKeyState` (left `NONE` without a key state).
    pub state: KeyState,
    /// A stale candidate mode without a candidate window: reset `_candidateMode`
    /// to `NONE` and `_isCandidateWithWildcard` to `FALSE` (issue #126).
    pub reset_candidate_mode: bool,
}

/// `_IsSentenceKeyEaten`: the keys that act on a pending sentence (with no
/// reading and no candidate window).
pub fn sentence_key(code: u32, modifiers: u32) -> Option<KeyState> {
    // Modified keys (Ctrl/Alt shortcuts) are not ours.
    if modifiers & (TF_MOD_CONTROL | TF_MOD_ALT | TF_MOD_LCONTROL | TF_MOD_RCONTROL | TF_MOD_LALT | TF_MOD_RALT) != 0 {
        return None;
    }
    let f = match code {
        vk::RETURN | vk::SPACE => function::CONVERT,
        vk::BACK | vk::DELETE => function::BACKSPACE,
        vk::LEFT => function::MOVE_LEFT,
        vk::RIGHT => function::MOVE_RIGHT,
        vk::HOME => function::MOVE_PAGE_TOP,
        vk::END => function::MOVE_PAGE_BOTTOM,
        vk::DOWN => function::MOVE_DOWN,
        vk::UP | vk::PRIOR | vk::NEXT => function::MOVE_UP, // eaten, no effect
        vk::ESCAPE => {
            return Some(KeyState {
                category: category::COMPOSING, // _HandleCancel discards
                function: function::CANCEL,
            });
        }
        _ => return None,
    };
    Some(KeyState {
        category: CATEGORY_SENTENCE,
        function: f,
    })
}

fn is_wildcard(c: u16) -> bool {
    c == u16::from(b'*') || c == u16::from(b'?')
}

/// `_DecideKeyEaten`.
pub fn decide_key_eaten(s: &SessionState, input: &DecideInput<'_>) -> DecideOutput {
    let k = &input.key;
    let has_state = k.has_key_state;
    let mut out = DecideOutput::default();
    let set = |out: &mut DecideOutput, category: u32, function: u32| {
        if has_state {
            out.state = KeyState { category, function };
        }
    };

    if input.keyboard_disabled {
        return out;
    }

    let wch = k.wch;
    let touch_special = wch == THIRDPARTY_NEXTPAGE || wch == THIRDPARTY_PREVPAGE;
    if touch_special {
        out.wch = wch;
    }
    // Keyboard closed: only the touch keyboard's special keys are eaten.
    if !input.is_open && !input.is_double_single_byte {
        out.eaten = touch_special;
        return out;
    }
    out.wch = wch;

    let shift = k.modifiers & ANY_SHIFT != 0;
    let mut mode = k.candidate_mode;
    if input.is_open && input.has_engine {
        // Issue #126: a stale candidate mode without a candidate window (make-phrase
        // sets PHRASE before the window exists).  Zero incremental candidates is a
        // normal composing state, so INCREMENTAL is kept (#132).
        if mode != candidate_mode::NONE && mode != candidate_mode::INCREMENTAL && input.candi_count == 0 {
            mode = candidate_mode::NONE;
            out.reset_candidate_mode = true;
        }

        // Already in Shift English and a wildcard is typed: continue as Shift English.
        if s.shifted_english && !input.is_composing && is_wildcard(wch) && shift {
            set(&mut out, category::COMPOSING, function::SHIFT_ENGLISH_INPUT);
            out.eaten = true;
            return out;
        }

        // A wildcard started a composition with Shift still held: only further
        // wildcards go to the composition; other keys are eaten and ignored.
        if s.pending_wildcard && shift {
            let valid = is_wildcard(wch) && !(k.ime_mode == ime_mode::PHONETIC && wch == u16::from(b'*'));
            if !valid {
                out.eaten = true;
                return out;
            }
        }

        // Associated phrases shown + Shift with a printable non-digit: dismiss them
        // and type Shift English (before IsVirtualKeyNeed, which would take radicals).
        if mode == candidate_mode::PHRASE
            && shift
            && wch != 0
            && k.wch_printable
            && wch != u16::from(b' ')
            && !(u32::from(b'0')..=u32::from(b'9')).contains(&k.code)
        {
            set(&mut out, category::COMPOSING, function::SHIFT_ENGLISH_INPUT);
            out.eaten = true;
            return out;
        }

        // Sentence mode: with a pending sentence and nothing being typed, the
        // sentence keys act on the sentence.
        if !s.sentence.is_empty() && input.is_composing && mode == candidate_mode::NONE && input.virtual_key_length == 0
        {
            if let Some(state) = sentence_key(k.code, k.modifiers) {
                set(&mut out, state.category, state.function);
                out.eaten = true;
                return out;
            }
        }

        let mut ctx = k.clone();
        ctx.candidate_mode = mode;
        let (eaten, state) = keyclass::classify_key(&ctx);
        out.state = state;
        if eaten {
            out.eaten = true;
            return out;
        }
    }

    // Double/single byte (full shape) for printable ASCII.
    if input.is_double_single_byte
        && input.has_engine
        && (u16::from(b' ')..=u16::from(b'~')).contains(&wch)
        && mode == candidate_mode::NONE
    {
        set(&mut out, category::COMPOSING, FUNCTION_DOUBLE_SINGLE_BYTE);
        out.eaten = true;
        return out;
    }

    out.eaten = touch_special;
    out
}

/// `OnTestKeyDown`: a fresh Shift press (lParam bit 30 clear: the key was up)
/// ends the Shift gesture.  On Windows 10 OnKeyDown / OnKeyUp never see Shift
/// when the IME does not eat it, so this is where the flags are cleared.
pub fn on_test_key_down(s: &mut SessionState, code: u32, lparam: isize) {
    if code == vk::SHIFT && s.shifted_english && (lparam & 0x4000_0000) == 0 {
        s.shifted_english = false;
        s.pending_wildcard = false;
    }
}

/// `OnKeyUp`: Shift released (where the platform reports it).
pub fn on_key_up(s: &mut SessionState, code: u32) {
    if code == vk::SHIFT && s.shifted_english {
        s.shifted_english = false;
        s.pending_wildcard = false;
    }
}

/// End of `OnKeyDown`: track Shift English (kept across Shift+Backspace) and a
/// wildcard typed with Shift held (cleared when Shift is released).
pub fn after_key_down(s: &mut SessionState, function: u32, code: u32, wch: u16, modifiers: u32) {
    let shift = modifiers & ANY_SHIFT != 0;
    if function == function::SHIFT_ENGLISH_INPUT {
        s.shifted_english = true;
    } else if code != vk::BACK || !shift {
        s.shifted_english = false;
    }

    if function == function::INPUT && !s.shifted_english && shift && is_wildcard(wch) {
        s.pending_wildcard = true;
    } else if !shift {
        s.pending_wildcard = false;
    }
}

/// `OnTestKeyDown`: the key state to invoke the handler with, if any.
pub fn test_key_down_invoke(eaten: bool, state: KeyState) -> Option<KeyState> {
    if state.category == category::INVOKE_COMPOSITION_EDIT_SESSION {
        Some(KeyState {
            category: category::COMPOSING,
            function: state.function,
        })
    } else if !eaten && state.category == category::CANDIDATE && state.function == function::CANCEL {
        // Any key cancels the associated phrases.
        Some(state)
    } else {
        None
    }
}

/// `OnKeyDown`: the key state to invoke the handler with, if any.
pub fn key_down_invoke(eaten: bool, state: KeyState, code: u32, wch: u16, candidate_mode: u32) -> Option<KeyState> {
    if eaten {
        let mut state = state;
        if code == vk::ESCAPE
            && !(candidate_mode == candidate_mode::PHRASE
                && state.category == category::CANDIDATE
                && state.function == function::CANCEL)
        {
            state.category = category::COMPOSING;
        }
        // The touch keyboard's page keys are always eaten, not always handled.
        if (wch == THIRDPARTY_NEXTPAGE || wch == THIRDPARTY_PREVPAGE)
            && state.category == category::NONE
            && state.function == function::NONE
        {
            return None;
        }
        Some(state)
    } else if state.category == category::INVOKE_COMPOSITION_EDIT_SESSION {
        Some(KeyState {
            category: category::COMPOSING,
            function: state.function,
        })
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn open_input<'a>(funcs: &'a [u32]) -> DecideInput<'a> {
        DecideInput {
            is_open: true,
            has_engine: true,
            key: KeyContext {
                has_key_state: true,
                dictionary_available: true,
                numeric_pad: 1,
                keystroke_functions: funcs,
                buffer_null: true,
                ..Default::default()
            },
            ..Default::default()
        }
    }

    #[test]
    fn closed_keyboard_eats_only_touch_page_keys() {
        let s = SessionState::new();
        let mut i = open_input(&[]);
        i.is_open = false;
        i.key.wch = u16::from(b'a');
        assert!(!decide_key_eaten(&s, &i).eaten);
        i.key.wch = THIRDPARTY_NEXTPAGE;
        let o = decide_key_eaten(&s, &i);
        assert!(o.eaten && o.wch == THIRDPARTY_NEXTPAGE);
    }

    #[test]
    fn stale_phrase_mode_is_reset() {
        let s = SessionState::new();
        let mut i = open_input(&[]);
        i.key.candidate_mode = candidate_mode::PHRASE;
        assert!(decide_key_eaten(&s, &i).reset_candidate_mode);
    }

    #[test]
    fn shift_english_continues_into_wildcards() {
        let mut s = SessionState::new();
        s.shifted_english = true;
        let mut i = open_input(&[]);
        i.key.wch = u16::from(b'*');
        i.key.modifiers = keyclass::TF_MOD_SHIFT;
        let o = decide_key_eaten(&s, &i);
        assert!(o.eaten);
        assert_eq!(o.state.function, function::SHIFT_ENGLISH_INPUT);
    }

    #[test]
    fn sentence_keys_and_flags() {
        assert_eq!(sentence_key(vk::SPACE, 0).map(|k| k.function), Some(function::CONVERT));
        assert_eq!(sentence_key(vk::SPACE, TF_MOD_CONTROL), None);
        let mut s = SessionState::new();
        after_key_down(
            &mut s,
            function::SHIFT_ENGLISH_INPUT,
            0x41,
            u16::from(b'A'),
            keyclass::TF_MOD_SHIFT,
        );
        assert!(s.shifted_english);
        after_key_down(&mut s, function::NONE, vk::BACK, 0, keyclass::TF_MOD_SHIFT);
        assert!(s.shifted_english, "kept across Shift+Backspace");
        on_test_key_down(&mut s, vk::SHIFT, 0x4000_0000);
        assert!(s.shifted_english, "auto-repeat is not a fresh press");
        on_test_key_down(&mut s, vk::SHIFT, 0);
        assert!(!s.shifted_english);
    }
}
