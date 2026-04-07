// SettingsGeometry.h — All layout geometry + theme color derivation
//
// Pixel values: compiled defaults that can be OVERRIDDEN at runtime by
//   SettingsLayout.ini placed next to DIMESettings.exe.
// Colors: derived from system theme APIs at runtime.
//
// To customize layout without recompiling:
//   1. Place SettingsLayout.ini next to DIMESettings.exe
//   2. Add any section/key from the struct below
//   3. Restart DIMESettings.exe — new values take effect immediately
//
// Example SettingsLayout.ini:
//   [Sidebar]
//   Width=320
//   [Card]
//   CornerRadius=12

#pragma once

#include <windows.h>
#include <dwmapi.h>

// ============================================================================
// All layout geometry in ONE place (px @96dpi, DPI-scaled at use site)
// ============================================================================
struct SettingsGeometry {
    // Sidebar
    int sidebarWidth          = 280;
    int sidebarItemH          = 40;
    int sidebarItemR          = 4;    // rounded highlight corner radius
    int sidebarIconSize       = 20;
    int sidebarIconGap        = 10;   // gap between icon and text
    int sidebarItemPadLeft    = 14;   // left padding before icon
    int sidebarAccentBarW     = 3;
    int sidebarAccentBarH     = 16;
    int sidebarAccentBarR     = 2;
    int sidebarCollapseBreak  = 800;  // below this width, sidebar collapses (sidebar 280 + min content ~520)
    int hamburgerSize         = 36;   // hamburger button size when sidebar collapsed

    // Content area
    int contentMargin         = 36;   // Win11 ≈36px from sidebar
    int topMargin             = 56;   // space for section title
    int breadcrumbH           = 44;

    // Card
    int cardRadius            = 8;
    int cardPad               = 10;   // Win11: 10 + 49 + 10 = 69px → 67px visual (border 1px each side)
    int cardGap               = 4;    // Win11: 3px visual + 1px shadow bleed

    // Row
    int rowHeight             = 49;   // two-line (label + description)
    int rowHeightSingle       = 38;   // single-line (no description)
    int categoryRowH          = 49;   // Level 0 category row
    int childRowH             = 50;   // expanded child row height (Win11 ≈50px)
    int rowIconSize           = 20;   // Win11 ≈20px icons
    int rowIconPadLeft        = 20;   // left padding before icon (inside card)
    int rowIconGap            = 14;   // gap between icon and text
    int rowSepIndent          = 54;   // separator indented past icon area

    // Toggle switch
    int toggleW               = 40;
    int toggleH               = 20;
    int toggleKnob            = 16;
    int toggleKnobBorderW     = 2;
    int toggleLabelGap        = 8;    // gap between "開啟" text and toggle

    // Controls
    int comboWidth            = 180;
    int editWidth             = 120;
    int buttonWidth           = 120;
    int controlRightMargin    = 16;

    // Color swatch
    int swatchSize            = 28;
    int swatchRadius          = 4;

    // Font sizes (pt, not px) — Win11 Settings measured at 96dpi
    // Win11 14px body = 10.5pt, 12px desc = 9pt, 28px title = 21pt
    int fontTitlePt           = 20;   // section title / breadcrumb (Win11 28px ≈ 21pt, 20 close match)
    int fontCardHeaderPt      = 10;   // card row label — regular (Win11 ≈10pt)
    int fontBodyPt            = 10;   // body text — regular weight (Win11 14px ≈ 10.5pt, use 10)
    int fontDescPt            = 9;    // description / secondary text (Win11 ≈9pt)

    // RichEdit editor (自建詞庫)
    int editorCardH           = 400;
    int editorHeaderH         = 52;    // card header + description height (before RichEdit)

    // Small offsets used in row layout
    int topPad                = 8;     // top padding before section title
    int sidebarStartY         = 56;    // sidebar items start Y — aligns with first card (= topMargin)
    int titleAreaH            = 40;    // section title area height
    int rowLabelTopOff        = 0;     // top offset for label in two-line row
    int rowLabelMidOff        = 5;     // positive = title bottom and desc top closer to midpoint
    int rowDescBotOff         = 0;     // bottom offset for description text
    int previewGap            = 28;    // gap below candidate/notify preview

    // Control sizing
    int ctrlHeight            = 24;    // standard combo/edit control height
    int buttonHeight          = 28;    // button height
    int actionBtnW            = 80;    // small action button (載入/匯出/匯入)
    int toggleLabelW          = 50;    // width for "開啟"/"關閉" text left of toggle
    int toggleLabelGapR       = 6;     // gap between toggle label and toggle track

    // Category/breadcrumb layout
    int chevronW              = 20;    // chevron column width (">", "∨")
    int bcModeNameW           = 200;   // breadcrumb: IME mode name max width
    int bcSepW                = 30;    // breadcrumb: separator "›" width
    int bcCatNameW            = 300;   // breadcrumb: category name max width

    // Combo dropdown (tall enough to show all items)
    int comboDropH            = 200;

    // Color swatch grid
    int swatchLabelH          = 14;    // height of swatch label text area
    int swatchCellGap         = 2;     // gap between swatch rows

    // Window
    int windowMinW            = 520;  // below sidebarCollapseBreak (740) to allow collapse
    int windowMinH            = 450;
    int windowDefaultW        = 900;
    int windowDefaultH        = 650;
};

// Single global instance
extern const SettingsGeometry g_geo;

// ============================================================================
// Theme colors — derived from system APIs, NOT hardcoded RGB
// ============================================================================
struct ThemeColors {
    COLORREF contentBg;
    COLORREF cardBg;
    COLORREF cardBorder;
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF rowSeparator;
    COLORREF rowHover;
    COLORREF sidebarBg;
    COLORREF sidebarHover;
    COLORREF sidebarSelect;
    COLORREF accent;
    COLORREF toggleOnTrack;
    COLORREF toggleOffBorder;
    COLORREF toggleOnKnob;
    COLORREF toggleOffKnob;
    // Secondary button (Win11 style: COLOR_BTNFACE-derived, NOT card white)
    COLORREF buttonFill;          // normal state fill
    COLORREF buttonFillHover;     // hovered state fill
    COLORREF buttonFillPressed;   // pressed state fill
    COLORREF buttonFillDisabled;  // disabled state fill
    COLORREF buttonBorder;        // normal/hover border
    COLORREF buttonText;          // enabled text
    COLORREF buttonTextDisabled;  // disabled text
};

// Compute theme colors from system APIs. Call on init + WM_THEMECHANGED.
ThemeColors ComputeThemeColors(bool isDark);

// Blend two colors. alpha: 0=100% c1, 255=100% c2.
COLORREF BlendColor(COLORREF c1, COLORREF c2, int alpha);

// DPI scaling helper
inline int ScaleForDpi(int value, int dpi) {
    return (dpi > 0) ? MulDiv(value, dpi, 96) : value;
}
