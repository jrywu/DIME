# Big5/CP950 Candidate Filter — Implementation Summary

**Scope**: Filter single-character candidates to CP950 (Big5) encodable characters only.
**Architecture**: Parallel engine — one normal engine, one Big5-filtered engine. The filtered table is cached to disk (e.g. `Dayi-Big5.cin`) so that only the first process performs the expensive line-by-line filtering; subsequent processes load the pre-filtered cache directly. Query time selects the engine pointer; no mutation.

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
- **`SetupReadBuffer()`** — first attempts to load a disk-cached version of the filtered table via `TryLoadCache()`. On cache hit, returns immediately. On cache miss, performs the single-pass selective copy (allocates worst-case buffer, walks source lines, calls `FilterLine()` on each, copies only passing lines), then calls `WriteCacheToDisk()` to persist the result for future processes. Sets `_fileSize` to actual written bytes.
- **`FilterLine()`** — pure predicate; parses keycode and character value from CIN (`key<TAB>char`) and TTS (`"key"="char"`) formats. Pass-through rules:
  - Empty lines and `%` directives always pass.
  - Surrogate pairs: SMP (U+10000–U+1FFFF, emoji/symbols) pass via code-point arithmetic; SIP+ (U+20000+, CJK Extension B/C/D/E/F) are filtered. `GetStringTypeW` is not used for surrogates (unreliable across Windows versions).
  - Multi-character value tokens always pass.
  - BMP symbols/emoji (`C3_SYMBOL`) always pass.
  - Single-character tokens passing the CP950 round-trip pass. Round-trip is used instead of `WC_NO_BEST_FIT_CHARS` because `WC_NO_BEST_FIT_CHARS` is unreliable for DBCS code pages — best-fit mapping silently passes non-CP950 characters without setting `usedDefault`.
  - Single-character tokens failing CP950 that carry `C3_IDEOGRAPH` (CJK radicals, Extension A, Kangxi) or `C3_ALPHA` (Cyrillic, Georgian, Arabic, Hebrew) are filtered. All other BMP symbols (Yijing hexagrams, superscripts, vulgar fractions, APL, enclosed numbers) have neither flag and pass.

### Disk Cache

The disk cache avoids repeating the expensive line-by-line CP950 filtering in every process that loads DIME.dll. The first process to need the Big5 engine filters and writes the result to disk; subsequent processes load the cached file directly.

