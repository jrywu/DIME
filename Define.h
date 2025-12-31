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

#ifndef DEFINE_H
#define DEFINE_H

#pragma once
#include "resource.h"

#define TEXTSERVICE_MODEL        L"Apartment"
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
#define TEXTSERVICE_DAYI_ICON_INDEX		-IDI_DAYI
#define TEXTSERVICE_ARRAY_ICON_INDEX	-IDI_ARRAY
#define TEXTSERVICE_PHONETIC_ICON_INDEX -IDI_PHONETIC
#define TEXTSERVICE_GENERIC_ICON_INDEX	-IDI_GENERIC

#define IME_MODE_ON_ICON_INDEX      IDI_IME_MODE_ON
#define IME_MODE_OFF_ICON_INDEX     IDI_IME_MODE_OFF
#define IME_DOUBLE_ON_INDEX         IDI_DOUBLE_SINGLE_BYTE_ON
#define IME_DOUBLE_OFF_INDEX        IDI_DOUBLE_SINGLE_BYTE_OFF


#define DIME_FONT_DEFAULT L"Microsoft JhengHei"

//---------------------------------------------------------------------
// defined Candidate Window
//---------------------------------------------------------------------
#define CANDWND_ROW_WIDTH				(30)
#define CANDWND_BORDER_COLOR			(RGB(0x00, 0x00, 0x00))
#define CANDWND_BORDER_WIDTH			(1)
#define CANDWND_PHRASE_COLOR			(RGB(0x00, 0x80, 0x00))
#define CANDWND_NUM_COLOR				(RGB(0xB4, 0xB4, 0xB4))
#define CANDWND_SELECTED_ITEM_COLOR		(RGB(0xFF, 0xFF, 0xFF))
#define CANDWND_SELECTED_BK_COLOR		(RGB(0x63, 0xB4, 0xFB)) //sky blue
#define CANDWND_ITEM_COLOR				(RGB(0x00, 0x00, 0x00))

//---------------------------------------------------------------------
// defined Notify Window
//---------------------------------------------------------------------
#define NOTIFYWND_BORDER_COLOR			(RGB(0x00, 0x00, 0x00))
#define NOTIFYWND_BORDER_WIDTH			(1)
#define NOTIFYWND_TEXT_COLOR			(RGB(0x00, 0x00, 0x00))
#define NOTIFYWND_TEXT_BK_COLOR			(RGB(0xFF, 0xFF, 0xFF))

//---------------------------------------------------------------------
// defined modifier
//---------------------------------------------------------------------
#define _TF_MOD_ON_KEYUP_SHIFT_ONLY    (0x00010000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_CONTROL_ONLY  (0x00020000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_ALT_ONLY      (0x00040000 | TF_MOD_ON_KEYUP)


//---------------------------------------------------------------------
// string length of CLSID
//---------------------------------------------------------------------
#define CLSID_STRLEN    (38)  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")


//---------------------------------------------------------------------
// candidate width
//---------------------------------------------------------------------
#define DEFAULT_CAND_ITEM_LENGTH 3
#define MAX_CAND_ITEM_LENGTH 13//6+6+1
#define TRAILING_SPACE 1


//---------------------------------------------------------------------
// Maximum radical size and candidate selkey size
//---------------------------------------------------------------------
#define MAX_RADICAL 64 // ascii table 0x20 (0d32) ~ 0x60 (0d96)
#define MAX_CAND_SELKEY  16 
// -------------------------------------------------------------------- -
// IM SLOTS
//---------------------------------------------------------------------
#define IM_SLOTS 6

//---------------------------------------------------------------------
// CANDIDATES KEY AND VALUE LENGTH
//---------------------------------------------------------------------

#define MAX_COMMIT_LENGTH 1024
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 960

#define CHN_ENG_NOTIFY_DELAY 1500
#endif