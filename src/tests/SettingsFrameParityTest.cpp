/********************************************************************************
* SettingsFrameParityTest: C++ (SettingsFrame::Cpp, formerly inline in
* WM_CREATE / UpdateTheme / WM_SETTINGCHANGE / WM_DPICHANGED) vs Rust
* (dime-settings frame.rs through SettingsFrame::Rust). Every event kind
* over both dark flags, system themes, content present or not, several dpis
* and client sizes, plus random event sequences: the new dark flag and every
* effect (kind, argument, rectangle) must match.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <climits>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsFrame.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SF = SettingsFrame;

namespace
{
    std::wstring D(const std::vector<SF::FrameEffect>& v, int dark)
    {
        std::wstring s = L"dark=" + std::to_wstring(dark);
        for (const auto& e : v)
            s += L" [" + std::to_wstring(e.kind) + L"," + std::to_wstring(e.arg) + L"," + std::to_wstring(e.rc.left) + L","
                + std::to_wstring(e.rc.top) + L"," + std::to_wstring(e.rc.right) + L"," + std::to_wstring(e.rc.bottom) + L"]";
        return s;
    }

    // Runs one event on both; returns the number of effects.
    size_t Check(SF::FrameState& a, SF::FrameState& b, const SF::FrameEvent& ev, const std::wstring& what)
    {
        std::vector<SF::FrameEffect> ea, eb;
        SF::Cpp::Handle(a, ev, ea);
        SF::Rust::Handle(b, ev, eb);
        std::wstring da = D(ea, a.isDark), db = D(eb, b.isDark);
        if (da != db)
            Assert::Fail((what + L"\nC++:  " + da + L"\nRust: " + db).c_str());
        return ea.size();
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsFrameParityTest)
    {
    public:
        TEST_METHOD(EveryEvent)
        {
            size_t effects = 0;
            int cases = 0;
            for (int kind = 0; kind <= 5; kind++)
                for (int dark = 0; dark < 2; dark++)
                    for (int sys : { 0, 1, 2 })
                        for (int content = 0; content < 2; content++)
                            for (UINT dpi : { 96u, 120u, 144u, 192u, 0u })
                                for (int hasRect = 0; hasRect < 2; hasRect++) {
                                    SF::FrameEvent ev = {};
                                    ev.kind = kind;
                                    ev.systemDark = sys;
                                    ev.dpi = dpi;
                                    ev.hasContent = content;
                                    ev.clientW = 1100;
                                    ev.clientH = 780;
                                    ev.hasRect = hasRect;
                                    ev.rc = { 100, 50, 1300, 900 };
                                    SF::FrameState a = { dark }, b = { dark };
                                    effects += Check(a, b, ev, L"kind=" + std::to_wstring(kind) + L" dark=" + std::to_wstring(dark)
                                        + L" sys=" + std::to_wstring(sys) + L" content=" + std::to_wstring(content)
                                        + L" dpi=" + std::to_wstring(dpi) + L" rect=" + std::to_wstring(hasRect));
                                    cases++;
                                }
            Assert::IsTrue(effects > 1000, L"effects");
            for (UINT dpi : { 0u, 96u, 120u, 144u, 168u, 192u, 288u }) {
                SIZE a = SF::Cpp::InitialSize(dpi), b = SF::Rust::InitialSize(dpi);
                Assert::IsTrue(a.cx == b.cx && a.cy == b.cy, (L"initial size dpi=" + std::to_wstring(dpi)).c_str());
            }
        }

        TEST_METHOD(RandomSequences)
        {
            std::mt19937 rng(5);
            size_t effects = 0;
            for (int n = 0; n < 2000; n++) {
                SF::FrameState a = { (int)(rng() & 1) }, b = a;
                for (int step = 0; step < 12; step++) {
                    SF::FrameEvent ev = {};
                    ev.kind = 1 + (int)(rng() % 4);
                    ev.systemDark = (int)(rng() % 3);
                    ev.dpi = (UINT)(rng() % 400);
                    ev.hasContent = (int)(rng() & 1);
                    ev.clientW = (rng() % 50 == 0) ? INT_MIN : (int)(rng() % 3000) - 100;
                    ev.clientH = (int)(rng() % 2000);
                    ev.hasRect = (int)(rng() & 1);
                    ev.rc = { (int)(rng() % 4000) - 2000, (int)(rng() % 4000) - 2000, (int)(rng() % 4000), (int)(rng() % 4000) };
                    if (rng() % 50 == 0) ev.rc.right = INT_MIN;
                    effects += Check(a, b, ev, L"sequence " + std::to_wstring(n) + L" step " + std::to_wstring(step));
                }
            }
            Assert::IsTrue(effects > 50000, L"effects");
        }
    };
}
