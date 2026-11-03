//! Port of `CDictionarySearch` (DictionarySearch.cpp).
//!
//! `FindWorker` is a line-by-line state machine written with `goto` in C++.
//! It is reproduced here statement-for-statement (labelled blocks replace the
//! `TryAgain` / `ReadValue` / `FindNextLine` labels) because callers depend on
//! its exact quirks, e.g. a mapping line without a delimiter aborts the search,
//! and `%selkey` is only recognised when tab-separated.

use std::collections::BTreeMap;

use crate::parser::{DictionaryParser, Str};
use crate::text::{towupper, w, wildcard_compare, TextOps, CR, LF};

/// `SEARCH_SECTION` (TTS dictionaries only).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
#[repr(i32)]
pub enum SearchSection {
    #[default]
    Text = 0,
    Symbol = 1,
    Phrase = 2,
    PhraseFromKeystroke = 3,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Mode {
    None,
    Mapping,
    Text,
    Phrase,
    Radical,
    Config,
    ControlKey,
    Symbol,
    PhraseFromKeystroke,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ControlKey {
    Not,
    Cin,
    Tts,
}

/// `CDictionaryResult`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct DictionaryResult {
    pub search_key: Vec<u16>,
    pub find_key: Vec<u16>,
    pub phrases: Vec<Vec<u16>>,
}

/// Everything `ParseConfig` writes.  In C++ these are scattered over the
/// engine (`_pRadicalMap`, `_pRadicalIndexMap`, `_pSelkey`, `_pEndkey`),
/// `CConfig` setters and `Global::hasPhraseSection`.
#[derive(Clone, Debug, Default)]
pub struct ConfigState {
    /// `%keyname` / `[Radical]`: upper-cased key → radical text (≤ 15 units).
    pub radical_map: BTreeMap<u16, Vec<u16>>,
    /// Sorted-CIN index: upper-cased initial → offset (UTF-16 units) of its first
    /// chardef line.  `None` for non-CIN dictionaries (null pointer in C++).
    pub radical_index: Option<BTreeMap<u16, usize>>,
    pub selkey: Vec<u16>,
    pub endkey: Vec<u16>,
    /// `%autoCompose` → `CConfig::SetAutoCompose`.
    pub auto_compose: Option<bool>,
    /// Raw `[Config]` key/value pairs, in file order.  The C++ side maps
    /// them onto `CConfig` setters.
    pub config_entries: Vec<(Vec<u16>, Vec<u16>)>,
    /// `Global::hasPhraseSection` was set (TTS `[Phrase]` seen).
    pub has_phrase_section: bool,
}

const MAX_CAND_SELKEY: usize = 16;

fn truncate(s: &[u16], cap_with_nul: usize) -> Vec<u16> {
    s[..s.len().min(cap_with_nul - 1)].to_vec()
}

pub struct DictionarySearch<'a> {
    parser: DictionaryParser<'a>,
    ops: &'a dyn TextOps,
    buf: &'a [u16],
    key: Option<&'a [u16]>,
    /// `_charIndex`: always the start of a line in the buffer.
    pub char_index: usize,
    section: SearchSection,
    mode: Mode,
    sorted_found: bool,
    /// `Global::hasCINPhraseSection`.
    pub has_cin_phrase_section: bool,
}

enum Flow {
    NextLine,
    Return(bool),
}

impl<'a> DictionarySearch<'a> {
    pub fn new(ops: &'a dyn TextOps, buf: &'a [u16], key: Option<&'a [u16]>, keyword_delimiter: u16) -> Self {
        Self {
            parser: DictionaryParser::new(ops, keyword_delimiter),
            ops,
            buf,
            key,
            char_index: 0,
            section: SearchSection::Text,
            mode: Mode::None,
            sorted_found: false,
            has_cin_phrase_section: false,
        }
    }

    pub fn set_search_section(&mut self, s: SearchSection) {
        self.section = s;
    }

