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

/* Process-wide settings (CConfig's storage when built with DIME_USE_RUST_CONFIG).
   The object and dime_settings_values_ptr(global) never move; dime_settings_free
   ignores it.  Access is unsynchronised, like the C++ statics it replaces. */
DimeSettings*     dime_settings_global(void);
void              dime_settings_global_init(const DimeConfigEnv* env); /* = static initialisers */
void              dime_settings_set_env(DimeSettings* s, const DimeConfigEnv* env);
DimeConfigValues* dime_settings_values_ptr(DimeSettings* s);
/* NUL-terminated description or NULL; valid until the description changes. */
const uint16_t*   dime_settings_description_cstr(const DimeSettings* s);

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

/* ---- settings key registry (the CLI's --get / --set keys) ----------------- */

/* Matches KeyType in DIMESettings/CLI.h. */
enum { DIME_KEY_BOOL = 0, DIME_KEY_INT = 1, DIME_KEY_COLOR = 2, DIME_KEY_STRING = 3, DIME_KEY_CLSID = 4 };
/* Matches CLI_MASK_* in DIMESettings/CLI.h. */
enum { DIME_KEY_MASK_DAYI = 0x1, DIME_KEY_MASK_ARRAY = 0x2, DIME_KEY_MASK_PHONETIC = 0x4, DIME_KEY_MASK_GENERIC = 0x8 };

int32_t dime_key_count(void);
/* name is not NUL-terminated and lives for the whole process.  min/max apply to
   BOOL/INT keys.  Returns 1 if index is valid. */
int32_t dime_key_info(int32_t index, const uint16_t** name, size_t* name_len, int32_t* key_type,
                      int32_t* min, int32_t* max, uint32_t* mode_mask);
int32_t dime_key_find(const uint16_t* name, size_t len);          /* index, or -1; ASCII case-insensitive */
int32_t dime_key_applicable(int32_t index, int32_t ime_mode);     /* 1 if the key applies to the mode */
/* The text --get prints.  Free with dime_buffer_free. */
DimeBuffer* dime_settings_get_key(const DimeSettings* s, int32_t index);
/* Validate and store a --set value: 0 = ok; 1 = rejected, *error (nullable) gets
   the message without newline (free with dime_buffer_free); 2 = invalid handle/index. */
int32_t dime_settings_set_key(DimeSettings* s, int32_t index, const uint16_t* value, size_t len,
                              DimeBuffer** error);

/* ---- candidate selection / paging (CCandidateListModel) ------------------- */
/* Only the selection and page layout; the strings stay with the caller, which
   calls push_item for every candidate it adds.  page_size = number of select
   keys (0 = no range).  HRESULTs match the C++ code. */
typedef struct DimeCandidates DimeCandidates;

DimeCandidates* dime_candidates_new(void);
void     dime_candidates_free(DimeCandidates* c);
void     dime_candidates_push_item(DimeCandidates* c);
void     dime_candidates_clear(DimeCandidates* c);
int32_t  dime_candidates_selection(DimeCandidates* c);
void     dime_candidates_set_selection_raw(DimeCandidates* c, int32_t index);
uint32_t dime_candidates_page_count(DimeCandidates* c);
uint32_t dime_candidates_page_start(DimeCandidates* c, uint32_t page);
void     dime_candidates_set_page_index(DimeCandidates* c, const uint32_t* pages, uint32_t count);
int32_t  dime_candidates_current_page(DimeCandidates* c, uint32_t* page);
int32_t  dime_candidates_set_selection_in_page(DimeCandidates* c, int32_t pos);
int32_t  dime_candidates_move_selection(DimeCandidates* c, int32_t offset);
int32_t  dime_candidates_set_selection(DimeCandidates* c, uint32_t page_size, int32_t index, int32_t* applied);
int32_t  dime_candidates_move_page(DimeCandidates* c, uint32_t page_size, int32_t offset, int32_t* wrapped);
int32_t  dime_candidates_set_selection_offset(DimeCandidates* c, uint32_t page_size, int32_t offset);
int32_t  dime_candidates_adjust_page_index_for_selection(DimeCandidates* c, uint32_t page_size);
int32_t  dime_candidates_current_page_has_empty_items(DimeCandidates* c, uint32_t page_size, int32_t* has_empty);
int32_t  dime_candidates_adjust_page_index(DimeCandidates* c, uint32_t page_size, uint32_t* current_page,
                                           uint32_t* current_page_index);

/* ---- Key classification (CCompositionProcessorEngine::IsVirtualKeyNeed) ----
 * A snapshot of everything IsVirtualKeyNeed reads; enum fields carry the C++
 * enum values, booleans are BOOL (non-zero = true). */
typedef struct DimeSelkey {
    uint16_t printable;
    uint32_t index;
    uint32_t modifiers;
} DimeSelkey;

typedef struct DimeKeyInput {
    uint32_t code;
    uint16_t wch;
    uint16_t base_char;             /* (WCHAR)MapVirtualKey(code, MAPVK_VK_TO_CHAR) */
    int32_t  wch_printable;         /* iswprint(wch) */
    int32_t  composing;
    uint32_t candidate_mode;
    uint32_t candi_count;
    int32_t  candi_selection;
    int32_t  has_key_state;         /* pKeyState != nullptr */
    uint32_t modifiers;             /* Global::ModifiersValue */
    int32_t  caps_on;               /* GetKeyState(VK_CAPITAL) & 1 */
    uint32_t ime_mode;              /* Global::imeMode */
    int32_t  dictionary_available;  /* IsDictionaryAvailable(_imeMode) */
    uint32_t array_scope;
    uint32_t numeric_pad;
    int32_t  space_as_page_down;
    uint32_t shift_english_digit_mode;
    int32_t  phonetic_eten;
    uint32_t candidate_page_size;
    int32_t  has_wildcard_in_buffer;
    int32_t  buffer_null;           /* _keystrokeBuffer.Get() == nullptr */
    const uint16_t* buffer;
    uint32_t buffer_len;
    const uint32_t* keystroke_functions; /* _KeystrokeComposition[i].Function */
    uint32_t keystroke_count;
    const uint16_t* end_keys;       /* _pEndkey, NULL when none */
    uint32_t end_keys_len;
    const DimeSelkey* selkeys;      /* active candidate range */
    uint32_t selkey_count;
} DimeKeyInput;

