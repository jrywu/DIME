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
 * DimeHostVtbl in rust/crates/dime-ffi/src/lib.rs.
 *
 * Phase 4b-2c appends fact callbacks. When DimeHost::presenter is set, Rust
 * drives the presenter itself and calls only those; the UI slots above stay
 * for a null presenter (the C++ build). */

/* ShowNotifyText's Win32/TSF facts (host_ui::NotifyShowFacts). */
typedef struct DimeNotifyShowFacts {
    int32_t has_context;
    int32_t have_view;
    void* parent;             /* MakeNotifyWindow parent (first GetActiveView) */
    void* view_parent;        /* second GetActiveView's window; may be null while have_view is 1 */
    void* hinstance;
    uint32_t notify_class;
    uint32_t notify_shadow_class;
    uint32_t color;
    uint32_t bk;
    int32_t refresh;          /* 1 for reverse lookup, special code, beep */
    int32_t has_parent;
    int32_t rc_left, rc_top, rc_right, rc_bottom;
    int32_t caret_x, caret_y;
} DimeNotifyShowFacts;

struct DimeLayoutFacts;   /* defined with the presenter; candidate_place writes one */

/* _StartCandidateList's facts (host_ui::CandOpenFacts). go: 0 skip S_OK, 1 proceed, 2 layout failed. */
typedef struct DimeCandOpenFacts {
    int32_t go;
    int32_t hr;
    int32_t text_service;
    int32_t show_mode;
    void* parent;
    void* hinstance;
    void* range;              /* CCandidateRange* */
    uint32_t width;
    uint32_t fill;
    uint32_t number, number_bk, text, bk, selected_text, selected_bk;
    uint32_t cls_candidate, cls_shadow, cls_scroll, cls_notify, cls_notify_shadow;
} DimeCandOpenFacts;

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
    /* Phase 4b-2c, appended. See the comment above DimeNotifyShowFacts. */
    void (*notify_show_facts)(void* ctx, uint32_t kind, DimeNotifyShowFacts* out);
    void (*notify_probe)(void* ctx, uint32_t kind);
    void (*candidate_text_facts)(void* ctx, void** range, uint32_t* width, int32_t* show_mode);
    size_t (*fetched_key)(void* ctx, uint32_t index, const uint16_t** out);
    void (*apply_candidate_colors)(void* ctx, int32_t phrase);
    void (*ui_show)(void* ctx, int32_t show);
    void (*ui_update)(void* ctx, uint32_t which);   /* 0 keep, 1 selection, 2 selection|page, 3 count|string, 4 hide */
    int32_t (*ui_show_mode)(void* ctx);
    int32_t (*arrow_switches_pages)(void* ctx);
    void (*candidate_open)(void* ctx, int32_t null_range, DimeCandOpenFacts* out);
    void (*candidate_place)(void* ctx, struct DimeLayoutFacts* out);
    void (*end_candidate_list)(void* ctx);
    int32_t (*candidate_delete_begin)(void* ctx, int32_t force);   /* 1: tear the list down */
    void (*candidate_delete_mid)(void* ctx);
    void (*candidate_delete_end)(void* ctx);
    uint32_t (*fetch_associated)(void* ctx, uint16_t last_char);
    size_t (*reading_for_key)(void* ctx, const uint16_t* key, size_t len, const uint16_t** out);
    void (*notify_end_layout)(void* ctx);
} DimeHostVtbl;

/* IME state owned by Rust, kept by CDIME across edit sessions. */
typedef struct DimeSession DimeSession;

