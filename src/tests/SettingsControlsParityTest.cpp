/********************************************************************************
* SettingsControlsParityTest: the child controls of each settings page, C++
* (SettingsControls::Cpp::BuildSpecs, formerly CreateRowControl /
* CreateColorGridControls / CreateRichEditorControl) vs Rust (dime-settings
* controls.rs through SettingsControls::BuildSpecsRust). For every page of the
* shipped tables and random synthetic cards, at several dpis, card widths, and
* expand states, every spec field must match: id, HMENU, role, class, styles,
* caption, create / AddControl rectangles, visible height, font, dark theme.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsControls.h"
#include "../DIMESettings/SettingsPageLayout.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SC = SettingsControls;
namespace SWL = SettingsWindowLayout;

namespace
{
    std::wstring R(const RECT& r)
    {
        return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom);
    }

    std::wstring D(const SC::ControlSpec& s)
    {
        return L"id=" + std::to_wstring(s.ctrlId) + L" menu=" + std::to_wstring(s.menuId) + L" role=" + std::to_wstring(s.role)
            + L" cls=" + std::to_wstring(s.controlClass) + L" ex=" + std::to_wstring(s.exStyle) + L" st=" + std::to_wstring(s.style)
            + L" text=" + (s.hasText ? L"[" + s.text + L"]" : L"null") + L" create=" + R(s.create) + L" add=" + R(s.add)
            + L" visH=" + std::to_wstring(s.visH) + L" font=" + std::to_wstring(s.setFont) + L" dark=" + std::to_wstring(s.darkTheme);
    }

    void Check(const SWL::PlanInput& in, int cardW, const LayoutCard* ime, int imeCount,
        const LayoutCard* load, int loadCount, const std::wstring& what, int& total)
    {
        std::vector<SC::ControlSpec> a = SC::Cpp::BuildSpecs(in, cardW, ime, imeCount, load, loadCount);
        std::vector<SC::ControlSpec> b = SC::BuildSpecsRust(in, cardW, ime, imeCount, load, loadCount);
        Assert::AreEqual(a.size(), b.size(), (what + L": count").c_str());
        for (size_t i = 0; i < a.size(); i++) {
            std::wstring da = D(a[i]), db = D(b[i]);
            if (da != db)
                Assert::Fail((what + L" spec " + std::to_wstring(i) + L"\nC++:  " + da + L"\nRust: " + db).c_str());
        }
        total += (int)a.size();
    }

    struct Synthetic
    {
        std::vector<std::vector<LayoutRow>> rows;
        std::vector<LayoutCard> cards;
    };

    Synthetic RandomCards(std::mt19937& rng, int maxCards)
    {
        static const wchar_t* labels[] = { L"切換", L"按鈕文字", nullptr };
        Synthetic s;
        int n = 1 + (int)(rng() % maxCards);
        s.rows.resize(n);
        for (int i = 0; i < n; i++) {
            int rc = 1 + (int)(rng() % 7);
            for (int r = 0; r < rc; r++) {
                LayoutRow lr = {};
                lr.id = (SettingsControlId)(1 + rng() % 45);
                lr.type = (RowType)(rng() % 8);
                lr.action = (RowAction)(rng() % 11);
                lr.expandParent = (rng() % 3) ? CTRL_NONE : ((rng() & 1) ? CTRL_COLOR_MODE : (SettingsControlId)(1 + rng() % 40));
                lr.label = labels[rng() % 3];
                lr.description = (rng() & 1) ? L"desc" : nullptr;
                lr.visibleModes = (int)(rng() % 16);
                lr.cardBreakBefore = (rng() % 5) == 0;
                s.rows[i].push_back(lr);
            }
        }
        for (int i = 0; i < n; i++) {
            LayoutCard c = {};
            c.title = L"card";
            int a = (int)(rng() % 3);
            c.action = a == 0 ? RowAction::None : a == 1 ? RowAction::ExpandSection : RowAction::NavigateToCard;
            c.rows = s.rows[i].data();
            c.rowCount = (int)s.rows[i].size();
            s.cards.push_back(c);
        }
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsControlsParityTest)
    {
    public:
        TEST_METHOD(ShippedPages)
        {
            int imeCount = 0, ltCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
            std::mt19937 rng(5);
            int total = 0;
            for (UINT dpi : { 96u, 120u, 144u, 192u }) {
                for (int sel : { 0, 1, 2, 3, 5 }) {
                    for (int nav = 0; nav < 2; nav++) {
                        for (int card = 0; card < imeCount; card++) {
                            for (int rep = 0; rep < 3; rep++) {
                                SWL::PlanInput in = {};
                                in.dpi = dpi;
                                in.selectedModeIndex = sel;
                                in.navLevel = nav;
                                in.currentCardIndex = card;
                                in.modeMask = sel < 4 ? (1 << sel) : 0xF;
                                in.colorMode = (int)(rng() % 4);
                                in.gridExpanded = (int)(rng() & 1);
                                in.previewH = 150 + (int)(rng() % 150);
                                for (bool& e : in.expanded) e = (rng() & 1) != 0;
                                int cardW = MulDiv(500 + (int)(rng() % 400), dpi, 96);
                                Check(in, cardW, ime, imeCount, lt, ltCount,
                                    L"sel " + std::to_wstring(sel) + L" nav " + std::to_wstring(nav) + L" card " + std::to_wstring(card)
                                        + L" dpi " + std::to_wstring(dpi), total);
                            }
                        }
                    }
                }
            }
            Assert::IsTrue(total > 2000, (L"specs checked: " + std::to_wstring(total)).c_str());
        }

        TEST_METHOD(SyntheticPages)
        {
            std::mt19937 rng(6);
            int total = 0;
            for (int iter = 0; iter < 2000; iter++) {
                Synthetic ime = RandomCards(rng, 4), lt = RandomCards(rng, 6);
                SWL::PlanInput in = {};
                in.dpi = 96 + 24 * (UINT)(rng() % 5);
                int sel[] = { 0, 1, 2, 3, 5 };
                in.selectedModeIndex = sel[rng() % 5];
                in.navLevel = (int)(rng() % 2);
                in.currentCardIndex = (int)(rng() % ime.cards.size());
                in.modeMask = (int)(rng() % 16);
                in.colorMode = (int)(rng() % 4);
                in.gridExpanded = (int)(rng() & 1);
                in.previewH = (int)(rng() % 300);
                for (bool& e : in.expanded) e = (rng() & 1) != 0;
                Check(in, 300 + (int)(rng() % 700), ime.cards.data(), (int)ime.cards.size(), lt.cards.data(),
                    (int)lt.cards.size(), L"iter " + std::to_wstring(iter), total);
            }
            Assert::IsTrue(total > 3000, (L"specs checked: " + std::to_wstring(total)).c_str());
        }
    };
}
