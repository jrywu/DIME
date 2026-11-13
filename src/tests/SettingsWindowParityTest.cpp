/********************************************************************************
* SettingsWindowParityTest: the whole settings window, C++ (SettingsWindow)
* vs Rust (dime-settings window.rs, through dime_settings_window_*). Both are
* real, hidden windows over the same kind of recording SettingsApp::Host fake
* (no CConfig, no real %APPDATA%, no registry but a test key). Random
* operations go to both: resizes, clicks on the sidebar, cards, breadcrumb
* and hamburger, wheel and scroll-bar messages, mouse moves and leaves,
* theme / setting / dpi changes, WM_COPYDATA mode switches, and WM_COMMAND
* from the page's check boxes, combo boxes and edit box. After every step:
* the app calls, the driver state and snapshot, every child control (class,
* id, styles, rectangle, visibility, text, check, combo items and
* selection), the scroll bar, and the pixels of both frames must match.
* At the end the placement each window saves must match too.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsWindow.h"
#include "../DIMESettings/SettingsApp.h"
#include "../DIMESettings/SettingsDriver.h"
#include "dime_ffi.h"
#include "SettingsFakeApp.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace SettingsTestApp;

namespace
{
    const wchar_t kTestKey[] = L"SOFTWARE\\DIME\\SettingsUIWindowParityTest";

    int R(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

    // Messages to child controls during one step (failure diagnostics).
    std::wstring g_trace;
    LRESULT CALLBACK TraceHook(int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION) {
            const CWPSTRUCT* m = (const CWPSTRUCT*)l;
            HWND parent = GetParent(m->hwnd);
            wchar_t pc[64] = {};
            if (parent) GetClassNameW(parent, pc, 64);
            if (wcsstr(pc, L"DIMEWinParityContent") &&
                (m->message == WM_SHOWWINDOW || m->message == WM_STYLECHANGED || m->message == WM_WINDOWPOSCHANGING
                 || (m->message >= 0x00F0 && m->message <= 0x00F7) || (m->message >= 0x0140 && m->message <= 0x0170))) {
                g_trace += std::wstring(pc).substr(wcslen(pc) - 1) + L" id=" + std::to_wstring(GetDlgCtrlID(m->hwnd)) + L" msg="
                    + std::to_wstring(m->message) + L" w=" + std::to_wstring(m->wParam);
                if (m->message == WM_WINDOWPOSCHANGING) {
                    const WINDOWPOS* wp = (const WINDOWPOS*)m->lParam;
                    g_trace += L" flags=" + std::to_wstring(wp->flags) + L" y=" + std::to_wstring(wp->y);
                }
                if (m->message == WM_STYLECHANGED) {
                    const STYLESTRUCT* ss = (const STYLESTRUCT*)m->lParam;
                    g_trace += L" old=" + std::to_wstring(ss->styleOld) + L" new=" + std::to_wstring(ss->styleNew);
                }
                g_trace += L"\n";
            }
        }
        return CallNextHookEx(nullptr, code, w, l);
    }

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    HWND ContentOf(HWND main)
    {
        for (HWND c = GetWindow(main, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            if (ClassOf(c) == L"DIMEWinParityContent") return c;
        return nullptr;
    }

    std::vector<HWND> Controls(HWND content)
    {
        std::vector<HWND> out;
        if (!content) return out;
        for (HWND c = GetWindow(content, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) out.push_back(c);
        return out;
    }

    std::wstring Text(HWND h)
    {
        int len = GetWindowTextLengthW(h);
        std::wstring t(len + 1, L'\0');
        GetWindowTextW(h, t.data(), len + 1);
        t.resize(len);
        return t;
    }

    std::wstring DescribeControl(HWND content, HWND c)
    {
        wchar_t cls[64] = {};
        GetClassNameW(c, cls, 64);
        RECT rc; GetWindowRect(c, &rc);
        MapWindowPoints(nullptr, content, (POINT*)&rc, 2);
        std::wstring d = std::wstring(cls) + L" id=" + std::to_wstring(GetDlgCtrlID(c))
            + L" st=" + std::to_wstring(GetWindowLongW(c, GWL_STYLE)) + L" ex=" + std::to_wstring(GetWindowLongW(c, GWL_EXSTYLE))
            + L" rc=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," + std::to_wstring(rc.right) + L"," + std::to_wstring(rc.bottom)
            + L" vis=" + std::to_wstring(IsWindowVisible(c) ? 1 : 0) + L" en=" + std::to_wstring(IsWindowEnabled(c) ? 1 : 0)
            + L" text=[" + Text(c).substr(0, 120) + L"]";
        if (wcscmp(cls, L"Button") == 0) d += L" check=" + std::to_wstring(SendMessageW(c, BM_GETCHECK, 0, 0));
        if (wcscmp(cls, L"ComboBox") == 0) {
            int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
            d += L" sel=" + std::to_wstring(SendMessageW(c, CB_GETCURSEL, 0, 0)) + L" items=";
            for (int i = 0; i < n; i++) {
                wchar_t item[128] = {};
                SendMessageW(c, CB_GETLBTEXT, i, (LPARAM)item);
                d += std::wstring(item) + L":" + std::to_wstring(SendMessageW(c, CB_GETITEMDATA, i, 0)) + L"|";
            }
        }
        return d;
    }

    std::wstring Pixels(HWND h, bool content, bool rust)
    {
        RECT rc; GetClientRect(h, &rc);
        int w = rc.right, ht = rc.bottom;
        if (w <= 0 || ht <= 0) return L"empty";
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -ht;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bmp);
        if (rust) dime_settings_window_paint(h, dc, w, ht, content ? 1 : 0);
        else SettingsWindow::PaintForTest(h, dc, w, ht, content);
        GdiFlush();
        unsigned long long hash = 1469598103934665603ull;
        const unsigned char* p = (const unsigned char*)bits;
        for (size_t i = 0; i < (size_t)w * ht * 4; i++) { hash ^= p[i]; hash *= 1099511628211ull; }
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        return std::to_wstring(hash);
    }

    struct Side {
        bool rust;
        FakeApp app;
        HWND main = nullptr;
        size_t logPos = 0;

        void Create(int mode, const std::wstring& dir)
        {
            app.dir = dir;
            std::wstring sfx = rust ? L"_R" : L"_C";
            std::wstring mainCls = L"DIMEWinParityMain" + sfx, contentCls = L"DIMEWinParityContent" + sfx;
            const wchar_t* caption = L"DIME 設定 v1.3.0";
            if (rust) {
                main = (HWND)dime_settings_window_create(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                    static_cast<SettingsApp::Host*>(&app), GetModuleHandleW(nullptr), mode, (const uint16_t*)caption,
                    (const uint16_t*)mainCls.c_str(), (const uint16_t*)contentCls.c_str(), (const uint16_t*)kTestKey,
                    (const uint16_t*)L"R", 10);
            } else {
                main = SettingsWindow::CreateForTest(&app, GetModuleHandleW(nullptr), (IME_MODE)mode, caption,
                    mainCls.c_str(), contentCls.c_str(), kTestKey, L"C");
            }
            Assert::IsNotNull(main);
            SetWindowPos(main, nullptr, 100, 100, 1000, 760, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        std::wstring NewLog()
        {
            std::wstring s = app.log.substr(logPos);
            logPos = app.log.size();
            return s;
        }

        std::wstring State()
        {
            SettingsDriver::State st = {};
            SettingsSnapshot sn = {};
            bool ok = rust ? dime_settings_window_state(main, &st, &sn) != 0 : SettingsWindow::StateForTest(main, &st, &sn);
            if (!ok) return L"no data";
            std::wstring s;
            const unsigned char* p = (const unsigned char*)&st;
            for (size_t i = 0; i < sizeof(st); i++) s += std::to_wstring(p[i]) + L",";
            s += L" snap:";
            p = (const unsigned char*)&sn;
            for (size_t i = 0; i < sizeof(sn); i++) s += std::to_wstring(p[i]) + L",";
            return s;
        }

        std::vector<std::wstring> Describe()
        {
            std::vector<std::wstring> out;
            RECT rc; GetClientRect(main, &rc);
            out.push_back(L"main " + std::to_wstring(rc.right) + L"x" + std::to_wstring(rc.bottom) + L" [" + Text(main) + L"]");
            HWND content = ContentOf(main);
            if (content) {
                RECT cr; GetWindowRect(content, &cr);
                MapWindowPoints(nullptr, main, (POINT*)&cr, 2);
                SCROLLINFO si = { sizeof(si), SIF_ALL };
                GetScrollInfo(content, SB_VERT, &si);
                out.push_back(L"content " + std::to_wstring(cr.left) + L"," + std::to_wstring(cr.top) + L"," + std::to_wstring(cr.right)
                    + L"," + std::to_wstring(cr.bottom) + L" scroll " + std::to_wstring(si.nMin) + L"-" + std::to_wstring(si.nMax)
                    + L" page " + std::to_wstring(si.nPage) + L" pos " + std::to_wstring(si.nPos)
                    + L" vis " + std::to_wstring(IsWindowVisible(content) ? 1 : 0));
                for (HWND c : Controls(content)) out.push_back(DescribeControl(content, c));
                out.push_back(L"content px " + Pixels(content, true, rust));
            }
            out.push_back(L"main px " + Pixels(main, false, rust));
            return out;
        }
    };

    // One random operation, as message parameters both sides can replay.
    struct Op {
        int kind = 0;
        int a = 0, b = 0, c = 0;
        std::wstring text;
        std::wstring Name() const
        {
            return L"op " + std::to_wstring(kind) + L" (" + std::to_wstring(a) + L"," + std::to_wstring(b) + L","
                + std::to_wstring(c) + L") " + text;
        }
    };

    Op RandomOp(std::mt19937& rng)
    {
        Op op;
        // Clicks and page controls more often than frame events.
        static const int kinds[] = { 0, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 11, 11, 11, 12, 13 };
        op.kind = kinds[R(rng, 0, (int)_countof(kinds) - 1)];
        op.a = R(rng, 0, 10000);
        op.b = R(rng, 0, 10000);
        op.c = R(rng, 0, 10000);
        static const wchar_t* modes[] = { L"--mode dayi", L"--mode array", L"--mode phonetic", L"--mode generic", L"--mode bogus" };
        static const wchar_t* texts[] = { L"5", L"12", L"", L"x", L"0", L"99999", L"３" };
        if (op.kind == 9) op.text = modes[R(rng, 0, 4)];
        if (op.kind == 12) op.text = texts[R(rng, 0, 6)];
        return op;
    }

    LPARAM XY(int x, int y) { return MAKELPARAM(x, y); }

    void Apply(Side& s, const Op& op)
    {
        HWND main = s.main;
        HWND content = ContentOf(main);
        RECT mc; GetClientRect(main, &mc);
        int mw = mc.right > 0 ? mc.right : 1, mh = mc.bottom > 0 ? mc.bottom : 1;
        switch (op.kind) {
        case 0: {   // resize, including narrow widths (narrow sidebar mode)
            int w = (op.c % 6 == 0) ? 420 + op.a % 300 : 820 + op.a % 600, h = 420 + op.b % 500;
            SetWindowPos(main, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
            break;
        }
        case 1: {   // click the sidebar / breadcrumb / hamburger area of the main window
            int x = (op.c % 4 == 0) ? op.a % 320 : 16 + op.a % 220, y = (op.c % 4 == 0) ? op.b % mh : 30 + op.b % 380;
            SendMessageW(main, WM_LBUTTONDOWN, MK_LBUTTON, XY(x, y));
            SendMessageW(main, WM_LBUTTONUP, 0, XY(x, y));
            break;
        }
        case 2: {   // click inside the content area (cards, rows, chevrons)
            if (!content) break;
            RECT cc; GetClientRect(content, &cc);
            int x = op.a % (cc.right > 0 ? cc.right : 1), y = op.b % (cc.bottom > 0 ? cc.bottom : 1);
            SendMessageW(content, WM_LBUTTONDOWN, MK_LBUTTON, XY(x, y));
            SendMessageW(content, WM_LBUTTONUP, 0, XY(x, y));
            break;
        }
        case 3: {   // wheel over the content area or the main window
            HWND target = (op.c & 1) && content ? content : main;
            short delta = (short)((op.a % 2) ? -WHEEL_DELTA * (1 + op.b % 3) : WHEEL_DELTA);
            SendMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), XY(200, 200));
            break;
        }
        case 4: {   // scroll bar
            if (!content) break;
            static const int codes[] = { SB_LINEUP, SB_LINEDOWN, SB_PAGEUP, SB_PAGEDOWN, SB_THUMBTRACK, SB_TOP, SB_BOTTOM };
            int code = codes[op.a % 7];
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(code, op.b % 800), 0);
            break;
        }
        case 5: {   // hover and leave
            HWND target = (op.c & 1) && content ? content : main;
            SendMessageW(target, WM_MOUSEMOVE, 0, XY(op.a % mw, op.b % mh));
            if (op.c % 3 == 0) SendMessageW(target, WM_MOUSELEAVE, 0, 0);
            break;
        }
        case 6:     // theme change
            s.app.dark = (op.a & 1) != 0;
            SendMessageW(main, WM_THEMECHANGED, 0, 0);
            break;
        case 7:     // setting change
            s.app.dark = (op.a & 1) != 0;
            SendMessageW(main, WM_SETTINGCHANGE, 0, 0);
            break;
        case 8: {   // dpi change with a suggested rectangle
            static const UINT dpis[] = { 96, 120, 144 };
            UINT dpi = dpis[op.a % 3];
            s.app.dpi = dpi;
            RECT rc = { 100, 100, 100 + 600 + op.b % 500, 100 + 450 + op.c % 400 };
            SendMessageW(main, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&rc);
            break;
        }
        case 9: {   // second instance: WM_COPYDATA mode switch
            COPYDATASTRUCT cds = { 1, (DWORD)((op.text.size() + 1) * sizeof(wchar_t)), (PVOID)op.text.c_str() };
            SendMessageW(main, WM_COPYDATA, 0, (LPARAM)&cds);
            break;
        }
        case 10: {  // a check box of the page
            std::vector<HWND> boxes;
            for (HWND c : Controls(content))
                if ((GetWindowLongW(c, GWL_STYLE) & 0xF) == BS_AUTOCHECKBOX && ClassOf(c) == L"Button") boxes.push_back(c);
            if (boxes.empty()) break;
            HWND b = boxes[op.a % boxes.size()];
            SendMessageW(b, BM_SETCHECK, SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(content, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(b), BN_CLICKED), (LPARAM)b);
            break;
        }
        case 11: {  // a combo box of the page
            std::vector<HWND> combos;
            for (HWND c : Controls(content))
                if (ClassOf(c) == L"ComboBox") combos.push_back(c);
            if (combos.empty()) break;
            HWND cb = combos[op.a % combos.size()];
            int n = (int)SendMessageW(cb, CB_GETCOUNT, 0, 0);
            if (n <= 0) break;
            SendMessageW(cb, CB_SETCURSEL, op.b % n, 0);
            SendMessageW(content, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(cb), CBN_SELCHANGE), (LPARAM)cb);
            break;
        }
        case 12: {  // the max-codes edit box
            for (HWND c : Controls(content)) {
                if (ClassOf(c) != L"Edit") continue;
                SetWindowTextW(c, op.text.c_str());   // EN_CHANGE reaches the content area
                break;
            }
            break;
        }
        default:    // back to the card list (Escape-free: click the breadcrumb row's left edge)
            SendMessageW(main, WM_LBUTTONDOWN, MK_LBUTTON, XY(330 + op.a % 40, 10 + op.b % 30));
            SendMessageW(main, WM_LBUTTONUP, 0, XY(330 + op.a % 40, 10 + op.b % 30));
            break;
        }
        Pump();
    }

    std::wstring ReadPlacement(const wchar_t* value)
    {
        BYTE buf[128] = {};
        DWORD cb = sizeof(buf), type = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, kTestKey, value, RRF_RT_ANY, &type, buf, &cb) != ERROR_SUCCESS) return L"none";
        std::wstring s;
        for (DWORD i = 0; i < cb; i++) s += std::to_wstring(buf[i]) + L",";
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsWindowParityTest)
    {
    public:
        TEST_CLASS_CLEANUP(Cleanup)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
        }

        TEST_METHOD(RandomOperations)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
            wchar_t base[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, base);
            std::wstring dir = std::wstring(base) + L"DIMEWindowParity" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(dir.c_str(), nullptr);
            SettingsSnapshot snap = SettingsModel::LoadFromConfig();
            int steps = 0, subPages = 0, narrow = 0, applies = 0, colorModes = 0, switches = 0, loadPage = 0;
            for (int run = 0; run < 12; run++) {
                std::mt19937 rng(700 + run);
                Side c{ false }, r{ true };
                for (Side* s : { &c, &r }) {
                    s->app.snap = snap;
                    s->app.dark = (run & 1) != 0;
                    s->app.colorMode = run % 4;
                    s->app.snap.colorMode = run % 4;
                }
                int mode = run % 4;
                c.Create(mode, dir);
                r.Create(mode, dir);
                Pump();
                std::wstring what = L"run " + std::to_wstring(run);
                std::vector<std::wstring> prev;
                auto compare = [&](const std::wstring& step) {
                    std::wstring lc = c.NewLog(), lr = r.NewLog();
                    if (lc != lr) Assert::Fail((what + L" " + step + L": app calls\nC++:\n" + lc + L"Rust:\n" + lr).c_str());
                    std::wstring sc = c.State(), sr = r.State();
                    if (sc != sr) Assert::Fail((what + L" " + step + L": state\nC++:  " + sc + L"\nRust: " + sr).c_str());
                    std::vector<std::wstring> dc = c.Describe(), dr = r.Describe();
                    Assert::AreEqual(dc.size(), dr.size(), (what + L" " + step + L": children").c_str());
                    for (size_t i = 0; i < dc.size(); i++)
                        if (dc[i] != dr[i])
                            Assert::Fail((what + L" " + step + L" item " + std::to_wstring(i) + L"\nC++:  " + dc[i] + L"\nRust: " + dr[i]
                                + L"\nbefore: " + (i < prev.size() ? prev[i] : std::wstring()) + L"\nlog:\n" + lc
                                + L"\ntrace:\n" + g_trace.substr(0, 6000)).c_str());
                    prev = dc;
                };
                compare(L"create");
                for (int i = 0; i < 60; i++) {
                    Op op = RandomOp(rng);
                    g_trace.clear();
                    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, TraceHook, nullptr, GetCurrentThreadId());
                    Apply(c, op);
                    Apply(r, op);
                    UnhookWindowsHookEx(hook);
                    compare(L"step " + std::to_wstring(i) + L" " + op.Name());
                    steps++;
                    SettingsDriver::State st = {};
                    SettingsWindow::StateForTest(c.main, &st, nullptr);
                    if (st.nav.navLevel == 1) subPages++;
                    if (st.nav.sidebarNarrowMode) narrow++;
                    if (st.nav.selectedModeIndex == 5) loadPage++;
                }
                DestroyWindow(c.main);
                DestroyWindow(r.main);
                Pump();
                Assert::AreEqual(c.NewLog(), r.NewLog(), (what + L": destroy").c_str());
                for (size_t at = 0; (at = c.app.log.find(L"apply ", at)) != std::wstring::npos; at++) applies++;
                for (size_t at = 0; (at = c.app.log.find(L"setColorMode ", at)) != std::wstring::npos; at++) colorModes++;
                for (size_t at = 0; (at = c.app.log.find(L"engine ", at)) != std::wstring::npos; at++) switches++;
                Assert::AreEqual(ReadPlacement(L"C"), ReadPlacement(L"R"), (what + L": placement").c_str());
                Assert::AreNotEqual(std::wstring(L"none"), ReadPlacement(L"R"));
            }
            RemoveDirectoryW(dir.c_str());
            std::wstring cover = L"subPages " + std::to_wstring(subPages) + L" narrow " + std::to_wstring(narrow)
                + L" loadPage " + std::to_wstring(loadPage) + L" applies " + std::to_wstring(applies)
                + L" colorModes " + std::to_wstring(colorModes) + L" switches " + std::to_wstring(switches);
            Logger::WriteMessage(cover.c_str());
            Assert::IsTrue(steps == 720);
            Assert::IsTrue(subPages > 30 && narrow > 30 && loadPage > 5 && applies > 20 && colorModes > 0 && switches > 20,
                cover.c_str());
        }
    };
}
