//! Port of `CTableDictionaryEngine` (TableDictionaryEngine.cpp).

use std::cmp::Ordering;
use std::collections::BTreeMap;

use crate::search::{ConfigState, DictionarySearch, SearchSection};
use crate::text::{towupper, TextOps};

/// `DICTIONARY_TYPE`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum DictionaryType {
    Tts = 0,
    Ini = 1,
    Cin = 2,
    Lime = 3,
}

impl DictionaryType {
    pub fn from_i32(v: i32) -> Option<Self> {
        Some(match v {
            0 => Self::Tts,
            1 => Self::Ini,
            2 => Self::Cin,
            3 => Self::Lime,
            _ => return None,
        })
    }

    fn keyword_delimiter(self) -> u16 {
        match self {
            Self::Tts | Self::Ini => b'=' as u16,
            Self::Cin => b'\t' as u16,
            Self::Lime => b'|' as u16,
        }
    }
}

/// `CCandidateListItem`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CandidateItem {
    pub item: Vec<u16>,
    pub find_key: Vec<u16>,
    pub word_frequency: i32,
}

/// Dictionary engine over an in-memory UTF-16 buffer (file contents
/// without the BOM, exactly what `CFile::GetReadBufferPointer` returns).
pub struct TableDictionaryEngine<O: TextOps> {
    ops: O,
    buf: Vec<u16>,
    dict_type: DictionaryType,
    delimiter: u16,
    search_section: SearchSection,
    sorted_cin: bool,
    config: ConfigState,
    /// `Global::hasCINPhraseSection`.
    pub has_cin_phrase_section: bool,
}

impl<O: TextOps> TableDictionaryEngine<O> {
    pub fn new(ops: O, buf: Vec<u16>, dict_type: DictionaryType) -> Self {
        let config = ConfigState {
            radical_index: (dict_type == DictionaryType::Cin).then(BTreeMap::new),
            ..ConfigState::default()
        };
        Self {
            ops,
            buf,
            dict_type,
            delimiter: dict_type.keyword_delimiter(),
            search_section: SearchSection::Text,
            sorted_cin: false,
            config,
            has_cin_phrase_section: false,
        }
    }

    /// Replace the buffer after the underlying file changed.  Like the C++
    /// engine, call [`parse_config`](Self::parse_config) again afterwards.
    pub fn set_buffer(&mut self, buf: Vec<u16>) {
        self.buf = buf;
    }

    pub fn buffer(&self) -> &[u16] {
        &self.buf
    }

    pub fn dictionary_type(&self) -> DictionaryType {
        self.dict_type
    }

    pub fn set_search_section(&mut self, s: SearchSection) {
        self.search_section = s;
    }

    pub fn config(&self) -> &ConfigState {
        &self.config
    }

    pub fn is_sorted_cin(&self) -> bool {
        self.sorted_cin
    }

    fn search<'a>(&'a self, key: &'a [u16]) -> DictionarySearch<'a> {
        let mut s = DictionarySearch::new(&self.ops, &self.buf, Some(key), self.delimiter);
        s.has_cin_phrase_section = self.has_cin_phrase_section;
        if self.dict_type == DictionaryType::Tts {
            s.set_search_section(self.search_section);
        }
        s
    }

    /// `CollectWord(CStringRange*, CDIMEArray<CStringRange>*)` — no sorted index.
    pub fn collect_word_strings(&self, key: &[u16]) -> Vec<Vec<u16>> {
        let mut s = self.search(key);
        let mut out = Vec::new();
        while let Some(r) = s.find_phrase() {
            out.extend(r.phrases);
        }
        out
    }

    /// `CollectWord(CStringRange*, CDIMEArray<CCandidateListItem>*)`.
    pub fn collect_word(&self, key: &[u16]) -> Vec<CandidateItem> {
        let mut s = self.search(key);
        if self.sorted_cin {
            let initial = towupper(key.first().copied().unwrap_or(0));
            if let Some(&off) = self.config.radical_index.as_ref().and_then(|m| m.get(&initial)) {
                s.set_search_offset(off);
            }
        }
        let mut out = Vec::new();
        while let Some(r) = s.find_phrase() {
            if self.sorted_cin && !r.phrases.is_empty() {
                s.set_sorted_search_result_found(true);
            }
            push_items(&mut out, r.phrases, &r.find_key);
        }
        out
    }

