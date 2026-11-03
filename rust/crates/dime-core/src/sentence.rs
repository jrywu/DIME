//! Sentence conversion ("整句輸入"): pick one candidate per typed character
//! so that the whole sentence reads best.
//!
//! The caller types one code per character and does not choose; every
//! character position therefore carries the full candidate list the main
//! table returned for its code (in table order).  [`convert`] builds a lattice
//! over those positions: every candidate is a one-position edge, and every
//! lexicon word whose characters appear, in order, among the candidates of
//! consecutive positions is a multi-position edge.  A Viterbi pass picks the
//! best-scoring path.
//!
//! The lexicon only needs words (optionally with frequencies), not codes, so
//! the same word list serves every input method.  Positions the user fixed by
//! hand are "pinned" and only allow that one candidate.
//!
//! Scoring (log domain, higher is better) is deliberately simple for now:
//! * a single character scores `ln((freq + 1) / (total + chars + 1))` from the
//!   character-frequency table, minus [`RANK_PENALTY`] per place it sits below
//!   the first candidate (the table order is a curated prior);
//! * a word of `L` characters scores the sum of its character scores (each
//!   floored at [`CHAR_FLOOR_IN_WORD`], so rare characters do not sink real
//!   words) plus [`WORD_BONUS`] for every character after the first, plus a
//!   small bonus from the word's own frequency when the lexicon has one.
//!
//! Learning ([`UserModel`]): when the user fixes a character by hand, the
//! model remembers that the code was meant to be that character, and the
//! pairs it forms with its neighbours as user words.  Both add a bonus that
//! grows with how often the choice was made.

use std::collections::{HashMap, HashSet};

/// Penalty for each place a candidate sits below the first one.
pub const RANK_PENALTY: f64 = 1.5;
/// Bonus for each character of a lexicon word after the first.
pub const WORD_BONUS: f64 = 8.0;
/// Lowest character score counted inside a lexicon word.
pub const CHAR_FLOOR_IN_WORD: f64 = -14.0;
/// Weight of `ln(1 + word_freq)` for words that come with a frequency.
pub const WORD_FREQ_WEIGHT: f64 = 0.5;
/// Longest word (in characters) the lexicon keeps.
pub const MAX_WORD_LEN: usize = 8;
/// Bonus for a candidate the user has chosen for this code before.
pub const USER_CHOICE_BONUS: f64 = 2.0;
/// Extra bonus for a user word on top of the normal word bonus.
pub const USER_WORD_BONUS: f64 = 4.0;
/// Weight of `ln(count)` for repeated user choices / words.
pub const USER_COUNT_WEIGHT: f64 = 1.0;
/// Most user words / choices kept when exporting.
pub const MAX_USER_ENTRIES: usize = 20000;

const ROOT: u32 = 0;
const USER_FILE_HEADER: &str = "# DIME sentence learning v1";

/// Character trie mapping words to a value (frequency or count).
#[derive(Default, Clone)]
struct Trie {
    /// (parent node, code point) -> child node.
    edges: HashMap<(u32, u32), u32>,
    /// Per node: `Some(value)` when the path from the root spells a word.
    word: Vec<Option<u32>>,
    words: usize,
}

impl Trie {
    fn new() -> Self {
        Trie {
            word: vec![None],
            ..Default::default()
        }
    }

    /// Insert `cps`; an existing value is combined with `combine`.  Returns
    /// `true` when the word is new.
    fn insert(&mut self, cps: &[u32], value: u32, combine: fn(u32, u32) -> u32) -> bool {
        let mut node = ROOT;
        for &c in cps {
            node = match self.edges.get(&(node, c)) {
                Some(&n) => n,
                None => {
                    let n = self.word.len() as u32;
                    self.word.push(None);
                    self.edges.insert((node, c), n);
                    n
                }
            };
        }
        let slot = &mut self.word[node as usize];
        match slot {
            Some(v) => {
                *v = combine(*v, value);
                false
            }
            None => {
                *slot = Some(value);
                self.words += 1;
                true
            }
        }
    }

    fn child(&self, node: u32, ch: u32) -> Option<u32> {
        self.edges.get(&(node, ch)).copied()
    }

    fn get(&self, cps: &[u32]) -> Option<u32> {
        let mut node = ROOT;
        for &c in cps {
            node = self.child(node, c)?;
        }
        self.word[node as usize]
    }