/* Returns 1 if the IME eats the key; category/function receive the key state. */
int32_t dime_classify_key(const DimeKeyInput* input, uint32_t* category, uint32_t* function);

/* ---- Key events (KeyEventSink, rust/crates/dime-core/src/keyevent.rs) ---- */
typedef struct DimeSession DimeSession;
typedef struct DimeDecideInput {
    int32_t  keyboard_disabled;
    int32_t  is_open;                /* IME-mode compartment */
    int32_t  is_double_single_byte;  /* double/single-byte compartment */
    int32_t  has_engine;
    int32_t  is_composing;
    uint32_t candi_count;
    size_t   virtual_key_length;
    DimeKeyInput key;                /* code = VKeyFromVKPacketAndWchar(...), wch, current candidate mode */
} DimeDecideInput;

typedef struct DimeDecideOutput {
    int32_t  eaten;
    uint16_t wch;                    /* *pwch */
    uint32_t category;
    uint32_t function;
    int32_t  reset_candidate_mode;   /* stale mode: _candidateMode = NONE, _isCandidateWithWildcard = FALSE */
} DimeDecideOutput;

void    dime_decide_key_eaten(const DimeSession* session, const DimeDecideInput* input, DimeDecideOutput* output);
void    dime_session_on_test_key_down(DimeSession* session, uint32_t code, intptr_t lparam);
void    dime_session_on_key_up(DimeSession* session, uint32_t code);
void    dime_session_after_key_down(DimeSession* session, uint32_t function, uint32_t code, uint16_t wch, uint32_t modifiers);
int32_t dime_session_shifted_english(const DimeSession* session);
int32_t dime_session_pending_wildcard(const DimeSession* session);
/* 1 and the key state (in/out) to invoke the handler with, or 0. */
int32_t dime_test_key_down_invoke(int32_t eaten, uint32_t* category, uint32_t* function);
int32_t dime_key_down_invoke(int32_t eaten, uint32_t* category, uint32_t* function, uint32_t code, uint16_t wch, uint32_t candidate_mode);

/* ---- Keystroke buffer (CCompositionProcessorEngine) ----
 * The buffer stays in C++; these return what the C++ code would change. */

/* IsEscapeInput. has_first/first_raw: *_keystrokeBuffer.Get() when the buffer is
 * not null, read even at length zero. */
int32_t  dime_is_escape_input(const uint16_t* keys, size_t len, int32_t has_first, uint16_t first_raw,
                              uint32_t ime_mode, uint32_t array_scope);
uint32_t dime_phonetic_add_key(uint32_t syllable, uint16_t wch, int32_t eten);   /* addPhoneticKey */
uint32_t dime_phonetic_remove_last(uint32_t syllable);                         /* removeLastPhoneticSymbol */
/* buildKeyStrokesFromPhoneticSyllable: keys + terminator into out[5]; returns the count. */
uint32_t dime_phonetic_keystrokes(uint32_t syllable, uint16_t* out);

typedef struct DimeAddKeyInput {
    const uint16_t* keys;
    size_t   len;
    int32_t  null;                  /* _keystrokeBuffer.Get() == nullptr */
    uint32_t ime_mode;              /* Global::imeMode */
    uint32_t array_scope;
    int32_t  phonetic_eten;
    uint32_t max_codes;             /* CConfig::GetMaxCodes() */
    uint32_t syllable;              /* phoneticSyllable */
} DimeAddKeyInput;

typedef struct DimeAddKeyResult {
    int32_t  accepted;              /* AddVirtualKey's return value */
    int32_t  beep;                  /* BEEP_TYPE to sound, -1 for none */
    uint32_t syllable;              /* new phoneticSyllable */
    int32_t  action;                /* 0 buffer unchanged, 1 append `append`, 2 replace with `replace` */
    uint16_t append;
    uint16_t replace[4];
    uint32_t replace_len;
} DimeAddKeyResult;

void   dime_add_key(const DimeAddKeyInput* input, uint16_t wch, DimeAddKeyResult* out);
/* RemoveVirtualKey on the buffer in place; returns the new length. */
size_t dime_remove_key(uint16_t* keys, size_t len, size_t index);

typedef struct DimeRadical {
    uint16_t key;
    const uint16_t* value;
    size_t   value_len;
} DimeRadical;

/* GetReadingString for a non-empty key code: reading string + terminator into
 * out[64] (MAX_READINGSTRING, truncated like StringCchCat); returns its length. */
size_t dime_reading_string(const uint16_t* keys, size_t len, int32_t escape_input, const DimeRadical* radicals,
                           size_t radical_count, uint16_t* out, int32_t* has_wildcard);

/* ---- Candidate generation plan (CCompositionProcessorEngine::GetCandidateList) ----
 * Rust decides the steps; C++ runs them against its dictionary engines. */
typedef struct DimeCandidateSearchInput {
    uint32_t ime_mode;                  /* Global::imeMode */
    int32_t  dictionary_available;      /* IsDictionaryAvailable(Global::imeMode) */
    const uint16_t* buffer;
    size_t   buffer_len;
    int32_t  buffer_null;
    int32_t  incremental;
    int32_t  wildcard_search;
    int32_t  array_phrase_ending;
    uint32_t array_scope;
    int32_t  big5_filter;
    int32_t  custom_table_priority;
    int32_t  array_single_quote_custom_phrase;
    int32_t  escape_input;              /* IsEscapeInput() */
    int32_t  table_is_tts;              /* main engines' GetDictionaryType() == TTS */
    int32_t  big5_is_tts;
    int32_t  has_big5;                  /* engine pointers present */
    int32_t  has_custom;
    int32_t  has_array_short_code;
    int32_t  has_array_phrase;
    int32_t  has_ext_b;
    int32_t  has_ext_cd;
    int32_t  has_ext_e;
    int32_t  has_freq_table;
    int32_t  keystroke_sort;            /* _isKeystrokeSort */
    int32_t  wildcard_freq_sort;        /* _isWildCardWordFreqSort */
    uint32_t phonetic_syllable;
} DimeCandidateSearchInput;

