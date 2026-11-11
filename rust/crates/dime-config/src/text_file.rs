//! Byte-to-UTF-16 ladder for `SettingsModel::LoadTextFileAsUtf16` (phase 5b-5).
//!
//! File open, the empty-file buffer, and the output allocation stay in C++.
//! `convert` is `MultiByteToWideChar`: `(code page, flags, bytes) -> wide`.
//! `None` means the sizing call returned 0. This module does not call Win32.

/// `CP_ACP`.
pub const CP_ACP: u32 = 0;
/// `CP_UTF8`.
pub const CP_UTF8: u32 = 65001;
/// `MB_ERR_INVALID_CHARS`.
pub const MB_ERR_INVALID_CHARS: u32 = 8;

/// Decode one file image. `Some` is code units with no trailing NUL. `None`
/// means the code-page ladder failed (the C++ loader then returns null).
///
/// UTF-16 LE (`FF FE`) copies little-endian units and drops a trailing odd
/// byte. UTF-16 BE (`FE FF`) keeps the C++ double byte-swap, so the payload
/// is stored as little-endian units. A UTF-8 BOM (`EF BB BF`) is stripped.
/// Anything else tries strict UTF-8, then `CP_ACP` with flags 0.
pub fn decode_text_bytes(
    bytes: &[u8],
    mut convert: impl FnMut(u32, u32, &[u8]) -> Option<Vec<u16>>,
) -> Option<Vec<u16>> {
    if bytes.len() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE {
        return Some(utf16_le(&bytes[2..]));
    }
    if bytes.len() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF {
        return Some(utf16_be_swapped(&bytes[2..]));
    }
    let payload = utf8_payload(bytes);
    if let Some(wide) = convert(CP_UTF8, MB_ERR_INVALID_CHARS, payload) {
        return Some(wide);
    }
    convert(CP_ACP, 0, payload)
}

fn utf8_payload(bytes: &[u8]) -> &[u8] {
    if bytes.len() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF {
        &bytes[3..]
    } else {
        bytes
    }
}

fn utf16_le(payload: &[u8]) -> Vec<u16> {
    let n = payload.len() / 2;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let lo = payload[i * 2] as u16;
        let hi = payload[i * 2 + 1] as u16;
        out.push(lo | (hi << 8));
    }
    out
}

/// The C++ loop builds the big-endian unit and then swaps it again.
fn utf16_be_swapped(payload: &[u8]) -> Vec<u16> {
    let n = payload.len() / 2;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let hi = payload[i * 2] as u16;
        let lo = payload[i * 2 + 1] as u16;
        let unit = (hi << 8) | lo;
        out.push(((unit & 0xFF) << 8) | ((unit >> 8) & 0xFF));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ladder(bytes: &[u8]) -> (Option<Vec<u16>>, Vec<u32>) {
        let mut pages = Vec::new();
        let decoded = decode_text_bytes(bytes, |cp, flags, input| {
            pages.push(cp);
            if input.is_empty() {
                return None;
            }
            if cp == CP_UTF8 && flags == MB_ERR_INVALID_CHARS {
                let text = std::str::from_utf8(input).ok()?;
                return Some(text.encode_utf16().collect());
            }
            if cp == CP_ACP && flags == 0 {
                return Some(input.iter().copied().map(u16::from).collect());
            }
            None
        });
        (decoded, pages)
    }

    #[test]
    fn utf16_le_bom_strips_and_keeps_crlf() {
        let bytes = [0xFF, 0xFE, 0x72, 0x00, 0x6F, 0x00, 0x63, 0x00, 0x0D, 0x00, 0x0A, 0x00];
        let (decoded, pages) = ladder(&bytes);
        assert_eq!(
            decoded.unwrap(),
            vec![u16::from(b'r'), u16::from(b'o'), u16::from(b'c'), 0x0D, 0x0A]
        );
        assert!(pages.is_empty());
    }

    #[test]
    fn utf16_be_keeps_the_double_swap() {
        // U+0041, U+0042 stored big-endian. The extra swap yields 0x4100, 0x4200.
        let bytes = [0xFE, 0xFF, 0x00, 0x41, 0x00, 0x42];
        let (decoded, pages) = ladder(&bytes);
        assert_eq!(decoded.unwrap(), vec![0x4100, 0x4200]);
        assert!(pages.is_empty());
    }

    #[test]
    fn utf16_drops_a_trailing_odd_byte_and_keeps_an_embedded_nul() {
        let bytes = [0xFF, 0xFE, 0x41, 0x00, 0x00, 0x00, 0x42, 0x00, 0x7F];
        let (decoded, _) = ladder(&bytes);
        assert_eq!(decoded.unwrap(), vec![u16::from(b'A'), 0, u16::from(b'B')]);
    }

    #[test]
    fn utf8_bom_is_stripped() {
        let bytes = [0xEF, 0xBB, 0xBF, b'r', b'o', b'c', 0x0D, 0x0A];
        let (decoded, pages) = ladder(&bytes);
        assert_eq!(
            decoded.unwrap(),
            vec![u16::from(b'r'), u16::from(b'o'), u16::from(b'c'), 0x0D, 0x0A]
        );
        assert_eq!(pages, vec![CP_UTF8]);
    }

    #[test]
    fn utf8_without_bom_does_not_ask_for_acp() {
        let (decoded, pages) = ladder(b"roc\r\n");
        assert_eq!(decoded.unwrap().len(), 5);
        assert_eq!(pages, vec![CP_UTF8]);
    }

    #[test]
    fn invalid_utf8_falls_back_to_acp() {
        let (decoded, pages) = ladder(&[0x61, 0x20, 0x80, 0x0D, 0x0A]);
        assert_eq!(decoded.unwrap(), vec![0x61, 0x20, 0x80, 0x0D, 0x0A]);
        assert_eq!(pages, vec![CP_UTF8, CP_ACP]);
    }

    #[test]
    fn bom_only_utf8_fails_and_a_utf16_bom_is_empty() {
        assert!(ladder(&[0xEF, 0xBB, 0xBF]).0.is_none());
        assert_eq!(ladder(&[0xFF, 0xFE]).0.unwrap(), Vec::<u16>::new());
    }
}
