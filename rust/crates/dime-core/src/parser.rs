//! Port of `CDictionaryParser` (DictionaryParser.cpp).
//!
//! The C++ parser works on `(pointer, length)` ranges into the dictionary
//! buffer and occasionally peeks one or two code units *before* a range or
//! *past* its end.  To reproduce that exactly, tokenisation here works on
//! absolute indices into the whole buffer (`base`), and every out-of-buffer
//! read yields `0` — where C++ would read past the allocation, this port
//! reads a terminating NUL instead.

use std::borrow::Cow;

use crate::text::{TextOps, BACKSLASH, CR, LF, QUOTE};

pub type Str<'a> = Cow<'a, [u16]>;

#[inline]
fn at(base: &[u16], i: usize) -> u16 {
    base.get(i).copied().unwrap_or(0)
}

#[inline]
fn before(base: &[u16], i: usize, n: usize) -> u16 {
    i.checked_sub(n).map_or(0, |j| at(base, j))
}

/// `base[start .. start+len]`, clamped to the buffer.
#[inline]
pub(crate) fn slice(base: &[u16], start: usize, len: usize) -> &[u16] {
    let s = start.min(base.len());
    let e = start.saturating_add(len).min(base.len());
    &base[s..e]
}

fn sub(s: Str<'_>, from: usize, to: usize) -> Str<'_> {
    match s {
        Cow::Borrowed(b) => Cow::Borrowed(&b[from..to]),
        Cow::Owned(v) => Cow::Owned(v[from..to].to_vec()),
    }
}

/// Result of [`DictionaryParser::parse_line`].
#[derive(Debug, Clone)]
pub struct ParsedLine<'a> {
    /// FALSE in C++: no keyword delimiter on the line.  `keyword` is still set.
    pub ok: bool,
    pub keyword: Str<'a>,
    pub values: Vec<Str<'a>>,
}

pub struct DictionaryParser<'o> {
    pub(crate) ops: &'o dyn TextOps,
    pub(crate) delimiter: u16,
}

impl<'o> DictionaryParser<'o> {
    pub fn new(ops: &'o dyn TextOps, keyword_delimiter: u16) -> Self {
        Self {
            ops,
            delimiter: keyword_delimiter,
        }
    }

    /// `GetOneLine`: length of the line starting at `pos`, bounded by `len`.
    pub fn get_one_line(base: &[u16], pos: usize, len: usize) -> usize {
        let mut i = 0;
        while i < len {
            let c = at(base, pos + i);
            if c == CR || c == LF || c == 0 {
                return i;
            }
            i += 1;
        }
        i
    }

