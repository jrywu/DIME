// CLI.cpp — Headless CLI implementation for DIMESettings.exe
//
// See CLI.h for public API documentation.

#include "framework.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <objbase.h>    // StringFromCLSID, CLSIDFromString
#include "CLI.h"
#include "..\define.h"
#include "..\Config.h"
#include "..\Globals.h"

// ---------------------------------------------------------------------------
// Key registry
// ---------------------------------------------------------------------------

static const KeyInfo g_keyRegistry[] =
{
    // Boolean keys
    { L"AutoCompose",                  BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"SpaceAsPageDown",              BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"SpaceAsFirstCandSelkey",       BOOL_T,   0,   1,   CLI_MASK_GENERIC   },
    { L"ArrowKeySWPages",              BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"ClearOnBeep",                  BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeep",                       BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeepNotify",                 BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeepOnCandi",                BOOL_T,   0,   1,   CLI_MASK_DAYI      },
    { L"ActivatedKeyboardMode",        BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"MakePhrase",                   BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"ShowNotifyDesktop",            BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoHanConvert",                 BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"FontItalic",                   BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"CustomTablePriority",          BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DayiArticleMode",              BOOL_T,   0,   1,   CLI_MASK_DAYI      },
    { L"ArrayForceSP",                 BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"ArrayNotifySP",                BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"ArraySingleQuoteCustomPhrase", BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"Big5Filter",                   BOOL_T,   0,   1,   CLI_MASK_NOT_ARRAY },
    // Integer / enum keys
    { L"MaxCodes",                     INT_T,    1,  20,   CLI_MASK_ALL       },
    { L"FontSize",                     INT_T,    8,  72,   CLI_MASK_ALL       },
    { L"FontWeight",                   INT_T,  100, 900,   CLI_MASK_ALL       },
    { L"IMEShiftMode",                 INT_T,    0,   3,   CLI_MASK_ALL       },
    { L"DoubleSingleByteMode",         INT_T,    0,   2,   CLI_MASK_ALL       },
    { L"ColorMode",                    INT_T,    0,   3,   CLI_MASK_ALL       },
    { L"NumericPad",                   INT_T,    0,   2,   CLI_MASK_ALL       },
    { L"ArrayScope",                   INT_T,    0,   5,   CLI_MASK_ARRAY     },
    { L"PhoneticKeyboardLayout",       INT_T,    0,   1,   CLI_MASK_PHONETIC  },
    // String keys
    { L"FontFaceName",                 STRING_T, 0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionDescription", STRING_T, 0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionCLSID",       CLSID_T,  0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionGUIDProfile", CLSID_T,  0,   0,   CLI_MASK_ALL       },
    // Custom-palette color keys (ColorMode=3)
    { L"ItemColor",                    COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"PhraseColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"NumberColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"ItemBGColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"SelectedItemColor",            COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"SelectedBGItemColor",          COLOR_T,  0,   0,   CLI_MASK_ALL       },
    // Light-palette color keys (ColorMode=1)
    { L"LightItemColor",               COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightPhraseColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightNumberColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightItemBGColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightSelectedItemColor",       COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightSelectedBGItemColor",     COLOR_T,  0,   0,   CLI_MASK_ALL       },
    // Dark-palette color keys (ColorMode=2)
    { L"DarkItemColor",                COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkPhraseColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkNumberColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkItemBGColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkSelectedItemColor",        COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkSelectedBGItemColor",      COLOR_T,  0,   0,   CLI_MASK_ALL       },
};

static const int g_keyCount = (int)(sizeof(g_keyRegistry) / sizeof(g_keyRegistry[0]));

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Find a key entry by name (case-insensitive).
static const KeyInfo* FindKey(const wchar_t* name)
{
    for (int i = 0; i < g_keyCount; i++)
        if (_wcsicmp(g_keyRegistry[i].name, name) == 0)
            return &g_keyRegistry[i];
    return nullptr;
}

// Returns true when the key is applicable to the given IME mode.
static bool KeyApplicableToMode(const KeyInfo* ki, IME_MODE mode)
{
    int bit = 0;
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     bit = CLI_MASK_DAYI;     break;
    case IME_MODE::IME_MODE_ARRAY:    bit = CLI_MASK_ARRAY;    break;
    case IME_MODE::IME_MODE_PHONETIC: bit = CLI_MASK_PHONETIC; break;
    case IME_MODE::IME_MODE_GENERIC:  bit = CLI_MASK_GENERIC;  break;
    default: return false;
    }
    return (ki->modeMask & bit) != 0;
}

// Returns a short string identifier for the mode (for error messages).
static const wchar_t* ModeName(IME_MODE mode)
{
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     return L"dayi";
    case IME_MODE::IME_MODE_ARRAY:    return L"array";
    case IME_MODE::IME_MODE_PHONETIC: return L"phonetic";
    case IME_MODE::IME_MODE_GENERIC:  return L"generic";
    default:                          return L"(none)";
    }
}

// Build %APPDATA%\DIME path into buf.
static void GetDimePath(WCHAR* buf, int bufLen)
{
    WCHAR appData[MAX_PATH] = {};
    SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, FALSE);
    StringCchPrintfW(buf, bufLen, L"%s\\DIME", appData);
}

// Build the path for the mode-specific custom table file.
static void GetCustomTablePath(IME_MODE mode, WCHAR* buf, int bufLen)
{
    WCHAR dimePath[MAX_PATH];
    GetDimePath(dimePath, MAX_PATH);
    const wchar_t* prefix = L"GENERIC";
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     prefix = L"DAYI";     break;
    case IME_MODE::IME_MODE_ARRAY:    prefix = L"ARRAY";    break;
    case IME_MODE::IME_MODE_PHONETIC: prefix = L"PHONETIC"; break;
    default: break;
    }
    StringCchPrintfW(buf, bufLen, L"%s\\%s-CUSTOM.txt", dimePath, prefix);
}