    /// `CollectWordForWildcard`.  Duplicates (by locale-aware comparison)
    /// are dropped.  Quirk preserved: once a phrase of a result is a
    /// duplicate, the remaining phrases of that same result are dropped too.
    pub fn collect_word_for_wildcard<F: TextOps>(
        &self,
        key: &[u16],
        word_freq: Option<&TableDictionaryEngine<F>>,
    ) -> Vec<CandidateItem> {
        self.collect_word_for_wildcard_after(key, word_freq, &[])
    }

    /// Like [`collect_word_for_wildcard`](Self::collect_word_for_wildcard),
    /// but also treats `existing` (item strings already in the caller's list)
    /// as duplicates, as the C++ engine does.  Returns only the new items.
    pub fn collect_word_for_wildcard_after<F: TextOps>(
        &self,
        key: &[u16],
        word_freq: Option<&TableDictionaryEngine<F>>,
        existing: &[&[u16]],
    ) -> Vec<CandidateItem> {
        let mut s = self.search(key);
        let mut out: Vec<CandidateItem> = Vec::new();
        while let Some(r) = s.find_phrase_for_wildcard() {
            let mut dupped = false;
            for phrase in r.phrases {
                if existing.iter().any(|e| self.ops.eq(e, &phrase))
                    || out.iter().any(|it| self.ops.eq(&it.item, &phrase))
                {
                    dupped = true;
                }
                if dupped {
                    continue;
                }
                let mut item = CandidateItem {
                    item: phrase,
                    find_key: r.find_key.clone(),
                    word_frequency: 0,
                };
                if let Some(freq) = word_freq {
                    item.word_frequency = if item.item.len() > 1 {
                        i32::try_from(item.item.len()).unwrap_or(i32::MAX)
                    } else {
                        let list = freq.collect_word(&item.item);
                        if list.len() == 1 {
                            wtoi(&list[0].item)
                        } else {
                            0
                        }
                    };
                }
                out.push(item);
            }
        }
        out
    }

    /// `CollectWordFromConvertedString` (reverse lookup: character → key codes).
    pub fn collect_word_from_converted_string(&self, s: &[u16]) -> Vec<CandidateItem> {
        let mut search = self.search(s);
        let mut out = Vec::new();
        while let Some(r) = search.find_converted_string() {
            push_items(&mut out, r.phrases, &r.find_key);
        }
        out
    }

    /// `CollectWordFromConvertedStringForWildcard`.
    pub fn collect_word_from_converted_string_for_wildcard(&self, s: &[u16]) -> Vec<CandidateItem> {
        let mut search = self.search(s);
        let mut out = Vec::new();
        while let Some(r) = search.find_converted_string_for_wildcard() {
            push_items(&mut out, r.phrases, &r.find_key);
        }
        out
    }

    /// `ParseConfig`.  Radical map is reset first (except for INI files);
    /// selkey/endkey/index persist like the C++ member fields.
    pub fn parse_config(&mut self) {
        if self.dict_type != DictionaryType::Ini {
            self.config.radical_map.clear();
        }
        let mut cfg = std::mem::take(&mut self.config);
        // Per-parse events (C++ applies them immediately to CConfig/Global);
        // selkey/endkey/radical maps persist like the C++ member fields.
        cfg.has_phrase_section = false;
        cfg.auto_compose = None;
        cfg.config_entries.clear();
        let sorted = {
            let mut s = DictionarySearch::new(&self.ops, &self.buf, None, self.delimiter);
            s.has_cin_phrase_section = self.has_cin_phrase_section;
            s.parse_config(&mut cfg)
        };
        if sorted && self.dict_type == DictionaryType::Cin && cfg.radical_index.as_ref().is_some_and(|m| !m.is_empty())
        {
            self.sorted_cin = true;
        }
        self.config = cfg;
    }

    /// `SortListItemByFindKeyCode` (C++ merge sort reproduced exactly).
    pub fn sort_by_find_key(&self, items: &mut [CandidateItem]) {
        if !items.is_empty() {
            let last = items.len() - 1;
            let gt =
                |a: &CandidateItem, b: &CandidateItem| self.ops.compare(&a.find_key, &b.find_key) == Ordering::Greater;
            merge_sort(items, 0, last, &|l, m| !gt(l, m), &gt);
        }
    }

