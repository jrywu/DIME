//! Layout and input decisions of `ThemedMessageBox` (phase 5b-6).
//! Text measuring (`DrawTextW` with `DT_CALCRECT`) stays with the caller.

use dime_config::geometry::mul_div;

pub const IDOK: i32 = 1;
pub const IDCANCEL: i32 = 2;
pub const IDYES: i32 = 6;
pub const IDNO: i32 = 7;

const MB_OKCANCEL: u32 = 0x1;
const MB_YESNOCANCEL: u32 = 0x3;
const MB_YESNO: u32 = 0x4;
const MB_ICONERROR: u32 = 0x10;
const MB_ICONQUESTION: u32 = 0x20;
const MB_ICONWARNING: u32 = 0x30;
const MB_ICONINFORMATION: u32 = 0x40;

const VK_RETURN: u32 = 0x0D;
const VK_ESCAPE: u32 = 0x1B;

/// Button labels, by index: 是(Y), 否(N), 取消, 確定.
pub const LABEL_YES: i32 = 0;
pub const LABEL_NO: i32 = 1;
pub const LABEL_CANCEL: i32 = 2;
pub const LABEL_OK: i32 = 3;

/// Buttons in display order.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Buttons {
    pub count: i32,
    pub ids: [i32; 3],
    pub labels: [i32; 3],
}

/// The button set for a `MessageBox` type. The tests are bit tests in this
/// order, so `MB_OKCANCEL` (1) matches `MB_YESNOCANCEL` (3) and shows
/// 是/否/取消, as the C++ does.
pub fn buttons(mb_type: u32) -> Buttons {
    if mb_type & MB_YESNOCANCEL != 0 {
        Buttons {
            count: 3,
            ids: [IDYES, IDNO, IDCANCEL],
            labels: [LABEL_YES, LABEL_NO, LABEL_CANCEL],
        }
    } else if mb_type & MB_YESNO != 0 {
        Buttons {
            count: 2,
            ids: [IDYES, IDNO, 0],
            labels: [LABEL_YES, LABEL_NO, 0],
        }
    } else if mb_type & MB_OKCANCEL != 0 {
        // Unreachable: MB_OKCANCEL is a subset of MB_YESNOCANCEL. Kept for parity.
        Buttons {
            count: 2,
            ids: [IDOK, IDCANCEL, 0],
            labels: [LABEL_OK, LABEL_CANCEL, 0],
        }
    } else {
        Buttons {
            count: 1,
            ids: [IDOK, 0, 0],
            labels: [LABEL_OK, 0, 0],
        }
    }
}

/// Icon for a type: 0 none, 1 warning, 2 error, 3 question, 4 information.
/// Bit tests in C++ order: any of the 0x30 bits picks the warning icon, so
/// `MB_ICONERROR` and `MB_ICONQUESTION` show it too.
pub fn icon(mb_type: u32) -> i32 {
    if mb_type & MB_ICONWARNING != 0 {
        1
    } else if mb_type & MB_ICONERROR != 0 {
        2
    } else if mb_type & MB_ICONQUESTION != 0 {
        3
    } else if mb_type & MB_ICONINFORMATION != 0 {
        4
    } else {
        0
    }
}

/// `ThemedMsgBox::Layout`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Layout {
    pub client: [i32; 4],
    pub text_x: i32,
    pub text_y: i32,
    pub text_w: i32,
    pub text_h: i32,
    pub btn_y: i32,
    pub btn_w: i32,
    pub btn_h: i32,
    pub btn_gap: i32,
    pub icon_x: i32,
    pub icon_y: i32,
    pub icon_size: i32,
    pub margin: i32,
}

fn scale(v: i32, dpi: u32) -> i32 {
    mul_div(v, dpi as i32, 96)
}

/// Widest the text may wrap to before measuring.
pub fn max_text_width(dpi: u32) -> i32 {
    scale(340, dpi)
}

