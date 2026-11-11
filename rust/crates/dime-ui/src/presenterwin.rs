//! The presenter owned by Rust (migration phase 4b-2, `docs/RUST_TSF_SHELL.md`):
//! `CUIPresenter`'s placement state (`_candLocation`, `_notifyLocation`,
//! `_rectCompRange`), its notify-window stack (`_pNotifyWnds`, one Rust
//! [`NotifyWindow`] per `NOTIFY_TYPE` slot; 4b-2a) and its candidate window
//! with the candidate list (`_pCandidateWnd`: the former `CRustCandidateWindow`
//! with its `CCandidateListModel`; 4b-2b), with the decisions of
//! [`crate::presenter`] over them.
//!
//! C++ keeps what needs TSF (the parent window from the context view, the
//! layout sink, `_ProbeComposition`, the UI element), the two window callbacks
//! (`_NotifyChangeNotification`, `_CandidateChangeNotification`) and the
//! selection keys (`CCandidateRange`, engine data read live through
//! [`CandSource`] as the C++ window did).  A callback may come back into this
//! object (show, dispose) while a window method is on the stack, so the state
//! is in `Cell`s and windows are cloned out of their tables before any call:
//! nothing is borrowed across a call into a window or into C++.

use crate::candpaint::Colors;
use crate::candwin::{self, CandidateWindow};
use crate::layout::{NotifySlot, Point, Rect};
use crate::notifywin::{self, Classes, NotifyWindow};
use crate::presenter::{self, Host, LayoutFacts, ShowFacts, State};
use dime_core::candidates::{self, CandidatePaging};
use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::rc::Rc;
use windows::Win32::Foundation::{HINSTANCE, HWND};
use windows::Win32::Graphics::Gdi::HDC;
use windows::Win32::UI::WindowsAndMessaging::{GetSystemMetrics, SM_CYSCREEN};

/// `NOTIFY_TYPE::NOTIFY_COUNT`.
pub const NOTIFY_COUNT: usize = 5;
/// `NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP` / `NOTIFY_SPECIAL_CODE`: the compositional hints.
pub const NOTIFY_REVERSE_LOOKUP: usize = 3;
pub const NOTIFY_SPECIAL_CODE: usize = 4;
/// `UI::DEFAULT_WINDOW_X` / `_Y`.
pub const DEFAULT_WINDOW_POS: i32 = -32768;

/// What the candidate list needs from C++: the selection keys and the
/// candidate window's callback.
#[derive(Clone, Copy)]
pub struct CandSource {
    /// `CUIPresenter` (for `invoke`).
    pub ctx: *mut c_void,
    /// `CANDWND_ACTION` (0 select, 1 cancel); may destroy the window and the list.
    pub invoke: unsafe extern "C" fn(ctx: *mut c_void, action: i32),
    /// The count of a `CCandidateRange` (-1 for null).
    pub range_count: unsafe extern "C" fn(range: *const c_void) -> i32,
    /// `range->GetAt(row)->CandIndex` (0 out of range).
    pub range_char: unsafe extern "C" fn(range: *const c_void, row: i32) -> u16,
}

/// A candidate (`CCandidateListItem` as `CCandidateListModel::AddItem` copies it).
struct Item {
    text: Vec<u16>,
    key_code: Vec<u16>,
}

/// The candidate list: `CCandidateListModel` with its selection and paging in
/// [`CandidatePaging`] (as `CRustCandidateListModel`), the strings here.
pub struct CandList {
    items: RefCell<Vec<Item>>,
    paging: RefCell<CandidatePaging>,
    /// The `CCandidateRange*` the window was given (`_pIndexRange`).
    range: Cell<*const c_void>,
    source: CandSource,
}

