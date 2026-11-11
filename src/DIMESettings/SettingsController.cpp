// SettingsModel.cpp — Implementation of the pure data layer for modernized settings UI
//
// All functions here are pure (no HWND, no GDI) and fully unit-testable.

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <strsafe.h>
#include <shlobj.h>
#include <sys/stat.h>
#include "..\define.h"
#include "..\BaseStructure.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\resource.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#include <cstddef>

// SettingsSnapshot must match dime-config::settings_model::SettingsSnapshot.
static_assert(sizeof(SettingsSnapshot) == 216, "SettingsSnapshot size");
static_assert(alignof(SettingsSnapshot) == 4, "SettingsSnapshot alignment");
static_assert(offsetof(SettingsSnapshot, fontFaceName) == 12, "fontFaceName");
static_assert(offsetof(SettingsSnapshot, colorMode) == 76, "colorMode");
static_assert(offsetof(SettingsSnapshot, autoCompose) == 104, "autoCompose");
static_assert(offsetof(SettingsSnapshot, showNotifyDesktop) == 144, "showNotifyDesktop");
static_assert(offsetof(SettingsSnapshot, big5Filter) == 176, "big5Filter");
static_assert(offsetof(SettingsSnapshot, maxCodes) == 180, "maxCodes");
static_assert(offsetof(SettingsSnapshot, reverseConversionIndex) == 212, "reverseConversionIndex");
static_assert(sizeof(SettingsModel::LineError) == sizeof(int), "LineError");
static_assert(static_cast<int>(SettingsModel::LineError::None) == 0, "LineError::None");
static_assert(static_cast<int>(SettingsModel::LineError::Format) == 1, "LineError::Format");
static_assert(static_cast<int>(SettingsModel::LineError::KeyTooLong) == 2, "LineError::KeyTooLong");
static_assert(static_cast<int>(SettingsModel::LineError::InvalidChar) == 3, "LineError::InvalidChar");
static_assert(static_cast<int>(IME_MODE::IME_MODE_NONE) == 4, "IME_MODE_NONE");
static_assert(CTRL_SHOW_NOTIFY == 11, "CTRL_SHOW_NOTIFY");
static_assert(CTRL_CHARSET_SCOPE == 14, "CTRL_CHARSET_SCOPE");
static_assert(CTRL_NUMERIC_PAD == 15, "CTRL_NUMERIC_PAD");
static_assert(CTRL_SHIFT_ENGLISH_DIGIT_MODE == 16, "CTRL_SHIFT_ENGLISH_DIGIT_MODE");
static_assert(CTRL_PHONETIC_KB == 17, "CTRL_PHONETIC_KB");
static_assert(CTRL_ARROW_KEY_SW_PAGES == 19, "CTRL_ARROW_KEY_SW_PAGES");
static_assert(CTRL_SPACE_AS_PAGEDOWN == 20, "CTRL_SPACE_AS_PAGEDOWN");
static_assert(CTRL_SPACE_AS_FIRST_CAND == 21, "CTRL_SPACE_AS_FIRST_CAND");
static_assert(CTRL_AUTO_COMPOSE == 22, "CTRL_AUTO_COMPOSE");
static_assert(CTRL_CLEAR_ON_BEEP == 23, "CTRL_CLEAR_ON_BEEP");
static_assert(CTRL_MAKE_PHRASE == 24, "CTRL_MAKE_PHRASE");
static_assert(CTRL_CUSTOM_TABLE_PRIORITY == 25, "CTRL_CUSTOM_TABLE_PRIORITY");
static_assert(CTRL_DO_BEEP == 26, "CTRL_DO_BEEP");
static_assert(CTRL_DO_BEEP_NOTIFY == 27, "CTRL_DO_BEEP_NOTIFY");
static_assert(CTRL_DO_BEEP_CANDI == 28, "CTRL_DO_BEEP_CANDI");
static_assert(CTRL_IME_SHIFT_MODE == 29, "CTRL_IME_SHIFT_MODE");
static_assert(CTRL_DOUBLE_SINGLE_BYTE == 30, "CTRL_DOUBLE_SINGLE_BYTE");
static_assert(CTRL_KEYBOARD_OPEN_CLOSE == 31, "CTRL_KEYBOARD_OPEN_CLOSE");
static_assert(CTRL_OUTPUT_CHT_CHS == 32, "CTRL_OUTPUT_CHT_CHS");
static_assert(CTRL_DAYI_ARTICLE == 34, "CTRL_DAYI_ARTICLE");
static_assert(CTRL_SENTENCE_MODE == 35, "CTRL_SENTENCE_MODE");
static_assert(CTRL_DAYI_SELKEY_MODE == 36, "CTRL_DAYI_SELKEY_MODE");
static_assert(CTRL_ARRAY_FORCE_SP == 37, "CTRL_ARRAY_FORCE_SP");
static_assert(CTRL_ARRAY_NOTIFY_SP == 38, "CTRL_ARRAY_NOTIFY_SP");
static_assert(CTRL_ARRAY_SINGLE_QUOTE == 39, "CTRL_ARRAY_SINGLE_QUOTE");
static_assert(CTRL_LOAD_DAYI_MAIN == 300, "CTRL_LOAD_DAYI_MAIN");
static_assert(CTRL_LOAD_ARRAY_MAIN == 301, "CTRL_LOAD_ARRAY_MAIN");
static_assert(CTRL_LOAD_ARRAY_EXPAND == 302, "CTRL_LOAD_ARRAY_EXPAND");
static_assert(CTRL_LOAD_ARRAY_SP == 303, "CTRL_LOAD_ARRAY_SP");
static_assert(CTRL_LOAD_ARRAY_SC == 304, "CTRL_LOAD_ARRAY_SC");
static_assert(CTRL_LOAD_ARRAY_EXT_B == 305, "CTRL_LOAD_ARRAY_EXT_B");
static_assert(CTRL_LOAD_ARRAY_EXT_CD == 306, "CTRL_LOAD_ARRAY_EXT_CD");
static_assert(CTRL_LOAD_ARRAY_EXT_E_TO_J == 307, "CTRL_LOAD_ARRAY_EXT_E_TO_J");
static_assert(CTRL_LOAD_ARRAY40 == 308, "CTRL_LOAD_ARRAY40");
static_assert(CTRL_LOAD_ARRAY_PHRASE == 309, "CTRL_LOAD_ARRAY_PHRASE");
static_assert(CTRL_LOAD_PHONETIC_MAIN == 310, "CTRL_LOAD_PHONETIC_MAIN");
static_assert(CTRL_LOAD_GENERIC_MAIN == 311, "CTRL_LOAD_GENERIC_MAIN");
static_assert(CTRL_LOAD_ASSOC_PHRASE == 312, "CTRL_LOAD_ASSOC_PHRASE");

