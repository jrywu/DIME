// SettingsWindowLayout.cpp — window-free computations of SettingsWindow.
// See SettingsWindowLayout.h. The Cpp:: functions are the code that used to
// live inline in SettingsWindow.cpp; dime-settings (Rust) mirrors them.

#include <windows.h>
#include <strsafe.h>
#include "..\BaseStructure.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsWindowLayout.h"
#include "SettingsGeometry.h"
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#include <cstddef>
#endif

namespace SettingsWindowLayout {

static_assert(sizeof(RowContext) == 24, "RowContext must match dime-settings");
static_assert(sizeof(PlanInput) == 48, "PlanInput must match dime-settings");
static_assert(sizeof(Placement) == 20, "Placement must match dime-settings");
static_assert(sizeof(PlanResult) == 12, "PlanResult must match dime-settings");
static_assert(sizeof(CtrlRect) == 20, "CtrlRect must match dime-settings");
static_assert(sizeof(MsgBoxButtons) == 28, "MsgBoxButtons must match dime-settings");
static_assert(sizeof(MsgBoxLayout) == 64, "MsgBoxLayout must match dime-settings");
static_assert(sizeof(bool) == 1, "PlanInput::expanded is one byte per slot");

namespace Cpp {

int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW)
{
    int sidebarW = sidebarTakesSpace ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    return clientRight - sidebarW - 2 * margin - vscrollW;
}

int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int ctrlRM = ScaleForDpi(g_geo.controlRightMargin, dpi);
    int chevArea = hasChevron ? ScaleForDpi(g_geo.chevronW + 4, dpi) : 0;
    return margin + cardW - pad - ctrlRM - chevArea;
}

int RowHeight(const LayoutRow& lr, UINT dpi, int previewH)
{
    if (lr.type == RowType::CandidatePreview)
        return previewH;
    if (lr.type == RowType::ColorGrid) {
        int swS = ScaleForDpi(g_geo.swatchSize, dpi);
        int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
        return 2 * (swS + swGap);
    }
    if (lr.type == RowType::RichEditor)
        return ScaleForDpi(g_geo.editorCardH, dpi);
    return lr.description ? ScaleForDpi(g_geo.rowHeight, dpi) : ScaleForDpi(g_geo.rowHeightSingle, dpi);
}

bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded)
{
    if (!IsRowVisible(lr, modeMask)) return false;
    if (lr.expandParent != CTRL_NONE && lr.expandParent == CTRL_COLOR_MODE
        && (colorMode != 3 || !gridExpanded)) return false;
    return true;
}

static bool CardVisible(const LayoutCard& card, int modeMask)
{
    bool vis = (card.rowCount == 0);
    if (!vis) for (int r = 0; !vis && r < card.rowCount; r++)
        if (IsRowVisible(card.rows[r], modeMask)) vis = true;
    return vis;
}

int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
    int curY2 = ScaleForDpi(g_geo.topMargin, dpi) + pad - scrollPos;
    for (int i = 0; i < cardCount; i++) {
        if (!CardVisible(cards[i], modeMask)) continue;
        int numCh = 0;
        int expIdx = expandBaseIdx + i;
        if (cards[i].action == RowAction::ExpandSection && cardExpanded[expIdx] && cards[i].rows)
            for (int r = 0; r < cards[i].rowCount; r++)
                if (IsRowVisible(cards[i].rows[r], modeMask)) numCh++;
        int cardH = 2 * pad + catRowH + (numCh > 0 ? numCh * (cRowH + 1) : 0);
        if (y >= curY2 - pad && y < curY2 - pad + cardH) return i;
        curY2 += cardH - 2 * pad + 2 * pad + cardGap;
    }
    return -1;
}

CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
    int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad - scrollPos;

    for (int i = 0; i < cardCount; i++) {
        if (!CardVisible(cards[i], modeMask)) continue;
        int expIdx = expandBaseIdx + i;

        if (y >= curY && y < curY + catRowH)
            return { 1, i, -1 };

        if (cards[i].action == RowAction::ExpandSection && cardExpanded[expIdx] && cards[i].rows) {
            int childY = curY + catRowH + pad;
            int numCh = 0;
            for (int r = 0; r < cards[i].rowCount; r++) {
                const LayoutRow& lr = cards[i].rows[r];
                if (!IsRowVisible(lr, modeMask)) continue;
                childY += 1; // 1px separator
                if (y >= childY && y < childY + cRowH)
                    return { 2, i, r };
                childY += cRowH;
                numCh++;
            }
            int totalCardH = 2 * pad + catRowH + numCh * (cRowH + 1);
            curY += totalCardH + cardGap;
        } else {
            curY += catRowH + 2 * pad + cardGap;
        }
    }
    return { 0, -1, -1 };
}

