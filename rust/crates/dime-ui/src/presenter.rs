//! The placement decisions of `CUIPresenter` (migration phase 4b-1,
//! `docs/RUST_TSF_SHELL.md`): where the candidate window and the notify-window
//! stack go when the composition moves (`_LayoutChangeNotification`) and when a
//! notify window is shown (the location part of `ShowNotifyText`).
//!
//! The presenter's location state is passed in and out ([`State`]); the facts
//! C++ reads from Win32 / TSF (caret positions, the parent window) are inputs;
//! the windows are reached through [`Host`].  The stack itself uses the
//! already ported `layout::stack_grow_up` / `layout::restack`, as
//! `_DecideStackGrowUp` / `_RestackVisibleSlots` do.

use crate::layout::{self, NotifySlot, Point, Rect};

/// `SHADOW_SPREAD` (`Define.h`).
pub const SHADOW_SPREAD: i32 = 14;

/// `CUIPresenter::_candLocation`, `_notifyLocation`, `_rectCompRange`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub cand_location: Point,
    pub notify_location: Point,
    pub rect_comp_range: Rect,
}

/// The windows `CUIPresenter` places.
pub trait Host {
    /// `_pCandidateWnd != nullptr`.
    fn has_candidate(&mut self) -> bool;
    /// `_pCandidateWnd->_GetClientRect`.
    fn candidate_client_rect(&mut self) -> Rect;
    /// `_pCandidateWnd->_GetWindowExtent(comp, cand, &pt)`.
    fn candidate_window_extent(&mut self, comp: &Rect, cand: &Rect) -> Point;
    /// `_pCandidateWnd->_Move`.
    fn candidate_move(&mut self, p: Point);
    /// `_pCandidateWnd->_IsWindowVisible` (only asked when there is one).
    fn candidate_visible(&mut self) -> bool;
    /// `_pCandidateWnd->_GetWindowRect`.
    fn candidate_window_rect(&mut self) -> Rect;
    /// `_NotifySlots`: one entry per `NOTIFY_TYPE` slot (a missing window: 0, 0, 0).
    fn slots(&mut self) -> Vec<NotifySlot>;
    /// `_pNotifyWnds[i]->_GetClientRect`.
    fn slot_client_rect(&mut self, i: usize) -> Rect;
    /// `_pNotifyWnds[i]->_GetWindowExtent(comp, rect, &pt)`.
    fn slot_window_extent(&mut self, i: usize, comp: &Rect, rect: &Rect) -> Point;
    /// `_pNotifyWnds[i]->_Move`.
    fn slot_move(&mut self, i: usize, p: Point);
    /// `GetSystemMetrics(SM_CYSCREEN)`.
    fn screen_bottom(&mut self) -> i32;
}

fn candidate_visible(h: &mut impl Host) -> bool {
    h.has_candidate() && h.candidate_visible()
}

/// `_DecideStackGrowUp`.
pub fn decide_stack_grow_up(h: &mut impl Host, st: &State) -> bool {
    let visible = candidate_visible(h);
    let top = visible.then(|| h.candidate_window_rect().top);
    let slots = h.slots();
    let screen_bottom = if visible { 0 } else { h.screen_bottom() };
    layout::stack_grow_up(top, st.notify_location.y, &slots, screen_bottom)
}

/// `_RestackVisibleSlots`.
pub fn restack_visible_slots(h: &mut impl Host, st: &State, grow_up: bool) {
    let candidate = candidate_visible(h).then(|| (h.candidate_window_rect(), st.cand_location));
    let slots = h.slots();
    for (i, p) in layout::restack(candidate, st.notify_location, &slots, grow_up).into_iter().enumerate() {
        if let Some(p) = p {
            h.slot_move(i, p);
        }
    }
}

/// The caret sits inside a composition wider than the window: the window
/// starts at the caret (both placements in `_LayoutChangeNotification`).
fn comp_left_at_caret(comp: &mut Rect, window: &Rect, caret: Point) {
    if (comp.right - comp.left) > (window.right - window.left) && caret.x < comp.right && caret.x >= comp.left {
        comp.left = caret.x;
    }
}

/// What C++ reads for `_LayoutChangeNotification`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LayoutFacts {
    /// `*lpRect`: the composition's text extent.
    pub comp: Rect,
    pub first_call: i32,
    /// A parent window (the context view's, else the foreground one).
    pub has_parent: i32,
    /// `GetCaretPos` mapped to the screen (used only with a parent).
    pub caret: Point,
}

