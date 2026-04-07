// SettingsPageLayout.cpp — THE layout definition
//
// This file defines the card→row tree structure for all pages.
// To add a new setting: add to ControlDef[] (SettingsModel.cpp) + add one line here.
// To rearrange: reorder lines here. Zero C++ renderer changes.

#include <windows.h>
#include "SettingsPageLayout.h"
#include "SettingsController.h"
#include "..\resource.h"

// ============================================================================
// 外觀設定 sub-page (NavigateToCard >)
// ============================================================================
static const LayoutRow s_appearanceRows[] = {
    { CTRL_CANDIDATE_PREVIEW, RowType::CandidatePreview, RowAction::None,   CTRL_NONE,       0, L"預覽",            nullptr,                                 SM_MODE_ALL, true,  0         },
    { CTRL_FONT_NAME,       RowType::Clickable, RowAction::OpenFontDialog,  CTRL_NONE,       0, L"字型設定",        L"候選字視窗顯示字型",                    SM_MODE_ALL, true,  L'\xE8D2' },
    { CTRL_COLOR_MODE,      RowType::ComboBox,  RowAction::ExpandSection,   CTRL_NONE,       0, L"色彩模式",        L"設定候選字視窗色彩主題",                SM_MODE_ALL, false, L'\xE790' },
    { CTRL_COLOR_GRID,      RowType::ColorGrid, RowAction::OpenColorDialog, CTRL_COLOR_MODE, 0, L"色彩",            nullptr,                                 SM_MODE_ALL, false, 0         },
    { CTRL_RESTORE_DEFAULT, RowType::Button,    RowAction::ResetDefaults,   CTRL_NONE,       0, L"還原預設值",      L"將所有外觀設定還原為預設值",            SM_MODE_ALL, false, L'\xE72C' },
};

// ============================================================================
// 聲音與通知 sub-page (NavigateToCard >)
// ============================================================================
static const LayoutRow s_soundRows[] = {
    { CTRL_DO_BEEP,        RowType::Toggle, RowAction::ToggleValue, CTRL_NONE, 0, L"錯誤提示音",         L"輸入錯誤字根時發出提示音",            SM_MODE_ALL,  false, L'\xEA8F' },
    { CTRL_DO_BEEP_NOTIFY, RowType::Toggle, RowAction::ToggleValue, CTRL_NONE, 0, L"錯誤提示音顯示通知", L"提示音發出時同時顯示桌面通知視窗",    SM_MODE_ALL,  false, L'\xE814' },
    { CTRL_DO_BEEP_CANDI,  RowType::Toggle, RowAction::ToggleValue, CTRL_NONE, 0, L"候選字提示音",       L"顯示候選字視窗時發出提示音",          SM_MODE_DAYI, false, L'\xE994' },
};

