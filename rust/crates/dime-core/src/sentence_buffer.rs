//! The pending sentence of sentence mode: port of `CSentenceBuffer`
//! (`src/SentenceBuffer.*`).
//!
//! Each position is one character: the keys typed for it, its candidates in
//! table order, the candidate the user pinned (if any) and the one the
//! conversion chose.  The cursor sits between positions (0..=count).

use crate::sentence::{convert_with_user, Lexicon, Position, UserModel};

/// `CSentenceBuffer::MaxPositions`.
pub const MAX_POSITIONS: usize = 40;

/// `CSentencePosition`.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SentencePosition {
    pub code: Vec<u16>,
    /// Never empty.
    pub candidates: Vec<Vec<u16>>,
    /// Candidate fixed by the user, or -1.
    pub pinned: i32,
    /// Candidate picked by the conversion.
    pub chosen: i32,
}

/// `CSentenceBuffer`.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SentenceBuffer {
    positions: Vec<SentencePosition>,
    cursor: usize,
}

impl SentenceBuffer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_empty(&self) -> bool {
        self.positions.is_empty()
    }
    pub fn count(&self) -> usize {
        self.positions.len()
    }
    pub fn at(&self, i: usize) -> &SentencePosition {
        &self.positions[i]
    }
    pub fn positions(&self) -> &[SentencePosition] {
        &self.positions
    }
    pub fn cursor(&self) -> usize {
        self.cursor
    }

    pub fn move_left(&mut self) {
        if self.cursor > 0 {
            self.cursor -= 1;
        }
    }
    pub fn move_right(&mut self) {
        if self.cursor < self.positions.len() {
            self.cursor += 1;
        }
    }
    pub fn home(&mut self) {
        self.cursor = 0;
    }
    pub fn end(&mut self) {
        self.cursor = self.positions.len();
    }

    /// Insert a character at the cursor and move past it.  `pinned` outside
    /// the candidates means "not pinned".  False when there are no candidates.
    pub fn insert(&mut self, code: &[u16], candidates: Vec<Vec<u16>>, pinned: i32) -> bool {
        if candidates.is_empty() {
            return false;
        }
        let pinned = if pinned >= 0 && (pinned as usize) < candidates.len() {
            pinned
        } else {
            -1
        };
        let position = SentencePosition {
            code: code.to_vec(),
            candidates,
            pinned,
            chosen: if pinned >= 0 { pinned } else { 0 },
        };
        self.positions.insert(self.cursor, position);
        self.cursor += 1;
        true
    }

    pub fn remove_before_cursor(&mut self) -> bool {
        if self.cursor == 0 {
            return false;
        }
        self.positions.remove(self.cursor - 1);
        self.cursor -= 1;
        true
    }

    pub fn remove_at_cursor(&mut self) -> bool {
        if self.cursor >= self.positions.len() {
            return false;
        }
        self.positions.remove(self.cursor);
        true
    }

    pub fn clear(&mut self) {
        self.positions.clear();
        self.cursor = 0;
    }

    /// The position Down edits: the one after the cursor, or the last.
    pub fn edit_target(&self) -> usize {
        if self.positions.is_empty() {
            return 0;
        }
        if self.cursor < self.positions.len() {
            self.cursor
        } else {
            self.positions.len() - 1
        }
    }

    /// Pin (`index` >= 0) or unpin (`index` < 0) a candidate.
    pub fn pin(&mut self, pos: usize, index: i32) -> bool {
        let Some(position) = self.positions.get_mut(pos) else {
            return false;
        };
        if index >= position.candidates.len() as i32 {
            return false;
        }
        position.pinned = if index < 0 { -1 } else { index };
        if position.pinned >= 0 {
            position.chosen = position.pinned;
        }
        true
    }

    /// The positions as the conversion sees them.
    fn views(&self) -> Vec<Position<'_>> {
        self.positions
            .iter()
            .map(|p| Position {
                code: &p.code,
                candidates: p.candidates.iter().map(Vec::as_slice).collect(),
                pinned: (p.pinned >= 0).then_some(p.pinned as usize),
            })
            .collect()
    }

    /// Re-choose every unpinned character (`Convert`).
    pub fn convert(&mut self, lexicon: Option<&Lexicon>, user: Option<&UserModel>) {
        if self.positions.is_empty() {
            return;
        }
        let empty = Lexicon::new();
        let chosen = convert_with_user(lexicon.unwrap_or(&empty), user, &self.views());
        for (position, k) in self.positions.iter_mut().zip(chosen) {
            position.chosen = if k < position.candidates.len() { k as i32 } else { 0 };
        }
    }

    /// `CSentenceUserModel::Learn`: what the pinned characters teach.
    pub fn learn_into(&self, user: &mut UserModel) -> bool {
        if self.positions.is_empty() {
            return false;
        }
        let chosen: Vec<usize> = self.positions.iter().map(|p| p.chosen.max(0) as usize).collect();
        user.learn(&self.views(), &chosen)
    }

    fn chosen_text(p: &SentencePosition) -> &[u16] {
        &p.candidates[p.chosen as usize]
    }

    /// The converted sentence.
    pub fn text(&self) -> Vec<u16> {
        self.positions
            .iter()
            .flat_map(|p| Self::chosen_text(p).iter().copied())
            .collect()
    }

    /// Length of the text of the first `n` positions.
    pub fn text_length(&self, n: usize) -> usize {
        self.positions.iter().take(n).map(|p| Self::chosen_text(p).len()).sum()
    }

    /// The sentence with the reading being typed shown at the cursor.
    pub fn display(&self, reading: &[u16]) -> Vec<u16> {
        let mut text = self.text();
        let at = self.text_length(self.cursor);
        text.splice(at..at, reading.iter().copied());
        text
    }

    pub fn display_caret(&self, reading: &[u16]) -> usize {
        self.text_length(self.cursor) + reading.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn insert_move_remove() {
        let mut b = SentenceBuffer::new();
        assert!(b.insert(&w("a"), vec![w("一"), w("二")], -1));
        assert!(b.insert(&w("b"), vec![w("三")], 5));
        assert_eq!(b.at(1).pinned, -1);
        b.move_left();
        assert_eq!(b.display(&w("x")), w("一x三"));
        assert_eq!(b.display_caret(&w("x")), 2);
        assert_eq!(b.edit_target(), 1);
        assert!(b.pin(0, 1));
        assert_eq!(b.text(), w("二三"));
        assert!(b.remove_at_cursor());
        assert!(!b.remove_at_cursor());
        assert!(b.remove_before_cursor());
        assert!(b.is_empty());
        assert!(!b.insert(&w("c"), vec![], 0));
    }
}
