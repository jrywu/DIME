//! GUID <-> string, matching `StringFromCLSID` / `CLSIDFromString`.
//!
//! GUIDs are kept as their 16 in-memory bytes (`Data1`/`Data2`/`Data3`
//! little-endian, then `Data4`), so they can be `memcpy`'d to a C++ `GUID`.

pub type Guid = [u8; 16];

/// `StringFromCLSID`: `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` in upper case.
pub fn to_string(g: &Guid) -> String {
    let d1 = u32::from_le_bytes([g[0], g[1], g[2], g[3]]);
    let d2 = u16::from_le_bytes([g[4], g[5]]);
    let d3 = u16::from_le_bytes([g[6], g[7]]);
    format!(
        "{{{d1:08X}-{d2:04X}-{d3:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]
    )
}

/// `CLSIDFromString` for the brace form.  Anything that is not a braced GUID
/// (including the empty string) returns `None`; the Win32 API would
/// additionally try a ProgID registry lookup, which DIME's settings never
/// rely on.
pub fn parse(s: &[u16]) -> Option<Guid> {
    if s.len() != 38 || s[0] != b'{' as u16 || s[37] != b'}' as u16 {
        return None;
    }
    let body = &s[1..37];
    let mut nibbles = Vec::with_capacity(32);
    for (i, &c) in body.iter().enumerate() {
        if matches!(i, 8 | 13 | 18 | 23) {
            if c != b'-' as u16 {
                return None;
            }
            continue;
        }
        let d = char::from_u32(u32::from(c))?.to_digit(16)?;
        nibbles.push(d as u8);
    }
    let byte = |i: usize| (nibbles[2 * i] << 4) | nibbles[2 * i + 1];
    let b: Vec<u8> = (0..16).map(byte).collect();
    let mut g = [0u8; 16];
    // Data1..Data3 are written big-endian in the text, stored little-endian.
    g[0..4].copy_from_slice(&[b[3], b[2], b[1], b[0]]);
    g[4..6].copy_from_slice(&[b[5], b[4]]);
    g[6..8].copy_from_slice(&[b[7], b[6]]);
    g[8..16].copy_from_slice(&b[8..16]);
    Some(g)
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_core::text::w;

    #[test]
    fn round_trip() {
        let s = "{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B}";
        let g = parse(&w(s)).unwrap();
        assert_eq!(&g[..4], &[0xBA, 0x76, 0xFE, 0x36]);
        assert_eq!(to_string(&g), s);
        assert_eq!(parse(&w(&s.to_lowercase())), Some(g));
    }

    #[test]
    fn null_and_invalid() {
        assert_eq!(parse(&[]), None);
        assert_eq!(to_string(&[0; 16]), "{00000000-0000-0000-0000-000000000000}");
        assert_eq!(parse(&w("36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B")), None);
        assert_eq!(parse(&w("{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18G}")), None);
    }
}