impl CandList {
    fn new(range: *const c_void, source: CandSource) -> CandList {
        CandList { items: RefCell::new(Vec::new()), paging: RefCell::new(CandidatePaging::new()), range: Cell::new(range), source }
    }
    /// The range's count, -1 without one.
    fn rows(&self) -> i32 {
        unsafe { (self.source.range_count)(self.range.get()) }
    }
    /// `PageSize()`: the range's count, 0 without one.
    fn page_size(&self) -> u32 {
        self.rows().max(0) as u32
    }
    pub fn count(&self) -> u32 {
        self.paging.borrow().count()
    }
    pub fn selection(&self) -> i32 {
        self.paging.borrow().selection()
    }
    /// The string of item `index` (`GetString`): a pointer valid until the list changes.
    pub fn string(&self, index: i32) -> (*const u16, u32) {
        let items = self.items.borrow();
        match usize::try_from(index).ok().and_then(|i| items.get(i)) {
            Some(it) if !it.text.is_empty() => (it.text.as_ptr(), it.text.len() as u32),
            _ => (std::ptr::null(), 0),
        }
    }
    /// `GetSelectedKeyCode`.
    pub fn selected_key_code(&self) -> (*const u16, u32) {
        let items = self.items.borrow();
        match usize::try_from(self.selection()).ok().and_then(|i| items.get(i)) {
            Some(it) if !it.key_code.is_empty() => (it.key_code.as_ptr(), it.key_code.len() as u32),
            _ => (std::ptr::null(), 0),
        }
    }
    /// `GetCurrentPage`: `(HRESULT, page)` (page 0 on failure).
    pub fn current_page(&self) -> (i32, u32) {
        match self.paging.borrow().current_page() {
            Ok(p) => (candidates::S_OK, p),
            Err(e) => (e, 0),
        }
    }
    /// `GetPageIndex` (`CRustCandidateListModel`): `(HRESULT, page count)`.
    pub fn page_index(&self, out: &mut [u32]) -> (i32, u32) {
        let paging = self.paging.borrow();
        let pages = paging.pages();
        let (n, hr) = if out.len() > pages.len() { (pages.len(), candidates::S_OK) } else { (out.len(), candidates::S_FALSE) };
        out[..n].copy_from_slice(&pages[..n]);
        (hr, pages.len() as u32)
    }
}

// The candidate window's model (`DimeCandModel`) over a `CandList`: the
// callbacks of `CRustCandidateWindowModel`.
fn list<'a>(ctx: *mut c_void) -> &'a CandList {
    unsafe { &*(ctx as *const CandList) }
}
unsafe extern "C" fn m_selection(ctx: *mut c_void) -> i32 {
    list(ctx).selection()
}
unsafe extern "C" fn m_set_selection_raw(ctx: *mut c_void, index: u32) {
    list(ctx).paging.borrow_mut().set_selection_raw(index as i32);
}
unsafe extern "C" fn m_set_selection_offset(ctx: *mut c_void, offset: i32) {
    let l = list(ctx);
    let ps = l.page_size();
    l.paging.borrow_mut().set_selection_offset(ps, offset);
}
unsafe extern "C" fn m_set_selection(ctx: *mut c_void, index: i32) {
    let l = list(ctx);
    let ps = l.page_size();
    l.paging.borrow_mut().set_selection(ps, index);
}
unsafe extern "C" fn m_move_page(ctx: *mut c_void, offset: i32) {
    if offset == 0 {
        return;
    }
    let l = list(ctx);
    let ps = l.page_size();
    l.paging.borrow_mut().move_page(ps, offset);
}
unsafe extern "C" fn m_current_page(ctx: *mut c_void, page: *mut i32) -> i32 {
    match list(ctx).paging.borrow().current_page() {
        Ok(p) => {
            unsafe { *page = p as i32 };
            1
        }
        Err(_) => 0,
    }
}
unsafe extern "C" fn m_paint_page(ctx: *mut c_void, page: *mut u32, start: *mut u32) -> i32 {
    let l = list(ctx);
    let Ok(mut p) = l.paging.borrow().current_page() else { return 0 };
    let mut st = 0;
    let ps = l.page_size();
    if ps != 0 {
        // AdjustPageIndex (without a range: E_FAIL, the outputs untouched).
        let (_, np, idx) = l.paging.borrow_mut().adjust_page_index(ps, p);
        p = np;
        st = idx;
    }
    unsafe {
        *page = p;
        *start = st;
    }
    1
}
unsafe extern "C" fn m_page_count(ctx: *mut c_void) -> u32 {
    list(ctx).paging.borrow().pages().len() as u32
}
unsafe extern "C" fn m_page_start(ctx: *mut c_void, page: i32) -> u32 {
    list(ctx).paging.borrow().pages().get(page as u32 as usize).copied().unwrap_or(0)
}
unsafe extern "C" fn m_count(ctx: *mut c_void) -> u32 {
    list(ctx).count()
}
unsafe extern "C" fn m_rows_in_page(ctx: *mut c_void) -> i32 {
    list(ctx).rows()
}
unsafe extern "C" fn m_index_char(ctx: *mut c_void, row: i32) -> u16 {
    let l = list(ctx);
    unsafe { (l.source.range_char)(l.range.get(), row) }
}
unsafe extern "C" fn m_item(ctx: *mut c_void, index: u32, len: *mut u32) -> *const u16 {
    // Null only without an item: an item with an empty string is still drawn (its text
    // background), as C++ `_DrawList` does (`ItemAt` is not null).
    let l = list(ctx);
    let items = l.items.borrow();
    match items.get(index as usize) {
        Some(it) => {
            unsafe { *len = it.text.len() as u32 };
            if it.text.is_empty() {
                EMPTY.as_ptr()
            } else {
                it.text.as_ptr()
            }
        }
        None => std::ptr::null(),
    }
}
static EMPTY: [u16; 1] = [0];
unsafe extern "C" fn m_has_callback(_: *mut c_void) -> i32 {
    1
}
unsafe extern "C" fn m_invoke(ctx: *mut c_void, action: i32) {
    // Copied before calling: the callback may free the list.
    let source = list(ctx).source;
    unsafe { (source.invoke)(source.ctx, action) }
}

