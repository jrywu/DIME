//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef GLOBAL_H
#define GLOBAL_H

#pragma once
#include <map>
#include "private.h"
#include "define.h"
#include "BaseStructure.h"
#include "Config.h"


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

extern IME_MODE imeMode;

extern map <WCHAR, WCHAR> radicalMap;

extern BOOL isWindows8; //OS Version
extern BOOL autoCompose; // show candidates while composing
extern BOOL threeCodeMode;
extern BOOL hasPhraseSection; // the dictionary file has TTS [Phrase] section
extern BOOL hasCINPhraseSection; // the dictionary file has CIN %phrasedef section

extern ATOM AtomCandidateWindow;
extern ATOM AtomCandidateShadowWindow;
extern ATOM AtomCandidateScrollBarWindow;
extern ATOM AtomNotifyWindow;
extern ATOM AtomNotifyShadowWindow;

BOOL RegisterWindowClass();

extern LONG dllRefCount;

extern CRITICAL_SECTION CS;
extern HFONT defaultlFontHandle;  // Global font object we use everywhere


extern const CLSID TSFTTSCLSID;
extern const CLSID TSFARRAYCLSID;
extern const CLSID TSFPHONETICCLSID;
extern const GUID TSFDayiGuidProfile;
extern const GUID TSFArrayGuidProfile;
extern const GUID TSFPhoneticGuidProfile;

extern const GUID TSFTTSGuidImeModePreserveKey;
extern const GUID TSFTTSGuidDoubleSingleBytePreserveKey;
extern const GUID TSFTTSGuidConfigPreserveKey;

LRESULT CALLBACK ThreadKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
BOOL CheckModifiers(UINT uModCurrent, UINT uMod);
BOOL UpdateModifiers(WPARAM wParam, LPARAM lParam);

extern USHORT ModifiersValue;
extern BOOL IsShiftKeyDownOnly;
extern BOOL IsControlKeyDownOnly;
extern BOOL IsAltKeyDownOnly;

extern const GUID TSFTTSGuidCompartmentIMEMode;
extern const GUID TSFTTSGuidCompartmentDoubleSingleByte;

extern const WCHAR FullWidthCharTable[];
extern const WCHAR symbolCharTable[27];
extern const _AddressDirectInput addressCharTable[5];

extern const GUID TSFTTSGuidLangBarIMEMode;
extern const GUID TSFTTSGuidLangBarDoubleSingleByte;

extern const GUID TSFTTSGuidDisplayAttributeInput;
extern const GUID TSFTTSGuidDisplayAttributeConverted;

extern const GUID TSFTTSGuidCandUIElement;

extern const WCHAR UnicodeByteOrderMark;
extern WCHAR KeywordDelimiter;
extern const WCHAR StringDelimiter;

extern WCHAR ImeModeDescription[50];
extern const int ImeModeOnIcoIndex;
extern const int ImeModeOffIcoIndex;

extern WCHAR DoubleSingleByteDescription[50];
extern const int DoubleSingleByteOnIcoIndex;
extern const int DoubleSingleByteOffIcoIndex;

extern const WCHAR LangbarImeModeDescription[];
extern const WCHAR LangbarDoubleSingleByteDescription[];
extern const WCHAR LangbarPunctuationDescription[];
}


#ifdef DEBUG_PRINT 
#include <ShlObj.h>
#include <Shlwapi.h>
#include <stdarg.h>
#include <stdio.h>

inline static void debugPrint(const WCHAR* format,...) 
{

	WCHAR wszAppData[MAX_PATH];

	if (SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE))
	{
		StringCchPrintf(wszAppData, MAX_PATH, L"%s\\%s", wszAppData, L"TSFTTS\\");
		if (PathFileExists(wszAppData))
			CreateDirectory(wszAppData, NULL);
	} 

	WCHAR wszDebugLogPath[MAX_PATH];
	StringCchPrintf(wszDebugLogPath, MAX_PATH, L"%s\\%s", wszAppData, L"debug.txt");
	

	FILE *fp;
	_wfopen_s(&fp, wszDebugLogPath, L"a");
	if(fp)
	{
		va_list args;
		va_start (args, format);
		vfwprintf_s (fp, format, args);
		va_end (args);
		fwprintf_s(fp, L"\n");
		fclose(fp);
	}
}
#else
    inline static void debugPrint(const WCHAR* format,...) {format;}
#endif



#endif