//! Text primitives shared by the parser and search engine.
//!
//! All text is UTF-16 (`u16` code units), exactly as the C++ code stores it,
//! so that offsets (e.g. the sorted-CIN radical index) are interchangeable
//! with the C++ implementation.
//!
//! Locale-sensitive behaviour (CompareString / GetStringTypeEx) is abstracted
//! behind [`TextOps`].  On Windows, [`WinTextOps`] calls the very same Win32
//! APIs the C++ code uses, so results are bit-for-bit compatible.
//! [`SimpleTextOps`] is a portable approximation for non-Windows hosts and
//! fuzzing; it is *not* guaranteed to match Windows collation.

use std::cmp::Ordering;

pub const QUOTE: u16 = b'"' as u16;
pub const BACKSLASH: u16 = b'\\' as u16;
pub const CR: u16 = b'\r' as u16;
pub const LF: u16 = b'\n' as u16;

/// LCID for zh-TW (MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT)).
pub const LCID_ZH_TW: u32 = 0x0404;

/// `GetStringTypeW(CT_CTYPE3)` flags used by the Big5 filter.
pub const C3_SYMBOL: u16 = 0x0008;
pub const C3_IDEOGRAPH: u16 = 0x0100;
pub const C3_ALPHA: u16 = 0x8000;

/// Locale-sensitive text operations.
pub trait TextOps {
    /// `CompareString(locale, NORM_IGNORECASE, a, b)`.
    fn compare(&self, a: &[u16], b: &[u16]) -> Ordering;
    /// `GetStringTypeEx(locale, CT_CTYPE1) & C1_SPACE`.
    fn is_space(&self, ch: u16) -> bool;
    /// `GetStringTypeW(CT_CTYPE3)` for a single BMP code unit.
    fn ctype3(&self, ch: u16) -> u16;
    /// True if `ch` survives a lossless CP950 (Big5) round trip.
    fn cp950_round_trip(&self, ch: u16) -> bool;

    fn eq(&self, a: &[u16], b: &[u16]) -> bool {
        self.compare(a, b) == Ordering::Equal
    }
}

/// Encode an ASCII/BMP `&str` as UTF-16 (helper for literals and tests).
pub fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

/// CRT `towupper` in the default "C" locale: only ASCII letters are mapped.
pub fn towupper(ch: u16) -> u16 {
    if (b'a' as u16..=b'z' as u16).contains(&ch) {
        ch - 32
    } else {
        ch
    }
}

fn is_surrogate(ch: u16) -> bool {
    (0xD800..=0xDFFF).contains(&ch)
}

/// Port of `CStringRange::CharNext`.
///
/// Note the original quirk: it skips *every* consecutive surrogate unit and
/// then the first non-surrogate unit too.  Kept as-is for compatibility.
fn char_next(s: &[u16]) -> &[u16] {
    let mut i = 0;
    while i < s.len() {
        let sur = is_surrogate(s[i]);
        i += 1;
        if !sur {
            break;
        }
    }
    &s[i..]
}

/// Port of `CStringRange::WildcardCompare` (`*` and `?` wildcards,
/// case-insensitive per-character comparison).
///
/// Divergence from C++: when the target is exhausted but the pattern still
/// has a literal character, the C++ code compares against whatever code unit
/// follows the target in memory (an out-of-bounds read).  Here that case is
/// simply a mismatch.
pub fn wildcard_compare(ops: &dyn TextOps, pattern: &[u16], target: &[u16]) -> bool {
    let Some(&p0) = pattern.first() else {
        return target.is_empty();
    };
    let pattern_next = char_next(pattern);
    let target_next = char_next(target);

    if p0 == b'*' as u16 {
        return wildcard_compare(ops, pattern_next, target)
            || (!target.is_empty() && wildcard_compare(ops, pattern, target_next));
    }
    if p0 == b'?' as u16 {
        return !target.is_empty() && wildcard_compare(ops, pattern_next, target_next);
    }
    let Some(&t0) = target.first() else {
        return false;
    };
    let plen = if is_surrogate(p0) { 2 } else { 1 }.min(pattern.len());
    let tlen = if is_surrogate(t0) { 2 } else { 1 }.min(target.len());
    ops.eq(&pattern[..plen], &target[..tlen]) && wildcard_compare(ops, pattern_next, target_next)
}

// ---------------------------------------------------------------------------
// Windows implementation (exact parity with the C++ code)
// ---------------------------------------------------------------------------

#[cfg(windows)]
mod win {
    use super::*;

    const NORM_IGNORECASE: u32 = 0x0000_0001;
    const CT_CTYPE1: u32 = 1;
    const CT_CTYPE3: u32 = 4;
    const C1_SPACE: u16 = 0x0008;
    const CSTR_LESS_THAN: i32 = 1;
    const CSTR_EQUAL: i32 = 2;
    const CSTR_GREATER_THAN: i32 = 3;

    #[link(name = "kernel32")]
    extern "system" {
        fn CompareStringW(locale: u32, flags: u32, s1: *const u16, c1: i32, s2: *const u16, c2: i32) -> i32;
        fn GetStringTypeExW(locale: u32, info: u32, src: *const u16, cch: i32, out: *mut u16) -> i32;
        fn GetStringTypeW(info: u32, src: *const u16, cch: i32, out: *mut u16) -> i32;
        fn WideCharToMultiByte(
            cp: u32,
            flags: u32,
            wide: *const u16,
            cch_wide: i32,
            mb: *mut u8,
            cb_mb: i32,
            default_char: *const u8,
            used_default: *mut i32,
        ) -> i32;
        fn MultiByteToWideChar(cp: u32, flags: u32, mb: *const u8, cb_mb: i32, wide: *mut u16, cch_wide: i32) -> i32;
    }

