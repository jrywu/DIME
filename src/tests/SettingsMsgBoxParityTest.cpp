/********************************************************************************
* SettingsMsgBoxParityTest: ThemedMessageBox, C++ (SettingsMsgBox::Cpp::Show,
* lifted out of SettingsWindow.cpp) vs Rust (dime-settings msgboxwin.rs via
* dime_settings_message_box). Both run as real modal windows on the test
* thread under their own class names. A driver thread waits for the window,
* records its rectangle and client pixels, then dismisses it with a key, a
* click, or WM_CLOSE; the returned button ids must match too.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <gdiplus.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "../DIMESettings/SettingsMsgBox.h"
#include "../DIMESettings/SettingsWindowLayout.h"
#include "dime_ffi.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    ULONG_PTR g_token = 0;
    std::atomic<UINT> g_dpi{ 96 };

    UINT TestDpi(HWND) { return g_dpi.load(); }
    uint32_t TestDpiRaw(void*) { return g_dpi.load(); }

    // How the driver dismisses the box.
    struct Action { UINT msg; WPARAM wp; int button; };   // button >= 0: click its centre

    struct Observed
    {
        bool found = false;
        RECT window = {};
        RECT client = {};
        std::vector<DWORD> pixels;
        int result = -999;
    };

    std::vector<DWORD> CaptureClient(HWND h, const RECT& rc)
    {
        int w = rc.right, hgt = rc.bottom;
        HDC wdc = GetDC(h);
        HDC mem = CreateCompatibleDC(wdc);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -hgt;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, w, hgt, wdc, 0, 0, SRCCOPY);
        GdiFlush();
        std::vector<DWORD> px((DWORD*)bits, (DWORD*)bits + (size_t)w * hgt);
        for (DWORD& p : px) p &= 0xFFFFFF;
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(h, wdc);
        return px;
    }

    Observed Run(bool rust, HWND parent, const wchar_t* text, const wchar_t* caption, UINT type,
        bool dark, const Action& act)
    {
        const wchar_t* cls = rust ? L"DIMEMsgBoxParityRust" : L"DIMEMsgBoxParityCpp";
        ThemeColors tc = ComputeThemeColors(dark);
        Observed obs;
        std::thread driver([&]() {
            HWND h = nullptr;
            // Wait until ShowWindow ran (the window exists, 1x1, during WM_CREATE).
            for (int i = 0; i < 500 && !(h && IsWindowVisible(h)); i++) {
                h = FindWindowW(cls, nullptr);
                if (!(h && IsWindowVisible(h))) Sleep(10);
            }
            if (!h) return;
            SendMessageW(h, WM_NULL, 0, 0);   // the modal loop is running
            obs.found = true;
            GetWindowRect(h, &obs.window);
            GetClientRect(h, &obs.client);
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            obs.pixels = CaptureClient(h, obs.client);
            // Hover in and out: exercises WM_MOUSEMOVE / WM_MOUSELEAVE.
            SendMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(obs.client.right - 30, obs.client.bottom - 30));
            SendMessageW(h, WM_MOUSELEAVE, 0, 0);
            SendMessageW(h, WM_KEYDOWN, 'A', 0);   // not handled: box stays
            if (act.button >= 0) {
                UINT dpi = g_dpi.load();
                SettingsWindowLayout::MsgBoxLayout L =
                    SettingsWindowLayout::Cpp::MsgBoxLayoutFor(false, 3, 0, 0, dpi);
                // Button geometry depends only on the client width and button count.
                SettingsWindowLayout::MsgBoxButtons b = SettingsWindowLayout::Cpp::MsgBoxButtonsFor(type);
                L.rcClient = obs.client;
                L.btnY = obs.client.bottom - L.margin - L.btnH;
                int x = SettingsWindowLayout::Cpp::MsgBoxFirstButtonX(L, b.count) + act.button * (L.btnW + L.btnGap) + L.btnW / 2;
                int y = L.btnY + L.btnH / 2;
                PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            } else {
                PostMessageW(h, act.msg, act.wp, 0);
            }
        });
        if (rust) {
            obs.result = dime_settings_message_box(parent, (const uint16_t*)text, (const uint16_t*)caption, type,
                (const uint16_t*)cls, dark ? 1 : 0, &tc, &TestDpiRaw);
        } else {
            obs.result = SettingsMsgBox::Cpp::Show(parent, text, caption, type, cls, dark, tc, &TestDpi);
        }
        driver.join();
        return obs;
    }

    void Compare(HWND parent, const wchar_t* text, UINT type, bool dark, const Action& act, const std::wstring& what)
    {
        Observed a = Run(false, parent, text, L"DIME 測試", type, dark, act);
        Observed b = Run(true, parent, text, L"DIME 測試", type, dark, act);
        Assert::IsTrue(a.found && b.found, (what + L": window not found").c_str());
        Assert::AreEqual(a.result, b.result, (what + L": result").c_str());
        auto R = [](const RECT& r) { return L"(" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L")"; };
        Assert::IsTrue(EqualRect(&a.window, &b.window) != 0, (what + L": window rect " + R(a.window) + L" vs " + R(b.window)).c_str());
        Assert::IsTrue(EqualRect(&a.client, &b.client) != 0, (what + L": client rect").c_str());
        Assert::IsTrue(a.pixels.size() == b.pixels.size() && !a.pixels.empty(), (what + L": capture").c_str());
        for (size_t i = 0; i < a.pixels.size(); i++) {
            if (a.pixels[i] != b.pixels[i]) {
                int w = a.client.right;
                Assert::Fail((what + L": pixel (" + std::to_wstring(i % w) + L"," + std::to_wstring(i / w) + L")").c_str());
            }
        }
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsMsgBoxParityTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Start)
        {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&g_token, &input, nullptr);
        }

        TEST_CLASS_CLEANUP(Stop)
        {
            if (g_token) Gdiplus::GdiplusShutdown(g_token);
            g_token = 0;
            g_dpi = 96;
        }

        TEST_METHOD(KeysAndClose)
        {
            const wchar_t* text = L"儲存失敗：偵測到 3 個格式錯誤行，第一個錯誤在第 2 行。\n\n  第 2 行\n  第 5 行\n";
            UINT types[] = { MB_OK, MB_ICONWARNING | MB_OK, MB_YESNO | MB_ICONQUESTION, MB_YESNOCANCEL, MB_OKCANCEL | MB_ICONINFORMATION };
            Action acts[] = { { WM_KEYDOWN, VK_RETURN, -1 }, { WM_KEYDOWN, VK_ESCAPE, -1 }, { WM_CLOSE, 0, -1 } };
            for (UINT dpi : { 96u, 144u }) {
                g_dpi = dpi;
                for (UINT t : types)
                    for (const Action& a : acts)
                        Compare(nullptr, text, t, (t & 1) != 0, a,
                            L"type=" + std::to_wstring(t) + L" msg=" + std::to_wstring(a.msg) + L" dpi=" + std::to_wstring(dpi));
            }
            g_dpi = 96;
        }

        TEST_METHOD(ButtonClicksWithParent)
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"DIMEMsgBoxParityParent";
            RegisterClassW(&wc);
            HWND parent = CreateWindowExW(0, wc.lpszClassName, L"parent", WS_OVERLAPPEDWINDOW,
                200, 150, 700, 500, nullptr, nullptr, wc.hInstance, nullptr);
            Assert::IsNotNull(parent);
            g_dpi = 120;
            for (UINT t : { (UINT)MB_YESNOCANCEL, (UINT)MB_YESNO, (UINT)MB_OK }) {
                int count = SettingsWindowLayout::Cpp::MsgBoxButtonsFor(t).count;
                for (int b = 0; b < count; b++) {
                    Compare(parent, L"自訂碼表載入完成。", t, b == 1, { 0, 0, b },
                        L"click type=" + std::to_wstring(t) + L" button=" + std::to_wstring(b));
                    Assert::IsTrue(IsWindowEnabled(parent) != 0, L"parent re-enabled");
                }
            }
            g_dpi = 96;
            DestroyWindow(parent);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
        }
    };
}