    /// Every word with its value.
    fn entries(&self) -> Vec<(Vec<u32>, u32)> {
        let mut children: HashMap<u32, Vec<(u32, u32)>> = HashMap::new();
        for (&(parent, cp), &child) in &self.edges {
            children.entry(parent).or_default().push((cp, child));
        }
        let mut out = Vec::new();
        let mut stack = vec![(ROOT, Vec::new())];
        while let Some((node, path)) = stack.pop() {
            if let Some(v) = self.word[node as usize] {
                out.push((path.clone(), v));
            }
            if let Some(kids) = children.get(&node) {
                for &(cp, child) in kids {
                    let mut p = path.clone();
                    p.push(cp);
                    stack.push((child, p));
                }
            }
        }
        out
    }
}

/// Word list (as a character trie) plus single-character frequencies.
pub struct Lexicon {
    trie: Trie,
    char_freq: HashMap<u32, u32>,
    char_total: u64,
}

impl Default for Lexicon {
    fn default() -> Self {
        Self::new()
    }
}

impl Lexicon {
    pub fn new() -> Self {
        Lexicon {
            trie: Trie::new(),
            char_freq: HashMap::new(),
            char_total: 0,
        }
    }

    /// Number of distinct words (two characters or more).
    pub fn word_count(&self) -> usize {
        self.trie.words
    }

    /// Number of characters with a frequency.
    pub fn char_count(&self) -> usize {
        self.char_freq.len()
    }

    /// Add a word of two or more characters.  Returns `true` if it was new.
    /// Words longer than [`MAX_WORD_LEN`] or containing ASCII are ignored.
    pub fn add_word(&mut self, word: &[u16], freq: u32) -> bool {
        let Some(cps) = code_points(word) else { return false };
        if cps.len() < 2 || cps.len() > MAX_WORD_LEN || cps.iter().any(|&c| c < 0x80) {
            return false;
        }
        self.trie.insert(&cps, freq, u32::max)
    }

    /// Set the frequency of a single character (keeps the larger value).
    pub fn add_char_freq(&mut self, ch: u32, freq: u32) {
        let e = self.char_freq.entry(ch).or_insert(0);
        if freq > *e {
            self.char_total += u64::from(freq - *e);
            *e = freq;
        }
    }

    /// Add every multi-character value of a `.cin` table (`code<TAB>value`,
    /// optionally quoted) as a word, e.g. `Array-Phrase.cin`.  Only the
    /// `%chardef` section is read when the file has one.  Returns the number
    /// of new words.
    pub fn load_cin_values(&mut self, buf: &[u16]) -> usize {
        let mut added = 0;
        for (_, value) in cin_entries(buf) {
            if self.add_word(value, 0) {
                added += 1;
            }
        }
        added
    }

    /// Load a frequency table (`text<TAB>count`, e.g. `TCFreq.cin`): single
    /// characters go to the character frequencies, longer text becomes a word
    /// with that frequency.  Returns the number of lines used.
    pub fn load_freq_table(&mut self, buf: &[u16]) -> usize {
        let mut used = 0;
        for (text, value) in cin_entries(buf) {
            let Some(freq) = parse_u32(value) else { continue };
            let Some(cps) = code_points(text) else { continue };
            match cps.len() {
                0 => {}
                1 => {
                    self.add_char_freq(cps[0], freq);
                    used += 1;
                }
                _ => {
                    self.add_word(text, freq);
                    used += 1;
                }
            }
        }
        used
    }

    fn char_score(&self, cand: &Cand) -> f64 {
        let base = match cand.cp {
            Some(cp) if self.char_total > 0 => {
                let f = f64::from(self.char_freq.get(&cp).copied().unwrap_or(0));
                ((f + 1.0) / (self.char_total as f64 + self.char_freq.len() as f64 + 1.0)).ln()
            }
            // No frequency data, or a multi-character candidate: neutral.
            _ => 0.0,
        };
        base - RANK_PENALTY * cand.rank as f64
    }
}

/// What the user taught the conversion: preferred characters per code and
/// user words.  Counts only grow; [`UserModel::merge`] adds another model's
/// counts (used to combine the file on disk with this process's new data).
#[derive(Default, Clone)]
pub struct UserModel {
    words: Trie,
    choices: HashMap<(Vec<u16>, u32), u32>,
}

impl UserModel {
    pub fn new() -> Self {
        UserModel {
            words: Trie::new(),
            choices: HashMap::new(),
        }
    }

