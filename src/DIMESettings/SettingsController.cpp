// SettingsModel.cpp — Implementation of the pure data layer for modernized settings UI
//
// All functions here are pure (no HWND, no GDI) and fully unit-testable.

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <strsafe.h>
#include <shlobj.h>
#include <sys/stat.h>
#include <regex>
#include "..\define.h"
#include "..\BaseStructure.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\resource.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"

// ============================================================================
// IME mode bitmask
// ============================================================================

int SettingsModel::GetModeBitmask(IME_MODE mode)
{
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return SM_MODE_DAYI;
    case IME_MODE::IME_MODE_ARRAY:    return SM_MODE_ARRAY;
    case IME_MODE::IME_MODE_PHONETIC: return SM_MODE_PHONETIC;
    case IME_MODE::IME_MODE_GENERIC:  return SM_MODE_GENERIC;
    default: return 0;
    }
}

// ============================================================================
// Sidebar hitTest
// ============================================================================

int SettingsModel::SidebarHitTest(int y, int itemHeight, int itemCount)
{
    if (y < 0 || itemHeight <= 0 || itemCount <= 0) return -1;
    int index = y / itemHeight;
    if (index >= itemCount) return -1;
    return index;
}

// ============================================================================
// Scroll computation
// ============================================================================

int SettingsModel::ComputeScrollRange(int contentHeight, int viewportHeight)
{
    if (contentHeight <= viewportHeight) return 0;
    return contentHeight - viewportHeight;
}

int SettingsModel::ClampScrollPos(int pos, int maxScroll)
{
    if (pos < 0) return 0;
    if (maxScroll < 0) maxScroll = 0;
    if (pos > maxScroll) return maxScroll;
    return pos;
}

// ============================================================================
// IME mode string conversion
// ============================================================================

const wchar_t* SettingsModel::ImeModeToString(IME_MODE mode)
{
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return ImeModeStrings::Dayi;
    case IME_MODE::IME_MODE_ARRAY:    return ImeModeStrings::Array;
    case IME_MODE::IME_MODE_PHONETIC: return ImeModeStrings::Phonetic;
    case IME_MODE::IME_MODE_GENERIC:  return ImeModeStrings::Generic;
    default: return nullptr;
    }
}

IME_MODE SettingsModel::StringToImeMode(const wchar_t* str)
{
    if (!str) return IME_MODE::IME_MODE_NONE;
    if (_wcsicmp(str, ImeModeStrings::Dayi) == 0)     return IME_MODE::IME_MODE_DAYI;
    if (_wcsicmp(str, ImeModeStrings::Array) == 0)    return IME_MODE::IME_MODE_ARRAY;
    if (_wcsicmp(str, ImeModeStrings::Phonetic) == 0) return IME_MODE::IME_MODE_PHONETIC;
    if (_wcsicmp(str, ImeModeStrings::Generic) == 0)  return IME_MODE::IME_MODE_GENERIC;
    return IME_MODE::IME_MODE_NONE;
}

// ============================================================================
// Settings snapshot — CConfig bridge
// ============================================================================

SettingsSnapshot SettingsModel::LoadFromConfig()
{
    SettingsSnapshot snap = {};

    snap.fontSize      = CConfig::GetFontSize();
    snap.fontWeight     = CConfig::GetFontWeight();
    snap.fontItalic     = CConfig::GetFontItalic();
    StringCchCopyW(snap.fontFaceName, LF_FACESIZE, CConfig::GetFontFaceName());
    snap.colorMode      = (int)CConfig::GetColorMode();

    snap.itemColor      = CConfig::GetItemColor();
    snap.phraseColor    = CConfig::GetPhraseColor();
    snap.numberColor    = CConfig::GetNumberColor();
    snap.itemBGColor    = CConfig::GetItemBGColor();
    snap.selectedColor  = CConfig::GetSelectedColor();
    snap.selectedBGColor = CConfig::GetSelectedBGColor();

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
    snap.doHanConvert            = CConfig::GetDoHanConvert();
    snap.activatedKeyboardMode   = CConfig::GetActivatedKeyboardMode();
    snap.big5Filter              = CConfig::GetBig5Filter();
    snap.maxCodes                = CConfig::GetMaxCodes();
    snap.arrayScope              = (int)CConfig::GetArrayScope();
    snap.numericPad              = (int)CConfig::GetNumericPad();
    snap.phoneticKeyboardLayout  = (int)CConfig::getPhoneticKeyboardLayout();
    snap.imeShiftMode            = (int)CConfig::GetIMEShiftMode();
    snap.doubleSingleByteMode    = (int)CConfig::GetDoubleSingleByteMode();

    snap.reverseConversionIndex  = 0; // default; UI populates from reverse conversion list

    return snap;
}

