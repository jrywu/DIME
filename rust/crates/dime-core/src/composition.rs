//! Keystroke buffer helpers of `CCompositionProcessorEngine`: escape-input
//! detection, the phonetic syllable (`addPhoneticKey`, `removeLastPhoneticSymbol`,
//! `buildKeyStrokesFromPhoneticSyllable`), `AddVirtualKey`, `RemoveVirtualKey`
//! and `GetReadingString`.
//!
//! The buffer itself is still owned by the C++ engine; these functions take a
//! view of it and return what the C++ code would change.  Like the C++ code,
//! a null buffer (after `PurgeVirtualKey`) is distinct from an empty one
//! (after removing the last key), and `IsEscapeInput` reads the first unit
//! even when the length is zero (the stale key left by `RemoveVirtualKey`).

use crate::keyclass::{ime_mode, upper, ARRAY40_BIG5, ETEN_TO_STANDARD, MAX_RADICAL};

const fn w(c: u8) -> u16 {
    c as u16
}

fn is_digit(c: u16) -> bool {
    (w(b'0')..=w(b'9')).contains(&c)
}

fn is_wildcard(c: u16) -> bool {
    c == w(b'?') || c == w(b'*')
}

/// `IsEscapeInputLeading`: the buffer starts an escape (symbol / custom phrase) input.
pub fn is_escape_input_leading(keys: &[u16], null: bool, ime: u32, array_scope: u32) -> bool {
    if null || keys.is_empty() {
        return false;
    }
    let first = keys[0];
    let big5 = array_scope == ARRAY40_BIG5;
    (ime == ime_mode::DAYI && first == w(b'='))
        || (ime == ime_mode::ARRAY
            && ((!big5 && upper(first) == w(b'W')) || (big5 && (upper(first) == w(b'H') || first == w(b'8')))))
        || (ime == ime_mode::PHONETIC && first == w(b'\\'))
}

/// `IsEscapeInput`.  `first_raw` is `*_keystrokeBuffer.Get()` when the buffer
/// is not null, read even when the length is zero.
pub fn is_escape_input(keys: &[u16], first_raw: Option<u16>, ime: u32, array_scope: u32) -> bool {
    let Some(c) = first_raw else {
        return false;
    };
    let len = keys.len();
    let big5 = array_scope == ARRAY40_BIG5;

    if ime == ime_mode::DAYI && len < 3 && c == w(b'=') {
        return true;
    }
    if ime == ime_mode::ARRAY && len == 2 {
        let c2 = keys[1];
        if !big5 && upper(c) == w(b'W') && is_digit(c2) {
            return true;
        }
        if big5 && (upper(c) == w(b'H') || c == w(b'8')) && b"'[]-=".iter().any(|&k| c2 == w(k)) {
            return true;
        }
    }
    if ime == ime_mode::ARRAY && len == 3 && !big5 {
        let (c2, c3) = (keys[1], keys[2]);
        if upper(c) == w(b'H') && upper(c2) == w(b'G') && is_digit(c3) {
            return true;
        }
    }
    ime == ime_mode::PHONETIC && c == w(b'\\') && len >= 2
}

// --- Phonetic syllable -------------------------------------------------------

/// `PHONETIC_SMBOLS` (`PhoneticSymbols.h`).
pub mod vp {
    pub const CONSONANT_MASK: u32 = 0x001F;
    pub const MIDDLE_VOWEL_MASK: u32 = 0x00E0;
    pub const VOWEL_MASK: u32 = 0x0F00;
    pub const TONE_MASK: u32 = 0x7000;

    pub const B: u32 = 0x01;
    pub const P: u32 = 0x02;
    pub const M: u32 = 0x03;
    pub const F: u32 = 0x04;
    pub const D: u32 = 0x05;
    pub const T: u32 = 0x06;
    pub const N: u32 = 0x07;
    pub const L: u32 = 0x08;
    pub const G: u32 = 0x09;
    pub const K: u32 = 0x0a;
    pub const H: u32 = 0x0b;
    pub const J: u32 = 0x0c;
    pub const Q: u32 = 0x0d;
    pub const X: u32 = 0x0e;
    pub const ZH: u32 = 0x0f;
    pub const CH: u32 = 0x10;
    pub const SH: u32 = 0x11;
    pub const R: u32 = 0x12;
    pub const Z: u32 = 0x13;
    pub const C: u32 = 0x14;
    pub const S: u32 = 0x15;
    pub const ANY_CONSONANT: u32 = 0x16;

