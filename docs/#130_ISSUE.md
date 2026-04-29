# Issue #130 — Import Custom Table hangs ("轉圈圈") on large user files

- **Repository:** jrywu/DIME
- **Reporter:** YongZheAZhi
- **Title (orig.):** 改進【匯入自建詞庫】超過 4120 行會當機問題
- **Severity:** High (feature unusable for users with large phrase tables)
- **Status:** **RC found 2026-04-29; fix landed and verified.** Final shipped design: encoding-aware reader (`LoadTextFileAsUtf16`), hardened in-memory validator (`SettingsModel::ValidateLine` with manual whitespace scan + length cap), TOM-based viewport-only live painting, Save-time full validation with auto-jump to first error, CLI parity through the same primitives. Big5 silent truncation retired as a side effect of the CLI restructure.
- **See also:**
  - [docs/CUSTOM_TABLE_VALIDATION.md](CUSTOM_TABLE_VALIDATION.md) — current architecture of the validator, encoding-aware loader, and live/save validation regimes.
  - [docs/RICHEDIT_VIEWPORT_VAL.md](RICHEDIT_VIEWPORT_VAL.md) — focused implementation reference for the viewport-only live validator (TOM painting, trigger wiring, RichEdit text-limit fix).

## Symptom

Clicking **匯入自建詞庫** in the new Settings window and selecting a large phrase file (the reporter's `四字成語.TXT`, 73,278 lines) hangs the Settings UI indefinitely. Reproduces with both ANSI/Big5 and UTF-8 sources. Manual paste of the same content also fails (truncates around `/VD. 節奏明朗`, ~1,500 lines, separate symptom — likely a clipboard/RichEdit paste-size limit, out of scope here). Worked in v1.2.355 (legacy ConfigDialog UI).

## Root cause

Two compounding defects; the format checker is the proximate hang.

### 1. Read path has no encoding conversion

[src/DIMESettings/SettingsWindow.cpp:3458-3480](../src/DIMESettings/SettingsWindow.cpp#L3458-L3480) — `RowAction::ImportCustomTable`:

```cpp
LPWSTR buf = new (std::nothrow) WCHAR[dwSize + 1];
ZeroMemory(buf, (dwSize + 1) * sizeof(WCHAR));
ReadFile(hFile, buf, dwSize, &dwRead, nullptr);   // dwSize *bytes* into a WCHAR[]
LPCWSTR text = buf;
if (dwRead >= 2 && buf[0] == 0xFEFF) text = buf + 1;
SetWindowTextW(hRE, text);
```

The only encoding handled is UTF-16LE+BOM. ANSI/Big5/UTF-8 input is reinterpreted as UTF-16: byte pairs become wide chars. CRLF (`0D 0A`) collapses to U+0A0D (Gujarati DDA) — **not** a line break. The whole file becomes one ~550K–700K-character "line" with **zero whitespace**.

### 2. Validator runs a backtracking regex on that single mega-line

`SetWindowTextW` triggers `EN_CHANGE` ([src/DIMESettings/SettingsWindow.cpp:3491-3522](../src/DIMESettings/SettingsWindow.cpp#L3491-L3522)) which synchronously calls `ValidateCustomTableRE` ([src/DIMESettings/SettingsWindow.cpp:376-436](../src/DIMESettings/SettingsWindow.cpp#L376-L436)). That iterates `EM_GETLINECOUNT == 1` line and calls `ValidateLine` ([src/DIMESettings/SettingsController.cpp:228-237](../src/DIMESettings/SettingsController.cpp#L228-L237)):

```cpp
static const std::wregex kv_re(L"^([^\\s]+)\\s+(.+)$");
if (!std::regex_match(trimmed, kv_match, kv_re))
```

`[^\s]+` greedily consumes all 549K chars, then `\s+` needs whitespace that does not exist. The engine backtracks one char at a time forever — **catastrophic backtracking**. The UI thread is parked inside `std::regex_match` permanently. That is the spinner.

The regex is a latent bug on its own: any single very-long no-whitespace line would trip it even with a correct reader.

## Verification

Performed on the user's actual file (`.claude/txt/issue130_source.txt`, Big5/CP950, 73,278 lines, 1,098,783 bytes):

| Test | Result | Means |
| --- | --- | --- |
| `DIMESettings.exe --import-custom <file> --mode dayi` on UTF-8 variant | 559 ms, 73,279 output lines | Parser is **clean** — bug is GUI-only |
| Same CLI on the original Big5 file | 109 ms, 5,190 output lines (silent truncation) | Parser separate bug — UTF-8 read mode rejects Big5 bytes; not the hang |
| Reinterpret 1,098,783 Big5 bytes as UTF-16 | 549,392 wchars, **0 LF / 0 CR / 0 space / 0 tab** | Confirms RichEdit will see one giant line |
| Same on UTF-8 variant | 695,861 wchars, also 0 whitespace | Confirms "UTF-8 也轉圈圈" symptom |

Note: the CLI does **not** run the format checker — it goes straight from `parseCINFile` to disk. Step 2 should plumb the same `ValidateLine` into the CLI for consistency (see follow-up plan).

## Files involved

| File | Role | Defect |
| --- | --- | --- |
| [src/DIMESettings/SettingsWindow.cpp:3440-3486](../src/DIMESettings/SettingsWindow.cpp#L3440-L3486) | `ImportCustomTable` button handler | Reads bytes as WCHARs, no `MultiByteToWideChar` |
| [src/DIMESettings/SettingsWindow.cpp:1625-1660](../src/DIMESettings/SettingsWindow.cpp#L1625-L1660) | RichEdit initial-load on settings open | Same byte-as-WCHAR bug |
| [src/DIMESettings/SettingsWindow.cpp:3491-3522](../src/DIMESettings/SettingsWindow.cpp#L3491-L3522) | `EN_CHANGE` handler | Runs validator synchronously, no length guard |
| [src/DIMESettings/SettingsWindow.cpp:376-436](../src/DIMESettings/SettingsWindow.cpp#L376-L436) | `ValidateCustomTableRE` | Per-line iteration, no per-line length cap |
| [src/DIMESettings/SettingsController.cpp:228-237](../src/DIMESettings/SettingsController.cpp#L228-L237) | `ValidateLine` | `std::wregex` catastrophic-backtracking site |

## Reproduction artefacts

`.claude/txt/`:

- `issue130_source.txt` — original (Big5, 73,278 lines, 1,098,783 bytes)
- `issue130_source.meta.txt` — encoding/line-ending metadata
- `issue130_source_utf8_bom.txt`, `issue130_source_utf8_nobom.txt`, `issue130_source_utf16le_bom.txt` — encoding variants

User's pre-existing custom table backed up at `%APPDATA%\DIME\DAYI-CUSTOM.txt.bak_issue130`.

## Out of scope

- The `=,` punctuation request (#130 point ②) and the Shift-digit mode toggle request (#130 point ③) — separate plans.
- Legacy `ConfigDialog` (DIME.dll-side) custom-table dialog — this report is against the new `SettingsWindow` flow only.

---

## Fix summary

Landed 2026-04-29. Full design — code paths, function signatures, performance regimes, three trigger paths, theme handling, configuration constants, regression test matrix — at **[docs/CUSTOM_TABLE_VALIDATION.md](CUSTOM_TABLE_VALIDATION.md)**. This issue records only the headline of what changed and why.

| Change | What | RC component addressed |
| --- | --- | --- |
| 1 | `SettingsModel::ValidateLine` — replaced `std::wregex(L"^([^\\s]+)\\s+(.+)$")` Level-1 format check with a manual `iswspace` scan, added a Level-0 `MAX_TABLE_LINE_LENGTH` hard cap, tightened to require exactly two non-whitespace tokens | Catastrophic-backtracking hang (RC §2 above) |
| 2 | Added `SettingsModel::LoadTextFileAsUtf16` — encoding ladder (UTF-16 BOM → UTF-8 BOM → strict UTF-8 sniff → CP_ACP/Big5). Wired into GUI Import, GUI initial-load, and CLI | Byte-as-WCHAR aliasing on non-UTF-16 sources (RC §1 above) |
| 3 | Restructured CLI `HandleImportCustom` to mirror GUI Save end-to-end: load → validate (shared `ValidateLine`, shared `CreateEngine` factory) → write UTF-16+BOM `.txt` → `parseCINFile(customTableMode=TRUE)` → `.cin`. Added `--no-validate` flag and exit code 3 | (a) CLI now matches GUI's format-check contract; (b) eliminates the parser's `ccs=UTF-8` re-read of the original source, retiring the Big5 silent-truncation bug as a side effect |
| 4 | Live foreground viewport validator using TOM (`ITextDocument` / `ITextRange::SetForeColor`) — never touches the user selection, so live highlighting on every keystroke causes no caret jumps and no flicker. `ValidateViewport` brackets its painting loop with `EM_SETEVENTMASK(0)` to prevent EN_CHANGE recursion. See [docs/RICHEDIT_VIEWPORT_VAL.md](RICHEDIT_VIEWPORT_VAL.md) for the focused implementation reference. | GUI typing responsiveness on imported large files |
| 5 | `EM_EXLIMITTEXT(100 MB)` set immediately after RichEdit creation — RichEdit's default ~32-64K cap silently rejects user input on buffers larger than that (`SetWindowTextW` itself bypasses the cap, so the symptom only shows up post-import). | "Editor accepts no input after 1.6 MB import" |
| 6 | Save handler validates the full buffer in-memory (`ValidateCustomTableBuffer`) at click time. On errors → message box with first-N line numbers, abort write, auto-jump caret to first error after dismiss. Import follows the same auto-jump pattern when its non-blocking pre-scan finds errors. | Off-screen errors users can't see in the viewport |
