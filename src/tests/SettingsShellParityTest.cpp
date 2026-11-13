/********************************************************************************
* SettingsShellParityTest: the settings window's shell, C++ (SettingsShell::Cpp,
* formerly in SettingsWindow.cpp) vs Rust (dime-settings shell.rs through
* SettingsShell::Rust):
* - window placement saved to / restored from / deleted in a test HKCU key
*   (raw bytes, return values, the restored WINDOWPLACEMENT; bad sizes,
*   lengths, types, and off-monitor rectangles),
* - theme brushes (colours, and deleted afterwards),
* - the WM_PAINT double buffering (pixels painted into a DIB, the callback
*   sequence, and BeginPaint / EndPaint validating the window),
* - class registration (WNDCLASSEX as registered; the content class once),
* - Run (caption, styles, size, placement or nCmdShow, the started callback,
*   WM_QUIT's code, -1 when the window cannot be created).
* The real placement key is never touched.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsShell.h"
#include "../DIMESettings/SettingsController.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SS = SettingsShell;
namespace SWP = SettingsWindowPaint;

namespace
{
    const wchar_t kTestKey[] = L"SOFTWARE\\DIME\\SettingsUIShellParityTest";

    int R(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

    // ---- Placement --------------------------------------------------------

    LRESULT CALLBACK PlainProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

    HWND MakeTopLevel()
    {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = PlainProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"DIMEShellParityPlain";
            RegisterClassW(&wc);
            registered = true;
        }
        return CreateWindowExW(0, L"DIMEShellParityPlain", L"", WS_OVERLAPPEDWINDOW, 100, 100, 400, 300,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    std::vector<BYTE> ReadValue(const wchar_t* value, DWORD* type)
    {
        BYTE buf[256] = {};
        DWORD cb = sizeof(buf);
        *type = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, kTestKey, value, RRF_RT_ANY, type, buf, &cb) != ERROR_SUCCESS)
            return {};
        return std::vector<BYTE>(buf, buf + cb);
    }

    void WriteValue(const wchar_t* value, DWORD type, const void* data, DWORD cb)
    {
        HKEY k;
        Assert::AreEqual((LONG)ERROR_SUCCESS, RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr));
        RegSetValueExW(k, value, 0, type, (const BYTE*)data, cb);
        RegCloseKey(k);
    }

    bool ValueExists(const wchar_t* value)
    {
        DWORD type = 0;
        return RegGetValueW(HKEY_CURRENT_USER, kTestKey, value, RRF_RT_ANY, &type, nullptr, nullptr) == ERROR_SUCCESS;
    }

    std::wstring PlacementText(HWND h)
    {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        GetWindowPlacement(h, &wp);
        return L"flags=" + std::to_wstring(wp.flags) + L" show=" + std::to_wstring(wp.showCmd)
            + L" min=" + std::to_wstring(wp.ptMinPosition.x) + L"," + std::to_wstring(wp.ptMinPosition.y)
            + L" max=" + std::to_wstring(wp.ptMaxPosition.x) + L"," + std::to_wstring(wp.ptMaxPosition.y)
            + L" rc=" + std::to_wstring(wp.rcNormalPosition.left) + L"," + std::to_wstring(wp.rcNormalPosition.top)
            + L"," + std::to_wstring(wp.rcNormalPosition.right) + L"," + std::to_wstring(wp.rcNormalPosition.bottom)
            + L" visible=" + std::to_wstring(IsWindowVisible(h) ? 1 : 0);
    }

    WINDOWPLACEMENT RandomPlacement(std::mt19937& rng)
    {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        static const UINT shows[] = { SW_HIDE, SW_SHOWMINNOACTIVE };
        wp.showCmd = shows[R(rng, 0, 1)];
        wp.flags = 0;
        wp.ptMinPosition = { R(rng, -1, 50), R(rng, -1, 50) };
        wp.ptMaxPosition = { R(rng, -1, 50), R(rng, -1, 50) };
        int x = R(rng, 0, 300), y = R(rng, 0, 300);
        wp.rcNormalPosition = { x, y, x + R(rng, 200, 700), y + R(rng, 150, 500) };
        return wp;
    }

    // ---- Paint ------------------------------------------------------------

    struct Source {
        std::wstring log;
        bool hasData = true;
        bool hasContent = true;
        SWP::PaintContext main = {}, content = {};
        SS::PaintSource Get() { return { this, &HasData, &Prepare }; }
        static int HasData(void* u)
        {
            Source* s = static_cast<Source*>(u);
            s->log += L"hasData;";
            return s->hasData ? 1 : 0;
        }
        static void Prepare(void* u, int content, SWP::PaintContext* first, SWP::PaintContext* second, int* hasContent)
        {
            Source* s = static_cast<Source*>(u);
            s->log += L"prepare " + std::to_wstring(content) + (second ? L" second" : L" nosecond") + L";";
            if (!content) {
                *first = s->main;
                *hasContent = s->hasContent ? 1 : 0;
            } else {
                *first = s->content;
                *second = s->main;
            }
        }
    };

    struct Dib {
        HDC dc = nullptr;
        HBITMAP bmp = nullptr;
        HGDIOBJ old = nullptr;
        void* bits = nullptr;
        int w = 0, h = 0;
        Dib(int width, int height) : w(width), h(height)
        {
            HDC screen = GetDC(nullptr);
            dc = CreateCompatibleDC(screen);
            ReleaseDC(nullptr, screen);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            old = SelectObject(dc, bmp);
            memset(bits, 0x5A, (size_t)w * h * 4);   // so "nothing painted" is visible
        }
        ~Dib()
        {
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
        }
        std::vector<BYTE> Pixels() const
        {
            GdiFlush();
            return std::vector<BYTE>((BYTE*)bits, (BYTE*)bits + (size_t)w * h * 4);
        }
    };

    SWP::PaintContext RandomContext(std::mt19937& rng, const SWP::Fonts& f, HBRUSH sidebar, HBRUSH content,
        const SettingsSnapshot& snap, bool dark, UINT dpi, int w, int h)
    {
        SWP::PaintContext c = {};
        c.theme = ComputeThemeColors(dark);
        c.snapshot = snap;
        c.fontTitle = f.title;
        c.fontCardHeader = f.cardHeader;
        c.fontBody = f.body;
        c.fontDesc = f.desc;
        c.fontMDL2 = f.mdl2;
        c.fontMDL2Icon = f.mdl2Icon;
        c.brushSidebarBg = sidebar;
        c.brushContentBg = content;
        c.iconModule = GetModuleHandleW(nullptr);
        c.dpi = dpi;
        c.hasMDL2 = f.hasMDL2;
        c.clientW = w;
        c.clientH = h;
        c.cardW = MulDiv(R(rng, 400, 800), dpi, 96);
        c.sysFace = GetSysColor(COLOR_3DFACE);
        c.selectedModeIndex = R(rng, 0, 5);
        c.hoverSidebarIndex = R(rng, -1, 5);
        c.navLevel = R(rng, 0, 1);
        c.currentCardIndex = R(rng, 0, 6);
        c.hoverRowIndex = R(rng, -1, 4);
        c.currentMode = R(rng, 0, 3);
        c.scrollPos = R(rng, 0, 200);
        c.sidebarNarrowMode = R(rng, 0, 1);
        c.sidebarCollapsed = R(rng, 0, 1);
        c.colorGridExpanded = R(rng, 0, 1);
        for (bool& e : c.cardExpanded) e = R(rng, 0, 1) != 0;
        for (BYTE& t : c.toggleOn) t = (BYTE)R(rng, 0, 1);
        return c;
    }

    // ---- Run --------------------------------------------------------------

    struct RunLog {
        std::wstring log;
        int quitCode = 0;
    };
    RunLog* g_run = nullptr;

    LRESULT CALLBACK RunProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_CLOSE && g_run) {
            g_run->log += L"close " + PlacementText(h) + L" iconic=" + std::to_wstring(IsIconic(h) ? 1 : 0) + L";";
            DestroyWindow(h);
            return 0;
        }
        if (m == WM_DESTROY && g_run) {
            PostQuitMessage(g_run->quitCode);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    // WM_NCCREATE fails: CreateWindowEx returns null.
    LRESULT CALLBACK FailProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_NCCREATE) return FALSE;
        return DefWindowProcW(h, m, w, l);
    }

    void Started(void* user, HWND h)
    {
        RunLog* r = static_cast<RunLog*>(user);
        wchar_t text[128] = {};
        GetWindowTextW(h, text, 128);
        RECT rc; GetWindowRect(h, &rc);
        WNDCLASSEXW wc = { sizeof(wc) };
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        GetClassInfoExW(GetModuleHandleW(nullptr), cls, &wc);
        r->log += std::wstring(L"started [") + text + L"] st=" + std::to_wstring(GetWindowLongW(h, GWL_STYLE))
            + L" ex=" + std::to_wstring(GetWindowLongW(h, GWL_EXSTYLE)) + L" size=" + std::to_wstring(rc.right - rc.left)
            + L"x" + std::to_wstring(rc.bottom - rc.top) + L" visible=" + std::to_wstring(IsWindowVisible(h) ? 1 : 0)
            + L" clsStyle=" + std::to_wstring(wc.style) + L" cursor=" + std::to_wstring(wc.hCursor != nullptr) + L";";
        PostMessageW(h, WM_CLOSE, 0, 0);
    }

    LRESULT CALLBACK FailProcForClass(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

    std::wstring ClassInfo(const wchar_t* name)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        if (!GetClassInfoExW(GetModuleHandleW(nullptr), name, &wc)) return L"none";
        return L"style=" + std::to_wstring(wc.style) + L" proc=" + std::to_wstring((ULONG_PTR)wc.lpfnWndProc)
            + L" cls=" + std::to_wstring(wc.cbClsExtra) + L" wnd=" + std::to_wstring(wc.cbWndExtra)
            + L" icon=" + std::to_wstring((ULONG_PTR)wc.hIcon) + L" sm=" + std::to_wstring((ULONG_PTR)wc.hIconSm)
            + L" cursor=" + std::to_wstring((ULONG_PTR)wc.hCursor) + L" bg=" + std::to_wstring((ULONG_PTR)wc.hbrBackground)
            + L" menu=" + std::to_wstring((ULONG_PTR)wc.lpszMenuName);
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsShellParityTest)
    {
    public:
        TEST_CLASS_CLEANUP(Cleanup)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
        }

        TEST_METHOD(Placement)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
            std::mt19937 rng(41);
            HWND a = MakeTopLevel(), b = MakeTopLevel();
            WINDOWPLACEMENT base = { sizeof(base) };
            base.showCmd = SW_HIDE;
            base.rcNormalPosition = { 50, 60, 450, 360 };

            // Save: the same placement gives the same bytes.
            for (int i = 0; i < 40; i++) {
                WINDOWPLACEMENT wp = RandomPlacement(rng);
                SetWindowPlacement(a, &wp);
                SetWindowPlacement(b, &wp);
                bool ra = SS::Cpp::SavePlacement(a, kTestKey, L"C");
                bool rb = SS::Rust::SavePlacement(b, kTestKey, L"R");
                Assert::AreEqual(ra, rb);
                DWORD ta = 0, tb = 0;
                std::vector<BYTE> va = ReadValue(L"C", &ta), vb = ReadValue(L"R", &tb);
                Assert::AreEqual(ta, tb);
                Assert::IsTrue(va == vb, (L"saved bytes differ at " + std::to_wstring(i)).c_str());
                Assert::AreEqual((size_t)sizeof(WINDOWPLACEMENT), va.size());
            }
            // Save with no window: false, nothing written.
            Assert::AreEqual(SS::Cpp::SavePlacement(nullptr, kTestKey, L"CN"), SS::Rust::SavePlacement(nullptr, kTestKey, L"RN"));
            Assert::AreEqual(ValueExists(L"CN"), ValueExists(L"RN"));

            // Restore: valid, missing, short, long, bad length, wrong type, off-monitor.
            for (int i = 0; i < 80; i++) {
                WINDOWPLACEMENT wp = RandomPlacement(rng);
                int kind = i < 20 ? 0 : R(rng, 0, 6);
                std::wstring what = L"restore case " + std::to_wstring(kind);
                RegDeleteKeyValueW(HKEY_CURRENT_USER, kTestKey, L"V");
                switch (kind) {
                case 0: WriteValue(L"V", REG_BINARY, &wp, sizeof(wp)); break;
                case 1: break;   // missing
                case 2: WriteValue(L"V", REG_BINARY, &wp, sizeof(wp) - 4); break;
                case 3: { BYTE big[64] = {}; memcpy(big, &wp, sizeof(wp)); WriteValue(L"V", REG_BINARY, big, sizeof(big)); break; }
                case 4: wp.length = sizeof(wp) + 1; WriteValue(L"V", REG_BINARY, &wp, sizeof(wp)); break;
                case 5: WriteValue(L"V", REG_DWORD, &wp, sizeof(DWORD)); break;
                default:
                    wp.rcNormalPosition = { -60000, -60000, -59000, -59500 };
                    WriteValue(L"V", REG_BINARY, &wp, sizeof(wp));
                    break;
                }
                SetWindowPlacement(a, &base);
                SetWindowPlacement(b, &base);
                bool ra = SS::Cpp::RestorePlacement(a, kTestKey, L"V");
                bool rb = SS::Rust::RestorePlacement(b, kTestKey, L"V");
                Assert::AreEqual(ra, rb, what.c_str());
                Assert::AreEqual(PlacementText(a), PlacementText(b), what.c_str());
                if (kind == 0) Assert::IsTrue(ra, what.c_str());
            }

            // Delete: the value goes; a missing key is not created.
            WriteValue(L"D", REG_BINARY, &base, sizeof(base));
            SS::Cpp::DeletePlacement(kTestKey, L"D");
            bool cppGone = !ValueExists(L"D");
            WriteValue(L"D", REG_BINARY, &base, sizeof(base));
            SS::Rust::DeletePlacement(kTestKey, L"D");
            Assert::IsTrue(cppGone);
            Assert::IsFalse(ValueExists(L"D"));
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
            SS::Cpp::DeletePlacement(kTestKey, L"D");
            SS::Rust::DeletePlacement(kTestKey, L"D");
            HKEY k;
            Assert::AreNotEqual((LONG)ERROR_SUCCESS, RegOpenKeyExW(HKEY_CURRENT_USER, kTestKey, 0, KEY_READ, &k));

            DestroyWindow(a);
            DestroyWindow(b);
        }

        TEST_METHOD(Brushes)
        {
            std::mt19937 rng(42);
            for (int i = 0; i < 200; i++) {
                ThemeColors t = ComputeThemeColors(R(rng, 0, 1) != 0);
                t.sidebarBg = (COLORREF)(rng() & 0xFFFFFF);
                t.contentBg = (COLORREF)(rng() & 0xFFFFFF);
                t.cardBg = (COLORREF)(rng() & 0xFFFFFF);
                SS::Brushes a = SS::Cpp::CreateBrushes(t), b = SS::Rust::CreateBrushes(t);
                HBRUSH ha[3] = { a.sidebarBg, a.contentBg, a.cardBg }, hb[3] = { b.sidebarBg, b.contentBg, b.cardBg };
                for (int k = 0; k < 3; k++) {
                    LOGBRUSH la = {}, lb = {};
                    Assert::AreEqual((int)sizeof(la), GetObjectW(ha[k], sizeof(la), &la));
                    Assert::AreEqual((int)sizeof(lb), GetObjectW(hb[k], sizeof(lb), &lb));
                    Assert::AreEqual(la.lbStyle, lb.lbStyle);
                    Assert::AreEqual((DWORD)la.lbColor, (DWORD)lb.lbColor);
                    Assert::AreEqual((ULONG_PTR)la.lbHatch, (ULONG_PTR)lb.lbHatch);
                }
                SS::Cpp::DestroyBrushes(a);
                SS::Rust::DestroyBrushes(b);
                Assert::IsTrue(!a.sidebarBg && !a.contentBg && !a.cardBg);
                Assert::IsTrue(!b.sidebarBg && !b.contentBg && !b.cardBg);
                // (GDI caches deleted solid brushes, so GetObjectType cannot
                // tell whether they were deleted.)
                // Null brushes stay null.
                SS::Brushes z = { nullptr, nullptr, nullptr };
                SS::Rust::DestroyBrushes(z);
                SS::Cpp::DestroyBrushes(z);
            }
        }

        TEST_METHOD(PaintInto)
        {
            std::mt19937 rng(43);
            SettingsSnapshot snap = SettingsModel::LoadFromConfig();
            int frames = 0;
            for (UINT dpi : { 96u, 144u }) {
                SWP::Fonts f = {};
                SWP::CreateFonts(dpi, &f);
                for (int iter = 0; iter < 24; iter++) {
                    bool dark = R(rng, 0, 1) != 0;
                    ThemeColors t = ComputeThemeColors(dark);
                    HBRUSH sb = CreateSolidBrush(t.sidebarBg), cb = CreateSolidBrush(t.contentBg);
                    int w = R(rng, 300, 900), h = R(rng, 200, 700);
                    Source src;
                    src.hasData = R(rng, 0, 4) != 0;
                    src.hasContent = R(rng, 0, 1) != 0;
                    src.main = RandomContext(rng, f, sb, cb, snap, dark, dpi, w + 200, h);
                    src.content = src.main;
                    src.content.clientW = w;
                    src.content.clientH = h;
                    for (int content = 0; content < 2; content++) {
                        std::wstring what = L"dpi " + std::to_wstring(dpi) + L" iter " + std::to_wstring(iter)
                            + (content ? L" content" : L" main") + L" data " + std::to_wstring(src.hasData);
                        Dib da(w, h), db(w, h);
                        Source sa = src, sb2 = src;
                        if (content) {
                            SS::Cpp::PaintContentInto(da.dc, w, h, sa.Get());
                            SS::Rust::PaintContentInto(db.dc, w, h, sb2.Get());
                        } else {
                            SS::Cpp::PaintMainInto(da.dc, w, h, sa.Get());
                            SS::Rust::PaintMainInto(db.dc, w, h, sb2.Get());
                        }
                        Assert::AreEqual(sa.log, sb2.log, (what + L": callbacks").c_str());
                        std::vector<BYTE> pa = da.Pixels(), pb = db.Pixels();
                        if (pa != pb) {
                            size_t k = 0;
                            while (pa[k] == pb[k]) k++;
                            Assert::Fail((what + L": pixel " + std::to_wstring(k / 4 % w) + L"," + std::to_wstring(k / 4 / w)).c_str());
                        }
                        frames++;
                    }
                    DeleteObject(sb);
                    DeleteObject(cb);
                }
                for (HFONT x : { f.title, f.cardHeader, f.body, f.desc, f.mdl2, f.mdl2Icon })
                    if (x) DeleteObject(x);
            }
            Assert::IsTrue(frames >= 96);
        }

        TEST_METHOD(PaintValidatesWindow)
        {
            // BeginPaint / EndPaint around the body: the update region is
            // gone afterwards, and the callbacks run as for PaintInto.
            // A visible but fully transparent window: hidden windows have no
            // update region.
            DestroyWindow(MakeTopLevel());   // registers the class
            HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"DIMEShellParityPlain", L"",
                WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
            ShowWindow(h, SW_SHOWNOACTIVATE);
            SWP::Fonts f = {};
            SWP::CreateFonts(96, &f);
            std::mt19937 rng(44);
            ThemeColors t = ComputeThemeColors(false);
            HBRUSH sb = CreateSolidBrush(t.sidebarBg), cb = CreateSolidBrush(t.contentBg);
            Source base;
            base.main = RandomContext(rng, f, sb, cb, SettingsModel::LoadFromConfig(), false, 96, 400, 300);
            base.content = base.main;
            for (int side = 0; side < 2; side++) {
                for (int content = 0; content < 2; content++) {
                    for (int data = 0; data < 2; data++) {
                        Source s = base;
                        s.hasData = data != 0;
                        InvalidateRect(h, nullptr, TRUE);
                        Assert::IsTrue(GetUpdateRect(h, nullptr, FALSE) != FALSE);
                        if (side == 0) content ? SS::Cpp::PaintContent(h, s.Get()) : SS::Cpp::PaintMain(h, s.Get());
                        else content ? SS::Rust::PaintContent(h, s.Get()) : SS::Rust::PaintMain(h, s.Get());
                        Assert::IsFalse(GetUpdateRect(h, nullptr, FALSE) != FALSE);
                        std::wstring expect = data ? (content ? L"hasData;prepare 1 second;" : L"hasData;prepare 0 nosecond;")
                                                   : L"hasData;";
                        Assert::AreEqual(expect, s.log);
                    }
                }
            }
            DeleteObject(sb);
            DeleteObject(cb);
            for (HFONT x : { f.title, f.cardHeader, f.body, f.desc, f.mdl2, f.mdl2Icon })
                if (x) DeleteObject(x);
            DestroyWindow(h);
        }

        TEST_METHOD(ClassRegistration)
        {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            for (int icon : { 0, 1, 32512, 107 }) {
                std::wstring c = L"DIMEShellParityMainC" + std::to_wstring(icon);
                std::wstring r = L"DIMEShellParityMainR" + std::to_wstring(icon);
                bool ra = SS::Cpp::RegisterMainClass(inst, c.c_str(), PlainProc, icon);
                bool rb = SS::Rust::RegisterMainClass(inst, r.c_str(), PlainProc, icon);
                Assert::AreEqual(ra, rb);
                Assert::AreEqual(ClassInfo(c.c_str()), ClassInfo(r.c_str()));
                // A second registration fails on both.
                Assert::AreEqual(SS::Cpp::RegisterMainClass(inst, c.c_str(), PlainProc, icon),
                    SS::Rust::RegisterMainClass(inst, r.c_str(), PlainProc, icon));
                UnregisterClassW(c.c_str(), inst);
                UnregisterClassW(r.c_str(), inst);
            }
            // The content class registers unless the name is taken: a second
            // call with another procedure keeps the first registration.
            SS::Cpp::RegisterContentClass(inst, L"DIMEShellParityContentC", PlainProc);
            SS::Cpp::RegisterContentClass(inst, L"DIMEShellParityContentC", FailProcForClass);
            SS::Rust::RegisterContentClass(inst, L"DIMEShellParityContentR", PlainProc);
            SS::Rust::RegisterContentClass(inst, L"DIMEShellParityContentR", FailProcForClass);
            Assert::AreEqual(ClassInfo(L"DIMEShellParityContentC"), ClassInfo(L"DIMEShellParityContentR"));
            Assert::AreNotEqual(std::wstring(L"none"), ClassInfo(L"DIMEShellParityContentR"));
            Assert::IsTrue(ClassInfo(L"DIMEShellParityContentR").find(L"proc=" + std::to_wstring((ULONG_PTR)PlainProc) + L" ")
                != std::wstring::npos);
            UnregisterClassW(L"DIMEShellParityContentC", inst);
            UnregisterClassW(L"DIMEShellParityContentR", inst);
        }

        TEST_METHOD(Run)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
            std::mt19937 rng(45);
            HINSTANCE inst = GetModuleHandleW(nullptr);
            for (int iter = 0; iter < 8; iter++) {
                // 0: no placement (nCmdShow), 1: valid placement, 2: off-monitor placement, 3: no window.
                int kind = iter % 4;
                WINDOWPLACEMENT wp = RandomPlacement(rng);
                if (kind == 2) wp.rcNormalPosition = { -60000, -60000, -59000, -59500 };
                RegDeleteKeyValueW(HKEY_CURRENT_USER, kTestKey, L"P");
                if (kind == 1 || kind == 2) WriteValue(L"P", REG_BINARY, &wp, sizeof(wp));
                int cmd = (iter & 1) ? SW_HIDE : SW_SHOWMINNOACTIVE;
                std::wstring results[2];
                int codes[2] = {};
                for (int side = 0; side < 2; side++) {
                    std::wstring cls = std::wstring(L"DIMEShellParityRun") + (side ? L"R" : L"C") + std::to_wstring(iter);
                    RunLog log;
                    log.quitCode = 100 + iter;
                    g_run = &log;
                    SS::RunParams p = {};
                    p.instance = inst;
                    p.nCmdShow = cmd;
                    p.mainClass = cls.c_str();
                    p.mainProc = kind == 3 ? FailProc : RunProc;
                    p.iconId = 107;
                    p.caption = L"DIME 設定 v1.3.0";
                    p.placementKey = kTestKey;
                    p.placementValue = L"P";
                    p.started = &Started;
                    p.user = &log;
                    codes[side] = side ? SS::Rust::Run(p) : SS::Cpp::Run(p);
                    g_run = nullptr;
                    results[side] = log.log;
                    UnregisterClassW(cls.c_str(), inst);
                }
                std::wstring what = L"run " + std::to_wstring(iter);
                Assert::AreEqual(codes[0], codes[1], what.c_str());
                Assert::AreEqual(kind == 3 ? -1 : 100 + iter, codes[0], what.c_str());
                if (kind == 0 || kind == 2) {
                    // CW_USEDEFAULT cascades: compare everything but the normal rectangle's position.
                    for (std::wstring& r : results) {
                        size_t a = r.find(L" rc=");
                        if (a != std::wstring::npos) {
                            size_t e = r.find(L' ', a + 4);
                            std::wstring rc = r.substr(a + 4, e - a - 4);
                            int l, t, rr, b;
                            swscanf_s(rc.c_str(), L"%d,%d,%d,%d", &l, &t, &rr, &b);
                            r = r.substr(0, a) + L" size=" + std::to_wstring(rr - l) + L"x" + std::to_wstring(b - t) + r.substr(e);
                        }
                    }
                }
                Assert::AreEqual(results[0], results[1], what.c_str());
                if (kind != 3) Assert::IsTrue(results[0].find(L"close ") != std::wstring::npos, what.c_str());
            }
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
        }
    };
}