    /// `SortListItemByWordFrequency` (descending, negatives treated as 0).
    pub fn sort_by_word_frequency(&self, items: &mut [CandidateItem]) {
        if !items.is_empty() {
            let last = items.len() - 1;
            let f = |i: &CandidateItem| i.word_frequency.max(0);
            // Note: ties take the *right* element in the merge step, but are not swapped in the 2-element case.
            merge_sort(items, 0, last, &|l, m| f(l) > f(m), &|a, b| f(a) < f(b));
        }
    }
}

fn push_items(out: &mut Vec<CandidateItem>, phrases: Vec<Vec<u16>>, find_key: &[u16]) {
    out.extend(phrases.into_iter().map(|p| CandidateItem {
        item: p,
        find_key: find_key.to_vec(),
        word_frequency: 0,
    }));
}

/// CRT `_wtoi`: optional leading whitespace and sign, then decimal digits.
pub fn wtoi(s: &[u16]) -> i32 {
    let mut i = 0;
    while i < s.len() && matches!(s[i], 0x20 | 0x09..=0x0D) {
        i += 1;
    }
    let mut neg = false;
    if i < s.len() && (s[i] == b'-' as u16 || s[i] == b'+' as u16) {
        neg = s[i] == b'-' as u16;
        i += 1;
    }
    let mut v: i64 = 0;
    while i < s.len() && (b'0' as u16..=b'9' as u16).contains(&s[i]) {
        v = (v * 10 + i64::from(s[i] - b'0' as u16)).min(i64::from(i32::MAX) + 1);
        i += 1;
    }
    let v = if neg { -v } else { v };
    v.clamp(i64::from(i32::MIN), i64::from(i32::MAX)) as i32
}

/// The C++ `MergeSortByFindKeyCode` / `MergeSortByWordFrequency` algorithm,
/// reproduced exactly (including its overlapping `[left, mid]` / `[mid, right]`
/// recursion, which does not always produce a fully sorted list).
///
/// The C++ code uses two independent conditions, so they are passed separately:
/// * `take_left(l, m)` — merge step: take the left element (else the right one);
/// * `swap(a, b)` — 2-element case: swap the pair.
type Pred<'a, T> = &'a dyn Fn(&T, &T) -> bool;

