/********************************************************************************
* SettingsWinHostParityTest: the Win32 side of the settings command host, C++
* (SettingsWinHost::Cpp, formerly in SettingsWindow::HandleCommand's
* WindowHost) vs Rust (dime-settings winhost.rs through SettingsWinHost::Rust).
* - WriteBomFile: the bytes written for several texts (empty, embedded NUL,
*   large), and failure on a missing folder.
* - DocumentsPath, LogPixelsY.
* - The custom table editor on a real RichEdit: text round trip, the line
*   count and restored event mask of SetEditorTextMuted (and no EN_CHANGE
*   reaching the parent), EM_LINEINDEX for in- and out-of-range lines, and
*   the selection after EditorFocusAt.
* - The font and colour dialogs confirmed with OK, and all four dialogs
*   closed (cancel), by a driver thread that finds the dialog on the test
*   thread and posts to it.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <richedit.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "../DIMESettings/SettingsWinHost.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace WH = SettingsWinHost;

namespace
{
    std::wstring TempDir()
    {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"DIMEWinHostParity" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    std::vector<char> ReadAll(const std::wstring& path)
    {
        std::vector<char> out;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return out;
        DWORD size = GetFileSize(h, nullptr), got = 0;
        out.resize(size);
        if (size) ReadFile(h, out.data(), size, &got, nullptr);
        CloseHandle(h);
        return out;
    }

    std::atomic<int> g_enChange{ 0 };

    LRESULT CALLBACK ParentProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_COMMAND && HIWORD(w) == EN_CHANGE) g_enChange++;
        return DefWindowProcW(h, m, w, l);
    }

    struct EditorPair
    {
        HWND parent = nullptr;
        HWND a = nullptr, b = nullptr;
        EditorPair()
        {
            LoadLibraryW(L"msftedit.dll");
            WNDCLASSW wc = {};
            wc.lpfnWndProc = ParentProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"DIMEWinHostParityParent";
            RegisterClassW(&wc);
            parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 600, 400,
                nullptr, nullptr, wc.hInstance, nullptr);
            DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL;
            a = CreateWindowExW(0, L"RICHEDIT50W", L"", style, 0, 0, 280, 300, parent, (HMENU)1, wc.hInstance, nullptr);
            b = CreateWindowExW(0, L"RICHEDIT50W", L"", style, 300, 0, 280, 300, parent, (HMENU)2, wc.hInstance, nullptr);
            for (HWND e : { a, b }) {
                SendMessageW(e, EM_EXLIMITTEXT, 0, (LPARAM)(100 * 1024 * 1024));
                SendMessageW(e, EM_SETEVENTMASK, 0, ENM_CHANGE);
            }
        }
        ~EditorPair() { if (parent) DestroyWindow(parent); }
    };

    // Runs `call` while a driver thread waits for a visible dialog on this
    // thread and posts `msg` (WM_COMMAND IDOK, or WM_CLOSE) to it.
    template <typename F>
    bool WithDialog(UINT msg, WPARAM wParam, F call)
    {
        DWORD tid = GetCurrentThreadId();
        std::atomic<bool> done{ false }, posted{ false };
        std::thread driver([&]() {
            for (int i = 0; i < 1500 && !done; i++) {
                HWND found = nullptr;
                EnumThreadWindows(tid, [](HWND h, LPARAM p) -> BOOL {
                    wchar_t cls[32] = {};
                    GetClassNameW(h, cls, 32);
                    if (wcscmp(cls, L"#32770") == 0 && IsWindowVisible(h)) {
                        *(HWND*)p = h;
                        return FALSE;
                    }
                    return TRUE;
                }, (LPARAM)&found);
                if (found) {
                    Sleep(300);   // let the dialog finish initialising
                    PostMessageW(found, msg, wParam, 0);
                    posted = true;
                    return;
                }
                Sleep(20);
            }
        });
        bool r = call();
        done = true;
        driver.join();
        Assert::IsTrue(posted.load(), L"dialog seen");
        return r;
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsWinHostParityTest)
    {
    public:
        TEST_METHOD(WriteBomFile)
        {
            std::wstring dir = TempDir();
            std::wstring big;
            for (int i = 0; i < 70000; i++) big += (wchar_t)(L'\x4E00' + i % 500);
            std::wstring withNul = std::wstring(L"ab") + L'\0' + L"cd\r\n";
            std::vector<std::wstring> texts = { L"", L"a", L"\x5927\x6613 abc\r\nxyz\r\n", withNul, big };
            for (size_t i = 0; i < texts.size(); i++) {
                std::wstring pa = dir + L"\\cpp" + std::to_wstring(i) + L".txt";
                std::wstring pb = dir + L"\\rust" + std::to_wstring(i) + L".txt";
                bool ra = WH::Cpp::WriteBomFile(pa, texts[i]);
                bool rb = WH::Rust::WriteBomFile(pb, texts[i]);
                Assert::IsTrue(ra && rb, L"written");
                std::vector<char> ba = ReadAll(pa), bb = ReadAll(pb);
                Assert::IsTrue(ba == bb, (L"bytes of text " + std::to_wstring(i)).c_str());
                Assert::AreEqual((size_t)(2 + texts[i].size() * 2), ba.size(), L"size");
                DeleteFileW(pa.c_str());
                DeleteFileW(pb.c_str());
            }
            std::wstring missing = dir + L"\\no\\such\\folder\\x.txt";
            Assert::AreEqual(WH::Cpp::WriteBomFile(missing, L"x"), WH::Rust::WriteBomFile(missing, L"x"), L"missing folder");
            Assert::IsFalse(WH::Rust::WriteBomFile(missing, L"x"), L"missing folder fails");
            RemoveDirectoryW(dir.c_str());
        }

        TEST_METHOD(PathsAndDpi)
        {
            Assert::AreEqual(WH::Cpp::DocumentsPath(), WH::Rust::DocumentsPath(), L"documents");
            Assert::IsFalse(WH::Rust::DocumentsPath().empty(), L"documents exists");
            EditorPair ep;
            for (HWND h : { (HWND)nullptr, ep.parent, ep.a })
                Assert::AreEqual(WH::Cpp::LogPixelsY(h), WH::Rust::LogPixelsY(h), L"LOGPIXELSY");
        }

        TEST_METHOD(EditorCalls)
        {
            EditorPair ep;
            std::wstring big;
            for (int i = 0; i < 3000; i++) big += L"\x4E00\x4E01 abcd\r\n";
            std::vector<std::wstring> texts = { L"", L"one line", L"a\r\nb\r\nc", L"a\nb\n\nd\r\n", L"x\ty\r\n\r\n\r\n", big };
            for (const auto& t : texts) {
                g_enChange = 0;
                int la = WH::Cpp::SetEditorTextMuted(ep.a, t);
                int lb = WH::Rust::SetEditorTextMuted(ep.b, t);
                Assert::AreEqual(la, lb, L"line count");
                Assert::AreEqual(0, g_enChange.load(), L"no EN_CHANGE while muted");
                Assert::AreEqual((LRESULT)ENM_CHANGE, SendMessageW(ep.b, EM_GETEVENTMASK, 0, 0), L"mask restored");
                Assert::AreEqual(SendMessageW(ep.a, EM_GETEVENTMASK, 0, 0), SendMessageW(ep.b, EM_GETEVENTMASK, 0, 0), L"masks");
                std::wstring ta = WH::Cpp::EditorText(ep.a), tb = WH::Rust::EditorText(ep.b);
                Assert::IsTrue(ta == tb, L"text");
                Assert::IsTrue(WH::Cpp::EditorText(ep.b) == WH::Rust::EditorText(ep.a), L"text crossed");
                Assert::AreEqual(WH::Cpp::EditorLineCount(ep.a), WH::Rust::EditorLineCount(ep.b), L"EM_GETLINECOUNT");
                for (int line : { -2, -1, 0, 1, 2, 3, la - 1, la, la + 1, 100000 })
                    Assert::AreEqual(WH::Cpp::EditorLineIndex(ep.a, line), WH::Rust::EditorLineIndex(ep.b, line), L"line index");
                for (int cp : { 0, 1, 3, (int)ta.size(), (int)ta.size() + 5, -1 }) {
                    WH::Cpp::EditorFocusAt(ep.a, cp);
                    WH::Rust::EditorFocusAt(ep.b, cp);
                    DWORD sa0 = 0, sa1 = 0, sb0 = 0, sb1 = 0;
                    SendMessageW(ep.a, EM_GETSEL, (WPARAM)&sa0, (LPARAM)&sa1);
                    SendMessageW(ep.b, EM_GETSEL, (WPARAM)&sb0, (LPARAM)&sb1);
                    Assert::IsTrue(sa0 == sb0 && sa1 == sb1, (L"selection after focus at " + std::to_wstring(cp)).c_str());
                    Assert::IsTrue(GetFocus() == ep.b, L"focus");
                }
            }
        }

        TEST_METHOD(Dialogs)
        {
            EditorPair ep;
            HWND owner = ep.parent;
            // Font: OK keeps the initial font; LOGFONT and point size must match.
            for (int i = 0; i < 2; i++) {
                LOGFONTW la = {}, lb = {};
                la.lfHeight = i ? -27 : -16;
                la.lfWeight = i ? FW_BOLD : FW_NORMAL;
                la.lfItalic = (BYTE)i;
                la.lfCharSet = DEFAULT_CHARSET;
                wcscpy_s(la.lfFaceName, i ? L"Segoe UI" : L"Microsoft JhengHei");
                lb = la;
                int pa = -1, pb = -1;
                bool ra = WithDialog(WM_COMMAND, IDOK, [&]() { return WH::Cpp::PickFont(owner, la, pa); });
                bool rb = WithDialog(WM_COMMAND, IDOK, [&]() { return WH::Rust::PickFont(owner, lb, pb); });
                Assert::IsTrue(ra && rb, L"font OK");
                Assert::AreEqual(pa, pb, L"point size");
                Assert::IsTrue(memcmp(&la, &lb, offsetof(LOGFONTW, lfFaceName)) == 0, L"LOGFONT fields");
                Assert::AreEqual(std::wstring(la.lfFaceName), std::wstring(lb.lfFaceName), L"face");
            }
            // Colour: OK returns the initial colour.
            for (COLORREF c : { RGB(0, 120, 215), RGB(250, 250, 250) }) {
                COLORREF ca = c, cb = c;
                bool ra = WithDialog(WM_COMMAND, IDOK, [&]() { return WH::Cpp::PickColor(owner, ca); });
                bool rb = WithDialog(WM_COMMAND, IDOK, [&]() { return WH::Rust::PickColor(owner, cb); });
                Assert::IsTrue(ra && rb, L"colour OK");
                Assert::AreEqual((DWORD)ca, (DWORD)cb, L"colour");
            }
            // Cancel (WM_CLOSE): false and nothing changed.
            {
                LOGFONTW la = {}, lb = {};
                wcscpy_s(la.lfFaceName, L"Segoe UI");
                la.lfHeight = -16;
                lb = la;
                int pa = 7, pb = 7;
                bool ra = WithDialog(WM_CLOSE, 0, [&]() { return WH::Cpp::PickFont(owner, la, pa); });
                bool rb = WithDialog(WM_CLOSE, 0, [&]() { return WH::Rust::PickFont(owner, lb, pb); });
                Assert::IsFalse(ra || rb, L"font cancel");
                Assert::IsTrue(pa == 7 && pb == 7, L"point size untouched");
                COLORREF ca = RGB(1, 2, 3), cb = ca;
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Cpp::PickColor(owner, ca); }), L"colour cancel C++");
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Rust::PickColor(owner, cb); }), L"colour cancel Rust");
                Assert::IsTrue(ca == RGB(1, 2, 3) && cb == ca, L"colour untouched");
            }
            for (int filter : { 0, 1 }) {
                std::wstring pa = L"keep", pb = L"keep";
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Cpp::OpenFileDialog(owner, filter, pa); }), L"open cancel C++");
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Rust::OpenFileDialog(owner, filter, pb); }), L"open cancel Rust");
                Assert::IsTrue(pa == L"keep" && pb == L"keep", L"open path untouched");
            }
            {
                std::wstring pa = L"C:\\x\\DIME-custom.txt", pb = pa;
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Cpp::SaveFileDialog(owner, pa); }), L"save cancel C++");
                Assert::IsFalse(WithDialog(WM_CLOSE, 0, [&]() { return WH::Rust::SaveFileDialog(owner, pb); }), L"save cancel Rust");
                Assert::IsTrue(pa == pb, L"save path untouched");
            }
        }
    };
}