enum {
    DIME_CANDGEN_SINGLE_WILDCARD_ITEM = 0,
    DIME_CANDGEN_RESET_ACTIVE_RANGE = 1,
    DIME_CANDGEN_SET_SECTION = 2,       /* engine, arg = SEARCH_SECTION */
    DIME_CANDGEN_COLLECT = 3,           /* engine, key */
    DIME_CANDGEN_COLLECT_WILDCARD = 4,  /* engine, key, arg = pass the TC frequency engine */
    DIME_CANDGEN_RETURN_IF_EMPTY = 5,
    DIME_CANDGEN_SORT_BY_FIND_KEY_CODE = 6,
    DIME_CANDGEN_TRIM_FIND_KEY_CODES = 7,
    DIME_CANDGEN_SETUP_FREQ_TABLE = 8,
    DIME_CANDGEN_SORT_BY_WORD_FREQUENCY = 9, /* engine; only if the frequency table is loaded */
    DIME_CANDGEN_UPDATE_WIDTH = 10,
    DIME_CANDGEN_CLEAR_WILDCARD_FLAG = 11,
};
enum {
    DIME_ENGINE_TABLE = 0, DIME_ENGINE_BIG5 = 1, DIME_ENGINE_CUSTOM = 2, DIME_ENGINE_ARRAY_SHORT_CODE = 3,
    DIME_ENGINE_ARRAY_PHRASE = 4, DIME_ENGINE_ARRAY_EXT_B = 5, DIME_ENGINE_ARRAY_EXT_CD = 6, DIME_ENGINE_ARRAY_EXT_E = 7,
};
enum {
    DIME_KEY_BUFFER = 0, DIME_KEY_INCREMENTAL = 1, DIME_KEY_NO_TONE = 2, DIME_KEY_CUSTOM_PHRASE = 3,
};

typedef struct DimeCandidateStep {
    uint32_t op;
    uint32_t engine;
    uint32_t key;
    uint32_t arg;
} DimeCandidateStep;

/* Writes up to cap steps; returns the total count. */
size_t dime_plan_candidate_search(const DimeCandidateSearchInput* input, DimeCandidateStep* steps, size_t cap);

/* GetCandidateStringInConverted: the associated-phrase lookup key for the character just
 * committed (exact for CIN tables). out needs len + 2 units; returns the key length. */
size_t dime_associated_phrase_key(const uint16_t* text, size_t len, int32_t phrase_table_is_tts,
                                  int32_t has_phrase_section, int32_t has_cin_phrase_section, uint16_t* out);

/* ---- Key handler flow (rust/crates/dime-core/src/session.rs) ----
 * The handlers run in Rust and drive the text service through this host,
 * implemented in C++ (RustSessionHost.cpp). Callbacks must not throw.
 * Booleans are int32_t; handlers return HRESULT. Strings returned through
 * `out` must stay valid until the next callback. The field order must match
 * DimeHostVtbl in rust/crates/dime-ffi/src/lib.rs. */
typedef struct DimeHostVtbl {
    int32_t (*is_composing)(void* ctx);
    int32_t (*is_full_shape)(void* ctx);
    size_t (*virtual_key_length)(void* ctx);
    int32_t (*caps_lock_on)(void* ctx);
    uint32_t (*shift_english_digit_mode)(void* ctx);
    int32_t (*dayi_article_mode)(void* ctx);
    int32_t (*is_alpha)(void* ctx, uint16_t c);
    int32_t (*layout_char)(void* ctx, uint16_t c, int32_t caps_lock_on, uint16_t* out);
    int32_t (*add_char_and_finalize)(void* ctx, const uint16_t* text, size_t len);
    void (*remove_dummy_composition)(void* ctx);
    void (*delete_candidate_list)(void* ctx, int32_t force);
    void (*terminate_composition)(void* ctx);
    int32_t (*has_ui_presenter)(void* ctx);
    int32_t (*is_sentence_mode)(void* ctx);
    uint32_t (*ime_mode)(void* ctx);
    uint32_t (*array_scope)(void* ctx);
    int32_t (*clear_on_beep)(void* ctx);
    int32_t (*array_force_sp)(void* ctx);
    int32_t (*array_notify_sp)(void* ctx);
    int32_t (*do_han_convert)(void* ctx);
    int32_t (*make_phrase)(void* ctx);
    int32_t (*is_cmd_shell)(void* ctx);
    int32_t (*is_ui_less_mode)(void* ctx);
    int32_t (*reverse_conversion_active)(void* ctx);
    int32_t (*is_escape_input_leading)(void* ctx);
    int32_t (*is_array_short_code)(void* ctx);
    size_t (*selected_candidate)(void* ctx, const uint16_t** out);
    int32_t (*candidate_selection)(void* ctx);
    int32_t (*selkey_index)(void* ctx, uint32_t code, uint16_t wch);
    int32_t (*set_candidate_selection_in_page)(void* ctx, int32_t index);
    void (*advise_ui_changed_by_arrow_key)(void* ctx, uint32_t function);
    int32_t (*array_special_code_matches)(void* ctx);
    int32_t (*array_special_code_for)(void* ctx, const uint16_t* text, size_t len, const uint16_t** out, size_t* out_len);
    size_t (*sc_from_tc)(void* ctx, const uint16_t* text, size_t len, const uint16_t** out);
    void (*show_notify)(void* ctx, const uint16_t* text, size_t len, uint32_t kind);
    void (*notify_selected_key_code)(void* ctx);
    int32_t (*show_associated_phrases)(void* ctx, uint16_t last_char);
    void (*start_composition)(void* ctx);
    int32_t (*add_composing_and_char)(void* ctx, const uint16_t* text, size_t len);
    void (*set_commit_string)(void* ctx, const uint16_t* text, size_t len);
    void (*async_reverse_conversion)(void* ctx);
    void (*beep)(void* ctx, uint32_t kind);
    int32_t (*selection)(void* ctx, int32_t strict);
    int32_t (*add_virtual_key)(void* ctx, uint16_t wch);
    void (*remove_virtual_key)(void* ctx, size_t index);
    int32_t (*is_escape_input)(void* ctx);
    size_t (*reading_string)(void* ctx, const uint16_t** out, int32_t* wildcard);
    int32_t (*auto_compose)(void* ctx);
    int32_t (*do_beep_on_candi)(void* ctx);
    uint32_t (*fetch_candidates)(void* ctx, int32_t incremental, int32_t wildcard, int32_t array_phrase_ending);
    void (*set_candidate_text)(void* ctx);
    void (*clear_candidate_list)(void* ctx);
    void (*hide_candidate_window)(void* ctx);
    int32_t (*create_and_start_candidate)(void* ctx);
    void (*composition_arrow_key)(void* ctx, uint32_t function);
    int32_t (*sentence_write)(void* ctx, const uint16_t* text, size_t len, size_t caret, int32_t with_attributes);
    void (*sentence_models)(void* ctx, const DimeLexicon** lexicon, const DimeUserModel** user);
    void (*sentence_learn)(void* ctx);
    void (*sentence_clear_display)(void* ctx);
    size_t (*keystroke_code)(void* ctx, const uint16_t** out);
    uint32_t (*fetched_count)(void* ctx);
    size_t (*fetched_string)(void* ctx, uint32_t index, const uint16_t** out);
    void (*show_sentence_candidates)(void* ctx, const uint16_t* const* candidates, const size_t* lens, size_t count, const uint16_t* code, size_t code_len, int32_t selection);
} DimeHostVtbl;

