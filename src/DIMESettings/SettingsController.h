// SettingsController.h — Data binding layer for the modernized settings UI
//
// This header defines:
//   - SettingsControlId: logical control identifiers (the binding key)
//   - SidebarItemDef: sidebar items
//   - SettingsSnapshot: flat struct bridging CConfig ↔ UI
//   - SettingsModel: pure functions for binding, hitTest, scroll
//
// Layout (labels, descriptions, visibility, icons) lives in SettingsPageLayout.h.
// No HWND, no GDI, no Win32 messages. Fully unit-testable.

#pragma once

#include <windows.h>
#include <vector>
#include "SettingsKeys.h"
#include "..\BaseStructure.h"

class CCompositionProcessorEngine;

// ============================================================================
// Logical control IDs (not Win32 control IDs)
// ============================================================================
enum SettingsControlId {
    CTRL_NONE = 0,
    CTRL_FONT_NAME = 1,
    CTRL_FONT_SIZE,
    CTRL_FONT_CHOOSE,
    CTRL_COLOR_MODE,
    CTRL_COLOR_FR,
    CTRL_COLOR_SEFR,
    CTRL_COLOR_BG,
    CTRL_COLOR_PHRASE,
    CTRL_COLOR_NU,
    CTRL_COLOR_SEBG,
    CTRL_SHOW_NOTIFY,
    CTRL_RESTORE_DEFAULT,

    CTRL_ARRAY_SCOPE,
    CTRL_CHARSET_SCOPE,
    CTRL_NUMERIC_PAD,
    CTRL_PHONETIC_KB,
    CTRL_MAX_CODES,

    CTRL_ARROW_KEY_SW_PAGES,
    CTRL_SPACE_AS_PAGEDOWN,
    CTRL_SPACE_AS_FIRST_CAND,
    CTRL_AUTO_COMPOSE,
    CTRL_CLEAR_ON_BEEP,
    CTRL_MAKE_PHRASE,
    CTRL_CUSTOM_TABLE_PRIORITY,

    CTRL_DO_BEEP,
    CTRL_DO_BEEP_NOTIFY,
    CTRL_DO_BEEP_CANDI,

    CTRL_IME_SHIFT_MODE,
    CTRL_DOUBLE_SINGLE_BYTE,
    CTRL_KEYBOARD_OPEN_CLOSE,
    CTRL_OUTPUT_CHT_CHS,
    CTRL_REVERSE_CONVERSION,

    CTRL_DAYI_ARTICLE,
    CTRL_ARRAY_FORCE_SP,
    CTRL_ARRAY_NOTIFY_SP,
    CTRL_ARRAY_SINGLE_QUOTE,
    CTRL_BIG5_FILTER,

    // Composite / special controls
    CTRL_COLOR_GRID,           // 3x2 color swatch grid (expandable child of CTRL_COLOR_MODE)
    CTRL_CANDIDATE_PREVIEW,    // candidate + notify window preview (RowType::CandidatePreview)

    CTRL_COUNT,

    // ── 自建詞庫 page controls ──────────────────────────────────────────────
    CTRL_CUSTOM_TABLE_EDITOR  = 200,   // RichEdit area (RowType::RichEditor)
    CTRL_CUSTOM_TABLE_SAVE    = 201,   // [儲存] button
    CTRL_CUSTOM_TABLE_EXPORT  = 202,   // [匯出] button
    CTRL_CUSTOM_TABLE_IMPORT  = 203,   // [匯入] button

    // ── 載入碼表 page controls ──────────────────────────────────────────────
    CTRL_LOAD_DAYI_MAIN       = 300,
    CTRL_LOAD_ARRAY_MAIN      = 301,
    CTRL_LOAD_ARRAY_EXPAND    = 302,   // expandable group header
    CTRL_LOAD_ARRAY_SP        = 303,
    CTRL_LOAD_ARRAY_SC        = 304,
    CTRL_LOAD_ARRAY_EXT_B     = 305,
    CTRL_LOAD_ARRAY_EXT_CD    = 306,
    CTRL_LOAD_ARRAY_EXT_E_TO_J   = 307,
    CTRL_LOAD_ARRAY40         = 308,
    CTRL_LOAD_ARRAY_PHRASE    = 309,
    CTRL_LOAD_PHONETIC_MAIN   = 310,
    CTRL_LOAD_GENERIC_MAIN    = 311,
    CTRL_LOAD_ASSOC_PHRASE    = 312,
};

// ============================================================================
// Sidebar item definition
// ============================================================================
struct SidebarItemDef {
    const wchar_t* label;
    int iconResourceId;   // Icon resource ID (IDI_xxx), 0 = no icon
};

