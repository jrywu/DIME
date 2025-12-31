/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef GLOBAL_H
#define GLOBAL_H

#pragma once

#include "private.h"
#include "define.h"
#include "BaseStructure.h"
#include "Config.h"



void DllAddRef();
void DllRelease();


namespace Global {

extern IME_MODE imeMode;
extern BOOL isWindows8; //OS Version
extern HFONT defaultlFontHandle;  // Global font object we use everywhere

extern BOOL hasPhraseSection; // the dictionary file has TTS [Phrase] section
extern BOOL hasCINPhraseSection; // the dictionary file has CIN %phrasedef section

extern const WCHAR UnicodeByteOrderMark;
extern WCHAR KeywordDelimiter;
extern const WCHAR StringDelimiter;

extern const CLSID DIMECLSID;


extern const GUID DIMEDayiGuidProfile;
extern const GUID DIMEArrayGuidProfile;
extern const GUID DIMEPhoneticGuidProfile;
extern const GUID DIMEGenericGuidProfile;

extern const GUID DIMEGuidImeModePreserveKey;
extern const GUID DIMEGuidDoubleSingleBytePreserveKey;
extern const GUID DIMEGuidConfigPreserveKey;

#ifndef DIMESettings
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


extern HINSTANCE hShcore;


extern ATOM AtomCandidateWindow;
extern ATOM AtomCandidateShadowWindow;
extern ATOM AtomCandidateScrollBarWindow;
extern ATOM AtomNotifyWindow;
extern ATOM AtomNotifyShadowWindow;

BOOL RegisterWindowClass();

extern LONG dllRefCount;

extern CRITICAL_SECTION CS;

extern const CLSID DIMECLSID;


extern const GUID DIMEDayiGuidProfile;
extern const GUID DIMEArrayGuidProfile;
extern const GUID DIMEPhoneticGuidProfile;
extern const GUID DIMEGenericGuidProfile;

extern const GUID DIMEGuidImeModePreserveKey;
extern const GUID DIMEGuidDoubleSingleBytePreserveKey;
extern const GUID DIMEGuidConfigPreserveKey;

//LRESULT CALLBACK ThreadKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
BOOL CheckModifiers(UINT uModCurrent, UINT uMod);
BOOL UpdateModifiers(WPARAM wParam, LPARAM lParam);

extern USHORT ModifiersValue;
extern BOOL IsShiftKeyDownOnly;
extern BOOL IsControlKeyDownOnly;
extern BOOL IsAltKeyDownOnly;

extern const GUID DIMEGuidCompartmentIMEMode;
extern const GUID DIMEGuidCompartmentDoubleSingleByte;

extern const WCHAR FullWidthCharTable[];
extern const WCHAR DayiSymbolCharTable[];
extern const _DAYI_ADDRESS_DIRECT_INPUT dayiAddressCharTable[6];
extern const _DAYI_ADDRESS_DIRECT_INPUT dayiArticleCharTable[6];

extern const GUID DIMEGuidLangBarIMEMode;
extern const GUID DIMEGuidLangBarDoubleSingleByte;

extern const GUID DIMEGuidDisplayAttributeInput;
extern const GUID DIMEGuidDisplayAttributeConverted;

extern const GUID DIMEGuidCandUIElement;



extern WCHAR ImeModeDescription[50];
extern const int ImeModeOnIcoIndex;
extern const int ImeModeOffIcoIndex;

extern WCHAR DoubleSingleByteDescription[50];
extern const int DoubleSingleByteOnIcoIndex;
extern const int DoubleSingleByteOffIcoIndex;

extern const WCHAR LangbarImeModeDescription[];
extern const WCHAR LangbarDoubleSingleByteDescription[];
extern const WCHAR LangbarPunctuationDescription[];
#endif
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
		StringCchPrintf(wszAppData, MAX_PATH, L"%s\\%s", wszAppData, L"DIME\\");
		if (PathFileExists(wszAppData))
			CreateDirectory(wszAppData, NULL);
	} 

	WCHAR wszDebugLogPath[MAX_PATH];
	StringCchPrintf(wszDebugLogPath, MAX_PATH, L"%s\\%s", wszAppData, L"debug.txt");
	

	FILE *fp;
	_wfopen_s(&fp, wszDebugLogPath, L"a, ccs=UTF-8");
	if(fp)
	{
		va_list args;
		va_start (args, format);
		vfwprintf_s(fp, format, args);
		va_end (args);
		fwprintf_s(fp, L"\n");
		fclose(fp);
	}
}
#else
    inline static void debugPrint(const WCHAR* format,...) {format;}
#endif



#endif