- **`BuildCachePath()`** — derives the cache path from the source filename by inserting `-Big5` before the extension (e.g. `Dayi.cin` → `Dayi-Big5.cin`). Only produces a cache path for files inside `%APPDATA%\DIME\` (test temp files are skipped).
- **`IsCacheValid()`** — opens the cache file and reads its first line, which must be `%src_mtime <decimal64>` — the source file's `st_mtime` at the time the cache was built. Compares this stored value with the current `_wstat(source).st_mtime`. Returns TRUE only if the header parses correctly and the values match.
- **`TryLoadCache()`** — calls `IsCacheValid()`, then reads the cache file (UTF-16LE with BOM), skips the `%src_mtime` header line, and loads the remaining data into `_pReadBuffer`. On any failure, returns FALSE and the caller falls through to filtering.
- **`WriteCacheToDisk()`** — writes the filtered buffer to a `.tmp` file (BOM + `%src_mtime <decimal64>\n` header + filtered data), then atomically renames via `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`. If the rename fails (cache locked by another process), the temp file is deleted and the in-memory buffer is used as-is.

**Concurrency**: atomic `.tmp` → rename ensures no process reads a partially-written cache. If two processes filter simultaneously, both produce identical output — last-writer-wins is harmless.

**Hot-reload**: when the source file changes on disk, `GetReadBufferPointer()` detects the source reload, deletes `_pReadBuffer`, and calls `SetupReadBuffer()`. Inside, `TryLoadCache()` → `IsCacheValid()` finds the stored mtime no longer matches → cache miss → re-filter → `WriteCacheToDisk()` writes a new cache with the updated source mtime.

### Cache File Mapping

All cache files are written to `%APPDATA%\DIME\` alongside the source tables.

| IME Mode | Source File | Cache File | Notes |
| --- | --- | --- | --- |
| DAYI | `Dayi.cin` | `Dayi-Big5.cin` | Primary CIN table |
| DAYI (TTS fallback) | `TableTextServiceDaYi.txt` | `TableTextServiceDaYi-Big5.txt` | When no `.cin` exists |
| ARRAY (ARRAY30_BIG5) | `Array.cin` | `Array-Big5.cin` | |
| ARRAY (TTS fallback) | `TableTextServiceArray.txt` | `TableTextServiceArray-Big5.txt` | When no `.cin` exists |
| PHONETIC | `Phone.cin` | `Phone-Big5.cin` | |
| GENERIC | `Generic.cin` | `Generic-Big5.cin` | |

`Array40.cin` (ARRAY40_BIG5 scope) does not need filtering — it is already a Big5-only table. No cache file is generated for it.

---

## `src/CompositionProcessorEngine.h` / `.cpp`

Added `_pBig5TableDictionaryFile[IM_SLOTS]` and `_pBig5TableDictionaryEngine[IM_SLOTS]` member arrays (initialized to `nullptr`). `GetCandidateList()` selects the Big5 engine when `ARRAY30_BIG5` scope is active or `_big5Filter` is set, falling back to the main engine otherwise.

---

## `src/SetupCompositionProcesseorEngine.cpp`

`SetupBig5Engine()` creates a `CMemoryFile` wrapping the already-loaded main dictionary file for a slot, builds a `CTableDictionaryEngine` over it, and calls `ParseConfig()`. Called after each main-table `ParseConfig()` inside `SetupDictionaryFile()`. Hot-reload propagation follows the same pattern as the main engine. `ReleaseDictionaryFiles()` deletes both arrays in the `IM_SLOTS` loop.

---

## Tests

- **`CMemoryFileTest.cpp`** (new): 31 unit tests (UT-09-01 through UT-09-31) covering constructor, `GetReadBufferPointer()` first/second call and hot-reload, `SetupReadBuffer()` null-source guard, `FilterLine()` for empty lines, directives, multi-char tokens, CP950 chars, non-CP950 CJK, CRLF endings, BMP symbols, Yijing hexagrams, SMP/SIP surrogate pairs, vulgar fractions, and disk cache (cache creation, header validation, cache reuse, cache invalidation on source change, corrupt cache fallback). Added to `DIMETests.vcxproj`.
- **`CMemoryFileIntegrationTest.cpp`** (modified): Added `IT-MF-03` (cache file on disk matches in-memory line count; second CMemoryFile from cache also matches), `IT-MF-04` (advancing source mtime via `_wutime` invalidates cache; rebuilt cache has updated `%src_mtime`), `IT-MF-05` (same as 03 but with DAYI TTS table `TableTextServiceDaYi.txt`), and `IT-MF-06` (same as 04 but with DAYI TTS table). IT-MF-03 through 06 fail (not skip) when the source table is not found in `%APPDATA%\DIME\`.
- **`SettingsDialogIntegrationTest.cpp`** (modified): `IT07_11` updated for item-data-based combo readback; new `IT07-NEW` (`Big5Filter_Persist_NonArray`) verifies round-trip persist for all non-Array modes.

**All 550/550 tests pass** (280 unit + 270 integration). Cache function coverage: **91.7%** (110/120 lines); File.cpp overall: **90.1%** (300/333 lines).

---

## Verification

1. Build with no errors or warnings.
2. Array IME — `ARRAY30_BIG5` scope: only CP950-encodable single-char candidates appear; phrases pass through unchanged.
3. Phonetic/Dayi/Generic — `Big5Filter = 1`: non-CP950 single chars absent.
4. Switch filter off and back on: candidates restore correctly on next keystroke.
5. INI round-trip: `ArrayScope = 5` and `Big5Filter = 1` survive save/close/reopen.
6. Settings dialog UI: Array shows scope combo (w=146) at y=106; Phonetic shows narrow charset combo (w=80) at y=106 and keyboard layout combo at y=197; DAYI/Generic show narrow charset combo at y=106 with max code width edit on the right side of the same row.
7. Hot-reload: replacing the `.cin` file on disk while DIME is running causes both the main engine and the Big5 engine to rebuild correctly on the next keystroke.
8. First launch with Big5 filter enabled: cache file (e.g. `Dayi-Big5.cin`) appears in `%APPDATA%\DIME\`.
9. Second launch: cache loaded directly without re-filtering.
10. Modify source `.cin` on disk: cache regenerated on next keystroke.
11. Delete cache file: self-healing — next load recreates it.

---