    /// `GetToken`: scans `[start, start+len)` for `delim`, honouring
    /// double-quoted spans.  Returns the (cleaned) token and the absolute
    /// index of the delimiter, if found.
    pub fn get_token<'a>(&self, base: &'a [u16], start: usize, len: usize, delim: u16) -> (Str<'a>, Option<usize>) {
        let orig = slice(base, start, len);
        let mut p = start;
        let mut len = len;
        let mut ch = at(base, p);
        let mut pch: u16 = 0;

        while ch != 0 && ch != delim && len > 0 {
            len -= 1;
            p += 1;
            if ch == QUOTE && pch != BACKSLASH {
                // ignore escaped quote \" which is not a quote actually
                while at(base, p) != 0
                    && (at(base, p) != QUOTE || (before(base, p, 1) == BACKSLASH && before(base, p, 2) != BACKSLASH))
                    && len > 0
                {
                    len -= 1;
                    p += 1;
                }
                if at(base, p) != 0 && len > 0 {
                    len -= 1;
                    p += 1;
                } else {
                    // Unterminated quote: C++ returns the raw, untrimmed range.
                    return (Cow::Borrowed(orig), None);
                }
            }
            ch = at(base, p);
            pch = before(base, p, 1);
        }

        if at(base, p) != 0 && len > 0 {
            let tok = Cow::Borrowed(&base[start.min(p)..p.min(base.len())]);
            return (self.clean(tok), Some(p));
        }
        (self.clean(Cow::Borrowed(orig)), None)
    }

    /// trim begin → trim end → strip quotes / unescape.
    pub(crate) fn clean<'a>(&self, s: Str<'a>) -> Str<'a> {
        let s = self.trim_begin(s);
        let s = self.trim_end(s);
        self.remove_string_delimiter(s)
    }

    /// `RemoveWhiteSpaceFromBegin` (via `SkipWhiteSpace`).  Quirk preserved:
    /// a string that is entirely whitespace is left unchanged.
    pub(crate) fn trim_begin<'a>(&self, s: Str<'a>) -> Str<'a> {
        let mut i = 0;
        while i < s.len() && s[i] != 0 && self.ops.is_space(s[i]) {
            i += 1;
        }
        if i < s.len() && s[i] != 0 {
            let n = s.len();
            sub(s, i, n)
        } else {
            s
        }
    }

    /// `RemoveWhiteSpaceFromEnd`.
    pub(crate) fn trim_end<'a>(&self, s: Str<'a>) -> Str<'a> {
        let mut n = s.len();
        while n > 0 && (self.ops.is_space(s[n - 1]) || s[n - 1] == CR || s[n - 1] == LF) {
            n -= 1;
        }
        if n == s.len() {
            s
        } else {
            sub(s, 0, n)
        }
    }

    /// `RemoveStringDelimiter`: strips surrounding double quotes and, if the
    /// inner text contains a backslash, removes escape backslashes
    /// (`\\` → `\`, `\x` → `x`).
    pub(crate) fn remove_string_delimiter<'a>(&self, s: Str<'a>) -> Str<'a> {
        let n = s.len();
        if n < 2 || s[0] != QUOTE || s[n - 1] != QUOTE {
            return s;
        }
        // C++ calls GetToken on the inner range; it may peek at the opening quote.
        let has_backslash = self.get_token(&s, 1, n - 2, BACKSLASH).1.is_some();
        if !has_backslash {
            return sub(s, 1, n - 1);
        }
        let inner = &s[1..n - 1];
        let mut out = Vec::with_capacity(inner.len());
        for (i, &wch) in inner.iter().enumerate() {
            if wch == BACKSLASH {
                if i > 0 && inner[i - 1] == BACKSLASH {
                    out.push(BACKSLASH);
                }
            } else {
                out.push(wch);
            }
        }
        Cow::Owned(out)
    }

    /// `ParseLine`.
    ///
    /// * `want_values` – C++ `pValue != nullptr`
    /// * `tts_phrase`  – split the value on `,` (phrase / symbol sections)
    /// * `search_text` – strip the search prefix from the value (wildcard text search)
    pub fn parse_line<'a>(
        &self,
        base: &'a [u16],
        start: usize,
        len: usize,
        want_values: bool,
        tts_phrase: bool,
        search_text: Option<&[u16]>,
    ) -> ParsedLine<'a> {
        let (keyword, d) = self.get_token(base, start, len, self.delimiter);
        let Some(d) = d else {
            return ParsedLine {
                ok: false,
                keyword,
                values: Vec::new(),
            };
        };
        let mut len = len - (d - start) - 1;
        let mut start = d + 1;
        let mut values = Vec::new();

        if want_values && len > 0 {
            if tts_phrase {
                loop {
                    let (local, d2) = self.get_token(base, start, len, b',' as u16);
                    match d2 {
                        Some(d2) => {
                            len -= d2 - start + 1;
                            start = d2 + 1;
                            values.push(self.clean(local));
                        }
                        None => {
                            values.push(self.clean(Cow::Borrowed(slice(base, start, len))));
                            break;
                        }
                    }
                }
            } else {
                let raw: Str<'a> = Cow::Borrowed(slice(base, start, len));
                let value = match search_text {
                    None => raw,
                    Some(st) => {
                        let local = self.clean(raw);
                        let mut st_len = st.len();
                        if st_len > 1 {
                            st_len -= 1; // search text is "xxx*": drop the trailing wildcard
                        }
                        if local.len() == st_len {
                            local
                        } else {
                            let n = local.len();
                            sub(local, st_len.min(n), n)
                        }
                    }
                };
                values.push(self.clean(value));
            }
        }
        ParsedLine {
            ok: true,
            keyword,
            values,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::text::{default_ops, w};

    fn kv(line: &str, delim: char, tts: bool) -> (bool, String, Vec<String>) {
        let ops = default_ops();
        let p = DictionaryParser::new(&ops, delim as u16);
        let buf = w(line);
        let r = p.parse_line(&buf, 0, buf.len(), true, tts, None);
        (
            r.ok,
            String::from_utf16_lossy(&r.keyword),
            r.values.iter().map(|v| String::from_utf16_lossy(v)).collect(),
        )
    }

    #[test]
    fn cin_line() {
        assert_eq!(kv("a\t中", '\t', false), (true, "a".into(), vec!["中".into()]));
        assert_eq!(
            kv("  ab \t 中文 \r", '\t', false),
            (true, "ab".into(), vec!["中文".into()])
        );
    }

    #[test]
    fn quoted_tts_line() {
        assert_eq!(kv("\"a\"=\"中\"", '=', false), (true, "a".into(), vec!["中".into()]));
        // '=' inside quotes is not a delimiter
        assert_eq!(kv("\"a=b\"=\"x\"", '=', false), (true, "a=b".into(), vec!["x".into()]));
    }

    #[test]
    fn phrase_split() {
        assert_eq!(
            kv("\"k\"=\"一\",\"二\", 三", '=', true),
            (true, "k".into(), vec!["一".into(), "二".into(), "三".into()])
        );
        assert_eq!(kv("k=a,", '=', true), (true, "k".into(), vec!["a".into(), "".into()]));
    }

    #[test]
    fn escapes() {
        assert_eq!(
            kv(r#""a\"b"="c\\d""#, '=', false),
            (true, r#"a"b"#.into(), vec![r"c\d".into()])
        );
    }

    #[test]
    fn no_delimiter() {
        let (ok, k, v) = kv("%chardef begin", '\t', false);
        assert!(!ok);
        assert_eq!(k, "%chardef begin");
        assert!(v.is_empty());
    }

    #[test]
    fn unterminated_quote_returns_raw() {
        let (ok, k, _) = kv(" \"abc", '\t', false);
        assert!(!ok);
        assert_eq!(k, " \"abc");
    }

    #[test]
    fn all_whitespace_not_trimmed_from_begin_but_from_end() {
        let ops = default_ops();
        let p = DictionaryParser::new(&ops, '\t' as u16);
        let s = w("   ");
        assert_eq!(p.trim_begin(Cow::Borrowed(&s)).len(), 3);
        assert_eq!(p.trim_end(Cow::Borrowed(&s)).len(), 0);
    }
}
