// SettingsNav.cpp — see SettingsNav.h. Cpp::Handle is the logic that used to
// live in SettingsWindow::WndProc / ContentWndProc (WM_SIZE, WM_MOUSEWHEEL,
// WM_VSCROLL, WM_MOUSEMOVE, WM_MOUSELEAVE, WM_LBUTTONDOWN), with every side
// effect turned into a NavEffect. dime-settings (nav.rs) mirrors it.

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsNav.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsGeometry.h"
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#include <cstddef>
#endif

namespace SettingsNav {

static_assert(sizeof(NavState) == 64, "NavState must match dime-settings");
static_assert(sizeof(NavEvent) == 36, "NavEvent must match dime-settings");
static_assert(sizeof(NavEffect) == 32, "NavEffect must match dime-settings");

namespace Cpp {

namespace {

struct Out {
    std::vector<NavEffect>& v;
    void add(int kind, int arg = 0) { v.push_back({ kind, arg, 0, 0, {} }); }
    void invalidateMain(bool erase) { v.push_back({ FX_INVALIDATE_MAIN, 0, erase ? 1 : 0, 0, {} }); }
    void invalidateMainRect(bool erase, RECT rc) { v.push_back({ FX_INVALIDATE_MAIN, 0, erase ? 1 : 0, 1, rc }); }
    void invalidateContent(bool erase) { v.push_back({ FX_INVALIDATE_CONTENT, 0, erase ? 1 : 0, 0, {} }); }
};

int ClampScroll(int pos, int max) { return SettingsModel::ClampScrollPos(pos, max); }

void Scroll(NavState& s, int newPos, Out& o)
{
    int oldPos = s.scrollPos;
    s.scrollPos = ClampScroll(newPos, s.scrollMax);
    if (s.scrollPos != oldPos) o.add(FX_SCROLL_CHANGED);
}

void Wheel(NavState& s, const NavEvent& ev, Out& o)
{
    Scroll(s, s.scrollPos - MulDiv(ev.delta, ScaleForDpi(g_geo.rowHeight * 3, ev.dpi), WHEEL_DELTA), o);
}

void NavigateBack(NavState& s, Out& o)
{
    s.navLevel = 0;
    s.scrollPos = 0;
    o.add(FX_REBUILD);
    o.invalidateMain(true);
    o.invalidateContent(true);
}

void NavigateToCategory(NavState& s, int cardIndex, Out& o)
{
    s.navLevel = 1;
    s.currentCardIndex = cardIndex;
    s.scrollPos = 0;
    o.add(FX_REBUILD);
    o.invalidateMain(true);
    o.invalidateContent(true);
}

void ShowLoadTablePage(NavState& s, Out& o)
{
    s.selectedModeIndex = 5;
    s.navLevel = 0;
    s.scrollPos = 0;
    int ltCount = 0;
    const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
    for (int li = 0; li < ltCount && (4 + li) < 16; li++)
        s.cardExpanded[4 + li] = ltCards[li].defaultExpanded;
    o.add(FX_REBUILD);
    o.invalidateMain(true);
    o.invalidateContent(true);
}

void SelectSidebarIndex(NavState& s, int idx, Out& o)
{
    if (idx < 4) {
        IME_MODE newMode = SettingsModel::IndexToMode(idx);
        if ((int)newMode != s.currentMode || s.navLevel == 1 || s.selectedModeIndex != idx)
            o.add(FX_SWITCH_MODE, (int)newMode);
    } else if (idx == 5) {
        ShowLoadTablePage(s, o);
    }
}

void ToggleSidebar(NavState& s, Out& o)
{
    s.sidebarCollapsed = !s.sidebarCollapsed;
    o.add(s.sidebarCollapsed ? FX_SHOW_ALL_CONTROLS : FX_HIDE_ALL_CONTROLS);
}

SettingsWindowLayout::RowContext RowCtx(const NavState& s, const NavEvent& ev)
{
    SettingsWindowLayout::RowContext ctx = {};
    ctx.dpi = ev.dpi;
    ctx.scrollPos = s.scrollPos;
    ctx.modeMask = SettingsModel::GetModeBitmask((IME_MODE)s.currentMode);
    ctx.colorMode = ev.colorMode;
    ctx.gridExpanded = s.colorGridExpanded ? 1 : 0;
    ctx.previewH = ev.previewH;
    return ctx;
}

void MainLButton(NavState& s, const NavEvent& ev, Out& o)
{
    UINT dpi = ev.dpi;
    int x = ev.x, y = ev.y;
    int sidebarW = (!s.sidebarNarrowMode && !s.sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;

    // Hamburger in the breadcrumb strip (Level 1 narrow mode)
    if (s.sidebarNarrowMode && s.navLevel == 1) {
        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
        if (x >= sidebarW && x < sidebarW + hbSize && y < hbSize) {
            ToggleSidebar(s, o);
            o.invalidateContent(true);
            o.invalidateMain(true);
            return;
        }
    }

    // Breadcrumb back link
    if (x >= sidebarW && s.navLevel == 1 && s.selectedModeIndex < 4) {
        int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
        if (y < bcH) {
            int margin = ScaleForDpi(g_geo.contentMargin, dpi);
            int relX = x - sidebarW;
            if (relX >= margin && relX < margin + ScaleForDpi(g_geo.bcModeNameW, dpi)) {
                if (ev.customTableDirty) o.add(FX_ASK_SAVE_THEN_BACK);
                else NavigateBack(s, o);
                return;
            }
        }
        return;
    }

    if (s.sidebarCollapsed) return;
    if (x >= sidebarW) return;

    int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
    int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
    int sidebarCount = 0;
    const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
    int idx = SettingsModel::SidebarHitTest(y - startY, itemH, sidebarCount);
    if (idx < 0 || idx >= sidebarCount) return;
    if (sItems[idx].iconResourceId == -1) return;

    SelectSidebarIndex(s, idx, o);
    if (s.sidebarNarrowMode) {
        s.sidebarCollapsed = 1;
        o.add(FX_SHOW_ALL_CONTROLS);
        o.invalidateContent(true);
        o.invalidateMain(true);
    }
}

void MainMouseMove(NavState& s, const NavEvent& ev, Out& o)
{
    UINT dpi = ev.dpi;
    int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
    int newHover = -1;
    if (ev.x < sidebarW && !s.sidebarCollapsed) {
        int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
        int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
        int sc = 0; ::GetSidebarItems(&sc);
        newHover = SettingsModel::SidebarHitTest(ev.y - startY, itemH, sc);
    }
    if (newHover != s.hoverSidebarIndex) {
        s.hoverSidebarIndex = newHover;
        o.invalidateMainRect(false, { 0, 0, sidebarW, 32767 });
        o.add(FX_TRACK_LEAVE_MAIN);
    }
}

void ContentMouseMove(NavState& s, const NavEvent& ev, Out& o)
{
    UINT dpi = ev.dpi;
    int x = ev.x, y = ev.y;

    // Sidebar hover in narrow overlay mode
    if (s.sidebarNarrowMode && !s.sidebarCollapsed) {
        int sidebarWM = ScaleForDpi(g_geo.sidebarWidth, dpi);
        if (x < sidebarWM) {
            bool hasBc = (s.navLevel == 1 && s.selectedModeIndex < 4);
            int bcOff = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpi) : 0;
            int mainY = y + bcOff;
            int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
            int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
            int sc = 0; ::GetSidebarItems(&sc);
            int newSbHover = SettingsModel::SidebarHitTest(mainY - startY, itemH, sc);
            if (newSbHover != s.hoverSidebarIndex) {
                s.hoverSidebarIndex = newSbHover;
                o.invalidateContent(false);
                o.add(FX_TRACK_LEAVE_CONTENT);
            }
            return;
        } else if (s.hoverSidebarIndex != -1) {
            s.hoverSidebarIndex = -1;
            o.invalidateContent(false);
        }
    }

    int newHover = -1;
    if (s.navLevel == 0 && s.selectedModeIndex < 4) {
        int layoutCount = 0;
        const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
        int modeMask = SettingsModel::GetModeBitmask((IME_MODE)s.currentMode);
        newHover = SettingsWindowLayout::CardListHitTest(y, cards, layoutCount, modeMask, s.cardExpanded, 0, dpi, s.scrollPos);
    } else if (s.selectedModeIndex == 5) {
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        newHover = SettingsWindowLayout::CardListHitTest(y, ltCards, ltCount, SM_MODE_ALL, s.cardExpanded, 4, dpi, s.scrollPos);
    } else if (s.navLevel == 1 && s.selectedModeIndex < 4) {
        int layoutCount = 0;
        const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
        if (s.currentCardIndex >= 0 && s.currentCardIndex < layoutCount)
            newHover = SettingsWindowLayout::RowCardsHit(y, allCards[s.currentCardIndex], RowCtx(s, ev)).spanStart;
    }
    if (newHover != s.hoverRowIndex) {
        s.hoverRowIndex = newHover;
        o.invalidateContent(false);
        o.add(FX_TRACK_LEAVE_CONTENT);
    }
}

// HandleCardListClick: true when the click was consumed.
bool CardListClick(NavState& s, const NavEvent& ev, const LayoutCard* cards, int count,
    int modeMask, int base, Out& o)
{
    SettingsWindowLayout::CardListClick c = SettingsWindowLayout::CardListClickTarget(
        ev.y, cards, count, modeMask, s.cardExpanded, base, ev.dpi, s.scrollPos);
    if (c.kind == 1) {
        int expIdx = base + c.card;
        if (cards[c.card].action == RowAction::NavigateToCard) {
            NavigateToCategory(s, c.card, o);
        } else if (cards[c.card].action == RowAction::ExpandSection) {
            s.cardExpanded[expIdx] = !s.cardExpanded[expIdx];
            s.scrollPos = 0;
            o.add(FX_REBUILD);
            o.add(FX_UPDATE_SCROLL_INFO);
            o.invalidateContent(false);
            o.add(FX_UPDATE_CONTENT);
        }
        return true;
    }
    if (c.kind == 2) {
        const LayoutRow& lr = cards[c.card].rows[c.row];
        if (lr.type == RowType::Toggle) o.add(FX_TOGGLE_CONTROL, lr.id);
        return true;
    }
    return false;
}

void RowCardClick(NavState& s, const NavEvent& ev, const LayoutCard& card, Out& o)
{
    SettingsWindowLayout::RowCardHit hit = SettingsWindowLayout::RowCardsHit(ev.y, card, RowCtx(s, ev));
    if (hit.spanStart < 0 || hit.row < 0) return;
    const LayoutRow& lr = card.rows[hit.row];
    if (lr.action == RowAction::ExpandSection && lr.type == RowType::ComboBox && ev.colorMode == 3) {
        s.colorGridExpanded = !s.colorGridExpanded;
        o.add(FX_REBUILD);
        o.invalidateContent(false);
        o.add(FX_UPDATE_CONTENT);
    } else if (lr.type == RowType::Toggle) {
        o.add(FX_TOGGLE_CONTROL, lr.id);
    }
}

void ContentLButton(NavState& s, const NavEvent& ev, Out& o)
{
    o.add(FX_SET_FOCUS_CONTENT);
    UINT dpi = ev.dpi;
    int x = ev.x, y = ev.y;

    // Hamburger — toggles the sidebar overlay (before the sidebar click handler)
    if (s.sidebarNarrowMode) {
        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
        if (x >= 0 && x < hbSize && y >= 0 && y < hbSize) {
            ToggleSidebar(s, o);
            o.invalidateContent(true);
            o.invalidateMain(true);
            return;
        }
    }

    // Narrow mode with the sidebar shown as an overlay
    if (s.sidebarNarrowMode && !s.sidebarCollapsed) {
        int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
        if (x < sidebarW) {
            bool hasBc = (s.navLevel == 1 && s.selectedModeIndex < 4);
            int bcOff = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpi) : 0;
            int mainY = y + bcOff;
            int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);
            int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
            int sidebarCount = 0;
            const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
            int idx = SettingsModel::SidebarHitTest(mainY - startY, itemH, sidebarCount);
            if (idx >= 0 && idx < sidebarCount && sItems[idx].iconResourceId != -1) {
                s.sidebarCollapsed = 1;
                SelectSidebarIndex(s, idx, o);
                o.add(FX_SHOW_ALL_CONTROLS);
            }
            return;
        }
        // Click outside the sidebar collapses it and falls through to the content.
        s.sidebarCollapsed = 1;
        o.add(FX_SHOW_ALL_CONTROLS);
        o.invalidateContent(true);
        o.invalidateMain(true);
    }

