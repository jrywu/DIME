//! Window placement (port of `CBaseWindow::_GetWindowExtent`,
//! `CalcFitPointAroundTextExtent`, `RectInRect`) and the notify-window stack
//! (`CUIPresenter::_DecideStackGrowUp`, `_ComputeStackPosForNewSlot`,
//! `_RestackVisibleSlots`).  Pure: the work area, the window rectangles and
//! the screen height come from the caller.

/// `SHADOW_SPREAD` (`Define.h`): the shadow around the windows.
pub const SHADOW_SPREAD: i32 = 14;

/// `RECT_*` flags of `RectInRect` (`BaseWindow.h`).
pub mod flags {
    pub const INSIDE: u32 = 0x0001;
    pub const OVER_LEFT: u32 = 0x0002;
    pub const OVER_TOP: u32 = 0x0004;
    pub const OVER_RIGHT: u32 = 0x0008;
    pub const OVER_BOTTOM: u32 = 0x0010;
    pub const TOO_WIDE: u32 = 0x0020;
    pub const TOO_TALL: u32 = 0x0040;
}

/// `RECT`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub fn width(&self) -> i32 {
        self.right - self.left
    }
    pub fn height(&self) -> i32 {
        self.bottom - self.top
    }
    /// `OffsetRect`.
    fn offset(mut self, dx: i32, dy: i32) -> Rect {
        self.left += dx;
        self.right += dx;
        self.top += dy;
        self.bottom += dy;
        self
    }
}

/// `POINT`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

/// `RectInRect`: where `target` lies relative to `limit`.
pub fn rect_in_rect(limit: &Rect, target: &Rect) -> u32 {
    use flags::*;
    if limit.left <= target.left
        && target.right <= limit.right
        && limit.top <= target.top
        && target.bottom <= limit.bottom
    {
        return INSIDE;
    }
    let mut f = 0;
    if target.left < limit.left && target.right > limit.right {
        f |= TOO_WIDE;
    } else if target.left < limit.left {
        f |= OVER_LEFT;
    } else if target.right > limit.right {
        f |= OVER_RIGHT;
    }
    if target.top < limit.top && target.bottom > limit.bottom {
        f |= TOO_TALL;
    } else if target.top < limit.top {
        f |= OVER_TOP;
    } else if target.bottom > limit.bottom {
        f |= OVER_BOTTOM;
    }
    f
}

/// `_GetWindowExtent`: the point whose monitor work area is used.  A text
/// extent wider than the window is a large area rather than a caret or a
/// composition (e.g. 32-bit Firefox): its top-left is used; otherwise the
/// bottom-right corner (the C++ reads `RECT::right` as a `POINT`).
pub fn work_area_probe(text: &Rect, window: &Rect) -> Point {
    if text.width() > window.width() {
        Point {
            x: text.left,
            y: text.top,
        }
    } else {
        Point {
            x: text.right,
            y: text.bottom,
        }
    }
}

/// `CalcFitPointAroundTextExtent`: the top-left of `window` placed below the
/// text extent, or above it when below does not fit the work area.
pub fn fit_point_around_text_extent(text: &Rect, work: &Rect, window: &Rect) -> Point {
    use flags::*;
    let base_x = if text.width() > window.width() {
        text.left
    } else {
        text.right
    };
    let targets = [
        // below the text
        window.offset(base_x, text.bottom + SHADOW_SPREAD / 2),
        // above the text
        window.offset(base_x, text.top - window.height() - SHADOW_SPREAD / 2),
    ];
    let mut f = [0u32; 2];
    for (i, target) in targets.iter().enumerate() {
        f[i] = rect_in_rect(work, target);
        // Off screen vertically (e.g. the window was dragged away): try the other.
        if f[i] & (OVER_TOP | OVER_BOTTOM) != 0 {
            continue;
        }
        if f[i] == INSIDE {
            return Point {
                x: target.left,
                y: target.top,
            };
        } else if f[i] & OVER_LEFT != 0 {
            return Point { x: 0, y: target.top };
        } else if f[i] & OVER_RIGHT != 0 {
            return Point {
                x: text.left - window.width(),
                y: target.top,
            };
        }
    }
    // Neither fits vertically: dock to the top or bottom of the work area.
    let y = if f[0] & OVER_TOP != 0 {
        0
    } else {
        work.bottom - window.height()
    };
    let x = if f[0] & OVER_LEFT != 0 {
        0
    } else if f[0] & OVER_RIGHT != 0 {
        text.left - window.width()
    } else {
        text.right
    };
    Point { x, y }
}

/// One notify-window slot (`CUIPresenter::_pNotifyWnds[i]`); a missing window
/// is not visible and has height 0.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct NotifySlot {
    pub visible: i32,
    pub width: i32,
    pub height: i32,
}