namespace {
extern "C" int32_t DimeSettingsCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr)
    {
        return 0;
    }
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}
#endif

// ============================================================================
// IME mode bitmask
// ============================================================================

int SettingsModel::GetModeBitmask(IME_MODE mode)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_mode_bitmask(static_cast<int32_t>(mode));
#else
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return SM_MODE_DAYI;
    case IME_MODE::IME_MODE_ARRAY:    return SM_MODE_ARRAY;
    case IME_MODE::IME_MODE_PHONETIC: return SM_MODE_PHONETIC;
    case IME_MODE::IME_MODE_GENERIC:  return SM_MODE_GENERIC;
    default: return 0;
    }
#endif
}

// ============================================================================
// Sidebar hitTest
// ============================================================================

int SettingsModel::SidebarHitTest(int y, int itemHeight, int itemCount)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_sidebar_hit_test(y, itemHeight, itemCount);
#else
    if (y < 0 || itemHeight <= 0 || itemCount <= 0) return -1;
    int index = y / itemHeight;
    if (index >= itemCount) return -1;
    return index;
#endif
}

// ============================================================================
// Scroll computation
// ============================================================================

int SettingsModel::ComputeScrollRange(int contentHeight, int viewportHeight)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_scroll_range(contentHeight, viewportHeight);
#else
    if (contentHeight <= viewportHeight) return 0;
    return contentHeight - viewportHeight;
#endif
}

int SettingsModel::ClampScrollPos(int pos, int maxScroll)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_clamp_scroll(pos, maxScroll);
#else
    if (pos < 0) return 0;
    if (maxScroll < 0) maxScroll = 0;
    if (pos > maxScroll) return maxScroll;
    return pos;
#endif
}

// ============================================================================
// IME mode string conversion
// ============================================================================

const wchar_t* SettingsModel::ImeModeToString(IME_MODE mode)
{
#ifdef DIME_USE_RUST_CONFIG
    return reinterpret_cast<const wchar_t*>(dime_settings_ime_mode_to_string(static_cast<int32_t>(mode)));
#else
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return ImeModeStrings::Dayi;
    case IME_MODE::IME_MODE_ARRAY:    return ImeModeStrings::Array;
    case IME_MODE::IME_MODE_PHONETIC: return ImeModeStrings::Phonetic;
    case IME_MODE::IME_MODE_GENERIC:  return ImeModeStrings::Generic;
    default: return nullptr;
    }
#endif
}