std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx)
{
    int pad = ScaleForDpi(g_geo.cardPad, ctx.dpi);
    std::vector<RowSpan> spans;
    for (int r = 0; card.rows && r < card.rowCount; r++) {
        const LayoutRow& lr = card.rows[r];
        if (!RowShown(lr, ctx.modeMask, ctx.colorMode, ctx.gridExpanded != 0)) continue;
        if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
            int h = RowHeight(lr, ctx.dpi, ctx.previewH);
            int endR = r + 1;
            while (endR < card.rowCount && card.rows[endR].expandParent != CTRL_NONE
                   && !card.rows[endR].cardBreakBefore) {
                if (RowShown(card.rows[endR], ctx.modeMask, ctx.colorMode, ctx.gridExpanded != 0))
                    h += RowHeight(card.rows[endR], ctx.dpi, ctx.previewH);
                endR++;
            }
            spans.push_back({ r, endR, h + 2 * pad });
            r = endR - 1;
        }
    }
    return spans;
}

RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx)
{
    int pad = ScaleForDpi(g_geo.cardPad, ctx.dpi);
    int cardGap = ScaleForDpi(g_geo.cardGap, ctx.dpi);
    int cardTop = ScaleForDpi(g_geo.topPad, ctx.dpi) - ctx.scrollPos;
    for (const RowSpan& span : Cpp::RowSpans(card, ctx)) {
        if (y >= cardTop && y < cardTop + span.height) {
            int curY = cardTop + pad;
            for (int ri = span.startRow; ri < span.endRow; ri++) {
                const LayoutRow& lr = card.rows[ri];
                if (!RowShown(lr, ctx.modeMask, ctx.colorMode, ctx.gridExpanded != 0)) continue;
                int thisH = RowHeight(lr, ctx.dpi, ctx.previewH);
                if (y >= curY && y < curY + thisH) return { span.startRow, ri };
                curY += thisH;
            }
            return { span.startRow, -1 };
        }
        cardTop += span.height + cardGap;
    }
    return { -1, -1 };
}

PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out)
{
    out.clear();
    UINT dpi = in.dpi;
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int ctrlH = ScaleForDpi(g_geo.ctrlHeight, dpi);
    int btnH = ScaleForDpi(g_geo.buttonHeight, dpi);

    if (in.selectedModeIndex == 5) {
        // 載入碼表 page — same per-card structure as Level 0
        int cardGapLT = ScaleForDpi(g_geo.cardGap, dpi);
        int cRowHLT = ScaleForDpi(g_geo.childRowH, dpi);
        int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad;
        for (int i = 0; i < loadCount; i++) {
            int expIdx = 4 + i;
            if (load[i].action == RowAction::None && load[i].rows && load[i].rowCount > 0) {
                out.push_back({ 0, i, 0, curY + (catRowH - btnH) / 2, 0 });
                curY += catRowH + 2 * pad + cardGapLT;
            } else if (load[i].action == RowAction::ExpandSection && in.expanded[expIdx] && load[i].rows) {
                int childY = curY + catRowH + pad;
                for (int r = 0; r < load[i].rowCount; r++) {
                    childY += 1;
                    out.push_back({ 0, i, r, childY + (cRowHLT - btnH) / 2, 0 });
                    childY += cRowHLT;
                }
                int numCh = load[i].rowCount;
                int totalCardH = 2 * pad + catRowH + numCh * (cRowHLT + 1);
                curY += totalCardH + cardGapLT;
            } else {
                curY += catRowH + 2 * pad + cardGapLT;
            }
        }
        return { 1, curY, 0 };
    }

    if (in.navLevel == 0 && in.selectedModeIndex < 4) {
        int cardGapL0 = ScaleForDpi(g_geo.cardGap, dpi);
        int curY = ScaleForDpi(g_geo.topMargin, dpi) + pad;
        for (int i = 0; i < imeCount; i++) {
            if (!CardVisible(ime[i], in.modeMask)) continue;
            if (ime[i].action == RowAction::ExpandSection && in.expanded[i] && ime[i].rows) {
                int cRowHL0 = ScaleForDpi(g_geo.childRowH, dpi);
                int childY = curY + catRowH + pad;
                int numCh = 0;
                for (int r = 0; r < ime[i].rowCount; r++) {
                    const LayoutRow& lr = ime[i].rows[r];
                    if (!IsRowVisible(lr, in.modeMask)) continue;
                    childY += 1;
                    out.push_back({ 0, i, r, childY + (cRowHL0 - ctrlH) / 2, 0 });
                    childY += cRowHL0;
                    numCh++;
                }
                int totalCardH = 2 * pad + catRowH + numCh * (cRowHL0 + 1);
                curY += totalCardH + cardGapL0;
            } else {
                curY += catRowH + 2 * pad + cardGapL0;
            }
        }
        return { 1, curY, 1 };
    }

    if (in.navLevel == 1 && in.selectedModeIndex < 4) {
        if (in.currentCardIndex < 0 || in.currentCardIndex >= imeCount)
            return { 1, 0, 0 };
        const LayoutCard& card = ime[in.currentCardIndex];
        int cardGapDpi = ScaleForDpi(g_geo.cardGap, dpi);
        int curY = ScaleForDpi(g_geo.topPad, dpi) + pad;
        bool firstTopLevel = true;
        if (card.rows) {
            for (int r = 0; r < card.rowCount; r++) {
                const LayoutRow& lr = card.rows[r];
                if (!RowShown(lr, in.modeMask, in.colorMode, in.gridExpanded != 0)) continue;
                if (lr.expandParent == CTRL_NONE || lr.cardBreakBefore) {
                    if (!firstTopLevel)
                        curY += pad + cardGapDpi + pad;
                    firstTopLevel = false;
                }
                int thisH = RowHeight(lr, dpi, in.previewH);
                int cy = curY + (thisH - ctrlH) / 2;
                switch (lr.type) {
                case RowType::RichEditor:
                    out.push_back({ 1, in.currentCardIndex, r, curY, thisH });
                    break;
                case RowType::Toggle:
                case RowType::ComboBox:
                case RowType::Edit:
                case RowType::Clickable:
                case RowType::Button:
                    out.push_back({ 0, in.currentCardIndex, r, cy, thisH });
                    break;
                case RowType::ColorGrid:
                    out.push_back({ 2, in.currentCardIndex, r, curY, thisH });
                    break;
                default:
                    break;
                }
                curY += thisH;
            }
        }
        return { 1, curY + pad + ScaleForDpi(g_geo.cardGap, dpi), 1 };
    }

    return { 0, 0, 0 };
}

bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out)
{
    int comboW = ScaleForDpi(g_geo.comboWidth, dpi);
    int editW = ScaleForDpi(g_geo.editWidth, dpi);
    int btnW = ScaleForDpi(g_geo.buttonWidth, dpi);
    switch (type) {
    case RowType::Toggle:
        *out = { 0, 0, 0, 0, 0 };
        return true;
    case RowType::ComboBox:
        *out = { ctrlRight - comboW, cy, comboW, ScaleForDpi(g_geo.comboDropH, dpi), ScaleForDpi(g_geo.ctrlHeight, dpi) };
        return true;
    case RowType::Edit:
        *out = { ctrlRight - editW, cy, editW, ctrlH, ctrlH };
        return true;
    case RowType::Button:
    case RowType::Clickable:
        *out = { ctrlRight - btnW, cy, btnW, btnH, btnH };
        return true;
    default:
        return false;
    }
}

CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi)
{
    int swS = ScaleForDpi(g_geo.swatchSize, dpi);
    int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
    int cellH = swS + swGap;
    int rightEdge = ctrlRight - margin;
    int colW = rightEdge / 3;
    int col = index % 3, row2 = index / 3;
    int colRight = margin + (col + 1) * colW;
    return { colRight - swS, curY + row2 * cellH, swS, swS, swS };
}