/* IME state owned by Rust, kept by CDIME across edit sessions. */
typedef struct DimeSession DimeSession;

typedef struct DimeHost {
    const DimeHostVtbl* vtbl;
    void* ctx;
    DimeSession* session;
} DimeHost;

int32_t dime_session_handle_complete(const DimeHost* host);
int32_t dime_session_handle_cancel(const DimeHost* host);
int32_t dime_session_handle_double_single_byte(const DimeHost* host, uint16_t wch);
int32_t dime_session_handle_shift_english_input(const DimeHost* host, uint32_t code, uint16_t wch);
int32_t dime_session_handle_address_char(const DimeHost* host, uint16_t wch);
int32_t dime_session_handle_candidate_finalize(const DimeHost* host);
int32_t dime_session_handle_candidate_arrow_key(const DimeHost* host, uint32_t function);
int32_t dime_session_handle_candidate_select_by_number(const DimeHost* host, uint32_t code, uint16_t wch);
int32_t dime_session_handle_phrase_finalize(const DimeHost* host);
int32_t dime_session_handle_phrase_select_by_number(const DimeHost* host, uint32_t code, uint16_t wch);
int32_t dime_session_handle_composition_input(const DimeHost* host, uint16_t wch);
int32_t dime_session_handle_composition_input_worker(const DimeHost* host);
int32_t dime_session_handle_composition_finalize(const DimeHost* host, int32_t is_candidate_list);
int32_t dime_session_handle_composition_convert(const DimeHost* host, int32_t wildcard, int32_t array_phrase_ending);
int32_t dime_session_handle_composition_backspace(const DimeHost* host);
int32_t dime_session_handle_composition_arrow_key(const DimeHost* host, uint32_t function);
int32_t dime_session_handle_sentence_commit(const DimeHost* host);
int32_t dime_session_handle_sentence_backspace(const DimeHost* host, uint32_t code);
int32_t dime_session_handle_sentence_arrow_key(const DimeHost* host, uint32_t function);

DimeSession* dime_session_new(void);
void        dime_session_free(DimeSession* s);
int32_t     dime_session_sentence_pending(const DimeSession* s);
int32_t     dime_session_sentence_learn(const DimeSession* s, DimeUserModel* user);
DimeBuffer* dime_session_sentence_text(const DimeSession* s);
void        dime_session_sentence_reset(DimeSession* s);
/* Addresses of session fields that CDIME binds references to (live as long as the session). */
uint32_t*   dime_session_candidate_mode_ptr(DimeSession* s);          /* CANDIDATE_MODE */
int32_t*    dime_session_candidate_with_wildcard_ptr(DimeSession* s); /* BOOL */

/* ---- Sentence buffer (CSentenceBuffer, rust/crates/dime-core/src/sentence_buffer.rs) ----
 * Pointers returned for codes / candidates stay valid until the buffer changes. */
typedef struct DimeSentenceBuffer DimeSentenceBuffer;
DimeSentenceBuffer* dime_sentence_buffer_new(void);
void     dime_sentence_buffer_free(DimeSentenceBuffer* b);
size_t   dime_sentence_buffer_count(DimeSentenceBuffer* b);
size_t   dime_sentence_buffer_cursor(DimeSentenceBuffer* b);
void     dime_sentence_buffer_move(DimeSentenceBuffer* b, uint32_t op);   /* 0 left, 1 right, 2 home, 3 end */
int32_t  dime_sentence_buffer_insert(DimeSentenceBuffer* b, const uint16_t* code, size_t code_len,
                                     const uint16_t* const* cands, const size_t* cand_lens, size_t count, int32_t pinned);