void SettingsModel::ApplyToConfig(const SettingsSnapshot& snap)
{
    CConfig::SetFontSize(snap.fontSize);
    CConfig::SetFontWeight(snap.fontWeight);
    CConfig::SetFontItalic(snap.fontItalic);
    CConfig::SetFontFaceName(const_cast<WCHAR*>(snap.fontFaceName));
    CConfig::SetColorMode((IME_COLOR_MODE)snap.colorMode);

    CConfig::SetItemColor(snap.itemColor);
    CConfig::SetPhraseColor(snap.phraseColor);
    CConfig::SetNumberColor(snap.numberColor);
    CConfig::SetItemBGColor(snap.itemBGColor);
    CConfig::SetSelectedColor(snap.selectedColor);
    CConfig::SetSelectedBGColor(snap.selectedBGColor);

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
    CConfig::SetDoHanConvert(snap.doHanConvert);
    CConfig::SetActivatedKeyboardMode(snap.activatedKeyboardMode);
    CConfig::SetBig5Filter(snap.big5Filter);
    CConfig::SetMaxCodes(snap.maxCodes);
    CConfig::SetArrayScope((ARRAY_SCOPE)snap.arrayScope);
    CConfig::SetNumericPad((NUMERIC_PAD)snap.numericPad);
    CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT)snap.phoneticKeyboardLayout);
    CConfig::SetIMEShiftMode((IME_SHIFT_MODE)snap.imeShiftMode);
    CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)snap.doubleSingleByteMode);
}

void SettingsModel::SyncToggleToSnapshot(SettingsSnapshot& s, SettingsControlId id, bool isOn)
{
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
    case CTRL_ARRAY_FORCE_SP:      s.arrayForceSP = isOn; break;
    case CTRL_ARRAY_NOTIFY_SP:     s.arrayNotifySP = isOn; break;
    case CTRL_ARRAY_SINGLE_QUOTE:  s.arraySingleQuoteCustomPhrase = isOn; break;
    default: break;
    }
}

SettingsModel::LineValidation SettingsModel::ValidateLine(const wchar_t* line, int len, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEngine)
{
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

    // Extract trimmed substring for format check
    std::wstring trimmed(line + s, e - s);

    // Level 1: regex format check — key (no whitespace) + whitespace + value
    static const std::wregex kv_re(L"^([^\\s]+)\\s+(.+)$");
    std::wsmatch kv_match;
    if (!std::regex_match(trimmed, kv_match, kv_re))
    {
        // Format error — mark entire line
        v.valid = false;
        v.errors.push_back({ LineError::Format, s, e - s });
        return v;
    }

    std::wstring key = kv_match[1].str();

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
    SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, TRUE);
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
    SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, TRUE);
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
// Sidebar index ↔ IME mode
// ============================================================================

IME_MODE SettingsModel::IndexToMode(int index)
{
    switch (index) {
    case 0: return IME_MODE::IME_MODE_DAYI;
    case 1: return IME_MODE::IME_MODE_ARRAY;
    case 2: return IME_MODE::IME_MODE_PHONETIC;
    case 3: return IME_MODE::IME_MODE_GENERIC;
    default: return IME_MODE::IME_MODE_DAYI;
    }
}

int SettingsModel::ModeToIndex(IME_MODE mode)
{
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     return 0;
    case IME_MODE::IME_MODE_ARRAY:    return 1;
    case IME_MODE::IME_MODE_PHONETIC: return 2;
    case IME_MODE::IME_MODE_GENERIC:  return 3;
    default: return 0;
    }
}

// ============================================================================
// Combo selection → snapshot field mapping
// ============================================================================

void SettingsModel::SyncComboToSnapshot(SettingsSnapshot& s, SettingsControlId id, int sel)
{
    switch (id) {
    case CTRL_CHARSET_SCOPE:       s.big5Filter = (sel == 1); break;
    case CTRL_NUMERIC_PAD:         s.numericPad = sel; break;
    case CTRL_PHONETIC_KB:         s.phoneticKeyboardLayout = sel; break;
    case CTRL_IME_SHIFT_MODE:      s.imeShiftMode = sel; break;
    case CTRL_DOUBLE_SINGLE_BYTE:  s.doubleSingleByteMode = sel; break;
    case CTRL_KEYBOARD_OPEN_CLOSE: s.activatedKeyboardMode = (sel == 0); break;
    case CTRL_OUTPUT_CHT_CHS:      s.doHanConvert = (sel == 1); break;
    default: break;
    }
}

// ============================================================================
// CTRL_MAX_CODES text → value
// ============================================================================

bool SettingsModel::ParseMaxCodes(const wchar_t* text, UINT& outVal)
{
    if (!text) return false;
    UINT val = (UINT)_wtoi(text);
    if (val >= 1 && val <= 20) {
        outVal = val;
        return true;
    }
    return false;
}

// ============================================================================
// Load table control ID → CIN file suffix
// ============================================================================

const wchar_t* SettingsModel::GetLoadTableSuffix(SettingsControlId id)
{
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
}
