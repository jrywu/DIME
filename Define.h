//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#pragma once
#include "resource.h"

#define TEXTSERVICE_MODEL        L"Apartment"
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
#define TEXTSERVICE_ICON_INDEX   -IDIS_TSFDayi


#define IME_MODE_ON_ICON_INDEX      IDI_IME_MODE_ON
#define IME_MODE_OFF_ICON_INDEX     IDI_IME_MODE_OFF
#define IME_DOUBLE_ON_INDEX         IDI_DOUBLE_SINGLE_BYTE_ON
#define IME_DOUBLE_OFF_INDEX        IDI_DOUBLE_SINGLE_BYTE_OFF
#define IME_PUNCTUATION_ON_INDEX    IDI_PUNCTUATION_ON
#define IME_PUNCTUATION_OFF_INDEX   IDI_PUNCTUATION_OFF

#define TSFDayi_FONT_DEFAULT L"Microsoft JhengHei"

//---------------------------------------------------------------------
// defined Candidated Window
//---------------------------------------------------------------------
#define CANDWND_ROW_WIDTH				(30)
#define CANDWND_BORDER_COLOR			(RGB(0x00, 0x00, 0x00))
#define CANDWND_BORDER_WIDTH			(2)
#define CANDWND_NUM_COLOR				(RGB(0xB4, 0xB4, 0xB4))
#define CANDWND_SELECTED_ITEM_COLOR		(RGB(0xFF, 0xFF, 0xFF))
#define CANDWND_SELECTED_BK_COLOR		(RGB(0xA6, 0xA6, 0x00))
#define CANDWND_ITEM_COLOR				(RGB(0x00, 0x00, 0x00))

//---------------------------------------------------------------------
// defined Notify Window
//---------------------------------------------------------------------
#define NOTIFYWND_ROW_WIDTH				(30)
#define NOTIFYWND_BORDER_COLOR			(RGB(0x00, 0x00, 0x00))
#define NOTIFYWND_BORDER_WIDTH			(2)
#define NOTIFYWND_TEXT_COLOR		(RGB(0xFF, 0xFF, 0xFF))
#define NOTIFYWND_TEXT_BK_COLOR		(RGB(0xA6, 0xA6, 0x00))

//---------------------------------------------------------------------
// defined modifier
//---------------------------------------------------------------------
#define _TF_MOD_ON_KEYUP_SHIFT_ONLY    (0x00010000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_CONTROL_ONLY  (0x00020000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_ALT_ONLY      (0x00040000 | TF_MOD_ON_KEYUP)

#define CAND_WIDTH     (13)      // * tmMaxCharWidth

//---------------------------------------------------------------------
// string length of CLSID
//---------------------------------------------------------------------
#define CLSID_STRLEN    (38)  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")