IME_MODE SettingsModel::StringToImeMode(const wchar_t* str)
{
#ifdef DIME_USE_RUST_CONFIG
    return static_cast<IME_MODE>(dime_settings_string_to_ime_mode(reinterpret_cast<const uint16_t*>(str)));
#else
    if (!str) return IME_MODE::IME_MODE_NONE;
    if (_wcsicmp(str, ImeModeStrings::Dayi) == 0)     return IME_MODE::IME_MODE_DAYI;
    if (_wcsicmp(str, ImeModeStrings::Array) == 0)    return IME_MODE::IME_MODE_ARRAY;
    if (_wcsicmp(str, ImeModeStrings::Phonetic) == 0) return IME_MODE::IME_MODE_PHONETIC;
    if (_wcsicmp(str, ImeModeStrings::Generic) == 0)  return IME_MODE::IME_MODE_GENERIC;
    return IME_MODE::IME_MODE_NONE;
#endif
}

// ============================================================================
// Settings snapshot — CConfig bridge
// ============================================================================

SettingsSnapshot SettingsModel::LoadFromConfig()
{
#ifdef DIME_USE_RUST_CONFIG
    uint32_t colors[6] = {};
    const ColorInfo* clrs = CConfig::GetColors();
    if (clrs != nullptr)
    {
        for (int i = 0; i < 6; ++i)
        {
            colors[i] = clrs[i].color;
        }
    }
    SettingsSnapshot snap = {};
    dime_settings_load_snapshot(&snap, colors);
    return snap;
#else
    SettingsSnapshot snap = {};

    snap.fontSize      = CConfig::GetFontSize();
    snap.fontWeight     = CConfig::GetFontWeight();
    snap.fontItalic     = CConfig::GetFontItalic();
    StringCchCopyW(snap.fontFaceName, LF_FACESIZE, CConfig::GetFontFaceName());
    snap.colorMode      = (int)CConfig::GetColorMode();

    // Load effective palette colors (respects system dark/light mode).
    // Caller must call CConfig::LoadColorsForMode() before this if needed.
    {
        const ColorInfo* clrs = CConfig::GetColors();
        snap.itemColor       = clrs[0].color;
        snap.selectedColor   = clrs[1].color;
        snap.itemBGColor     = clrs[2].color;
        snap.phraseColor     = clrs[3].color;
        snap.numberColor     = clrs[4].color;
        snap.selectedBGColor = clrs[5].color;
    }

    snap.autoCompose             = CConfig::GetAutoCompose();
    snap.clearOnBeep             = CConfig::GetClearOnBeep();
    snap.doBeep                  = CConfig::GetDoBeep();
    snap.doBeepNotify            = CConfig::GetDoBeepNotify();
    snap.doBeepOnCandi           = CConfig::GetDoBeepOnCandi();
    snap.makePhrase              = CConfig::GetMakePhrase();
    snap.customTablePriority     = CConfig::getCustomTablePriority();
    snap.arrowKeySWPages         = CConfig::GetArrowKeySWPages();
    snap.spaceAsPageDown         = CConfig::GetSpaceAsPageDown();
    snap.spaceAsFirstCandSelkey  = CConfig::GetSpaceAsFirstCaniSelkey();
    snap.showNotifyDesktop       = CConfig::GetShowNotifyDesktop();
    snap.arrayNotifySP           = CConfig::GetArrayNotifySP();
    snap.arrayForceSP            = CConfig::GetArrayForceSP();
    snap.arraySingleQuoteCustomPhrase = CConfig::GetArraySingleQuoteCustomPhrase();
    snap.dayiArticleMode         = CConfig::getDayiArticleMode();
    snap.sentenceMode            = CConfig::GetSentenceMode();
    snap.doHanConvert            = CConfig::GetDoHanConvert();
    snap.activatedKeyboardMode   = CConfig::GetActivatedKeyboardMode();
    snap.big5Filter              = CConfig::GetBig5Filter();
    snap.maxCodes                = CConfig::GetMaxCodes();
    snap.arrayScope              = (int)CConfig::GetArrayScope();
    snap.numericPad              = (int)CConfig::GetNumericPad();
    snap.phoneticKeyboardLayout  = (int)CConfig::getPhoneticKeyboardLayout();
    snap.dayiSelkeyMode          = (int)CConfig::GetDayiSelkeyMode();
    snap.imeShiftMode            = (int)CConfig::GetIMEShiftMode();
    snap.doubleSingleByteMode    = (int)CConfig::GetDoubleSingleByteMode();
    snap.shiftEnglishDigitMode   = (int)CConfig::GetShiftEnglishDigitMode();

    snap.reverseConversionIndex  = 0; // default; UI populates from reverse conversion list

    return snap;
#endif
}