/// `_DecideStackGrowUp`: with a candidate window, grow up when it is above
/// the anchor; without, grow up only if growing down would leave the screen.
pub fn stack_grow_up(candidate_top: Option<i32>, notify_y: i32, slots: &[NotifySlot], screen_bottom: i32) -> bool {
    if let Some(top) = candidate_top {
        return top < notify_y;
    }
    let total: i32 = slots
        .iter()
        .filter(|s| s.visible != 0)
        .map(|s| s.height + SHADOW_SPREAD / 2)
        .sum();
    notify_y + total > screen_bottom
}

/// `_ComputeStackPosForNewSlot`: where slot `new_slot` goes, past the other
/// visible slots.
pub fn stack_pos_for_new_slot(notify: Point, slots: &[NotifySlot], new_slot: usize, grow_up: bool) -> Point {
    let offset: i32 = slots
        .iter()
        .enumerate()
        .filter(|(i, s)| *i != new_slot && s.visible != 0)
        .map(|(_, s)| s.height + SHADOW_SPREAD / 2)
        .sum();
    let h = slots.get(new_slot).map_or(0, |s| s.height);
    Point {
        x: notify.x,
        y: if grow_up {
            notify.y - h - offset
        } else {
            notify.y + offset
        },
    }
}

/// `_RestackVisibleSlots`: the position of every visible slot (`None` for the
/// others).  With a candidate window each slot's right edge meets the
/// candidate's left edge and the stack starts at its top (growing down) or
/// bottom (growing up); without, the slots share `notify.x` from `notify.y`.
pub fn restack(
    candidate: Option<(Rect, Point)>,
    notify: Point,
    slots: &[NotifySlot],
    grow_up: bool,
) -> Vec<Option<Point>> {
    let (cand_left, anchor_y) = match candidate {
        Some((rect, location)) => (
            location.x - SHADOW_SPREAD / 2,
            if grow_up { rect.bottom } else { location.y },
        ),
        None => (0, notify.y),
    };
    let mut offset = 0;
    slots
        .iter()
        .map(|s| {
            if s.visible == 0 {
                return None;
            }
            let x = if candidate.is_some() {
                cand_left - s.width
            } else {
                notify.x
            };
            let y = if grow_up {
                anchor_y - s.height - offset
            } else {
                anchor_y + offset
            };
            offset += s.height + SHADOW_SPREAD / 2;
            Some(Point { x, y })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn r(l: i32, t: i32, rt: i32, b: i32) -> Rect {
        Rect {
            left: l,
            top: t,
            right: rt,
            bottom: b,
        }
    }

    #[test]
    fn below_when_it_fits_above_when_not() {
        let work = r(0, 0, 1920, 1040);
        let window = r(0, 0, 200, 300);
        let caret = r(100, 100, 102, 120);
        assert_eq!(
            fit_point_around_text_extent(&caret, &work, &window),
            Point { x: 102, y: 127 }
        );
        let low = r(100, 900, 102, 920);
        assert_eq!(
            fit_point_around_text_extent(&low, &work, &window),
            Point { x: 102, y: 593 }
        );
    }

    #[test]
    fn over_right_moves_left_of_text() {
        let work = r(0, 0, 1000, 1000);
        let window = r(0, 0, 200, 100);
        let caret = r(900, 100, 902, 120);
        assert_eq!(
            fit_point_around_text_extent(&caret, &work, &window),
            Point { x: 700, y: 127 }
        );
    }

    #[test]
    fn rect_flags() {
        let limit = r(0, 0, 100, 100);
        assert_eq!(rect_in_rect(&limit, &r(10, 10, 20, 20)), flags::INSIDE);
        assert_eq!(
            rect_in_rect(&limit, &r(-5, 10, 20, 120)),
            flags::OVER_LEFT | flags::OVER_BOTTOM
        );
    }

    #[test]
    fn stack_positions() {
        let slots = [
            NotifySlot {
                visible: 1,
                width: 50,
                height: 20,
            },
            NotifySlot::default(),
            NotifySlot {
                visible: 1,
                width: 80,
                height: 30,
            },
        ];
        let p = restack(None, Point { x: 5, y: 100 }, &slots, false);
        assert_eq!(
            p,
            vec![Some(Point { x: 5, y: 100 }), None, Some(Point { x: 5, y: 127 })]
        );
        assert_eq!(
            stack_pos_for_new_slot(Point { x: 5, y: 100 }, &slots, 2, true),
            Point { x: 5, y: 43 }
        );
        assert!(stack_grow_up(None, 1000, &slots, 1050));
        assert!(!stack_grow_up(Some(200), 100, &slots, 1050));
    }
}