fn merge_sort<T: Clone>(items: &mut [T], left: usize, right: usize, take_left: Pred<T>, swap: Pred<T>) {
    let count = right - left + 1;
    if count > 2 {
        let mid = left + count / 2;
        merge_sort(items, left, mid, take_left, swap);
        merge_sort(items, mid, right, take_left, swap);

        let mut tmp: Vec<T> = Vec::with_capacity(right - left);
        let (mut l, mut m) = (left, mid);
        while l != mid || m != right {
            if l == mid {
                tmp.push(items[m].clone());
                m += 1;
            } else if m == right || take_left(&items[l], &items[m]) {
                tmp.push(items[l].clone());
                l += 1;
            } else {
                tmp.push(items[m].clone());
                m += 1;
            }
        }
        for (k, v) in tmp.into_iter().enumerate() {
            items[left + k] = v;
        }
    } else if count == 2 && swap(&items[left], &items[left + 1]) {
        items.swap(left, left + 1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::text::{default_ops, w};

    fn engine(src: &str, t: DictionaryType) -> TableDictionaryEngine<impl TextOps> {
        let mut e = TableDictionaryEngine::new(default_ops(), w(src), t);
        e.parse_config();
        e
    }

    fn items(v: &[CandidateItem]) -> Vec<String> {
        v.iter().map(|i| String::from_utf16_lossy(&i.item)).collect()
    }

    const CIN: &str = "%gen_inp\n%ename Test\n%selkey\t1234567890\n%keyname begin\na\t日\nb\t月\n%keyname end\n%chardef begin\na\t中\na\t央\nab\t文\nb\t字\n%chardef end\n";

    #[test]
    fn collect_word_cin() {
        let e = engine(CIN, DictionaryType::Cin);
        assert_eq!(items(&e.collect_word(&w("a"))), ["中", "央"]);
        assert_eq!(items(&e.collect_word(&w("A"))), ["中", "央"]); // case-insensitive
        assert_eq!(items(&e.collect_word(&w("ab"))), ["文"]);
        assert!(e.collect_word(&w("zz")).is_empty());
    }

    #[test]
    fn parse_config_cin() {
        let e = engine(CIN, DictionaryType::Cin);
        let c = e.config();
        assert_eq!(c.selkey, w("1234567890"));
        assert_eq!(c.radical_map.get(&(b'A' as u16)), Some(&w("日")));
        assert_eq!(c.radical_map.get(&(b'B' as u16)), Some(&w("月")));
        assert!(!e.is_sorted_cin());
    }

    #[test]
    fn sorted_cin_uses_index() {
        let src = "%sorted\t1\n%chardef begin\na00\t字\na01\t詞\nb00\t測\nb01\t試\nc00\t驗\n%chardef end\n";
        let e = engine(src, DictionaryType::Cin);
        assert!(e.is_sorted_cin());
        let idx = e.config().radical_index.as_ref().unwrap();
        assert_eq!(idx.len(), 3);
        assert_eq!(items(&e.collect_word(&w("b01"))), ["試"]);
        assert_eq!(items(&e.collect_word(&w("c00"))), ["驗"]);
    }

    #[test]
    fn wildcard_dedupes() {
        let e = engine(CIN, DictionaryType::Cin);
        let r = e.collect_word_for_wildcard::<crate::text::SimpleTextOps>(&w("a*"), None);
        assert_eq!(items(&r), ["中", "央", "文"]);
    }

    #[test]
    fn reverse_lookup() {
        let e = engine(CIN, DictionaryType::Cin);
        let r = e.collect_word_from_converted_string(&w("文"));
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].find_key, w("ab"));
    }

    #[test]
    fn tts_sections() {
        // A comma inside a quoted value does not split it; separate quoted items do.
        let src = "[Text]\n\"a\"=\"中\"\n[Phrase]\n\"中\"=\"中文\",\"中國\"\n\"x\"=\"1,2\"\n[Config]\nFontSize=12\n";
        let mut e = engine(src, DictionaryType::Tts);
        assert_eq!(items(&e.collect_word(&w("a"))), ["中"]);
        assert!(e.config().has_phrase_section);
        assert_eq!(e.config().config_entries, vec![(w("FontSize"), w("12"))]);
        e.set_search_section(SearchSection::Phrase);
        assert_eq!(items(&e.collect_word(&w("中"))), ["中文", "中國"]);
        assert_eq!(items(&e.collect_word(&w("x"))), ["1,2"]);
    }

    #[test]
    fn line_without_delimiter_aborts_search() {
        let e = engine(
            "%chardef begin\na\t中\nbroken\nc\t字\n%chardef end\n",
            DictionaryType::Cin,
        );
        assert!(e.collect_word(&w("c")).is_empty());
    }

    #[test]
    fn sort_by_find_key_and_frequency() {
        let e = engine(CIN, DictionaryType::Cin);
        let mk = |k: &str, f: i32| CandidateItem {
            item: w(k),
            find_key: w(k),
            word_frequency: f,
        };
        let mut v = vec![mk("c", 1), mk("a", 3), mk("b", 2)];
        e.sort_by_find_key(&mut v);
        assert_eq!(items(&v), ["a", "b", "c"]);
        let mut v = vec![mk("x", 1), mk("y", 3), mk("z", 2)];
        e.sort_by_word_frequency(&mut v);
        assert_eq!(items(&v), ["y", "z", "x"]);
        // C++ quirk (found by the parity test): equal frequencies are reordered.
        let mut v = vec![mk("a", 1), mk("b", 1), mk("c", 1)];
        e.sort_by_word_frequency(&mut v);
        assert_eq!(items(&v), ["b", "a", "c"]);
    }

    #[test]
    fn wtoi_behaves_like_crt() {
        assert_eq!(wtoi(&w(" 123abc")), 123);
        assert_eq!(wtoi(&w("-7")), -7);
        assert_eq!(wtoi(&w("x")), 0);
    }
}