/// `CalcLayout`. `text_w` / `text_h` are the `DT_CALCRECT` right and bottom.
pub fn layout(has_icon: bool, btn_count: i32, text_w: i32, text_h: i32, dpi: u32) -> Layout {
    let margin = scale(20, dpi);
    let icon_size = scale(32, dpi);
    let icon_gap = scale(14, dpi);
    let btn_h = scale(30, dpi);
    let btn_w = scale(88, dpi);
    let btn_gap = scale(8, dpi);
    let text_x = if has_icon {
        margin + icon_size + icon_gap
    } else {
        margin
    };
    let text_h = if has_icon && text_h < icon_size {
        icon_size
    } else {
        text_h
    };
    let btn_y = margin + text_h + scale(20, dpi);
    let total_btn_w = btn_count * btn_w + (btn_count - 1) * btn_gap;
    let client_w = (text_x + text_w + margin).max(total_btn_w + 2 * margin);
    let client_h = btn_y + btn_h + margin;
    Layout {
        client: [0, 0, client_w, client_h],
        text_x,
        text_y: margin,
        text_w,
        text_h,
        btn_y,
        btn_w,
        btn_h,
        btn_gap,
        icon_x: margin,
        icon_y: margin,
        icon_size,
        margin,
    }
}

/// Left edge of the first button (buttons are right-aligned).
pub fn first_button_x(l: &Layout, btn_count: i32) -> i32 {
    let total = btn_count * l.btn_w + (btn_count - 1) * l.btn_gap;
    l.client[2] - l.margin - total
}

/// Button index under a client point, or -1.
pub fn button_at(l: &Layout, btn_count: i32, x: i32, y: i32) -> i32 {
    let mut bx = first_button_x(l, btn_count);
    for i in 0..btn_count {
        if x >= bx && x < bx + l.btn_w && y >= l.btn_y && y < l.btn_y + l.btn_h {
            return i;
        }
        bx += l.btn_w + l.btn_gap;
    }
    -1
}

/// `WM_KEYDOWN`: Enter picks the default button; Escape picks the first
/// 取消 or 否, else the default. Other keys return -1 (not handled).
pub fn key_result(b: &Buttons, default_btn: i32, vk: u32) -> i32 {
    let default_id = || b.ids.get(default_btn as usize).copied().unwrap_or(0);
    match vk {
        VK_RETURN => default_id(),
        VK_ESCAPE => {
            for &id in b.ids.iter().take(b.count.clamp(0, 3) as usize) {
                if id == IDCANCEL || id == IDNO {
                    return id;
                }
            }
            default_id()
        }
        _ => -1,
    }
}

/// `WM_CLOSE`: 取消 when the box has it, else -1 (the result stays).
pub fn close_result(b: &Buttons) -> i32 {
    if b.ids
        .iter()
        .take(b.count.clamp(0, 3) as usize)
        .any(|&id| id == IDCANCEL)
    {
        IDCANCEL
    } else {
        -1
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn button_sets_follow_cpp_bit_tests() {
        assert_eq!(buttons(0x30).count, 1); // MB_OK | MB_ICONWARNING
        assert_eq!(buttons(MB_YESNO).ids, [IDYES, IDNO, 0]);
        assert_eq!(buttons(MB_OKCANCEL).ids, [IDYES, IDNO, IDCANCEL]);
        assert_eq!(icon(MB_ICONERROR), 1);
        assert_eq!(icon(MB_ICONINFORMATION), 4);
        assert_eq!(icon(0), 0);
    }

    #[test]
    fn layout_and_hits() {
        let l = layout(true, 2, 200, 10, 96);
        assert_eq!(l.text_x, 66);
        assert_eq!(l.text_h, 32);
        assert_eq!(l.btn_y, 72);
        assert_eq!(l.client, [0, 0, 286, 122]);
        let x0 = first_button_x(&l, 2);
        assert_eq!(x0, 286 - 20 - 184);
        assert_eq!(button_at(&l, 2, x0, 72), 0);
        assert_eq!(button_at(&l, 2, x0 + 88, 72), -1);
        assert_eq!(button_at(&l, 2, x0 + 96, 101), 1);
        assert_eq!(button_at(&l, 2, x0 + 96, 102), -1);
    }

    #[test]
    fn keys_and_close() {
        let yn = buttons(MB_YESNO);
        assert_eq!(key_result(&yn, 0, VK_RETURN), IDYES);
        assert_eq!(key_result(&yn, 0, VK_ESCAPE), IDNO);
        assert_eq!(key_result(&yn, 0, 0x41), -1);
        assert_eq!(close_result(&yn), -1);
        let ok = buttons(0);
        assert_eq!(key_result(&ok, 0, VK_ESCAPE), IDOK);
        assert_eq!(close_result(&buttons(MB_YESNOCANCEL)), IDCANCEL);
    }
}