void SettingsModel::ApplyToConfig(const SettingsSnapshot& snap)
{
#ifdef DIME_USE_RUST_CONFIG
    dime_settings_apply_snapshot(&snap);
#else
    CConfig::SetFontSize(snap.fontSize);
    CConfig::SetFontWeight(snap.fontWeight);
    CConfig::SetFontItalic(snap.fontItalic);
    CConfig::SetFontFaceName(const_cast<WCHAR*>(snap.fontFaceName));
    CConfig::SetColorMode((IME_COLOR_MODE)snap.colorMode);

    // Do NOT write palette colors here — snapshot holds effective colors
    // (may be light/dark/system palette), not custom palette values.
    // Custom palette is only written via SaveColorsForMode() when the user
    // explicitly changes a color swatch in CUSTOM mode.

    CConfig::SetAutoCompose(snap.autoCompose);
    CConfig::SetClearOnBeep(snap.clearOnBeep);
    CConfig::SetDoBeep(snap.doBeep);
    CConfig::SetDoBeepNotify(snap.doBeepNotify);
    CConfig::SetDoBeepOnCandi(snap.doBeepOnCandi);
    CConfig::SetMakePhrase(snap.makePhrase);
    CConfig::setCustomTablePriority(snap.customTablePriority);
    CConfig::SetArrowKeySWPages(snap.arrowKeySWPages);
    CConfig::SetSpaceAsPageDown(snap.spaceAsPageDown);
    CConfig::SetSpaceAsFirstCaniSelkey(snap.spaceAsFirstCandSelkey);
    CConfig::SetShowNotifyDesktop(snap.showNotifyDesktop);
    CConfig::SetArrayNotifySP(snap.arrayNotifySP);
    CConfig::SetArrayForceSP(snap.arrayForceSP);
    CConfig::SetArraySingleQuoteCustomPhrase(snap.arraySingleQuoteCustomPhrase);
    CConfig::setDayiArticleMode(snap.dayiArticleMode);
    CConfig::SetSentenceMode(snap.sentenceMode);
    CConfig::SetDoHanConvert(snap.doHanConvert);
    CConfig::SetActivatedKeyboardMode(snap.activatedKeyboardMode);
    CConfig::SetBig5Filter(snap.big5Filter);
    CConfig::SetMaxCodes(snap.maxCodes);
    CConfig::SetArrayScope((ARRAY_SCOPE)snap.arrayScope);
    CConfig::SetNumericPad((NUMERIC_PAD)snap.numericPad);
    CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT)snap.phoneticKeyboardLayout);
    CConfig::SetDayiSelkeyMode((DAYI_SELKEY_MODE)snap.dayiSelkeyMode);
    CConfig::SetIMEShiftMode((IME_SHIFT_MODE)snap.imeShiftMode);
    CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)snap.doubleSingleByteMode);
    CConfig::SetShiftEnglishDigitMode((IME_SHIFT_ENGLISH_DIGIT_MODE)snap.shiftEnglishDigitMode);
#endif
}

void SettingsModel::SyncToggleToSnapshot(SettingsSnapshot& s, SettingsControlId id, bool isOn)
{
#ifdef DIME_USE_RUST_CONFIG
    dime_settings_sync_toggle(&s, static_cast<int32_t>(id), isOn ? 1 : 0);
#else
    switch (id) {
    case CTRL_DO_BEEP:             s.doBeep = isOn; break;
    case CTRL_DO_BEEP_NOTIFY:      s.doBeepNotify = isOn; break;
    case CTRL_DO_BEEP_CANDI:       s.doBeepOnCandi = isOn; break;
    case CTRL_SHOW_NOTIFY:         s.showNotifyDesktop = isOn; break;
    case CTRL_AUTO_COMPOSE:        s.autoCompose = isOn; break;
    case CTRL_CLEAR_ON_BEEP:       s.clearOnBeep = isOn; break;
    case CTRL_MAKE_PHRASE:         s.makePhrase = isOn; break;
    case CTRL_CUSTOM_TABLE_PRIORITY: s.customTablePriority = isOn; break;
    case CTRL_ARROW_KEY_SW_PAGES:  s.arrowKeySWPages = isOn; break;
    case CTRL_SPACE_AS_PAGEDOWN:   s.spaceAsPageDown = isOn; break;
    case CTRL_SPACE_AS_FIRST_CAND: s.spaceAsFirstCandSelkey = isOn; break;
    case CTRL_KEYBOARD_OPEN_CLOSE: s.activatedKeyboardMode = isOn; break;
    case CTRL_OUTPUT_CHT_CHS:      s.doHanConvert = isOn; break;
    case CTRL_DAYI_ARTICLE:        s.dayiArticleMode = isOn; break;
    case CTRL_SENTENCE_MODE:       s.sentenceMode = isOn; break;
    case CTRL_ARRAY_FORCE_SP:      s.arrayForceSP = isOn; break;
    case CTRL_ARRAY_NOTIFY_SP:     s.arrayNotifySP = isOn; break;
    case CTRL_ARRAY_SINGLE_QUOTE:  s.arraySingleQuoteCustomPhrase = isOn; break;
    default: break;
    }
#endif
}

