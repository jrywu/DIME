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

#pragma once

#include "StringRange.h"
#include "PointerArray.h"

class CDisplayString
{
public:
    CDisplayString() { }
    ~CDisplayString() { }

    int Count() { }
    // VOID SetDisplayString(int iIndex, WCHAR* pchText, USHORT cchMax, TF_DISPLAYATTRIBUTE tfDisplayAttribute) { }
    // VOID GetDisplayString(int iIndex, WCHAR* pchText, USHORT cchMax, USHORT* pch, TF_DISPLAYATTRIBUTE tfDisplayAttribute) { }
    VOID SetLogicalFont(LOGFONTW LogFont) { }
    VOID GetLogicalFont(LOGFONTW* pLogFont) { }

private:
    //typedef struct _DISPLAY_STRING {
    //    CStringRange         _StringRange;                   // Unicode string.
    //                                                         // Length and MaximumLength is in character count.
    //                                                         // Buffer doesn't add zero terminate.
    //    TF_DISPLAYATTRIBUTE  _tfDisplayAttribute;            // Display attribute for each array.
    //} DISPLAY_STRING;

    //
    // Array of DISPLAY_STRING
    //
    //CPointerArray<DISPLAY_STRING>  _pDisplayString;

    // Logical font
    LOGFONTW                       _logfont;
};