int32_t  dime_sentence_buffer_remove_before_cursor(DimeSentenceBuffer* b);
int32_t  dime_sentence_buffer_remove_at_cursor(DimeSentenceBuffer* b);
void     dime_sentence_buffer_clear(DimeSentenceBuffer* b);
size_t   dime_sentence_buffer_edit_target(DimeSentenceBuffer* b);
int32_t  dime_sentence_buffer_pin(DimeSentenceBuffer* b, size_t pos, int32_t index);
void     dime_sentence_buffer_convert(DimeSentenceBuffer* b, const DimeLexicon* lex, const DimeUserModel* user);
int32_t  dime_sentence_buffer_learn(DimeSentenceBuffer* b, DimeUserModel* user);
const uint16_t* dime_sentence_buffer_position_code(DimeSentenceBuffer* b, size_t pos, size_t* len);
size_t   dime_sentence_buffer_candidate_count(DimeSentenceBuffer* b, size_t pos);
const uint16_t* dime_sentence_buffer_candidate(DimeSentenceBuffer* b, size_t pos, size_t i, size_t* len);
int32_t  dime_sentence_buffer_pinned(DimeSentenceBuffer* b, size_t pos);
int32_t  dime_sentence_buffer_chosen(DimeSentenceBuffer* b, size_t pos);
DimeBuffer* dime_sentence_buffer_text(DimeSentenceBuffer* b);
size_t   dime_sentence_buffer_text_length(DimeSentenceBuffer* b, size_t n);
DimeBuffer* dime_sentence_buffer_display(DimeSentenceBuffer* b, const uint16_t* reading, size_t len);
size_t   dime_sentence_buffer_display_caret(DimeSentenceBuffer* b, const uint16_t* reading, size_t len);

/* ---- UI layout (rust/crates/dime-ui/src/layout.rs) ----
 * DimeRect / DimePoint have the layout of RECT / POINT. */
typedef struct DimeRect { int32_t left, top, right, bottom; } DimeRect;
typedef struct DimePoint { int32_t x, y; } DimePoint;
typedef struct DimeNotifySlot { int32_t visible, width, height; } DimeNotifySlot;  /* missing window: 0, 0, 0 */

uint32_t dime_ui_rect_in_rect(const DimeRect* limit, const DimeRect* target);                 /* RectInRect */
void     dime_ui_work_area_probe(const DimeRect* text, const DimeRect* window, DimePoint* out);  /* _GetWindowExtent */
void     dime_ui_fit_point(const DimeRect* text, const DimeRect* work, const DimeRect* window, DimePoint* out); /* CalcFitPointAroundTextExtent */
int32_t  dime_ui_stack_grow_up(int32_t has_candidate, int32_t candidate_top, int32_t notify_y,
                               const DimeNotifySlot* slots, size_t count, int32_t screen_bottom);
void     dime_ui_stack_pos_for_new_slot(DimePoint notify, const DimeNotifySlot* slots, size_t count, size_t new_slot,
                                        int32_t grow_up, DimePoint* out);
void     dime_ui_restack(int32_t has_candidate, DimeRect candidate_rect, DimePoint candidate_location, DimePoint notify,
                         const DimeNotifySlot* slots, size_t count, int32_t grow_up, DimePoint* positions, int32_t* placed);

/* ---- Candidate window / scroll bar layout (rust/crates/dime-ui/src/candwnd.rs) ---- */
typedef struct DimeCandMetrics {
    int32_t cx_line, cy_line, first_line_offset, left_padding, cx_title, padding, cy_offset;
} DimeCandMetrics;
typedef struct DimeCandRow {
    DimeRect row, highlight, number, text;
    int32_t corner_radius, number_x, text_x, text_y;
} DimeCandRow;
typedef struct DimeScrollInfo { int32_t max, page, pos; } DimeScrollInfo;  /* CScrollInfo */
typedef struct DimeScrollParts { DimeRect up, down, track, thumb; } DimeScrollParts;
typedef struct DimeScrollArrows {
    DimePoint up[3], down[3];
    int32_t pill_left, pill_right, pill_radius;
} DimeScrollArrows;

int32_t  dime_ui_mul_div(int32_t a, int32_t b, int32_t c);                                  /* MulDiv */
void     dime_ui_cand_initial_metrics(int32_t cx, int32_t cy, uint32_t wnd_width,
                                      int32_t* cx_title, int32_t* cy_row, int32_t* padding);  /* WM_CREATE */
void     dime_ui_cand_metrics(int32_t cx, int32_t cy, int32_t ave_char_width, uint32_t wnd_width, DimeCandMetrics* out);
void     dime_ui_cand_window_size(int32_t cx_title, int32_t cy_row, int32_t padding, int32_t rows, int32_t sb_width,
                                  int32_t* width, int32_t* height);
void     dime_ui_cand_scrollbar_rect(const DimeRect* client, int32_t sb_width, uint32_t dpi, int32_t corner_radius_base, DimeRect* out);
void     dime_ui_cand_row(const DimeRect* paint, int32_t index, const DimeCandMetrics* m, int32_t sb_width, uint32_t dpi, DimeCandRow* out);
int32_t  dime_ui_cand_truncate(int32_t length, int32_t* tail_offset);                       /* tail_offset -1: not truncated */
int32_t  dime_ui_cand_item_needs_measure(int32_t display_length, uint32_t wnd_width);
int32_t  dime_ui_cand_widen(int32_t display_length, int32_t measured_cx, int32_t text_left, int32_t* cx_title, uint32_t* wnd_width);
int32_t  dime_ui_cand_needs_resize(int32_t cx_title, const DimeRect* paint, int32_t sb_width);
int32_t  dime_ui_cand_hit_row(const DimeRect* client, int32_t padding, int32_t sb_width, int32_t cy_row,
                              uint32_t rows_in_page, uint32_t page_start, uint32_t count, DimePoint pt);  /* -1: none */