SettingsModel::LineValidation SettingsModel::ValidateLine(const wchar_t* line, int len, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEngine)
{
#ifdef DIME_USE_RUST_CONFIG
    DimeLineError errs[1024];
    size_t count = 0;
    const int32_t ok = dime_settings_validate_line(
        reinterpret_cast<const uint16_t*>(line),
        (line != nullptr && len > 0) ? static_cast<size_t>(len) : 0,
        static_cast<int32_t>(mode),
        maxCodes,
        (pEngine != nullptr) ? &DimeSettingsCharOk : nullptr,
        pEngine,
        errs,
        1024,
        &count);
    LineValidation v;
    v.valid = (ok != 0);
    if (count > 1024) count = 1024;
    v.errors.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        LineError kind = LineError::None;
        if (errs[i].code == static_cast<int32_t>(LineError::Format)) kind = LineError::Format;
        else if (errs[i].code == static_cast<int32_t>(LineError::KeyTooLong)) kind = LineError::KeyTooLong;
        else if (errs[i].code == static_cast<int32_t>(LineError::InvalidChar)) kind = LineError::InvalidChar;
        v.errors.push_back({ kind, errs[i].start, errs[i].len });
    }
    return v;
#else
    // Matches CConfig::ValidateCustomTableLines per-line logic exactly.
    // 3 levels: Format → KeyTooLong → InvalidChar (may report multiple chars)
    LineValidation v = { true, {} };
    if (!line || len <= 0) return v;

    // Trim
    int s = 0;
    while (s < len && iswspace(line[s])) ++s;
    int e = len;
    while (e > s && iswspace(line[e - 1])) --e;
    if (s >= e) return v; // empty line → valid (skip)

    // Level 0: hard length cap. Bounds worst-case validator runtime regardless
    // of caller. Defends against the catastrophic-backtracking footgun that the
    // old std::wregex below used to hit when the read path fed the validator a
    // single mega-line (issue #130).
    if ((e - s) > MAX_TABLE_LINE_LENGTH) {
        v.valid = false;
        v.errors.push_back({ LineError::Format, s, e - s });
        return v;
    }

    // Level 1: manual format scan. Per-line format is documented as
    // "輸入碼 空白 詞彙" (key whitespace value) — exactly two non-whitespace
    // tokens. Anything else (no separator, no value, or extra tokens after
    // the value like "key value extra") is a format error.
    const wchar_t* p      = line + s;
    const wchar_t* end    = line + e;

    // Key: first non-whitespace run.
    const wchar_t* keyEnd = p;
    while (keyEnd < end && !iswspace(*keyEnd)) ++keyEnd;
    if (keyEnd == p || keyEnd == end) {
        v.valid = false;
        v.errors.push_back({ LineError::Format, s, e - s });
        return v;
    }
    // Separator: whitespace run.
    const wchar_t* valStart = keyEnd;
    while (valStart < end && iswspace(*valStart)) ++valStart;
    if (valStart == end) {
        v.valid = false;
        v.errors.push_back({ LineError::Format, s, e - s });
        return v;
    }
    // Value: second non-whitespace run.
    const wchar_t* valEnd = valStart;
    while (valEnd < end && !iswspace(*valEnd)) ++valEnd;
    // Anything after the value must be whitespace only — no third token.
    const wchar_t* tail = valEnd;
    while (tail < end && iswspace(*tail)) ++tail;
    if (tail != end) {
        v.valid = false;
        v.errors.push_back({ LineError::Format, s, e - s });
        return v;
    }
    std::wstring key(p, keyEnd - p);

    // Level 2: Check key length
    UINT maxKeyLength = (mode == IME_MODE::IME_MODE_PHONETIC) ? MAX_KEY_LENGTH : maxCodes;
    if (key.length() > maxKeyLength)
    {
        // Key too long — mark entire key
        v.valid = false;
        v.errors.push_back({ LineError::KeyTooLong, s, (int)key.length() });
        return v; // Skip character validation if key already too long (same as original)
    }

    // Level 3: Validate each character in key
    int keyStart = s;

    if (mode == IME_MODE::IME_MODE_PHONETIC)
    {
        for (size_t i = 0; i < key.length(); ++i)
        {
            WCHAR c = key[i];
            // Validate: printable ASCII excluding space, AND cannot be '*' or '?'
            if (!(c >= L'!' && c <= L'~') || (c == L'*' || c == L'?'))
            {
                v.valid = false;
                v.errors.push_back({ LineError::InvalidChar, keyStart + (int)i, 1 });
            }
        }
    }
    else
    {
        // Quick accept: if the key contains only ASCII letters, skip char-by-char validation
        bool allAsciiLetters = true;
        for (WCHAR c : key) {
            WCHAR cu = towupper(c);
            if (!(cu >= L'A' && cu <= L'Z')) {
                allAsciiLetters = false;
                break;
            }
        }

        if (!allAsciiLetters)
        {
            // Validate each character with composition engine if available
            for (size_t i = 0; i < key.length(); ++i)
            {
                WCHAR c = key[i];
                BOOL ok = FALSE;

                if (pEngine)
                {
                    // Use engine's full validation logic (authoritative)
                    ok = pEngine->ValidateCompositionKeyCharFull(c) ? TRUE : FALSE;
                }
                else
                {
                    // Fallback: basic range check same as inline ValidateCompositionKeyChar
                    WCHAR cu = towupper(c);
                    ok = (cu >= 32 && cu <= 32 + MAX_RADICAL) ? TRUE : FALSE;
                }

                if (!ok)
                {
                    v.valid = false;
                    v.errors.push_back({ LineError::InvalidChar, keyStart + (int)i, 1 });
                }
            }
        }
    }
    return v;
