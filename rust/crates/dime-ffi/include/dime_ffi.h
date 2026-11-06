/* dime_ffi.h - C ABI of the Rust dime-core library (rust/crates/dime-ffi).
 *
 * Conventions
 *   - Strings are UTF-16 (pointer, length) pairs; never NUL-terminated.
 *   - Objects returned by *_new / collect functions are owned by the caller
 *     and must be released with the matching *_free function.
 *   - Pointers returned by accessors (dime_items_item, dime_engine_radical_at,
 *     ...) stay valid until the owning object is modified or freed.
 *   - Every function tolerates NULL handles and never lets a Rust panic
 *     unwind into the caller (it returns the documented failure value).
 *
 * Link with: dime_ffi.lib kernel32.lib ntdll.lib userenv.lib ws2_32.lib dbghelp.lib
 * CRT: build with RUSTFLAGS="-C target-feature=+crt-static" for /MT consumers
 * (DIME.dll Release); the default build matches /MD, and /MDd consumers add
 * /NODEFAULTLIB:msvcrt.lib (see DIME.vcxproj / DIMETests.vcxproj).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DimeEngine DimeEngine;
typedef struct DimeItems DimeItems;
typedef struct DimeBuffer DimeBuffer;

/* Matches DICTIONARY_TYPE in BaseStructure.h */
enum { DIME_DICT_TTS = 0, DIME_DICT_INI = 1, DIME_DICT_CIN = 2, DIME_DICT_LIME = 3 };
/* Matches SEARCH_SECTION in BaseStructure.h */
enum { DIME_SECTION_TEXT = 0, DIME_SECTION_SYMBOL = 1, DIME_SECTION_PHRASE = 2, DIME_SECTION_PHRASE_FROM_KEYSTROKE = 3 };

/* ---- engine (CTableDictionaryEngine) ----------------------------------- */

/* buf: file contents without BOM (what CFile::GetReadBufferPointer returns). Copied. */
DimeEngine* dime_engine_new(uint32_t lcid, const uint16_t* buf, size_t len, int32_t dict_type);
void        dime_engine_free(DimeEngine* e);
void        dime_engine_set_buffer(DimeEngine* e, const uint16_t* buf, size_t len);
void        dime_engine_set_search_section(DimeEngine* e, int32_t section);
void        dime_engine_set_has_cin_phrase_section(DimeEngine* e, int32_t value);

int32_t dime_engine_parse_config(DimeEngine* e);            /* 1 = ok */
int32_t dime_engine_is_sorted_cin(const DimeEngine* e);
int32_t dime_engine_has_phrase_section(const DimeEngine* e);
int32_t dime_engine_auto_compose(const DimeEngine* e);      /* -1 = absent, else 0/1 */

/* Copy up to cap units into out (may be NULL); returns the full length. */
size_t dime_engine_get_selkey(const DimeEngine* e, uint16_t* out, size_t cap);
size_t dime_engine_get_endkey(const DimeEngine* e, uint16_t* out, size_t cap);

/* Radical map (%keyname / [Radical]), ordered by key. Returns 1 if i is valid. */
size_t  dime_engine_radical_count(const DimeEngine* e);
int32_t dime_engine_radical_at(const DimeEngine* e, size_t i, uint16_t* key, const uint16_t** value, size_t* value_len);

/* Sorted-CIN index: initial -> offset (UTF-16 units). Ordered by key. */
size_t  dime_engine_radical_index_count(const DimeEngine* e);
int32_t dime_engine_radical_index_at(const DimeEngine* e, size_t i, uint16_t* key, size_t* offset);

/* Raw [Config] entries in file order. */
size_t  dime_engine_config_entry_count(const DimeEngine* e);
int32_t dime_engine_config_entry_at(const DimeEngine* e, size_t i, const uint16_t** key, size_t* key_len,
                                    const uint16_t** value, size_t* value_len);

/* ---- searches (return NULL only on invalid handle / internal error) ----- */

DimeItems* dime_engine_collect_word(const DimeEngine* e, const uint16_t* key, size_t len);
/* CollectWord(key, CDIMEArray<CStringRange>*): no sorted index, find_key is empty. */
DimeItems* dime_engine_collect_word_strings(const DimeEngine* e, const uint16_t* key, size_t len);
/* existing: item strings already in the caller's list; they count as duplicates
   (CTableDictionaryEngine de-duplicates against the whole list). May be NULL/0.
   Returns only the new items. */