/// The candidate window object (`_pCandidateWnd`): the window is `None` when it
/// could not be created (`_Create` still succeeded, as in C++).  The window
/// is declared first so it goes before the list it reads.
struct Cand {
    window: Option<Rc<CandidateWindow>>,
    list: Rc<CandList>,
}

/// How the candidate window is made (`MakeCandidateWindow` / `_Create`).
pub struct CandCreate {
    pub hinstance: HINSTANCE,
    pub classes: candwin::Classes,
    pub parent: Option<HWND>,
    pub wnd_width: u32,
    /// The `CCandidateRange*`.
    pub range: *const c_void,
    pub colors: Colors,
    pub fill: u32,
}

/// A slot whose object exists (`_pNotifyWnds[i] != nullptr`); the window is
/// `None` when it could not be created (`_Create` still succeeded, as in C++).
struct Slot {
    window: Option<Rc<NotifyWindow>>,
}

pub struct Presenter {
    state: Cell<State>,
    slots: RefCell<Vec<Option<Slot>>>,
    cand: RefCell<Option<Cand>>,
    env: notifywin::Env,
    callback: notifywin::Callback,
    source: CandSource,
}

impl Presenter {
    /// The constructor: the locations off-screen, no windows.  `callback` gets
    /// every notify window's actions (`_NotifyWndCallback`); `source` is the
    /// candidate window's side.
    pub fn new(env: notifywin::Env, callback: notifywin::Callback, source: CandSource) -> Presenter {
        let off = Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS };
        Presenter {
            state: Cell::new(State {
                cand_location: off,
                notify_location: off,
                rect_comp_range: Rect { left: off.x, top: off.y, right: off.x, bottom: off.y },
            }),
            slots: RefCell::new((0..NOTIFY_COUNT).map(|_| None).collect()),
            cand: RefCell::new(None),
            env,
            callback,
            source,
        }
    }

    pub fn state(&self) -> State {
        self.state.get()
    }
    pub fn set_state(&self, st: State) {
        self.state.set(st);
    }
    /// `ResetCompRange`.
    pub fn reset_comp_range(&self) {
        let mut st = self.state.get();
        st.rect_comp_range = Rect::default();
        st.notify_location = Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS };
        self.state.set(st);
    }

    // ---- The notify stack (4b-2a) ----

    /// The window of slot `i` (cloned out: no borrow is held while it is used).
    pub fn window(&self, i: usize) -> Option<Rc<NotifyWindow>> {
        self.slots.borrow().get(i).and_then(|s| s.as_ref()).and_then(|s| s.window.clone())
    }
    /// `_pNotifyWnds[i] != nullptr`.
    pub fn exists(&self, i: usize) -> bool {
        self.slots.borrow().get(i).is_some_and(|s| s.is_some())
    }
    pub fn any_exists(&self) -> bool {
        self.slots.borrow().iter().any(|s| s.is_some())
    }

    /// `MakeNotifyWindow` after C++ found the parent: the slot's object and
    /// its window, once (the text is used only when the window is created).
    /// False for a slot out of range (`S_FALSE`).
    ///
    /// # Safety
    /// The class atoms must be registered by [`crate::win::register_class`] (or 0).
    pub unsafe fn make_notify(
        &self,
        i: usize,
        hinstance: HINSTANCE,
        classes: Classes,
        parent: Option<HWND>,
        text: &[u16],
    ) -> bool {
        if i >= NOTIFY_COUNT {
            return false;
        }
        if self.exists(i) {
            return true;
        }
        // The object before the window, as `make_unique` then `_Create`.
        self.slots.borrow_mut()[i] = Some(Slot { window: None });
        let w = unsafe { NotifyWindow::create(hinstance, classes, parent, i as i32, text, self.env, self.callback) };
        if let Some(s) = self.slots.borrow_mut()[i].as_mut() {
            s.window = w.map(Rc::new);
        }
        true
    }

    /// `DisposeNotifyWindow(i)`; `None`: every slot.
    pub fn dispose(&self, i: Option<usize>) {
        let taken: Vec<Slot> = {
            let mut slots = self.slots.borrow_mut();
            match i {
                None => slots.iter_mut().filter_map(Option::take).collect(),
                Some(i) => slots.get_mut(i).and_then(Option::take).into_iter().collect(),
            }
        };
        // Dropped after the borrow ends: destroying a window sends messages.
        drop(taken);
    }

    /// `SetNotifyText`.
    pub fn set_string(&self, i: usize, text: &[u16]) {
        if let Some(w) = self.window(i) {
            w.set_string(text);
        }
    }
    /// `ShowNotify`.
    pub fn show(&self, i: usize, on: bool, delay_show: u32, time_to_hide: u32) {
        if let Some(w) = self.window(i) {
            w.show(on, delay_show, time_to_hide);
        }
    }
    /// `_SetNotifyTextColor`: every slot.
    pub fn set_text_color(&self, color: u32, bk: u32) {
        for i in 0..NOTIFY_COUNT {
            if let Some(w) = self.window(i) {
                w.set_text_color(color, bk);
                w.set_fill_color(bk);
                w.invalidate();
            }
        }
    }
    pub fn is_visible(&self, i: usize) -> bool {
        self.window(i).is_some_and(|w| w.is_visible())
    }
    /// `IsNotifyShown`.
    pub fn any_visible(&self) -> bool {
        (0..NOTIFY_COUNT).any(|i| self.is_visible(i))
    }
    /// `_AnyHintSlotVisible`.
    pub fn any_hint_visible(&self) -> bool {
        self.is_visible(NOTIFY_REVERSE_LOOKUP) || self.is_visible(NOTIFY_SPECIAL_CODE)
    }

    // ---- The candidate window and list (4b-2b) ----

    /// `_pCandidateWnd != nullptr`.
    pub fn has_candidate(&self) -> bool {
        self.cand.borrow().is_some()
    }
    /// The candidate window (cloned out).
    pub fn cand_window(&self) -> Option<Rc<CandidateWindow>> {
        self.cand.borrow().as_ref().and_then(|c| c.window.clone())
    }
    /// The candidate list (cloned out); `None` without a candidate window object.
    pub fn cand_list(&self) -> Option<Rc<CandList>> {
        self.cand.borrow().as_ref().map(|c| Rc::clone(&c.list))
    }
    /// `PageSize()` of the list (0 without one, or when the range's count is not positive).
    pub fn candidate_page_size(&self) -> u32 {
        self.cand_list().map(|l| l.page_size()).unwrap_or(0)
    }

    /// `MakeCandidateWindow` after C++ found the parent: false when there is
    /// one already (`E_FAIL`).  Then `CRustCandidateWindow::_Create`: the
    /// window with the constructor's colors and `_SetTextColor`.
    ///
    /// # Safety
    /// The class atoms must be registered by [`crate::win::register_class`] (or 0).
    pub unsafe fn make_candidate(&self, c: &CandCreate) -> bool {
        if self.has_candidate() {
            return false;
        }
        let list = Rc::new(CandList::new(c.range, self.source));
        *self.cand.borrow_mut() = Some(Cand { window: None, list: Rc::clone(&list) });
        let model = candwin::Model {
            ctx: Rc::as_ptr(&list) as *mut c_void,
            selection: m_selection,
            set_selection_raw: m_set_selection_raw,
            set_selection_offset: m_set_selection_offset,
            set_selection: m_set_selection,
            move_page: m_move_page,
            current_page: m_current_page,
            paint_page: m_paint_page,
            page_count: m_page_count,
            page_start: m_page_start,
            count: m_count,
            rows_in_page: m_rows_in_page,
            index_char: m_index_char,
            item: m_item,
            has_callback: m_has_callback,
            invoke: m_invoke,
        };
        let w = unsafe { CandidateWindow::create(c.hinstance, c.classes, c.parent, c.wnd_width, self.env, model, c.colors, c.fill) };
        if let Some(w) = &w {
            w.set_text_color(c.colors.text, c.colors.bk);
        }
        if let Some(cand) = self.cand.borrow_mut().as_mut() {
            cand.window = w.map(Rc::new);
        }
        true
    }

    /// `DisposeCandidateWindow`: the window, then the list.
    pub fn dispose_candidate(&self) {
        let taken = self.cand.borrow_mut().take();
        drop(taken);
    }

    /// `_AddString` for each item: (text, key code); the key code is kept only when `with_key_code`.
    pub fn add_items(&self, items: &[(&[u16], &[u16])], with_key_code: bool) {
        if let Some(l) = self.cand_list() {
            let mut v = l.items.borrow_mut();
            let mut paging = l.paging.borrow_mut();
            for (text, key) in items {
                v.push(Item { text: text.to_vec(), key_code: if with_key_code { key.to_vec() } else { Vec::new() } });
                paging.push_item();
            }
        }
    }
    /// `_ClearList`.
    pub fn clear_list(&self) {
        if let Some(l) = self.cand_list() {
            l.items.borrow_mut().clear();
            l.paging.borrow_mut().clear();
        }
    }
    /// `_SetCandIndexRange`.
    pub fn set_index_range(&self, range: *const c_void) {
        if let Some(l) = self.cand_list() {
            l.range.set(range);
        }
    }
    /// `_SetPageIndex`.
    pub fn set_page_index(&self, pages: &[u32]) {
        if let Some(l) = self.cand_list() {
            l.paging.borrow_mut().set_page_index(pages);
        }
    }
    /// `_SetSelectionInPage`.
    pub fn set_selection_in_page(&self, pos: i32) -> bool {
        self.cand_list().is_some_and(|l| l.paging.borrow_mut().set_selection_in_page(pos))
    }
    /// `_SetSelection(nIndex)` (raw, `ITfCandidateListUIElementBehavior::SetSelection`).
    pub fn set_selection_raw(&self, index: i32) {
        if let Some(l) = self.cand_list() {
            l.paging.borrow_mut().set_selection_raw(index);
        }
    }
    /// `_MoveSelection(offset, isNotify)`: the list, then the scroll bar.
    pub fn move_selection(&self, offset: i32, notify: bool) -> bool {
        let Some(l) = self.cand_list() else { return false };
        if !l.paging.borrow_mut().move_selection(offset) {
            return false;
        }
        if notify {
            if let Some(w) = self.cand_window() {
                w.sb_shift_line(offset, true);
            }
        }
        true
    }
    /// `_SetSelection(index, isNotify)`.
    pub fn set_selection(&self, index: i32, notify: bool) -> bool {
        let Some(l) = self.cand_list() else { return false };
        let ps = l.page_size();
        let (ret, applied) = l.paging.borrow_mut().set_selection(ps, index);
        if applied && notify {
            if let Some(w) = self.cand_window() {
                w.sb_shift_position(index, true);
            }
        }
        ret
    }
    /// `_MovePage(offset, isNotify)`.
    pub fn move_page(&self, offset: i32, notify: bool) -> bool {
        let Some(l) = self.cand_list() else { return false };
        if offset == 0 {
            return true;
        }
        let ps = l.page_size();
        let (ok, wrapped) = l.paging.borrow_mut().move_page(ps, offset);
        if !ok {
            return false;
        }
        if notify {
            if let Some(w) = self.cand_window() {
                if wrapped {
                    w.sb_shift_position(l.selection(), false);
                } else {
                    w.sb_shift_page(offset, true);
                }
            }
        }
        true
    }

    // ---- Placement ----

    fn with_host<R>(&self, f: impl FnOnce(&mut Owned<'_>, &mut State) -> R) -> R {
        let mut st = self.state.get();
        let r = f(&mut Owned { p: self }, &mut st);
        self.state.set(st);
        r
    }

    /// `_LayoutChangeNotification` after C++ read the facts.
    pub fn layout_change(&self, f: &LayoutFacts) {
        self.with_host(|h, st| presenter::layout_change(h, st, f));
    }
    /// `ShowNotifyText` before showing; returns `caretUpdated`.
    pub fn notify_location_for_show(&self, f: &ShowFacts) -> bool {
        let mut st = self.state.get();
        let r = presenter::notify_location_for_show(&mut st, f);
        self.state.set(st);
        r
    }
    /// `ShowNotifyText` after showing (no delay).
    pub fn restack_for_show(&self, caret_updated: bool) {
        self.with_host(|h, st| presenter::restack_for_show(h, st, caret_updated));
    }
    /// `_RestackVisibleSlots`.
    pub fn restack_visible_slots(&self, grow_up: bool) {
        self.with_host(|h, st| presenter::restack_visible_slots(h, st, grow_up));
    }

    /// Paints the candidate window into `dc` as `WM_PAINT` would (tests).
    ///
    /// # Safety
    /// `dc` must be a valid DC.
    pub unsafe fn paint_candidate(&self, dc: HDC, paint: Rect) {
        if let Some(w) = self.cand_window() {
            unsafe { w.paint(dc, paint) };
        }
    }
}