#endif
}

// ============================================================================
// Composition engine lifecycle
// ============================================================================

CCompositionProcessorEngine* SettingsModel::CreateEngine(IME_MODE mode)
{
    auto* pEngine = new (std::nothrow) CCompositionProcessorEngine(nullptr);
    if (pEngine) {
        pEngine->SetupDictionaryFile(mode);
        pEngine->SetupConfiguration(mode);
        pEngine->SetupKeystroke(mode);
    }
    return pEngine;
}

void SettingsModel::DestroyEngine(CCompositionProcessorEngine*& pEngine)
{
    if (pEngine) {
        delete pEngine;
        pEngine = nullptr;
    }
}

// ============================================================================
// Custom table file paths
// ============================================================================

void SettingsModel::GetCustomTableTxtPath(IME_MODE mode, WCHAR* out, DWORD cch)
{
    WCHAR appData[MAX_PATH] = {};
    Global::GetAppDataPath(appData, TRUE);
    const wchar_t* suffix;
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     suffix = L"\\DIME\\DAYI-Custom.txt";     break;
    case IME_MODE::IME_MODE_ARRAY:    suffix = L"\\DIME\\ARRAY-Custom.txt";    break;
    case IME_MODE::IME_MODE_PHONETIC: suffix = L"\\DIME\\PHONETIC-Custom.txt"; break;
    default:                          suffix = L"\\DIME\\GENERIC-Custom.txt";  break;
    }
    StringCchPrintfW(out, cch, L"%s%s", appData, suffix);
}

void SettingsModel::GetCustomTableCINPath(IME_MODE mode, WCHAR* out, DWORD cch)
{
    WCHAR appData[MAX_PATH] = {};
    Global::GetAppDataPath(appData, TRUE);
    const wchar_t* suffix;
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     suffix = L"\\DIME\\DAYI-Custom.cin";     break;
    case IME_MODE::IME_MODE_ARRAY:    suffix = L"\\DIME\\ARRAY-Custom.cin";    break;
    case IME_MODE::IME_MODE_PHONETIC: suffix = L"\\DIME\\PHONETIC-Custom.cin"; break;
    default:                          suffix = L"\\DIME\\GENERIC-Custom.cin";  break;
    }
    StringCchPrintfW(out, cch, L"%s%s", appData, suffix);
}