DimeItems* dime_engine_collect_word_for_wildcard(const DimeEngine* e, const uint16_t* key, size_t len,
                                                 const DimeEngine* word_freq /* nullable */,
                                                 const uint16_t* const* existing, const size_t* existing_lens,
                                                 size_t existing_count);
DimeItems* dime_engine_collect_word_from_converted_string(const DimeEngine* e, const uint16_t* s, size_t len);
DimeItems* dime_engine_collect_word_from_converted_string_for_wildcard(const DimeEngine* e, const uint16_t* s, size_t len);

void dime_engine_sort_by_find_key(const DimeEngine* e, DimeItems* items);
void dime_engine_sort_by_word_frequency(const DimeEngine* e, DimeItems* items);

/* ---- candidate item lists ------------------------------------------------ */

size_t          dime_items_count(const DimeItems* items);
const uint16_t* dime_items_item(const DimeItems* items, size_t i, size_t* len);
const uint16_t* dime_items_find_key(const DimeItems* items, size_t i, size_t* len);
int32_t         dime_items_word_frequency(const DimeItems* items, size_t i);
void            dime_items_free(DimeItems* items);

/* ---- Big5 filter (CMemoryFile::SetupReadBuffer without the disk cache) -- */

DimeBuffer*     dime_big5_filter(uint32_t lcid, const uint16_t* src, size_t len);
const uint16_t* dime_buffer_data(const DimeBuffer* b, size_t* len);
void            dime_buffer_free(DimeBuffer* b);

/* ---- sentence conversion (dime_core::sentence) ---------------------------- */

typedef struct DimeLexicon DimeLexicon;

DimeLexicon* dime_lexicon_new(void);
void         dime_lexicon_free(DimeLexicon* lex);
/* Every multi-character value of a .cin table becomes a word (e.g. Array-Phrase.cin).
   Returns the number of new words. */
size_t  dime_lexicon_load_cin_values(DimeLexicon* lex, const uint16_t* buf, size_t len);
/* "text<TAB>count" lines (e.g. TCFreq.cin): one character -> character frequency,
   longer text -> word with that frequency.  Returns the number of lines used. */
size_t  dime_lexicon_load_freq_table(DimeLexicon* lex, const uint16_t* buf, size_t len);
int32_t dime_lexicon_add_word(DimeLexicon* lex, const uint16_t* word, size_t len, uint32_t freq);
size_t  dime_lexicon_word_count(const DimeLexicon* lex);
size_t  dime_lexicon_char_count(const DimeLexicon* lex);

/* Learned data (user choices per code, user words).  Text format: see
   dime_user_model_export.  Counts only grow; merge adds counts. */
typedef struct DimeUserModel DimeUserModel;

DimeUserModel* dime_user_model_new(void);
void           dime_user_model_free(DimeUserModel* model);
/* Add the entries of an exported text (UTF-16, BOM optional).  Returns entries read. */
size_t      dime_user_model_import(DimeUserModel* model, const uint16_t* buf, size_t len);
/* "W<TAB>word<TAB>count" / "C<TAB>code<TAB>char<TAB>count" lines after a header line,
   most used first.  Free with dime_buffer_free. */
DimeBuffer* dime_user_model_export(const DimeUserModel* model);
void        dime_user_model_merge(DimeUserModel* dst, const DimeUserModel* src);
void        dime_user_model_clear(DimeUserModel* model);
size_t      dime_user_model_word_count(const DimeUserModel* model);
size_t      dime_user_model_choice_count(const DimeUserModel* model);

/* Sentence arrays shared by convert and learn:
   - position i typed codes[i] (codes/code_lens nullable together: no codes);
   - it owns candidates [cand_start[i], cand_start[i+1]) of cands/cand_lens
     (cand_start has position_count + 1 entries, cand_start[0] == 0);
   - pinned (nullable) holds the candidate fixed by the user, or -1. */

/* Pick one candidate per position into out_chosen.  lex and user may be NULL.
   Returns 1 on success, 0 on invalid arguments. */
int32_t dime_sentence_convert(const DimeLexicon* lex, const DimeUserModel* user, size_t position_count,
                              const uint16_t* const* codes, const size_t* code_lens, const size_t* cand_start,
                              const uint16_t* const* cands, const size_t* cand_lens, const int32_t* pinned,
                              int32_t* out_chosen);