    if (s.navLevel == 0 && s.selectedModeIndex < 4) {
        int layoutCount = 0;
        const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
        int modeMask = SettingsModel::GetModeBitmask((IME_MODE)s.currentMode);
        CardListClick(s, ev, cards, layoutCount, modeMask, 0, o);
    } else if (s.navLevel == 1) {
        int layoutCount = 0;
        const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
        if (s.currentCardIndex >= 0 && s.currentCardIndex < layoutCount)
            RowCardClick(s, ev, allCards[s.currentCardIndex], o);
    } else if (s.selectedModeIndex == 5) {
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        CardListClick(s, ev, ltCards, ltCount, SM_MODE_ALL, 4, o);
    }
}

} // namespace

void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& outVec)
{
    Out o{ outVec };
    UINT dpi = ev.dpi;
    switch (ev.kind) {
    case EV_MAIN_SIZE: {
        int winW = ev.x, winH = ev.y;
        int collapseBreak = ScaleForDpi(g_geo.sidebarCollapseBreak, dpi);
        bool isNarrow = (winW < collapseBreak);
        if (!isNarrow) {
            s.sidebarCollapsed = 0;
            s.sidebarNarrowMode = 0;
        } else {
            s.sidebarNarrowMode = 1;
            s.sidebarCollapsed = 1;
        }
        int sidebarW = isNarrow ? 0 : ScaleForDpi(g_geo.sidebarWidth, dpi);
        outVec.push_back({ FX_MOVE_CONTENT, 0, 0, 1, { sidebarW, 0, winW - sidebarW, winH } });
        o.add(FX_REBUILD);
        o.invalidateMain(true);
        break;
    }
    case EV_MAIN_WHEEL:
    case EV_CONTENT_WHEEL:
        Wheel(s, ev, o);
        break;
    case EV_MAIN_LBUTTON:
        MainLButton(s, ev, o);
        break;
    case EV_MAIN_MOUSEMOVE:
        MainMouseMove(s, ev, o);
        break;
    case EV_MAIN_MOUSELEAVE:
        if (s.hoverSidebarIndex != -1) {
            s.hoverSidebarIndex = -1;
            o.invalidateMainRect(false, { 0, 0, ScaleForDpi(g_geo.sidebarWidth, dpi), 32767 });
        }
        break;
    case EV_CONTENT_VSCROLL: {
        int pos = s.scrollPos;
        switch (ev.delta) {
        case SB_LINEUP:     pos -= ScaleForDpi(g_geo.rowHeight, dpi); break;
        case SB_LINEDOWN:   pos += ScaleForDpi(g_geo.rowHeight, dpi); break;
        case SB_PAGEUP:     pos -= s.totalContentHeight / 4; break;
        case SB_PAGEDOWN:   pos += s.totalContentHeight / 4; break;
        case SB_THUMBTRACK: pos = ev.thumb; break;
        default: break;
        }
        Scroll(s, pos, o);
        break;
    }
    case EV_CONTENT_MOUSEMOVE:
        ContentMouseMove(s, ev, o);
        break;
    case EV_CONTENT_MOUSELEAVE:
        if (s.hoverRowIndex != -1) {
            s.hoverRowIndex = -1;
            o.invalidateContent(false);
        }
        break;
    case EV_CONTENT_LBUTTON:
        ContentLButton(s, ev, o);
        break;
    case EV_BACK:
        NavigateBack(s, o);
        break;
    default:
        break;
    }
}

POINT MinTrackSize(UINT screenDpi)
{
    return { ScaleForDpi(g_geo.windowMinW, screenDpi), ScaleForDpi(g_geo.windowMinH, screenDpi) };
}

} // namespace Cpp

#ifdef DIME_USE_RUST_CONFIG
static_assert(offsetof(NavState, cardExpanded) == 48, "NavState::cardExpanded");

void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& out)
{
    NavEffect buf[32];
    size_t count = 0;
    dime_settings_nav_handle(&s, &ev, buf, 32, &count);
    for (size_t i = 0; i < count && i < 32; i++) out.push_back(buf[i]);
}

POINT MinTrackSize(UINT screenDpi)
{
    POINT p = {};
    dime_settings_nav_min_track_size(screenDpi, &p);
    return p;
}
#else
void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& out) { Cpp::Handle(s, ev, out); }
POINT MinTrackSize(UINT screenDpi) { return Cpp::MinTrackSize(screenDpi); }
#endif

} // namespace SettingsNav
