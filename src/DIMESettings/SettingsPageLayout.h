// SettingsPageLayout.h — Declarative layout tree for DIMESettings
//
// Defines WHAT goes WHERE (like .rc for dialogs).
// The renderer walks this tree generically.
// Adding a setting = adding data here, not modifying C++ render code.

#pragma once

#include <windows.h>
#include "SettingsController.h"

// IME mode bitmask for control visibility
static const int SM_MODE_DAYI     = 0x1;
static const int SM_MODE_ARRAY    = 0x2;
static const int SM_MODE_PHONETIC = 0x4;
static const int SM_MODE_GENERIC  = 0x8;
static const int SM_MODE_ALL      = 0xF;
static const int SM_MODE_NON_ARRAY = SM_MODE_DAYI | SM_MODE_PHONETIC | SM_MODE_GENERIC;

// ============================================================================
// Row type — how the renderer draws this row
// ============================================================================
enum class RowType {
    Toggle,       // label+desc (left) | "開啟"/"關閉" + toggle (right) | full-row click
    ComboBox,     // label+desc (left) | combo dropdown (right, fixed width)
    Edit,         // label+desc (left) | edit box (right, fixed width)
    ColorGrid,    // 3xN grid of color swatches inside expandable section
    Clickable,    // label+desc (left) | [button] (right) | button triggers action
    Button,       // label+desc (left) | [button] (right) | action row
    RichEditor,       // full-width multi-line rich-text editor (takes most of card height)
    CandidatePreview, // candidate + notify window preview (dynamic height based on font)
};

// ============================================================================
// Row action — what happens when the row or its control is activated
// ============================================================================
enum class RowAction {
    None,
    ToggleValue,       // Click toggles the snapshot boolean
    OpenFontDialog,    // [變更字型] button opens ChooseFontW
    OpenColorDialog,   // Click swatch opens ChooseColorW
    ExpandSection,     // ∨ — expand/collapse children in-place
    NavigateToCard,    // > — push sub-page (breadcrumb navigation)
    LoadTable,         // [載入] button opens file dialog to load .cin table
    SaveCustomTable,   // [儲存] button saves custom table from RichEdit
    ExportCustomTable, // [匯出] button exports custom table
    ImportCustomTable, // [匯入] button imports custom table
    ResetDefaults,     // [還原預設值] resets appearance to defaults
};

// ============================================================================
// Layout row — one row in the settings UI
// ============================================================================
struct LayoutRow {
    SettingsControlId id;           // Maps to ControlDef for label/desc/keyName/visibility
    RowType type;                   // How to render
    RowAction action;               // What happens on click/activate
    SettingsControlId expandParent; // CTRL_NONE = always visible
                                    // Otherwise: hidden until parent is expanded
    COLORREF iconColor;             // Fallback icon color (0 = use iconGlyph or no icon)
    const wchar_t* label;           // Primary display text
    const wchar_t* description;     // Secondary text (nullable)
    int visibleModes;               // Bitmask: SM_MODE_DAYI | SM_MODE_ARRAY etc.
    bool cardBreakBefore;           // true = start a new card before this row (default false)
    wchar_t iconGlyph;              // Segoe MDL2 Assets codepoint (0 = use iconColor fallback)
};

// ============================================================================
// Layout card — a group of rows, can be navigable (>) or expandable (∨)
// ============================================================================
struct LayoutCard {
    const wchar_t* title;           // Card header / category name
    const wchar_t* description;     // Brief description (shown in Level 1 row)
    RowAction action;               // NavigateToCard (>) or ExpandSection (∨)
    const LayoutRow* rows;          // Child rows (shown in sub-page or expanded in-place)
    int rowCount;                   // Number of child rows
    COLORREF iconColor;             // Fallback icon color (0 = use iconGlyph)
    wchar_t iconGlyph;              // Segoe MDL2 Assets codepoint (0 = use iconColor fallback)
    bool defaultExpanded;           // ExpandSection: true = start expanded
};

// ============================================================================
// Page types
// ============================================================================

// Get the IME mode settings page layout (Level 1 cards for a specific IME)
const LayoutCard* GetIMEPageLayout(int* count);

// Get the 載入碼表 page layout
const LayoutCard* GetLoadTablePageLayout(int* count);

// Get the number of sidebar items (4 IME modes + separator + 載入碼表)
struct SidebarItemDef;  // forward decl
const SidebarItemDef* GetSidebarItems(int* count);

// Visibility check — replaces SettingsModel::IsControlVisible
inline bool IsRowVisible(const LayoutRow& row, int modeBitmask) {
    return (row.visibleModes & modeBitmask) != 0;
}
