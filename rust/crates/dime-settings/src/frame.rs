//! Window creation, theme and dpi changes of the settings window (phase 5b-8i).
//!
//! Port of `SettingsFrame::Cpp::Handle` (`src/DIMESettings/SettingsFrame.cpp`,
//! formerly inline in `WM_CREATE`, `UpdateTheme`, `WM_SETTINGCHANGE`, and
//! `WM_DPICHANGED`): a reducer that updates the dark-theme flag and lists
//! the Win32 effects in order. The caller carries them out.

use dime_config::geometry::defaults;

use crate::layout::scale;

pub const EV_CREATE: i32 = 1;
pub const EV_THEME_CHANGED: i32 = 2;
pub const EV_SETTING_CHANGE: i32 = 3;
pub const EV_DPI_CHANGED: i32 = 4;

/// `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE, arg)`.
pub const FX_SET_DARK_TITLE: i32 = 1;
/// `DwmSetWindowAttribute(arg, theme.contentBg)`; arg is 35 or 36.
pub const FX_CAPTION_COLOR: i32 = 2;
/// `CreateThemeBrushes` (from the current dark flag).
pub const FX_THEME_BRUSHES: i32 = 3;
/// `CreateFonts(arg)`.
pub const FX_CREATE_FONTS: i32 = 4;
pub const FX_DESTROY_FONTS: i32 = 5;
/// `InvalidateRect(main, nullptr, TRUE)`.
pub const FX_INVALIDATE_MAIN: i32 = 6;
/// `InvalidateRect(content, nullptr, TRUE)`.
pub const FX_INVALIDATE_CONTENT: i32 = 7;
/// `SetWindowTheme(content, arg ? L"DarkMode_Explorer" : nullptr, nullptr)`.
pub const FX_CONTENT_THEME: i32 = 8;
/// `RebuildContentArea`.
pub const FX_REBUILD: i32 = 9;
/// `SetWindowPos(main, rc = x, y, w, h)`.
pub const FX_SET_POS: i32 = 10;
/// Register the content class and create the content area at rc = x, y, w, h.
pub const FX_CREATE_CONTENT: i32 = 11;

/// The DWM attribute `WM_CREATE`, `WM_SETTINGCHANGE` and `WM_DPICHANGED`
/// use for the caption colour; `UpdateTheme` uses 36.
const CAPTION_ATTR: i32 = 35;
const CAPTION_ATTR_UPDATE_THEME: i32 = 36;

/// `SettingsFrame::FrameState`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameState {
    pub is_dark: i32,
}

/// `SettingsFrame::FrameEvent` (48 bytes).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameEvent {
    pub kind: i32,
    /// `CConfig::IsSystemDarkTheme()`.
    pub system_dark: i32,
    /// The window's dpi (create / theme), or the new dpi (dpi change).
    pub dpi: u32,
    pub has_content: i32,
    pub client_w: i32,
    pub client_h: i32,
    /// `WM_DPICHANGED` suggested rectangle (left, top, right, bottom).
    pub has_rect: i32,
    pub rc: [i32; 4],
    pub reserved: i32,
}

/// `SettingsFrame::FrameEffect` (24 bytes).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameEffect {
    pub kind: i32,
    pub arg: i32,
    pub rc: [i32; 4],
}

fn fx(kind: i32, arg: i32) -> FrameEffect {
    FrameEffect { kind, arg, rc: [0; 4] }
}

/// `UpdateTheme`.
fn update_theme(st: &mut FrameState, ev: &FrameEvent, out: &mut Vec<FrameEffect>) {
    st.is_dark = i32::from(ev.system_dark != 0);
    out.push(fx(FX_THEME_BRUSHES, 0));
    out.push(fx(FX_CREATE_FONTS, ev.dpi as i32));
    if st.is_dark != 0 {
        out.push(fx(FX_SET_DARK_TITLE, 1));
    }
    out.push(fx(FX_CAPTION_COLOR, CAPTION_ATTR_UPDATE_THEME));
    out.push(fx(FX_INVALIDATE_MAIN, 0));
    if ev.has_content != 0 {
        out.push(fx(FX_INVALIDATE_CONTENT, 0));
    }
}