// Parse a hex colour string ("0xRRGGBB" or "RRGGBB") into a COLORREF.
// Returns false if the string cannot be parsed.
static bool ParseColorValue(const wchar_t* str, COLORREF& out)
{
    if (!str || str[0] == L'\0') return false;

    const wchar_t* p = str;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
        p += 2;

    wchar_t* end = nullptr;
    unsigned long val = wcstoul(p, &end, 16);
    if (end == p || *end != L'\0') return false;
    out = (COLORREF)val;
    return true;
}

// ---------------------------------------------------------------------------
// Read current value of a key into outBuf (config must already be loaded).
// Returns true on success.
// ---------------------------------------------------------------------------
static bool GetKeyValue(const KeyInfo* ki, WCHAR* outBuf, int bufLen)
{
    const wchar_t* n = ki->name;

    // --- Boolean ---
    if (_wcsicmp(n, L"AutoCompose") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetAutoCompose() ? 1 : 0);
    else if (_wcsicmp(n, L"SpaceAsPageDown") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetSpaceAsPageDown() ? 1 : 0);
    else if (_wcsicmp(n, L"SpaceAsFirstCandSelkey") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetSpaceAsFirstCaniSelkey() ? 1 : 0);
    else if (_wcsicmp(n, L"ArrowKeySWPages") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetArrowKeySWPages() ? 1 : 0);
    else if (_wcsicmp(n, L"ClearOnBeep") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetClearOnBeep() ? 1 : 0);
    else if (_wcsicmp(n, L"DoBeep") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetDoBeep() ? 1 : 0);
    else if (_wcsicmp(n, L"DoBeepNotify") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetDoBeepNotify() ? 1 : 0);
    else if (_wcsicmp(n, L"DoBeepOnCandi") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetDoBeepOnCandi() ? 1 : 0);
    else if (_wcsicmp(n, L"ActivatedKeyboardMode") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetActivatedKeyboardMode() ? 1 : 0);
    else if (_wcsicmp(n, L"MakePhrase") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetMakePhrase() ? 1 : 0);
    else if (_wcsicmp(n, L"ShowNotifyDesktop") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetShowNotifyDesktop() ? 1 : 0);
    else if (_wcsicmp(n, L"DoHanConvert") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetDoHanConvert() ? 1 : 0);
    else if (_wcsicmp(n, L"FontItalic") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetFontItalic() ? 1 : 0);
    else if (_wcsicmp(n, L"CustomTablePriority") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::getCustomTablePriority() ? 1 : 0);
    else if (_wcsicmp(n, L"DayiArticleMode") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::getDayiArticleMode() ? 1 : 0);
    else if (_wcsicmp(n, L"ArrayForceSP") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetArrayForceSP() ? 1 : 0);
    else if (_wcsicmp(n, L"ArrayNotifySP") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetArrayNotifySP() ? 1 : 0);
    else if (_wcsicmp(n, L"ArraySingleQuoteCustomPhrase") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetArraySingleQuoteCustomPhrase() ? 1 : 0);
    else if (_wcsicmp(n, L"Big5Filter") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", CConfig::GetBig5Filter() ? 1 : 0);
    // --- Integer / enum ---
    else if (_wcsicmp(n, L"MaxCodes") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%u", CConfig::GetMaxCodes());
    else if (_wcsicmp(n, L"FontSize") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%u", CConfig::GetFontSize());
    else if (_wcsicmp(n, L"FontWeight") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%u", CConfig::GetFontWeight());
    else if (_wcsicmp(n, L"IMEShiftMode") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::GetIMEShiftMode());
    else if (_wcsicmp(n, L"DoubleSingleByteMode") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::GetDoubleSingleByteMode());
    else if (_wcsicmp(n, L"ColorMode") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::GetColorMode());
    else if (_wcsicmp(n, L"NumericPad") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::GetNumericPad());
    else if (_wcsicmp(n, L"ArrayScope") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::GetArrayScope());
    else if (_wcsicmp(n, L"PhoneticKeyboardLayout") == 0)
        StringCchPrintfW(outBuf, bufLen, L"%d", (int)CConfig::getPhoneticKeyboardLayout());
    // --- String ---
    else if (_wcsicmp(n, L"FontFaceName") == 0)
        StringCchCopyW(outBuf, bufLen, CConfig::GetFontFaceName());
    else if (_wcsicmp(n, L"ReverseConversionDescription") == 0)
    {
        WCHAR* desc = CConfig::GetReverseConversionDescription();
        StringCchCopyW(outBuf, bufLen, desc ? desc : L"");
    }
    // --- CLSID / GUID ---
    else if (_wcsicmp(n, L"ReverseConversionCLSID") == 0)
    {
        BSTR bstr = nullptr;
        if (SUCCEEDED(StringFromCLSID(CConfig::GetReverseConverstionCLSID(), &bstr)) && bstr)
        {
            StringCchCopyW(outBuf, bufLen, bstr);
            CoTaskMemFree(bstr);
        }
        else
            StringCchCopyW(outBuf, bufLen, L"");
    }
    else if (_wcsicmp(n, L"ReverseConversionGUIDProfile") == 0)
    {
        BSTR bstr = nullptr;
        if (SUCCEEDED(StringFromCLSID(CConfig::GetReverseConversionGUIDProfile(), &bstr)) && bstr)
        {
            StringCchCopyW(outBuf, bufLen, bstr);
            CoTaskMemFree(bstr);
        }
        else
            StringCchCopyW(outBuf, bufLen, L"");
    }
    // --- Colour keys ---
    else if (_wcsicmp(n, L"ItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetItemColor());
    else if (_wcsicmp(n, L"PhraseColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetPhraseColor());
    else if (_wcsicmp(n, L"NumberColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetNumberColor());
    else if (_wcsicmp(n, L"ItemBGColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetItemBGColor());
    else if (_wcsicmp(n, L"SelectedItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetSelectedColor());
    else if (_wcsicmp(n, L"SelectedBGItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetSelectedBGColor());
    else if (_wcsicmp(n, L"LightItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightItemColor());
    else if (_wcsicmp(n, L"LightPhraseColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightPhraseColor());
    else if (_wcsicmp(n, L"LightNumberColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightNumberColor());
    else if (_wcsicmp(n, L"LightItemBGColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightItemBGColor());
    else if (_wcsicmp(n, L"LightSelectedItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightSelectedColor());
    else if (_wcsicmp(n, L"LightSelectedBGItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetLightSelectedBGColor());
    else if (_wcsicmp(n, L"DarkItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkItemColor());
    else if (_wcsicmp(n, L"DarkPhraseColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkPhraseColor());
    else if (_wcsicmp(n, L"DarkNumberColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkNumberColor());
    else if (_wcsicmp(n, L"DarkItemBGColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkItemBGColor());
    else if (_wcsicmp(n, L"DarkSelectedItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkSelectedColor());
    else if (_wcsicmp(n, L"DarkSelectedBGItemColor") == 0)
        StringCchPrintfW(outBuf, bufLen, L"0x%06X", CConfig::GetDarkSelectedBGColor());
    else
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Apply a key=value string pair to the in-memory config.
// Config must already be loaded before calling.
// Returns 0 on success, 1 on invalid value, 2 on allocation failure.
// ---------------------------------------------------------------------------
static int ApplyKeyValue(const KeyInfo* ki, const wchar_t* valueStr, FILE* err)
{
    const wchar_t* n = ki->name;

    if (ki->type == BOOL_T || ki->type == INT_T)
    {
        wchar_t* end = nullptr;
        long v = wcstol(valueStr, &end, 10);
        if (end == valueStr || *end != L'\0')
        {
            fwprintf_s(err, L"Error: value '%s' is not a valid integer.\n", valueStr);
            return 1;
        }
        if (v < ki->minVal || v > ki->maxVal)
        {
            fwprintf_s(err, L"Error: value %ld is out of range [%d, %d] for key '%s'.\n",
                v, ki->minVal, ki->maxVal, ki->name);
            return 1;
        }
        // Apply to in-memory config
        if      (_wcsicmp(n, L"AutoCompose") == 0)              CConfig::SetAutoCompose((BOOL)v);
        else if (_wcsicmp(n, L"SpaceAsPageDown") == 0)          CConfig::SetSpaceAsPageDown((BOOL)v);
        else if (_wcsicmp(n, L"SpaceAsFirstCandSelkey") == 0)   CConfig::SetSpaceAsFirstCaniSelkey((BOOL)v);
        else if (_wcsicmp(n, L"ArrowKeySWPages") == 0)          CConfig::SetArrowKeySWPages((BOOL)v);
        else if (_wcsicmp(n, L"ClearOnBeep") == 0)              CConfig::SetClearOnBeep((BOOL)v);
        else if (_wcsicmp(n, L"DoBeep") == 0)                   CConfig::SetDoBeep((BOOL)v);
        else if (_wcsicmp(n, L"DoBeepNotify") == 0)             CConfig::SetDoBeepNotify((BOOL)v);
        else if (_wcsicmp(n, L"DoBeepOnCandi") == 0)            CConfig::SetDoBeepOnCandi((BOOL)v);
        else if (_wcsicmp(n, L"ActivatedKeyboardMode") == 0)    CConfig::SetActivatedKeyboardMode((BOOL)v);
        else if (_wcsicmp(n, L"MakePhrase") == 0)               CConfig::SetMakePhrase((BOOL)v);
        else if (_wcsicmp(n, L"ShowNotifyDesktop") == 0)        CConfig::SetShowNotifyDesktop((BOOL)v);
        else if (_wcsicmp(n, L"DoHanConvert") == 0)             CConfig::SetDoHanConvert((BOOL)v);
        else if (_wcsicmp(n, L"FontItalic") == 0)               CConfig::SetFontItalic((BOOL)v);
        else if (_wcsicmp(n, L"CustomTablePriority") == 0)      CConfig::setCustomTablePriority((BOOL)v);
        else if (_wcsicmp(n, L"DayiArticleMode") == 0)          CConfig::setDayiArticleMode((BOOL)v);
        else if (_wcsicmp(n, L"ArrayForceSP") == 0)             CConfig::SetArrayForceSP((BOOL)v);
        else if (_wcsicmp(n, L"ArrayNotifySP") == 0)            CConfig::SetArrayNotifySP((BOOL)v);
        else if (_wcsicmp(n, L"ArraySingleQuoteCustomPhrase") == 0) CConfig::SetArraySingleQuoteCustomPhrase((BOOL)v);
        else if (_wcsicmp(n, L"Big5Filter") == 0)               CConfig::SetBig5Filter((BOOL)v);
        else if (_wcsicmp(n, L"MaxCodes") == 0)                 CConfig::SetMaxCodes((UINT)v);
        else if (_wcsicmp(n, L"FontSize") == 0)                 CConfig::SetFontSize((UINT)v);
        else if (_wcsicmp(n, L"FontWeight") == 0)               CConfig::SetFontWeight((UINT)v);
        else if (_wcsicmp(n, L"IMEShiftMode") == 0)             CConfig::SetIMEShiftMode((IME_SHIFT_MODE)v);
        else if (_wcsicmp(n, L"DoubleSingleByteMode") == 0)     CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)v);
        else if (_wcsicmp(n, L"ColorMode") == 0)                CConfig::SetColorMode((IME_COLOR_MODE)v);
        else if (_wcsicmp(n, L"NumericPad") == 0)               CConfig::SetNumericPad((NUMERIC_PAD)v);
        else if (_wcsicmp(n, L"ArrayScope") == 0)               CConfig::SetArrayScope((ARRAY_SCOPE)v);
        else if (_wcsicmp(n, L"PhoneticKeyboardLayout") == 0)   CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT)v);
        else return 1; // should not reach here
    }
    else if (ki->type == COLOR_T)
    {
        COLORREF c = 0;
        if (!ParseColorValue(valueStr, c))
        {
            fwprintf_s(err, L"Error: '%s' is not a valid hex colour (e.g. 0xRRGGBB).\n", valueStr);
            return 1;
        }
        if      (_wcsicmp(n, L"ItemColor") == 0)               CConfig::SetItemColor(c);
        else if (_wcsicmp(n, L"PhraseColor") == 0)             CConfig::SetPhraseColor(c);
        else if (_wcsicmp(n, L"NumberColor") == 0)             CConfig::SetNumberColor(c);
        else if (_wcsicmp(n, L"ItemBGColor") == 0)             CConfig::SetItemBGColor(c);
        else if (_wcsicmp(n, L"SelectedItemColor") == 0)       CConfig::SetSelectedColor(c);
        else if (_wcsicmp(n, L"SelectedBGItemColor") == 0)     CConfig::SetSelectedBGColor(c);
        else if (_wcsicmp(n, L"LightItemColor") == 0)          CConfig::SetLightItemColor(c);
        else if (_wcsicmp(n, L"LightPhraseColor") == 0)        CConfig::SetLightPhraseColor(c);
        else if (_wcsicmp(n, L"LightNumberColor") == 0)        CConfig::SetLightNumberColor(c);
        else if (_wcsicmp(n, L"LightItemBGColor") == 0)        CConfig::SetLightItemBGColor(c);
        else if (_wcsicmp(n, L"LightSelectedItemColor") == 0)  CConfig::SetLightSelectedColor(c);
        else if (_wcsicmp(n, L"LightSelectedBGItemColor") == 0)CConfig::SetLightSelectedBGColor(c);
        else if (_wcsicmp(n, L"DarkItemColor") == 0)           CConfig::SetDarkItemColor(c);
        else if (_wcsicmp(n, L"DarkPhraseColor") == 0)         CConfig::SetDarkPhraseColor(c);
        else if (_wcsicmp(n, L"DarkNumberColor") == 0)         CConfig::SetDarkNumberColor(c);
        else if (_wcsicmp(n, L"DarkItemBGColor") == 0)         CConfig::SetDarkItemBGColor(c);
        else if (_wcsicmp(n, L"DarkSelectedItemColor") == 0)   CConfig::SetDarkSelectedColor(c);
        else if (_wcsicmp(n, L"DarkSelectedBGItemColor") == 0) CConfig::SetDarkSelectedBGColor(c);
        else return 1;
    }
    else if (ki->type == STRING_T)
    {
        if (_wcsicmp(n, L"FontFaceName") == 0)
        {
            WCHAR buf[LF_FACESIZE] = {};
            StringCchCopyW(buf, LF_FACESIZE, valueStr);
            CConfig::SetFontFaceName(buf);
        }
        else if (_wcsicmp(n, L"ReverseConversionDescription") == 0)
        {
            size_t len = wcslen(valueStr) + 1;
            WCHAR* p = new (std::nothrow) WCHAR[len];
            if (!p) return 2;
            StringCchCopyW(p, len, valueStr);
            // Free previous description before overwriting (plug pre-existing leak).
            WCHAR* old = CConfig::GetReverseConversionDescription();
            if (old) delete[] old;
            CConfig::SetReverseConversionDescription(p);
            CConfig::SetReloadReverseConversion(TRUE);
        }
        else return 1;
    }
    else if (ki->type == CLSID_T)
    {
        CLSID clsid = CLSID_NULL;
        if (wcslen(valueStr) > 0)
        {
            HRESULT hr = CLSIDFromString(valueStr, &clsid);
            if (FAILED(hr))
            {
                fwprintf_s(err, L"Error: '%s' is not a valid GUID.\n", valueStr);
                return 1;
            }
        }
        if (_wcsicmp(n, L"ReverseConversionCLSID") == 0)
        {
            CConfig::SetReverseConverstionCLSID(clsid);
            CConfig::SetReloadReverseConversion(TRUE);
        }
        else if (_wcsicmp(n, L"ReverseConversionGUIDProfile") == 0)
        {
            CConfig::SetReverseConversionGUIDProfile(clsid);
            CConfig::SetReloadReverseConversion(TRUE);
        }
        else return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --help / -h
// ---------------------------------------------------------------------------
static int HandleHelp(FILE* out)
{
    fwprintf_s(out,
        L"DIMESettings.exe [--mode <mode>] <command> [options]\n"
        L"\n"
        L"GUI launch:\n"
        L"  (no arguments)                    Open settings UI (default: dayi tab)\n"
        L"  --mode <mode>                     Open settings UI to specific IME tab\n"
        L"  --reset-ui                        Reset window position/size to defaults\n"
        L"\n"
        L"Modes:\n"
        L"  dayi, array, phonetic, generic\n"
        L"\n"
        L"CLI commands:\n"
        L"  --help, -h                        Show this help message\n"
        L"  --list-modes                      List available IME modes\n"
        L"  --list-keys                       List all keys for the selected mode\n"
        L"  --get <key>                       Get the value of a setting key\n"
        L"  --get-all                         Get all settings for the selected mode\n"
        L"  --set <key> <value> [--set ...]   Set one or more settings\n"
        L"  --reset                           Reset all settings to defaults\n"
        L"  --import-custom <file>            Import a custom phrase table\n"
        L"  --export-custom <file>            Export the custom phrase table\n"
        L"  --load-main <file>                Load a main dictionary (.cin)\n"
        L"  --load-phrase <file>              Load a phrase dictionary (.cin)\n"
        L"  --load-array <table> <file>       Load an Array supplementary table\n"
        L"                                    tables: sp, sc, ext-b, ext-cd,\n"
        L"                                            ext-efg, array40, phrase\n"
        L"\n"
        L"Options:\n"
        L"  --json                            Output in JSON format\n"
        L"  --silent                          Suppress all output\n"
        L"\n"
        L"Exit codes: 0=success, 1=invalid argument, 2=I/O error,\n"
        L"            3=key not applicable to mode\n"
        L"\n"
        L"Examples:\n"
        L"  DIMESettings.exe                                    (open UI, dayi tab)\n"
        L"  DIMESettings.exe --mode array                       (open UI, array tab)\n"
        L"  DIMESettings.exe --reset-ui --mode phonetic         (reset window, phonetic tab)\n"
        L"  DIMESettings.exe --mode dayi --get FontSize\n"
        L"  DIMESettings.exe --mode array --set ColorMode 2 --set FontSize 16\n"
        L"  DIMESettings.exe --mode phonetic --get-all --json\n"
        L"  DIMESettings.exe --mode dayi --reset\n"
    );
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --list-modes
// ---------------------------------------------------------------------------
static int HandleListModes(const CLIArgs& args, FILE* out)
{
    if (args.jsonOutput)
        fwprintf_s(out, L"[\"dayi\",\"array\",\"phonetic\",\"generic\"]\n");
    else
        fwprintf_s(out, L"dayi\narray\nphonetic\ngeneric\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --list-keys
// ---------------------------------------------------------------------------
static int HandleListKeys(const CLIArgs& args, FILE* out, FILE* err)
{
    CConfig::LoadConfig(args.mode);
    int bit = 0;
    switch (args.mode)
    {
    case IME_MODE::IME_MODE_DAYI:     bit = CLI_MASK_DAYI;     break;
    case IME_MODE::IME_MODE_ARRAY:    bit = CLI_MASK_ARRAY;    break;
    case IME_MODE::IME_MODE_PHONETIC: bit = CLI_MASK_PHONETIC; break;
    case IME_MODE::IME_MODE_GENERIC:  bit = CLI_MASK_GENERIC;  break;
    default:
        fwprintf_s(err, L"Error: invalid mode.\n");
        return 1;
    }

    WCHAR valBuf[512];
    if (args.jsonOutput)
    {
        fwprintf_s(out, L"{\n  \"mode\": \"%s\",\n  \"keys\": [\n", ModeName(args.mode));
        bool first = true;
        for (int i = 0; i < g_keyCount; i++)
        {
            const KeyInfo& ki = g_keyRegistry[i];
            if (!(ki.modeMask & bit)) continue;
            if (!GetKeyValue(&ki, valBuf, 512)) continue;
            if (!first) fwprintf_s(out, L",\n");
            first = false;
            fwprintf_s(out, L"    {\"key\":\"%s\",\"value\":\"%s\"}",
                ki.name, valBuf);
        }
        fwprintf_s(out, L"\n  ]\n}\n");
    }
    else
    {
        for (int i = 0; i < g_keyCount; i++)
        {
            const KeyInfo& ki = g_keyRegistry[i];
            if (!(ki.modeMask & bit)) continue;
            if (!GetKeyValue(&ki, valBuf, 512)) continue;
            fwprintf_s(out, L"%s=%s\n", ki.name, valBuf);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --get
// ---------------------------------------------------------------------------
static int HandleGet(const CLIArgs& args, FILE* out, FILE* err)
{
    CConfig::LoadConfig(args.mode);
    const KeyInfo* ki = FindKey(args.getKey);
    if (!ki)
    {
        fwprintf_s(err, L"Error: unknown key '%s'. Use --list-keys to see valid keys.\n",
            args.getKey);
        return 1;
    }
    if (!KeyApplicableToMode(ki, args.mode))
    {
        fwprintf_s(err, L"Error: key '%s' is not applicable to mode '%s'.\n",
            args.getKey, ModeName(args.mode));
        return 3;
    }
    WCHAR valBuf[512] = {};
    if (!GetKeyValue(ki, valBuf, 512))
    {
        fwprintf_s(err, L"Error: failed to read key '%s'.\n", args.getKey);
        return 2;
    }
    if (args.jsonOutput)
        fwprintf_s(out, L"{\"%s\":\"%s\"}\n", ki->name, valBuf);
    else
        fwprintf_s(out, L"%s=%s\n", ki->name, valBuf);
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --get-all
// ---------------------------------------------------------------------------
static int HandleGetAll(const CLIArgs& args, FILE* out, FILE* err)
{
    CConfig::LoadConfig(args.mode);
    int bit = 0;
    switch (args.mode)
    {
    case IME_MODE::IME_MODE_DAYI:     bit = CLI_MASK_DAYI;     break;
    case IME_MODE::IME_MODE_ARRAY:    bit = CLI_MASK_ARRAY;    break;
    case IME_MODE::IME_MODE_PHONETIC: bit = CLI_MASK_PHONETIC; break;
    case IME_MODE::IME_MODE_GENERIC:  bit = CLI_MASK_GENERIC;  break;
    default:
        fwprintf_s(err, L"Error: invalid mode.\n");
        return 1;
    }

    WCHAR valBuf[512];
    if (args.jsonOutput)
    {
        fwprintf_s(out, L"{\n  \"mode\": \"%s\"", ModeName(args.mode));
        for (int i = 0; i < g_keyCount; i++)
        {
            const KeyInfo& ki = g_keyRegistry[i];
            if (!(ki.modeMask & bit)) continue;
            if (!GetKeyValue(&ki, valBuf, 512)) continue;
            // Quote all values in JSON for simplicity
            fwprintf_s(out, L",\n  \"%s\": \"%s\"", ki.name, valBuf);
        }
        fwprintf_s(out, L"\n}\n");
    }
    else
    {
        for (int i = 0; i < g_keyCount; i++)
        {
            const KeyInfo& ki = g_keyRegistry[i];
            if (!(ki.modeMask & bit)) continue;
            if (!GetKeyValue(&ki, valBuf, 512)) continue;
            fwprintf_s(out, L"%s=%s\n", ki.name, valBuf);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --set
// ---------------------------------------------------------------------------
static int HandleSet(const CLIArgs& args, FILE* out, FILE* err)
{
    CConfig::LoadConfig(args.mode);

    // Validate all pairs first, then apply, so we don't partially update on error.
    for (int i = 0; i < args.setCount; i++)
    {
        const KeyInfo* ki = FindKey(args.setPairs[i].key);
        if (!ki)
        {
            fwprintf_s(err, L"Error: unknown key '%s'.\n", args.setPairs[i].key);
            return 1;
        }
        if (!KeyApplicableToMode(ki, args.mode))
        {
            fwprintf_s(err, L"Error: key '%s' is not applicable to mode '%s'.\n",
                args.setPairs[i].key, ModeName(args.mode));
            return 3;
        }
    }

    for (int i = 0; i < args.setCount; i++)
    {
        const KeyInfo* ki = FindKey(args.setPairs[i].key);
        int rc = ApplyKeyValue(ki, args.setPairs[i].value, err);
        if (rc != 0) return rc;
    }

    // Write config (FALSE = skip concurrent-modification check)
    CConfig::WriteConfig(args.mode, FALSE);
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --reset  (restore defaults then save)
// ---------------------------------------------------------------------------
static int HandleReset(const CLIArgs& args, FILE* out, FILE* err)
{
    // LoadConfig sets up the INI file path via SetIMEMode.
    // We must call it before WriteConfig so the path is known.
    // The file may be read into in-memory state here, but ResetAllDefaults()
    // immediately overwrites every field with its initial default value, so
    // any previously loaded values (e.g. FontSize=30) are discarded.
    CConfig::LoadConfig(args.mode);
    CConfig::ResetAllDefaults(args.mode);
    CConfig::WriteConfig(args.mode, FALSE);
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --import-custom <file>
// ---------------------------------------------------------------------------
static int HandleImportCustom(const CLIArgs& args, FILE* out, FILE* err)
{
    // Verify the source file exists before calling parseCINFile
    if (!PathFileExistsW(args.filePath))
    {
        fwprintf_s(err, L"Error: file not found: %s\n", args.filePath);
        return 2;
    }

    WCHAR destPath[MAX_PATH];
    GetCustomTablePath(args.mode, destPath, MAX_PATH);

    if (!CConfig::parseCINFile(args.filePath, destPath, FALSE, TRUE))
    {
        fwprintf_s(err, L"Error: failed to import custom table from %s\n", args.filePath);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --export-custom <file>
// ---------------------------------------------------------------------------
static int HandleExportCustom(const CLIArgs& args, FILE* out, FILE* err)
{
    WCHAR srcPath[MAX_PATH];
    GetCustomTablePath(args.mode, srcPath, MAX_PATH);

    if (!PathFileExistsW(srcPath))
    {
        fwprintf_s(err, L"Error: no custom table found for mode '%s'.\n", ModeName(args.mode));
        return 2;
    }
    if (!CopyFileW(srcPath, args.filePath, FALSE))
    {
        fwprintf_s(err, L"Error: could not write to %s (error %lu)\n",
            args.filePath, GetLastError());
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --load-main <file>
// ---------------------------------------------------------------------------
static int HandleLoadMain(const CLIArgs& args, FILE* out, FILE* err)
{
    if (!PathFileExistsW(args.filePath))
    {
        fwprintf_s(err, L"Error: file not found: %s\n", args.filePath);
        return 2;
    }

    WCHAR dimePath[MAX_PATH];
    GetDimePath(dimePath, MAX_PATH);
    WCHAR destPath[MAX_PATH];

    switch (args.mode)
    {
    case IME_MODE::IME_MODE_DAYI:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Dayi.cin", dimePath); break;
    case IME_MODE::IME_MODE_ARRAY:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array.cin", dimePath); break;
    case IME_MODE::IME_MODE_PHONETIC:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Phone.cin", dimePath); break;
    case IME_MODE::IME_MODE_GENERIC:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Generic.cin", dimePath); break;
    default:
        fwprintf_s(err, L"Error: invalid mode for --load-main.\n");
        return 1;
    }

    if (!CConfig::parseCINFile(args.filePath, destPath, FALSE, TRUE))
    {
        fwprintf_s(err, L"Error: failed to load main table from %s\n", args.filePath);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --load-phrase <file>
// ---------------------------------------------------------------------------
static int HandleLoadPhrase(const CLIArgs& args, FILE* out, FILE* err)
{
    if (!PathFileExistsW(args.filePath))
    {
        fwprintf_s(err, L"Error: file not found: %s\n", args.filePath);
        return 2;
    }

    WCHAR dimePath[MAX_PATH];
    GetDimePath(dimePath, MAX_PATH);
    WCHAR destPath[MAX_PATH];
    StringCchPrintfW(destPath, MAX_PATH, L"%s\\Phrase.cin", dimePath);

    if (!CConfig::parseCINFile(args.filePath, destPath, FALSE, TRUE))
    {
        fwprintf_s(err, L"Error: failed to load phrase table from %s\n", args.filePath);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: --load-array <table-name>
// ---------------------------------------------------------------------------
static int HandleLoadArray(const CLIArgs& args, FILE* out, FILE* err)
{
    WCHAR dimePath[MAX_PATH];
    GetDimePath(dimePath, MAX_PATH);
    WCHAR destPath[MAX_PATH];

    switch (args.arrayTable)
    {
    case CLI_TABLE_SP:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-special.cin", dimePath); break;
    case CLI_TABLE_SC:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-shortcode.cin", dimePath); break;
    case CLI_TABLE_EXT_B:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-Ext-B.cin", dimePath); break;
    case CLI_TABLE_EXT_CD:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-Ext-CD.cin", dimePath); break;
    case CLI_TABLE_EXT_EFG:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-Ext-EF.cin", dimePath); break;
    case CLI_TABLE_ARRAY40:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array40.cin", dimePath); break;
    case CLI_TABLE_PHRASE:
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\Array-Phrase.cin", dimePath); break;
    default:
        fwprintf_s(err, L"Error: invalid --load-array table.\n");
        return 1;
    }

    // --load-array receives a source file path in args.filePath
    if (!PathFileExistsW(args.filePath))
    {
        fwprintf_s(err, L"Error: file not found: %s\n", args.filePath);
        return 2;
    }

    if (!CConfig::parseCINFile(args.filePath, destPath, FALSE, TRUE))
    {
        fwprintf_s(err, L"Error: failed to load array table from %s\n", args.filePath);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ParseCLIArgs — public
// ---------------------------------------------------------------------------
bool ParseCLIArgs(_In_ const wchar_t* cmdLine, _Out_ CLIArgs& args, _In_ FILE* err)
{
    ZeroMemory(&args, sizeof(args));
    args.command    = CLI_NONE;
    args.mode       = IME_MODE::IME_MODE_NONE;
    args.hasMode    = FALSE;
    args.jsonOutput = FALSE;
    args.silent     = FALSE;
    args.setCount   = 0;
    args.arrayTable = CLI_TABLE_NONE;

    int    argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return false;

    bool ok = true;
    for (int i = 0; i < argc && ok; i++)
    {
        const wchar_t* arg = argv[i];

        if (_wcsicmp(arg, L"--mode") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --mode requires an argument.\n"); ok = false; break; }
            i++;
            const wchar_t* m = argv[i];
            if      (_wcsicmp(m, L"dayi")     == 0) args.mode = IME_MODE::IME_MODE_DAYI;
            else if (_wcsicmp(m, L"array")    == 0) args.mode = IME_MODE::IME_MODE_ARRAY;
            else if (_wcsicmp(m, L"phonetic") == 0) args.mode = IME_MODE::IME_MODE_PHONETIC;
            else if (_wcsicmp(m, L"generic")  == 0) args.mode = IME_MODE::IME_MODE_GENERIC;
            else { fwprintf_s(err, L"Error: unknown mode '%s'.\n", m); ok = false; break; }
            args.hasMode = TRUE;
        }
        else if (_wcsicmp(arg, L"--json")        == 0) args.jsonOutput = TRUE;
        else if (_wcsicmp(arg, L"--silent")      == 0) args.silent     = TRUE;
        else if (_wcsicmp(arg, L"--list-modes")  == 0) args.command    = CLI_LIST_MODES;
        else if (_wcsicmp(arg, L"--list-keys")   == 0) args.command    = CLI_LIST_KEYS;
        else if (_wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"-h") == 0) args.command = CLI_HELP;
        else if (_wcsicmp(arg, L"--get-all")     == 0) args.command    = CLI_GET_ALL;
        else if (_wcsicmp(arg, L"--reset")       == 0) args.command    = CLI_RESET;
        else if (_wcsicmp(arg, L"--get") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --get requires a key name.\n"); ok = false; break; }
            i++;
            args.command = CLI_GET;
            StringCchCopyW(args.getKey, _countof(args.getKey), argv[i]);
        }
        else if (_wcsicmp(arg, L"--set") == 0)
        {
            if (i + 2 >= argc)
            { fwprintf_s(err, L"Error: --set requires a key and a value.\n"); ok = false; break; }
            if (args.setCount >= CLI_MAX_SET_PAIRS)
            { fwprintf_s(err, L"Error: too many --set pairs (max %d).\n", CLI_MAX_SET_PAIRS); ok = false; break; }
            i++;
            StringCchCopyW(args.setPairs[args.setCount].key,   _countof(args.setPairs[0].key),   argv[i]);
            i++;
            StringCchCopyW(args.setPairs[args.setCount].value, _countof(args.setPairs[0].value), argv[i]);
            args.setCount++;
            args.command = CLI_SET;
        }
        else if (_wcsicmp(arg, L"--import-custom") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --import-custom requires a file path.\n"); ok = false; break; }
            i++;
            args.command = CLI_IMPORT_CUSTOM;
            StringCchCopyW(args.filePath, MAX_PATH, argv[i]);
        }
        else if (_wcsicmp(arg, L"--export-custom") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --export-custom requires a file path.\n"); ok = false; break; }
            i++;
            args.command = CLI_EXPORT_CUSTOM;
            StringCchCopyW(args.filePath, MAX_PATH, argv[i]);
        }
        else if (_wcsicmp(arg, L"--load-main") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --load-main requires a file path.\n"); ok = false; break; }
            i++;
            args.command = CLI_LOAD_MAIN;
            StringCchCopyW(args.filePath, MAX_PATH, argv[i]);
        }
        else if (_wcsicmp(arg, L"--load-phrase") == 0)
        {
            if (i + 1 >= argc) { fwprintf_s(err, L"Error: --load-phrase requires a file path.\n"); ok = false; break; }
            i++;
            args.command = CLI_LOAD_PHRASE;
            StringCchCopyW(args.filePath, MAX_PATH, argv[i]);
        }
        else if (_wcsicmp(arg, L"--load-array") == 0)
        {
            if (i + 2 >= argc)
            { fwprintf_s(err, L"Error: --load-array requires a table name and a file path.\n"); ok = false; break; }
            i++;
            const wchar_t* tbl = argv[i];
            if      (_wcsicmp(tbl, L"sp")      == 0) args.arrayTable = CLI_TABLE_SP;
            else if (_wcsicmp(tbl, L"sc")      == 0) args.arrayTable = CLI_TABLE_SC;
            else if (_wcsicmp(tbl, L"ext-b")   == 0) args.arrayTable = CLI_TABLE_EXT_B;
            else if (_wcsicmp(tbl, L"ext-cd")  == 0) args.arrayTable = CLI_TABLE_EXT_CD;
            else if (_wcsicmp(tbl, L"ext-efg") == 0) args.arrayTable = CLI_TABLE_EXT_EFG;
            else if (_wcsicmp(tbl, L"array40") == 0) args.arrayTable = CLI_TABLE_ARRAY40;
            else if (_wcsicmp(tbl, L"phrase")  == 0) args.arrayTable = CLI_TABLE_PHRASE;
            else { fwprintf_s(err, L"Error: unknown array table '%s'.\n", tbl); ok = false; break; }
            i++;
            args.command = CLI_LOAD_ARRAY;
            StringCchCopyW(args.filePath, MAX_PATH, argv[i]);
        }
        else
        {
            fwprintf_s(err, L"Error: unknown argument '%s'.\n", arg);
            ok = false;
        }
    }

    LocalFree(argv);
    return ok;
}

// ---------------------------------------------------------------------------
// RunCLI — public entry point
// ---------------------------------------------------------------------------
int RunCLI(_In_ const wchar_t* cmdLine, _In_ FILE* out)
{
    FILE* err = stderr;

    CLIArgs args;
    if (!ParseCLIArgs(cmdLine, args, err))
        return 1;

    if (args.command == CLI_NONE)
    {
        fwprintf_s(err, L"Error: no command specified. Use --help for usage.\n");
        return 1;
    }

    // --silent: send output to NUL
    FILE* nullFile = nullptr;
    if (args.silent)
    {
        _wfopen_s(&nullFile, L"NUL", L"w");
        out = nullFile ? nullFile : out;
    }

    // --mode is required for all commands except --list-modes and --help
    if (args.command != CLI_LIST_MODES && args.command != CLI_HELP && !args.hasMode)
    {
        fwprintf_s(err, L"Error: --mode is required for this command.\n");
        if (nullFile) fclose(nullFile);
        return 1;
    }

    int rc = 0;
    switch (args.command)
    {
    case CLI_HELP:         rc = HandleHelp(out);                           break;
    case CLI_LIST_MODES:   rc = HandleListModes(args, out);               break;
    case CLI_LIST_KEYS:    rc = HandleListKeys(args, out, err);           break;
    case CLI_GET:          rc = HandleGet(args, out, err);                break;
    case CLI_GET_ALL:      rc = HandleGetAll(args, out, err);             break;
    case CLI_SET:          rc = HandleSet(args, out, err);                break;
    case CLI_RESET:        rc = HandleReset(args, out, err);              break;
    case CLI_IMPORT_CUSTOM:rc = HandleImportCustom(args, out, err);       break;
    case CLI_EXPORT_CUSTOM:rc = HandleExportCustom(args, out, err);       break;
    case CLI_LOAD_MAIN:    rc = HandleLoadMain(args, out, err);           break;
    case CLI_LOAD_PHRASE:  rc = HandleLoadPhrase(args, out, err);         break;
    case CLI_LOAD_ARRAY:   rc = HandleLoadArray(args, out, err);          break;
    default:               rc = 1;                                         break;
    }

    if (nullFile) fclose(nullFile);
    return rc;
}

// ---------------------------------------------------------------------------
// Unit-test shims (only compiled when DIME_UNIT_TESTING is defined)
// ---------------------------------------------------------------------------
#ifdef DIME_UNIT_TESTING
const KeyInfo* CLI_FindKey(const wchar_t* name)            { return FindKey(name); }
bool CLI_KeyApplicableToMode(const KeyInfo* ki, IME_MODE m){ return KeyApplicableToMode(ki, m); }
bool CLI_ParseColorValue(const wchar_t* str, COLORREF& out){ return ParseColorValue(str, out); }
int  CLI_GetKeyCount()                                      { return g_keyCount; }
const KeyInfo* CLI_GetKeyRegistry()                         { return g_keyRegistry; }
#endif
