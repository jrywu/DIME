// SettingsKeys.h — Canonical names for all settings keys
//
// Both CLI's g_keyRegistry and UI's ControlDef reference these constants
// to ensure key names stay in sync. Adding a key here without adding it
// to both CLI and UI will be caught by consistency unit tests (UT-SM-22/23/24).

#pragma once

namespace SettingsKeys {
    // Boolean keys
    constexpr const wchar_t AutoCompose[]                  = L"AutoCompose";
    constexpr const wchar_t SpaceAsPageDown[]              = L"SpaceAsPageDown";
    constexpr const wchar_t SpaceAsFirstCandSelkey[]       = L"SpaceAsFirstCandSelkey";
    constexpr const wchar_t ArrowKeySWPages[]              = L"ArrowKeySWPages";
    constexpr const wchar_t ClearOnBeep[]                  = L"ClearOnBeep";
    constexpr const wchar_t DoBeep[]                       = L"DoBeep";
    constexpr const wchar_t DoBeepNotify[]                 = L"DoBeepNotify";
    constexpr const wchar_t DoBeepOnCandi[]                = L"DoBeepOnCandi";
    constexpr const wchar_t ActivatedKeyboardMode[]        = L"ActivatedKeyboardMode";
    constexpr const wchar_t MakePhrase[]                   = L"MakePhrase";
    constexpr const wchar_t ShowNotifyDesktop[]            = L"ShowNotifyDesktop";
    constexpr const wchar_t DoHanConvert[]                 = L"DoHanConvert";
    constexpr const wchar_t FontItalic[]                   = L"FontItalic";
    constexpr const wchar_t CustomTablePriority[]          = L"CustomTablePriority";
    constexpr const wchar_t DayiArticleMode[]              = L"DayiArticleMode";
    constexpr const wchar_t ArrayForceSP[]                 = L"ArrayForceSP";
    constexpr const wchar_t ArrayNotifySP[]                = L"ArrayNotifySP";
    constexpr const wchar_t ArraySingleQuoteCustomPhrase[] = L"ArraySingleQuoteCustomPhrase";
    constexpr const wchar_t Big5Filter[]                   = L"Big5Filter";

    // Integer / enum keys
    constexpr const wchar_t MaxCodes[]                     = L"MaxCodes";
    constexpr const wchar_t FontSize[]                     = L"FontSize";
    constexpr const wchar_t FontWeight[]                   = L"FontWeight";
    constexpr const wchar_t IMEShiftMode[]                 = L"IMEShiftMode";
    constexpr const wchar_t DoubleSingleByteMode[]         = L"DoubleSingleByteMode";
    constexpr const wchar_t ColorMode[]                    = L"ColorMode";
    constexpr const wchar_t NumericPad[]                   = L"NumericPad";
    constexpr const wchar_t ArrayScope[]                   = L"ArrayScope";
    constexpr const wchar_t PhoneticKeyboardLayout[]       = L"PhoneticKeyboardLayout";

    // String keys
    constexpr const wchar_t FontFaceName[]                 = L"FontFaceName";

    // Total count of non-color, non-CLSID keys (keys that have a UI control)
    constexpr int UI_KEY_COUNT = 30;
}

// IME mode string identifiers (used by --mode CLI parameter and CDIME::Show() migration)
namespace ImeModeStrings {
    constexpr const wchar_t Dayi[]     = L"dayi";
    constexpr const wchar_t Array[]    = L"array";
    constexpr const wchar_t Phonetic[] = L"phonetic";
    constexpr const wchar_t Generic[]  = L"generic";
}