/// [`presenter::Host`] over the windows of a [`Presenter`].
struct Owned<'a> {
    p: &'a Presenter,
}

impl Host for Owned<'_> {
    fn has_candidate(&mut self) -> bool {
        self.p.has_candidate()
    }
    fn candidate_client_rect(&mut self) -> Rect {
        self.p.cand_window().map(|w| w.client_rect()).unwrap_or_default()
    }
    fn candidate_window_extent(&mut self, comp: &Rect, cand: &Rect) -> Point {
        self.p.cand_window().map(|w| w.window_extent(comp, cand)).unwrap_or_default()
    }
    fn candidate_move(&mut self, p: Point) {
        if let Some(w) = self.p.cand_window() {
            w.move_to(p.x, p.y);
        }
    }
    fn candidate_visible(&mut self) -> bool {
        self.p.cand_window().is_some_and(|w| w.is_visible())
    }
    fn candidate_window_rect(&mut self) -> Rect {
        self.p.cand_window().map(|w| w.window_rect()).unwrap_or_default()
    }
    fn slots(&mut self) -> Vec<NotifySlot> {
        (0..NOTIFY_COUNT)
            .map(|i| match self.p.window(i) {
                Some(w) => {
                    let (cx, cy) = w.title_size();
                    NotifySlot { visible: i32::from(w.is_visible()), width: cx, height: cy }
                }
                None => NotifySlot::default(),
            })
            .collect()
    }
    fn slot_client_rect(&mut self, i: usize) -> Rect {
        self.p.window(i).map(|w| w.client_rect()).unwrap_or_default()
    }
    fn slot_window_extent(&mut self, i: usize, comp: &Rect, rect: &Rect) -> Point {
        self.p.window(i).map(|w| w.window_extent(comp, rect)).unwrap_or_default()
    }
    fn slot_move(&mut self, i: usize, p: Point) {
        if let Some(w) = self.p.window(i) {
            w.move_to(p.x, p.y);
        }
    }
    fn screen_bottom(&mut self) -> i32 {
        unsafe { GetSystemMetrics(SM_CYSCREEN) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    unsafe extern "C" fn dpi(_: *mut c_void) -> u32 {
        96
    }
    unsafe extern "C" fn zero() -> i32 {
        0
    }
    unsafe extern "C" fn font() -> *mut c_void {
        std::ptr::null_mut()
    }
    unsafe extern "C" fn set_font(_: *mut c_void) {}
    unsafe extern "C" fn invoke(_: *mut c_void, _: i32, _: usize, _: isize) {}
    unsafe extern "C" fn cand_invoke(_: *mut c_void, _: i32) {}
    /// A "range" is a pointer to a count of keys '1'..
    unsafe extern "C" fn range_count(range: *const c_void) -> i32 {
        if range.is_null() {
            -1
        } else {
            unsafe { *(range as *const i32) }
        }
    }
    unsafe extern "C" fn range_char(range: *const c_void, row: i32) -> u16 {
        if row >= 0 && row < unsafe { range_count(range) } {
            u16::from(b'1') + row as u16
        } else {
            0
        }
    }

    fn presenter() -> Presenter {
        let env = notifywin::Env { dpi_for_hwnd: dpi, dark: zero, always_show: zero, font, set_default_text_font: set_font };
        let source = CandSource { ctx: std::ptr::null_mut(), invoke: cand_invoke, range_count, range_char };
        Presenter::new(env, notifywin::Callback { ctx: std::ptr::null_mut(), invoke }, source)
    }

    #[test]
    fn slots_exist_without_a_window_and_are_disposed() {
        let p = presenter();
        assert_eq!(p.state().notify_location, Point { x: DEFAULT_WINDOW_POS, y: DEFAULT_WINDOW_POS });
        let none = Classes { notify: 0, notify_shadow: 0 };
        // Class atom 0: the window cannot be created, the slot still exists (as C++).
        assert!(unsafe { p.make_notify(3, HINSTANCE::default(), none, None, &[0x4e2d]) });
        assert!(unsafe { p.make_notify(1, HINSTANCE::default(), none, None, &[]) });
        assert!(!unsafe { p.make_notify(NOTIFY_COUNT, HINSTANCE::default(), none, None, &[]) }, "out of range");
        assert!(p.exists(3) && p.exists(1) && !p.exists(0) && p.any_exists());
        assert!(p.window(3).is_none() && !p.any_visible() && !p.any_hint_visible());
        p.show(3, true, 0, 0);
        p.dispose(Some(3));
        assert!(!p.exists(3) && p.exists(1));
        p.dispose(None);
        assert!(!p.any_exists());
        p.set_state(State { rect_comp_range: Rect { left: 1, top: 2, right: 3, bottom: 4 }, ..p.state() });
        p.reset_comp_range();
        assert_eq!(p.state().rect_comp_range, Rect::default());
    }

    #[test]
    fn the_candidate_list_works_without_a_window() {
        let p = presenter();
        let three: i32 = 3;
        let c = CandCreate {
            hinstance: HINSTANCE::default(),
            classes: candwin::Classes::default(),
            parent: None,
            wnd_width: 4,
            range: &three as *const i32 as *const c_void,
            colors: Colors::default(),
            fill: 0,
        };
        assert!(unsafe { p.make_candidate(&c) });
        assert!(!unsafe { p.make_candidate(&c) }, "one already");
        assert!(p.has_candidate() && p.cand_window().is_none());
        let a: Vec<u16> = "一".encode_utf16().collect();
        let k: Vec<u16> = "a".encode_utf16().collect();
        let items: Vec<(&[u16], &[u16])> = (0..7).map(|_| (&a[..], &k[..])).chain([(&[][..], &[][..])]).collect();
        p.add_items(&items, true);
        let l = p.cand_list().unwrap();
        assert_eq!(l.count(), 8);
        assert_eq!(l.string(7).1, 0, "an empty string is no string");
        assert_eq!(l.string(0).1, 1);
        p.set_page_index(&[0, 3, 6]);
        assert_eq!(l.current_page(), (candidates::S_OK, 0));
        assert!(p.move_page(1, true));
        assert_eq!(l.selection(), 3);
        assert!(p.set_selection_in_page(1));
        assert_eq!(l.selection(), 4);
        assert_eq!(l.selected_key_code().1, 1);
        let mut buf = [0u32; 2];
        assert_eq!(l.page_index(&mut buf), (candidates::S_FALSE, 3));
        assert_eq!(buf, [0, 3]);
        p.clear_list();
        assert_eq!(l.count(), 0);
        p.dispose_candidate();
        assert!(!p.has_candidate());
    }
}