void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save)
{
    int reX = margin + pad;
    int reY = curY + ScaleForDpi(g_geo.editorHeaderH, dpi);
    int reW = cardW - 2 * pad;
    int reH = thisH - ScaleForDpi(g_geo.editorHeaderH + g_geo.rowHeightSingle, dpi);
    int saveY = reY + reH + ScaleForDpi(g_geo.topPad, dpi);
    *editor = { reX, reY, reW, reH, reH };
    *save = { ctrlRight - btnW, saveY, btnW, btnH, btnH };
}

CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb)
{
    int sidebarW = sidebarTakesSpace ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
    int bcH = hasBreadcrumb ? ScaleForDpi(g_geo.breadcrumbH, dpi) : 0;
    return { sidebarW, bcH, clientW - sidebarW, clientH - bcH, clientH - bcH };
}

int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport)
{
    if (outFirstErrLine) *outFirstErrLine = 0;
    if (outSummary && summaryCch > 0) outSummary[0] = 0;
    if (!text || wlen == 0) return 0;

    int errCount = 0;
    int lineNo = 0;
    const wchar_t* p = text;
    const wchar_t* lineStart = p;
    const wchar_t* endBuf = text + wlen;

    while (p <= endBuf) {
        if (p == endBuf || *p == L'\n') {
            ++lineNo;
            int len = (int)(p - lineStart);
            if (len > 0 && lineStart[len - 1] == L'\r') --len;
            auto v = SettingsModel::ValidateLine(lineStart, len, mode, maxCodes, pEng);
            if (!v.valid) {
                if (errCount == 0 && outFirstErrLine) *outFirstErrLine = lineNo;
                if (errCount < maxReport && outSummary && summaryCch > 0) {
                    wchar_t one[64];
                    StringCchPrintfW(one, _countof(one), L"  第 %d 行\n", lineNo);
                    StringCchCatW(outSummary, summaryCch, one);
                }
                ++errCount;
            }
            if (p == endBuf) break;
            lineStart = p + 1;
        }
        ++p;
    }
    return errCount;
}

MsgBoxButtons MsgBoxButtonsFor(UINT type)
{
    MsgBoxButtons b = {};
    if (type & MB_YESNOCANCEL) {
        b.count = 3;
        b.ids[0] = IDYES;    b.labels[0] = 0;
        b.ids[1] = IDNO;     b.labels[1] = 1;
        b.ids[2] = IDCANCEL; b.labels[2] = 2;
    } else if (type & MB_YESNO) {
        b.count = 2;
        b.ids[0] = IDYES;    b.labels[0] = 0;
        b.ids[1] = IDNO;     b.labels[1] = 1;
    } else if (type & MB_OKCANCEL) {
        b.count = 2;
        b.ids[0] = IDOK;     b.labels[0] = 3;
        b.ids[1] = IDCANCEL; b.labels[1] = 2;
    } else {
        b.count = 1;
        b.ids[0] = IDOK;     b.labels[0] = 3;
    }
    return b;
}

int MsgBoxIcon(UINT type)
{
    if (type & MB_ICONWARNING) return 1;
    if (type & MB_ICONERROR) return 2;
    if (type & MB_ICONQUESTION) return 3;
    if (type & MB_ICONINFORMATION) return 4;
    return 0;
}

int MsgBoxMaxTextWidth(UINT dpi)
{
    return ScaleForDpi(340, dpi);
}

MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi)
{
    MsgBoxLayout L = {};
    L.margin = ScaleForDpi(20, dpi);
    L.iconSize = ScaleForDpi(32, dpi);
    int iconGap = ScaleForDpi(14, dpi);
    L.btnH = ScaleForDpi(30, dpi);
    L.btnW = ScaleForDpi(88, dpi);
    L.btnGap = ScaleForDpi(8, dpi);
    L.iconX = L.margin;
    L.iconY = L.margin;
    L.textX = hasIcon ? (L.margin + L.iconSize + iconGap) : L.margin;

    L.textW = textW;
    L.textH = textH;
    if (hasIcon && L.textH < L.iconSize) L.textH = L.iconSize;
    L.textY = L.margin;

    int contentBottom = L.margin + L.textH + ScaleForDpi(20, dpi);
    L.btnY = contentBottom;
    int totalBtnW = btnCount * L.btnW + (btnCount - 1) * L.btnGap;
    int clientW = L.textX + L.textW + L.margin;
    int minW = totalBtnW + 2 * L.margin;
    if (clientW < minW) clientW = minW;
    int clientH = L.btnY + L.btnH + L.margin;
    SetRect(&L.rcClient, 0, 0, clientW, clientH);
    return L;
}

