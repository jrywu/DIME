//! Emulation of the MSVC UCRT number parsers used by the C++ settings code
//! (`_wtoi`, `wcstol`, `wcstoul`).  The INI and CLI code paths feed raw user
//! text into these functions, so their exact behaviour (leading whitespace,
//! signs, `0x`/octal prefixes with base 0, non-ASCII digits, overflow
//! clamping) is part of the file format.

/// `iswspace` in the "C" locale.
pub fn is_space(c: u16) -> bool {
    matches!(
        c,
        0x09..=0x0D | 0x20 | 0x85 | 0xA0 | 0x1680 | 0x2000..=0x200A | 0x2028 | 0x2029 | 0x202F | 0x205F | 0x3000
    )
}

/// UCRT `_wchartodigit`: decimal digits of the Unicode blocks it recognises.
fn wchar_to_digit(c: u16) -> Option<u32> {
    const ZEROS: [u16; 18] = [
        0x0030, 0x0660, 0x06F0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66, 0x0C66, 0x0CE6, 0x0D66, 0x0E50,
        0x0ED0, 0x0F20, 0x1040, 0x17E0, 0x1810, 0xFF10,
    ];
    ZEROS.iter().find(|&&z| (z..z + 10).contains(&c)).map(|&z| u32::from(c - z))
}

/// Digit value of `c` in bases up to 36 (UCRT `parse_digit`).
fn digit(c: u16) -> Option<u32> {
    if let Some(d) = wchar_to_digit(c) {
        return Some(d);
    }
    match c {
        0x41..=0x5A => Some(u32::from(c - 0x41) + 10),
        0x61..=0x7A => Some(u32::from(c - 0x61) + 10),
        _ => None,
    }
}

/// Result of a `wcstoul`-style parse.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Parsed {
    /// Magnitude, saturated at `u64::MAX` (callers clamp to their type).
    pub magnitude: u64,
    pub negative: bool,
    /// Number of code units consumed (0 when no digits were found).
    pub consumed: usize,
}

/// Shared core of `wcstol` / `wcstoul` (UCRT `parse_integer`).
pub fn parse_integer(s: &[u16], base: u32) -> Parsed {
    let none = Parsed { magnitude: 0, negative: false, consumed: 0 };
    let mut i = 0;
    while i < s.len() && is_space(s[i]) {
        i += 1;
    }
    let mut negative = false;
    if i < s.len() && (s[i] == b'-' as u16 || s[i] == b'+' as u16) {
        negative = s[i] == b'-' as u16;
        i += 1;
    }
    let mut base = base;
    let is_x = |c: u16| c == b'x' as u16 || c == b'X' as u16;
    if (base == 0 || base == 16) && i < s.len() && wchar_to_digit(s[i]) == Some(0) {
        if i + 1 < s.len() && is_x(s[i + 1]) {
            // "0x" only counts as a prefix when a hex digit follows.
            if i + 2 < s.len() && digit(s[i + 2]).is_some_and(|d| d < 16) {
                i += 2;
                base = 16;
            } else if base == 0 {
                base = 8;
            }
        } else if base == 0 {
            base = 8;
        }
    }
    if base == 0 {
        base = 10;
    }
    let start = i;
    let mut magnitude: u64 = 0;
    while i < s.len() {
        let Some(d) = digit(s[i]).filter(|&d| d < base) else { break };
        magnitude = magnitude.saturating_mul(u64::from(base)).saturating_add(u64::from(d));
        i += 1;
    }
    if i == start {
        return none;
    }
    Parsed { magnitude, negative, consumed: i }
}

/// `wcstoul(s, &end, base)`: returns the value and the number of units consumed.
pub fn wcstoul(s: &[u16], base: u32) -> (u32, usize) {
    let p = parse_integer(s, base);
    if p.magnitude > u64::from(u32::MAX) {
        return (u32::MAX, p.consumed);
    }
    let v = p.magnitude as u32;
    (if p.negative { v.wrapping_neg() } else { v }, p.consumed)
}

/// `wcstol(s, &end, base)` (`long` is 32-bit on Windows).
pub fn wcstol(s: &[u16], base: u32) -> (i32, usize) {
    let p = parse_integer(s, base);
    let v = if p.negative {
        if p.magnitude > 1u64 << 31 { i32::MIN } else { (p.magnitude as i64).wrapping_neg() as i32 }
    } else if p.magnitude > i32::MAX as u64 {
        i32::MAX
    } else {
        p.magnitude as i32
    };
    (v, p.consumed)
}

/// `_wtoi(s)`.
pub fn wtoi(s: &[u16]) -> i32 {
    wcstol(s, 10).0
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_core::text::w;

    #[test]
    fn wtoi_basics() {
        assert_eq!(wtoi(&w("12")), 12);
        assert_eq!(wtoi(&w("  -7xyz")), -7);
        assert_eq!(wtoi(&w("+3")), 3);
        assert_eq!(wtoi(&w("abc")), 0);
        assert_eq!(wtoi(&w("")), 0);
        assert_eq!(wtoi(&w("99999999999")), i32::MAX);
        assert_eq!(wtoi(&w("-99999999999")), i32::MIN);
        assert_eq!(wtoi(&w("0x10")), 0);
        assert_eq!(wtoi(&w("１２")), 12); // full-width digits
    }

    #[test]
    fn wcstoul_base0() {
        assert_eq!(wcstoul(&w("0x1F"), 0), (0x1F, 4));
        assert_eq!(wcstoul(&w("010"), 0), (8, 3));
        assert_eq!(wcstoul(&w("09"), 0), (0, 1));
        assert_eq!(wcstoul(&w("0x"), 0), (0, 1));
        assert_eq!(wcstoul(&w("-1"), 0), (u32::MAX, 2));
        assert_eq!(wcstoul(&w("0x1FFFFFFFF"), 0).0, u32::MAX);
        assert_eq!(wcstoul(&w("FF"), 16), (0xFF, 2));
        assert_eq!(wcstoul(&w("0xFF"), 16), (0xFF, 4));
    }

    #[test]
    fn wcstol_base0() {
        assert_eq!(wcstol(&w("3"), 0), (3, 1));
        assert_eq!(wcstol(&w("-0x10"), 0), (-16, 5));
        assert_eq!(wcstol(&w("2147483648"), 0).0, i32::MAX);
        assert_eq!(wcstol(&w("-2147483648"), 0).0, i32::MIN);
        assert_eq!(wcstol(&w("-2147483649"), 0).0, i32::MIN);
    }
}
