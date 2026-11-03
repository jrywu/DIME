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

use std::collections::HashMap;

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

const ROOT: u32 = 0;

/// Word list (as a character trie) plus single-character frequencies.
#[derive(Default)]
pub struct Lexicon {
    /// (parent node, code point) -> child node.
    edges: HashMap<(u32, u32), u32>,
    /// Per node: `Some(freq)` when the path from the root spells a word
    /// (`freq` is 0 when the source had no frequency).
    word: Vec<Option<u32>>,
    words: usize,
    char_freq: HashMap<u32, u32>,
    char_total: u64,
}

impl Lexicon {
    pub fn new() -> Self {
        Lexicon {
            word: vec![None],
            ..Default::default()
        }
    }

    /// Number of distinct words (two characters or more).
    pub fn word_count(&self) -> usize {
        self.words
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
        let mut node = ROOT;
        for &c in &cps {
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
            Some(f) => {
                *f = (*f).max(freq);
                false
            }
            None => {
                *slot = Some(freq);
                self.words += 1;
                true
            }
        }
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

    fn child(&self, node: u32, ch: u32) -> Option<u32> {
        self.edges.get(&(node, ch)).copied()
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

/// One character position: its candidates in table order, and the index the
/// user pinned (if any).
pub struct Position<'a> {
    pub candidates: Vec<&'a [u16]>,
    pub pinned: Option<usize>,
}

struct Cand {
    index: usize,
    rank: usize,
    /// The code point when the candidate is exactly one character.
    cp: Option<u32>,
}

#[derive(Clone)]
enum Back {
    None,
    /// One position, candidate index (or `usize::MAX` for an empty position).
    Single(usize),
    /// A word starting at `start`, with the candidate index for each position.
    Word(Vec<usize>),
}

/// Choose one candidate index for every position.  Pinned positions always
/// keep their pin; positions without candidates get index 0.
pub fn convert(lex: &Lexicon, positions: &[Position]) -> Vec<usize> {
    let n = positions.len();
    if n == 0 {
        return Vec::new();
    }

    let allowed: Vec<Vec<Cand>> = positions
        .iter()
        .map(|p| {
            let make = |index: usize| Cand {
                index,
                rank: index,
                cp: single_cp(p.candidates[index]),
            };
            match p.pinned {
                Some(i) if i < p.candidates.len() => vec![Cand { rank: 0, ..make(i) }],
                _ => (0..p.candidates.len()).map(make).collect(),
            }
        })
        .collect();

    let mut best = vec![f64::NEG_INFINITY; n + 1];
    let mut back: Vec<(usize, Back)> = vec![(0, Back::None); n + 1];
    best[0] = 0.0;

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
            let s = best[i] + lex.char_score(c);
            relax(&mut best, &mut back, i + 1, s, i, Back::Single(c.index));
        }
        let mut path = Vec::new();
        extend_words(lex, &allowed, i, i, ROOT, 0.0, &mut path, &mut best, &mut back);
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

#[allow(clippy::too_many_arguments)]
fn extend_words(
    lex: &Lexicon,
    allowed: &[Vec<Cand>],
    start: usize,
    pos: usize,
    node: u32,
    score: f64,
    path: &mut Vec<usize>,
    best: &mut [f64],
    back: &mut [(usize, Back)],
) {
    if pos >= allowed.len() || path.len() >= MAX_WORD_LEN {
        return;
    }
    for c in &allowed[pos] {
        let Some(cp) = c.cp else { continue };
        let Some(next) = lex.child(node, cp) else { continue };
        let s = score + lex.char_score(c).max(CHAR_FLOOR_IN_WORD);
        path.push(c.index);
        if path.len() >= 2 {
            if let Some(freq) = lex.word[next as usize] {
                let mut total = best[start] + s + WORD_BONUS * (path.len() - 1) as f64;
                if freq > 0 {
                    total += WORD_FREQ_WEIGHT * (1.0 + f64::from(freq)).ln();
                }
                relax(best, back, pos + 1, total, start, Back::Word(path.clone()));
            }
        }
        extend_words(lex, allowed, start, pos + 1, next, s, path, best, back);
        path.pop();
    }
}

fn relax(best: &mut [f64], back: &mut [(usize, Back)], end: usize, score: f64, start: usize, b: Back) {
    if score > best[end] {
        best[end] = score;
        back[end] = (start, b);
    }
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
}