/// `_LayoutChangeNotification(lpRect, firstCall)` after the facts are read.
pub fn layout_change(h: &mut impl Host, st: &mut State, f: &LayoutFacts) {
    let lp = f.comp;
    let mut comp = f.comp;
    let mut cand_rect = Rect::default();
    let mut cand_pt = Point::default();
    let caret = if f.has_parent != 0 { f.caret } else { Point::default() };

    if h.has_candidate() {
        if lp.bottom - lp.top >= 0 || lp.right - lp.left >= 0 {
            cand_rect = h.candidate_client_rect();
            comp_left_at_caret(&mut comp, &cand_rect, caret);
            cand_pt = h.candidate_window_extent(&comp, &cand_rect);
            h.candidate_move(cand_pt);
            st.cand_location = cand_pt;
        } else if f.first_call != 0 && f.has_parent != 0 {
            cand_rect = h.candidate_client_rect();
            comp = Rect { left: caret.x, top: caret.y, right: caret.x, bottom: caret.y };
            cand_pt = h.candidate_window_extent(&comp, &cand_rect);
            h.candidate_move(cand_pt);
            st.cand_location = cand_pt;
        }
    }

    // The anchor: the first visible slot (index order).
    let slots = h.slots();
    if let Some(anchor) = slots.iter().position(|s| s.visible != 0) {
        if lp.bottom - lp.top > 1 || lp.right - lp.left > 1 {
            if candidate_visible(h) {
                st.notify_location.y = if cand_pt.y < lp.top {
                    cand_pt.y + (cand_rect.bottom - cand_rect.top) - slots[anchor].height
                } else {
                    cand_pt.y
                };
            } else {
                let notify_rect = h.slot_client_rect(anchor);
                comp_left_at_caret(&mut comp, &notify_rect, caret);
                st.notify_location = h.slot_window_extent(anchor, &comp, &notify_rect);
            }
            // A flipped candidate that is gone: keep growing up from above the composition.
            let mut grow_up = decide_stack_grow_up(h, st);
            if !candidate_visible(h) && comp.bottom > comp.top && st.cand_location.y < comp.top {
                st.notify_location.y = comp.top - SHADOW_SPREAD / 2;
                grow_up = true;
            }
            restack_visible_slots(h, st, grow_up);
        }
    }
    st.rect_comp_range = comp;
}

/// What C++ reads for the location part of `ShowNotifyText`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ShowFacts {
    /// A transient slot (reverse lookup, special code, beep): refresh the location each show.
    pub refresh: i32,
    /// The context view's window.
    pub has_parent: i32,
    /// `GUITHREADINFO::rcCaret` as returned (client coordinates; zero when
    /// `GetGUIThreadInfo` fails).  The original's `GetCaretPos` branch ran only
    /// when `new GUITHREADINFO` returned null, which a throwing `new` never does.
    pub rc_caret: Rect,
    /// `(rcCaret.left, rcCaret.bottom)` mapped to the screen.
    pub gui_caret: Point,
}

/// `ShowNotifyText` before showing: the caret position the stack starts
/// from, with the fallback to the last candidate position.  Returns whether a
/// caret position was written (`caretUpdated`).
pub fn notify_location_for_show(st: &mut State, f: &ShowFacts) -> bool {
    let refresh = f.refresh != 0;
    let mut caret_updated = false;
    // Firefox / Chromium return rcCaret = {0,0,0,0}: mapped to the screen it is the
    // window origin, not the caret, so it is never used.
    let r = f.rc_caret;
    let valid = r.left != 0 || r.top != 0 || r.right != 0 || r.bottom != 0;
    let source = (f.has_parent != 0 && valid).then_some(f.gui_caret);
    if let Some(p) = source {
        if refresh || st.notify_location.x < 0 {
            st.notify_location.x = p.x;
        }
        if refresh || st.notify_location.y < 0 {
            st.notify_location.y = p.y;
        }
        caret_updated = true;
    }
    if !caret_updated && st.notify_location.x < 0 && st.cand_location.x >= 0 {
        st.notify_location.x = st.cand_location.x;
        let rc = st.rect_comp_range;
        st.notify_location.y = if rc.bottom > rc.top && st.cand_location.y < rc.top {
            rc.bottom + SHADOW_SPREAD / 2
        } else {
            st.cand_location.y
        };
    }
    caret_updated
}