    pub fn word_count(&self) -> usize {
        self.words.words
    }

    pub fn choice_count(&self) -> usize {
        self.choices.len()
    }

    pub fn is_empty(&self) -> bool {
        self.word_count() == 0 && self.choice_count() == 0
    }

    pub fn clear(&mut self) {
        *self = UserModel::new();
    }

    /// Record that `code` was meant to be `ch` (`count` more times).
    pub fn add_choice(&mut self, code: &[u16], ch: u32, count: u32) {
        if code.is_empty() || count == 0 {
            return;
        }
        let e = self.choices.entry((code.to_vec(), ch)).or_insert(0);
        *e = e.saturating_add(count);
    }

    /// Record a user word (`count` more times).  Same rules as
    /// [`Lexicon::add_word`].
    pub fn add_word(&mut self, word: &[u16], count: u32) -> bool {
        let Some(cps) = code_points(word) else { return false };
        if cps.len() < 2 || cps.len() > MAX_WORD_LEN || cps.iter().any(|&c| c < 0x80) || count == 0 {
            return false;
        }
        self.words.insert(&cps, count, u32::saturating_add);
        true
    }

    /// Times `ch` was chosen for `code`.
    pub fn choice(&self, code: &[u16], ch: u32) -> u32 {
        self.choices.get(&(code.to_vec(), ch)).copied().unwrap_or(0)
    }

    /// Times `word` was learned.
    pub fn word(&self, word: &[u16]) -> u32 {
        code_points(word).and_then(|cps| self.words.get(&cps)).unwrap_or(0)
    }

    /// Learn from a committed sentence: every pinned position whose chosen
    /// candidate is a single Han character teaches the code -> character
    /// choice (when the code had alternatives) and the pairs it forms with its
    /// Han neighbours.  Returns `true` if anything was learned.
    pub fn learn(&mut self, positions: &[Position], chosen: &[usize]) -> bool {
        let chars: Vec<Option<u32>> = positions
            .iter()
            .enumerate()
            .map(|(i, p)| {
                let k = chosen.get(i).copied().unwrap_or(0);
                p.candidates.get(k).and_then(|c| single_cp(c)).filter(|&cp| is_han(cp))
            })
            .collect();
        let mut pairs: HashSet<usize> = HashSet::new();
        let mut learned = false;
        for (i, p) in positions.iter().enumerate() {
            if p.pinned.is_none() {
                continue;
            }
            let Some(cp) = chars[i] else { continue };
            if p.candidates.len() > 1 {
                self.add_choice(p.code, cp, 1);
                learned = true;
            }
            if i > 0 && chars[i - 1].is_some() {
                pairs.insert(i - 1);
            }
            if i + 1 < positions.len() && chars[i + 1].is_some() {
                pairs.insert(i);
            }
        }
        for start in pairs {
            let (Some(a), Some(b)) = (chars[start], chars[start + 1]) else {
                continue;
            };
            let word: String = [a, b].iter().filter_map(|&c| char::from_u32(c)).collect();
            let units: Vec<u16> = word.encode_utf16().collect();
            learned |= self.add_word(&units, 1);
        }
        learned
    }

    /// Add all counts of `other`.
    pub fn merge(&mut self, other: &UserModel) {
        for ((code, ch), count) in &other.choices {
            self.add_choice(code, *ch, *count);
        }
        for (cps, count) in other.words.entries() {
            self.words.insert(&cps, count, u32::saturating_add);
        }
    }

    /// Read the text format written by [`UserModel::export`], adding counts.
    /// Unknown or malformed lines are skipped.  Returns the number of entries read.
    pub fn import(&mut self, buf: &[u16]) -> usize {
        let text = String::from_utf16_lossy(buf);
        let mut read = 0;
        for line in text.lines() {
            let fields: Vec<&str> = line.trim_end_matches('\r').split('\t').collect();
            match fields.as_slice() {
                ["W", word, count] => {
                    if let Ok(n) = count.parse::<u32>() {
                        let units: Vec<u16> = word.encode_utf16().collect();
                        if self.add_word(&units, n) {
                            read += 1;
                        }
                    }
                }
                ["C", code, ch, count] => {
                    let mut chars = ch.chars();
                    if let (Some(c), None, Ok(n)) = (chars.next(), chars.next(), count.parse::<u32>()) {
                        if n > 0 && !code.is_empty() {
                            let units: Vec<u16> = code.encode_utf16().collect();
                            self.add_choice(&units, u32::from(c), n);
                            read += 1;
                        }
                    }
                }
                _ => {}
            }
        }
        read
    }