// ============================================================================
// Encoding-aware text loader (issue #130)
// ============================================================================
// The legacy ImportCustomTable / initial-load path read raw bytes straight
// into a WCHAR[] buffer with no encoding conversion. For ANSI/Big5/UTF-8
// sources the bytes reinterpreted to wide chars with zero whitespace, which
// in turn fed the format-checker regex catastrophic input. This helper
// detects encoding and returns a real UTF-16LE wide string.
LPWSTR SettingsModel::LoadTextFileAsUtf16(LPCWSTR path, size_t* outLen)
{
#ifdef DIME_USE_RUST_CONFIG
    // File open and the empty-file buffer stay here. The byte ladder,
    // including the UTF-16 BE double swap, is dime_settings_decode_text*.
    if (outLen) *outLen = 0;
    if (!path) return nullptr;

    HANDLE hFile = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD dwSize = GetFileSize(hFile, nullptr);
    if (dwSize == INVALID_FILE_SIZE || dwSize == 0) {
        CloseHandle(hFile);
        LPWSTR empty = new (std::nothrow) WCHAR[1];
        if (empty) empty[0] = 0;
        return empty;
    }

    BYTE* raw = new (std::nothrow) BYTE[dwSize];
    if (!raw) { CloseHandle(hFile); return nullptr; }
    DWORD dwRead = 0;
    if (!ReadFile(hFile, raw, dwSize, &dwRead, nullptr) || dwRead == 0) {
        delete[] raw;
        CloseHandle(hFile);
        return nullptr;
    }
    CloseHandle(hFile);

    uint32_t wlen = 0;
    if (dime_settings_decode_text_len(raw, dwRead, &wlen) == 0) {
        delete[] raw;
        return nullptr;
    }
    LPWSTR out = new (std::nothrow) WCHAR[static_cast<size_t>(wlen) + 1];
    if (!out) { delete[] raw; return nullptr; }
    if (dime_settings_decode_text(raw, dwRead, reinterpret_cast<uint16_t*>(out), wlen) == 0) {
        delete[] out;
        delete[] raw;
        return nullptr;
    }
    out[wlen] = 0;
    delete[] raw;
    if (outLen) *outLen = wlen;
    return out;
#else
    if (outLen) *outLen = 0;
    if (!path) return nullptr;

    HANDLE hFile = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD dwSize = GetFileSize(hFile, nullptr);
    if (dwSize == INVALID_FILE_SIZE || dwSize == 0) {
        CloseHandle(hFile);
        // Empty file → return a valid 1-wchar NUL buffer so callers can
        // SetWindowTextW(L"") rather than blank-on-failure.
        LPWSTR empty = new (std::nothrow) WCHAR[1];
        if (empty) empty[0] = 0;
        return empty;
    }

    BYTE* raw = new (std::nothrow) BYTE[dwSize];
    if (!raw) { CloseHandle(hFile); return nullptr; }
    DWORD dwRead = 0;
    if (!ReadFile(hFile, raw, dwSize, &dwRead, nullptr) || dwRead == 0) {
        delete[] raw;
        CloseHandle(hFile);
        return nullptr;
    }
    CloseHandle(hFile);

    // ---- Detect BOM ----
    // UTF-16LE BOM: FF FE
    if (dwRead >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t bytes = dwRead - 2;
        size_t wlen = bytes / sizeof(WCHAR);
        LPWSTR out = new (std::nothrow) WCHAR[wlen + 1];
        if (!out) { delete[] raw; return nullptr; }
        memcpy(out, raw + 2, wlen * sizeof(WCHAR));
        out[wlen] = 0;
        delete[] raw;
        if (outLen) *outLen = wlen;
        return out;
    }
    // UTF-16BE BOM: FE FF — byte-swap into LE
    if (dwRead >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        size_t bytes = dwRead - 2;
        size_t wlen = bytes / sizeof(WCHAR);
        LPWSTR out = new (std::nothrow) WCHAR[wlen + 1];
        if (!out) { delete[] raw; return nullptr; }
        for (size_t i = 0; i < wlen; ++i) {
            BYTE hi = raw[2 + i * 2];
            BYTE lo = raw[2 + i * 2 + 1];
            out[i] = (WCHAR)((hi << 8) | lo);
            // swap to LE
            out[i] = (WCHAR)(((out[i] & 0xFF) << 8) | ((out[i] >> 8) & 0xFF));
        }
        out[wlen] = 0;
        delete[] raw;
        if (outLen) *outLen = wlen;
        return out;
    }

    // UTF-8 BOM: EF BB BF — strip and decode as UTF-8
    BYTE* utf8Start = raw;
    DWORD utf8Bytes = dwRead;
    if (dwRead >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        utf8Start = raw + 3;
        utf8Bytes = dwRead - 3;
    }

    // Try strict UTF-8 first (catches any UTF-8 file, with or without BOM).
    int wcharsNeeded = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        (LPCCH)utf8Start, (int)utf8Bytes, nullptr, 0);
    UINT useCp = CP_UTF8;
    DWORD useFlags = MB_ERR_INVALID_CHARS;
    if (wcharsNeeded == 0) {
        // Not valid UTF-8 → fall back to system ANSI codepage (CP950 on zh-TW).
        useCp = CP_ACP;
        useFlags = 0;
        wcharsNeeded = MultiByteToWideChar(CP_ACP, 0,
            (LPCCH)utf8Start, (int)utf8Bytes, nullptr, 0);
    }
    if (wcharsNeeded <= 0) {
        delete[] raw;
        return nullptr;
    }

    LPWSTR out = new (std::nothrow) WCHAR[wcharsNeeded + 1];
    if (!out) { delete[] raw; return nullptr; }
    int wlen = MultiByteToWideChar(useCp, useFlags,
        (LPCCH)utf8Start, (int)utf8Bytes, out, wcharsNeeded);
    out[wlen] = 0;
    delete[] raw;
    if (outLen) *outLen = (size_t)wlen;
    return out;
#endif
}

// ============================================================================
// Sidebar index ↔ IME mode
// ============================================================================