    pub const I: u32 = 0x20;
    pub const U: u32 = 0x40;
    pub const V: u32 = 0x60;
    pub const ANY_MIDDLE_VOWEL: u32 = 0x80;

    pub const A: u32 = 0x100;
    pub const O: u32 = 0x200;
    pub const ER: u32 = 0x300;
    pub const E: u32 = 0x400;
    pub const AI: u32 = 0x500;
    pub const EI: u32 = 0x600;
    pub const AO: u32 = 0x700;
    pub const OU: u32 = 0x800;
    pub const AN: u32 = 0x900;
    pub const EN: u32 = 0xA00;
    pub const ANG: u32 = 0xB00;
    pub const ENG: u32 = 0xC00;
    pub const ERR: u32 = 0xD00;
    pub const ANY_VOWEL: u32 = 0xE00;

    pub const TONE2: u32 = 0x1000;
    pub const TONE3: u32 = 0x2000;
    pub const TONE4: u32 = 0x3000;
    pub const TONE5: u32 = 0x4000;
    pub const ANY_TONE: u32 = 0x5000;

    pub const ANY: u32 = 0x8000;
}

/// `vpStandardKeyTable` (`PhoneticComposition.h`), indexed by `c - 32`.
#[rustfmt::skip]
const STANDARD_KEY_TABLE: [u32; MAX_RADICAL as usize + 1] = {
    use vp::*;
    [
        0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, E, ERR, OU, ENG, AN,
        B, D, TONE3, TONE4, ZH, TONE2, TONE5, A,
        AI, 0, ANG, 0, 0, 0, ANY, 0,
        M, R, H, K, G, Q, SH, C,
        O, U, ER, AO, V, S, EI, EN,
        P, J, N, CH, I, X, T, L,
        Z, F, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ]
};

/// `vpStandardLayoutCode`.
const STANDARD_LAYOUT_CODE: &[u8; 42] = b" 1qaz2wsxedcrfv5tgbyhnujm8ik,9ol.0p;/-6347";

/// `addPhoneticKey`: the syllable after typing `wch`.
pub fn phonetic_add_key(syllable: u32, wch: u16, eten: bool) -> u32 {
    use vp::*;
    let mut c = upper(wch) as u32;
    if !(32..=32 + MAX_RADICAL).contains(&c) {
        return syllable;
    }
    if eten {
        c = ETEN_TO_STANDARD[(c - 32) as usize] as u32;
    }
    let s = STANDARD_KEY_TABLE[if c == 0 { 0 } else { (c - 32) as usize }];
    let mut syl = syllable;

    if s == ANY {
        if syl == 0 {
            syl |= ANY_CONSONANT;
        } else if (syl & CONSONANT_MASK) != 0 && (syl & (MIDDLE_VOWEL_MASK | VOWEL_MASK | TONE_MASK)) == 0 {
            syl |= ANY_MIDDLE_VOWEL;
        } else if (syl & MIDDLE_VOWEL_MASK) != 0 && (syl & (VOWEL_MASK | TONE_MASK)) == 0 {
            syl |= ANY_TONE;
        } else if (syl & (MIDDLE_VOWEL_MASK | TONE_MASK)) != 0 && (syl & VOWEL_MASK) == 0 {
            syl |= ANY_VOWEL;
        } else if (syl & (MIDDLE_VOWEL_MASK | VOWEL_MASK)) != 0 && (syl & TONE_MASK) == 0 {
            syl |= ANY_TONE;
        }
        return syl;
    }

    let tone = s & TONE_MASK;
    if tone != 0 && syl != 0 {
        syl = (syl & !TONE_MASK) | tone;
    } else {
        let old = syl;
        let vowel = s & VOWEL_MASK;
        let middle = s & MIDDLE_VOWEL_MASK;
        let consonant = s & CONSONANT_MASK;
        if vowel != 0 {
            syl = (syl & !VOWEL_MASK) | vowel;
        } else if middle != 0 {
            syl = (syl & !MIDDLE_VOWEL_MASK) | middle;
        } else if consonant != 0 {
            syl = (syl & !CONSONANT_MASK) | consonant;
        }
        if (syl & TONE_MASK) != 0 && old != syl {
            syl &= !TONE_MASK;
        }
    }
    syl
}

