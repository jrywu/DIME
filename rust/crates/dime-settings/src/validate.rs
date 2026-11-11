//! `ValidateCustomTableBuffer` (phase 5b-6): validate a whole custom-table
//! buffer line by line with `settings_model::validate_line`.

use dime_config::settings_model::validate_line;
use dime_config::ImeMode;

/// Result of [`validate_buffer`].
#[derive(Debug, Default, PartialEq, Eq)]
pub struct BufferReport {
    pub error_count: i32,
    /// 1-based line of the first invalid line, 0 when none.
    pub first_error_line: i32,
    /// `"  第 N 行\n"` for the first `max_report` invalid lines, before
    /// truncation to the caller's buffer.
    pub summary: Vec<u16>,
}

/// Lines end at `\n`; one trailing `\r` is dropped. The end of the buffer
/// closes the last line, so a final `\n` adds an empty (valid) line.
pub fn validate_buffer(
    text: &[u16],
    mode: ImeMode,
    max_codes: u32,
    mut char_ok: Option<&mut dyn FnMut(u16) -> bool>,
    max_report: i32,
) -> BufferReport {
    let mut report = BufferReport::default();
    if text.is_empty() {
        return report;
    }
    let mut line_no = 0;
    for line in text.split(|&c| c == u16::from(b'\n')) {
        line_no += 1;
        let line = match line.last() {
            Some(&c) if c == u16::from(b'\r') => &line[..line.len() - 1],
            _ => line,
        };
        let check = char_ok.as_mut().map(|f| &mut **f as &mut dyn FnMut(u16) -> bool);
        if !validate_line(line, mode, max_codes, check).is_empty() {
            if report.error_count == 0 {
                report.first_error_line = line_no;
            }
            if report.error_count < max_report {
                report.summary.extend(format!("  第 {line_no} 行\n").encode_utf16());
            }
            report.error_count += 1;
        }
    }
    report
}

/// `StringCchCatW` truncation: at most `cch - 1` units, then a NUL.
/// Returns the units to write including the NUL, or empty for `cch == 0`.
pub fn truncate_for_buffer(summary: &[u16], cch: usize) -> Vec<u16> {
    if cch == 0 {
        return Vec::new();
    }
    let n = summary.len().min(cch - 1);
    let mut out = summary[..n].to_vec();
    out.push(0);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn counts_lines_and_reports_first() {
        let text = w("ab 一\r\nbad\n\ncd 二\nx\n");
        let r = validate_buffer(&text, ImeMode::Dayi, 4, None, 10);
        assert_eq!(r.error_count, 2);
        assert_eq!(r.first_error_line, 2);
        assert_eq!(String::from_utf16(&r.summary).unwrap(), "  第 2 行\n  第 5 行\n");
    }

    #[test]
    fn report_is_capped_but_count_is_not() {
        let text = w("x\ny\nz");
        let r = validate_buffer(&text, ImeMode::Dayi, 4, None, 1);
        assert_eq!(r.error_count, 3);
        assert_eq!(String::from_utf16(&r.summary).unwrap(), "  第 1 行\n");
    }

    #[test]
    fn empty_and_truncation() {
        assert_eq!(
            validate_buffer(&[], ImeMode::Dayi, 4, None, 10),
            BufferReport::default()
        );
        assert_eq!(truncate_for_buffer(&w("abcdef"), 4), vec![97, 98, 99, 0]);
        assert!(truncate_for_buffer(&w("abc"), 0).is_empty());
    }
}
