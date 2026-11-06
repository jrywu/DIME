//! Candidate list selection and paging (port of `CCandidateListModel`,
//! formerly part of `CCandidateWindow`).
//!
//! Only the selection/page state lives here; the candidate strings stay with
//! the host.  Arithmetic mirrors the C++ code, including its signed/unsigned
//! mixing (`INT` selection compared with `UINT` counts), because callers depend
//! on the resulting page layouts.  The page size (number of select keys) is
//! passed to every call, as the C++ code reads it from a shared range object.

/// Selection and page layout of a candidate list.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CandidatePaging {
    count: u32,
    selection: i32,
    pages: Vec<u32>,
    dont_adjust_on_empty_item_page: bool,
}

/// `HRESULT` values the C++ code returns.
pub const S_OK: i32 = 0;
pub const S_FALSE: i32 = 1;
pub const E_FAIL: i32 = 0x8000_4005_u32 as i32;
pub const E_UNEXPECTED: i32 = 0x8000_FFFF_u32 as i32;

impl CandidatePaging {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn count(&self) -> u32 {
        self.count
    }
    pub fn selection(&self) -> i32 {
        self.selection
    }
    pub fn pages(&self) -> &[u32] {
        &self.pages
    }

    /// `AddItem`: one more candidate.
    pub fn push_item(&mut self) {
        self.count += 1;
    }

    /// `Clear`.
    pub fn clear(&mut self) {
        self.selection = 0;
        self.count = 0;
        self.pages.clear();
    }

    pub fn set_selection_raw(&mut self, index: i32) {
        self.selection = index;
    }

    /// `GetCurrentPage`: `Err(E_UNEXPECTED)` when there is no page index.
    pub fn current_page(&self) -> Result<u32, i32> {
        if self.pages.is_empty() {
            return Err(E_UNEXPECTED);
        }
        if self.pages.len() == 1 || self.selection < 0 {
            return Ok(0);
        }
        let mut i = 1;
        while i < self.pages.len() {
            if self.pages[i] > self.selection as u32 {
                break;
            }
            i += 1;
        }
        Ok(i as u32 - 1)
    }

    /// `SetSelectionInPage`.
    pub fn set_selection_in_page(&mut self, pos: i32) -> bool {
        if pos < 0 || pos as u32 >= self.count {
            return false;
        }
        let Ok(page) = self.current_page() else { return false };
        self.selection = self.pages[page as usize].wrapping_add(pos as u32) as i32;
        true
    }

    /// `MoveSelection`.
    pub fn move_selection(&mut self, offset: i32) -> bool {
        let target = self.selection.wrapping_add(offset);
        if target >= self.count as i32 || target < 0 {
            return false;
        }
        self.selection = target;
        self.dont_adjust_on_empty_item_page = true;
        true
    }

    /// `SetSelection`: returns `(adjust result, applied)`.
    pub fn set_selection(&mut self, page_size: u32, index: i32) -> (bool, bool) {
        let mut index = index;
        if index == -2 {
            index = self.count.wrapping_sub(1) as i32;
        }
        if index < -2 {
            return (false, false);
        }
        if index >= self.count as i32 {
            return (false, false);
        }
        self.selection = index;
        (self.adjust_page_index_for_selection(page_size), true)
    }

    /// `MovePage`: returns `(ok, wrapped)`.
    pub fn move_page(&mut self, page_size: u32, offset: i32) -> (bool, bool) {
        if offset == 0 {
            return (true, false);
        }
        let Ok(current) = self.current_page() else { return (false, false) };
        let current = current as i32;
        let page_count = self.pages.len() as i32;
        let mut wrapped = false;
        let mut new_page = current + offset;
        if new_page < 0 {
            new_page = page_count - 1;
            wrapped = true;
        } else if new_page >= page_count {
            new_page = 0;
            wrapped = true;
        }
        if self.selection < 0 {
            self.selection = 0;
        }
        let current_start = self.pages[current as usize];
        if page_size != 0
            && (self.selection as u32).is_multiple_of(page_size)
            && self.selection == current_start as i32
        {
            self.dont_adjust_on_empty_item_page = true;
        }
        let selection_offset = self.selection.wrapping_sub(current_start as i32);
        let sel = (self.pages[new_page as usize] as i32).wrapping_add(selection_offset);
        self.selection = if self.count > sel as u32 { sel } else { self.count.wrapping_sub(1) as i32 };
        (true, wrapped)
    }

    /// `SetSelectionOffset` (scroll bar).
    pub fn set_selection_offset(&mut self, page_size: u32, offset: i32) -> bool {
        let new_offset = self.selection.wrapping_add(offset);
        if new_offset >= self.count as i32 || new_offset < 0 {
            return false;
        }
        let has_empty = self.current_page_has_empty_items(page_size).unwrap_or(false);
        let mut adjust = true;
        if (offset == 1 || offset == -1) && has_empty && self.pages.len() > 1 {
            let last_start = self.pages[self.pages.len() - 1] as i32;
            adjust = new_offset < last_start;
            self.dont_adjust_on_empty_item_page = true;
        }
        self.selection = new_offset;
        if adjust {
            return self.adjust_page_index_for_selection(page_size);
        }
        true
    }

    /// `SetPageIndex`.
    pub fn set_page_index(&mut self, pages: &[u32]) {
        self.pages = pages.to_vec();
    }