/// `removeLastPhoneticSymbol`.
pub fn phonetic_remove_last(syllable: u32) -> u32 {
    use vp::*;
    if syllable & TONE_MASK != 0 {
        syllable & !TONE_MASK
    } else if syllable & VOWEL_MASK != 0 {
        syllable & !VOWEL_MASK
    } else if syllable & MIDDLE_VOWEL_MASK != 0 {
        syllable & !MIDDLE_VOWEL_MASK
    } else {
        0
    }
}

/// `VPSymbolToStandardLayoutChar`.
fn symbol_to_layout_char(syllable: u32) -> u16 {
    use vp::*;
    if syllable == ANY_CONSONANT || syllable == ANY_MIDDLE_VOWEL || syllable == ANY_VOWEL || syllable == ANY_TONE {
        return w(b'?');
    }
    let ordinal = if syllable & CONSONANT_MASK != 0 {
        syllable & CONSONANT_MASK
    } else if syllable & MIDDLE_VOWEL_MASK != 0 {
        ((syllable & MIDDLE_VOWEL_MASK) >> 5) + 21
    } else if syllable & VOWEL_MASK != 0 {
        ((syllable & VOWEL_MASK) >> 8) + 24
    } else if syllable & TONE_MASK != 0 {
        ((syllable & TONE_MASK) >> 12) + 37
    } else {
        0
    };
    if ordinal > 41 {
        return 0;
    }
    w(STANDARD_LAYOUT_CODE[ordinal as usize])
}

/// `buildKeyStrokesFromPhoneticSyllable`: the standard-layout keys of a
/// syllable.  Like the C++ (`wcslen`), it ends at the first unmapped symbol.
pub fn phonetic_keystrokes(syllable: u32) -> Vec<u16> {
    use vp::*;
    let mut out = Vec::with_capacity(4);
    for mask in [CONSONANT_MASK, MIDDLE_VOWEL_MASK, VOWEL_MASK, TONE_MASK] {
        if syllable & mask != 0 {
            out.push(symbol_to_layout_char(syllable & mask));
        }
    }
    if let Some(end) = out.iter().position(|&c| c == 0) {
        out.truncate(end);
    }
    out
}

// --- AddVirtualKey / RemoveVirtualKey -----------------------------------------

/// `BEEP_TYPE` values `AddVirtualKey` asks for.
pub const BEEP_COMPOSITION_ERROR: u32 = 0;
pub const BEEP_WARNING: u32 = 1;

/// `MAX_KEY_LENGTH`: the limit for escape input.
pub const MAX_KEY_LENGTH: u32 = 64;

/// What `AddVirtualKey` does to the buffer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AddAction {
    None,
    Append(u16),
    /// Phonetic: the buffer becomes the syllable's keys.
    Replace(Vec<u16>),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AddKeyResult {
    pub accepted: bool,
    pub beep: Option<u32>,
    pub syllable: u32,
    pub action: AddAction,
}

/// Engine state `AddVirtualKey` reads.
#[derive(Debug, Clone, Copy)]
pub struct AddKeyContext<'a> {
    pub keys: &'a [u16],
    pub null: bool,
    pub ime_mode: u32,
    pub array_scope: u32,
    pub phonetic_eten: bool,
    /// `CConfig::GetMaxCodes()`.
    pub max_codes: u32,
    pub syllable: u32,
}

/// `AddVirtualKey`.
pub fn add_key(ctx: &AddKeyContext<'_>, wch: u16) -> AddKeyResult {
    let mut r = AddKeyResult {
        accepted: false,
        beep: None,
        syllable: ctx.syllable,
        action: AddAction::None,
    };
    if wch == 0 {
        return r;
    }
    let leading = is_escape_input_leading(ctx.keys, ctx.null, ctx.ime_mode, ctx.array_scope);
    if ctx.ime_mode == ime_mode::PHONETIC && wch != w(b'\\') && !leading {
        let syllable = phonetic_add_key(ctx.syllable, wch, ctx.phonetic_eten);
        if syllable != ctx.syllable {
            r.syllable = syllable;
            let keys = phonetic_keystrokes(syllable);
            if !keys.is_empty() {
                r.action = AddAction::Replace(keys);
            }
            r.accepted = true;
        } else {
            r.beep = Some(BEEP_COMPOSITION_ERROR);
        }
        return r;
    }
    let max_len = if leading { MAX_KEY_LENGTH } else { ctx.max_codes };
    if ctx.keys.len() as u64 >= u64::from(max_len) {
        r.beep = Some(BEEP_WARNING);
        return r;
    }
    r.action = AddAction::Append(wch);
    r.accepted = true;
    r
}

