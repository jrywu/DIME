/********************************************************************************
* SettingsLifecycleParityTest: C++ (SettingsLifecycle::Cpp) vs Rust
* (dime-settings lifecycle.rs through SettingsLifecycle::Rust) for what
* wWinMain makes of its command line, the mode a second instance sends with
* WM_COPYDATA, the default card expand state, and where scrolling moves each
* control. Command lines are hand-picked edge cases plus random token
* sequences; expand defaults use the shipped tables and random synthetic
* card lists; scroll plans use random controls, scroll positions, and dpis.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsLifecycle.h"
#include "../DIMESettings/SettingsPageLayout.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SL = SettingsLifecycle;

namespace
{
    std::wstring D(const SL::LaunchArgs& a)
    {
        return L"legacy=" + std::to_wstring(a.legacy) + L" resetUi=" + std::to_wstring(a.resetUi)
            + L" newUi=" + std::to_wstring(a.newUi) + L" mode=" + std::to_wstring(a.guiMode);
    }

    void CheckLine(const wchar_t* line, int& total)
    {
        std::wstring what = line ? L"[" + std::wstring(line) + L"]" : L"null";
        std::wstring a = D(SL::Cpp::ParseLaunch(line)), b = D(SL::Rust::ParseLaunch(line));
        if (a != b)
            Assert::Fail((L"launch " + what + L"\nC++:  " + a + L"\nRust: " + b).c_str());
        int ma = (int)SL::Cpp::CopyDataMode(line), mb = (int)SL::Rust::CopyDataMode(line);
        if (ma != mb)
            Assert::Fail((L"copydata " + what + L" C++=" + std::to_wstring(ma) + L" Rust=" + std::to_wstring(mb)).c_str());
        total++;
    }

    std::wstring D(const SL::ScrollAction& a)
    {
        return L"moved=" + std::to_wstring(a.moved) + L" y=" + std::to_wstring(a.y)
            + L" rgn=" + std::to_wstring(a.resetRgn) + L" show=" + std::to_wstring(a.show);
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsLifecycleParityTest)
    {
    public:
        TEST_METHOD(LaunchAndCopyData)
        {
            int total = 0;
            static const wchar_t* fixed[] = {
                L"", L"--mode dayi", L"--mode Array", L"--mode PHONETIC x", L"--mode generic",
                L"--mode", L"--mode ", L"--mode  dayi", L"--mode dayiX", L"x--mode array",
                L"--legacy", L"--legacy --mode array", L"--reset-ui", L"--reset-ui --mode phonetic",
                L"--reset", L"--reset --mode array", L"--reset-ui --reset --mode array",
                L"--reset --reset-ui --mode array", L"--resetx --mode array", L"--reset- --mode dayi",
                L"--mode array --get FontSize", L"--set A=1 --mode dayi", L"--import x --mode dayi",
                L"--export x --mode dayi", L"--load x --mode dayi", L"--list --mode dayi", L"--help --mode dayi",
                L"--getter --mode dayi", L"--mode dayi --mode array", L"--mode dayi\t", L"--mode \x5927\x6613",
                L"--mode aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", L"--mode phoneticphoneticphoneticphoneticX",
                L"--MODE dayi", L"--mode\tdayi",
            };
            CheckLine(nullptr, total);
            for (const wchar_t* line : fixed)
                CheckLine(line, total);

            static const wchar_t* tokens[] = {
                L"--mode ", L"--mode", L"dayi", L"DAYI", L"array", L"Phonetic", L"generic", L"nope", L" ", L"  ",
                L"--legacy", L"--reset", L"--reset-ui", L"-", L"--get", L"--set", L"--import", L"--export",
                L"--load", L"--list", L"--help", L"x", L"\t", L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", L"\x5927\x6613",
            };
            std::mt19937 rng(20261112);
            for (int n = 0; n < 20000; n++) {
                std::wstring line;
                int len = rng() % 7;
                for (int t = 0; t < len; t++)
                    line += tokens[rng() % _countof(tokens)];
                CheckLine(line.c_str(), total);
            }
            Assert::IsTrue(total > 20000, L"command lines");
        }

        TEST_METHOD(DefaultExpanded)
        {
            int imeCount = 0, ltCount = 0;
            const LayoutCard* ime = GetIMEPageLayout(&imeCount);
            const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
            Assert::IsTrue(imeCount > 0 && ltCount > 0, L"tables");
            std::mt19937 rng(7);
            int cases = 0;
            for (int n = 0; n < 3000; n++) {
                std::vector<LayoutCard> a, b;
                int na = n == 0 ? imeCount : (int)(rng() % 22);
                int nb = n == 0 ? ltCount : (int)(rng() % 22);
                for (int i = 0; i < na; i++) {
                    LayoutCard c = ime[i % imeCount];
                    if (n) c.defaultExpanded = (rng() & 1) != 0;
                    a.push_back(c);
                }
                for (int i = 0; i < nb; i++) {
                    LayoutCard c = lt[i % ltCount];
                    if (n) c.defaultExpanded = (rng() & 1) != 0;
                    b.push_back(c);
                }
                bool ec[SettingsWindowLayout::kExpandedSlots], er[SettingsWindowLayout::kExpandedSlots];
                memset(ec, 0xCC, sizeof(ec));
                memset(er, 0x33, sizeof(er));
                SL::Cpp::DefaultExpanded(a.data(), na, b.data(), nb, ec);
                SL::Rust::DefaultExpanded(a.data(), na, b.data(), nb, er);
                for (int i = 0; i < SettingsWindowLayout::kExpandedSlots; i++) {
                    if (ec[i] != er[i])
                        Assert::Fail((L"case " + std::to_wstring(n) + L" ime=" + std::to_wstring(na) + L" load="
                            + std::to_wstring(nb) + L" slot " + std::to_wstring(i)).c_str());
                }
                cases++;
            }
            Assert::AreEqual(3000, cases);
        }

        TEST_METHOD(PlanScroll)
        {
            std::mt19937 rng(11);
            int actions = 0;
            for (int n = 0; n < 5000; n++) {
                std::vector<SL::ScrollItem> items(rng() % 30);
                for (auto& it : items) {
                    it.origY = (int)(rng() % 3000) - 200;
                    it.origW = (rng() % 5 == 0) ? 0 : (int)(rng() % 300);
                    it.origH = (rng() % 5 == 0) ? 0 : (int)(rng() % 40);
                    it.alive = (rng() % 8) != 0;
                    it.visible = rng() & 1;
                }
                int scrollPos = (int)(rng() % 2500);
                if (n % 500 == 0) scrollPos = (n % 1000) ? INT_MIN : INT_MAX;
                static const UINT dpis[] = { 96u, 120u, 144u, 192u, 0u };
                UINT dpi = dpis[rng() % 5];
                std::vector<SL::ScrollAction> a = SL::Cpp::PlanScroll(items, scrollPos, dpi);
                std::vector<SL::ScrollAction> b = SL::Rust::PlanScroll(items, scrollPos, dpi);
                Assert::AreEqual(a.size(), b.size(), L"count");
                for (size_t i = 0; i < a.size(); i++) {
                    std::wstring da = D(a[i]), db = D(b[i]);
                    if (da != db)
                        Assert::Fail((L"case " + std::to_wstring(n) + L" item " + std::to_wstring(i) + L"\nC++:  " + da
                            + L"\nRust: " + db).c_str());
                    actions += a[i].show != 0;
                }
            }
            Assert::IsTrue(actions > 1000, L"show / hide coverage");
        }
    };
}