// ============================================================================
// 輸入設定 — merged: 輸入行為 + 候選字設定 + 切換設定 + 輸入法專屬設定
// Single ∨ expandable group per plan
// ============================================================================
static const LayoutRow s_inputSettingsRows[] = {
    { CTRL_SHOW_NOTIFY,           RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"在桌面模式顯示浮動中英切換視窗", nullptr,                  SM_MODE_ALL,       false, L'\xE982' },
    // --- Input behavior ---
    { CTRL_ARRAY_SCOPE,           RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"字集查詢範圍",         nullptr,                                SM_MODE_ARRAY,     false, L'\xE774' },
    { CTRL_CHARSET_SCOPE,         RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"字集查詢範圍",         nullptr,                                SM_MODE_NON_ARRAY, false, L'\xE774' },
    { CTRL_NUMERIC_PAD,           RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"九宮格數字鍵盤",       nullptr,                                SM_MODE_ALL,       false, L'\xE75F' },
    { CTRL_PHONETIC_KB,           RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"鍵盤對應選擇",         nullptr,                                SM_MODE_PHONETIC,  false, L'\xE765' },
    { CTRL_MAX_CODES,             RowType::Edit,     RowAction::None,        CTRL_NONE, 0, L"組字區最大長度",       nullptr,                                SM_MODE_ALL,       false, L'\xE8C1' },
    // --- Candidate settings ---
    { CTRL_ARROW_KEY_SW_PAGES,    RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"以方向鍵換頁",         nullptr,                                SM_MODE_ALL,       false, L'\xE76C' },
    { CTRL_SPACE_AS_PAGEDOWN,     RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"以空白鍵換頁",         nullptr,                                SM_MODE_ALL,       false, L'\xE75D' },
    { CTRL_SPACE_AS_FIRST_CAND,   RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"空白鍵為第一選字鍵",   nullptr,                                SM_MODE_GENERIC,   false, L'\xE8C4' },
    { CTRL_AUTO_COMPOSE,          RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"打字時同步組字",       nullptr,                                SM_MODE_ALL,       false, L'\xE8D3' },
    { CTRL_CLEAR_ON_BEEP,         RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"錯誤組字時清除字根",   nullptr,                                SM_MODE_ALL,       false, L'\xE894' },
    { CTRL_MAKE_PHRASE,           RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"提示聯想字詞",         nullptr,                                SM_MODE_ALL,       false, L'\xE82F' },
    { CTRL_CUSTOM_TABLE_PRIORITY, RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"自建詞優先",           nullptr,                                SM_MODE_ALL,       false, L'\xE82E' },
    // --- Switching ---
    { CTRL_IME_SHIFT_MODE,        RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"中英切換熱鍵",         nullptr,                                SM_MODE_ALL,       false, L'\xE8AB' },
    { CTRL_DOUBLE_SINGLE_BYTE,    RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"全半形輸入模式",       nullptr,                                SM_MODE_ALL,       false, L'\xE97F' },
    { CTRL_KEYBOARD_OPEN_CLOSE,   RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"預設輸入模式",         nullptr,                                SM_MODE_ALL,       false, L'\xE983' },
    { CTRL_OUTPUT_CHT_CHS,        RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"輸出字元",             nullptr,                                SM_MODE_ALL,       false, L'\xE98A' },
    { CTRL_REVERSE_CONVERSION,    RowType::ComboBox, RowAction::None,        CTRL_NONE, 0, L"反查輸入字根",         nullptr,                                SM_MODE_ALL,       false, L'\xE721' },
    // --- IME-specific ---
    { CTRL_DAYI_ARTICLE,          RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"地址鍵輸入符號",       nullptr,                                SM_MODE_DAYI,      false, L'\xE8AC' },
    { CTRL_ARRAY_FORCE_SP,        RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"僅接受輸入特別碼",     nullptr,                                SM_MODE_ARRAY,     false, L'\xE73E' },
    { CTRL_ARRAY_NOTIFY_SP,       RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"特別碼提示",           nullptr,                                SM_MODE_ARRAY,     false, L'\xE7E7' },
    { CTRL_ARRAY_SINGLE_QUOTE,    RowType::Toggle,   RowAction::ToggleValue, CTRL_NONE, 0, L"以'鍵查詢自建詞庫",    nullptr,                                SM_MODE_ARRAY,     false, L'\xE848' },
};

// ============================================================================
// 自建詞庫 sub-page rows
// ============================================================================
static const LayoutRow s_customTableRows[] = {
    // Card 1: Editor (Save button is created programmatically inside RichEditor rendering)
    { CTRL_CUSTOM_TABLE_EDITOR, RowType::RichEditor, RowAction::None,             CTRL_NONE, 0, L"自訂碼表",       L"每行格式：輸入碼 空白 詞彙",  SM_MODE_ALL, false, 0         },
    // Card 2: Export + Import (cardBreakBefore = true on first row)
    { CTRL_CUSTOM_TABLE_EXPORT, RowType::Button,     RowAction::ExportCustomTable, CTRL_NONE, 0, L"匯出自建詞庫",   L"匯出自建詞庫至檔案",          SM_MODE_ALL, true,  L'\xEDE1' },
    { CTRL_CUSTOM_TABLE_IMPORT, RowType::Button,     RowAction::ImportCustomTable, CTRL_NONE, 0, L"匯入自建詞庫",   L"從檔案匯入（覆蓋現有內容）",  SM_MODE_ALL, false, L'\xE8B5' },
};

// ============================================================================
// IME mode page layout — THE card→row tree for each IME mode
// ============================================================================
#define COUNTOF(arr) (sizeof(arr)/sizeof(arr[0]))
static const int kAppearanceRowCount    = COUNTOF(s_appearanceRows);
static const int kSoundRowCount         = COUNTOF(s_soundRows);
static const int kInputSettingsRowCount = COUNTOF(s_inputSettingsRows);
static const int kCustomTableRowCount   = COUNTOF(s_customTableRows);

static const LayoutCard s_imePageLayout[] = {
    // ">" navigate to sub-page
    { L"外觀設定",   L"字型、色彩、候選字視窗樣式",
      RowAction::NavigateToCard, s_appearanceRows,    kAppearanceRowCount,    0, L'\xE790', false },
    { L"聲音與通知", L"提示音、錯誤通知",
      RowAction::ExpandSection,  s_soundRows,         kSoundRowCount,         0, L'\xEA8F', false },
    // "∨" expand in-place — single merged group (default expanded)
    { L"輸入設定",   L"字集、組字、候選字、切換、輸入法專屬選項",
      RowAction::ExpandSection,  s_inputSettingsRows, kInputSettingsRowCount, 0, L'\xE765', true },
    // ">" navigate to custom phrase editor (RichEdit page)
    { L"自建詞庫",   L"自建詞庫編輯、匯入匯出",
      RowAction::NavigateToCard, s_customTableRows,   kCustomTableRowCount,   0, L'\xE70F', false },
};