/// `RemoveVirtualKey` on the buffer in place.  Returns the new length; the
/// units past it keep their old values, as with the C++ `memmove`.
/// (C++ underflows the length on an empty buffer; callers never do that.)
pub fn remove_key(keys: &mut [u16], index: usize) -> usize {
    let len = keys.len();
    if index + 1 < len {
        keys.copy_within(index + 1.., index);
    }
    len.saturating_sub(1)
}

// --- GetReadingString ------------------------------------------------------------

/// `MAX_READINGSTRING` (`CompositionProcessorEngine.cpp`).
pub const MAX_READINGSTRING: usize = 64;

/// `StringCchCat` into a `MAX_READINGSTRING` buffer: truncates when full.
fn cat(out: &mut Vec<u16>, src: &[u16]) {
    let room = (MAX_READINGSTRING - 1).saturating_sub(out.len());
    out.extend_from_slice(&src[..src.len().min(room)]);
}

/// `GetReadingString` for a non-empty key code: each key replaced by its
/// radical (looked up by the ASCII upper case) unless the engine's own buffer
/// is an escape input.  Returns the reading string and whether a wildcard was
/// seen (only while converting radicals).
pub fn reading_string(keys: &[u16], escape_input: bool, radicals: &[(u16, &[u16])]) -> (Vec<u16>, bool) {
    let mut out = Vec::new();
    let mut wildcard = false;
    if !radicals.is_empty() && !escape_input {
        for &ch in keys {
            let key = upper(ch);
            match radicals.iter().find(|(k, _)| *k == key) {
                Some((_, value)) => cat(&mut out, value),
                None => cat(&mut out, &[ch]),
            }
            if is_wildcard(ch) {
                wildcard = true;
            }
        }
    } else {
        cat(&mut out, keys);
    }
    (out, wildcard)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(t: &str) -> Vec<u16> {
        t.encode_utf16().collect()
    }

    #[test]
    fn phonetic_syllable_round_trip() {
        // ㄓ (5) ㄨ (j) ㄥ (/) ˇ (3) -> "5j/3"
        let mut syl = 0;
        for c in "5j/3".encode_utf16() {
            syl = phonetic_add_key(syl, c, false);
        }
        assert_eq!(phonetic_keystrokes(syl), s("5j/3"));
        syl = phonetic_remove_last(syl);
        assert_eq!(phonetic_keystrokes(syl), s("5j/"));
        // A tone alone is not added to an empty syllable.
        assert_eq!(phonetic_add_key(0, w(b'3'), false), 0);
        // Wildcard first becomes "any consonant".
        assert_eq!(phonetic_keystrokes(phonetic_add_key(0, w(b'?'), false)), s("?"));
    }

    #[test]
    fn add_key_limits() {
        let keys = s("abcd");
        let ctx = AddKeyContext {
            keys: &keys,
            null: false,
            ime_mode: ime_mode::DAYI,
            array_scope: 0,
            phonetic_eten: false,
            max_codes: 4,
            syllable: 0,
        };
        assert_eq!(add_key(&ctx, w(b'e')).beep, Some(BEEP_WARNING));
        let eq = s("=abc");
        let ctx = AddKeyContext { keys: &eq, ..ctx };
        assert_eq!(add_key(&ctx, w(b'e')).action, AddAction::Append(w(b'e')));
    }

    #[test]
    fn escape_input_reads_stale_first_key() {
        assert!(is_escape_input(&[], Some(w(b'=')), ime_mode::DAYI, 0));
        assert!(!is_escape_input(&[], None, ime_mode::DAYI, 0));
    }

    #[test]
    fn reading_string_truncates_like_strsafe() {
        let long = vec![w(b'x'); 40];
        let radicals = [(w(b'A'), long.as_slice())];
        let (r, _) = reading_string(&s("aa"), false, &radicals);
        assert_eq!(r.len(), MAX_READINGSTRING - 1);
        let (r, wild) = reading_string(&s("a?b"), false, &radicals);
        assert_eq!(r.len(), 42);
        assert!(wild);
        let (r, wild) = reading_string(&s("a?b"), true, &radicals);
        assert_eq!(r, s("a?b"));
        assert!(!wild);
    }

    #[test]
    fn remove_key_shifts_left() {
        let mut b = s("abcd");
        let n = remove_key(&mut b, 1);
        assert_eq!(&b[..n], &s("acd")[..]);
        let n = remove_key(&mut b[..n], 9);
        assert_eq!(n, 2);
    }
}
