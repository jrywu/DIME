// SettingsWindowPaint.h — GDI / GDI+ painting of the settings window
//
// Phase 5b-7 of docs/RUST_MIGRATION_PLAN.md. The painters were lifted out of
// SettingsWindow.cpp so they can be compared pixel by pixel; the C++
// originals live in SettingsWindowPaint::Cpp. With DIME_USE_RUST_CONFIG the
// unqualified functions call dime-settings (paint.rs) instead; change both
// together. Double buffering, clipping, and the order of the pieces stay in
// the window procedures.

#pragma once

#include <windows.h>
#include "SettingsController.h"
#include "SettingsGeometry.h"
#include "SettingsWindowLayout.h"

namespace SettingsWindowPaint {

constexpr int kToggleSlots = 64;   // toggleOn[] is indexed by SettingsControlId

// Everything the page painters read. Fonts and brushes are owned by the caller.
struct PaintContext {
    ThemeColors theme;
    SettingsSnapshot snapshot;
    HFONT fontTitle;
    HFONT fontCardHeader;
    HFONT fontBody;
    HFONT fontDesc;
    HFONT fontMDL2;          // small Segoe MDL2 Assets (chevrons)
    HFONT fontMDL2Icon;      // large Segoe MDL2 Assets (row icons)
    HBRUSH brushSidebarBg;
    HBRUSH brushContentBg;
    HMODULE iconModule;      // module with the sidebar .ico resources
    UINT dpi;
    int hasMDL2;
    int clientW;             // client size of the window painted into
    int clientH;
    int cardW;               // CalcCardW (from the main window)
    COLORREF sysFace;        // GetSysColor(COLOR_3DFACE), read by the preview shadows
    int selectedModeIndex;
    int hoverSidebarIndex;
    int navLevel;
    int currentCardIndex;
    int hoverRowIndex;
    int currentMode;         // IME_MODE
    int scrollPos;
    int sidebarNarrowMode;
    int sidebarCollapsed;
    int colorGridExpanded;
    bool cardExpanded[SettingsWindowLayout::kExpandedSlots];
    BYTE toggleOn[kToggleSlots];   // BM_GETCHECK of the first control with that id
};

struct Fonts {
    HFONT title, cardHeader, body, desc, mdl2, mdl2Icon;
    int hasMDL2;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
void CreateFonts(UINT dpi, Fonts* out);
void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor);
void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc);
int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi);
void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace);
void PaintSidebar(HDC hdc, const PaintContext& ctx);
void PaintContent(HDC hdc, const PaintContext& ctx);
void PaintBreadcrumb(HDC hdc, const PaintContext& ctx);
void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg);
void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi);
void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius);
void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi);

// Main window frame: sidebar, then on a sub-page the breadcrumb strip (or,
// with the narrow sidebar open, the sidebar over the strip and the
// hamburger). hasContent: the content area exists.
void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent);
// Content area frame: the page, the narrow-mode sidebar overlay (clipped to
// the sidebar, shifted up by the breadcrumb), and the Level 0 hamburger.
// `content` has the content area's client size; `main` the main window's.
void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main);

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent);
void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main);
void CreateFonts(UINT dpi, Fonts* out);
void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor);
void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc);
int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi);
void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace);
void PaintSidebar(HDC hdc, const PaintContext& ctx);
void PaintContent(HDC hdc, const PaintContext& ctx);
void PaintBreadcrumb(HDC hdc, const PaintContext& ctx);
void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg);
void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi);
void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius);
void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi);
} // namespace Cpp

} // namespace SettingsWindowPaint
