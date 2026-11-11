// SettingsWindowLayout.h — window-free computations of SettingsWindow
//
// Phase 5b-6 of docs/RUST_MIGRATION_PLAN.md. Card / row geometry, hit tests,
// control placement, the custom-table buffer validator, and the themed
// message box layout were lifted out of SettingsWindow.cpp so they can be
// unit-tested and ported. The C++ originals live in SettingsWindowLayout::Cpp.
// With DIME_USE_RUST_CONFIG the unqualified functions call dime-settings
// (rust/crates/dime-settings) instead; change both together.
//
// `dpi` is the UINT the window passes to ScaleForDpi(int, UINT), which is
// always MulDiv(value, dpi, 96).

#pragma once

#include <windows.h>
#include <vector>
#include "SettingsController.h"
#include "SettingsPageLayout.h"

namespace SettingsWindowLayout {

constexpr int kExpandedSlots = 16;   // WindowData::cardExpanded

// Sub-page state that decides which rows show and how tall they are.
struct RowContext {
    UINT dpi;
    int scrollPos;
    int modeMask;
    int colorMode;
    int gridExpanded;
    int previewH;      // CalcCandidatePreviewH; only the preview row uses it
};

// kind: 0 nothing, 1 category row of `card`, 2 expanded child `row` of `card`.
struct CardListClick { int kind; int card; int row; };

// One card on a sub-page: rows [startRow, endRow), height includes 2 * cardPad.
struct RowSpan { int startRow; int endRow; int height; };

// spanStart: first row of the card under the point, or -1.
// row: shown row under the point in that card, or -1 for padding.
struct RowCardHit { int spanStart; int row; };

struct PlanInput {
    UINT dpi;
    int selectedModeIndex;   // 0-3 IME mode, 5 = 載入碼表
    int navLevel;
    int currentCardIndex;
    int modeMask;
    int colorMode;
    int gridExpanded;
    int previewH;
    bool expanded[kExpandedSlots];
};

// kind 0: CreateRowControl with cy = y.
// kind 1: CreateRichEditorControl with curY = y, thisH = h.
// kind 2: CreateColorGridControls with curY = y.
struct Placement { int kind; int card; int row; int y; int h; };

// setTotal: totalContentHeight = totalHeight. populate: call PopulateControls.
struct PlanResult { int setTotal; int totalHeight; int populate; };

// x, y, w, h of a child control; visH is ControlHandle::visH.
struct CtrlRect { int x; int y; int w; int h; int visH; };

// labels: 0 是(Y), 1 否(N), 2 取消, 3 確定.
struct MsgBoxButtons { int count; int ids[3]; int labels[3]; };

struct MsgBoxLayout {
    RECT rcClient;
    int textX, textY, textW, textH;
    int btnY, btnW, btnH, btnGap;
    int iconX, iconY, iconSize;
    int margin;
};

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW);
int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron);
int RowHeight(const LayoutRow& lr, UINT dpi, int previewH);
bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded);
int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos);
CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos);
std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx);
RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx);
PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out);
bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out);
CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi);
void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save);
CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb);
int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport);
MsgBoxButtons MsgBoxButtonsFor(UINT type);
int MsgBoxIcon(UINT type);   // 0 none, 1 warning, 2 error, 3 question, 4 information
int MsgBoxMaxTextWidth(UINT dpi);
MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi);
int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount);
int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y);
int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk);   // -1 = not handled
int MsgBoxCloseResult(const MsgBoxButtons& b);                          // -1 = keep result

// ---- C++ originals --------------------------------------------------------
namespace Cpp {
int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW);
int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron);
int RowHeight(const LayoutRow& lr, UINT dpi, int previewH);
bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded);
int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos);
CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos);
std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx);
RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx);
PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out);
bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out);
CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi);
void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save);
CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb);
int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport);
MsgBoxButtons MsgBoxButtonsFor(UINT type);
int MsgBoxIcon(UINT type);
int MsgBoxMaxTextWidth(UINT dpi);
MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi);
int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount);
int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y);
int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk);
int MsgBoxCloseResult(const MsgBoxButtons& b);
} // namespace Cpp

} // namespace SettingsWindowLayout