/* Learn from a committed sentence (chosen = candidate index per position):
   pinned single Han characters teach their code choice and neighbour pairs.
   Returns 1 if anything was learned. */
int32_t dime_user_model_learn(DimeUserModel* model, size_t position_count,
                              const uint16_t* const* codes, const size_t* code_lens, const size_t* cand_start,
                              const uint16_t* const* cands, const size_t* cand_lens, const int32_t* pinned,
                              const int32_t* chosen);

/* ---- settings (dime_config, port of CConfig) ------------------------------ */

/* One six-colour candidate-window palette (COLORREF values). */
typedef struct DimePalette {
    uint32_t item, phrase, number, item_bg, selected, selected_bg;
} DimePalette;

/* CConfig's plain-data settings.  BOOL / enum fields are int32_t, UINT /
   COLORREF fields uint32_t, GUIDs their 16 in-memory bytes.  Must match
   dime_config::values::ConfigValues (308 bytes, checked on both sides). */
typedef struct DimeConfigValues {
    int32_t auto_compose, space_as_page_down, space_as_first_cand_selkey, arrow_key_sw_pages,
            clear_on_beep, do_beep, do_beep_notify, do_beep_on_candi, sentence_mode,
            activated_keyboard_mode, make_phrase, show_notify_desktop, do_han_convert, font_italic,
            custom_table_priority, dayi_article_mode, array_force_sp, array_notify_sp,
            array_single_quote_custom_phrase, big5_filter, reload_reverse_conversion,
            app_permission_set, load_table_mode, color_mode_key_found;
    uint32_t max_codes, font_size, font_weight;
    int32_t ime_shift_mode, double_single_byte_mode, shift_english_digit_mode, color_mode,
            numeric_pad, array_scope, phonetic_keyboard_layout, dayi_selkey_mode;
    DimePalette custom, light, dark;
    uint16_t font_face_name[32];              /* NUL-terminated (LF_FACESIZE) */
    uint8_t  reverse_conversion_clsid[16];
    uint8_t  reverse_conversion_guid_profile[16];
} DimeConfigValues;

/* Host facts the defaults depend on. */
typedef struct DimeConfigEnv {
    uint32_t sys_3d_highlight;      /* GetSysColor(COLOR_3DHIGHLIGHT) */
    int32_t  windows_1809_or_later; /* Global::isWindows1809OrLater */
} DimeConfigEnv;

typedef struct DimeSettings DimeSettings;

/* values NULL: the Config.cpp static initialisers.  env is required. */
DimeSettings* dime_settings_new(const DimeConfigValues* values, const DimeConfigEnv* env);
void          dime_settings_free(DimeSettings* s);
int32_t       dime_settings_get_values(const DimeSettings* s, DimeConfigValues* out); /* 1 = ok */
void          dime_settings_set_values(DimeSettings* s, const DimeConfigValues* values);
/* ReverseConversionDescription: returns 0 when it is NULL. */
int32_t       dime_settings_get_description(const DimeSettings* s, const uint16_t** text, size_t* len);
void          dime_settings_set_description(DimeSettings* s, const uint16_t* text, size_t len, int32_t present);

/* ApplyIniConfigEntry: one [Config] key/value (keys compared with CompareString(lcid)). */
void dime_settings_apply_entry(DimeSettings* s, uint32_t lcid, const uint16_t* key, size_t key_len,
                               const uint16_t* value, size_t value_len);
/* CConfig::LoadConfig, file present: begin_load, apply every entry, finish_load
   (legacy ColorMode migration).  File missing: load_missing. */
void dime_settings_begin_load(DimeSettings* s, int32_t ime_mode);
void dime_settings_finish_load(DimeSettings* s);
void dime_settings_load_missing(DimeSettings* s, int32_t ime_mode);
void dime_settings_set_mode_defaults(DimeSettings* s, int32_t ime_mode);  /* SetIMEModeDefaults */
void dime_settings_reset_all_defaults(DimeSettings* s, int32_t ime_mode); /* ResetAllDefaults */
/* CConfig::WriteConfig text: UTF-16 with BOM and CRLF; write the units as-is.
   Free with dime_buffer_free. */
DimeBuffer* dime_settings_serialize_ini(const DimeSettings* s, int32_t ime_mode);

#ifdef __cplusplus
}
#endif
