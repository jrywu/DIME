/********************************************************************************
* UILayoutParityTest: the window layout calculations, C++ (UILayout::Cpp, the
* code moved from CBaseWindow / CUIPresenter) vs Rust (dime-ui layout via
* dime_ui_*): RectInRect, the work-area probe point, the placement around the
* composition (below / above / docked, multiple monitors with negative
* coordinates), and the notify-window stack (grow direction, new-slot position,
* restacking with and without a candidate window); MulDiv; the candidate
* window (metrics, size, rows, truncation, widening, hit tests, page
* indicator) and the scroll bar (buttons, track, thumb, hit test, idle line,
* arrows, dragging, clamping, placement).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <climits>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../UILayout.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    std::wstring R(const RECT& r)
    {
        return L"(" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L")";
    }

    RECT RandomRect(std::mt19937& rng, int lo, int hi, int maxSize)
    {
        std::uniform_int_distribution<int> pos(lo, hi), size(0, maxSize);
        RECT r;
        r.left = pos(rng);
        r.top = pos(rng);
        r.right = r.left + size(rng);
        r.bottom = r.top + size(rng);
        return r;
    }

    // C++'s RectInRect asserts (Debug) for a target wider / taller than the limit
    // that sticks out on both sides; real windows are never that large.
    bool Straddles(const RECT& limit, const RECT& target)
    {
        return (target.left < limit.left && target.right > limit.right) ||
               (target.top < limit.top && target.bottom > limit.bottom);
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(UILayoutParityTest)
    {
    public:
        TEST_METHOD(RectInRect)
        {
            std::mt19937 rng(1);
            int compared = 0;
            for (int i = 0; i < 200000; i++)
            {
                RECT limit = RandomRect(rng, -3000, 3000, 2500);
                RECT target = RandomRect(rng, -4000, 4000, 2000);
                if (Straddles(limit, target)) continue;
                DWORD c = UILayout::Cpp::RectInRect(&limit, &target);
                uint32_t r = dime_ui_rect_in_rect((const DimeRect*)&limit, (const DimeRect*)&target);
                if (c != r)
                    Assert::Fail((L"RectInRect " + R(limit) + L" " + R(target) + L": C++ " + std::to_wstring(c) + L" Rust " + std::to_wstring(r)).c_str());
                compared++;
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" rectangles").c_str());
        }

        TEST_METHOD(WindowPlacement)
        {
            std::mt19937 rng(2);
            std::uniform_int_distribution<int> coin(0, 9);
            int compared = 0;
            for (int i = 0; i < 200000; i++)
            {
                // A work area (possibly a monitor left of / above the primary one).
                RECT work = RandomRect(rng, -3000, 2000, 0);
                work.right = work.left + 800 + (int)(rng() % 3000);
                work.bottom = work.top + 600 + (int)(rng() % 2000);
                // The window, smaller than the work area.
                RECT window = { 0, 0, 50 + (int)(rng() % 700), 30 + (int)(rng() % 550) };
                // The text extent: a caret / composition near the work area, or a large area.
                RECT text;
                if (coin(rng) < 8)
                {
                    text = RandomRect(rng, 0, 0, 0);
                    text.left = work.left - 200 + (int)(rng() % (work.right - work.left + 400));
                    text.top = work.top - 200 + (int)(rng() % (work.bottom - work.top + 400));
                    text.right = text.left + (int)(rng() % 300);
                    text.bottom = text.top + 10 + (int)(rng() % 40);
                }
                else
                {
                    text = RandomRect(rng, -3000, 3000, 2500);
                }

                POINT pc = UILayout::Cpp::WorkAreaProbePoint(&text, &window);
                DimePoint pr = { 0, 0 };
                dime_ui_work_area_probe((const DimeRect*)&text, (const DimeRect*)&window, &pr);
                if (pc.x != pr.x || pc.y != pr.y)
                    Assert::Fail((L"WorkAreaProbePoint " + R(text) + L" " + R(window)).c_str());

                // Skip inputs that would hit C++'s Debug assert in RectInRect.
                RECT below = window, above = window;
                LONG baseX = (text.right - text.left) > (window.right - window.left) ? text.left : text.right;
                OffsetRect(&below, baseX, text.bottom + 7);
                OffsetRect(&above, baseX, text.top - (window.bottom - window.top) - 7);
                if (Straddles(work, below) || Straddles(work, above)) continue;

                POINT c = { -12345, -12345 };
                UILayout::Cpp::CalcFitPointAroundTextExtent(&text, &work, &window, &c);
                DimePoint r = { -12345, -12345 };
                dime_ui_fit_point((const DimeRect*)&text, (const DimeRect*)&work, (const DimeRect*)&window, &r);
                if (c.x != r.x || c.y != r.y)
                    Assert::Fail((L"CalcFitPoint text " + R(text) + L" work " + R(work) + L" window " + R(window) +
                        L": C++ " + std::to_wstring(c.x) + L"," + std::to_wstring(c.y) + L" Rust " + std::to_wstring(r.x) + L"," + std::to_wstring(r.y)).c_str());
                compared++;
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" placements").c_str());
        }

        TEST_METHOD(NotifyStack)
        {
            std::mt19937 rng(3);
            for (int i = 0; i < 100000; i++)
            {
                size_t count = 1 + rng() % 6;
                std::vector<UILayout::NotifySlot> slots(count);
                for (auto& s : slots)
                {
                    bool exists = rng() % 5 != 0;
                    s.visible = exists && rng() % 3 != 0;
                    s.width = exists ? 40 + (int)(rng() % 300) : 0;
                    s.height = exists ? 20 + (int)(rng() % 80) : 0;
                }
                BOOL candVisible = rng() % 2;
                RECT cand = RandomRect(rng, -2000, 2000, 600);
                POINT candLoc = { (LONG)(rng() % 4000) - 2000, (LONG)(rng() % 4000) - 2000 };
                POINT notify = { (LONG)(rng() % 4000) - 2000, (LONG)(rng() % 4000) - 2000 };
                int screenBottom = 600 + (int)(rng() % 2000);
                BOOL growUp = rng() % 2;
                size_t newSlot = rng() % (count + 1);   // includes one past the end

                BOOL gc = UILayout::Cpp::DecideStackGrowUp(candVisible, cand.top, notify.y, slots.data(), count, screenBottom);
                int gr = dime_ui_stack_grow_up(candVisible, cand.top, notify.y, (const DimeNotifySlot*)slots.data(), count, screenBottom);
                Assert::AreEqual((int)gc, gr, L"DecideStackGrowUp");

                POINT pc = UILayout::Cpp::ComputeStackPosForNewSlot(notify, slots.data(), count, newSlot, growUp);
                DimePoint pr = { 0, 0 };
                dime_ui_stack_pos_for_new_slot({ notify.x, notify.y }, (const DimeNotifySlot*)slots.data(), count, newSlot, growUp, &pr);
                Assert::IsTrue(pc.x == pr.x && pc.y == pr.y, L"ComputeStackPosForNewSlot");

                std::vector<POINT> posC(count), posR(count);
                std::vector<BOOL> placedC(count), placedR(count);
                UILayout::Cpp::RestackVisibleSlots(candVisible, cand, candLoc, notify, slots.data(), count, growUp, posC.data(), placedC.data());
                dime_ui_restack(candVisible, { cand.left, cand.top, cand.right, cand.bottom }, { candLoc.x, candLoc.y }, { notify.x, notify.y },
                    (const DimeNotifySlot*)slots.data(), count, growUp, (DimePoint*)posR.data(), (int32_t*)placedR.data());
                for (size_t k = 0; k < count; k++)
                {
                    Assert::AreEqual((int)placedC[k], (int)placedR[k], L"RestackVisibleSlots placed");
                    if (placedC[k])
                        Assert::IsTrue(posC[k].x == posR[k].x && posC[k].y == posR[k].y, L"RestackVisibleSlots position");
                }
            }
        }

        // ---- Candidate window and scroll bar (phase 3b) ----

        TEST_METHOD(MulDivMatchesWin32)
        {
            std::mt19937 rng(10);
            const int edges[] = { 0, 1, -1, 2, -2, 3, 96, 100, 0x7fffffff, (int)0x80000000, 0x7ffffffe, -0x7fffffff, 46341, -46341 };
            int compared = 0;
            auto check = [&](int a, int b, int c)
            {
                int w = ::MulDiv(a, b, c);
                int r = dime_ui_mul_div(a, b, c);
                if (w != r)
                    Assert::Fail((L"MulDiv(" + std::to_wstring(a) + L"," + std::to_wstring(b) + L"," + std::to_wstring(c) +
                        L"): Win32 " + std::to_wstring(w) + L" Rust " + std::to_wstring(r)).c_str());
                compared++;
            };
            for (int a : edges) for (int b : edges) for (int c : edges) check(a, b, c);
            std::uniform_int_distribution<int> nearDist(-3000, 3000), anyDist(INT_MIN, INT_MAX);
            for (int i = 0; i < 300000; i++)
            {
                check(nearDist(rng), nearDist(rng), nearDist(rng));
                check(anyDist(rng), nearDist(rng), nearDist(rng));
                check(anyDist(rng), anyDist(rng), anyDist(rng));
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" MulDiv calls").c_str());
        }

        TEST_METHOD(CandidateWindowLayout)
        {
            std::mt19937 rng(11);
            const UINT dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384 };
            int compared = 0;
            for (int i = 0; i < 100000; i++)
            {
                UINT wndWidth = 1 + rng() % 20;
                if (rng() % 50 == 0) wndWidth = 1 + rng() % 5000;
                SIZE measured = { (LONG)(rng() % 3000), (LONG)(rng() % 200) };
                int ave = (int)(rng() % 60);
                UINT dpi = (rng() % 4 == 0) ? 48 + rng() % 500 : dpis[rng() % 8];
                int sb = ::MulDiv(16, dpi, 96);

                // WM_CREATE
                int t1 = 0, r1 = 0, p1 = 0, t2 = -9, r2 = -9, p2 = -9;
                UILayout::Cpp::CandInitialMetrics(measured, wndWidth, &t1, &r1, &p1);
                dime_ui_cand_initial_metrics(measured.cx, measured.cy, wndWidth, &t2, &r2, &p2);
                Assert::IsTrue(t1 == t2 && r1 == r2 && p1 == p2, L"CandInitialMetrics");

                // _DrawList metrics
                UILayout::CandMetrics mc = UILayout::Cpp::CandMeasureMetrics(measured, ave, wndWidth);
                DimeCandMetrics mr = {};
                dime_ui_cand_metrics(measured.cx, measured.cy, ave, wndWidth, &mr);
                Assert::IsTrue(memcmp(&mc, &mr, sizeof(mc)) == 0, L"CandMeasureMetrics");

                Assert::AreEqual(UILayout::Cpp::ScaleForDpi(16, dpi), dime_ui_mul_div(16, (int)dpi, 96), L"ScaleForDpi");

                int rows = 1 + (int)(rng() % 10);
                SIZE sc = UILayout::Cpp::CandWindowSize(mc.cxTitle, mc.cyLine, mc.padding, rows, sb);
                int32_t sw = 0, sh = 0;
                dime_ui_cand_window_size(mc.cxTitle, mc.cyLine, mc.padding, rows, sb, &sw, &sh);
                Assert::IsTrue(sc.cx == sw && sc.cy == sh, L"CandWindowSize");

                RECT client = { 0, 0, sc.cx - 2, sc.cy - 2 };
                if (rng() % 4 == 0) client = RandomRect(rng, -50, 50, 600);
                RECT bc = UILayout::Cpp::CandScrollBarRect(client, sb, dpi, 10);
                RECT br = {};
                dime_ui_cand_scrollbar_rect((const DimeRect*)&client, sb, dpi, 10, (DimeRect*)&br);
                Assert::IsTrue(EqualRect(&bc, &br) && bc.left == br.left, (L"CandScrollBarRect " + R(bc) + L" " + R(br)).c_str());

                for (int row = 0; row < rows; row++)
                {
                    UILayout::CandRow rc = UILayout::Cpp::CandRowLayout(client, row, mc, sb, dpi);
                    DimeCandRow rr = {};
                    dime_ui_cand_row((const DimeRect*)&client, row, &mr, sb, dpi, &rr);
                    Assert::IsTrue(memcmp(&rc, &rr, sizeof(rc)) == 0, L"CandRowLayout");
                }

                int length = (int)(rng() % 40);
                int tc = 0, tr = 0;
                int lc = UILayout::Cpp::CandTruncateItem(length, &tc);
                int lr = dime_ui_cand_truncate(length, &tr);
                Assert::IsTrue(lc == lr && tc == tr, L"CandTruncateItem");

                Assert::AreEqual((int)UILayout::Cpp::CandItemNeedsMeasure(lc, wndWidth), dime_ui_cand_item_needs_measure(lc, wndWidth), L"CandItemNeedsMeasure");

                int measuredCx = (int)(rng() % 4000);
                int titleC = mc.cxTitle, titleR = mc.cxTitle;
                UINT widthC = wndWidth, widthR = wndWidth;
                BOOL wc = UILayout::Cpp::CandWidenForItem(lc, measuredCx, mc.leftPadding + mc.cxLine, &titleC, &widthC);
                int wr = dime_ui_cand_widen(lc, measuredCx, mc.leftPadding + mc.cxLine, &titleR, &widthR);
                Assert::IsTrue((int)wc == wr && titleC == titleR && widthC == widthR, L"CandWidenForItem");

                Assert::AreEqual((int)UILayout::Cpp::CandNeedsResize(titleC, client, sb), dime_ui_cand_needs_resize(titleC, (const DimeRect*)&client, sb), L"CandNeedsResize");

                UINT pageStart = rng() % 30, count = rng() % 40;
                for (int k = 0; k < 8; k++)
                {
                    POINT pt = { client.left - 5 + (LONG)(rng() % (client.right - client.left + 10 + 1)),
                                 client.top - 5 + (LONG)(rng() % (client.bottom - client.top + 10 + 1)) };
                    if (client.right < client.left || client.bottom < client.top) pt = { (LONG)(rng() % 100), (LONG)(rng() % 100) };
                    int hc = UILayout::Cpp::CandHitRow(client, mc.padding, sb, mc.cyLine, (UINT)rows, pageStart, count, pt);
                    int hr = dime_ui_cand_hit_row((const DimeRect*)&client, mc.padding, sb, mc.cyLine, (UINT)rows, pageStart, count, { pt.x, pt.y });
                    Assert::AreEqual(hc, hr, L"CandHitRow");
                }

                RECT ac = UILayout::Cpp::CandScrollColumnRect(client, sb), ar = {};
                dime_ui_cand_mouse_area((const DimeRect*)&client, sb, 0, (DimeRect*)&ar);
                Assert::IsTrue(memcmp(&ac, &ar, sizeof(ac)) == 0, L"CandScrollColumnRect");
                ac = UILayout::Cpp::CandListAreaRect(client, sb);
                dime_ui_cand_mouse_area((const DimeRect*)&client, sb, 1, (DimeRect*)&ar);
                Assert::IsTrue(memcmp(&ac, &ar, sizeof(ac)) == 0, L"CandListAreaRect");
                ac = UILayout::Cpp::CandPageIndicatorRect(client, mc.cyLine, rows, sb, dpi);
                dime_ui_cand_page_indicator((const DimeRect*)&client, mc.cyLine, rows, sb, dpi, (DimeRect*)&ar);
                Assert::IsTrue(memcmp(&ac, &ar, sizeof(ac)) == 0, L"CandPageIndicatorRect");

                int lf = -(int)(rng() % 80);
                Assert::AreEqual(UILayout::Cpp::CandPageIndicatorFontHeight(lf), dime_ui_mul_div(lf, 70, 100), L"CandPageIndicatorFontHeight");
                compared++;
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" candidate layouts").c_str());
        }

        TEST_METHOD(ScrollBarLayout)
        {
            std::mt19937 rng(12);
            const UINT dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384 };
            int compared = 0;
            for (int i = 0; i < 200000; i++)
            {
                UINT dpi = (rng() % 4 == 0) ? 48 + rng() % 500 : dpis[rng() % 8];
                RECT client = { 0, 0, (LONG)(rng() % 50), (LONG)(rng() % 900) - 20 };
                if (rng() % 5 == 0) client = RandomRect(rng, -40, 40, 400);
                UILayout::ScrollInfo si = { (int)(rng() % 300) - 5, (int)(rng() % 40) - 3, (int)(rng() % 320) - 10 };

                UILayout::ScrollParts pc = UILayout::Cpp::ScrollBarParts(client, dpi, si);
                DimeScrollParts pr = {};
                dime_ui_sb_parts((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, &pr);
                Assert::IsTrue(memcmp(&pc, &pr, sizeof(pc)) == 0, (L"ScrollBarParts thumb " + R(pc.thumb)).c_str());

                for (int k = 0; k < 6; k++)
                {
                    POINT pt = { (LONG)(rng() % 60) - 5 + client.left, (LONG)(rng() % 920) - 10 + client.top };
                    Assert::AreEqual(UILayout::Cpp::ScrollBarHitTest(client, dpi, si, pt),
                        dime_ui_sb_hit_test((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, { pt.x, pt.y }), L"ScrollBarHitTest");
                }

                // The paint path only draws the idle thumb for nMax > nPage.
                if (si.nMax > si.nPage)
                {
                    RECT ic = {}, ir = {};
                    BOOL hc = UILayout::Cpp::ScrollBarIdleThumb(client, dpi, si, &ic);
                    int hr = dime_ui_sb_idle_thumb((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, (DimeRect*)&ir);
                    Assert::AreEqual((int)hc, hr, L"ScrollBarIdleThumb");
                    if (hc) Assert::IsTrue(memcmp(&ic, &ir, sizeof(ic)) == 0, L"ScrollBarIdleThumb rect");
                }

                UILayout::ScrollArrows wc = UILayout::Cpp::ScrollBarArrows(client, dpi);
                DimeScrollArrows wr = {};
                dime_ui_sb_arrows((const DimeRect*)&client, dpi, &wr);
                Assert::IsTrue(memcmp(&wc, &wr, sizeof(wc)) == 0, L"ScrollBarArrows");

                int start = (int)(rng() % 300), delta = (int)(rng() % 2000) - 1000;
                int dc = -7, dr = -7;
                BOOL gc = UILayout::Cpp::ScrollBarDragPos(client, dpi, si, start, delta, &dc);
                int gr = dime_ui_sb_drag_pos((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, start, delta, &dr);
                Assert::AreEqual((int)gc, gr, L"ScrollBarDragPos");
                if (gc) Assert::AreEqual(dc, dr, L"ScrollBarDragPos position");

                Assert::AreEqual(UILayout::Cpp::ScrollBarClampPos(si.nPos, si.nMax), dime_ui_sb_clamp_pos(si.nPos, si.nMax), L"ScrollBarClampPos");

                int pw = (int)(rng() % 600), ph = (int)(rng() % 900);
                RECT tc = UILayout::Cpp::ScrollBarAttachedRect(pw, ph, ::MulDiv(16, dpi, 96)), tr = {};
                dime_ui_sb_attached_rect(pw, ph, ::MulDiv(16, dpi, 96), (DimeRect*)&tr);
                Assert::IsTrue(memcmp(&tc, &tr, sizeof(tc)) == 0, L"ScrollBarAttachedRect");
                compared++;
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" scroll bar layouts").c_str());
        }
    };
}