static const int s_imePageLayoutCount = COUNTOF(s_imePageLayout);

// ============================================================================
// 載入碼表 page — load tables for all IMEs
// ============================================================================
// 行列擴充碼表 child rows (expanded under the parent card)
static const LayoutRow s_arrayExpandRows[] = {
    { CTRL_LOAD_ARRAY_SP,      RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"特別碼碼表",           nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY_SC,      RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"簡碼碼表",             nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY_EXT_B,   RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"Unicode Ext. B 碼表",  nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY_EXT_CD,  RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"Unicode Ext. CD 碼表", nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY_EXT_E_TO_J, RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"Unicode Ext. E-J 碼表",nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY40,       RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"行列40碼表",           nullptr, SM_MODE_ALL, false, L'\xE82D' },
    { CTRL_LOAD_ARRAY_PHRASE,  RowType::Button, RowAction::LoadTable, CTRL_LOAD_ARRAY_EXPAND, 0, L"行列詞庫碼表",         nullptr, SM_MODE_ALL, false, L'\xE82D' },
};

// Standalone load-table rows (one row per card, for button creation)
static const LayoutRow s_loadDayiRow[]    = { { CTRL_LOAD_DAYI_MAIN,     RowType::Button, RowAction::LoadTable, CTRL_NONE, 0, L"載入", nullptr, SM_MODE_ALL } };
static const LayoutRow s_loadArrayRow[]   = { { CTRL_LOAD_ARRAY_MAIN,    RowType::Button, RowAction::LoadTable, CTRL_NONE, 0, L"載入", nullptr, SM_MODE_ALL } };
static const LayoutRow s_loadPhoneticRow[]= { { CTRL_LOAD_PHONETIC_MAIN, RowType::Button, RowAction::LoadTable, CTRL_NONE, 0, L"載入", nullptr, SM_MODE_ALL } };
static const LayoutRow s_loadGenericRow[] = { { CTRL_LOAD_GENERIC_MAIN,  RowType::Button, RowAction::LoadTable, CTRL_NONE, 0, L"載入", nullptr, SM_MODE_ALL } };
static const LayoutRow s_loadAssocRow[]   = { { CTRL_LOAD_ASSOC_PHRASE,  RowType::Button, RowAction::LoadTable, CTRL_NONE, 0, L"載入", nullptr, SM_MODE_ALL } };

// 載入碼表 page — one LayoutCard per top-level row (same structure as IME page)
static const LayoutCard s_loadTablePageLayout[] = {
    { L"大易主碼表",   L"載入或更新大易輸入法主碼表",
      RowAction::None, s_loadDayiRow, 1, 0, L'\xE82D', false },
    { L"行列主碼表",   L"載入或更新行列輸入法主碼表",
      RowAction::None, s_loadArrayRow, 1, 0, L'\xE82D', false },
    { L"行列擴充碼表", L"特碼、簡碼、ExtB/CD/E~J、行列40、行列詞庫",
      RowAction::ExpandSection, s_arrayExpandRows, COUNTOF(s_arrayExpandRows), 0, L'\xE8F1', false },
    { L"注音主碼表",   L"載入或更新注音輸入法主碼表",
      RowAction::None, s_loadPhoneticRow, 1, 0, L'\xE82D', false },
    { L"自建主碼表",   L"載入或更新自建輸入法主碼表",
      RowAction::None, s_loadGenericRow, 1, 0, L'\xE82D', false },
    { L"聯想詞彙表",   L"載入所有輸入法共用的聯想詞彙表",
      RowAction::None, s_loadAssocRow, 1, 0, L'\xE82D', false },
};

static const int s_loadTablePageLayoutCount = COUNTOF(s_loadTablePageLayout);

// ============================================================================
// Sidebar items — IME modes + separator + 自定碼表
// ============================================================================
static const SidebarItemDef s_sidebarItems[] = {
    { L"大易輸入法",       IDI_DAYI },
    { L"行列輸入法",       IDI_ARRAY },
    { L"傳統注音輸入法",   IDI_PHONETIC },
    { L"自建輸入法",       IDI_GENERIC },
    { nullptr,             -1 },         // separator (iconResourceId == -1)
    { L"自定碼表",         IDI_DIMESETTINGS },
};

static const int s_sidebarItemCount = COUNTOF(s_sidebarItems);
#undef COUNTOF

// ============================================================================
// Public accessors
// ============================================================================

const LayoutCard* GetIMEPageLayout(int* count)
{
    if (count) *count = s_imePageLayoutCount;
    return s_imePageLayout;
}

const LayoutCard* GetLoadTablePageLayout(int* count)
{
    if (count) *count = s_loadTablePageLayoutCount;
    return s_loadTablePageLayout;
}

const SidebarItemDef* GetSidebarItems(int* count)
{
    if (count) *count = s_sidebarItemCount;
    return s_sidebarItems;
}
