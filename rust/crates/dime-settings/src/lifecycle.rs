//! Launch arguments, the single-instance mode switch, page defaults, and
//! scroll placement (phase 5b-8h).
//!
//! Port of `SettingsLifecycle::Cpp` (`src/DIMESettings/SettingsLifecycle.cpp`,
//! formerly inline in `wWinMain`, `WM_COPYDATA`, `SwitchMode`, and
//! `RepositionControlsForScroll`).

use dime_config::geometry::defaults;
use dime_config::page_layout::LayoutCard;
use dime_config::settings_model::string_to_ime_mode;
use dime_config::ImeMode;

use crate::layout::{scale, EXPANDED_SLOTS};

/// `wcsstr` on a command line without its NUL.
fn find(hay: &[u16], needle: &str) -> Option<usize> {
    let n: Vec<u16> = needle.encode_utf16().collect();
    if n.len() > hay.len() {
        return None;
    }
    (0..=hay.len() - n.len()).find(|&i| hay[i..i + n.len()] == n[..])
}

/// The mode name after `--mode `: up to 31 units, stopping at a space.
fn mode_after(cmd: &[u16], start: usize) -> ImeMode {
    let name: Vec<u16> = cmd[start..]
        .iter()
        .copied()
        .take_while(|&c| c != 0 && c != u16::from(b' '))
        .take(31)
        .collect();
    string_to_ime_mode(Some(&name))
}

/// `SettingsLifecycle::LaunchArgs` (16 bytes).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LaunchArgs {
    pub legacy: i32,
    pub reset_ui: i32,
    pub new_ui: i32,
    pub gui_mode: i32,
}

/// `SettingsLifecycle::Cpp::ParseLaunch`: what `wWinMain` makes of its
/// command line. Only the first `--reset` decides whether a reset command
/// is present (`--reset-ui` first hides a later `--reset`).
pub fn parse_launch(cmd: &[u16]) -> LaunchArgs {
    let mut a = LaunchArgs {
        gui_mode: ImeMode::Dayi as i32,
        ..LaunchArgs::default()
    };
    if cmd.is_empty() {
        return a;
    }
    a.legacy = i32::from(find(cmd, "--legacy").is_some());
    if find(cmd, "--reset-ui").is_some() {
        a.reset_ui = 1;
        a.new_ui = 1;
    }
    let mode_arg = find(cmd, "--mode ");
    let has_reset = find(cmd, "--reset").is_some_and(|i| cmd.get(i + 7).copied().unwrap_or(0) != u16::from(b'-'));
    let has_cli = has_reset
        || ["--get", "--set", "--import", "--export", "--load", "--list", "--help"]
            .iter()
            .any(|n| find(cmd, n).is_some());
    if let Some(i) = mode_arg {
        if !has_cli && a.legacy == 0 {
            let m = mode_after(cmd, i + 7);
            if m != ImeMode::None {
                a.gui_mode = m as i32;
                a.new_ui = 1;
            }
        }
    }
    a
}

/// `SettingsLifecycle::Cpp::CopyDataMode`: the mode a second instance asks
/// for; `ImeMode::None` means no switch.
pub fn copy_data_mode(cmd: &[u16]) -> ImeMode {
    match find(cmd, "--mode ") {
        Some(i) => mode_after(cmd, i + 7),
        None => ImeMode::None,
    }
}

/// `SettingsLifecycle::Cpp::DefaultExpanded`: IME cards from slot 0, then
/// load-table cards from slot 4 (overwriting IME cards past the fourth).
pub fn default_expanded(ime: &[LayoutCard], load: &[LayoutCard]) -> [bool; EXPANDED_SLOTS] {
    let mut out = [false; EXPANDED_SLOTS];
    for (slot, card) in out.iter_mut().zip(ime) {
        *slot = card.default_expanded;
    }
    for (slot, card) in out[4..].iter_mut().zip(load) {
        *slot = card.default_expanded;
    }
    out
}

