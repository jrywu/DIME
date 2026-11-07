/********************************************************************************
* UILayoutParityTest: the window layout calculations, C++ (UILayout::Cpp, the
* code moved from CBaseWindow / CUIPresenter) vs Rust (dime-ui layout via
* dime_ui_*): RectInRect, the work-area probe point, the placement around the
* composition (below / above / docked, multiple monitors with negative
* coordinates), and the notify-window stack (grow direction, new-slot position,
* restacking with and without a candidate window).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
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
    };
}