    /// Text form (UTF-16, no BOM): a header line, then `W<TAB>word<TAB>count`
    /// and `C<TAB>code<TAB>char<TAB>count` lines, most used first, at most
    /// [`MAX_USER_ENTRIES`] of each.
    pub fn export(&self) -> Vec<u16> {
        let mut words: Vec<(String, u32)> = self
            .words
            .entries()
            .into_iter()
            .map(|(cps, n)| (cps.iter().filter_map(|&c| char::from_u32(c)).collect(), n))
            .collect();
        words.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        words.truncate(MAX_USER_ENTRIES);

        let mut choices: Vec<(String, char, u32)> = self
            .choices
            .iter()
            .filter_map(|((code, ch), n)| Some((String::from_utf16(code).ok()?, char::from_u32(*ch)?, *n)))
            .filter(|(code, _, _)| !code.contains(['\t', '\r', '\n']))
            .collect();
        choices.sort_by(|a, b| b.2.cmp(&a.2).then_with(|| (&a.0, a.1).cmp(&(&b.0, b.1))));
        choices.truncate(MAX_USER_ENTRIES);

        let mut out = String::from(USER_FILE_HEADER);
        out.push_str("\r\n");
        for (word, n) in words {
            out.push_str(&format!("W\t{word}\t{n}\r\n"));
        }
        for (code, ch, n) in choices {
            out.push_str(&format!("C\t{code}\t{ch}\t{n}\r\n"));
        }
        out.encode_utf16().collect()
    }

    fn choice_bonus(&self, code: &[u16], cand: &Cand) -> f64 {
        match cand.cp {
            Some(cp) if !code.is_empty() => count_bonus(USER_CHOICE_BONUS, self.choice(code, cp)),
            _ => 0.0,
        }
    }
}

fn count_bonus(base: f64, count: u32) -> f64 {
    if count == 0 {
        0.0
    } else {
        base + USER_COUNT_WEIGHT * f64::from(count).ln()
    }
}

/// One character position: the code typed, its candidates in table order,
/// and the index the user pinned (if any).
pub struct Position<'a> {
    pub code: &'a [u16],
    pub candidates: Vec<&'a [u16]>,
    pub pinned: Option<usize>,
}

struct Cand {
    index: usize,
    rank: usize,
    /// The code point when the candidate is exactly one character.
    cp: Option<u32>,
    /// Character score including the user's preference for this code.
    score: f64,
}

#[derive(Clone)]
enum Back {
    None,
    /// One position, candidate index (or `usize::MAX` for an empty position).
    Single(usize),
    /// A word starting at `start`, with the candidate index for each position.
    Word(Vec<usize>),
}

/// Choose one candidate index for every position without learned data.
pub fn convert(lex: &Lexicon, positions: &[Position]) -> Vec<usize> {
    convert_with_user(lex, None, positions)
}

/// Choose one candidate index for every position.  Pinned positions always
/// keep their pin; positions without candidates get index 0.
pub fn convert_with_user(lex: &Lexicon, user: Option<&UserModel>, positions: &[Position]) -> Vec<usize> {
    let n = positions.len();
    if n == 0 {
        return Vec::new();
    }

    let allowed: Vec<Vec<Cand>> = positions
        .iter()
        .map(|p| {
            let make = |index: usize, rank: usize| {
                let mut c = Cand {
                    index,
                    rank,
                    cp: single_cp(p.candidates[index]),
                    score: 0.0,
                };
                c.score = lex.char_score(&c) + user.map_or(0.0, |u| u.choice_bonus(p.code, &c));
                c
            };
            match p.pinned {
                Some(i) if i < p.candidates.len() => vec![make(i, 0)],
                _ => (0..p.candidates.len()).map(|i| make(i, i)).collect(),
            }
        })
        .collect();

    let mut best = vec![f64::NEG_INFINITY; n + 1];
    let mut back: Vec<(usize, Back)> = vec![(0, Back::None); n + 1];
    best[0] = 0.0;

    let lexicon_bonus = |freq: u32| {
        if freq > 0 {
            WORD_FREQ_WEIGHT * (1.0 + f64::from(freq)).ln()
        } else {
            0.0
        }
    };
    let user_bonus = |count: u32| count_bonus(USER_WORD_BONUS, count);

    for i in 0..n {
        if best[i] == f64::NEG_INFINITY {
            continue;
        }
        if allowed[i].is_empty() {
            let s = best[i];
            relax(&mut best, &mut back, i + 1, s, i, Back::Single(usize::MAX));
            continue;
        }
        for c in &allowed[i] {
            let s = best[i] + c.score;
            relax(&mut best, &mut back, i + 1, s, i, Back::Single(c.index));
        }
        let mut search = WordSearch {
            allowed: &allowed,
            start: i,
            best: &mut best,
            back: &mut back,
        };
        search.extend(&lex.trie, &lexicon_bonus, i, ROOT, 0.0, &mut Vec::new());
        if let Some(u) = user {
            search.extend(&u.words, &user_bonus, i, ROOT, 0.0, &mut Vec::new());
        }
    }

    let mut chosen = vec![0usize; n];
    let mut end = n;
    while end > 0 {
        let (start, b) = &back[end];
        match b {
            Back::Single(k) => chosen[*start] = if *k == usize::MAX { 0 } else { *k },
            Back::Word(ks) => {
                for (d, k) in ks.iter().enumerate() {
                    chosen[start + d] = *k;
                }
            }
            Back::None => unreachable!("every reachable end has a back pointer"),
        }
        end = *start;
    }
    chosen
}