void     dime_ui_cand_mouse_area(const DimeRect* client, int32_t sb_width, int32_t which, DimeRect* out); /* 0 scroll column, 1 list */
void     dime_ui_cand_page_indicator(const DimeRect* client, int32_t cy_row, int32_t rows, int32_t sb_width, uint32_t dpi, DimeRect* out);
void     dime_ui_sb_parts(const DimeRect* client, uint32_t dpi, const DimeScrollInfo* si, DimeScrollParts* out);
int32_t  dime_ui_sb_hit_test(const DimeRect* client, uint32_t dpi, const DimeScrollInfo* si, DimePoint pt); /* SCROLLBAR_HITTEST */
int32_t  dime_ui_sb_idle_thumb(const DimeRect* client, uint32_t dpi, const DimeScrollInfo* si, DimeRect* out);
void     dime_ui_sb_arrows(const DimeRect* client, uint32_t dpi, DimeScrollArrows* out);
int32_t  dime_ui_sb_drag_pos(const DimeRect* client, uint32_t dpi, const DimeScrollInfo* si, int32_t start_pos, int32_t delta_y, int32_t* out);
int32_t  dime_ui_sb_clamp_pos(int32_t pos, int32_t max);
void     dime_ui_sb_attached_rect(int32_t parent_w, int32_t parent_h, int32_t sb_width, DimeRect* out);

/* ---- Candidate window drawing (rust/crates/dime-ui/src/candpaint.rs) ----
 * GDI handles are passed as void*. The same GDI calls as CCandidateWindow::_OnPaintCpp. */
typedef struct DimeCandPaintRow {
    const uint16_t* text;
    uint32_t len;
    uint16_t index_char;   /* CandIndex */
    uint16_t has_text;     /* 0: no item */
} DimeCandPaintRow;
typedef struct DimeCandColors {
    uint32_t number, number_bk, text, bk, selected_text, selected_bk;  /* COLORREF */
} DimeCandColors;
typedef struct DimeCandPaintList {
    void* font;                 /* HFONT */
    void* bk_brush;             /* HBRUSH, may be NULL */
    DimeRect paint;             /* pps->rcPaint */
    int32_t draw_list;          /* _DrawList runs */
    uint32_t wnd_width;
    int32_t sb_width;
    uint32_t dpi;
    int32_t rows_in_page;
    int32_t selected_row;       /* -1: none on this page */
    const DimeCandPaintRow* rows;
    uint32_t row_count;
    DimeCandColors colors;
    int32_t has_sb_origin;      /* non-layered scroll bar (RC26) */
    DimePoint sb_origin;        /* its origin in candidate client coordinates */
} DimeCandPaintList;
typedef struct DimeCandPaintListOut {
    void* old_font;             /* give back to dime_ui_cand_paint_end */
    int32_t drew_list;
    int32_t cx_title, cy_row, padding;
    uint32_t wnd_width;
    int32_t needs_resize;
    int32_t highlight_state;    /* 0: leave; 1: reset; 2: reset, then set highlight */
    DimeRect highlight;
    int32_t highlight_radius;
    uint32_t highlight_color;
} DimeCandPaintListOut;
typedef struct DimeCandPaintIndicator {
    void* font;
    DimeRect client;
    int32_t cy_row, rows_in_page, sb_width;
    uint32_t dpi, current_page, page_count, number_color;
} DimeCandPaintIndicator;

void dime_ui_cand_paint_list(void* dc, const DimeCandPaintList* input, DimeCandPaintListOut* out);
void dime_ui_cand_paint_indicator(void* dc, const DimeCandPaintIndicator* input);
void dime_ui_cand_paint_end(void* dc, void* old_font);

/* ---- Candidate window message handling (rust/crates/dime-ui/src/candproc.rs) ----
 * The C++ side (CCandidateWindowRustHost in src/CandidateWindow.cpp). Field order
 * must match DimeCandHostVtbl in rust/crates/dime-ffi/src/lib.rs. Callbacks must
 * not throw; invoke may destroy the window (it is always the last call). */
typedef struct DimeCandHostVtbl {
    int32_t  (*selection)(void* ctx);
    void     (*set_selection_raw)(void* ctx, uint32_t index);
    void     (*set_selection_offset)(void* ctx, int32_t offset);   /* _SetSelectionOffset */
    void     (*set_selection)(void* ctx, int32_t index);           /* _SetSelection(index, FALSE) */
    void     (*move_page)(void* ctx, int32_t offset);              /* _MovePage(offset, FALSE) */
    int32_t  (*current_page)(void* ctx, int32_t* page);            /* 0: _GetCurrentPage failed */
    uint32_t (*page_count)(void* ctx);
    uint32_t (*page_start)(void* ctx, int32_t page);
    uint32_t (*count)(void* ctx);
    int32_t  (*rows_in_page)(void* ctx);                           /* -1: no index range */
    int32_t  (*padding)(void* ctx);
    int32_t  (*cy_row)(void* ctx);
    uint32_t (*wnd_width)(void* ctx);
    void     (*set_initial_metrics)(void* ctx, int32_t cx_title, int32_t cy_row, int32_t padding);
    int32_t  (*sb_width)(void* ctx);
    void*    (*font)(void* ctx);                                   /* HFONT */
    void*    (*bk_brush)(void* ctx);                               /* HBRUSH or NULL */
    int32_t  (*has_callback)(void* ctx);
    void     (*invoke)(void* ctx, int32_t action);                 /* CANDWND_ACTION */
    void     (*paint)(void* ctx, void* dc, void* ps);              /* _OnPaint(HDC, PAINTSTRUCT*) */
    void     (*invalidate)(void* ctx);
    void     (*dpi_changed)(void* ctx, void* hwnd);
    int32_t  (*is_capture)(void* ctx);
    int32_t  (*tracking_mouse)(void* ctx);
    void     (*set_tracking_mouse)(void* ctx, int32_t on);
    int32_t  (*has_shadow)(void* ctx);
    void*    (*shadow_hwnd)(void* ctx);
    void     (*shadow_show)(void* ctx, int32_t show);
    void     (*shadow_owner_moved)(void* ctx, int32_t resized);
    void     (*delete_shadow)(void* ctx);
    int32_t  (*has_scroll_bar)(void* ctx);
    void*    (*sb_hwnd)(void* ctx);
    void     (*sb_client_rect)(void* ctx, DimeRect* out);
    int32_t  (*sb_is_enabled)(void* ctx);
    void     (*sb_show)(void* ctx, int32_t show);
    void     (*sb_owner_moved)(void* ctx, int32_t resized);
    void     (*sb_set_candidate_mouse_in)(void* ctx, int32_t inside);
    void     (*sb_on_scroll_activity)(void* ctx);
    void     (*sb_shift_position)(void* ctx, int32_t position);
    void     (*sb_mouse)(void* ctx, uint32_t msg, DimePoint pt);   /* WM_LBUTTONDOWN / UP / MOUSEMOVE */
} DimeCandHostVtbl;

