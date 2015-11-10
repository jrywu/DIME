//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

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
// defined Candidated Window
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
#define TRAILING_SPACE 1


//---------------------------------------------------------------------
// candidate width
//---------------------------------------------------------------------
#define MAX_RADICAL 64 // ascii table 0x20 (0d32) ~ 0x60 (0d96)

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