/// Depth-first search for words starting at one position.
struct WordSearch<'a> {
    allowed: &'a [Vec<Cand>],
    start: usize,
    best: &'a mut [f64],
    back: &'a mut [(usize, Back)],
}

impl WordSearch<'_> {
    fn extend(
        &mut self,
        trie: &Trie,
        word_bonus: &dyn Fn(u32) -> f64,
        pos: usize,
        node: u32,
        score: f64,
        path: &mut Vec<usize>,
    ) {
        if pos >= self.allowed.len() || path.len() >= MAX_WORD_LEN {
            return;
        }
        for c in &self.allowed[pos] {
            let Some(cp) = c.cp else { continue };
            let Some(next) = trie.child(node, cp) else { continue };
            let s = score + c.score.max(CHAR_FLOOR_IN_WORD);
            path.push(c.index);
            if path.len() >= 2 {
                if let Some(value) = trie.word[next as usize] {
                    let total = self.best[self.start] + s + WORD_BONUS * (path.len() - 1) as f64 + word_bonus(value);
                    relax(
                        self.best,
                        self.back,
                        pos + 1,
                        total,
                        self.start,
                        Back::Word(path.clone()),
                    );
                }
            }
            self.extend(trie, word_bonus, pos + 1, next, s, path);
            path.pop();
        }
    }
}

fn relax(best: &mut [f64], back: &mut [(usize, Back)], end: usize, score: f64, start: usize, b: Back) {
    if score > best[end] {
        best[end] = score;
        back[end] = (start, b);
    }
}

/// CJK unified ideographs (with extensions) and compatibility ideographs.
fn is_han(cp: u32) -> bool {
    matches!(cp, 0x3400..=0x4DBF | 0x4E00..=0x9FFF | 0xF900..=0xFAFF | 0x20000..=0x3FFFF)
}

/// Decode UTF-16 into code points; `None` on unpaired surrogates.
fn code_points(s: &[u16]) -> Option<Vec<u32>> {
    char::decode_utf16(s.iter().copied())
        .map(|r| r.ok().map(u32::from))
        .collect()
}

fn single_cp(s: &[u16]) -> Option<u32> {
    match code_points(s)?.as_slice() {
        [c] => Some(*c),
        _ => None,
    }
}

fn parse_u32(s: &[u16]) -> Option<u32> {
    if s.is_empty() || s.len() > 10 {
        return None;
    }
    let mut v: u64 = 0;
    for &u in s {
        if !(u16::from(b'0')..=u16::from(b'9')).contains(&u) {
            return None;
        }
        v = v * 10 + u64::from(u - u16::from(b'0'));
    }
    u32::try_from(v).ok()
}