struct DimePresenter;
typedef struct DimeHost {
    const DimeHostVtbl* vtbl;
    void* ctx;
    DimeSession* session;
    struct DimePresenter* presenter;   /* phase 4b-2c; null: the UI slots above */
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
/* CKeyHandlerEditSession::DoEditSession: the key state of category (KEYSTROKE_CATEGORY) handles
 * function (KEYSTROKE_FUNCTION), as CKeyStateCategory and its subclasses (phase 4a). */
int32_t dime_session_handle_key(const DimeHost* host, uint32_t category, uint32_t function, uint32_t code, uint16_t wch);

DimeSession* dime_session_new(void);
void         dime_host_fact_sizes(size_t* notify_show, size_t* cand_open);   /* sizeof the 4b-2c fact structs */
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

/* The candidate window colors (rust/crates/dime-ui/src/candpaint.rs). */
typedef struct DimeCandColors {
    uint32_t number, number_bk, text, bk, selected_text, selected_bk;  /* COLORREF */
} DimeCandColors;

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

/* ---- The scroll bar owned by Rust (rust/crates/dime-ui/src/sbwin.rs, phase 3c-5c) ---- */
typedef struct DimeSbEnv {
    uint32_t (*dpi_for_hwnd)(void* hwnd);   /* CConfig::GetDpiForHwnd */
    int32_t  (*dark)(void);                 /* CConfig::GetEffectiveDarkMode */
    int32_t  (*always_show)(void);          /* CConfig::GetAlwaysShowScrollbars */
} DimeSbEnv;
/* The candidate window's side: CBaseWindow keeps a scroll bar's capture and timers on it,
 * and it forwards the WM_TIMERs it receives to dime_ui_scrollbar_op(10). */
typedef struct DimeSbParent {
    void* ctx;
    void* hwnd;
    int32_t (*is_capture)(void* ctx);
    void    (*start_capture)(void* ctx);
    void    (*end_capture)(void* ctx);
    void    (*start_timer)(void* ctx, uint32_t elapse, uintptr_t id);
    void    (*end_timer)(void* ctx, uintptr_t id);
} DimeSbParent;
typedef struct DimeSbSnapshot {
    DimeScrollInfo si;
    int32_t dragging, drag_start_y, drag_start_pos;
    uint32_t alpha;
    int32_t fading, hovered, tracking, held, repeat_started;
} DimeSbSnapshot;
typedef struct DimeScrollBar DimeScrollBar;
DimeScrollBar* dime_ui_scrollbar_create(void* hinstance, uint16_t atom, uint32_t ex_style, uint32_t style,
                                        const DimeSbEnv* env, const DimeSbParent* parent);   /* NULL on failure */
void    dime_ui_scrollbar_free(DimeScrollBar* s);
void*   dime_ui_scrollbar_hwnd(const DimeScrollBar* s);
int32_t dime_ui_scrollbar_hover_width(const DimeScrollBar* s);
int32_t dime_ui_scrollbar_is_enabled(const DimeScrollBar* s);
void    dime_ui_scrollbar_resize(const DimeScrollBar* s, int32_t x, int32_t y, int32_t cx, int32_t cy);
/* op: 0 _Show(a)  1 _ShiftLine(a, b)  2 _ShiftPage(a, b)  3 _ShiftPosition(a, b)  4 _SetCandidateMouseIn(a)
 *     5 _OnScrollActivity  6 _OnOwnerWndMoved  7 _OnLButtonDown(a, b)  8 _OnLButtonUp  9 _OnMouseMove
 *     10 _OnTimerID(a)  11 hovered = a (tests)  12 _SetCurPos(a, b) (tests)  13 _Enable(a) (tests) */
void    dime_ui_scrollbar_op(const DimeScrollBar* s, uint32_t op, int32_t a, int32_t b);
void    dime_ui_scrollbar_set_scroll_info(const DimeScrollBar* s, const DimeScrollInfo* si);
void    dime_ui_scrollbar_set_highlight(const DimeScrollBar* s, DimeRect rect, int32_t radius, uint32_t color);
void    dime_ui_scrollbar_highlight(const DimeScrollBar* s, DimeRect* rect, int32_t* radius, uint32_t* color);   /* tests */
void    dime_ui_scrollbar_snapshot(const DimeScrollBar* s, DimeSbSnapshot* out);                              /* tests */
void    dime_ui_scrollbar_paint(const DimeScrollBar* s, void* dc, const DimeRect* paint);                     /* tests; paint NULL: no PAINTSTRUCT */

/* ---- The candidate window owned by Rust (rust/crates/dime-ui/src/candwin.rs, phase 3c-5d) ---- */
typedef struct DimeCandEnv {
    uint32_t (*dpi_for_hwnd)(void* hwnd);           /* CConfig::GetDpiForHwnd */
    int32_t  (*dark)(void);                         /* CConfig::GetEffectiveDarkMode */
    int32_t  (*always_show)(void);                  /* CConfig::GetAlwaysShowScrollbars */
    void*    (*font)(void);                         /* Global::defaultlFontHandle */
    void     (*set_default_text_font)(void* hwnd);  /* CConfig::SetDefaultTextFont (WM_DPICHANGED) */
} DimeCandEnv;
typedef struct DimeCandClasses {
    uint16_t candidate, candidate_shadow, scroll_bar, notify, notify_shadow;   /* class atoms */
} DimeCandClasses;
/* The C++ candidate list (CCandidateListModel + the index range) and the selection callback. */
typedef struct DimeCandModel {
    void* ctx;
    int32_t         (*selection)(void* ctx);
    void            (*set_selection_raw)(void* ctx, uint32_t index);
    void            (*set_selection_offset)(void* ctx, int32_t offset);
    void            (*set_selection)(void* ctx, int32_t index);          /* without the scroll bar */
    void            (*move_page)(void* ctx, int32_t offset);             /* without the scroll bar */
    int32_t         (*current_page)(void* ctx, int32_t* page);           /* 0: GetCurrentPage failed */
    int32_t         (*paint_page)(void* ctx, uint32_t* page, uint32_t* start);   /* GetCurrentPage + AdjustPageIndex */
    uint32_t        (*page_count)(void* ctx);
    uint32_t        (*page_start)(void* ctx, int32_t page);
    uint32_t        (*count)(void* ctx);
    int32_t         (*rows_in_page)(void* ctx);                          /* -1: no index range */
    uint16_t        (*index_char)(void* ctx, int32_t row);               /* CandIndex */
    const uint16_t* (*item)(void* ctx, uint32_t index, uint32_t* len);   /* NULL: no item */
    int32_t         (*has_callback)(void* ctx);
    void            (*invoke)(void* ctx, int32_t action);                /* CANDWND_ACTION; may destroy the window */
} DimeCandModel;
typedef struct DimeCandidate DimeCandidate;
/* CCandidateWindow + _Create; NULL on failure. colors: the initial colors; fill: the initial fill. */
DimeCandidate* dime_ui_candidate_create(void* hinstance, const DimeCandClasses* classes, void* parent, uint32_t wnd_width,
                                        const DimeCandEnv* env, const DimeCandModel* model,
                                        const DimeCandColors* colors, uint32_t fill);
void    dime_ui_candidate_free(DimeCandidate* c);                 /* destroys the window if it still exists */
void*   dime_ui_candidate_hwnd(const DimeCandidate* c);
int32_t dime_ui_candidate_is_visible(const DimeCandidate* c);
int32_t dime_ui_candidate_width(const DimeCandidate* c);         /* _GetWidth */
void    dime_ui_candidate_rect(const DimeCandidate* c, int32_t client, DimeRect* out);   /* 0: window, 1: client */
void    dime_ui_candidate_window_extent(const DimeCandidate* c, const DimeRect* text, const DimeRect* window, DimePoint* out);
/* op: 0 _Show(a)  1 _Move(a, b)  2 _ResizeWindow  3 _InvalidateRect  4 _SetCandStringLength(a)  5 _SetFillColor(a)
 *     6 scroll bar _ShiftLine(a, b)  7 _ShiftPage(a, b)  8 _ShiftPosition(a, b)  9 _SetScrollInfo(nMax a, nPage b) */
void    dime_ui_candidate_op(const DimeCandidate* c, uint32_t op, int32_t a, int32_t b);
void    dime_ui_candidate_set_color(const DimeCandidate* c, int32_t which, uint32_t color, uint32_t bk);   /* 0 number, 1 text, 2 selected */
/* tests */
void    dime_ui_candidate_layout_state(const DimeCandidate* c, int32_t set, int32_t* state);   /* cxTitle, cyRow, padding, wndWidth */
void    dime_ui_candidate_paint(const DimeCandidate* c, void* dc, const DimeRect* paint);
void*   dime_ui_candidate_child(const DimeCandidate* c, int32_t which);   /* 0 scroll bar HWND, 1 shadow HWND, 2 tracking mouse (0/1) */
int32_t dime_ui_candidate_sb_state(const DimeCandidate* c, DimeSbSnapshot* snapshot, DimeRect* highlight, int32_t* radius, uint32_t* color);

/* ---- The notify window owned by Rust (rust/crates/dime-ui/src/notifywin.rs, phase 3c-5e) ---- */
typedef struct DimeNotifyClasses {
    uint16_t notify, notify_shadow;   /* class atoms registered by dime_ui_register_window_class */
} DimeNotifyClasses;
/* NOTIFYWNDCALLBACK and its object; invoke may destroy the window (always the last call). */
typedef struct DimeNotifyCallback {
    void* ctx;
    void (*invoke)(void* ctx, int32_t action, uintptr_t wparam, intptr_t lparam);   /* NOTIFY_WND */
} DimeNotifyCallback;
typedef struct DimeNotify DimeNotify;
/* CNotifyWindow + _Create (text may be NULL / 0); NULL on failure. env: the candidate window's. */
DimeNotify* dime_ui_notifywin_create(void* hinstance, const DimeNotifyClasses* classes, void* parent, int32_t notify_type,
                                           const uint16_t* text, size_t len, const DimeCandEnv* env,
                                           const DimeNotifyCallback* callback);
void    dime_ui_notifywin_free(DimeNotify* n);                /* destroys the window if it still exists */
void*   dime_ui_notifywin_hwnd(const DimeNotify* n);
int32_t dime_ui_notifywin_is_visible(const DimeNotify* n);
void    dime_ui_notifywin_rect(const DimeNotify* n, int32_t client, DimeRect* out);   /* 0: window, 1: client */
void    dime_ui_notifywin_window_extent(const DimeNotify* n, const DimeRect* text, const DimeRect* window, DimePoint* out);
void    dime_ui_notifywin_title_size(const DimeNotify* n, int32_t* cx, int32_t* cy);   /* _GetWidth / _GetHeight */
/* op: 0 _Show(a, delay b, time to hide c)  1 _Move(a, b)  2 _SetFillColor(a)  3 _InvalidateRect  4 _OnTimerID(a) */
void    dime_ui_notifywin_op(const DimeNotify* n, uint32_t op, int32_t a, int32_t b, int32_t c);
void    dime_ui_notifywin_set_text_color(const DimeNotify* n, uint32_t color, uint32_t bk);
void    dime_ui_notifywin_set_string(const DimeNotify* n, const uint16_t* text, size_t len);   /* empty: kept */
/* tests */
void    dime_ui_notifywin_state(const DimeNotify* n, int32_t* state);   /* cxTitle, cyTitle, x, y, delayShow, timeToHide */
void    dime_ui_notifywin_paint(const DimeNotify* n, void* dc, const DimeRect* paint);   /* paint NULL: no PAINTSTRUCT */
void*   dime_ui_notifywin_shadow_hwnd(const DimeNotify* n);
typedef void (*DimeNotifyTimerHook)(void* ctx, int32_t start, uint32_t elapse, uintptr_t id);
void    dime_ui_notifywin_set_timer_hook(const DimeNotify* n, DimeNotifyTimerHook hook, void* ctx);   /* NULL removes */

/* ---- CUIPresenter's state, notify-window stack and candidate window owned by Rust
 *      (rust/crates/dime-ui/src/presenterwin.rs, phase 4b-2; decisions in presenter.rs, 4b-1) ---- */
typedef struct DimePresenterState {
    DimePoint cand_location;      /* _candLocation */
    DimePoint notify_location;    /* _notifyLocation */
    DimeRect  rect_comp_range;    /* _rectCompRange */
} DimePresenterState;
typedef struct DimeLayoutFacts {
    DimeRect comp;          /* *lpRect */
    int32_t first_call;
    int32_t has_parent;     /* the context view's window, else the foreground one */
    DimePoint caret;        /* GetCaretPos on the screen (with a parent) */
} DimeLayoutFacts;
typedef struct DimeShowFacts {
    int32_t refresh;        /* reverse lookup, special code, beep */
    int32_t has_parent;
    DimeRect rc_caret;      /* GUITHREADINFO::rcCaret as returned (zero if GetGUIThreadInfo failed) */
    DimePoint gui_caret;    /* (rcCaret.left, rcCaret.bottom) on the screen */
} DimeShowFacts;
/* The candidate window's C++ side: its callback (CANDWND_ACTION 0 select / 1 cancel; may destroy the
 * window and the list) and the selection keys, a CCandidateRange* read live (count -1 for null). */
typedef struct DimePresenterCandSource {
    void* ctx;
    void (*invoke)(void* ctx, int32_t action);
    int32_t (*range_count)(const void* range);
    uint16_t (*range_char)(const void* range, int32_t row);
} DimePresenterCandSource;
typedef struct DimeCandItem {
    const uint16_t* text;
    size_t text_len;
    const uint16_t* key_code;
    size_t key_code_len;
} DimeCandItem;
typedef struct DimePresenter DimePresenter;
/* callback: every notify window's actions (_NotifyWndCallback); env as the candidate window's. */
DimePresenter* dime_ui_presenter_create(const DimeCandEnv* env, const DimeNotifyCallback* callback, const DimePresenterCandSource* source);
void    dime_ui_presenter_free(DimePresenter* p);
void    dime_ui_presenter_get_state(const DimePresenter* p, DimePresenterState* out);
void    dime_ui_presenter_set_state(const DimePresenter* p, const DimePresenterState* st);   /* tests */
void    dime_ui_presenter_reset_comp_range(const DimePresenter* p);
/* The notify stack. MakeNotifyWindow after the parent is known: 1, or 0 for a slot out of range. */
int32_t dime_ui_presenter_notify_make(const DimePresenter* p, uint32_t slot, void* hinstance, const DimeNotifyClasses* classes,
                                      void* parent, const uint16_t* text, size_t len);
void    dime_ui_presenter_notify_dispose(const DimePresenter* p, uint32_t slot);   /* slot >= NOTIFY_COUNT: all */
void    dime_ui_presenter_notify_set_string(const DimePresenter* p, uint32_t slot, const uint16_t* text, size_t len);
void    dime_ui_presenter_notify_show(const DimePresenter* p, uint32_t slot, int32_t show, uint32_t delay_show, uint32_t time_to_hide);
void    dime_ui_presenter_notify_set_text_color(const DimePresenter* p, uint32_t color, uint32_t bk);
/* slot < NOTIFY_COUNT: what 0 exists, 1 visible; slot >= NOTIFY_COUNT: 0 any exists, 1 any visible, 2 a hint visible. */
int32_t dime_ui_presenter_notify_query(const DimePresenter* p, uint32_t slot, uint32_t what);
void*   dime_ui_presenter_notify_window(const DimePresenter* p, uint32_t slot, int32_t* cx, int32_t* cy);   /* HWND, title size */
void    dime_ui_presenter_notify_paint(const DimePresenter* p, uint32_t slot, void* dc, const DimeRect* paint);   /* tests */
/* The candidate window and list. MakeCandidateWindow after the parent is known: 1, or 0 when there is one. */
int32_t dime_ui_presenter_cand_make(const DimePresenter* p, void* hinstance, const DimeCandClasses* classes, void* parent,
                                    uint32_t wnd_width, const void* range, const DimeCandColors* colors, uint32_t fill);
void    dime_ui_presenter_cand_dispose(const DimePresenter* p);
/* what: 0 exists, 1 visible, 2 _GetWidth, 3 _GetCount, 4 _GetSelection (-1 without one). */
int32_t dime_ui_presenter_cand_query(const DimePresenter* p, uint32_t what);
void*   dime_ui_presenter_cand_hwnd(const DimePresenter* p);
void    dime_ui_presenter_cand_rect(const DimePresenter* p, int32_t client, DimeRect* out);   /* untouched without a window */
/* op: 0 _Show(a)  1 _Move(a, b)  2 _ResizeWindow  3 _InvalidateRect  4 _SetCandStringLength(a)  5 _SetFillColor(a)
 *     9 _SetScrollInfo(a, b) */
void    dime_ui_presenter_cand_op(const DimePresenter* p, uint32_t op, int32_t a, int32_t b);
void    dime_ui_presenter_cand_set_color(const DimePresenter* p, int32_t which, uint32_t color, uint32_t bk);   /* 0 number, 1 text, 2 selected */
void    dime_ui_presenter_cand_add_items(const DimePresenter* p, const DimeCandItem* items, size_t count, int32_t with_key_code);
void    dime_ui_presenter_cand_clear(const DimePresenter* p);
void    dime_ui_presenter_cand_set_index_range(const DimePresenter* p, const void* range);
void    dime_ui_presenter_cand_set_page_index(const DimePresenter* p, const uint32_t* pages, uint32_t count);
int32_t dime_ui_presenter_cand_get_page_index(const DimePresenter* p, uint32_t* out, uint32_t size, uint32_t* page_count);   /* HRESULT */
int32_t dime_ui_presenter_cand_current_page(const DimePresenter* p, uint32_t* page);   /* HRESULT */
/* op: 0 _MoveSelection(a, notify)  1 _SetSelection(a, notify)  2 _MovePage(a, notify)  3 _SetSelectionInPage(a)
 *     4 _SetSelection(a) raw. Returns the BOOL result. */
int32_t dime_ui_presenter_cand_select(const DimePresenter* p, uint32_t op, int32_t a, int32_t notify);
/* which: 0 item index, 1 the selected item, 2 its key code. Length; *text valid until the list changes. */
uint32_t dime_ui_presenter_cand_string(const DimePresenter* p, uint32_t which, int32_t index, const uint16_t** text);
void    dime_ui_presenter_cand_paint(const DimePresenter* p, void* dc, const DimeRect* paint);   /* tests */
int32_t dime_ui_presenter_cand_sb_info(const DimePresenter* p, int32_t* out);   /* tests: nMax, nPage, nPos; 0 without one */
/* Placement. */
void    dime_ui_presenter_layout_change(const DimePresenter* p, const DimeLayoutFacts* facts);
int32_t dime_ui_presenter_notify_location_for_show(const DimePresenter* p, const DimeShowFacts* facts);   /* caretUpdated */
void    dime_ui_presenter_restack_for_show(const DimePresenter* p, int32_t caret_updated);
void    dime_ui_presenter_restack_visible_slots(const DimePresenter* p, int32_t grow_up);

/* ---- Drop shadow (rust/crates/dime-ui/src/shadow.rs) ---- */
void dime_ui_shadow_color(int32_t dark, uint32_t sys_window_color, uint8_t* gray, uint8_t* max_alpha);
/* 0xAARRGGBB premultiplied, bitmap memory order; out has count = cx * cy entries. */
void dime_ui_shadow_pixels(int32_t cx, int32_t cy, int32_t spread, int32_t owner_radius, uint8_t gray, uint8_t max_alpha,
                           uint32_t* out, size_t count);

/* ---- Windows owned by Rust (rust/crates/dime-ui/src/win.rs, phase 3c-5) ---- */
/* Registers a window class with the Rust window procedure (popup: candidate / notify styles;
 * else the child / shadow styles with CS_IME). 0 on failure. */
uint16_t dime_ui_register_window_class(void* hinstance, const uint16_t* name, int32_t popup);
/* CConfig::SetSetThreadDpiAwarenessContext: Rust-owned windows switch the thread DPI context with it
 * (per-monitor v2, as CBaseWindow::_Create) only when it is set (DllMain). */
void dime_ui_set_thread_dpi_context_function(void* fn);

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

/* ---- Engine setup (phase 4c, rust/crates/dime-core/src/engine_setup.rs) ----
 * Rust chooses which table to copy, open, reload, or alias. The host performs
 * filesystem and CFile / engine operations. Callback order is the vtable order.
 * id is kind * 16 + slot (kind: 0 main, 1 custom, 2 phrase, 3 ext-b, 4 ext-cd,
 * 5 ext-e, 6 array-phrase, 7 special, 8 short, 9 tcsc, 10 tcfreq).
 */
typedef struct DimeSetupHost DimeSetupHost;
typedef struct DimeSetupHostVtbl {
    int    (*path_exists)(DimeSetupHost*, const uint16_t* path, size_t len);
    /* 0 when _wstat fails (the caller's timestamp stays). 1 and *mtime set on success. */
    int    (*file_mtime)(DimeSetupHost*, const uint16_t* path, size_t len, int64_t* mtime);
    void   (*copy_file)(DimeSetupHost*, const uint16_t* src, size_t slen, const uint16_t* dst, size_t dlen, int fail_if_exists);
    void   (*create_dir)(DimeSetupHost*, const uint16_t* path, size_t len);
    int    (*path_equal)(DimeSetupHost*, uint32_t locale, const uint16_t* a, size_t alen, const uint16_t* b, size_t blen);
    int    (*has_file)(DimeSetupHost*, int32_t id);
    int    (*has_engine)(DimeSetupHost*, int32_t id);
    int    (*dict_type)(DimeSetupHost*, int32_t id);   /* -1 when there is no engine */
    int    (*file_updated)(DimeSetupHost*, int32_t id);
    size_t (*file_name)(DimeSetupHost*, int32_t id, uint16_t* buf, size_t cap);   /* units written */
    void   (*drop_file)(DimeSetupHost*, int32_t id);
    void   (*drop_engine)(DimeSetupHost*, int32_t id);
    int    (*new_file)(DimeSetupHost*, int32_t id);   /* leaks a previous CFile, as C++ does */
    int    (*create_file)(DimeSetupHost*, int32_t id, const uint16_t* path, size_t len, uint32_t share_mode);
    int    (*create_engine)(DimeSetupHost*, int32_t id, uint32_t locale, int32_t dict_type);   /* does not delete the previous */
    void   (*parse_config)(DimeSetupHost*, int32_t id, int32_t mode);   /* no-op when the engine is null */
    void   (*alias_phrase_to_main)(DimeSetupHost*, int32_t slot);
    void   (*setup_big5)(DimeSetupHost*, int32_t mode, uint32_t locale);
    void   (*setup_keystroke)(DimeSetupHost*, int32_t mode);             /* public SetupKeystroke */
    void   (*load_config)(DimeSetupHost*, int32_t mode);
    void   (*setup_configuration)(DimeSetupHost*, int32_t mode);
    void   (*setup_candidate_range)(DimeSetupHost*, int32_t mode);       /* public SetupCandidateListRange */
    int    (*has_text_service)(DimeSetupHost*);
    int    (*array_scope)(DimeSetupHost*);
    int    (*active_mode)(DimeSetupHost*);
    int    (*do_han_convert)(DimeSetupHost*);
} DimeSetupHostVtbl;
struct DimeSetupHost { const DimeSetupHostVtbl* vtbl; void* ctx; };

/* 1 / 0. settings_build skips SetupCandidateListRange on a reload (DIMESettings). */
int dime_setup_dictionary(DimeSetupHost* host, int32_t mode, uint32_t locale, int32_t settings_build,
                          const uint16_t* app, size_t app_len, const uint16_t* pf, size_t pf_len);
int dime_setup_han_convert(DimeSetupHost* host, uint32_t locale,
                           const uint16_t* app, size_t app_len, const uint16_t* pf, size_t pf_len);
int dime_setup_tc_freq(DimeSetupHost* host, uint32_t locale,
                       const uint16_t* app, size_t app_len, const uint16_t* pf, size_t pf_len);

/* Natural alignment, 20 bytes. function is KEYSTROKE_FUNCTION. */
typedef struct DimeKeystrokeOut {
    uint32_t index;
    uint16_t printable;
    uint16_t cand_index;
    uint32_t virtual_key;
    uint32_t modifiers;
    uint32_t function;
} DimeKeystrokeOut;

/* Writes min(97, cap) rows. Returns the full row count (97). *inserted_equal is 1 when Dayi added '='. */
size_t dime_build_keystrokes(int32_t mode, const uint16_t* radicals, size_t radical_count,
                             const uint16_t* endkey, size_t endkey_len,
                             DimeKeystrokeOut* out, size_t cap, int32_t* inserted_equal);
/* *page_size is the C++ page size (the dangling else is preserved). Returns entries written. */
size_t dime_build_candidate_range(int32_t mode, int32_t global_mode, int32_t dayi_selkey_mode,
                                  int32_t space_as_first, int32_t array_scope,
                                  const uint16_t* selkey, size_t selkey_len,
                                  DimeKeystrokeOut* cand, DimeKeystrokeOut* phrase, size_t cap,
                                  uint32_t* page_size);

/* desc_len 0: the C++ resource string (ids 0 and 1). Otherwise a literal, not NUL-terminated. */
typedef struct DimePreservedKey {
    uint32_t id;
    uint32_t vkey;
    uint32_t modifiers;
    uint32_t desc_len;
} DimePreservedKey;
int dime_preserved_key_count(void);
int dime_preserved_key_at(int32_t index, DimePreservedKey* out, uint16_t* desc, size_t desc_cap);

/* ---- Language bar (phase 4d, rust/crates/dime-core/src/langbar.rs) ----
 * Rust decides when to sync a compartment, switch shape, or show text.
 * The host reads and writes compartments and runs the side effect immediately,
 * so a sink can re-enter before the callback returns.
 * Bool ids: 0 double/single, 1 keyboard open, 2 IME mode.
 * Keyboard which: 0 OPENCLOSE, 1 IME mode, anything else neither.
 * Preserved ids: 0 IME, 1 width, 2 config, 3 han; anything else is eaten.
 * Show kinds: 0 Chinese/English (desktop-notify gate), 1 full/half, 2 han.
 * get_* returns 1 and writes *out on SUCCEEDED (including S_FALSE). 0 is a failed HRESULT.
 * dime_langbar_preserved returns 1 when the key is eaten.
 */
typedef struct DimeLangHost DimeLangHost;
typedef struct DimeLangHostVtbl {
    int      (*has_conversion)(DimeLangHost*);
    int      (*get_conversion)(DimeLangHost*, uint32_t* out);
    void     (*set_conversion)(DimeLangHost*, uint32_t mode);
    int      (*get_bool)(DimeLangHost*, int32_t id, int32_t* out);
    void     (*set_bool)(DimeLangHost*, int32_t id, int32_t value);
    int      (*windows8)(DimeLangHost*);
    void     (*set_chinese)(DimeLangHost*, int32_t on);
    void     (*set_full_shape)(DimeLangHost*, int32_t on);
    void     (*end_composition_if_any)(DimeLangHost*);
    void     (*delete_candidate)(DimeLangHost*, int32_t force);
    int      (*width_mode)(DimeLangHost*);
    void     (*set_width_mode)(DimeLangHost*, int32_t mode);
    int      (*notify_desktop)(DimeLangHost*);
    void     (*show_text)(DimeLangHost*, const uint16_t* text, size_t len, uint32_t delay);
    int      (*shift_mode)(DimeLangHost*);
    int      (*shift_key_only)(DimeLangHost*);
    uint32_t (*modifiers)(DimeLangHost*);
    int      (*do_han)(DimeLangHost*);
    void     (*set_do_han)(DimeLangHost*, int32_t on);
    void     (*write_config)(DimeLangHost*);
    void     (*setup_han)(DimeLangHost*);
    void     (*inject_space)(DimeLangHost*);
    void     (*show_config)(DimeLangHost*);
    int      (*has_text_service)(DimeLangHost*);
} DimeLangHostVtbl;
struct DimeLangHost { const DimeLangHostVtbl* vtbl; void* ctx; };

void dime_langbar_conversion(DimeLangHost* host, int has_override, int override_open);
void dime_langbar_private(DimeLangHost* host);
void dime_langbar_keyboard(DimeLangHost* host, int32_t which);
int  dime_langbar_preserved(DimeLangHost* host, int32_t id);
void dime_langbar_keyboard_side(DimeLangHost* host, int open);
void dime_langbar_shape(DimeLangHost* host, int full);
void dime_langbar_show(DimeLangHost* host, int32_t kind, int on, uint32_t delay);

/* ---- CLI (phase 5a, dime-config::cli) ----------------------------------
 * dime_cli_parse fills DimeCliArgs (fixed buffers are NUL-terminated; strings
 * are already truncated like StringCchCopyW). Returns 1, or 0 and an error
 * buffer (trailing newline included; free with dime_buffer_free).
 * dime_cli_run is RunCLI. The host performs file and CConfig side effects.
 * Vtable order is frozen: dime-ffi/src/cli.rs and CLI.cpp kCliVtbl.
 * array_table is -1 when the command is not --load-array.
 * main_dest / array_dest return 0 when the mode or table is not valid.
 * write_utf16_bom: 0 ok, 1 open failed (*gle = GetLastError), 2 write failed.
 * copy_file / parse_cin / file_exists / load_text: 1 success, 0 failure.
 * set_value: 0 ok, 1 rejected, 2 failure. *error is optional.
 * validate_line: 0 valid, 1 format, 2 key too long, 3 invalid char.
 */
typedef struct DimeCliArgs {
    int32_t command;
    int32_t mode;
    int32_t has_mode;
    int32_t json_output;
    int32_t silent;
    int32_t no_validate;
    uint16_t get_key[64];
    uint16_t set_keys[32][64];
    uint16_t set_values[32][256];
    int32_t set_count;
    uint16_t file_path[260];
    int32_t array_table;
} DimeCliArgs;

int32_t dime_cli_parse(const uint16_t* const* argv, const size_t* lens, size_t argc,
                       DimeCliArgs* out, DimeBuffer** error);

typedef struct DimeCliHost DimeCliHost;
typedef struct DimeCliHostVtbl {
    void     (*write_out)(DimeCliHost*, const uint16_t*, size_t);
    void     (*write_err)(DimeCliHost*, const uint16_t*, size_t);
    void     (*load_config)(DimeCliHost*, int32_t mode);
    void     (*write_config)(DimeCliHost*, int32_t mode);
    void     (*reset_defaults)(DimeCliHost*, int32_t mode);
    void     (*get_value)(DimeCliHost*, int32_t index, DimeBuffer** out);
    int32_t  (*set_value)(DimeCliHost*, int32_t index, const uint16_t* value, size_t len, DimeBuffer** error);
    int32_t  (*file_exists)(DimeCliHost*, const uint16_t*, size_t);
    int32_t  (*load_text)(DimeCliHost*, const uint16_t*, size_t, uint16_t** out, size_t* out_len);
    void     (*free_text)(DimeCliHost*, uint16_t*);
    uint32_t (*begin_validate)(DimeCliHost*, int32_t mode);
    int32_t  (*validate_line)(DimeCliHost*, const uint16_t*, size_t, int32_t mode, uint32_t max_codes);
    void     (*end_validate)(DimeCliHost*);
    size_t   (*custom_txt_path)(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap);
    size_t   (*custom_cin_path)(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap);
    int32_t  (*write_utf16_bom)(DimeCliHost*, const uint16_t*, size_t, const uint16_t*, size_t, uint32_t* gle);
    int32_t  (*parse_cin)(DimeCliHost*, const uint16_t*, size_t, const uint16_t*, size_t, int32_t custom);
    size_t   (*export_src_path)(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap);
    int32_t  (*copy_file)(DimeCliHost*, const uint16_t*, size_t, const uint16_t*, size_t, uint32_t* gle);
    size_t   (*main_dest)(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap);
    size_t   (*phrase_dest)(DimeCliHost*, uint16_t* out, size_t cap);
    size_t   (*array_dest)(DimeCliHost*, int32_t table, uint16_t* out, size_t cap);
} DimeCliHostVtbl;
struct DimeCliHost { const DimeCliHostVtbl* vtbl; void* ctx; };

int32_t dime_cli_run(const uint16_t* const* argv, const size_t* lens, size_t argc, DimeCliHost* host);

/* ---- Settings model (phase 5b-1, dime-config::settings_model) ----------
 * Window-free SettingsModel functions. snapshot is a C++ SettingsSnapshot
 * (216 bytes). Returned strings are static NUL-terminated UTF-16, or NULL.
 * string_to_ime_mode: NULL or an unknown name is IME_MODE_NONE (4).
 * parse_max_codes: 1 and *out_val written when the text is in 1..=20;
 *   0 otherwise (NULL text does not write). validate_line: 1 when the line
 *   is valid. char_ok NULL selects the no-engine fallback. out receives up
 *   to cap entries; *out_count is how many were written. Codes: 1 format,
 *   2 key too long, 3 invalid char.
 */
int32_t dime_settings_mode_bitmask(int32_t mode);
int32_t dime_settings_sidebar_hit_test(int32_t y, int32_t item_height, int32_t item_count);
int32_t dime_settings_scroll_range(int32_t content_height, int32_t viewport_height);
int32_t dime_settings_clamp_scroll(int32_t pos, int32_t max_scroll);
const uint16_t* dime_settings_ime_mode_to_string(int32_t mode);
int32_t dime_settings_string_to_ime_mode(const uint16_t* text);
int32_t dime_settings_index_to_mode(int32_t index);
int32_t dime_settings_mode_to_index(int32_t mode);
void dime_settings_sync_toggle(void* snapshot, int32_t id, int32_t is_on);
void dime_settings_sync_combo(void* snapshot, int32_t id, int32_t sel);
int32_t dime_settings_parse_max_codes(const uint16_t* text, uint32_t* out_val);
const uint16_t* dime_settings_load_table_suffix(int32_t id);

typedef struct DimeLineError {
    int32_t code;
    int32_t start;
    int32_t len;
} DimeLineError;

int32_t dime_settings_validate_line(
    const uint16_t* line, size_t len, int32_t mode, uint32_t max_codes,
    int32_t (*char_ok)(uint16_t ch, void* ctx), void* ctx,
    DimeLineError* out, size_t cap, size_t* out_count);

/* ---- Settings page layout (phase 5b-2, dime-config::page_layout) -------
 * Card, row, and sidebar tables. The bytes match C++ LayoutCard, LayoutRow,
 * and SidebarItemDef (x64 sizes 48/48/16, Win32 sizes 28/36/8). String
 * pointers are static NUL-terminated UTF-16, or NULL. The tables live for
 * the process lifetime. A null count pointer is not written. A panic
 * returns NULL and leaves count unchanged.
 */
const void* dime_settings_ime_page_layout(int32_t* count);
const void* dime_settings_load_table_page_layout(int32_t* count);
const void* dime_settings_sidebar_items(int32_t* count);

/* ---- Settings geometry (phase 5b-3, dime-config::geometry) -------------
 * Defaults match SettingsGeometry (260 bytes, 65 ints). ThemeColors is 88
 * bytes, 22 COLORREFs. out is not written when null. sys null is all zeros.
 * blend_color: alpha <= 0 returns c1, alpha >= 255 returns c2, otherwise
 * each channel (c1 * (255 - alpha) + c2 * alpha) / 255 and the high byte
 * is cleared. scale_for_dpi: dpi <= 0 returns value; otherwise Win32
 * MulDiv(value, dpi, 96), including the x86 / x64 i32::MIN difference.
 * A panic returns 0 from blend and scale, and leaves out unchanged.
 */
typedef struct DimeSysColors {
    uint32_t hotlight;
    uint32_t face;
    uint32_t window;
    uint32_t window_text;
    uint32_t gray_text;
    uint32_t highlight_text;
} DimeSysColors;

void dime_settings_geometry(void* out);
void dime_settings_theme_colors(int32_t is_dark, const DimeSysColors* sys, void* out);
uint32_t dime_settings_blend_color(uint32_t c1, uint32_t c2, int32_t alpha);
int32_t dime_settings_scale_for_dpi(int32_t value, int32_t dpi);

/* ---- Settings snapshot bridge (phase 5b-4) ------------------------------
 * Load copies the plain CConfig fields into SettingsSnapshot (216 bytes).
 * colors is six COLORREFs from the live colors[] array, in order: item,
 * selected, item background, phrase, number, selected background. Stored
 * palettes are not read. A null colors pointer is six zeros.
 * reverseConversionIndex is always 0. Apply writes those fields back except
 * the six colors. Palettes, reverse conversion, and the reload flags stay.
 * The font face is truncated like StringCchCopy into LF_FACESIZE (32).
 * A null out or snap is not written. A panic leaves out unchanged and does
 * not apply.
 */
void dime_settings_load_snapshot(void* out, const uint32_t* colors);
void dime_settings_apply_snapshot(const void* snap);

/* ---- Text file decode (phase 5b-5) --------------------------------------
 * Decode the bytes LoadTextFileAsUtf16 has already read. Opening the file,
 * the empty-file WCHAR[1], and the output allocation stay in C++.
 * FF FE copies little-endian code units and drops a trailing odd byte.
 * FE FF keeps the C++ double byte-swap, so the payload is stored as
 * little-endian units. EF BB BF is stripped. Other bytes try strict UTF-8
 * (CP_UTF8, MB_ERR_INVALID_CHARS) and then CP_ACP with flags 0.
 * decode_text_len writes the code-unit count with no NUL and returns 1.
 * decode_text writes exactly cap units, writes no NUL, and returns 1 only
 * when the decoded length is cap. A null out_wlen or out is not written.
 * A null bytes pointer with a non-zero length returns 0. A panic returns 0
 * and does not write. A byte length above INT_MAX fails the code-page
 * calls instead of passing a negative MultiByteToWideChar length.
 */
int32_t dime_settings_decode_text_len(const uint8_t* bytes, uint32_t len, uint32_t* out_wlen);
int32_t dime_settings_decode_text(const uint8_t* bytes, uint32_t len, uint16_t* out, uint32_t cap);

/* ---- Settings window computations (phase 5b-6, dime-settings) ----------
 * Window-free pieces of SettingsWindow (SettingsWindowLayout.h). Cards and
 * rows are C++ LayoutCard / LayoutRow. Struct pointers are the
 * SettingsWindowLayout structs: RowContext (24 bytes), PlanInput (48, the
 * 16 cardExpanded bools as bytes at offset 32), Placement (20), PlanResult
 * (12), CtrlRect (20), CardListClick (12), RowSpan (12), RowCardHit (8),
 * MsgBoxButtons (28), MsgBoxLayout (64). `expanded` is 16 bools or NULL
 * (all collapsed). dpi is ScaleForDpi(int, UINT): MulDiv(v, dpi, 96).
 * List functions write at most cap entries and the full count to
 * out_count; call once with cap 0 to size. Null out-pointers are not
 * written. A panic writes nothing and returns 0 (-1 for hit tests).
 */
int32_t dime_settings_card_width(int32_t client_right, uint32_t dpi, int32_t sidebar_takes_space, int32_t vscroll_w);
int32_t dime_settings_ctrl_right(int32_t margin, int32_t card_w, uint32_t dpi, int32_t has_chevron);
int32_t dime_settings_row_height(const void* row, uint32_t dpi, int32_t preview_h);
int32_t dime_settings_row_shown(const void* row, int32_t mode_mask, int32_t color_mode, int32_t grid_expanded);
int32_t dime_settings_card_list_hit_test(int32_t y, const void* cards, int32_t count, int32_t mode_mask,
    const uint8_t* expanded, int32_t expand_base, uint32_t dpi, int32_t scroll_pos);
void dime_settings_card_list_click(int32_t y, const void* cards, int32_t count, int32_t mode_mask,
    const uint8_t* expanded, int32_t expand_base, uint32_t dpi, int32_t scroll_pos, void* out);
void dime_settings_row_spans(const void* card, const void* ctx, void* out, size_t cap, size_t* out_count);
void dime_settings_row_cards_hit(int32_t y, const void* card, const void* ctx, void* out);
void dime_settings_plan_controls(const void* input, const void* ime, int32_t ime_count,
    const void* load, int32_t load_count, void* out, size_t cap, size_t* out_count, void* result);
int32_t dime_settings_row_control_rect(int32_t row_type, int32_t ctrl_right, int32_t cy, int32_t ctrl_h,
    int32_t btn_h, uint32_t dpi, void* out); /* 0 = row type has no control */
void dime_settings_swatch_rect(int32_t index, int32_t margin, int32_t cur_y, int32_t ctrl_right, uint32_t dpi, void* out);
void dime_settings_rich_editor_rects(int32_t margin, int32_t card_w, int32_t pad, int32_t cur_y, int32_t this_h,
    int32_t ctrl_right, int32_t btn_w, int32_t btn_h, uint32_t dpi, void* out_editor, void* out_save);
void dime_settings_content_area_rect(int32_t client_w, int32_t client_h, uint32_t dpi,
    int32_t sidebar_takes_space, int32_t has_breadcrumb, void* out);
/* ValidateCustomTableBuffer: returns the invalid line count. first_err_line
 * and summary[0] are cleared first; one line per error, for the first
 * max_report errors, is appended like StringCchCatW (truncated, NUL
 * terminated): two spaces, U+7B2C, the 1-based line number, U+884C, LF. */
int32_t dime_settings_validate_buffer(const uint16_t* text, size_t len, int32_t mode, uint32_t max_codes,
    int32_t (*char_ok)(uint16_t ch, void* ctx), void* ctx, int32_t max_report,
    int32_t* first_err_line, uint16_t* summary, size_t summary_cch);
/* ThemedMessageBox. MB_* bit tests in C++ order: MB_OKCANCEL shows Yes/No/Cancel
 * and every MB_ICON in 0x30 shows the warning icon. key_result returns -1
 * for keys it does not handle; close_result -1 keeps the current result. */
void dime_settings_msgbox_buttons(uint32_t mb_type, void* out);
int32_t dime_settings_msgbox_icon(uint32_t mb_type);
int32_t dime_settings_msgbox_max_text_width(uint32_t dpi);
void dime_settings_msgbox_layout(int32_t has_icon, int32_t btn_count, int32_t text_w, int32_t text_h, uint32_t dpi, void* out);
int32_t dime_settings_msgbox_first_button_x(const void* layout, int32_t btn_count);
int32_t dime_settings_msgbox_button_at(const void* layout, int32_t btn_count, int32_t x, int32_t y);
int32_t dime_settings_msgbox_key_result(const void* buttons, int32_t default_btn, uint32_t vk);
int32_t dime_settings_msgbox_close_result(const void* buttons);

/* ---- Settings window painting (phase 5b-7, dime-settings::paint) -------
 * Call-by-call ports of SettingsWindowPaint::Cpp. Handles (HDC, HFONT,
 * HBRUSH, HICON) are passed as void*. ctx is a SettingsWindowPaint::
 * PaintContext (x64 520 bytes, Win32 484); fonts is SettingsWindowPaint::
 * Fonts (56 / 28). rc is a RECT, tc a ThemeColors (88 bytes), snap a
 * SettingsSnapshot (216), layout a SettingsWindowLayout::MsgBoxLayout (64).
 * Strings are NUL terminated. GDI+ must already be started. A null
 * required pointer paints nothing. A panic paints nothing more and returns 0.
 */
void dime_settings_create_fonts(uint32_t dpi, void* fonts);
void dime_settings_draw_card(void* hdc, int32_t x, int32_t y, int32_t w, int32_t h, int32_t corner_r,
    uint32_t card_bg, uint32_t card_border, uint32_t content_bg, int32_t is_hover, uint32_t hover_color);
void dime_settings_paint_toggle(void* hdc, const void* rc, int32_t is_on, int32_t is_hover, const void* tc);
int32_t dime_settings_preview_height(void* hdc, const void* snap, uint32_t dpi);
void dime_settings_paint_preview(void* hdc, const void* snap, int32_t margin, int32_t cur_y,
    int32_t card_w, int32_t total_h, uint32_t dpi, uint32_t sys_face);
void dime_settings_paint_sidebar(void* hdc, const void* ctx);
void dime_settings_paint_content(void* hdc, const void* ctx);
void dime_settings_paint_breadcrumb(void* hdc, const void* ctx);
void dime_settings_paint_hamburger(void* hdc, const void* ctx, const void* rc, void* bg_brush);
void dime_settings_paint_button(void* hdc, const void* rc, const uint16_t* text, int32_t pressed,
    int32_t disabled, const void* tc, void* font, uint32_t dpi);
void dime_settings_paint_swatch(void* hdc, const void* rc, uint32_t color, uint32_t border, int32_t radius);
void dime_settings_paint_msgbox(void* hdc, const void* layout, const uint16_t* text,
    const uint16_t* const* labels, int32_t btn_count, int32_t hover_btn, int32_t default_btn,
    const void* tc, void* font, void* icon, uint32_t dpi);

/* ---- Settings message box window (phase 5b-8a, dime-settings::msgboxwin)
 * SettingsMsgBox::Cpp::Show in Rust: registers class_name once per process,
 * runs the modal box, and returns IDOK / IDYES / IDNO / IDCANCEL, or the
 * MessageBoxW result when the window cannot be created. Strings are NUL
 * terminated. tc is a ThemeColors (88 bytes). dpi_for is the host's
 * CConfig::GetDpiForHwnd. A null tc or dpi_for, or a panic, returns IDCANCEL.
 */
int32_t dime_settings_message_box(void* parent, const uint16_t* text, const uint16_t* caption,
    uint32_t mb_type, const uint16_t* class_name, int32_t is_dark, const void* tc,
    uint32_t (*dpi_for)(void* hwnd));

/* ---- Settings navigation reducer (phase 5b-8b, dime-settings::nav) -------
 * SettingsNav::Cpp::Handle in Rust. state is a SettingsNav::NavState (64
 * bytes, the 16 cardExpanded bools at offset 48, written back as 0 / 1),
 * ev a NavEvent (36), out NavEffect[cap] (32 bytes each). The effects are
 * written in order, at most cap of them, with the total in out_count.
 * min_track_size writes a POINT. Null pointers are not written.
 */
void dime_settings_nav_handle(void* state, const void* ev, void* out, size_t cap, size_t* out_count);
void dime_settings_nav_min_track_size(uint32_t screen_dpi, void* out_point);

/* ---- Settings WM_COMMAND (phase 5b-8c, dime-settings::command) ---------
 * SettingsCommand::Cpp::Handle in Rust, run against a host vtable. Field
 * order must match SettingsCommand.cpp (kVtbl) and dime-ffi's CmdHostVtbl.
 * Strings are (pointer, length) UTF-16 without NULs, except the message box
 * caption, which is NUL terminated. A returned string stays valid until the
 * host's next call that returns a string. logfont is a LOGFONTW (92 bytes),
 * snapshot a SettingsSnapshot (216), ev a SettingsCommand::CommandEvent (52).
 * Host callbacks must not throw. A null pointer argument does nothing.
 */
typedef struct DimeSettingsCmdHostVtbl {
    int32_t (*log_pixels_y)(void* ctx);
    int32_t (*choose_font)(void* ctx, void* logfont, int32_t* point_size10);
    int32_t (*choose_color)(void* ctx, uint32_t* rgb);
    const uint16_t* (*get_open_file)(void* ctx, int32_t filter, int32_t* ok, size_t* len);
    const uint16_t* (*get_save_file)(void* ctx, const uint16_t* default_path, size_t default_len, int32_t* ok, size_t* len);
    int32_t (*message_box)(void* ctx, const uint16_t* text, size_t len, const uint16_t* caption, uint32_t mb_type);
    const uint16_t* (*app_data_path)(void* ctx, size_t* len);
    const uint16_t* (*documents_path)(void* ctx, size_t* len);
    const uint16_t* (*custom_table_txt_path)(void* ctx, int32_t mode, size_t* len);
    const uint16_t* (*custom_table_cin_path)(void* ctx, int32_t mode, size_t* len);
    int32_t (*write_bom_file)(void* ctx, const uint16_t* path, size_t path_len, const uint16_t* text, size_t text_len);
    const uint16_t* (*load_text_file)(void* ctx, const uint16_t* path, size_t path_len, int32_t* ok, size_t* len);
    int32_t (*parse_cin)(void* ctx, const uint16_t* src, size_t src_len, const uint16_t* dst, size_t dst_len, int32_t custom_table);
    void (*reset_all_defaults)(void* ctx, int32_t mode);
    void (*write_config)(void* ctx, int32_t mode);
    void (*load_config)(void* ctx, int32_t mode);
    void (*load_snapshot)(void* ctx, void* out_snapshot);
    void (*apply_and_save)(void* ctx);
    void (*set_color_mode)(void* ctx, int32_t color_mode);
    void (*load_colors_for_mode)(void* ctx, int32_t color_mode);
    void (*get_colors)(void* ctx, uint32_t* out6);
    void (*set_color)(void* ctx, int32_t index, uint32_t color);
    void (*save_colors_for_mode)(void* ctx, int32_t color_mode);
    void (*set_reverse_conversion_selection)(void* ctx, int32_t sel);
    void (*rebuild)(void* ctx);
    void (*invalidate_content)(void* ctx, int32_t erase);
    void (*update_content)(void* ctx);
    void (*invalidate_swatch)(void* ctx, int32_t index);
    int32_t (*editor_exists)(void* ctx);
    const uint16_t* (*editor_text)(void* ctx, size_t* len);
    void (*set_editor_text_muted)(void* ctx, const uint16_t* text, size_t len);
    int32_t (*editor_line_index)(void* ctx, int32_t line);
    void (*editor_focus_at)(void* ctx, int32_t cp);
    void (*validate_viewport)(void* ctx);
    void (*cancel_bg_scan)(void* ctx);
    void (*enable_save)(void* ctx, int32_t enable);
    void (*update_line_count)(void* ctx);
    const uint16_t* (*validate_buffer)(void* ctx, const uint16_t* text, size_t len, int32_t max_report,
        int32_t* count, int32_t* first_line, size_t* summary_len);
} DimeSettingsCmdHostVtbl;

void dime_settings_command_handle(const DimeSettingsCmdHostVtbl* vtbl, void* ctx, void* snapshot,
    int32_t* grid_expanded, const void* ev);

/* ---- Settings custom table editor (phase 5b-8d, dime-settings::editor) --
 * SettingsEditor::Cpp in Rust. hre is the RICHEDIT50W. viewport writes the
 * inclusive visible line range (0, 0 for an empty editor). validate_viewport
 * validates the visible lines and colours them through TOM without moving
 * the selection, with EN_CHANGE muted; char_ok / char_ctx is the engine's
 * key check or NULL. subclass_proc returns the SUBCLASSPROC to install with
 * SetWindowSubclass(hre, proc, 0, 0); it removes itself on WM_NCDESTROY.
 */
void dime_settings_editor_viewport(void* hre, int32_t* first_visible, int32_t* last_visible);
void dime_settings_editor_validate_viewport(void* hre, int32_t mode, uint32_t max_codes, int32_t is_dark,
    int32_t (*char_ok)(uint16_t ch, void* ctx), void* char_ctx);
const void* dime_settings_editor_subclass_proc(void);

/* ---- Settings populate controls (phase 5b-8e, dime-settings::populate) --
 * SettingsPopulate::Cpp::Populate as control operations, in order. The
 * window applies each to the control with ctrl_id (skipping controls not on
 * the page): RESET = CB_RESETCONTENT, ADD = CB_ADDSTRING of the text (then
 * CB_SETITEMDATA of data when has_data), SELECT = CB_SETCURSEL value,
 * CHECK = BM_SETCHECK value, TEXT = SetWindowTextW. Text is
 * text[text_offset .. text_offset + text_len] without NULs. snapshot is a
 * SettingsSnapshot (216 bytes); reverse_descs are NUL terminated.
 */
enum {
    DIME_POPULATE_RESET = 1,
    DIME_POPULATE_ADD = 2,
    DIME_POPULATE_SELECT = 3,
    DIME_POPULATE_CHECK = 4,
    DIME_POPULATE_TEXT = 5,
};
typedef struct DimePopulateOp {
    int32_t kind;
    int32_t ctrl_id;
    int32_t value;
    int32_t has_data;
    int32_t data;
    int32_t text_offset;
    int32_t text_len;
} DimePopulateOp;
void dime_settings_populate_ops(const void* snapshot, int32_t windows1809, int32_t reverse_is_null,
    const uint16_t* const* reverse_descs, const int32_t* reverse_selected, int32_t reverse_count,
    DimePopulateOp* out, size_t cap, size_t* out_count, uint16_t* text, size_t text_cap, size_t* text_len);

/* ---- Settings control specs (phase 5b-8f, dime-settings::controls) ----
 * SettingsControls::Cpp::BuildSpecs: the child windows of a page in
 * creation order. input is a SettingsWindowLayout::PlanInput (48 bytes),
 * card_w CalcCardW, ime / load the page tables. Rectangles are x, y, w, h.
 * control_class: 0 BUTTON, 1 COMBOBOX, 2 EDIT, 3 STATIC, 4 RICHEDIT50W;
 * role: 0 plain, 1 rich editor, 2 save button; dark_theme (when the window
 * is dark): 0 none, 1 DarkMode_CFD, 2 DarkMode_Explorer. has_text 0 means
 * a null caption; text is text[text_offset .. +text_len] without NULs.
 * Writes up to cap specs and text_cap units; full counts go to out_count
 * and text_len (call once with zero caps to size).
 */
typedef struct DimeControlSpec {
    int32_t ctrl_id;
    int32_t menu_id;
    int32_t role;
    int32_t control_class;
    uint32_t ex_style;
    uint32_t style;
    int32_t has_text;
    int32_t text_offset;
    int32_t text_len;
    int32_t create[4];
    int32_t add[4];
    int32_t vis_h;
    int32_t set_font;
    int32_t dark_theme;
} DimeControlSpec;
void dime_settings_control_specs(const void* input, int32_t card_w, const void* ime, int32_t ime_count,
    const void* load, int32_t load_count, DimeControlSpec* out, size_t cap, size_t* out_count,
    uint16_t* text, size_t text_cap, size_t* text_len);

/* ---- Settings window frames (phase 5b-8g, dime-settings::paint) --------
 * SettingsWindowPaint::Cpp::PaintMainFrame / PaintContentFrame: how the
 * main window and the content area compose the sidebar, the breadcrumb
 * strip, the narrow-mode overlay, and the hamburger. Contexts are
 * PaintContext; `main` has the main window's client size, `content` the
 * content area's. Double buffering and hiding controls stay with the caller.
 */
void dime_settings_paint_main_frame(void* hdc, const void* main_ctx, int32_t has_content);
void dime_settings_paint_content_frame(void* hdc, const void* content_ctx, const void* main_ctx);

/* ---- Settings launch, defaults, scroll (phase 5b-8h, dime-settings::lifecycle)
 * SettingsLifecycle::Cpp: what wWinMain makes of its command line, the mode
 * a second instance sends with WM_COPYDATA, the default card expand state,
 * and where scrolling moves each control (hidden in the title band).
 */
typedef struct DimeLaunchArgs {
    int32_t legacy;
    int32_t reset_ui;
    int32_t new_ui;
    int32_t gui_mode;   /* IME_MODE */
} DimeLaunchArgs;
void dime_settings_parse_launch(const uint16_t* cmd_line, DimeLaunchArgs* out);
int32_t dime_settings_copy_data_mode(const uint16_t* cmd_line);   /* IME_MODE; 4 = none */
void dime_settings_default_expanded(const void* ime, int32_t ime_count, const void* load, int32_t load_count,
    uint8_t* out16);
typedef struct DimeScrollItem {
    int32_t orig_y;
    int32_t orig_w;
    int32_t orig_h;
    int32_t alive;
    int32_t visible;
} DimeScrollItem;
typedef struct DimeScrollAction {
    int32_t moved;
    int32_t y;
    int32_t reset_rgn;
    int32_t show;       /* -1 hide, 1 show */
} DimeScrollAction;
void dime_settings_plan_scroll(const DimeScrollItem* items, size_t count, int32_t scroll_pos, uint32_t dpi,
    DimeScrollAction* out);

/* ---- Settings window create / theme / dpi (phase 5b-8i, dime-settings::frame)
 * SettingsFrame::Cpp::Handle: WM_CREATE, WM_THEMECHANGED (UpdateTheme),
 * WM_SETTINGCHANGE, and WM_DPICHANGED as a reducer over the dark flag that
 * lists the Win32 effects in order. state is SettingsFrame::FrameState
 * (4 bytes), ev FrameEvent (48), out FrameEffect (24).
 */
void dime_settings_frame_handle(void* state, const void* ev, void* out, size_t cap, size_t* out_count);
void dime_settings_frame_initial_size(uint32_t dpi, int32_t out_size[2]);

/* ---- Settings owner draw and control colours (phase 5b-8j, dime-settings::owner)
 * SettingsOwnerDraw::Cpp: WM_DRAWITEM (owner-drawn buttons, the six colour
 * swatches with HMENU 4000-4005) and WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX
 * in the dark theme. dis is a DRAWITEMSTRUCT, ctx a
 * SettingsOwnerDraw::Context (ThemeColors, body font, dpi, six swatch
 * colours), tc a ThemeColors. Both return 1 when handled.
 */
int32_t dime_settings_draw_item(const void* dis, const void* ctx);
int32_t dime_settings_ctl_color(void* hdc, int32_t is_dark, const void* tc, void** brush);

/* ---- Settings command host Win32 calls (phase 5b-8k, dime-settings::winhost)
 * SettingsWinHost::Cpp: the font / colour / open / save dialogs, writing a
 * UTF-16 file with a BOM, the Documents folder, LOGPIXELSY, and the custom
 * table editor's text, line index, caret, and line count. Paths are
 * MAX_PATH (260) unit buffers. lf is a LOGFONTW. Dialog functions return 1
 * on OK.
 */
int32_t dime_settings_win_pick_font(void* owner, void* lf, int32_t* point_size10);
int32_t dime_settings_win_pick_color(void* owner, uint32_t* rgb);
int32_t dime_settings_win_open_file(void* owner, int32_t filter, uint16_t* path);
int32_t dime_settings_win_save_file(void* owner, uint16_t* path);
int32_t dime_settings_win_write_bom_file(const uint16_t* path, const uint16_t* text, size_t len);
void dime_settings_win_documents_path(uint16_t* buf);
int32_t dime_settings_win_log_pixels_y(void* wnd);
void dime_settings_win_editor_text(void* re, uint16_t* buf, size_t cap, size_t* len);
int32_t dime_settings_win_set_editor_text_muted(void* re, const uint16_t* text);
int32_t dime_settings_win_editor_line_index(void* re, int32_t line);
void dime_settings_win_editor_focus_at(void* re, int32_t cp);
int32_t dime_settings_win_editor_line_count(void* re);

/* ---- Settings window procedures (phase 5b-9a, dime-settings::wndproc)
 * SettingsProc::Cpp::Main / Content: which message does what, how its
 * parameters are decoded, and what it returns. Every side effect goes
 * through the host, including DefWindowProc, PostQuitMessage, and
 * SetForegroundWindow. The field order must match SettingsProc.cpp (kVtbl)
 * and settings_window.rs (ProcHostVtbl). Host callbacks must not throw.
 * lparam must be what Windows passes with the message.
 */
typedef struct DimeSettingsProcHostVtbl {
    int32_t (*has_data)(void* ctx);
    int32_t (*has_content)(void* ctx);
    void (*create)(void* ctx, intptr_t instance);
    void (*nav)(void* ctx, int32_t kind, int32_t x, int32_t y, int32_t delta, int32_t thumb);
    void (*theme_changed)(void* ctx);
    void (*setting_changed)(void* ctx);
    void (*dpi_changed)(void* ctx, uint32_t dpi, const int32_t* rect4_or_null);
    void (*paint_main)(void* ctx);
    void (*paint_content)(void* ctx);
    uint32_t (*screen_dpi)(void* ctx);
    void (*switch_mode)(void* ctx, int32_t mode);
    void (*set_foreground)(void* ctx);
    void (*destroy)(void* ctx);
    void (*quit)(void* ctx);
    void (*command)(void* ctx, uintptr_t wparam, intptr_t lparam);
    void (*editor_revalidate)(void* ctx);
    void (*editor_viewport)(void* ctx);
    int32_t (*draw_item)(void* ctx, intptr_t dis);
    int32_t (*ctl_color)(void* ctx, intptr_t hdc, intptr_t* brush);
    intptr_t (*def)(void* ctx, uint32_t msg, uintptr_t wparam, intptr_t lparam);
} DimeSettingsProcHostVtbl;
intptr_t dime_settings_main_proc(const DimeSettingsProcHostVtbl* vtbl, void* ctx, uint32_t msg, uintptr_t wparam,
    intptr_t lparam);
intptr_t dime_settings_content_proc(const DimeSettingsProcHostVtbl* vtbl, void* ctx, uint32_t msg,
    uintptr_t wparam, intptr_t lparam);

/* ---- Settings window driver (phase 5b-9b, dime-settings::driver)
 * SettingsDriver::Cpp: carrying out navigation and frame effects (RunNav /
 * RunFrame), SwitchMode, RebuildContentArea, the scroll bar, and moving
 * controls on scroll. state is SettingsDriver::State (72 bytes), read and
 * written in place (host calls may re-enter the driver); nav_ev is
 * SettingsNav::NavEvent, frame_ev SettingsFrame::FrameEvent. control() writes
 * a SettingsDriver::ControlInfo (32 bytes); create_controls gets a
 * SettingsWindowLayout::PlanInput. invalidate_main's rect is RECT or null.
 * The order must match SettingsDriver.cpp (kVtbl) and settings_window.rs
 * (DriverHostVtbl). Host callbacks must not throw.
 */
typedef struct DimeSettingsDriverHostVtbl {
    uint32_t (*main_dpi)(void* ctx);
    uint32_t (*content_dpi)(void* ctx);
    int32_t (*has_content)(void* ctx);
    void (*main_client)(void* ctx, int32_t* w, int32_t* h);
    int32_t (*content_client_height)(void* ctx);
    int32_t (*vscroll_width)(void* ctx);
    int32_t (*system_dark)(void* ctx);
    int32_t (*color_mode)(void* ctx);
    int32_t (*save_enabled)(void* ctx);
    int32_t (*preview_height)(void* ctx, uint32_t dpi);
    int32_t (*control_count)(void* ctx);
    void (*control)(void* ctx, int32_t index, void* out_info);
    void (*move_control)(void* ctx, int32_t index, int32_t x, int32_t y, int32_t w, int32_t h);
    void (*show_control)(void* ctx, int32_t index, int32_t show);
    void (*reset_control_region)(void* ctx, int32_t index);
    int32_t (*control_checked)(void* ctx, int32_t index);
    void (*set_control_checked)(void* ctx, int32_t index, int32_t on);
    void (*destroy_controls)(void* ctx);
    void (*create_controls)(void* ctx, const void* plan_input, int32_t card_w);
    void (*populate)(void* ctx);
    void (*invalidate_main)(void* ctx, const int32_t* rect4_or_null, int32_t erase);
    void (*invalidate_content)(void* ctx, int32_t erase);
    void (*track_leave)(void* ctx, int32_t content);
    void (*set_scroll_info)(void* ctx, int32_t total, int32_t page, int32_t pos);
    void (*redraw_content)(void* ctx);
    void (*update_content)(void* ctx);
    void (*move_content)(void* ctx, int32_t x, int32_t y, int32_t w, int32_t h);
    void (*focus_content)(void* ctx);
    void (*create_content)(void* ctx, int32_t x, int32_t y, int32_t w, int32_t h);
    void (*content_theme)(void* ctx, int32_t dark);
    void (*set_main_pos)(void* ctx, int32_t x, int32_t y, int32_t w, int32_t h);
    void (*set_dark_title)(void* ctx, int32_t dark);
    void (*set_caption_color)(void* ctx, int32_t attribute);
    void (*theme_brushes)(void* ctx);
    void (*create_fonts)(void* ctx, int32_t dpi);
    void (*destroy_fonts)(void* ctx);
    int32_t (*ask_save_before_back)(void* ctx);
    void (*click_save)(void* ctx);
    void (*toggle_snapshot)(void* ctx, int32_t id, int32_t on);
    void (*apply_and_save)(void* ctx);
    void (*enumerate_reverse_providers)(void* ctx);
    void (*load_config)(void* ctx, int32_t mode);
    void (*load_colors)(void* ctx);
    void (*load_snapshot)(void* ctx);
    void (*recreate_engine)(void* ctx, int32_t mode);
} DimeSettingsDriverHostVtbl;
void dime_settings_driver_nav(const DimeSettingsDriverHostVtbl* vtbl, void* ctx, void* state, const void* nav_ev);
void dime_settings_driver_frame(const DimeSettingsDriverHostVtbl* vtbl, void* ctx, void* state,
    const void* frame_ev);
void dime_settings_driver_switch_mode(const DimeSettingsDriverHostVtbl* vtbl, void* ctx, void* state, int32_t mode);
void dime_settings_driver_rebuild(const DimeSettingsDriverHostVtbl* vtbl, void* ctx, void* state);

/* ---- Settings child windows (phase 5b-9c, dime-settings::children)
 * SettingsChildren::Cpp::Create: CreateWindowEx per DimeControlSpec (text
 * is text[text_offset..+text_len]), the wheel forwarder, body font, dark
 * theme, and the custom table RichEdit (limit, colours, saved table,
 * editor subclass, event mask). ctx_in is SettingsChildren::Context.
 * editor_path returns the full length and writes up to cap - 1 units plus
 * a NUL. The order must match SettingsChildren.cpp (kVtbl) and
 * settings_window.rs (ChildHostVtbl). Host callbacks must not throw.
 */
typedef struct DimeSettingsChildContext {
    void* parent;
    void* instance;
    void* font_body;
    int32_t is_dark;
    uint32_t card_bg;
    uint32_t text_primary;
} DimeSettingsChildContext;
typedef struct DimeSettingsChildHostVtbl {
    void (*added)(void* ctx, int32_t ctrl_id, void* hwnd, int32_t x, int32_t y, int32_t w, int32_t h, int32_t vis_h);
    size_t (*editor_path)(void* ctx, uint16_t* buf, size_t cap);
    void (*editor_ready)(void* ctx, void* hre, int32_t line_count);
} DimeSettingsChildHostVtbl;
void dime_settings_create_children(const DimeSettingsChildHostVtbl* vtbl, void* ctx, const void* ctx_in,
    const DimeControlSpec* specs, size_t count, const uint16_t* text, size_t text_len);

/* ---- Settings window shell (phase 5b-9d, dime-settings::shell)
 * SettingsShell::Cpp: the window placement in HKCU (key / value are
 * NUL-terminated), the three theme brushes (brushes is HBRUSH[3]: sidebar,
 * content, card; theme is ThemeColors), the WM_PAINT double buffering, class
 * registration, and Run. Paint sources call back into the window: has_data,
 * then prepare(user, content, first, second, has_content) with PaintContexts
 * (second is null for the main window). Callbacks must not throw.
 */
typedef struct DimeSettingsPaintSource {
    void* user;
    int32_t (*has_data)(void* user);
    void (*prepare)(void* user, int32_t content, void* first, void* second, int32_t* has_content);
} DimeSettingsPaintSource;
typedef struct DimeSettingsRunParams {
    void* instance;
    int32_t n_cmd_show;
    const uint16_t* main_class;
    const void* main_proc;      /* WNDPROC */
    int32_t icon_id;
    const uint16_t* caption;
    const uint16_t* placement_key;
    const uint16_t* placement_value;
    void (*started)(void* user, void* hwnd);
    void* user;
} DimeSettingsRunParams;
int32_t dime_settings_save_placement(void* hwnd, const uint16_t* key, const uint16_t* value);
int32_t dime_settings_restore_placement(void* hwnd, const uint16_t* key, const uint16_t* value);
void dime_settings_delete_placement(const uint16_t* key, const uint16_t* value);
void dime_settings_create_brushes(const void* theme, void** brushes);
void dime_settings_destroy_brushes(void** brushes);
void dime_settings_shell_paint_main(void* hwnd, const DimeSettingsPaintSource* src);
void dime_settings_shell_paint_content(void* hwnd, const DimeSettingsPaintSource* src);
void dime_settings_shell_paint_main_into(void* hdc, int32_t w, int32_t h, const DimeSettingsPaintSource* src);
void dime_settings_shell_paint_content_into(void* hdc, int32_t w, int32_t h, const DimeSettingsPaintSource* src);
int32_t dime_settings_register_main_class(void* instance, const uint16_t* name, const void* proc, int32_t icon_id);
void dime_settings_register_content_class(void* instance, const uint16_t* name, const void* proc);
int32_t dime_settings_shell_run(const DimeSettingsRunParams* params);

/* ---- Settings window (phase 5b-9e, dime-settings::window)
 * The Rust-owned settings window. Everything it needs from C++ (CConfig,
 * the composition engine's key check, the table parser, custom-table paths,
 * the reverse-conversion list, the themed message box) comes through
 * DimeSettingsAppVtbl over a SettingsApp::Host (ctx). Strings returned
 * through (buf, cap) return the full length and write up to cap - 1 units
 * plus a NUL. Snapshots are SettingsSnapshot. The order must match
 * SettingsApp.cpp (kVtbl) and settings_window.rs (AppVtbl). Callbacks must
 * not throw.
 */
typedef struct DimeSettingsAppVtbl {
    uint32_t (*dpi_for_window)(void* ctx, void* hwnd);
    int32_t (*system_dark)(void* ctx);
    int32_t (*show_message)(void* ctx, void* parent, const uint16_t* text, const uint16_t* caption, uint32_t type);
    void (*load_config)(void* ctx, int32_t mode);
    void (*write_config)(void* ctx, int32_t mode);
    void (*reset_all_defaults)(void* ctx, int32_t mode);
    int32_t (*color_mode)(void* ctx);
    void (*set_color_mode)(void* ctx, int32_t color_mode);
    void (*load_colors_for_mode)(void* ctx, int32_t color_mode);
    void (*save_colors_for_mode)(void* ctx, int32_t color_mode);
    void (*get_colors)(void* ctx, uint32_t* out6);
    void (*set_color)(void* ctx, int32_t index, uint32_t color);
    void (*set_reverse_selection)(void* ctx, int32_t sel);
    void (*enumerate_reverse_providers)(void* ctx);
    int32_t (*reverse_count)(void* ctx);
    size_t (*reverse_description)(void* ctx, int32_t index, uint16_t* buf, size_t cap);
    int32_t (*reverse_is_current)(void* ctx, int32_t index);
    int32_t (*reverse_is_null)(void* ctx);
    void (*load_snapshot)(void* ctx, void* out);
    void (*apply_snapshot)(void* ctx, const void* snapshot);
    int32_t (*parse_cin)(void* ctx, const uint16_t* src, const uint16_t* dst, int32_t custom_table);
    void (*recreate_engine)(void* ctx, int32_t mode);
    void (*destroy_engine)(void* ctx);
    int32_t (*has_engine)(void* ctx);
    int32_t (*engine_char_ok)(void* ctx, uint16_t ch);
    size_t (*custom_table_txt_path)(void* ctx, int32_t mode, uint16_t* buf, size_t cap);
    size_t (*custom_table_cin_path)(void* ctx, int32_t mode, uint16_t* buf, size_t cap);
    size_t (*app_data_path)(void* ctx, uint16_t* buf, size_t cap);
    int32_t (*windows_1809_or_later)(void* ctx);
} DimeSettingsAppVtbl;
/* SettingsWindow::Run: register the classes, create and show the window
 * (saved placement or n_cmd_show), run the message loop. */
int32_t dime_settings_window_run(const DimeSettingsAppVtbl* vtbl, void* app, void* instance, int32_t n_cmd_show,
    int32_t mode, const uint16_t* caption, int32_t icon_id);
/* Tests: SettingsWindow::CreateForTest / StateForTest / PaintForTest. state
 * is SettingsDriver::State, snapshot SettingsSnapshot (either may be null). */
void* dime_settings_window_create(const DimeSettingsAppVtbl* vtbl, void* app, void* instance, int32_t mode,
    const uint16_t* caption, const uint16_t* main_class, const uint16_t* content_class,
    const uint16_t* placement_key, const uint16_t* placement_value, int32_t icon_id);
int32_t dime_settings_window_state(void* hwnd, void* state, void* snapshot);
void dime_settings_window_paint(void* hwnd, void* hdc, int32_t w, int32_t h, int32_t content);
void dime_settings_window_navigate(void* hwnd, int32_t mode, int32_t selected_mode_index, int32_t nav_level,
    int32_t card_index);

#ifdef __cplusplus
}
#endif
