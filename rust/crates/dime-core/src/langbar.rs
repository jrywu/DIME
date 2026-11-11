//! Phase 4d: language-bar and compartment decisions.
//!
//! Rust chooses when to sync a compartment, end composition, or show a
//! notify string. The host performs the TSF read/write and the side effect
//! immediately, so a compartment sink can re-enter before the host call
//! returns (the same order as `CCompartment::_SetCompartment*`).

/// `TF_CONVERSIONMODE_NATIVE`.
pub const NATIVE: u32 = 0x0001;
/// `TF_CONVERSIONMODE_FULLSHAPE`.
pub const FULLSHAPE: u32 = 0x0008;
pub const TF_MOD_LSHIFT: u16 = 0x0100;
pub const TF_MOD_RSHIFT: u16 = 0x0020;

pub const IME_BOTH: i32 = 0;
pub const IME_RIGHT: i32 = 1;
pub const IME_LEFT: i32 = 2;
pub const IME_NO_SHIFT: i32 = 3;

pub const SHIFT_SPACE: i32 = 0;
pub const ALWAYS_SINGLE: i32 = 1;
pub const ALWAYS_DOUBLE: i32 = 2;

/// Bool compartment id.
pub const BOOL_DOUBLE: i32 = 0;
pub const BOOL_KEYBOARD: i32 = 1;
pub const BOOL_IME: i32 = 2;

/// `KeyboardOpenCompartmentUpdated` guid.
pub const WHICH_OPENCLOSE: i32 = 0;
pub const WHICH_IME: i32 = 1;

/// `OnPreservedKey` guid.
pub const PRESERVED_IME: i32 = 0;
pub const PRESERVED_WIDTH: i32 = 1;
pub const PRESERVED_CONFIG: i32 = 2;
pub const PRESERVED_HAN: i32 = 3;

/// `show*Notify` kind. 0 is gated by the desktop-notify setting.
pub const KIND_CHN: i32 = 0;
pub const KIND_FULL: i32 = 1;
pub const KIND_HAN: i32 = 2;

const CHN: [u16; 2] = [0x4E2D, 0x6587];
const ENG: [u16; 2] = [0x82F1, 0x6578];
const FULL: [u16; 2] = [0x5168, 0x5F62];
const HALF: [u16; 2] = [0x534A, 0x5F62];
const SIMPL: [u16; 1] = [0x7C21];
const TRAD: [u16; 1] = [0x7E41];

pub trait LangHost {
    fn has_conversion(&mut self) -> bool;
    /// `None` is a failed `HRESULT`. `S_FALSE` is `Some` (the C++ flag stays false).
    fn get_conversion(&mut self) -> Option<u32>;
    fn set_conversion(&mut self, mode: u32);
    fn get_bool(&mut self, id: i32) -> Option<bool>;
    fn set_bool(&mut self, id: i32, value: bool);
    fn windows8(&mut self) -> bool;
    fn set_chinese(&mut self, on: bool);
    fn set_full_shape(&mut self, on: bool);
    fn end_composition_if_any(&mut self);
    fn delete_candidate(&mut self, force: bool);
    fn width_mode(&mut self) -> i32;
    fn set_width_mode(&mut self, mode: i32);
    fn notify_desktop(&mut self) -> bool;
    fn show_text(&mut self, text: &[u16], delay: u32);
    fn shift_mode(&mut self) -> i32;
    fn shift_key_only(&mut self) -> bool;
    fn modifiers(&mut self) -> u16;
    fn do_han(&mut self) -> bool;
    fn set_do_han(&mut self, on: bool);
    fn write_config(&mut self);
    fn setup_han(&mut self);
    fn inject_space(&mut self);
    fn show_config(&mut self);
    fn has_text_service(&mut self) -> bool;
}

fn sync_native(open: bool, mode: u32) -> u32 {
    if open && (mode & NATIVE) == 0 {
        mode | NATIVE
    } else if !open && (mode & NATIVE) != 0 {
        mode & !NATIVE
    } else {
        mode
    }
}

fn apply_open_flag(host: &mut impl LangHost, id: i32, mode: u32, override_open: Option<bool>) {
    if let Some(value) = override_open {
        host.set_bool(id, value);
    } else if let Some(open) = host.get_bool(id) {
        if open && (mode & NATIVE) == 0 {
            host.set_bool(id, false);
        } else if !open && (mode & NATIVE) != 0 {
            host.set_bool(id, true);
        }
    }
}