typedef struct DimeCandHost {
    const DimeCandHostVtbl* vtbl;
    void* ctx;
} DimeCandHost;

/* CCandidateWindow::_WindowProcCallback (LRESULT). */
intptr_t dime_ui_cand_window_proc(const DimeCandHost* host, void* hwnd, uint32_t msg, uintptr_t wparam, intptr_t lparam);

/* ---- Scroll bar drawing (rust/crates/dime-ui/src/sbpaint.rs) ---- */
typedef struct DimeSbPaint {
    void* hwnd;                 /* the scroll bar */
    int32_t has_paint;          /* 0: no PAINTSTRUCT (no background) */
    DimeRect paint;             /* pps->rcPaint */
    DimeRect client;
    uint32_t dpi;
    DimeScrollInfo si;
    int32_t dark;               /* CConfig::GetEffectiveDarkMode() */
    int32_t full;               /* hovered or "always show scrollbars" */
    DimeRect highlight;         /* RC26 selection highlight, scroll bar coordinates */
    int32_t highlight_radius;   /* -1: none */
    uint32_t highlight_color;
} DimeSbPaint;

void dime_ui_sb_paint(void* dc, const DimeSbPaint* input);   /* CScrollBarWindow::_OnPaint */

/* ---- Scroll bar events (rust/crates/dime-ui/src/sbproc.rs) ----
 * The state stays in CScrollBarWindow; CScrollBarWindowRustHost (src/ScrollBarWindow.cpp)
 * reads and writes it. Field order must match DimeSbHostVtbl in dime-ffi. */
typedef struct DimeSbHostVtbl {
    void*    (*hwnd)(void* ctx);
    void*    (*parent_hwnd)(void* ctx);                 /* the candidate window or NULL */
    void     (*client_rect)(void* ctx, DimeRect* out);
    uint32_t (*dpi)(void* ctx);
    int32_t  (*hover_width)(void* ctx);
    void     (*scroll_info)(void* ctx, DimeScrollInfo* out);
    void     (*set_scroll_info)(void* ctx, const DimeScrollInfo* si);
    int32_t  (*dragging)(void* ctx, int32_t* start_y, int32_t* start_pos);
    void     (*set_dragging)(void* ctx, int32_t on, int32_t start_y, int32_t start_pos);
    uint8_t  (*alpha)(void* ctx);
    void     (*set_alpha)(void* ctx, uint8_t alpha);
    int32_t  (*fading)(void* ctx);
    void     (*set_fading)(void* ctx, int32_t on);
    int32_t  (*hovered)(void* ctx);
    void     (*set_hovered)(void* ctx, int32_t on);
    int32_t  (*tracking_mouse)(void* ctx);
    void     (*set_tracking_mouse)(void* ctx, int32_t on);
    int32_t  (*held_button)(void* ctx);                 /* SCROLLBAR_HITTEST */
    void     (*set_held_button)(void* ctx, int32_t button);
    int32_t  (*repeat_started)(void* ctx);
    void     (*set_repeat_started)(void* ctx, int32_t on);
    int32_t  (*is_enabled)(void* ctx);
    void     (*base_enable)(void* ctx, int32_t on);     /* CBaseWindow::_Enable */
    int32_t  (*is_visible)(void* ctx);
    void     (*base_show)(void* ctx, int32_t on);       /* CBaseWindow::_Show */
    void     (*invalidate)(void* ctx);
    int32_t  (*is_capture)(void* ctx);
    void     (*start_capture)(void* ctx);
    void     (*end_capture)(void* ctx);
    void     (*start_timer)(void* ctx, uint32_t elapse, uintptr_t id);
    void     (*end_timer)(void* ctx, uintptr_t id);
    void     (*notify_command)(void* ctx, uint32_t msg, int32_t code, int32_t pos);   /* re-enters the scroll bar */
    void     (*shift_page)(void* ctx, int32_t pages, int32_t notify);                 /* the virtual _ShiftPage */
    void     (*paint)(void* ctx, void* dc, void* ps);                                 /* _OnPaint */
} DimeSbHostVtbl;

typedef struct DimeSbHost {
    const DimeSbHostVtbl* vtbl;
    void* ctx;
} DimeSbHost;

/* op: 0 _SetCurPos(a, b)  1 _ShiftLine(a, b)  2 _ShiftPage(a, b)  3 _ShiftPosition(a, b)
 *     4 _OnScrollActivity  5 _SetCandidateMouseIn(a)  6 _OnLButtonDown(a, b)  7 _OnLButtonUp
 *     8 _OnMouseMove  9 _OnTimerID(a)  10 _OnOwnerWndMoved  11 _Show(a)  12 _Enable(a) */