/// `(key, value)` pairs of a `.cin`-style table: lines `key<TAB or SPACE>value`,
/// skipping `%` directives and `#` comments, restricted to the `%chardef`
/// section when there is one.  Surrounding double quotes are removed.
fn cin_entries(buf: &[u16]) -> Vec<(&[u16], &[u16])> {
    let lines: Vec<&[u16]> = buf
        .split(|&u| u == u16::from(b'\n'))
        .map(|l| l.strip_suffix(&[u16::from(b'\r')]).unwrap_or(l))
        .collect();
    let is_directive = |l: &[u16], word: &str| {
        let lw: Vec<u16> = l
            .iter()
            .map(|&u| {
                if u < 0x80 {
                    u16::from((u as u8).to_ascii_lowercase())
                } else {
                    u
                }
            })
            .collect();
        let pat: Vec<u16> = word.encode_utf16().collect();
        lw.starts_with(&pat)
    };
    let has_chardef = lines.iter().any(|l| is_directive(l, "%chardef"));
    let mut inside = !has_chardef;
    let mut out = Vec::new();
    for l in lines {
        if is_directive(l, "%chardef") {
            inside = is_directive(l, "%chardef begin") || is_directive(l, "%chardef\tbegin");
            continue;
        }
        if !inside || l.is_empty() || l[0] == u16::from(b'%') || l[0] == u16::from(b'#') {
            continue;
        }
        let sep = |u: &u16| *u == u16::from(b'\t') || *u == u16::from(b' ');
        let Some(cut) = l.iter().position(sep) else { continue };
        let key = unquote(&l[..cut]);
        let rest = &l[cut..];
        let Some(vstart) = rest.iter().position(|u| !sep(u)) else {
            continue;
        };
        let mut value = &rest[vstart..];
        while let [head @ .., last] = value {
            if sep(last) {
                value = head;
            } else {
                break;
            }
        }
        out.push((key, unquote(value)));
    }
    out
}