/// `SettingsLifecycle::ScrollItem` (20 bytes): one tracked control.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ScrollItem {
    pub orig_y: i32,
    pub orig_w: i32,
    pub orig_h: i32,
    /// Has a live HWND.
    pub alive: i32,
    /// `IsWindowVisible` before the move.
    pub visible: i32,
}

/// `SettingsLifecycle::ScrollAction` (16 bytes). With `moved`, the control
/// goes to `(origX, y)`; `show` -1 hides it, 1 shows it; `reset_rgn` clears
/// its window region (before showing).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ScrollAction {
    pub moved: i32,
    pub y: i32,
    pub reset_rgn: i32,
    pub show: i32,
}

/// `SettingsLifecycle::Cpp::PlanScroll`: controls whose top enters the
/// title band are hidden rather than clipped.
pub fn plan_scroll(items: &[ScrollItem], scroll_pos: i32, dpi: u32) -> Vec<ScrollAction> {
    let title_bottom = scale(defaults().top_margin, dpi);
    items
        .iter()
        .map(|it| {
            if it.alive == 0 || !(it.orig_w > 0 || it.orig_h > 0) {
                return ScrollAction::default();
            }
            let y = it.orig_y.wrapping_sub(scroll_pos);
            if y < title_bottom {
                ScrollAction {
                    moved: 1,
                    y,
                    reset_rgn: 0,
                    show: if it.visible != 0 { -1 } else { 0 },
                }
            } else {
                ScrollAction {
                    moved: 1,
                    y,
                    reset_rgn: 1,
                    show: if it.visible == 0 { 1 } else { 0 },
                }
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_config::page_layout::{ime_cards, load_cards};

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn launch_flags() {
        let a = parse_launch(&w("--mode array"));
        assert_eq!(
            a,
            LaunchArgs {
                legacy: 0,
                reset_ui: 0,
                new_ui: 1,
                gui_mode: 1
            }
        );
        assert_eq!(parse_launch(&w("--mode array --get Foo")).new_ui, 0);
        assert_eq!(parse_launch(&w("--reset-ui --mode dayi")).reset_ui, 1);
        // --reset-ui first hides a later --reset.
        assert_eq!(parse_launch(&w("--reset-ui --reset --mode phonetic")).gui_mode, 2);
        assert_eq!(parse_launch(&w("--reset --mode phonetic")).gui_mode, 0);
        assert_eq!(parse_launch(&w("--legacy --mode generic")).new_ui, 0);
        assert_eq!(
            parse_launch(&[]),
            LaunchArgs {
                gui_mode: 0,
                ..LaunchArgs::default()
            }
        );
    }

    #[test]
    fn copy_data() {
        assert_eq!(copy_data_mode(&w("x --mode Generic y")), ImeMode::Generic);
        assert_eq!(copy_data_mode(&w("--mode")), ImeMode::None);
        assert_eq!(copy_data_mode(&w("--mode nope")), ImeMode::None);
    }

    #[test]
    fn scroll_hides_in_title_band() {
        let items = [
            ScrollItem {
                orig_y: 60,
                orig_w: 10,
                orig_h: 10,
                alive: 1,
                visible: 1,
            },
            ScrollItem {
                orig_y: 200,
                orig_w: 10,
                orig_h: 10,
                alive: 1,
                visible: 0,
            },
            ScrollItem {
                orig_y: 200,
                orig_w: 0,
                orig_h: 0,
                alive: 1,
                visible: 1,
            },
        ];
        let acts = plan_scroll(&items, 50, 96);
        assert_eq!(
            acts[0],
            ScrollAction {
                moved: 1,
                y: 10,
                reset_rgn: 0,
                show: -1
            }
        );
        assert_eq!(
            acts[1],
            ScrollAction {
                moved: 1,
                y: 150,
                reset_rgn: 1,
                show: 1
            }
        );
        assert_eq!(acts[2], ScrollAction::default());
    }

    #[test]
    fn defaults_follow_tables() {
        let e = default_expanded(ime_cards(), load_cards());
        for (i, c) in load_cards().iter().enumerate().take(EXPANDED_SLOTS - 4) {
            assert_eq!(e[4 + i], c.default_expanded);
        }
    }
}