/// `SettingsFrame::Cpp::Handle`.
pub fn handle(st: &mut FrameState, ev: &FrameEvent) -> Vec<FrameEffect> {
    let mut out = Vec::new();
    match ev.kind {
        EV_CREATE => {
            st.is_dark = i32::from(ev.system_dark != 0);
            out.push(fx(FX_THEME_BRUSHES, 0));
            out.push(fx(FX_CREATE_FONTS, ev.dpi as i32));
            if st.is_dark != 0 {
                out.push(fx(FX_SET_DARK_TITLE, 1));
            }
            out.push(fx(FX_CAPTION_COLOR, CAPTION_ATTR));
            let sidebar_w = scale(defaults().sidebar_width, ev.dpi);
            out.push(FrameEffect {
                kind: FX_CREATE_CONTENT,
                arg: 0,
                rc: [sidebar_w, 0, ev.client_w.wrapping_sub(sidebar_w), ev.client_h],
            });
            if st.is_dark != 0 {
                out.push(fx(FX_CONTENT_THEME, 1));
            }
        }
        EV_THEME_CHANGED => update_theme(st, ev, &mut out),
        EV_SETTING_CHANGE => {
            let was = st.is_dark;
            st.is_dark = i32::from(ev.system_dark != 0);
            if was != st.is_dark {
                update_theme(st, ev, &mut out);
                out.push(fx(FX_SET_DARK_TITLE, st.is_dark));
                out.push(fx(FX_CAPTION_COLOR, CAPTION_ATTR));
                if ev.has_content != 0 {
                    out.push(fx(FX_CONTENT_THEME, st.is_dark));
                }
                out.push(fx(FX_REBUILD, 0));
            }
        }
        EV_DPI_CHANGED => {
            if ev.has_rect != 0 {
                let [l, t, r, b] = ev.rc;
                out.push(FrameEffect {
                    kind: FX_SET_POS,
                    arg: 0,
                    rc: [l, t, r.wrapping_sub(l), b.wrapping_sub(t)],
                });
            }
            out.push(fx(FX_DESTROY_FONTS, 0));
            out.push(fx(FX_CREATE_FONTS, ev.dpi as i32));
            out.push(fx(FX_REBUILD, 0));
            out.push(fx(FX_CAPTION_COLOR, CAPTION_ATTR));
            out.push(fx(FX_INVALIDATE_MAIN, 0));
        }
        _ => {}
    }
    out
}

/// `SettingsFrame::Cpp::InitialSize`: the window size `Run` creates.
pub fn initial_size(dpi: u32) -> (i32, i32) {
    let g = defaults();
    (scale(g.window_default_w, dpi), scale(g.window_default_h, dpi))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn setting_change_only_on_flip() {
        let mut st = FrameState { is_dark: 0 };
        let ev = FrameEvent {
            kind: EV_SETTING_CHANGE,
            system_dark: 0,
            dpi: 96,
            has_content: 1,
            ..Default::default()
        };
        assert!(handle(&mut st, &ev).is_empty());
        let ev = FrameEvent { system_dark: 1, ..ev };
        let fx = handle(&mut st, &ev);
        assert_eq!(st.is_dark, 1);
        assert_eq!(fx.last().unwrap().kind, FX_REBUILD);
    }

    #[test]
    fn create_places_content_right_of_sidebar() {
        let mut st = FrameState::default();
        let ev = FrameEvent {
            kind: EV_CREATE,
            dpi: 144,
            client_w: 1000,
            client_h: 700,
            ..Default::default()
        };
        let fx = handle(&mut st, &ev);
        let c = fx.iter().find(|e| e.kind == FX_CREATE_CONTENT).unwrap();
        assert_eq!(c.rc, [420, 0, 580, 700]);
        assert_eq!(initial_size(96), (900, 650));
    }
}
