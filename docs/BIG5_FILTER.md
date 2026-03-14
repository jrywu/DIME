# Big5/CP950 Candidate Filter — Implementation Summary

**Scope**: Filter single-character candidates to CP950 (Big5) encodable characters only.
**Architecture**: Parallel engine — one normal engine, one Big5-filtered engine built at load time from a `CMemoryFile` copy. Query time selects the pointer; no mutation.

---

## `src/BaseStructure.h`

Added `ARRAY30_BIG5 = 5` to the `ARRAY_SCOPE` enum.

---

## `src/Config.h` / `src/Config.cpp`

Added static `_big5Filter` field with `GetBig5Filter()` / `SetBig5Filter()` accessors. Initialized to `FALSE` in `SetIMEModeDefaults()` for each non-Array branch.

The Array scope combo (`IDC_COMBO_ARRAY_SCOPE`) now uses `CB_SETITEMDATA` to decouple display order from enum value — 行列30 Big5 appears first (index 0) while retaining enum value 5. Selection and readback use item data instead of raw combo index.

For non-Array modes, a separate charset scope combo (`IDC_COMBO_CHARSET_SCOPE`) offers "完整字集" / "繁體中文". `PSN_APPLY` writes `_big5Filter` from the selection; `WriteConfig()` persists `Big5Filter = %d`.

---

## `src/resource.h` / `src/DIME.rc`

Added `IDC_COMBO_CHARSET_SCOPE` (1164) and `IDC_STATIC_CHARSET_SCOPE` (1165). The non-Array charset combo sits at y=106 (w=80); the Array scope combo remains unchanged at y=106 (w=146). Max code-width controls moved to the right side of y=106 (DAYI/Generic only). Phonetic keyboard layout combo moved to y=197.

---

## `src/DictionarySearch.cpp`

Added `Big5Filter` key handler in the `SEARCH_CONFIG` block — reads `"1"` → `SetBig5Filter(TRUE)`.

---

## `src/File.h` / `src/File.cpp`

`CMemoryFile` extends `CFile` and constructs from a source `CFile*` (non-owning). It copies and filters the source buffer into its own heap buffer on first access and rebuilds automatically when the source reports a disk reload.

- **`GetReadBufferPointer()`** — polls the source for changes; if the source reloaded, discards own buffer and rebuilds. Propagates `fileReloaded = TRUE` to callers.
- **`SetupReadBuffer()`** — single-pass selective copy: allocates worst-case buffer, walks source lines, calls `FilterLine()` on each, and copies only passing lines. Sets `_fileSize` to actual written bytes.
- **`FilterLine()`** — pure predicate; parses keycode and character value from CIN (`key<TAB>char`) and TTS (`"key"="char"`) formats. Pass-through rules:
  - Empty lines and `%` directives always pass.
  - Surrogate pairs: SMP (U+10000–U+1FFFF, emoji/symbols) pass via code-point arithmetic; SIP+ (U+20000+, CJK Extension B/C/D/E/F) are filtered. `GetStringTypeW` is not used for surrogates (unreliable across Windows versions).
  - Multi-character value tokens always pass.
  - BMP symbols/emoji (`C3_SYMBOL`) always pass.
  - Single-character tokens passing the CP950 round-trip pass. Round-trip is used instead of `WC_NO_BEST_FIT_CHARS` because `WC_NO_BEST_FIT_CHARS` is unreliable for DBCS code pages — best-fit mapping silently passes non-CP950 characters without setting `usedDefault`.
  - Single-character tokens failing CP950 that carry `C3_IDEOGRAPH` (CJK radicals, Extension A, Kangxi) or `C3_ALPHA` (Cyrillic, Georgian, Arabic, Hebrew) are filtered. All other BMP symbols (Yijing hexagrams, superscripts, vulgar fractions, APL, enclosed numbers) have neither flag and pass.

---

## `src/CompositionProcessorEngine.h` / `.cpp`

Added `_pBig5TableDictionaryFile[IM_SLOTS]` and `_pBig5TableDictionaryEngine[IM_SLOTS]` member arrays (initialized to `nullptr`). `GetCandidateList()` selects the Big5 engine when `ARRAY30_BIG5` scope is active or `_big5Filter` is set, falling back to the main engine otherwise.

---

## `src/SetupCompositionProcesseorEngine.cpp`

`SetupBig5Engine()` creates a `CMemoryFile` wrapping the already-loaded main dictionary file for a slot, builds a `CTableDictionaryEngine` over it, and calls `ParseConfig()`. Called after each main-table `ParseConfig()` inside `SetupDictionaryFile()`. Hot-reload propagation follows the same pattern as the main engine. `ReleaseDictionaryFiles()` deletes both arrays in the `IM_SLOTS` loop.

---

## Tests

- **`CMemoryFileTest.cpp`** (new): 20 unit tests (UT-09-01 through UT-09-20) covering constructor, `GetReadBufferPointer()` first/second call and hot-reload, `SetupReadBuffer()` null-source guard, and `FilterLine()` for empty lines, directives, multi-char tokens, CP950 chars, non-CP950 CJK, CRLF endings, BMP symbols, Yijing hexagrams, SMP/SIP surrogate pairs, and vulgar fractions. Added to `DIMETests.vcxproj`.
- **`SettingsDialogIntegrationTest.cpp`** (modified): `IT07_11` updated for item-data-based combo readback; new `IT07-NEW` (`Big5Filter_Persist_NonArray`) verifies round-trip persist for all non-Array modes.

**All 362/362 tests pass** (151 unit + 211 integration). `CMemoryFile` code coverage: 100%.

---

## Verification

1. Build with no errors or warnings.
2. Array IME — `ARRAY30_BIG5` scope: only CP950-encodable single-char candidates appear; phrases pass through unchanged.
3. Phonetic/Dayi/Generic — `Big5Filter = 1`: non-CP950 single chars absent.
4. Switch filter off and back on: candidates restore correctly on next keystroke.
5. INI round-trip: `ArrayScope = 5` and `Big5Filter = 1` survive save/close/reopen.
6. Settings dialog UI: Array shows scope combo (w=146) at y=106; Phonetic shows narrow charset combo (w=80) at y=106 and keyboard layout combo at y=197; DAYI/Generic show narrow charset combo at y=106 with max code width edit on the right side of the same row.
7. Hot-reload: replacing the `.cin` file on disk while DIME is running causes both the main engine and the Big5 engine to rebuild correctly on the next keystroke.