void     dime_ui_sb_event(const DimeSbHost* host, uint32_t op, int32_t a, int32_t b);
void     dime_ui_sb_set_scroll_info(const DimeSbHost* host, const DimeScrollInfo* si);
intptr_t dime_ui_sb_window_proc(const DimeSbHost* host, void* hwnd, uint32_t msg, uintptr_t wparam, intptr_t lparam);

/* ---- Drop shadow (rust/crates/dime-ui/src/shadow.rs) ---- */
void dime_ui_shadow_color(int32_t dark, uint32_t sys_window_color, uint8_t* gray, uint8_t* max_alpha);
/* 0xAARRGGBB premultiplied, bitmap memory order; out has count = cx * cy entries. */
void dime_ui_shadow_pixels(int32_t cx, int32_t cy, int32_t spread, int32_t owner_radius, uint8_t gray, uint8_t max_alpha,
                           uint32_t* out, size_t count);

/* ---- Windows owned by Rust (rust/crates/dime-ui/src/win.rs, phase 3c-5) ---- */
/* Registers a window class with the Rust window procedure (popup: candidate / notify styles;
 * else the child / shadow styles with CS_IME). 0 on failure. */
uint16_t dime_ui_register_window_class(void* hinstance, const uint16_t* name, int32_t popup);

/* The shadow window owned by Rust (CRustShadowWindow). */
typedef struct DimeShadowEnv {
    uint32_t (*dpi_for_hwnd)(void* hwnd);   /* CConfig::GetDpiForHwnd */
    int32_t  (*dark)(void);                 /* CConfig::GetEffectiveDarkMode */
} DimeShadowEnv;
typedef struct DimeShadow DimeShadow;
/* CShadowWindow::_Create; NULL on failure. */
DimeShadow* dime_ui_shadow_create(void* hinstance, uint16_t atom, uint32_t ex_style, uint32_t style, void* owner,
                                  int32_t owner_corner_radius_base, const DimeShadowEnv* env);
void  dime_ui_shadow_free(DimeShadow* s);                        /* destroys the window if it still exists */
void* dime_ui_shadow_hwnd(const DimeShadow* s);
void  dime_ui_shadow_show(const DimeShadow* s, int32_t on);
void  dime_ui_shadow_owner_moved(const DimeShadow* s, int32_t resized);
void  dime_ui_shadow_setting_change(const DimeShadow* s);
void  dime_ui_shadow_state(const DimeShadow* s, uint32_t* color, DimePoint* shift, int32_t* gradient);   /* tests */

/* ---- Notify window (rust/crates/dime-ui/src/notify.rs) ---- */
typedef struct DimeNotifyPaint {
    void* font;                 /* HFONT */
    void* bk_brush;             /* HBRUSH, may be NULL */
    int32_t has_paint;          /* 0: no PAINTSTRUCT */
    DimeRect paint;             /* pps->rcPaint */
    const uint16_t* text;
    uint32_t text_len;
    uint32_t text_color, bk_color;
    int32_t cx_title;           /* before painting */
} DimeNotifyPaint;
/* CNotifyWindow::_OnPaint. Returns 1 with the new _cxTitle / _cyTitle (resize: _ResizeWindow follows)
 * when the text was measured; 0 for an empty text (the C++ original divides by zero there). */
int32_t dime_ui_notify_paint(void* dc, const DimeNotifyPaint* input, int32_t* cx_title, int32_t* cy_title, int32_t* resize);

/* The C++ side (CNotifyWindowRustHost in src/NotifyWindow.cpp); order must match dime-ffi.
 * invoke may destroy the window (it is always the last call). */
typedef struct DimeNotifyHostVtbl {
    void*           (*hwnd)(void* ctx);
    const uint16_t* (*text)(void* ctx, uint32_t* len);
    void*           (*font)(void* ctx);
    int32_t         (*notify_type)(void* ctx);                  /* NOTIFY_TYPE */
    void            (*text_metric)(void* ctx, int32_t* max_w, int32_t* ave_w, int32_t* height);
    void            (*set_title_size)(void* ctx, int32_t cx, int32_t cy);
    void            (*set_position)(void* ctx, int32_t x, int32_t y);
    void            (*set_show_times)(void* ctx, uint32_t delay_show, uint32_t time_to_hide);
    uint32_t        (*time_to_hide)(void* ctx);
    int32_t         (*corner_radius_base)(void* ctx);
    uint32_t        (*dpi)(void* ctx);
    void            (*invoke)(void* ctx, int32_t action, uintptr_t wparam, intptr_t lparam);   /* NOTIFY_WND */
    void            (*paint)(void* ctx, void* dc, void* ps);    /* _OnPaint */
    void            (*dpi_changed)(void* ctx, void* hwnd);
    void            (*base_show)(void* ctx, int32_t on);        /* CBaseWindow::_Show */
    int32_t         (*is_capture)(void* ctx);
    void            (*end_capture)(void* ctx);
    int32_t         (*is_timer)(void* ctx);
    void            (*start_timer)(void* ctx, uint32_t elapse, uintptr_t id);
    void            (*end_timer)(void* ctx, uintptr_t id);
    int32_t         (*has_shadow)(void* ctx);
    void*           (*shadow_hwnd)(void* ctx);
    void            (*shadow_show)(void* ctx, int32_t show);
    void            (*shadow_owner_moved)(void* ctx, int32_t resized);
    void            (*delete_shadow)(void* ctx);
} DimeNotifyHostVtbl;

typedef struct DimeNotifyHost {
    const DimeNotifyHostVtbl* vtbl;
    void* ctx;
} DimeNotifyHost;

void     dime_ui_notify_show(const DimeNotifyHost* host, int32_t on, uint32_t delay_show, uint32_t time_to_hide);
void     dime_ui_notify_timer(const DimeNotifyHost* host, uintptr_t id);
intptr_t dime_ui_notify_window_proc(const DimeNotifyHost* host, void* hwnd, uint32_t msg, uintptr_t wparam, intptr_t lparam);

#ifdef __cplusplus
}
#endif