    /// Calls the same Win32 APIs as the C++ implementation.
    #[derive(Clone, Copy, Debug)]
    pub struct WinTextOps {
        pub lcid: u32,
    }

    impl Default for WinTextOps {
        fn default() -> Self {
            Self { lcid: LCID_ZH_TW }
        }
    }

    fn clamp_len(s: &[u16]) -> i32 {
        i32::try_from(s.len()).unwrap_or(i32::MAX)
    }

    impl TextOps for WinTextOps {
        fn compare(&self, a: &[u16], b: &[u16]) -> Ordering {
            // SAFETY: pointers/lengths come from valid slices; zero lengths are allowed.
            let r = unsafe {
                CompareStringW(
                    self.lcid,
                    NORM_IGNORECASE,
                    a.as_ptr(),
                    clamp_len(a),
                    b.as_ptr(),
                    clamp_len(b),
                )
            };
            match r {
                CSTR_EQUAL => Ordering::Equal,
                CSTR_GREATER_THAN => Ordering::Greater,
                CSTR_LESS_THAN => Ordering::Less,
                // Error: the C++ code treats it as "not equal, not greater".
                _ => Ordering::Less,
            }
        }

        fn is_space(&self, ch: u16) -> bool {
            let mut t: u16 = 0;
            // SAFETY: one input unit, one output WORD.
            unsafe { GetStringTypeExW(self.lcid, CT_CTYPE1, &ch, 1, &mut t) };
            t & C1_SPACE != 0
        }

        fn ctype3(&self, ch: u16) -> u16 {
            let mut t: u16 = 0;
            // SAFETY: one input unit, one output WORD.
            unsafe { GetStringTypeW(CT_CTYPE3, &ch, 1, &mut t) };
            t
        }

        fn cp950_round_trip(&self, ch: u16) -> bool {
            let mut mb = [0u8; 4];
            // SAFETY: buffers are sized as passed.
            let r = unsafe {
                WideCharToMultiByte(
                    950,
                    0,
                    &ch,
                    1,
                    mb.as_mut_ptr(),
                    mb.len() as i32,
                    std::ptr::null(),
                    std::ptr::null_mut(),
                )
            };
            if r <= 0 {
                return false;
            }
            let mut rt: u16 = 0;
            // SAFETY: r bytes were written to mb; one output unit.
            let r2 = unsafe { MultiByteToWideChar(950, 0, mb.as_ptr(), r, &mut rt, 1) };
            r2 == 1 && rt == ch
        }
    }
}

#[cfg(windows)]
pub use win::WinTextOps;

// ---------------------------------------------------------------------------
// Portable approximation
// ---------------------------------------------------------------------------

/// Portable approximation: ordinal comparison with ASCII case folding and a
/// Unicode whitespace table.  Use only where Windows parity is not required.
#[derive(Clone, Copy, Debug, Default)]
pub struct SimpleTextOps;

impl TextOps for SimpleTextOps {
    fn compare(&self, a: &[u16], b: &[u16]) -> Ordering {
        a.iter().map(|&c| towupper(c)).cmp(b.iter().map(|&c| towupper(c)))
    }

    fn is_space(&self, ch: u16) -> bool {
        matches!(ch, 0x09..=0x0D | 0x20 | 0x85 | 0xA0 | 0x1680 | 0x2000..=0x200A | 0x2028 | 0x2029 | 0x202F | 0x205F | 0x3000)
    }

    fn ctype3(&self, ch: u16) -> u16 {
        if (0x3400..=0x4DBF).contains(&ch) || (0x4E00..=0x9FFF).contains(&ch) {
            return C3_IDEOGRAPH | C3_ALPHA;
        }
        match char::from_u32(ch as u32) {
            Some(c) if c.is_alphabetic() => C3_ALPHA,
            Some(c) if !c.is_alphanumeric() && !c.is_whitespace() && !c.is_control() => C3_SYMBOL,
            _ => 0,
        }
    }

    fn cp950_round_trip(&self, ch: u16) -> bool {
        // Without a codepage table, only ASCII is known to be in CP950.
        ch < 0x80
    }
}

/// The default [`TextOps`] for the current platform.
#[cfg(windows)]
pub fn default_ops() -> WinTextOps {
    WinTextOps::default()
}

#[cfg(not(windows))]
pub fn default_ops() -> SimpleTextOps {
    SimpleTextOps
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ops() -> impl TextOps {
        default_ops()
    }

    #[test]
    fn wildcard_basic() {
        let o = ops();
        assert!(wildcard_compare(&o, &w("a*"), &w("abc")));
        assert!(wildcard_compare(&o, &w("A?C"), &w("abc")));
        assert!(wildcard_compare(&o, &w("*"), &w("")));
        assert!(!wildcard_compare(&o, &w("a?"), &w("a")));
        assert!(!wildcard_compare(&o, &w("ab"), &w("a")));
        assert!(wildcard_compare(&o, &w("%chardef?begin"), &w("%chardef begin")));
        assert!(wildcard_compare(&o, &w("%selkey*"), &w("%selkey")));
    }

    #[test]
    fn compare_ignores_case() {
        let o = ops();
        assert!(o.eq(&w("%CharDef"), &w("%chardef")));
        assert!(!o.eq(&w("ab"), &w("abc")));
    }

    #[test]
    fn towupper_ascii_only() {
        assert_eq!(towupper(b'a' as u16), b'A' as u16);
        assert_eq!(towupper(0x00E9), 0x00E9);
    }
}