int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount)
{
    int totalBtnW = btnCount * L.btnW + (btnCount - 1) * L.btnGap;
    return L.rcClient.right - L.margin - totalBtnW;
}

int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y)
{
    int btnX = Cpp::MsgBoxFirstButtonX(L, btnCount);
    for (int i = 0; i < btnCount; i++) {
        if (x >= btnX && x < btnX + L.btnW && y >= L.btnY && y < L.btnY + L.btnH)
            return i;
        btnX += L.btnW + L.btnGap;
    }
    return -1;
}

int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk)
{
    if (vk == VK_RETURN)
        return b.ids[defaultBtn];
    if (vk == VK_ESCAPE) {
        for (int i = 0; i < b.count; i++) {
            if (b.ids[i] == IDCANCEL) return IDCANCEL;
            if (b.ids[i] == IDNO) return IDNO;
        }
        return b.ids[defaultBtn];
    }
    return -1;
}

int MsgBoxCloseResult(const MsgBoxButtons& b)
{
    for (int i = 0; i < b.count; i++)
        if (b.ids[i] == IDCANCEL) return IDCANCEL;
    return -1;
}

} // namespace Cpp

// ============================================================================
// Dispatch
// ============================================================================
#ifdef DIME_USE_RUST_CONFIG

static_assert(offsetof(PlanInput, expanded) == 32, "PlanInput::expanded");
static_assert(offsetof(MsgBoxLayout, margin) == 60, "MsgBoxLayout::margin");

namespace {
extern "C" int32_t SettingsWindowLayoutCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr) return 0;
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}

int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW)
{
    return dime_settings_card_width(clientRight, dpi, sidebarTakesSpace ? 1 : 0, vscrollW);
}

int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron)
{
    return dime_settings_ctrl_right(margin, cardW, dpi, hasChevron ? 1 : 0);
}

int RowHeight(const LayoutRow& lr, UINT dpi, int previewH)
{
    return dime_settings_row_height(&lr, dpi, previewH);
}

bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded)
{
    return dime_settings_row_shown(&lr, modeMask, colorMode, gridExpanded ? 1 : 0) != 0;
}

int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    return dime_settings_card_list_hit_test(y, cards, cardCount, modeMask,
        reinterpret_cast<const uint8_t*>(cardExpanded), expandBaseIdx, dpi, scrollPos);
}

CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    CardListClick c = { 0, -1, -1 };
    dime_settings_card_list_click(y, cards, cardCount, modeMask,
        reinterpret_cast<const uint8_t*>(cardExpanded), expandBaseIdx, dpi, scrollPos, &c);
    return c;
}

std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx)
{
    size_t count = 0;
    dime_settings_row_spans(&card, &ctx, nullptr, 0, &count);
    std::vector<RowSpan> spans(count);
    if (count > 0) {
        size_t written = 0;
        dime_settings_row_spans(&card, &ctx, spans.data(), spans.size(), &written);
        if (written < spans.size()) spans.resize(written);
    }
    return spans;
}

RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx)
{
    RowCardHit h = { -1, -1 };
    dime_settings_row_cards_hit(y, &card, &ctx, &h);
    return h;
}

PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out)
{
    PlanResult res = { 0, 0, 0 };
    size_t count = 0;
    dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, nullptr, 0, &count, &res);
    out.assign(count, Placement{});
    if (count > 0) {
        size_t written = 0;
        dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, out.data(), out.size(), &written, &res);
        if (written < out.size()) out.resize(written);
    }
    return res;
}

bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out)
{
    return dime_settings_row_control_rect(static_cast<int32_t>(type), ctrlRight, cy, ctrlH, btnH, dpi, out) != 0;
}

CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi)
{
    CtrlRect r = {};
    dime_settings_swatch_rect(index, margin, curY, ctrlRight, dpi, &r);
    return r;
}