/// `CDIME::ConversionModeCompartmentUpdated`. `override_open` is `setKeyboardOpenClose`.
pub fn conversion_updated(host: &mut impl LangHost, override_open: Option<bool>) {
    if !host.has_conversion() {
        return;
    }
    let Some(mode) = host.get_conversion() else { return };
    if let Some(is_double) = host.get_bool(BOOL_DOUBLE) {
        if !is_double && (mode & FULLSHAPE) != 0 {
            host.set_bool(BOOL_DOUBLE, true);
        } else if is_double && (mode & FULLSHAPE) == 0 {
            host.set_bool(BOOL_DOUBLE, false);
        }
    }
    if host.windows8() {
        apply_open_flag(host, BOOL_KEYBOARD, mode, override_open);
    }
    apply_open_flag(host, BOOL_IME, mode, override_open);
}

/// `CDIME::PrivateCompartmentsUpdated`.
pub fn private_updated(host: &mut impl LangHost) {
    if !host.has_conversion() {
        return;
    }
    let Some(mut mode) = host.get_conversion() else { return };
    let prev = mode;
    let mut is_double = false;
    if let Some(value) = host.get_bool(BOOL_DOUBLE) {
        is_double = value;
        if !is_double && (mode & FULLSHAPE) != 0 {
            mode &= !FULLSHAPE;
        } else if is_double && (mode & FULLSHAPE) == 0 {
            mode |= FULLSHAPE;
        }
    }
    if mode != prev {
        host.set_conversion(mode);
        if is_double {
            shape_side(host, true);
        } else {
            shape_side(host, false);
        }
    }
}

/// `CDIME::KeyboardOpenCompartmentUpdated`. `which` selects the guid.
pub fn keyboard_updated(host: &mut impl LangHost, which: i32) {
    if !host.has_conversion() {
        return;
    }
    let Some(mut mode) = host.get_conversion() else { return };
    let prev = mode;
    if which == WHICH_OPENCLOSE {
        let mut is_open = false;
        if let Some(open) = host.get_bool(BOOL_KEYBOARD) {
            is_open = open;
            mode = sync_native(is_open, mode);
        }
        if mode != prev {
            host.set_conversion(mode);
            keyboard_side(host, is_open);
            show(host, KIND_CHN, is_open, 0);
        }
    }
    if which == WHICH_IME {
        if let Some(is_open) = host.get_bool(BOOL_IME) {
            mode = sync_native(is_open, mode);
            if mode != prev {
                host.set_conversion(mode);
                keyboard_side(host, is_open);
                show(host, KIND_CHN, is_open, 0);
            }
        }
    }
}

pub fn keyboard_side(host: &mut impl LangHost, open: bool) {
    host.set_chinese(open);
    if !open {
        host.end_composition_if_any();
        host.delete_candidate(true);
    }
}

pub fn shape_side(host: &mut impl LangHost, full: bool) {
    host.set_full_shape(full);
    host.end_composition_if_any();
    host.delete_candidate(!full);
    if host.width_mode() != SHIFT_SPACE {
        host.set_width_mode(if full { ALWAYS_DOUBLE } else { ALWAYS_SINGLE });
    }
}

pub fn show(host: &mut impl LangHost, kind: i32, on: bool, delay: u32) {
    if kind == KIND_CHN && !host.notify_desktop() {
        return;
    }
    let text: &[u16] = match kind {
        KIND_CHN => {
            if on {
                &CHN
            } else {
                &ENG
            }
        }
        KIND_FULL => {
            if on {
                &FULL
            } else {
                &HALF
            }
        }
        KIND_HAN => {
            if on {
                &SIMPL
            } else {
                &TRAD
            }
        }
        _ => return,
    };
    host.show_text(text, delay);
}

/// `CCompositionProcessorEngine::OnPreservedKey`.
/// Early `FALSE` returns stay false. Every other path ends eaten, including
/// the unknown guid (`*pIsEaten = FALSE` then the trailing `*pIsEaten = TRUE`).
pub fn preserved(host: &mut impl LangHost, id: i32) -> bool {
    preserved_body(host, id).unwrap_or(true)
}