fn unquote(s: &[u16]) -> &[u16] {
    let q = u16::from(b'"');
    match s {
        [a, inner @ .., b] if *a == q && *b == q => inner,
        _ => s,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::text::w;

    fn positions(cands: &[Vec<Vec<u16>>]) -> Vec<Position<'_>> {
        cands
            .iter()
            .map(|cs| Position {
                code: &[],
                candidates: cs.iter().map(|c| c.as_slice()).collect(),
                pinned: None,
            })
            .collect()
    }

    fn texts(cands: &[Vec<Vec<u16>>], chosen: &[usize]) -> String {
        chosen
            .iter()
            .enumerate()
            .map(|(i, &k)| String::from_utf16_lossy(&cands[i][k]))
            .collect()
    }

    fn cs(list: &[&str]) -> Vec<Vec<u16>> {
        list.iter().map(|s| w(s)).collect()
    }

    fn lexicon(words: &[&str], freqs: &[(&str, u32)]) -> Lexicon {
        let mut lex = Lexicon::new();
        for word in words {
            lex.add_word(&w(word), 0);
        }
        for (c, f) in freqs {
            lex.add_char_freq(c.chars().next().unwrap() as u32, *f);
        }
        lex
    }

    #[test]
    fn empty_input() {
        assert!(convert(&Lexicon::new(), &[]).is_empty());
    }

    #[test]
    fn without_data_the_first_candidate_wins() {
        let cands = vec![cs(&["甲", "乙"]), cs(&["丙", "丁"])];
        let chosen = convert(&Lexicon::new(), &positions(&cands));
        assert_eq!(chosen, vec![0, 0]);
    }

    #[test]
    fn character_frequency_breaks_close_calls() {
        // "的" is far more common than "旳"; one rank of penalty is not enough to keep "旳".
        let lex = lexicon(&[], &[("的", 300000), ("旳", 3), ("一", 1000)]);
        let cands = vec![cs(&["旳", "的"])];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "的");
    }

    #[test]
    fn a_word_pulls_in_a_lower_ranked_candidate() {
        // Position 2 prefers "在" alone, but "現代" is a word.
        let lex = lexicon(&["現代"], &[("現", 500), ("在", 9000), ("代", 800)]);
        let cands = vec![cs(&["現"]), cs(&["在", "代"])];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "現代");
    }

    #[test]
    fn longer_words_win_over_shorter_ones() {
        let lex = lexicon(&["電腦", "腦力", "電腦科學"], &[]);
        let cands = vec![cs(&["店", "電"]), cs(&["惱", "腦"]), cs(&["科"]), cs(&["學", "穴"])];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "電腦科學");
    }

    #[test]
    fn words_combine_across_the_sentence() {
        let lex = lexicon(&["我們", "今天", "上課"], &[]);
        let cands = vec![
            cs(&["俄", "我"]),
            cs(&["們", "門"]),
            cs(&["金", "今"]),
            cs(&["天", "夭"]),
            cs(&["尚", "上"]),
            cs(&["課", "棵"]),
        ];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "我們今天上課");
    }

    #[test]
    fn pinned_position_is_kept_and_breaks_the_word() {
        let lex = lexicon(&["現代"], &[]);
        let cands = vec![cs(&["現"]), cs(&["在", "代"])];
        let mut pos = positions(&cands);
        pos[1].pinned = Some(0);
        assert_eq!(texts(&cands, &convert(&lex, &pos)), "現在");
    }

    #[test]
    fn pinned_index_out_of_range_is_ignored() {
        let cands = vec![cs(&["甲", "乙"])];
        let mut pos = positions(&cands);
        pos[0].pinned = Some(9);
        assert_eq!(convert(&Lexicon::new(), &pos), vec![0]);
    }

    #[test]
    fn multi_character_candidates_and_empty_positions_do_not_break_conversion() {
        let lex = lexicon(&["中文"], &[]);
        let cands = vec![cs(&["中華民國"]), vec![], cs(&["忠", "中"]), cs(&["紋", "文"])];
        let chosen = convert(&lex, &positions(&cands));
        assert_eq!(chosen, vec![0, 0, 1, 1]);
    }

    #[test]
    fn supplementary_plane_characters_are_single_characters() {
        // U+20000 (𠀀) is one character in two UTF-16 units.
        let lex = lexicon(&["𠀀一"], &[]);
        let cands = vec![cs(&["丁", "𠀀"]), cs(&["一"])];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "𠀀一");
    }

    #[test]
    fn add_word_rules() {
        let mut lex = Lexicon::new();
        assert!(lex.add_word(&w("中文"), 0));
        assert!(!lex.add_word(&w("中文"), 5), "duplicate");
        assert!(!lex.add_word(&w("中"), 0), "single character");
        assert!(!lex.add_word(&w("a中"), 0), "contains ASCII");
        assert!(!lex.add_word(&w("一二三四五六七八九"), 0), "too long");
        assert_eq!(lex.word_count(), 1);
    }

    #[test]
    fn load_cin_values_reads_chardef_and_unquotes() {
        let src = "%gen_inp\n%keyname begin\na\t\u{5B57}\n%keyname end\n%chardef begin\r\n\"ab\"\t\"中文\"\r\nac\t輸入法\r\nad 字\r\n# note\r\nae\t\"x中\"\r\n%chardef end\r\nzz\t不算\r\n";
        let mut lex = Lexicon::new();
        assert_eq!(lex.load_cin_values(&w(src)), 2);
        let cands = vec![cs(&["種", "中"]), cs(&["聞", "文"])];
        assert_eq!(texts(&cands, &convert(&lex, &positions(&cands))), "中文");
    }

    #[test]
    fn load_freq_table_splits_chars_and_words() {
        let src = "%chardef begin\n的\t310665\n是\t152789\n我們\t5000\n壞\tx\n%chardef\tend\n";
        let mut lex = Lexicon::new();
        assert_eq!(lex.load_freq_table(&w(src)), 3);
        assert_eq!(lex.char_count(), 2);
        assert_eq!(lex.word_count(), 1);
    }

    #[test]
    fn long_sentences_are_fast_enough() {
        let mut lex = Lexicon::new();
        let chars: Vec<char> = "一二三四五六七八九十".chars().collect();
        for a in &chars {
            for b in &chars {
                lex.add_word(&w(&format!("{a}{b}")), 0);
                lex.add_word(&w(&format!("{a}{b}{a}")), 0);
            }
        }
        let cands: Vec<Vec<Vec<u16>>> = (0..60)
            .map(|_| chars.iter().map(|c| w(&c.to_string())).collect())
            .collect();
        let chosen = convert(&lex, &positions(&cands));
        assert_eq!(chosen.len(), 60);
    }

    fn coded<'a>(codes: &'a [Vec<u16>], cands: &'a [Vec<Vec<u16>>]) -> Vec<Position<'a>> {
        codes
            .iter()
            .zip(cands)
            .map(|(code, cs)| Position {
                code,
                candidates: cs.iter().map(|c| c.as_slice()).collect(),
                pinned: None,
            })
            .collect()
    }

    fn cp(s: &str) -> u32 {
        s.chars().next().unwrap() as u32
    }

    #[test]
    fn learning_a_pinned_character_records_choice_and_pairs() {
        let codes = vec![w("k"), w("kln"), w("rb1")];
        let cands = vec![cs(&["之", "立", "亠"]), cs(&["刻"]), cs(&["引", "弔"])];
        let mut pos = coded(&codes, &cands);
        pos[0].pinned = Some(1);
        let mut user = UserModel::new();
        assert!(user.learn(&pos, &[1, 0, 0]));
        assert_eq!(user.choice(&w("k"), cp("立")), 1);
        assert_eq!(
            user.choice(&w("kln"), cp("刻")),
            0,
            "unpinned / single-candidate codes are not choices"
        );
        assert_eq!(user.word(&w("立刻")), 1);
        assert_eq!(user.word(&w("刻引")), 0, "only pairs touching a pinned character");

        // Nothing pinned: nothing learned.
        let pos = coded(&codes, &cands);
        assert!(!UserModel::new().learn(&pos, &[0, 0, 0]));
    }

    #[test]
    fn learning_skips_symbols_and_multi_character_candidates() {
        let codes = vec![w("a"), w("b"), w("c")];
        let cands = vec![cs(&["中", "終"]), cs(&["，", "。"]), cs(&["中華民國", "國"])];
        let mut pos = coded(&codes, &cands);
        pos[1].pinned = Some(0);
        pos[2].pinned = Some(0);
        let mut user = UserModel::new();
        assert!(!user.learn(&pos, &[0, 0, 0]));
        assert!(user.is_empty());
    }

    #[test]
    fn learned_choice_changes_the_default_after_repeats() {
        let lex = lexicon(&[], &[("之", 24653), ("立", 7394)]);
        let codes = vec![w("k")];
        let cands = vec![cs(&["之", "立"])];
        let mut user = UserModel::new();
        assert_eq!(convert_with_user(&lex, Some(&user), &coded(&codes, &cands)), vec![0]);
        user.add_choice(&w("k"), cp("立"), 1);
        assert_eq!(
            convert_with_user(&lex, Some(&user), &coded(&codes, &cands)),
            vec![0],
            "one choice is not enough on its own"
        );
        user.add_choice(&w("k"), cp("立"), 2);
        assert_eq!(convert_with_user(&lex, Some(&user), &coded(&codes, &cands)), vec![1]);
        // The same character under another code is not affected.
        let other = vec![w("x")];
        assert_eq!(convert_with_user(&lex, Some(&user), &coded(&other, &cands)), vec![0]);
    }

    #[test]
    fn learned_word_wins_in_context() {
        let lex = lexicon(&[], &[("之", 24653), ("立", 7394), ("初", 2122)]);
        let codes = vec![w("k"), w("kn")];
        let cands = vec![cs(&["之", "立"]), cs(&["初"])];
        assert_eq!(convert(&lex, &coded(&codes, &cands)), vec![0, 0]);
        let mut user = UserModel::new();
        user.add_word(&w("立初"), 1);
        assert_eq!(convert_with_user(&lex, Some(&user), &coded(&codes, &cands)), vec![1, 0]);
    }

    #[test]
    fn export_import_merge_round_trip() {
        let mut a = UserModel::new();
        a.add_word(&w("立刻"), 3);
        a.add_word(&w("𠀀一"), 1);
        a.add_choice(&w("k"), cp("立"), 2);
        a.add_choice(&w("kl/n"), cp("刻"), 1);
        let text = a.export();
        assert!(String::from_utf16_lossy(&text).starts_with(USER_FILE_HEADER));

        let mut b = UserModel::new();
        assert_eq!(b.import(&text), 4);
        assert_eq!(b.word(&w("立刻")), 3);
        assert_eq!(b.word(&w("𠀀一")), 1);
        assert_eq!(b.choice(&w("kl/n"), cp("刻")), 1);

        // Import adds; merge adds.
        b.import(&text);
        assert_eq!(b.word(&w("立刻")), 6);
        let mut c = UserModel::new();
        c.add_choice(&w("k"), cp("立"), 1);
        b.merge(&c);
        assert_eq!(b.choice(&w("k"), cp("立")), 5);

        // Garbage is skipped.
        let mut d = UserModel::new();
        assert_eq!(
            d.import(&w(
                "junk\nW\tx\t1\nW\t中文\tabc\nC\tk\t立立\t1\nC\t\t立\t1\nW\t中文\t2\n"
            )),
            1
        );
        d.clear();
        assert!(d.is_empty());
    }
}