void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save)
{
    dime_settings_rich_editor_rects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi, editor, save);
}

CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb)
{
    CtrlRect r = {};
    dime_settings_content_area_rect(clientW, clientH, dpi, sidebarTakesSpace ? 1 : 0, hasBreadcrumb ? 1 : 0, &r);
    return r;
}

int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport)
{
    return dime_settings_validate_buffer(reinterpret_cast<const uint16_t*>(text), wlen,
        static_cast<int32_t>(mode), maxCodes,
        pEng ? &SettingsWindowLayoutCharOk : nullptr, pEng, maxReport,
        outFirstErrLine, reinterpret_cast<uint16_t*>(outSummary), summaryCch);
}

MsgBoxButtons MsgBoxButtonsFor(UINT type)
{
    MsgBoxButtons b = {};
    dime_settings_msgbox_buttons(type, &b);
    return b;
}

int MsgBoxIcon(UINT type) { return dime_settings_msgbox_icon(type); }

int MsgBoxMaxTextWidth(UINT dpi) { return dime_settings_msgbox_max_text_width(dpi); }

MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi)
{
    MsgBoxLayout L = {};
    dime_settings_msgbox_layout(hasIcon ? 1 : 0, btnCount, textW, textH, dpi, &L);
    return L;
}

int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount)
{
    return dime_settings_msgbox_first_button_x(&L, btnCount);
}

int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y)
{
    return dime_settings_msgbox_button_at(&L, btnCount, x, y);
}

int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk)
{
    return dime_settings_msgbox_key_result(&b, defaultBtn, vk);
}

int MsgBoxCloseResult(const MsgBoxButtons& b)
{
    return dime_settings_msgbox_close_result(&b);
}

#else

int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW) { return Cpp::CardWidth(clientRight, dpi, sidebarTakesSpace, vscrollW); }
int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron) { return Cpp::CtrlRight(margin, cardW, dpi, hasChevron); }
int RowHeight(const LayoutRow& lr, UINT dpi, int previewH) { return Cpp::RowHeight(lr, dpi, previewH); }
bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded) { return Cpp::RowShown(lr, modeMask, colorMode, gridExpanded); }
int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    return Cpp::CardListHitTest(y, cards, cardCount, modeMask, cardExpanded, expandBaseIdx, dpi, scrollPos);
}
CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    return Cpp::CardListClickTarget(y, cards, cardCount, modeMask, cardExpanded, expandBaseIdx, dpi, scrollPos);
}
std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx) { return Cpp::RowSpans(card, ctx); }
RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx) { return Cpp::RowCardsHit(y, card, ctx); }
PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out)
{
    return Cpp::PlanControls(in, ime, imeCount, load, loadCount, out);
}
bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out)
{
    return Cpp::RowControlRect(type, ctrlRight, cy, ctrlH, btnH, dpi, out);
}
CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi) { return Cpp::SwatchRect(index, margin, curY, ctrlRight, dpi); }
void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save)
{
    Cpp::RichEditorRects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi, editor, save);
}
CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb)
{
    return Cpp::ContentAreaRect(clientW, clientH, dpi, sidebarTakesSpace, hasBreadcrumb);
}
int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport)
{
    return Cpp::ValidateBuffer(text, wlen, mode, maxCodes, pEng, outFirstErrLine, outSummary, summaryCch, maxReport);
}
MsgBoxButtons MsgBoxButtonsFor(UINT type) { return Cpp::MsgBoxButtonsFor(type); }
int MsgBoxIcon(UINT type) { return Cpp::MsgBoxIcon(type); }
int MsgBoxMaxTextWidth(UINT dpi) { return Cpp::MsgBoxMaxTextWidth(dpi); }
MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi) { return Cpp::MsgBoxLayoutFor(hasIcon, btnCount, textW, textH, dpi); }
int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount) { return Cpp::MsgBoxFirstButtonX(L, btnCount); }
int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y) { return Cpp::MsgBoxButtonAt(L, btnCount, x, y); }
int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk) { return Cpp::MsgBoxKeyResult(b, defaultBtn, vk); }
int MsgBoxCloseResult(const MsgBoxButtons& b) { return Cpp::MsgBoxCloseResult(b); }

#endif

} // namespace SettingsWindowLayout
