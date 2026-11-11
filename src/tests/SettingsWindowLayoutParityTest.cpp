/********************************************************************************
* SettingsWindowLayoutParityTest: the window-free computations of the settings
* window, C++ (SettingsWindowLayout::Cpp, the code lifted out of
* SettingsWindow.cpp) vs Rust (dime-settings via dime_settings_*): card and
* control widths, row heights and visibility, card-list hit tests and click
* targets, sub-page card grouping and hits, control placement for every page,
* control / swatch / editor / content-area rectangles, the custom-table buffer
* validator, and the themed message box (buttons, icon, layout, hits, keys).
* Both the shipped page tables and random synthetic cards are used.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../BaseStructure.h"
#include "../DIMESettings/SettingsWindowLayout.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SWL = SettingsWindowLayout;

namespace
{
    const UINT kDpis[] = { 0, 72, 96, 120, 144, 168, 192, 240, 288, 480 };

    UINT RandomDpi(std::mt19937& rng)
    {
        return kDpis[std::uniform_int_distribution<int>(0, _countof(kDpis) - 1)(rng)];
    }

    bool Same(const SWL::CtrlRect& a, const SWL::CtrlRect& b)
    {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.visH == b.visH;
    }

    // Random synthetic cards. Rows always exist when rowCount > 0 (the C++
    // walks rows[] by rowCount without a null check on card lists).
    struct SyntheticPage
    {
        std::vector<std::vector<LayoutRow>> rows;
        std::vector<LayoutCard> cards;
    };

    SyntheticPage RandomPage(std::mt19937& rng, int maxCards)
    {
        static const wchar_t* kDesc = L"desc";
        std::uniform_int_distribution<int> any(0, 1 << 20);
        SyntheticPage page;
        int n = 1 + any(rng) % maxCards;
        page.rows.resize(n);
        for (int i = 0; i < n; i++) {
            int rc = any(rng) % 9;
            for (int r = 0; r < rc; r++) {
                LayoutRow lr = {};
                lr.id = (SettingsControlId)(1 + any(rng) % 45);
                lr.type = (RowType)(any(rng) % 8);
                lr.action = (RowAction)(any(rng) % 11);
                int parent = any(rng) % 4;
                lr.expandParent = parent == 0 ? CTRL_NONE : parent == 1 ? CTRL_COLOR_MODE : (SettingsControlId)(1 + any(rng) % 40);
                lr.label = L"row";
                lr.description = (any(rng) & 1) ? kDesc : nullptr;
                lr.visibleModes = any(rng) % 16;
                lr.cardBreakBefore = (any(rng) % 5) == 0;
                page.rows[i].push_back(lr);
            }
        }
        for (int i = 0; i < n; i++) {
            LayoutCard c = {};
            c.title = L"card";
            int a = any(rng) % 4;
            c.action = a == 0 ? RowAction::None : a == 1 ? RowAction::ExpandSection : a == 2 ? RowAction::NavigateToCard : RowAction::LoadTable;
            c.rowCount = (int)page.rows[i].size();
            c.rows = c.rowCount > 0 ? page.rows[i].data() : ((any(rng) & 1) ? nullptr : page.rows[i].data());
            c.defaultExpanded = (any(rng) & 1) != 0;
            page.cards.push_back(c);
        }
        return page;
    }

    void RandomExpanded(std::mt19937& rng, bool* expanded)
    {
        for (int i = 0; i < SWL::kExpandedSlots; i++)
            expanded[i] = (rng() & 1) != 0;
    }

    SWL::RowContext RandomContext(std::mt19937& rng)
    {
        SWL::RowContext ctx = {};
        ctx.dpi = RandomDpi(rng);
        ctx.scrollPos = std::uniform_int_distribution<int>(0, 600)(rng);
        ctx.modeMask = std::uniform_int_distribution<int>(0, 15)(rng);
        ctx.colorMode = std::uniform_int_distribution<int>(0, 4)(rng);
        ctx.gridExpanded = (int)(rng() & 1);
        ctx.previewH = std::uniform_int_distribution<int>(0, 400)(rng);
        return ctx;
    }

    void CheckCardList(const LayoutCard* cards, int count, int modeMask, const bool* expanded,
        int base, UINT dpi, int scroll, const wchar_t* what)
    {
        for (int y = -40; y < 2200; y += 1) {
            int cpp = SWL::Cpp::CardListHitTest(y, cards, count, modeMask, expanded, base, dpi, scroll);
            int rust = dime_settings_card_list_hit_test(y, cards, count, modeMask,
                reinterpret_cast<const uint8_t*>(expanded), base, dpi, scroll);
            if (cpp != rust)
                Assert::Fail((std::wstring(what) + L" hit test y=" + std::to_wstring(y) + L" dpi=" + std::to_wstring(dpi)).c_str());
            SWL::CardListClick a = SWL::Cpp::CardListClickTarget(y, cards, count, modeMask, expanded, base, dpi, scroll);
            SWL::CardListClick b = { 7, 7, 7 };
            dime_settings_card_list_click(y, cards, count, modeMask,
                reinterpret_cast<const uint8_t*>(expanded), base, dpi, scroll, &b);
            if (a.kind != b.kind || a.card != b.card || a.row != b.row)
                Assert::Fail((std::wstring(what) + L" click y=" + std::to_wstring(y) + L" dpi=" + std::to_wstring(dpi)).c_str());
        }
    }

    void CheckSubPage(const LayoutCard& card, const SWL::RowContext& ctx, const wchar_t* what)
    {
        std::vector<SWL::RowSpan> cpp = SWL::Cpp::RowSpans(card, ctx);
        size_t count = 0;
        dime_settings_row_spans(&card, &ctx, nullptr, 0, &count);
        Assert::AreEqual(cpp.size(), count, (std::wstring(what) + L" span count").c_str());
        std::vector<SWL::RowSpan> rust(count + 1, SWL::RowSpan{ -9, -9, -9 });
        size_t written = 0;
        dime_settings_row_spans(&card, &ctx, rust.data(), count, &written);
        Assert::AreEqual(count, written);
        for (size_t i = 0; i < count; i++) {
            Assert::AreEqual(cpp[i].startRow, rust[i].startRow, what);
            Assert::AreEqual(cpp[i].endRow, rust[i].endRow, what);
            Assert::AreEqual(cpp[i].height, rust[i].height, what);
        }
        Assert::AreEqual(-9, rust[count].startRow, L"wrote past cap");
        for (int y = -40; y < 2400; y++) {
            SWL::RowCardHit a = SWL::Cpp::RowCardsHit(y, card, ctx);
            SWL::RowCardHit b = { 7, 7 };
            dime_settings_row_cards_hit(y, &card, &ctx, &b);
            if (a.spanStart != b.spanStart || a.row != b.row)
                Assert::Fail((std::wstring(what) + L" row hit y=" + std::to_wstring(y)).c_str());
        }
    }

    void CheckPlan(const SWL::PlanInput& in, const LayoutCard* ime, int imeCount,
        const LayoutCard* load, int loadCount)
    {
        std::vector<SWL::Placement> cpp;
        SWL::PlanResult a = SWL::Cpp::PlanControls(in, ime, imeCount, load, loadCount, cpp);
        size_t count = 0;
        SWL::PlanResult b = { 7, 7, 7 };
        dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, nullptr, 0, &count, &b);
        std::wstring ctx = L"mode=" + std::to_wstring(in.selectedModeIndex) + L" nav=" + std::to_wstring(in.navLevel)
            + L" card=" + std::to_wstring(in.currentCardIndex) + L" dpi=" + std::to_wstring(in.dpi);
        Assert::AreEqual(cpp.size(), count, ctx.c_str());
        Assert::AreEqual(a.setTotal, b.setTotal, ctx.c_str());
        Assert::AreEqual(a.totalHeight, b.totalHeight, ctx.c_str());
        Assert::AreEqual(a.populate, b.populate, ctx.c_str());
        std::vector<SWL::Placement> rust(count);
        size_t written = 0;
        dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, rust.data(), rust.size(), &written, &b);
        for (size_t i = 0; i < count; i++) {
            const SWL::Placement& p = cpp[i];
            const SWL::Placement& q = rust[i];
            if (p.kind != q.kind || p.card != q.card || p.row != q.row || p.y != q.y || p.h != q.h)
                Assert::Fail((ctx + L" placement " + std::to_wstring(i)).c_str());
        }
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsWindowLayoutParityTest)
    {
    public:
        TEST_METHOD(Widths)
        {
            std::mt19937 rng(11);
            std::uniform_int_distribution<int> v(-200, 4000);
            for (int i = 0; i < 20000; i++) {
                UINT dpi = RandomDpi(rng);
                int a = v(rng), b = v(rng), c = v(rng) % 40;
                bool f = (rng() & 1) != 0, g = (rng() & 1) != 0;
                Assert::AreEqual(SWL::Cpp::CardWidth(a, dpi, f, c), dime_settings_card_width(a, dpi, f, c), L"CardWidth");
                Assert::AreEqual(SWL::Cpp::CtrlRight(a, b, dpi, f), dime_settings_ctrl_right(a, b, dpi, f), L"CtrlRight");
                SWL::CtrlRect r1 = SWL::Cpp::ContentAreaRect(a, b, dpi, f, g), r2 = {};
                dime_settings_content_area_rect(a, b, dpi, f, g, &r2);
                Assert::IsTrue(Same(r1, r2), L"ContentAreaRect");
            }
        }

        TEST_METHOD(ControlRects)
        {
            std::mt19937 rng(12);
            std::uniform_int_distribution<int> v(-100, 3000);
            for (int i = 0; i < 20000; i++) {
                UINT dpi = RandomDpi(rng);
                int type = (int)(rng() % 9);
                int ctrlRight = v(rng), cy = v(rng), ctrlH = v(rng) % 60, btnH = v(rng) % 60;
                SWL::CtrlRect r1 = { 9, 9, 9, 9, 9 }, r2 = { 9, 9, 9, 9, 9 };
                bool a = SWL::Cpp::RowControlRect((RowType)type, ctrlRight, cy, ctrlH, btnH, dpi, &r1);
                bool b = dime_settings_row_control_rect(type, ctrlRight, cy, ctrlH, btnH, dpi, &r2) != 0;
                Assert::AreEqual(a, b, L"RowControlRect has control");
                Assert::IsTrue(Same(r1, r2), L"RowControlRect");

                int idx = (int)(rng() % 6), margin = v(rng) % 100, curY = v(rng);
                SWL::CtrlRect s1 = SWL::Cpp::SwatchRect(idx, margin, curY, ctrlRight, dpi), s2 = {};
                dime_settings_swatch_rect(idx, margin, curY, ctrlRight, dpi, &s2);
                Assert::IsTrue(Same(s1, s2), L"SwatchRect");

                int cardW = v(rng), pad = v(rng) % 30, thisH = v(rng), btnW = v(rng) % 200;
                SWL::CtrlRect e1 = {}, e2 = {}, f1 = {}, f2 = {};
                SWL::Cpp::RichEditorRects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi, &e1, &f1);
                dime_settings_rich_editor_rects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi, &e2, &f2);
                Assert::IsTrue(Same(e1, e2) && Same(f1, f2), L"RichEditorRects");
            }
        }

        TEST_METHOD(RowsOfShippedTables)
        {
            int imeCount = 0, ltCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
            for (const auto& page : { std::make_pair(ime, imeCount), std::make_pair(lt, ltCount) }) {
                for (int i = 0; i < page.second; i++) {
                    const LayoutCard& c = page.first[i];
                    for (int r = 0; c.rows && r < c.rowCount; r++) {
                        for (UINT dpi : kDpis) {
                            for (int previewH : { 0, 137, 260 }) {
                                Assert::AreEqual(SWL::Cpp::RowHeight(c.rows[r], dpi, previewH),
                                    dime_settings_row_height(&c.rows[r], dpi, previewH), L"RowHeight");
                            }
                        }
                        for (int mask = 0; mask < 16; mask++)
                            for (int cm = 0; cm < 5; cm++)
                                for (int g = 0; g < 2; g++)
                                    Assert::AreEqual(SWL::Cpp::RowShown(c.rows[r], mask, cm, g != 0),
                                        dime_settings_row_shown(&c.rows[r], mask, cm, g) != 0, L"RowShown");
                    }
                }
            }
        }

        TEST_METHOD(CardListsOfShippedTables)
        {
            int imeCount = 0, ltCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
            Assert::IsTrue(imeCount <= 4 && 4 + ltCount <= SWL::kExpandedSlots, L"expanded slots");
            std::mt19937 rng(13);
            for (int iter = 0; iter < 40; iter++) {
                bool expanded[SWL::kExpandedSlots];
                RandomExpanded(rng, expanded);
                UINT dpi = RandomDpi(rng);
                int scroll = std::uniform_int_distribution<int>(0, 300)(rng);
                int mask = 1 << (iter % 4);
                CheckCardList(ime, imeCount, mask, expanded, 0, dpi, scroll, L"ime");
                CheckCardList(lt, ltCount, 0xF, expanded, 4, dpi, scroll, L"load");
            }
            // A null expanded pointer is all collapsed.
            bool none[SWL::kExpandedSlots] = {};
            for (int y = 0; y < 1200; y++)
                Assert::AreEqual(SWL::Cpp::CardListHitTest(y, ime, imeCount, 1, none, 0, 96, 0),
                    dime_settings_card_list_hit_test(y, ime, imeCount, 1, nullptr, 0, 96, 0), L"null expanded");
        }

        TEST_METHOD(SyntheticCardLists)
        {
            std::mt19937 rng(14);
            for (int iter = 0; iter < 300; iter++) {
                SyntheticPage page = RandomPage(rng, 6);
                bool expanded[SWL::kExpandedSlots];
                RandomExpanded(rng, expanded);
                int base = (rng() & 1) ? 4 : 0;
                CheckCardList(page.cards.data(), (int)page.cards.size(), (int)(rng() % 16), expanded, base,
                    RandomDpi(rng), std::uniform_int_distribution<int>(-50, 400)(rng), L"synthetic");
            }
        }

        TEST_METHOD(SubPagesOfShippedTables)
        {
            int imeCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            std::mt19937 rng(15);
            for (int iter = 0; iter < 60; iter++) {
                SWL::RowContext ctx = RandomContext(rng);
                ctx.modeMask = 1 << (iter % 4);
                ctx.colorMode = (iter / 4) % 2 ? 3 : (int)(rng() % 3);
                for (int i = 0; i < imeCount; i++)
                    CheckSubPage(ime[i], ctx, L"ime");
            }
        }

        TEST_METHOD(SyntheticSubPages)
        {
            std::mt19937 rng(16);
            for (int iter = 0; iter < 400; iter++) {
                SyntheticPage page = RandomPage(rng, 3);
                SWL::RowContext ctx = RandomContext(rng);
                for (const LayoutCard& c : page.cards)
                    CheckSubPage(c, ctx, L"synthetic");
            }
        }

        TEST_METHOD(PlanControlsShippedTables)
        {
            int imeCount = 0, ltCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
            std::mt19937 rng(17);
            for (int iter = 0; iter < 400; iter++) {
                SWL::PlanInput in = {};
                in.dpi = RandomDpi(rng);
                in.selectedModeIndex = (int)(rng() % 7);
                in.navLevel = (int)(rng() % 3);
                in.currentCardIndex = (int)(rng() % (imeCount + 2)) - 1;
                in.modeMask = 1 << (rng() % 4);
                in.colorMode = (int)(rng() % 4);
                in.gridExpanded = (int)(rng() & 1);
                in.previewH = std::uniform_int_distribution<int>(0, 300)(rng);
                RandomExpanded(rng, in.expanded);
                CheckPlan(in, ime, imeCount, lt, ltCount);
            }
        }

        TEST_METHOD(PlanControlsSynthetic)
        {
            std::mt19937 rng(18);
            for (int iter = 0; iter < 2000; iter++) {
                SyntheticPage ime = RandomPage(rng, 4), lt = RandomPage(rng, 6);
                SWL::PlanInput in = {};
                in.dpi = RandomDpi(rng);
                in.selectedModeIndex = (int)(rng() % 7);
                in.navLevel = (int)(rng() % 3);
                in.currentCardIndex = (int)(rng() % (ime.cards.size() + 2)) - 1;
                in.modeMask = (int)(rng() % 16);
                in.colorMode = (int)(rng() % 4);
                in.gridExpanded = (int)(rng() & 1);
                in.previewH = std::uniform_int_distribution<int>(0, 300)(rng);
                RandomExpanded(rng, in.expanded);
                CheckPlan(in, ime.cards.data(), (int)ime.cards.size(), lt.cards.data(), (int)lt.cards.size());
            }
        }

        TEST_METHOD(ValidateBuffer)
        {
            static const wchar_t kAlphabet[] = L"ab,.;/AZ09 \t\r\n\n中語*?~";
            std::mt19937 rng(19);
            for (int iter = 0; iter < 3000; iter++) {
                std::wstring text;
                int len = (int)(rng() % 120);
                for (int i = 0; i < len; i++)
                    text += kAlphabet[rng() % (_countof(kAlphabet) - 1)];
                IME_MODE mode = (IME_MODE)(rng() % 4);
                UINT maxCodes = (UINT)(rng() % 7);
                int maxReport = (int)(rng() % 12) - 1;
                static const size_t kCch[] = { 0, 1, 5, 11, 24, 1024 };
                size_t cch = kCch[rng() % 6];
                int f1 = -5, f2 = -5;
                std::vector<wchar_t> s1(cch + 1, L'#'), s2(cch + 1, L'#');
                int a = SWL::Cpp::ValidateBuffer(text.c_str(), text.size(), mode, maxCodes, nullptr,
                    &f1, cch ? s1.data() : nullptr, cch, maxReport);
                int b = dime_settings_validate_buffer(reinterpret_cast<const uint16_t*>(text.c_str()), text.size(),
                    (int32_t)mode, maxCodes, nullptr, nullptr, maxReport, &f2,
                    cch ? reinterpret_cast<uint16_t*>(s2.data()) : nullptr, cch);
                Assert::AreEqual(a, b, L"error count");
                Assert::AreEqual(f1, f2, L"first error line");
                Assert::IsTrue(s1 == s2, (L"summary for: " + text).c_str());
            }
        }

        TEST_METHOD(ThemedMessageBoxLayout)
        {
            for (UINT type = 0; type < 0x100; type++) {
                SWL::MsgBoxButtons a = SWL::Cpp::MsgBoxButtonsFor(type), b = {};
                dime_settings_msgbox_buttons(type, &b);
                Assert::IsTrue(memcmp(&a, &b, sizeof(a)) == 0, L"buttons");
                Assert::AreEqual(SWL::Cpp::MsgBoxIcon(type), dime_settings_msgbox_icon(type), L"icon");
                for (int def = 0; def < a.count; def++)
                    for (UINT vk = 0; vk < 0x30; vk++)
                        Assert::AreEqual(SWL::Cpp::MsgBoxKeyResult(a, def, vk), dime_settings_msgbox_key_result(&b, def, vk), L"key");
                Assert::AreEqual(SWL::Cpp::MsgBoxCloseResult(a), dime_settings_msgbox_close_result(&b), L"close");
            }
            std::mt19937 rng(20);
            for (int iter = 0; iter < 3000; iter++) {
                UINT dpi = RandomDpi(rng);
                bool icon = (rng() & 1) != 0;
                int count = 1 + (int)(rng() % 3);
                int tw = (int)(rng() % 700), th = (int)(rng() % 300);
                Assert::AreEqual(SWL::Cpp::MsgBoxMaxTextWidth(dpi), dime_settings_msgbox_max_text_width(dpi), L"max text");
                SWL::MsgBoxLayout L1 = SWL::Cpp::MsgBoxLayoutFor(icon, count, tw, th, dpi), L2 = {};
                dime_settings_msgbox_layout(icon, count, tw, th, dpi, &L2);
                Assert::IsTrue(memcmp(&L1, &L2, sizeof(L1)) == 0, L"layout");
                Assert::AreEqual(SWL::Cpp::MsgBoxFirstButtonX(L1, count), dime_settings_msgbox_first_button_x(&L2, count), L"first x");
                for (int k = 0; k < 200; k++) {
                    int x = (int)(rng() % (L1.rcClient.right + 20)) - 10;
                    int y = (int)(rng() % (L1.rcClient.bottom + 20)) - 10;
                    Assert::AreEqual(SWL::Cpp::MsgBoxButtonAt(L1, count, x, y), dime_settings_msgbox_button_at(&L2, count, x, y), L"button at");
                }
            }
        }
    };
}
