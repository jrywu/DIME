/********************************************************************************
* SettingsChildrenParityTest: creating a settings page's child windows, C++
* (SettingsChildren::Cpp::Create, formerly SettingsWindow::CreateControls /
* AddControl / SetupRichEditor / LoadRichEditor and the wheel forwarder) vs
* Rust (dime-settings children.rs through SettingsChildren::CreateRust). Both
* create the specs of every shipped page into their own parent window, light
* and dark, with the custom table file missing, empty, UTF-8, UTF-16 LE / BE,
* undecodable, or a directory. The Host calls, and for every child its class,
* styles, caption, rectangle, id, font, wheel forwarding, and for the RichEdit
* its limit, event mask, colours, default format, text and line count must
* match, and so must the theme SetWindowTheme recorded and the child pixels
* (PrintWindow; the test process does not opt into dark mode, so the theme
* itself does not show in the pixels).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <richedit.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsChildren.h"
#include "../DIMESettings/SettingsPageLayout.h"
#include "../DIMESettings/SettingsEditor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SCH = SettingsChildren;
namespace SC = SettingsControls;
namespace SWL = SettingsWindowLayout;

namespace
{
    const wchar_t* kParentClass = L"DIMEChildrenParityParent";

    struct ParentLog {
        int wheel = 0;
        std::vector<UINT> posted;   // editor notifications
    };

    LRESULT CALLBACK ParentProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        ParentLog* log = (ParentLog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (log) {
            if (m == WM_MOUSEWHEEL) { log->wheel++; return 0; }
            if (m == SettingsEditor::kMsgRevalidate || m == SettingsEditor::kMsgViewport) log->posted.push_back(m);
        }
        return DefWindowProcW(h, m, w, l);
    }

    HWND MakeParent(ParentLog* log)
    {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = ParentProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kParentClass;
            wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
            RegisterClassW(&wc);
            registered = true;
        }
        // Both parents at the same screen position: the editor subclass
        // compares the cursor with its window rectangle.
        HWND h = CreateWindowExW(0, kParentClass, L"", WS_POPUP | WS_CLIPCHILDREN, -4000, -4000, 1400, 3000,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)log);
        return h;
    }

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Records Host calls; Added handles become indexes into the parent's children.
    class Recorder : public SCH::Host {
    public:
        std::wstring log;
        std::wstring path;
        std::vector<HWND> added;
        explicit Recorder(const std::wstring& p) : path(p) {}

        void Added(int ctrlId, HWND h, int x, int y, int w, int ht, int visH) override
        {
            added.push_back(h);
            log += L"Added " + std::to_wstring(ctrlId) + (h ? L" hwnd " : L" null ") + std::to_wstring(x) + L","
                + std::to_wstring(y) + L"," + std::to_wstring(w) + L"," + std::to_wstring(ht) + L" visH "
                + std::to_wstring(visH) + L"\n";
        }
        std::wstring EditorPath() override
        {
            log += L"EditorPath\n";
            return path;
        }
        void EditorReady(HWND hRE, int lineCount) override
        {
            int index = -1;
            for (size_t i = 0; i < added.size(); i++)
                if (added[i] == hRE) index = (int)i;
            log += L"EditorReady #" + std::to_wstring(index) + L" lines " + std::to_wstring(lineCount)
                + L" textLen " + std::to_wstring(GetWindowTextLengthW(hRE)) + L"\n";
        }
    };

    std::vector<HWND> Children(HWND parent)
    {
        std::vector<HWND> out;
        for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) out.push_back(c);
        return out;
    }

    std::wstring Pixels(HWND h)
    {
        RECT rc; GetClientRect(h, &rc);
        int w = rc.right, ht = rc.bottom;
        if (w <= 0 || ht <= 0) return L"empty";
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -ht;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bmp);
        PrintWindow(h, dc, PW_CLIENTONLY);
        unsigned long long hash = 1469598103934665603ull;
        const unsigned char* p = (const unsigned char*)bits;
        for (size_t i = 0; i < (size_t)w * ht * 4; i++) { hash ^= p[i]; hash *= 1099511628211ull; }
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return std::to_wstring(hash);
    }

    std::wstring Format(HWND re, DWORD scf)
    {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        SendMessageW(re, EM_GETCHARFORMAT, scf, (LPARAM)&cf);
        return std::wstring(L"face=") + cf.szFaceName + L" h=" + std::to_wstring(cf.yHeight) + L" w="
            + std::to_wstring(cf.wWeight) + L" color=" + std::to_wstring(cf.crTextColor) + L" mask="
            + std::to_wstring(cf.dwMask) + L" fx=" + std::to_wstring(cf.dwEffects);
    }

    // SetWindowTheme keeps the sub-app name as a window property whose value
    // is a local atom (no API reads it back); list those names.
    BOOL CALLBACK ThemePropProc(HWND, LPWSTR, HANDLE data, ULONG_PTR param)
    {
        ULONG_PTR v = (ULONG_PTR)data;
        wchar_t name[128] = {};
        if (v >= 0xC000 && v <= 0xFFFF && GetAtomNameW((ATOM)v, name, 128))
            *(std::wstring*)param += std::wstring(name) + L";";
        return TRUE;
    }

    // Everything observable about one child. Rectangles are parent-relative.
    std::wstring Describe(HWND parent, HWND c, ParentLog& log)
    {
        wchar_t cls[64] = {};
        GetClassNameW(c, cls, 64);
        int len = GetWindowTextLengthW(c);
        std::wstring text(len + 1, L'\0');
        GetWindowTextW(c, text.data(), len + 1);
        text.resize(len);
        RECT rc; GetWindowRect(c, &rc);
        MapWindowPoints(nullptr, parent, (POINT*)&rc, 2);
        std::wstring d = std::wstring(cls) + L" id=" + std::to_wstring(GetDlgCtrlID(c))
            + L" st=" + std::to_wstring(GetWindowLongW(c, GWL_STYLE)) + L" ex=" + std::to_wstring(GetWindowLongW(c, GWL_EXSTYLE))
            + L" rc=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," + std::to_wstring(rc.right) + L"," + std::to_wstring(rc.bottom)
            + L" font=" + std::to_wstring((LONG_PTR)SendMessageW(c, WM_GETFONT, 0, 0))
            + L" text=[" + (len > 200 ? text.substr(0, 200) + L"..." + std::to_wstring(len) : text) + L"]";
        if (wcscmp(cls, L"RICHEDIT50W") == 0) {
            d += L" limit=" + std::to_wstring(SendMessageW(c, EM_GETLIMITTEXT, 0, 0))
                + L" mask=" + std::to_wstring(SendMessageW(c, EM_GETEVENTMASK, 0, 0))
                + L" lines=" + std::to_wstring(SendMessageW(c, EM_GETLINECOUNT, 0, 0))
                + L" all{" + Format(c, SCF_SELECTION) + L"} def{" + Format(c, SCF_DEFAULT) + L"}";
            // EM_SETBKGNDCOLOR returns the previous colour; put it back.
            LRESULT bk = SendMessageW(c, EM_SETBKGNDCOLOR, 0, RGB(1, 2, 3));
            SendMessageW(c, EM_SETBKGNDCOLOR, 0, bk);
            d += L" bk=" + std::to_wstring(bk);
        }
        // The wheel goes to the parent through the forwarder (or the editor
        // subclass, which forwards it when the cursor is outside).
        int before = log.wheel;
        SendMessageW(c, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(10, 10));
        d += L" wheel=" + std::to_wstring(log.wheel - before);
        std::wstring theme;
        EnumPropsExW(c, ThemePropProc, (LPARAM)&theme);
        d += L" theme=" + theme;
        d += L" px=" + Pixels(c);
        return d;
    }

    void WriteFile(const std::wstring& path, const std::string& bytes)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE);
        DWORD wrote = 0;
        if (!bytes.empty()) ::WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
        CloseHandle(h);
    }

    std::string Utf16(const std::wstring& s, bool be)
    {
        std::string out = be ? std::string("\xFE\xFF", 2) : std::string("\xFF\xFE", 2);
        for (wchar_t ch : s) {
            char lo = (char)(ch & 0xFF), hi = (char)(ch >> 8);
            if (be) { out += hi; out += lo; } else { out += lo; out += hi; }
        }
        return out;
    }

    // The custom table file cases. The name is the case label.
    std::vector<std::pair<std::wstring, std::wstring>> TableFiles(const std::wstring& dir)
    {
        std::wstring table = L"abc 中\r\nab 語言\r\nx,y 錯\r\n";
        for (int i = 0; i < 60; i++) table += L"a" + std::to_wstring(i) + L" 字\r\n";
        std::vector<std::pair<std::wstring, std::wstring>> out;
        out.push_back({ L"missing", dir + L"\\missing.txt" });
        WriteFile(dir + L"\\empty.txt", "");
        out.push_back({ L"empty", dir + L"\\empty.txt" });
        WriteFile(dir + L"\\utf8.txt", "\xEF\xBB\xBF" "abc \xE4\xB8\xAD\r\nzz \xE8\xAA\x9E\r\n");
        out.push_back({ L"utf8 bom", dir + L"\\utf8.txt" });
        WriteFile(dir + L"\\plain.txt", "abc x\nab y\n");
        out.push_back({ L"utf8", dir + L"\\plain.txt" });
        WriteFile(dir + L"\\le.txt", Utf16(table, false));
        out.push_back({ L"utf16 le", dir + L"\\le.txt" });
        WriteFile(dir + L"\\be.txt", Utf16(L"ab 中\r\n", true));
        out.push_back({ L"utf16 be", dir + L"\\be.txt" });
        WriteFile(dir + L"\\bomonly.txt", "\xEF\xBB\xBF");
        out.push_back({ L"bom only", dir + L"\\bomonly.txt" });
        CreateDirectoryW((dir + L"\\folder.txt").c_str(), nullptr);
        out.push_back({ L"directory", dir + L"\\folder.txt" });
        return out;
    }

    struct Page {
        std::wstring name;
        std::vector<SC::ControlSpec> specs;
    };

    std::vector<Page> ShippedPageSpecs()
    {
        int imeCount = 0, ltCount = 0;
        const LayoutCard* ime = GetIMEPageLayout(&imeCount);
        const LayoutCard* lt = GetLoadTablePageLayout(&ltCount);
        std::vector<Page> out;
        for (int sel : { 0, 1, 2, 3, 5 }) {
            for (int nav = 0; nav < 2; nav++) {
                for (int card = 0; card < (nav ? imeCount : 1); card++) {
                    SWL::PlanInput in = {};
                    in.dpi = 120;
                    in.selectedModeIndex = sel;
                    in.navLevel = nav;
                    in.currentCardIndex = card;
                    in.modeMask = sel < 4 ? (1 << sel) : 0xF;
                    in.colorMode = 3;
                    in.gridExpanded = 1;
                    in.previewH = 200;
                    for (bool& e : in.expanded) e = true;
                    Page p;
                    p.name = L"sel " + std::to_wstring(sel) + L" nav " + std::to_wstring(nav) + L" card " + std::to_wstring(card);
                    p.specs = SC::Cpp::BuildSpecs(in, 700, ime, imeCount, lt, ltCount);
                    if (!p.specs.empty()) out.push_back(p);
                }
            }
        }
        return out;
    }

    bool HasEditor(const std::vector<SC::ControlSpec>& specs)
    {
        for (const auto& s : specs)
            if (s.role == SC::ROLE_RICH_EDITOR) return true;
        return false;
    }

    // One side: create, describe every child, destroy.
    std::vector<std::wstring> Run(bool rust, const std::vector<SC::ControlSpec>& specs, const std::wstring& path,
        bool dark, HFONT font, std::wstring& hostLog)
    {
        ParentLog plog;
        HWND parent = MakeParent(&plog);
        SCH::Context ctx = { parent, GetModuleHandleW(nullptr), font, dark ? 1 : 0, RGB(43, 43, 43), RGB(240, 240, 240) };
        Recorder rec(path);
        if (rust) SCH::CreateRust(rec, ctx, specs);
        else SCH::Cpp::Create(rec, ctx, specs);
        Pump();
        hostLog = rec.log;
        std::vector<std::wstring> out;
        for (HWND c : Children(parent)) out.push_back(Describe(parent, c, plog));
        for (size_t i = 0; i < rec.added.size(); i++) {
            int index = -1;
            std::vector<HWND> kids = Children(parent);
            for (size_t k = 0; k < kids.size(); k++)
                if (kids[k] == rec.added[i]) index = (int)k;
            out.push_back(L"added " + std::to_wstring(i) + L" -> child " + std::to_wstring(index));
        }
        Pump();
        std::wstring posted;
        for (UINT m : plog.posted) posted += std::to_wstring(m) + L" ";
        out.push_back(L"posted " + posted);
        SetWindowLongPtrW(parent, GWLP_USERDATA, 0);
        DestroyWindow(parent);
        Pump();
        return out;
    }

    void Compare(const std::vector<SC::ControlSpec>& specs, const std::wstring& path, bool dark, HFONT font,
        const std::wstring& what, int& checked)
    {
        std::wstring logA, logB;
        std::vector<std::wstring> a = Run(false, specs, path, dark, font, logA);
        std::vector<std::wstring> b = Run(true, specs, path, dark, font, logB);
        if (logA != logB)
            Assert::Fail((what + L": host calls\nC++:\n" + logA + L"Rust:\n" + logB).c_str());
        Assert::AreEqual(a.size(), b.size(), (what + L": child count").c_str());
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i] != b[i])
                Assert::Fail((what + L" child " + std::to_wstring(i) + L"\nC++:  " + a[i] + L"\nRust: " + b[i]).c_str());
        }
        checked += (int)a.size();
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsChildrenParityTest)
    {
    public:
        TEST_METHOD(ShippedPages)
        {
            wchar_t base[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, base);
            std::wstring dir = std::wstring(base) + L"DIMEChildrenParity" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(dir.c_str(), nullptr);
            std::vector<std::pair<std::wstring, std::wstring>> files = TableFiles(dir);

            HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                L"Microsoft JhengHei UI");
            int checked = 0, editorPages = 0;
            for (const Page& p : ShippedPageSpecs()) {
                bool editor = HasEditor(p.specs);
                if (editor) editorPages++;
                for (int dark = 0; dark < 2; dark++) {
                    // Only pages with the editor read the table file.
                    if (editor) {
                        for (const auto& f : files)
                            Compare(p.specs, f.second, dark != 0, font,
                                p.name + (dark ? L" dark " : L" light ") + f.first, checked);
                    } else {
                        Compare(p.specs, files[0].second, dark != 0, font, p.name + (dark ? L" dark" : L" light"), checked);
                    }
                }
            }
            DeleteObject(font);
            for (const auto& f : files) {
                DeleteFileW(f.second.c_str());
                RemoveDirectoryW(f.second.c_str());
            }
            RemoveDirectoryW(dir.c_str());
            Assert::IsTrue(editorPages > 0, L"no page with the custom table editor");
            Assert::IsTrue(checked > 500, (L"children checked: " + std::to_wstring(checked)).c_str());
        }

        TEST_METHOD(EdgeSpecs)
        {
            // Random plain specs: every class but the RichEdit, null and
            // empty captions, random styles, rectangles, visH, font and dark
            // theme flags.
            std::mt19937 rng(9);
            HFONT font = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            int checked = 0;
            for (int iter = 0; iter < 60; iter++) {
                std::vector<SC::ControlSpec> specs;
                int n = 1 + (int)(rng() % 8);
                for (int i = 0; i < n; i++) {
                    SC::ControlSpec s = {};
                    s.ctrlId = (int)(rng() % 210);
                    s.menuId = 3000 + (int)(rng() % 300);
                    s.role = (rng() % 4 == 0) ? SC::ROLE_SAVE_BUTTON : SC::ROLE_PLAIN;
                    s.controlClass = (int)(rng() % 5);
                    if (s.controlClass == SC::CLS_RICHEDIT) s.controlClass = SC::CLS_STATIC;
                    s.style = WS_CHILD | ((rng() & 1) ? WS_VISIBLE : 0) | (DWORD)(rng() % 16);
                    s.exStyle = (rng() & 1) ? WS_EX_CLIENTEDGE : 0;
                    int t = (int)(rng() % 3);
                    s.hasText = t != 0;
                    s.text = t == 1 ? L"" : t == 2 ? L"文字 " + std::to_wstring(i) : L"";
                    s.create = { (LONG)(rng() % 400), (LONG)(rng() % 900), (LONG)(rng() % 300), (LONG)(rng() % 80) };
                    s.add = { (LONG)(rng() % 400), (LONG)(rng() % 900), (LONG)(rng() % 300), (LONG)(rng() % 80) };
                    s.visH = (rng() & 1) ? -1 : (int)(rng() % 40);
                    s.setFont = (rng() & 1) != 0;
                    s.darkTheme = (int)(rng() % 3);
                    specs.push_back(s);
                }
                Compare(specs, L"", (rng() & 1) != 0, font, L"edge " + std::to_wstring(iter), checked);
            }
            DeleteObject(font);
            Assert::IsTrue(checked > 100);
        }
    };
}