IME_MODE SettingsModel::IndexToMode(int index)
{
#ifdef DIME_USE_RUST_CONFIG
    return static_cast<IME_MODE>(dime_settings_index_to_mode(index));
#else
    switch (index) {
    case 0: return IME_MODE::IME_MODE_DAYI;
    case 1: return IME_MODE::IME_MODE_ARRAY;
    case 2: return IME_MODE::IME_MODE_PHONETIC;
    case 3: return IME_MODE::IME_MODE_GENERIC;
    default: return IME_MODE::IME_MODE_DAYI;
    }
#endif
}

int SettingsModel::ModeToIndex(IME_MODE mode)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_mode_to_index(static_cast<int32_t>(mode));
#else
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return 0;
    case IME_MODE::IME_MODE_ARRAY:    return 1;
    case IME_MODE::IME_MODE_PHONETIC: return 2;
    case IME_MODE::IME_MODE_GENERIC:  return 3;
    default: return 0;
    }
#endif
}

// ============================================================================
// Combo selection → snapshot field mapping
// ============================================================================

void SettingsModel::SyncComboToSnapshot(SettingsSnapshot& s, SettingsControlId id, int sel)
{
#ifdef DIME_USE_RUST_CONFIG
    dime_settings_sync_combo(&s, static_cast<int32_t>(id), sel);
#else
    switch (id) {
    case CTRL_CHARSET_SCOPE:       s.big5Filter = (sel == 1); break;
    case CTRL_NUMERIC_PAD:         s.numericPad = sel; break;
    case CTRL_PHONETIC_KB:         s.phoneticKeyboardLayout = sel; break;
    case CTRL_DAYI_SELKEY_MODE:    s.dayiSelkeyMode = sel; break;
    case CTRL_IME_SHIFT_MODE:      s.imeShiftMode = sel; break;
    case CTRL_DOUBLE_SINGLE_BYTE:        s.doubleSingleByteMode = sel; break;
    case CTRL_SHIFT_ENGLISH_DIGIT_MODE:  s.shiftEnglishDigitMode = sel; break;
    case CTRL_KEYBOARD_OPEN_CLOSE: s.activatedKeyboardMode = (sel == 0); break;
    case CTRL_OUTPUT_CHT_CHS:      s.doHanConvert = (sel == 1); break;
    default: break;
    }
#endif
}

// ============================================================================
// CTRL_MAX_CODES text → value
// ============================================================================

bool SettingsModel::ParseMaxCodes(const wchar_t* text, UINT& outVal)
{
#ifdef DIME_USE_RUST_CONFIG
    uint32_t val = 0;
    if (!dime_settings_parse_max_codes(reinterpret_cast<const uint16_t*>(text), &val))
    {
        return false;
    }
    outVal = val;
    return true;
#else
    if (!text) return false;
    UINT val = (UINT)_wtoi(text);
    if (val >= 1 && val <= 20) {
        outVal = val;
        return true;
    }
    return false;
#endif
}

// ============================================================================
// Load table control ID → CIN file suffix
// ============================================================================

const wchar_t* SettingsModel::GetLoadTableSuffix(SettingsControlId id)
{
#ifdef DIME_USE_RUST_CONFIG
    return reinterpret_cast<const wchar_t*>(dime_settings_load_table_suffix(static_cast<int32_t>(id)));
#else
    switch (id) {
    case CTRL_LOAD_DAYI_MAIN:     return L"\\DIME\\Dayi.cin";
    case CTRL_LOAD_ARRAY_MAIN:    return L"\\DIME\\Array.cin";
    case CTRL_LOAD_PHONETIC_MAIN: return L"\\DIME\\Phone.cin";
    case CTRL_LOAD_GENERIC_MAIN:  return L"\\DIME\\Generic.cin";
    case CTRL_LOAD_ASSOC_PHRASE:   return L"\\DIME\\Phrase.cin";
    case CTRL_LOAD_ARRAY_SP:      return L"\\DIME\\Array-special.cin";
    case CTRL_LOAD_ARRAY_SC:      return L"\\DIME\\Array-shortcode.cin";
    case CTRL_LOAD_ARRAY_EXT_B:   return L"\\DIME\\Array-Ext-B.cin";
    case CTRL_LOAD_ARRAY_EXT_CD:  return L"\\DIME\\Array-Ext-CD.cin";
    case CTRL_LOAD_ARRAY_EXT_E_TO_J: return L"\\DIME\\Array-Ext-EF.cin";
    case CTRL_LOAD_ARRAY40:       return L"\\DIME\\Array40.cin";
    case CTRL_LOAD_ARRAY_PHRASE:   return L"\\DIME\\Array-Phrase.cin";
    default: return nullptr;
    }
#endif
}
