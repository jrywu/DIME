//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#pragma once
#include <map>
#include "private.h"
#include "define.h"
#include "TSFDayiBaseStructure.h"
using std::map;
void DllAddRef();
void DllRelease();


namespace Global {
//---------------------------------------------------------------------
// inline
//---------------------------------------------------------------------

inline void SafeRelease(_In_ IUnknown *punk)
{
    if (punk != nullptr)
    {
        punk->Release();
    }
}

inline void QuickVariantInit(_Inout_ VARIANT *pvar)
{
    pvar->vt = VT_EMPTY;
}

inline void QuickVariantClear(_Inout_ VARIANT *pvar)
{
    switch (pvar->vt) 
    {
    // some ovbious VTs that don't need to call VariantClear.
    case VT_EMPTY:
    case VT_NULL:
    case VT_I2:
    case VT_I4:
    case VT_R4:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I1:
    case VT_UI1:
    case VT_UI2:
    case VT_UI4:
    case VT_I8:
    case VT_UI8:
    case VT_INT:
    case VT_UINT:
    case VT_BOOL:
        break;

        // Call release for VT_UNKNOWN.
    case VT_UNKNOWN:
        SafeRelease(pvar->punkVal);
        break;

    default:
        // we call OleAut32 for other VTs.
        VariantClear(pvar);
        break;
    }
    pvar->vt = VT_EMPTY;
}

//+---------------------------------------------------------------------------
//
// IsTooSimilar
//
//  Return TRUE if the colors cr1 and cr2 are so similar that they
//  are hard to distinguish. Used for deciding to use reverse video
//  selection instead of system selection colors.
//
//----------------------------------------------------------------------------

inline BOOL IsTooSimilar(COLORREF cr1, COLORREF cr2)
{
    if ((cr1 | cr2) & 0xFF000000)        // One color and/or the other isn't RGB, so algorithm doesn't apply
    {
        return FALSE;
    }

    LONG DeltaR = abs(GetRValue(cr1) - GetRValue(cr2));
    LONG DeltaG = abs(GetGValue(cr1) - GetGValue(cr2));
    LONG DeltaB = abs(GetBValue(cr1) - GetBValue(cr2));

    return DeltaR + DeltaG + DeltaB < 80;
}

//---------------------------------------------------------------------
// extern
//---------------------------------------------------------------------
extern HINSTANCE dllInstanceHandle;

extern map <WCHAR, WCHAR> radicalMap;

extern BOOL isWindows8; //OS Version
extern BOOL autoCompose; // show candidates while composing
extern BOOL threeCodeMode;
extern BOOL hasPhraseSection; // the dictionary file has TTS [Phrase] section
extern BOOL hasCINPhraseSection; // the dictionary file has CIN %phrasedef section

extern ATOM AtomCandidateWindow;
extern ATOM AtomShadowWindow;
extern ATOM AtomScrollBarWindow;

BOOL RegisterWindowClass();

extern LONG dllRefCount;

extern CRITICAL_SECTION CS;
extern HFONT defaultlFontHandle;  // Global font object we use everywhere

extern const CLSID TSFDayiCLSID;
extern const CLSID TSFDayiGuidProfile;
extern const CLSID TSFDayiGuidImeModePreserveKey;
extern const CLSID TSFDayiGuidDoubleSingleBytePreserveKey;
extern const CLSID TSFDayiGuidPunctuationPreserveKey;

LRESULT CALLBACK ThreadKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
BOOL CheckModifiers(UINT uModCurrent, UINT uMod);
BOOL UpdateModifiers(WPARAM wParam, LPARAM lParam);

extern USHORT ModifiersValue;
extern BOOL IsShiftKeyDownOnly;
extern BOOL IsControlKeyDownOnly;
extern BOOL IsAltKeyDownOnly;

extern const GUID TSFDayiGuidCompartmentIMEMode;
extern const GUID TSFDayiGuidCompartmentDoubleSingleByte;
extern const GUID TSFDayiGuidCompartmentPunctuation;

extern const WCHAR FullWidthCharTable[];
extern const WCHAR symbolCharTable[25];
extern const _AddressDirectInput addressCharTable[5];

extern const GUID TSFDayiGuidLangBarIMEMode;
extern const GUID TSFDayiGuidLangBarDoubleSingleByte;
extern const GUID TSFDayiGuidLangBarPunctuation;

extern const GUID TSFDayiGuidDisplayAttributeInput;
extern const GUID TSFDayiGuidDisplayAttributeConverted;

extern const GUID TSFDayiGuidCandUIElement;

extern const WCHAR UnicodeByteOrderMark;
extern WCHAR KeywordDelimiter;
extern const WCHAR StringDelimiter;

extern const WCHAR ImeModeDescription[];
extern const int ImeModeOnIcoIndex;
extern const int ImeModeOffIcoIndex;

extern const WCHAR DoubleSingleByteDescription[];
extern const int DoubleSingleByteOnIcoIndex;
extern const int DoubleSingleByteOffIcoIndex;

extern const WCHAR PunctuationDescription[];
extern const int PunctuationOnIcoIndex;
extern const int PunctuationOffIcoIndex;

extern const WCHAR LangbarImeModeDescription[];
extern const WCHAR LangbarDoubleSingleByteDescription[];
extern const WCHAR LangbarPunctuationDescription[];
}