    pub fn set_search_offset(&mut self, offset: usize) {
        self.char_index = offset;
        self.mode = Mode::Mapping;
    }

    pub fn set_sorted_search_result_found(&mut self, v: bool) {
        self.sorted_found = v;
    }

    pub fn find_phrase(&mut self) -> Option<DictionaryResult> {
        self.find_worker(false, false, None).1
    }

    pub fn find_phrase_for_wildcard(&mut self) -> Option<DictionaryResult> {
        self.find_worker(false, true, None).1
    }

    pub fn find_converted_string(&mut self) -> Option<DictionaryResult> {
        self.find_worker(true, false, None).1
    }

    pub fn find_converted_string_for_wildcard(&mut self) -> Option<DictionaryResult> {
        self.find_worker(true, true, None).1
    }

    /// Returns whether the file declares `%sorted 1` (C++ return value).
    pub fn parse_config(&mut self, cfg: &mut ConfigState) -> bool {
        self.find_worker(false, false, Some(cfg)).0
    }

    fn key_eq(&self, s: &[u16]) -> bool {
        self.key.is_some_and(|k| self.ops.eq(s, k))
    }

    fn find_worker(
        &mut self,
        is_text_search: bool,
        is_wildcard: bool,
        mut cfg: Option<&mut ConfigState>,
    ) -> (bool, Option<DictionaryResult>) {
        let pc = cfg.is_some();
        let buf = self.buf;
        if self.char_index >= buf.len() {
            return (false, None);
        }
        let base = self.char_index;
        let mut total = buf.len() - base;
        let mut it: usize = 0; // indexTrace
        let mut result: Option<DictionaryResult> = None;
        let mut found = false;
        let mut last_initial: u16 = 0;
        let mut cin_sorted = false;
        let at = |i: usize| buf.get(i).copied().unwrap_or(0);

        loop {
            // TryAgain:
            let mut line_len = DictionaryParser::get_one_line(buf, base + it, total);

            if line_len != 0 {
                if at(base + it) == 0xFEFF {
                    total = total.saturating_sub(1);
                    it += 1; // bypass unicode byte order signature
                }
                let flow = self.process_line(
                    base + it,
                    line_len,
                    is_text_search,
                    is_wildcard,
                    cfg.as_deref_mut(),
                    &mut result,
                    &mut found,
                    &mut last_initial,
                    &mut cin_sorted,
                );
                if let Flow::Return(r) = flow {
                    return (r, if r && !pc { result } else { None });
                }
            }

            // FindNextLine:
            loop {
                total = total.saturating_sub(line_len);
                if total == 0 {
                    it += line_len;
                    self.char_index += it;
                    return if pc {
                        (cin_sorted, None)
                    } else if found {
                        (true, result)
                    } else {
                        (false, None)
                    };
                }
                it += line_len;
                let c = at(base + it);
                if c == CR || c == LF || c == 0 {
                    line_len = 1;
                    continue;
                }
                break;
            }
            if found {
                self.char_index += it;
                return (true, if pc { None } else { result });
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn process_line(
        &mut self,
        pos: usize,
        line_len: usize,
        is_text_search: bool,
        is_wildcard: bool,
        mut cfg: Option<&mut ConfigState>,
        result: &mut Option<DictionaryResult>,
        found: &mut bool,
        last_initial: &mut u16,
        cin_sorted: &mut bool,
    ) -> Flow {
        let pc = cfg.is_some();
        let buf = self.buf;
        let ops = self.ops;

        let control = match buf.get(pos).copied().unwrap_or(0) {
            0x23 /* # */ => return Flow::NextLine, // comment
            0x25 /* % */ => ControlKey::Cin,
            0x5B /* [ */ => ControlKey::Tts,
            _ => ControlKey::Not,
        };

        let first = self.parser.parse_line(buf, pos, line_len, false, false, None);
        if !first.ok && control == ControlKey::Not {
            // key without value
            return Flow::Return(if pc { *cin_sorted } else { false });
        }
        let keyword: Str<'a> = first.keyword;

        if !is_wildcard && self.sorted_found && !self.key_eq(&keyword) {
            // sorted search: stop once past the matching block
            return Flow::Return(false);
        }

        let mut go_read_value = false;

        if control != ControlKey::Not {
            let is = |lit: &str| ops.eq(&keyword, &w(lit));
            let like = |pat: &str| wildcard_compare(ops, &w(pat), &keyword);
            if control == ControlKey::Tts {
                if is("[Text]") {
                    self.mode = if !pc {
                        if is_text_search {
                            if self.section == SearchSection::Phrase {
                                Mode::None
                            } else {
                                Mode::Text
                            }
                        } else if self.section == SearchSection::Text {
                            Mode::Mapping
                        } else {
                            Mode::None
                        }
                    } else {
                        Mode::None
                    };
                } else if is("[Phrase]") {
                    if let Some(c) = cfg.as_deref_mut() {
                        c.has_phrase_section = true;
                    }
                    self.mode = if !pc && self.section == SearchSection::Phrase {
                        Mode::Phrase
                    } else {
                        Mode::None
                    };
                } else if pc && is("[Radical]") {
                    self.mode = Mode::Radical;
                } else if is("[Symbol]") {
                    self.mode = if !pc && !is_text_search && self.section == SearchSection::Symbol {
                        Mode::Symbol
                    } else {
                        Mode::None
                    };
                } else if is("[PhraseFromKeystroke]") {
                    self.mode = if !pc && !is_text_search && self.section == SearchSection::PhraseFromKeystroke {
                        Mode::PhraseFromKeystroke
                    } else {
                        Mode::None
                    };
                } else if pc && is("[Config]") {
                    self.mode = Mode::Config;
                }
                return Flow::NextLine;
            }
            // CIN control key
            if like("%chardef?begin") {
                if !pc {
                    if is_text_search {
                        self.mode = if self.has_cin_phrase_section {
                            Mode::None
                        } else {
                            Mode::Text
                        };
                    } else {
                        self.mode = Mode::Mapping;
                        *last_initial = 0;
                    }
                } else {
                    self.mode = if *cin_sorted { Mode::Mapping } else { Mode::None };
                    *last_initial = 0;
                }
            } else if like("%chardef?end") {
                self.mode = Mode::None;
            } else if pc && like("%keyname?begin") {
                self.mode = Mode::Radical;
                clear_index(cfg.as_deref_mut());
            } else if pc && like("%keyname?end") {
                self.mode = Mode::None;
            } else if pc
                && (like("%autoCompose*")
                    || like("%keyname*")
                    || like("%chardef*")
                    || like("%selkey*")
                    || like("%endkey*")
                    || like("%sorted*"))
            {
                self.mode = Mode::ControlKey;
                go_read_value = true;
            }
            if !go_read_value {
                return Flow::NextLine;
            }
        }

        if !go_read_value {
            // start searching keys
            match self.mode {
                Mode::Radical | Mode::Config => {}
                Mode::Mapping | Mode::Symbol | Mode::Phrase | Mode::PhraseFromKeystroke => {
                    // Compare dictionary key code with the input key code
                    // (exact compare, then wildcard compare — same as C++).
                    if let Some(k) = self.key {
                        if (!is_wildcard && !ops.eq(&keyword, k)) || !wildcard_compare(ops, k, &keyword) {
                            return Flow::NextLine;
                        }
                    }
                }
                Mode::Text => {
                    let conv = self.parser.parse_line(buf, pos, line_len, true, false, None);
                    if !conv.ok {
                        return Flow::Return(false);
                    }
                    if conv.values.len() != 1 {
                        return Flow::NextLine;
                    }
                    let v = &conv.values[0];
                    let Some(k) = self.key else { return Flow::NextLine };
                    if (!is_wildcard && !ops.eq(v, k)) || !wildcard_compare(ops, k, v) {
                        return Flow::NextLine;
                    }
                }
                _ => return Flow::NextLine,
            }
        }

        // ReadValue:
        if self.mode == Mode::None {
            return Flow::NextLine;
        }
        if !pc {
            *result = Some(DictionaryResult::default());
        }
        let is_phrase_entry = matches!(self.mode, Mode::Phrase | Mode::Symbol | Mode::PhraseFromKeystroke);
        let parsed = self.parser.parse_line(
            buf,
            pos,
            line_len,
            true,
            is_phrase_entry,
            if is_phrase_entry { self.key } else { None },
        );
        if !parsed.ok {
            if !pc {
                *result = None;
            }
            if control == ControlKey::Not {
                return Flow::Return(false);
            }
        }
        let keyword = parsed.keyword;
        let values = parsed.values;
        let v0: &[u16] = values.first().map(|v| &v[..]).unwrap_or(&[]);
        let v0_is = |lit: &str| ops.eq(v0, &w(lit));

        if self.mode == Mode::Radical {
            if let Some(c) = cfg.as_deref_mut() {
                let radical_char = keyword.first().copied().unwrap_or(0);
                c.radical_map.insert(towupper(radical_char), truncate(v0, 16));
                return Flow::NextLine;
            }
        }
        if self.mode == Mode::Config {
            if let Some(c) = cfg.as_deref_mut() {
                c.config_entries.push((keyword.to_vec(), v0.to_vec()));
            }
            return Flow::NextLine;
        }
        if self.mode == Mode::ControlKey && control == ControlKey::Cin {
            let kw_is = |lit: &str| ops.eq(&keyword, &w(lit));
            if let Some(c) = cfg.as_deref_mut() {
                if kw_is("%autoCompose") {
                    c.auto_compose = Some(v0_is("1"));
                } else if kw_is("%keyname") {
                    if v0_is("begin") {
                        self.mode = Mode::Radical;
                        c.radical_index.as_mut().map(BTreeMap::clear);
                    } else if v0_is("end") {
                        self.mode = Mode::None;
                    }
                } else if kw_is("%chardef") {
                    if v0_is("begin") {
                        self.mode = if *cin_sorted { Mode::Mapping } else { Mode::None };
                        *last_initial = 0;
                    } else if v0_is("end") {
                        self.mode = Mode::None;
                    }
                } else if kw_is("%selkey") {
                    c.selkey = truncate(v0, MAX_CAND_SELKEY);
                } else if kw_is("%endkey") {
                    c.endkey = truncate(v0, MAX_CAND_SELKEY);
                } else if kw_is("%sorted") && v0_is("1") {
                    *cin_sorted = true;
                }
            }
            return Flow::NextLine;
        }
        if let Some(c) = cfg {
            if *cin_sorted && self.mode == Mode::Mapping {
                if let Some(index) = c.radical_index.as_mut() {
                    // build radical index map
                    let current = keyword.first().copied().unwrap_or(0);
                    if current != *last_initial {
                        index.insert(towupper(current), pos - self.char_index);
                    }
                    *last_initial = current;
                    return Flow::NextLine;
                }
            }
        } else {
            // MAPPING / SYMBOL / PHRASE all go here.
            *result = Some(DictionaryResult {
                find_key: keyword.to_vec(),
                search_key: self.key.map(<[u16]>::to_vec).unwrap_or_default(),
                phrases: values.iter().map(|v| v.to_vec()).collect(),
            });
        }
        *found = true;
        Flow::NextLine
    }
}

fn clear_index(cfg: Option<&mut ConfigState>) {
    if let Some(index) = cfg.and_then(|c| c.radical_index.as_mut()) {
        index.clear();
    }
}