fn preserved_body(host: &mut impl LangHost, id: i32) -> Option<bool> {
    match id {
        PRESERVED_IME => {
            let mode = host.shift_mode();
            let mods = host.modifiers();
            if mode == IME_NO_SHIFT
                || !host.shift_key_only()
                || (mode == IME_RIGHT && (mods & TF_MOD_LSHIFT) != 0)
                || (mode == IME_LEFT && (mods & TF_MOD_RSHIFT) != 0)
            {
                return Some(false);
            }
            let which = if host.windows8() { BOOL_KEYBOARD } else { BOOL_IME };
            let is_open = host.get_bool(which).unwrap_or(false);
            host.set_bool(which, !is_open);
            if host.has_text_service() {
                show(host, KIND_CHN, !is_open, 0);
            }
            None
        }
        PRESERVED_WIDTH => {
            if host.width_mode() != SHIFT_SPACE {
                host.inject_space();
                return Some(true);
            }
            let is_double = host.get_bool(BOOL_DOUBLE).unwrap_or(false);
            host.set_bool(BOOL_DOUBLE, !is_double);
            if host.has_text_service() {
                show(host, KIND_FULL, !is_double, 0);
            }
            None
        }
        PRESERVED_CONFIG => {
            if host.has_text_service() {
                host.show_config();
            }
            None
        }
        PRESERVED_HAN => {
            let which = if host.windows8() { BOOL_KEYBOARD } else { BOOL_IME };
            let is_open = host.get_bool(which).unwrap_or(false);
            if !is_open {
                return Some(false);
            }
            let current = host.do_han();
            host.set_do_han(!current);
            host.write_config();
            if !current {
                host.setup_han();
            }
            if host.has_text_service() {
                show(host, KIND_HAN, !current, 0);
            }
            None
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Mem {
        has_conversion: bool,
        conversion: Option<u32>,
        bools: [Option<bool>; 3],
        windows8: bool,
        chinese: bool,
        full: bool,
        ended: u32,
        deleted: Vec<bool>,
        width: i32,
        desktop: bool,
        notes: Vec<(Vec<u16>, u32)>,
        shift_mode: i32,
        shift_only: bool,
        modifiers: u16,
        han: bool,
        wrote: u32,
        setup_han: u32,
        injected: u32,
        showed_config: u32,
        has_ts: bool,
        reenter: bool,
        depth: u32,
    }

    impl Mem {
        fn new() -> Self {
            Self {
                has_conversion: true,
                conversion: Some(0),
                bools: [Some(false), Some(false), Some(false)],
                windows8: false,
                chinese: false,
                full: false,
                ended: 0,
                deleted: Vec::new(),
                width: SHIFT_SPACE,
                desktop: true,
                notes: Vec::new(),
                shift_mode: IME_BOTH,
                shift_only: true,
                modifiers: 0,
                han: false,
                wrote: 0,
                setup_han: 0,
                injected: 0,
                showed_config: 0,
                has_ts: true,
                reenter: false,
                depth: 0,
            }
        }
    }

    impl LangHost for Mem {
        fn has_conversion(&mut self) -> bool { self.has_conversion }
        fn get_conversion(&mut self) -> Option<u32> { self.conversion }
        fn set_conversion(&mut self, mode: u32) {
            self.conversion = Some(mode);
            if self.reenter && self.depth < 8 {
                self.depth += 1;
                conversion_updated(self, None);
                self.depth -= 1;
            }
        }
        fn get_bool(&mut self, id: i32) -> Option<bool> {
            match id {
                0..=2 => self.bools[id as usize],
                _ => None,
            }
        }
        fn set_bool(&mut self, id: i32, value: bool) {
            if (0..3).contains(&id) {
                self.bools[id as usize] = Some(value);
            }
            if self.reenter && self.depth < 8 {
                self.depth += 1;
                if id == BOOL_DOUBLE {
                    private_updated(self);
                } else if id == BOOL_KEYBOARD {
                    keyboard_updated(self, WHICH_OPENCLOSE);
                } else if id == BOOL_IME {
                    keyboard_updated(self, WHICH_IME);
                }
                self.depth -= 1;
            }
        }
        fn windows8(&mut self) -> bool { self.windows8 }
        fn set_chinese(&mut self, on: bool) { self.chinese = on; }
        fn set_full_shape(&mut self, on: bool) { self.full = on; }
        fn end_composition_if_any(&mut self) { self.ended += 1; }
        fn delete_candidate(&mut self, force: bool) { self.deleted.push(force); }
        fn width_mode(&mut self) -> i32 { self.width }
        fn set_width_mode(&mut self, mode: i32) { self.width = mode; }
        fn notify_desktop(&mut self) -> bool { self.desktop }
        fn show_text(&mut self, text: &[u16], delay: u32) { self.notes.push((text.to_vec(), delay)); }
        fn shift_mode(&mut self) -> i32 { self.shift_mode }
        fn shift_key_only(&mut self) -> bool { self.shift_only }
        fn modifiers(&mut self) -> u16 { self.modifiers }
        fn do_han(&mut self) -> bool { self.han }
        fn set_do_han(&mut self, on: bool) { self.han = on; }
        fn write_config(&mut self) { self.wrote += 1; }
        fn setup_han(&mut self) { self.setup_han += 1; }
        fn inject_space(&mut self) { self.injected += 1; }
        fn show_config(&mut self) { self.showed_config += 1; }
        fn has_text_service(&mut self) -> bool { self.has_ts }
    }

    fn note(m: &Mem, i: usize) -> &[u16] { &m.notes[i].0 }

    #[test]
    fn conversion_noop_without_compartment() {
        let mut m = Mem::new();
        m.has_conversion = false;
        m.conversion = Some(FULLSHAPE);
        conversion_updated(&mut m, None);
        assert!(m.bools[0] == Some(false));
        assert!(m.notes.is_empty());
    }

    #[test]
    fn conversion_syncs_double_and_ime() {
        let mut m = Mem::new();
        m.conversion = Some(FULLSHAPE);
        conversion_updated(&mut m, None);
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(true));
        assert_eq!(m.bools[BOOL_IME as usize], Some(false));
        assert!(!m.windows8);
        assert_eq!(m.bools[BOOL_KEYBOARD as usize], Some(false));

        let mut m = Mem::new();
        m.conversion = Some(NATIVE);
        m.bools[BOOL_IME as usize] = Some(false);
        m.bools[BOOL_DOUBLE as usize] = Some(true);
        conversion_updated(&mut m, None);
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(false));
        assert_eq!(m.bools[BOOL_IME as usize], Some(true));
    }

    #[test]
    fn conversion_win8_override_writes_without_reading() {
        let mut m = Mem::new();
        m.windows8 = true;
        m.conversion = Some(0);
        m.bools[BOOL_KEYBOARD as usize] = None;
        m.bools[BOOL_IME as usize] = None;
        conversion_updated(&mut m, Some(true));
        assert_eq!(m.bools[BOOL_KEYBOARD as usize], Some(true));
        assert_eq!(m.bools[BOOL_IME as usize], Some(true));

        let mut m = Mem::new();
        m.windows8 = false;
        m.conversion = Some(0);
        m.bools[BOOL_KEYBOARD as usize] = Some(true);
        conversion_updated(&mut m, Some(false));
        assert_eq!(m.bools[BOOL_KEYBOARD as usize], Some(true));
        assert_eq!(m.bools[BOOL_IME as usize], Some(false));
    }

    #[test]
    fn conversion_matched_does_not_set() {
        let mut m = Mem::new();
        m.conversion = Some(NATIVE | FULLSHAPE);
        m.bools = [Some(true), Some(false), Some(true)];
        m.reenter = true;
        conversion_updated(&mut m, None);
        assert_eq!(m.conversion, Some(NATIVE | FULLSHAPE));
        assert!(!m.chinese);
        assert!(m.notes.is_empty());
    }

    #[test]
    fn private_clears_fullshape_when_single() {
        let mut m = Mem::new();
        m.conversion = Some(NATIVE | FULLSHAPE);
        m.bools[BOOL_DOUBLE as usize] = Some(false);
        m.width = ALWAYS_DOUBLE;
        private_updated(&mut m);
        assert_eq!(m.conversion, Some(NATIVE));
        assert!(!m.full);
        assert_eq!(m.width, ALWAYS_SINGLE);
        assert_eq!(m.ended, 1);
        assert_eq!(m.deleted, vec![true]);
    }

    #[test]
    fn private_sets_fullshape_when_double() {
        let mut m = Mem::new();
        m.conversion = Some(NATIVE);
        m.bools[BOOL_DOUBLE as usize] = Some(true);
        m.width = ALWAYS_SINGLE;
        private_updated(&mut m);
        assert_eq!(m.conversion, Some(NATIVE | FULLSHAPE));
        assert!(m.full);
        assert_eq!(m.width, ALWAYS_DOUBLE);
        assert_eq!(m.deleted, vec![false]);
    }

    #[test]
    fn private_shift_space_keeps_width_mode() {
        let mut m = Mem::new();
        m.conversion = Some(FULLSHAPE);
        m.width = SHIFT_SPACE;
        private_updated(&mut m);
        assert_eq!(m.conversion, Some(0));
        assert_eq!(m.width, SHIFT_SPACE);
    }

    #[test]
    fn private_failed_double_get_does_not_change_mode() {
        let mut m = Mem::new();
        m.conversion = Some(FULLSHAPE);
        m.bools[BOOL_DOUBLE as usize] = None;
        private_updated(&mut m);
        assert_eq!(m.conversion, Some(FULLSHAPE));
        assert_eq!(m.ended, 0);
    }

    #[test]
    fn keyboard_open_and_close() {
        let mut m = Mem::new();
        m.bools[BOOL_KEYBOARD as usize] = Some(true);
        keyboard_updated(&mut m, WHICH_OPENCLOSE);
        assert_eq!(m.conversion, Some(NATIVE));
        assert!(m.chinese);
        assert_eq!(m.ended, 0);
        assert_eq!(note(&m, 0), CHN);

        let mut m = Mem::new();
        m.conversion = Some(NATIVE | FULLSHAPE);
        m.chinese = true;
        m.bools[BOOL_IME as usize] = Some(false);
        m.desktop = false;
        keyboard_updated(&mut m, WHICH_IME);
        assert_eq!(m.conversion, Some(FULLSHAPE));
        assert!(!m.chinese);
        assert_eq!(m.ended, 1);
        assert_eq!(m.deleted, vec![true]);
        assert!(m.notes.is_empty());
    }

    #[test]
    fn keyboard_openclose_failed_get_does_not_notify() {
        let mut m = Mem::new();
        m.conversion = Some(NATIVE);
        m.bools[BOOL_KEYBOARD as usize] = None;
        keyboard_updated(&mut m, WHICH_OPENCLOSE);
        assert_eq!(m.conversion, Some(NATIVE));
        assert!(m.notes.is_empty());
        assert!(!m.chinese);
    }

    #[test]
    fn keyboard_unknown_which_is_noop() {
        let mut m = Mem::new();
        m.conversion = Some(0);
        m.bools[BOOL_IME as usize] = Some(true);
        keyboard_updated(&mut m, -1);
        assert_eq!(m.conversion, Some(0));
        assert!(m.notes.is_empty());
    }

    #[test]
    fn show_kinds_and_delay() {
        let mut m = Mem::new();
        show(&mut m, KIND_FULL, false, 7);
        show(&mut m, KIND_HAN, true, 3);
        show(&mut m, 9, true, 1);
        assert_eq!(note(&m, 0), HALF);
        assert_eq!(m.notes[0].1, 7);
        assert_eq!(note(&m, 1), SIMPL);
        assert_eq!(m.notes.len(), 2);
        m.desktop = false;
        show(&mut m, KIND_CHN, true, 0);
        show(&mut m, KIND_FULL, true, 0);
        assert_eq!(m.notes.len(), 3);
        assert_eq!(note(&m, 2), FULL);
    }

    #[test]
    fn preserved_ime_rejects_and_toggles() {
        let mut m = Mem::new();
        m.shift_mode = IME_NO_SHIFT;
        assert!(!preserved(&mut m, PRESERVED_IME));
        assert_eq!(m.bools[BOOL_IME as usize], Some(false));

        let mut m = Mem::new();
        m.shift_mode = IME_RIGHT;
        m.modifiers = TF_MOD_LSHIFT;
        assert!(!preserved(&mut m, PRESERVED_IME));

        let mut m = Mem::new();
        m.shift_mode = IME_LEFT;
        m.modifiers = TF_MOD_RSHIFT;
        assert!(!preserved(&mut m, PRESERVED_IME));

        let mut m = Mem::new();
        m.shift_only = false;
        assert!(!preserved(&mut m, PRESERVED_IME));

        let mut m = Mem::new();
        m.shift_mode = IME_BOTH;
        m.modifiers = TF_MOD_LSHIFT;
        m.bools[BOOL_IME as usize] = Some(true);
        assert!(preserved(&mut m, PRESERVED_IME));
        assert_eq!(m.bools[BOOL_IME as usize], Some(false));
        assert_eq!(note(&m, 0), ENG);
        assert!(!m.chinese);

        let mut m = Mem::new();
        m.windows8 = true;
        m.bools[BOOL_KEYBOARD as usize] = None;
        assert!(preserved(&mut m, PRESERVED_IME));
        assert_eq!(m.bools[BOOL_KEYBOARD as usize], Some(true));
        assert_eq!(m.bools[BOOL_IME as usize], Some(false));
    }

    #[test]
    fn preserved_width_injects_or_toggles() {
        let mut m = Mem::new();
        m.width = ALWAYS_SINGLE;
        m.bools[BOOL_DOUBLE as usize] = Some(false);
        assert!(preserved(&mut m, PRESERVED_WIDTH));
        assert_eq!(m.injected, 1);
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(false));
        assert!(m.notes.is_empty());

        let mut m = Mem::new();
        m.width = SHIFT_SPACE;
        m.bools[BOOL_DOUBLE as usize] = Some(true);
        assert!(preserved(&mut m, PRESERVED_WIDTH));
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(false));
        assert_eq!(note(&m, 0), HALF);
        assert!(!m.full);
    }

    #[test]
    fn preserved_unknown_and_config_are_eaten() {
        let mut m = Mem::new();
        assert!(preserved(&mut m, -1));
        assert_eq!(m.showed_config, 0);
        assert!(m.notes.is_empty());

        assert!(preserved(&mut m, PRESERVED_CONFIG));
        assert_eq!(m.showed_config, 1);

        m.has_ts = false;
        m.showed_config = 0;
        assert!(preserved(&mut m, PRESERVED_CONFIG));
        assert_eq!(m.showed_config, 0);
    }

    #[test]
    fn preserved_han() {
        let mut m = Mem::new();
        m.bools[BOOL_IME as usize] = Some(false);
        m.han = true;
        assert!(!preserved(&mut m, PRESERVED_HAN));
        assert!(m.han);
        assert_eq!(m.wrote, 0);

        let mut m = Mem::new();
        m.bools[BOOL_IME as usize] = Some(true);
        m.han = true;
        assert!(preserved(&mut m, PRESERVED_HAN));
        assert!(!m.han);
        assert_eq!(m.wrote, 1);
        assert_eq!(m.setup_han, 0);
        assert_eq!(note(&m, 0), TRAD);

        let mut m = Mem::new();
        m.windows8 = true;
        m.bools[BOOL_KEYBOARD as usize] = Some(true);
        m.han = false;
        assert!(preserved(&mut m, PRESERVED_HAN));
        assert!(m.han);
        assert_eq!(m.setup_han, 1);
        assert_eq!(note(&m, 0), SIMPL);
    }

    #[test]
    fn reenter_private_clear_then_conversion() {
        let mut m = Mem::new();
        m.reenter = true;
        m.conversion = Some(FULLSHAPE);
        m.bools[BOOL_DOUBLE as usize] = Some(false);
        m.bools[BOOL_IME as usize] = Some(false);
        m.width = ALWAYS_DOUBLE;
        private_updated(&mut m);
        assert_eq!(m.conversion, Some(0));
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(false));
        assert!(!m.full);
        assert_eq!(m.width, ALWAYS_SINGLE);
        assert!(m.notes.is_empty());
    }

    #[test]
    fn reenter_preserved_ime_sets_native_before_notify() {
        let mut m = Mem::new();
        m.reenter = true;
        m.conversion = Some(0);
        m.bools[BOOL_IME as usize] = Some(false);
        assert!(preserved(&mut m, PRESERVED_IME));
        assert_eq!(m.bools[BOOL_IME as usize], Some(true));
        assert_eq!(m.conversion, Some(NATIVE));
        assert!(m.chinese);
        assert_eq!(m.notes.len(), 2);
        assert_eq!(note(&m, 0), CHN);
        assert_eq!(note(&m, 1), CHN);
    }

    #[test]
    fn reenter_preserved_width_sets_fullshape_before_notify() {
        let mut m = Mem::new();
        m.reenter = true;
        m.conversion = Some(NATIVE);
        m.bools[BOOL_IME as usize] = Some(true);
        m.bools[BOOL_DOUBLE as usize] = Some(false);
        m.width = SHIFT_SPACE;
        assert!(preserved(&mut m, PRESERVED_WIDTH));
        assert_eq!(m.bools[BOOL_DOUBLE as usize], Some(true));
        assert_eq!(m.conversion, Some(NATIVE | FULLSHAPE));
        assert!(m.full);
        assert_eq!(m.width, SHIFT_SPACE);
        assert_eq!(note(&m, 0), FULL);
    }
}