// ============================================================================
// Settings snapshot — flat struct mirroring CConfig 1:1
// ============================================================================
struct SettingsSnapshot {
    UINT fontSize;
    UINT fontWeight;
    BOOL fontItalic;
    wchar_t fontFaceName[LF_FACESIZE];
    int colorMode;  // IME_COLOR_MODE as int
    COLORREF itemColor, phraseColor, numberColor;
    COLORREF itemBGColor, selectedColor, selectedBGColor;
    BOOL autoCompose;
    BOOL clearOnBeep;
    BOOL doBeep;
    BOOL doBeepNotify;
    BOOL doBeepOnCandi;
    BOOL makePhrase;
    BOOL customTablePriority;
    BOOL arrowKeySWPages;
    BOOL spaceAsPageDown;
    BOOL spaceAsFirstCandSelkey;
    BOOL showNotifyDesktop;
    BOOL arrayNotifySP;
    BOOL arrayForceSP;
    BOOL arraySingleQuoteCustomPhrase;
    BOOL dayiArticleMode;
    BOOL doHanConvert;
    BOOL activatedKeyboardMode;
    BOOL big5Filter;
    UINT maxCodes;
    int arrayScope;       // ARRAY_SCOPE as int
    int numericPad;       // NUMERIC_PAD as int
    int phoneticKeyboardLayout;  // PHONETIC_KEYBOARD_LAYOUT as int
    int imeShiftMode;     // IME_SHIFT_MODE as int
    int doubleSingleByteMode;    // DOUBLE_SINGLE_BYTE_MODE as int
    int reverseConversionIndex;
};

// ============================================================================
// SettingsModel — pure functions, no HWND
// ============================================================================
class SettingsModel {
public:
    // --- IME mode bitmask (pure function) ---
    static int  GetModeBitmask(IME_MODE mode);

    // --- Sidebar hitTest (pure function) ---
    static int SidebarHitTest(int y, int itemHeight, int itemCount);

    // --- Scroll computation (pure functions) ---
    static int ComputeScrollRange(int contentHeight, int viewportHeight);
    static int ClampScrollPos(int pos, int maxScroll);

    // --- IME mode string conversion (pure functions) ---
    static const wchar_t* ImeModeToString(IME_MODE mode);
    static IME_MODE StringToImeMode(const wchar_t* str);

    // --- Settings snapshot (CConfig bridge) ---
    static SettingsSnapshot LoadFromConfig();
    static void ApplyToConfig(const SettingsSnapshot& snap);

    // --- Toggle dispatch (maps control ID → snapshot field) ---
    static void SyncToggleToSnapshot(SettingsSnapshot& s, SettingsControlId id, bool isOn);

    // --- Custom table line validation (pure logic, no HWND) ---
    // Matches CConfig::ValidateCustomTableLines 3-level checking exactly:
    //   Level 1: Format (regex: key + whitespace + value)
    //   Level 2: Key too long
    //   Level 3: Invalid char(s) in key — may report MULTIPLE errors per line
    enum class LineError { None, Format, KeyTooLong, InvalidChar };
    struct LineErrorEntry {
        LineError error;
        int errorStart;  // char offset within line
        int errorLen;     // chars to highlight
    };
    struct LineValidation {
        bool valid;
        std::vector<LineErrorEntry> errors;  // empty = valid line
    };
    static LineValidation ValidateLine(const wchar_t* line, int len, IME_MODE mode, UINT maxCodes,
        CCompositionProcessorEngine* pEngine = nullptr);

    // --- Composition engine lifecycle (for custom table validation) ---
    static CCompositionProcessorEngine* CreateEngine(IME_MODE mode);
    static void DestroyEngine(CCompositionProcessorEngine*& pEngine);

    // --- Custom table file paths ---
    static void GetCustomTableTxtPath(IME_MODE mode, WCHAR* out, DWORD cch);
    static void GetCustomTableCINPath(IME_MODE mode, WCHAR* out, DWORD cch);

    // --- Encoding-aware text loader (issue #130) ---
    // Reads a text file from disk and decodes it to UTF-16LE in a freshly
    // allocated buffer. Detection order: UTF-16LE/BE BOM → UTF-8 BOM →
    // UTF-8 sniff (MultiByteToWideChar with MB_ERR_INVALID_CHARS) → CP_ACP
    // fallback (CP950/Big5 on zh-TW Windows). BOM is stripped from the
    // returned buffer. Caller frees with delete[]. *outLen receives wchar
    // count (excluding NUL terminator). Returns nullptr on failure.
    static LPWSTR LoadTextFileAsUtf16(LPCWSTR path, size_t* outLen);

    // --- Sidebar index ↔ IME mode ---
    static IME_MODE IndexToMode(int index);
    static int ModeToIndex(IME_MODE mode);

    // --- Combo selection → snapshot field mapping ---
    static void SyncComboToSnapshot(SettingsSnapshot& s, SettingsControlId id, int sel);

    // --- CTRL_MAX_CODES text → snapshot ---
    static bool ParseMaxCodes(const wchar_t* text, UINT& outVal);

    // --- Load table control ID → CIN file suffix ---
    static const wchar_t* GetLoadTableSuffix(SettingsControlId id);
};