/// `ShowNotifyText` after showing (no delay): restack every visible slot from
/// one anchor.
pub fn restack_for_show(h: &mut impl Host, st: &mut State, caret_updated: bool) {
    let mut grow_up = decide_stack_grow_up(h, st);
    let rc = st.rect_comp_range;
    if candidate_visible(h) {
        st.notify_location.y = if rc.top > 0 && st.cand_location.y < rc.top {
            let r = h.candidate_window_rect();
            st.cand_location.y + (r.bottom - r.top)
        } else {
            st.cand_location.y
        };
    } else if !caret_updated && rc.bottom > rc.top && st.cand_location.y < rc.top {
        // The candidate was flipped above the caret: its bottom is SHADOW_SPREAD / 2
        // above the composition.
        st.notify_location.y = rc.top - SHADOW_SPREAD / 2;
        grow_up = true;
        if st.cand_location.x >= 0 {
            st.notify_location.x = st.cand_location.x;
        }
    }
    restack_visible_slots(h, st, grow_up);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Windows as rectangles; extents put the window right under the composition.
    #[derive(Default)]
    struct Mock {
        cand: Option<(Rect, bool)>,
        slots: Vec<(Rect, bool)>,
        log: Vec<String>,
    }

    fn size(r: &Rect) -> (i32, i32) {
        (r.right - r.left, r.bottom - r.top)
    }

    impl Host for Mock {
        fn has_candidate(&mut self) -> bool {
            self.cand.is_some()
        }
        fn candidate_client_rect(&mut self) -> Rect {
            let (w, h) = size(&self.cand.unwrap().0);
            Rect { left: 0, top: 0, right: w, bottom: h }
        }
        fn candidate_window_extent(&mut self, comp: &Rect, _cand: &Rect) -> Point {
            Point { x: comp.left, y: comp.bottom }
        }
        fn candidate_move(&mut self, p: Point) {
            let c = self.cand.as_mut().unwrap();
            let (w, h) = size(&c.0);
            c.0 = Rect { left: p.x, top: p.y, right: p.x + w, bottom: p.y + h };
            self.log.push(format!("cand {} {}", p.x, p.y));
        }
        fn candidate_visible(&mut self) -> bool {
            self.cand.unwrap().1
        }
        fn candidate_window_rect(&mut self) -> Rect {
            self.cand.unwrap().0
        }
        fn slots(&mut self) -> Vec<NotifySlot> {
            self.slots
                .iter()
                .map(|(r, v)| NotifySlot { visible: i32::from(*v), width: size(r).0, height: size(r).1 })
                .collect()
        }
        fn slot_client_rect(&mut self, i: usize) -> Rect {
            let (w, h) = size(&self.slots[i].0);
            Rect { left: 0, top: 0, right: w, bottom: h }
        }
        fn slot_window_extent(&mut self, _i: usize, comp: &Rect, _rect: &Rect) -> Point {
            Point { x: comp.left, y: comp.bottom }
        }
        fn slot_move(&mut self, i: usize, p: Point) {
            self.log.push(format!("slot{i} {} {}", p.x, p.y));
        }
        fn screen_bottom(&mut self) -> i32 {
            1080
        }
    }

    fn rect(l: i32, t: i32, r: i32, b: i32) -> Rect {
        Rect { left: l, top: t, right: r, bottom: b }
    }

    #[test]
    fn candidate_starts_at_the_caret_in_a_wide_composition() {
        let mut m = Mock { cand: Some((rect(0, 0, 50, 100), true)), ..Default::default() };
        let mut st = State::default();
        let f = LayoutFacts { comp: rect(100, 200, 300, 220), first_call: 0, has_parent: 1, caret: Point { x: 180, y: 210 } };
        layout_change(&mut m, &mut st, &f);
        assert_eq!(m.log, ["cand 180 220"]);
        assert_eq!(st.cand_location, Point { x: 180, y: 220 });
        assert_eq!(st.rect_comp_range, rect(180, 200, 300, 220));
        // Without a parent the caret is (0, 0): outside the composition.
        let mut st = State::default();
        layout_change(&mut m, &mut st, &LayoutFacts { has_parent: 0, ..f });
        assert_eq!(st.cand_location, Point { x: 100, y: 220 });
    }

    #[test]
    fn notify_stack_follows_the_composition_without_a_candidate() {
        let mut m = Mock { slots: vec![(rect(0, 0, 0, 0), false), (rect(0, 0, 40, 20), true)], ..Default::default() };
        let mut st = State { cand_location: Point { x: -1, y: 500 }, ..Default::default() };
        let f = LayoutFacts { comp: rect(100, 200, 120, 220), first_call: 0, has_parent: 1, caret: Point::default() };
        layout_change(&mut m, &mut st, &f);
        assert_eq!(st.notify_location, Point { x: 100, y: 220 });
        assert_eq!(m.log, ["slot1 100 220"]);
        // An invalid (1-pixel) extent leaves the stack alone.
        m.log.clear();
        layout_change(&mut m, &mut st, &LayoutFacts { comp: rect(5, 5, 6, 6), ..f });
        assert!(m.log.is_empty());
    }

    #[test]
    fn show_ignores_an_empty_rc_caret_and_falls_back_to_the_candidate() {
        let mut st = State {
            cand_location: Point { x: 30, y: 40 },
            notify_location: Point { x: -1, y: -1 },
            rect_comp_range: rect(0, 50, 10, 60),
        };
        let f = ShowFacts { refresh: 1, has_parent: 1, gui_caret: Point { x: 7, y: 8 }, ..Default::default() };
        assert!(!notify_location_for_show(&mut st, &f));
        // The candidate was above the composition: below the composition instead.
        assert_eq!(st.notify_location, Point { x: 30, y: 60 + SHADOW_SPREAD / 2 });
        let mut st2 = State { notify_location: Point { x: 1, y: 2 }, ..st };
        let f2 = ShowFacts { refresh: 0, rc_caret: rect(0, 0, 1, 1), ..f };
        assert!(notify_location_for_show(&mut st2, &f2));
        assert_eq!(st2.notify_location, Point { x: 1, y: 2 }, "kept when not refreshing");
    }
}