    /// `AdjustPageIndexForSelection`.
    pub fn adjust_page_index_for_selection(&mut self, page_size: u32) -> bool {
        if page_size == 0 {
            // C++: no range -> FALSE; a range of 0 keys would divide by zero.
            return false;
        }
        let p = page_size;
        let count = self.count;
        if count < p {
            return true;
        }
        let sel = self.selection;
        let is_before = sel > 0;
        let is_after = (sel < 0 && count > p) || count > (sel as u32).wrapping_add(p);
        let new_page_cnt: u32 = match (is_before, is_after) {
            (false, false) => 1,
            (false, true) => (count - 1) / p + 1,
            (true, false) => (((sel - 1) as u32) / p).wrapping_add(2),
            (true, true) => (count.wrapping_sub(2) / p).wrapping_add(2),
        };
        if new_page_cnt > 0x1000_0000 {
            // `new (std::nothrow) UINT[new_page_cnt]` fails for such (wrapped) sizes.
            return false;
        }
        let mut pages = vec![0u32; new_page_cnt as usize];
        let first_page = if sel < 0 { p } else { (sel as u32) % p };
        if first_page != 0 && new_page_cnt > 1 {
            pages[1] = first_page;
        }
        let start = if first_page != 0 { 2 } else { 1 };
        for i in start..new_page_cnt as usize {
            pages[i] = pages[i - 1].wrapping_add(p);
        }
        self.pages = pages;
        true
    }

    /// `CurrentPageHasEmptyItems`: `Err(E_FAIL)` without a page size,
    /// `Ok(None)` when there is no current page (C++ returns S_FALSE and leaves
    /// the out value untouched).
    pub fn current_page_has_empty_items(&self, page_size: u32) -> Result<bool, i32> {
        if page_size == 0 {
            return Err(E_FAIL);
        }
        let Ok(page) = self.current_page() else { return Err(S_FALSE) };
        let last = self.pages.len() as u32 - 1;
        Ok((page == 0 || page == last)
            && !self.pages.is_empty()
            && self.pages[page as usize] > self.count.wrapping_sub(page_size))
    }

    /// `AdjustPageIndex` (paint time): returns `(hr, current_page, current_page_index)`.
    pub fn adjust_page_index(&mut self, page_size: u32, current_page: u32) -> (i32, u32, u32) {
        if page_size == 0 {
            return (E_FAIL, current_page, 0);
        }
        let mut current_page = current_page;
        let mut current_page_index = self.pages[current_page as usize];
        match self.current_page_has_empty_items(page_size) {
            Ok(true) => {}
            // FAILED(hr) or no empty items: E_FAIL, nothing changed.
            _ => return (E_FAIL, current_page, current_page_index),
        }
        if self.dont_adjust_on_empty_item_page {
            return (E_FAIL, current_page, current_page_index);
        }
        let temp = self.selection;
        let cand_num = self.count;
        let page_num = self.pages.len() as u32;
        if current_page_index > cand_num.wrapping_sub(page_size) && page_num > 0 && current_page == page_num - 1 {
            self.selection = cand_num.wrapping_sub(page_size) as i32;
            self.adjust_page_index_for_selection(page_size);
            self.selection = temp;
            match self.current_page() {
                Ok(p) => current_page = p,
                Err(_) => return (E_FAIL, 0, current_page_index),
            }
            current_page_index = self.pages[current_page as usize];
        } else if current_page_index < page_size && current_page == 0 {
            self.selection = 0;
            self.adjust_page_index_for_selection(page_size);
            self.selection = temp;
        }
        self.dont_adjust_on_empty_item_page = false;
        (S_OK, current_page, current_page_index)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn twelve() -> CandidatePaging {
        let mut c = CandidatePaging::new();
        for _ in 0..12 {
            c.push_item();
        }
        c
    }

    #[test]
    fn pages_and_wrapping() {
        let mut c = twelve();
        assert_eq!(c.set_selection(9, 0), (true, true));
        assert_eq!(c.pages(), &[0, 9]);
        assert_eq!(c.move_page(9, 1), (true, false));
        assert_eq!(c.selection(), 9);
        assert_eq!(c.move_page(9, 1), (true, true));
        assert_eq!(c.selection(), 0);
        assert_eq!(c.move_page(9, -1), (true, true));
        assert_eq!(c.selection(), 9);
    }

    #[test]
    fn select_last_and_paint_adjust() {
        let mut c = twelve();
        assert_eq!(c.set_selection(9, -2), (true, true));
        assert_eq!(c.pages(), &[0, 2, 11]);
        assert_eq!(c.current_page(), Ok(2));
        assert_eq!(c.adjust_page_index(9, 2), (S_OK, 1, 3));
        assert_eq!(c.selection(), 11);
    }

    #[test]
    fn phrase_selection() {
        let mut c = twelve();
        assert_eq!(c.set_selection(9, -1), (true, true));
        assert_eq!(c.pages(), &[0, 9]);
        assert_eq!(c.current_page(), Ok(0));
        assert_eq!(c.move_page(9, 1), (true, false));
        assert_eq!(c.selection(), 9);
    }

    #[test]
    fn empty_list() {
        let mut c = CandidatePaging::new();
        assert_eq!(c.current_page(), Err(E_UNEXPECTED));
        assert!(!c.move_selection(1));
        assert_eq!(c.move_page(9, 1), (false, false));
    }
}
