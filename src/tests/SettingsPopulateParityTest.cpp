/********************************************************************************
* SettingsPopulateParityTest: filling the settings controls from the snapshot,
* C++ (SettingsPopulate::Cpp::Populate, formerly PopulateControls) vs Rust
* (dime-settings populate.rs applied by SettingsPopulate::PopulateRust), on
* two sets of real combo boxes, checkboxes, and the max-codes edit. Random
* snapshots, reverse-conversion lists, the 1809 flag, and controls missing
* from the page; every combo's items, item data, and selection, every check
* state, and the edit text must match.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsPopulate.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SP = SettingsPopulate;

namespace
{
    const SettingsControlId kCombos[] = {
        CTRL_COLOR_MODE, CTRL_ARRAY_SCOPE, CTRL_CHARSET_SCOPE, CTRL_NUMERIC_PAD, CTRL_SHIFT_ENGLISH_DIGIT_MODE,
        CTRL_DAYI_SELKEY_MODE, CTRL_PHONETIC_KB, CTRL_IME_SHIFT_MODE, CTRL_DOUBLE_SINGLE_BYTE,
        CTRL_REVERSE_CONVERSION, CTRL_KEYBOARD_OPEN_CLOSE, CTRL_OUTPUT_CHT_CHS,
    };
    const SettingsControlId kChecks[] = {
        CTRL_SHOW_NOTIFY, CTRL_ARROW_KEY_SW_PAGES, CTRL_SPACE_AS_PAGEDOWN, CTRL_SPACE_AS_FIRST_CAND,
        CTRL_AUTO_COMPOSE, CTRL_CLEAR_ON_BEEP, CTRL_MAKE_PHRASE, CTRL_CUSTOM_TABLE_PRIORITY, CTRL_DO_BEEP,
        CTRL_DO_BEEP_NOTIFY, CTRL_DO_BEEP_CANDI, CTRL_DAYI_ARTICLE, CTRL_SENTENCE_MODE, CTRL_ARRAY_FORCE_SP,
        CTRL_ARRAY_NOTIFY_SP, CTRL_ARRAY_SINGLE_QUOTE,
    };

    struct Page
    {
        HWND parent = nullptr;
        std::map<int, HWND> controls;
        ~Page() { if (parent) DestroyWindow(parent); }
    };

    HWND Find(void* ctx, SettingsControlId id)
    {
        auto& m = static_cast<Page*>(ctx)->controls;
        auto it = m.find((int)id);
        return it == m.end() ? nullptr : it->second;
    }

    void Build(Page& page, const std::vector<bool>& present)
    {
        page.parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
        size_t k = 0;
        for (SettingsControlId id : kCombos)
            if (present[k++])
                page.controls[id] = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                    0, 0, 100, 200, page.parent, nullptr, nullptr, nullptr);
        for (SettingsControlId id : kChecks)
            if (present[k++])
                page.controls[id] = CreateWindowExW(0, L"BUTTON", L"x", WS_CHILD | BS_AUTOCHECKBOX,
                    0, 0, 10, 10, page.parent, nullptr, nullptr, nullptr);
        if (present[k++])
            page.controls[CTRL_MAX_CODES] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD,
                0, 0, 50, 20, page.parent, nullptr, nullptr, nullptr);
    }

    std::wstring Describe(HWND h, bool combo)
    {
        std::wstring s;
        if (combo) {
            int n = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
            s += L"count=" + std::to_wstring(n) + L" sel=" + std::to_wstring((int)SendMessageW(h, CB_GETCURSEL, 0, 0));
            for (int i = 0; i < n; i++) {
                wchar_t buf[256] = {};
                SendMessageW(h, CB_GETLBTEXT, i, (LPARAM)buf);
                s += L" [" + std::wstring(buf) + L"|" + std::to_wstring((LONG_PTR)SendMessageW(h, CB_GETITEMDATA, i, 0)) + L"]";
            }
        } else {
            wchar_t buf[64] = {};
            GetWindowTextW(h, buf, 64);
            s = L"check=" + std::to_wstring((int)SendMessageW(h, BM_GETCHECK, 0, 0)) + L" text=" + buf;
        }
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsPopulateParityTest)
    {
    public:
        TEST_METHOD(RandomSnapshots)
        {
            std::mt19937 rng(91);
            static const wchar_t* descs[] = { L"大易輸入法", L"行列輸入法", L"注音", L"Microsoft Bopomofo", L"" };
            size_t controlCount = _countof(kCombos) + _countof(kChecks) + 1;
            for (int iter = 0; iter < 300; iter++) {
                SettingsSnapshot s = {};
                BYTE* p = (BYTE*)&s;
                for (size_t i = 0; i < sizeof(s); i++) p[i] = (BYTE)rng();
                s.fontFaceName[0] = 0;
                // Mostly in range, sometimes not.
                auto pick = [&](int n) { return (rng() % 5) ? (int)(rng() % n) : (int)(rng() % 20) - 5; };
                s.colorMode = pick(4); s.arrayScope = pick(6); s.numericPad = pick(3);
                s.shiftEnglishDigitMode = pick(3); s.dayiSelkeyMode = pick(3); s.phoneticKeyboardLayout = pick(2);
                s.imeShiftMode = pick(4); s.doubleSingleByteMode = pick(3);
                s.maxCodes = (rng() % 4) ? (UINT)(rng() % 10) : (UINT)rng();

                std::vector<SP::ReverseEntry> reverse;
                int rc = (int)(rng() % 5);
                for (int i = 0; i < rc; i++) reverse.push_back({ descs[rng() % _countof(descs)], (rng() % 3) == 0 });

                SP::Inputs in = {};
                in.snapshot = &s;
                in.windows1809OrLater = (rng() & 1) != 0;
                in.reverseIsNull = (rng() & 1) != 0;
                in.reverse = reverse.data();
                in.reverseCount = rc;

                std::vector<bool> present(controlCount);
                for (size_t i = 0; i < controlCount; i++) present[i] = (rng() % 6) != 0;
                Page a, b;
                Build(a, present);
                Build(b, present);
                SP::Cpp::Populate(in, &Find, &a);
                SP::PopulateRust(in, &Find, &b);

                for (auto& kv : a.controls) {
                    bool combo = false;
                    for (SettingsControlId id : kCombos) if ((int)id == kv.first) combo = true;
                    std::wstring da = Describe(kv.second, combo), db = Describe(b.controls[kv.first], combo);
                    if (da != db)
                        Assert::Fail((L"iter " + std::to_wstring(iter) + L" control " + std::to_wstring(kv.first)
                            + L"\nC++:  " + da + L"\nRust: " + db).c_str());
                }
            }
        }
    };
}
