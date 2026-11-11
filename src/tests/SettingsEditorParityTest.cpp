/********************************************************************************
* SettingsEditorParityTest: live validation of the custom table RichEdit, C++
* (SettingsEditor::Cpp, lifted out of SettingsWindow.cpp) vs Rust
* (dime-settings editor.rs via dime_settings_editor_*), on real RICHEDIT50W
* controls. Pairs of editors get the same random text, scroll position, and
* selection; after the C++ / Rust viewport pass every character's colour, the
* selection, the visible range, and the restored event mask must match. The
* subclass procedures get the same message sequence (keys, paste, scroll,
* wheel, focus) and must post / forward the same messages to the parent and
* leave the same selection.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <tom.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsEditor.h"
#include "dime_ffi.h"

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SE = SettingsEditor;

namespace
{
    HMODULE g_msftedit = nullptr;
    std::vector<std::pair<HWND, UINT>> g_parentLog;   // (parent, message)

    LRESULT CALLBACK ParentProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == SE::kMsgRevalidate || m == SE::kMsgViewport || m == WM_MOUSEWHEEL)
            g_parentLog.push_back({ h, m });
        return DefWindowProcW(h, m, w, l);
    }

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    const wchar_t* kParentClass = L"DIMEEditorParityParent";

    HWND MakeParent()
    {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = ParentProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kParentClass;
            RegisterClassW(&wc);
            registered = true;
        }
        return CreateWindowExW(0, kParentClass, L"", WS_POPUP, -3000, -3000, 700, 500, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
    }

    HWND MakeEditor(HWND parent, int w, int h)
    {
        HWND re = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(re, EM_EXLIMITTEXT, 0, (LPARAM)(16 * 1024 * 1024));
        return re;
    }

    std::wstring RandomTable(std::mt19937& rng, int lines)
    {
        static const wchar_t* keys[] = { L"abc", L"a", L"roc", L"x,", L"ab;", L"zzzzzzzz", L"A?", L"\x3105\x3106", L"", L"b c" };
        static const wchar_t* vals[] = { L"中", L"語言", L"x", L"", L" 中", L"中 文" };
        std::wstring t;
        for (int i = 0; i < lines; i++) {
            t += keys[rng() % _countof(keys)];
            t += (rng() % 4) ? L" " : L"\t";
            t += vals[rng() % _countof(vals)];
            if (i + 1 < lines) t += (rng() % 3) ? L"\r\n" : L"\n";
        }
        return t;
    }

    std::vector<LONG> Colors(HWND re)
    {
        std::vector<LONG> out;
        IUnknown* unk = nullptr;
        SendMessageW(re, EM_GETOLEINTERFACE, 0, (LPARAM)&unk);
        if (!unk) return out;
        ITextDocument* doc = nullptr;
        unk->QueryInterface(__uuidof(ITextDocument), (void**)&doc);
        unk->Release();
        if (!doc) return out;
        LONG len = GetWindowTextLengthW(re);
        for (LONG i = 0; i < len; i++) {
            ITextRange* r = nullptr;
            LONG c = -1;
            if (SUCCEEDED(doc->Range(i, i + 1, &r)) && r) {
                ITextFont* f = nullptr;
                if (SUCCEEDED(r->GetFont(&f)) && f) { f->GetForeColor(&c); f->Release(); }
                r->Release();
            }
            out.push_back(c);
        }
        doc->Release();
        return out;
    }

    CHARRANGE Sel(HWND re)
    {
        CHARRANGE cr = {};
        SendMessageW(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        return cr;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsEditorParityTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Load)
        {
            g_msftedit = LoadLibraryW(L"Msftedit.dll");
        }

        TEST_CLASS_CLEANUP(Unload)
        {
            if (g_msftedit) FreeLibrary(g_msftedit);
            g_msftedit = nullptr;
        }

        TEST_METHOD(ViewportPass)
        {
            Assert::IsNotNull(g_msftedit, L"Msftedit.dll");
            std::mt19937 rng(8);
            HWND parent = MakeParent();
            int painted = 0;
            for (int iter = 0; iter < 60; iter++) {
                int w = 300 + (int)(rng() % 400), h = 120 + (int)(rng() % 300);
                HWND a = MakeEditor(parent, w, h), b = MakeEditor(parent, w, h);
                std::wstring text = RandomTable(rng, 1 + (int)(rng() % 120));
                SetWindowTextW(a, text.c_str());
                SetWindowTextW(b, text.c_str());
                int scroll = (int)(rng() % 60);
                SendMessageW(a, EM_LINESCROLL, 0, scroll);
                SendMessageW(b, EM_LINESCROLL, 0, scroll);
                LONG len = GetWindowTextLengthW(a);
                CHARRANGE sel = { (LONG)(rng() % (len + 1)), 0 };
                sel.cpMax = sel.cpMin + (LONG)(rng() % 5);
                SendMessageW(a, EM_EXSETSEL, 0, (LPARAM)&sel);
                SendMessageW(b, EM_EXSETSEL, 0, (LPARAM)&sel);
                CHARRANGE before = Sel(a);   // RichEdit clamps / normalises what we asked for
                Assert::IsTrue(before.cpMin == Sel(b).cpMin && before.cpMax == Sel(b).cpMax, L"same start selection");
                SendMessageW(a, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SCROLL);
                SendMessageW(b, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SCROLL);

                int fa = -1, la = -1, fb = -2, lb = -2;
                SE::Cpp::ViewportLineRange(a, &fa, &la);
                dime_settings_editor_viewport(b, &fb, &lb);
                Assert::AreEqual(fa, fb, L"first visible");
                Assert::AreEqual(la, lb, L"last visible");

                SE::EditorContext ctx = { (int)(rng() % 4), (UINT)(rng() % 7), (int)(rng() % 2), nullptr };
                SE::Cpp::ValidateViewport(a, ctx);
                dime_settings_editor_validate_viewport(b, ctx.mode, ctx.maxCodes, ctx.isDark, nullptr, nullptr);

                std::vector<LONG> ca = Colors(a), cb = Colors(b);
                Assert::AreEqual(ca.size(), cb.size(), L"text length");
                for (size_t i = 0; i < ca.size(); i++) {
                    if (ca[i] != cb[i])
                        Assert::Fail((L"iter " + std::to_wstring(iter) + L" char " + std::to_wstring(i)
                            + L": C++ " + std::to_wstring(ca[i]) + L" Rust " + std::to_wstring(cb[i])).c_str());
                    if (ca[i] != ca[0]) painted++;
                }
                CHARRANGE sa = Sel(a), sb = Sel(b);
                Assert::IsTrue(sa.cpMin == before.cpMin && sa.cpMax == before.cpMax, L"C++ kept the selection");
                Assert::IsTrue(sb.cpMin == before.cpMin && sb.cpMax == before.cpMax, L"Rust kept the selection");
                Assert::AreEqual((LRESULT)(ENM_CHANGE | ENM_SCROLL), SendMessageW(b, EM_GETEVENTMASK, 0, 0), L"mask restored");
                DestroyWindow(a);
                DestroyWindow(b);
            }
            DestroyWindow(parent);
            Assert::IsTrue(painted > 100, L"some characters got error colours");
        }

        TEST_METHOD(SubclassProc)
        {
            Assert::IsNotNull(g_msftedit, L"Msftedit.dll");
            HWND pa = MakeParent(), pb = MakeParent();
            HWND a = MakeEditor(pa, 400, 200), b = MakeEditor(pb, 400, 200);
            std::wstring text = L"abc 中\r\nroc 語\r\nxyz 文字\r\n0123456789abcdef 長\r\n";
            SetWindowTextW(a, text.c_str());
            SetWindowTextW(b, text.c_str());
            SetWindowSubclass(a, &SE::Cpp::SubclassProc, 0, 0);
            SetWindowSubclass(b, reinterpret_cast<SUBCLASSPROC>(dime_settings_editor_subclass_proc()), 0, 0);

            struct Step { UINT msg; WPARAM wp; LPARAM lp; LONG selMin; LONG selMax; };
            Step steps[] = {
                { WM_KEYDOWN, VK_RETURN, 0, 3, 3 },
                { WM_KEYDOWN, VK_SPACE, 0, 0, 0 },
                { WM_KEYDOWN, 'A', 0, 1, 1 },
                { WM_KEYDOWN, VK_DELETE, 0, 2, 7 },
                { WM_KEYDOWN, VK_DELETE, 0, 2, 20 },
                { WM_KEYDOWN, VK_BACK, 0, 0, 11 },
                { WM_KEYDOWN, VK_BACK, 0, 5, 15 },
                { WM_VSCROLL, SB_LINEDOWN, 0, 4, 4 },
                { WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)(SHORT)-120), MAKELPARAM(10, 10), 4, 4 },
                { WM_SETFOCUS, 0, 0, 6, 9 },
                { WM_KEYDOWN, VK_RETURN, 0, 8, 8 },
            };
            for (size_t i = 0; i < _countof(steps); i++) {
                const Step& st = steps[i];
                CHARRANGE cr = { st.selMin, st.selMax };
                SendMessageW(a, EM_EXSETSEL, 0, (LPARAM)&cr);
                SendMessageW(b, EM_EXSETSEL, 0, (LPARAM)&cr);
                g_parentLog.clear();
                SendMessageW(a, st.msg, st.wp, st.lp);
                Pump();
                std::vector<UINT> la;
                for (auto& e : g_parentLog) if (e.first == pa) la.push_back(e.second);
                g_parentLog.clear();
                SendMessageW(b, st.msg, st.wp, st.lp);
                Pump();
                std::vector<UINT> lb;
                for (auto& e : g_parentLog) if (e.first == pb) lb.push_back(e.second);
                std::wstring what = L"step " + std::to_wstring(i);
                Assert::IsTrue(la == lb, (what + L": parent messages").c_str());
                CHARRANGE sa = Sel(a), sb = Sel(b);
                Assert::IsTrue(sa.cpMin == sb.cpMin && sa.cpMax == sb.cpMax, (what + L": selection").c_str());
                Assert::AreEqual(GetWindowTextLengthW(a), GetWindowTextLengthW(b), (what + L": text").c_str());
            }
            // The focus step resets the selection to the start.
            CHARRANGE cr = { 6, 9 };
            SendMessageW(b, EM_EXSETSEL, 0, (LPARAM)&cr);
            SendMessageW(b, WM_SETFOCUS, 0, 0);
            CHARRANGE sb = Sel(b);
            Assert::IsTrue(sb.cpMin == 0 && sb.cpMax == 0, L"focus resets the caret");
            DestroyWindow(pa);
            DestroyWindow(pb);
        }
    };
}
