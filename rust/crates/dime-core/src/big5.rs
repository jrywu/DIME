//! Port of the in-memory Big5 filter of `CMemoryFile` (File.cpp):
//! `SetupReadBuffer`'s section-selective copy and `FilterLine`.
//!
//! Cache-file I/O (`%src_mtime` header, `-Big5.cin` path) stays on the C++
//! side for now; this module is the pure transformation.

use crate::text::{TextOps, C3_ALPHA, C3_IDEOGRAPH, C3_SYMBOL, CR, LF, QUOTE};

const SP: u16 = b' ' as u16;
const TAB: u16 = b'\t' as u16;
const EQ: u16 = b'=' as u16;
const PCT: u16 = b'%' as u16;
const LBR: u16 = b'[' as u16;

/// `_wcsnicmp(s, lit, lit.len()) == 0` in the "C" locale (ASCII folding).
fn starts_with_ci(s: &[u16], lit: &str) -> bool {
    let lit = lit.as_bytes();
    s.len() >= lit.len()
        && s.iter().zip(lit).all(|(&a, &b)| {
            let a = if (b'A' as u16..=b'Z' as u16).contains(&a) {
                a + 32
            } else {
                a
            };
            a == u16::from(b.to_ascii_lowercase())
        })
}

fn skip_blanks(s: &[u16]) -> &[u16] {
    let n = s.iter().take_while(|&&c| c == SP || c == TAB).count();
    &s[n..]
}

#[derive(PartialEq, Eq)]
enum FileType {
    Unknown,
    Cin,
    Tts,
}

/// Returns the filtered buffer: only key→character mapping lines (CIN
/// `%chardef` block, TTS `[Text]`/`[Symbol]` sections) that pass
/// [`filter_line`], plus the CIN `%sorted` / `%selkey` / `%endkey` directives.
pub fn filter_buffer(ops: &dyn TextOps, src: &[u16]) -> Vec<u16> {
    let mut out = Vec::with_capacity(src.len());
    let mut file_type = FileType::Unknown;
    let mut in_section = false;
    let mut rp = 0;

    while rp < src.len() {
        let line_start = rp;
        while rp < src.len() && src[rp] != LF {
            rp += 1;
        }
        let mut line = &src[line_start..rp];
        if line.last() == Some(&CR) {
            line = &line[..line.len() - 1];
        }
        let had_newline = rp < src.len();
        if had_newline {
            rp += 1;
        }

        let p = skip_blanks(line);

        if file_type == FileType::Unknown && !p.is_empty() {
            if p[0] == PCT {
                file_type = FileType::Cin;
            } else if p[0] == LBR {
                file_type = FileType::Tts;
            }
        }

        if file_type == FileType::Cin && p.first() == Some(&PCT) {
            let d = &p[1..];
            if starts_with_ci(d, "chardef") {
                let d = skip_blanks(&d[7..]);
                if starts_with_ci(d, "begin") {
                    in_section = true;
                } else if starts_with_ci(d, "end") {
                    in_section = false;
                }
            }
        } else if file_type == FileType::Tts && p.first() == Some(&LBR) {
            let s = &p[1..];
            in_section = starts_with_ci(s, "Text]") || starts_with_ci(s, "Symbol]");
        }

        let critical = file_type == FileType::Cin
            && !in_section
            && p.first() == Some(&PCT)
            && (starts_with_ci(&p[1..], "sorted")
                || starts_with_ci(&p[1..], "selkey")
                || starts_with_ci(&p[1..], "endkey"));

        if (in_section || critical) && filter_line(ops, line) {
            out.extend_from_slice(line);
            if had_newline {
                out.push(LF);
            }
        }
    }
    out
}

/// `CMemoryFile::FilterLine`: TRUE if the line is kept.  Only single-character
/// values that are neither CP950-encodable nor symbols are rejected; CJK in the
/// supplementary ideographic planes (U+20000+) is rejected as well.
pub fn filter_line(ops: &dyn TextOps, line: &[u16]) -> bool {
    let end = line.len();
    let mut q = line.len() - skip_blanks(line).len();

    if q >= end || line[q] == PCT {
        return true;
    }

    // keycode token (optionally quoted)
    if line[q] == QUOTE {
        q += 1;
        while q < end && line[q] != QUOTE {
            q += 1;
        }
        if q < end {
            q += 1;
        }
    } else {
        while q < end && line[q] != SP && line[q] != TAB && line[q] != EQ {
            q += 1;
        }
    }
    while q < end && (line[q] == SP || line[q] == TAB || line[q] == EQ) {
        q += 1;
    }

    // value (optionally quoted)
    let (vs, ve);
    if q < end && line[q] == QUOTE {
        q += 1;
        vs = q;
        while q < end && line[q] != QUOTE {
            q += 1;
        }
        ve = q;
    } else {
        vs = q;
        while q < end && line[q] != SP && line[q] != TAB {
            q += 1;
        }
        ve = q;
    }
    let val = &line[vs..ve];

    match val.len() {
        0 => true,
        2 if (val[0] & 0xFC00) == 0xD800 && (val[1] & 0xFC00) == 0xDC00 => {
            // Same expression as C++ (note: `+` binds tighter than `|`).
            let cp = (0x10000u32 + ((u32::from(val[0]) & 0x3FF) << 10)) | (u32::from(val[1]) & 0x3FF);
            cp < 0x20000
        }
        1 => {
            let ch = val[0];
            let ct3 = ops.ctype3(ch);
            if ct3 & C3_SYMBOL != 0 || ops.cp950_round_trip(ch) {
                return true;
            }
            ct3 & (C3_IDEOGRAPH | C3_ALPHA) == 0
        }
        _ => true,
    }
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use crate::text::{w, WinTextOps};

    #[test]
    fn keeps_big5_drops_ext_a() {
        let ops = WinTextOps::default();
        assert!(filter_line(&ops, &w("a\t中")));
        assert!(!filter_line(&ops, &w("a\t\u{3400}"))); // CJK Ext-A
        assert!(filter_line(&ops, &w("a\t中文"))); // multi-char always kept
        assert!(filter_line(&ops, &w("a\t\u{1F600}"))); // SMP emoji
        assert!(!filter_line(&ops, &w("a\t\u{20000}"))); // Ext-B
        assert!(filter_line(&ops, &w("\"a\"=\"中\"")));
    }

    #[test]
    fn keeps_only_chardef_and_critical_directives() {
        let ops = WinTextOps::default();
        let src = w("%gen_inp\n%selkey\t123\n%keyname begin\na\t日\n%keyname end\n%chardef begin\na\t中\nb\t\u{3400}\n%chardef end\n");
        let out = String::from_utf16(&filter_buffer(&ops, &src)).unwrap();
        assert_eq!(out, "%selkey\t123\n%chardef begin\na\t中\n");
    }
}
