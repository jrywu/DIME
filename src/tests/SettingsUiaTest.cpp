/********************************************************************************
* SettingsUiaTest: the settings window through UI Automation, C++
* (SettingsWindow) vs Rust (dime-settings window.rs), both over the
* recording SettingsTestApp::FakeApp (no CConfig, no real %APPDATA%).
* A UIA client on a worker thread (the window thread keeps pumping) walks
* the raw view of each window for every page (each mode's card list, every
* card's sub-page, the load-table page) and records control type, name,
* automation id, class, rectangle, enabled / offscreen / focusable, and the
* toggle, value and selection state; the two trees must match. It then
* drives the page through UIA patterns (Toggle on check boxes, Select on
* combo box items, SetValue on the edit box) on both windows and compares
* the app calls and the trees afterwards. Replaces the old dialog's
* SettingsDialogIntegrationTest (phase 5b, docs/RUST_MIGRATION_PLAN.md).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <UIAutomation.h>
#include <atlbase.h>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../DIMESettings/SettingsWindow.h"
#include "../DIMESettings/SettingsPageLayout.h"
#include "dime_ffi.h"
#include "SettingsFakeApp.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace SettingsTestApp;

namespace
{
    const wchar_t kTestKey[] = L"SOFTWARE\\DIME\\SettingsUIUiaTest";

    // Run f on a UIA client thread (MTA) while this thread pumps messages:
    // UIA reaches the windows with cross-thread messages.
    void OnUiaThread(const std::function<void(IUIAutomation*)>& f)
    {
        bool done = false;
        std::wstring error;
        std::thread t([&] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            {
                CComPtr<IUIAutomation> uia;
                if (FAILED(uia.CoCreateInstance(__uuidof(CUIAutomation)))) error = L"no UIA";
                else f(uia);
            }
            CoUninitialize();
            done = true;
        });
        HANDLE h = t.native_handle();
        for (;;) {
            DWORD r = MsgWaitForMultipleObjects(1, &h, FALSE, 30000, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) break;
            if (r == WAIT_TIMEOUT) { error = L"UIA thread timed out"; break; }
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        t.join();
        Assert::IsTrue(error.empty(), error.c_str());
    }

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    std::wstring Bstr(BSTR b)
    {
        std::wstring s = b ? b : L"";
        SysFreeString(b);
        return s;
    }

    // One element: everything but runtime ids and handles.
    std::wstring Describe(IUIAutomationElement* e, const RECT& origin)
    {
        CONTROLTYPEID type = 0;
        e->get_CurrentControlType(&type);
        BSTR name = nullptr, id = nullptr, cls = nullptr;
        e->get_CurrentName(&name);
        e->get_CurrentAutomationId(&id);
        e->get_CurrentClassName(&cls);
        RECT rc = {};
        e->get_CurrentBoundingRectangle(&rc);
        BOOL enabled = FALSE, offscreen = FALSE, focusable = FALSE;
        e->get_CurrentIsEnabled(&enabled);
        e->get_CurrentIsOffscreen(&offscreen);
        e->get_CurrentIsKeyboardFocusable(&focusable);
        std::wstring clsName = Bstr(cls);
        if (clsName.size() > 2 && clsName[clsName.size() - 2] == L'_') clsName.resize(clsName.size() - 2);
        std::wstring d = L"type=" + std::to_wstring(type) + L" name=[" + Bstr(name) + L"] id=" + Bstr(id) + L" cls=" + clsName
            + L" rc=" + std::to_wstring(rc.left - origin.left) + L"," + std::to_wstring(rc.top - origin.top) + L","
            + std::to_wstring(rc.right - rc.left) + L"," + std::to_wstring(rc.bottom - rc.top)
            + L" en=" + std::to_wstring(enabled) + L" off=" + std::to_wstring(offscreen) + L" focus=" + std::to_wstring(focusable);
        CComPtr<IUIAutomationTogglePattern> toggle;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle) {
            ToggleState st = ToggleState_Indeterminate;
            toggle->get_CurrentToggleState(&st);
            d += L" toggle=" + std::to_wstring(st);
        }
        CComPtr<IUIAutomationValuePattern> value;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
            BSTR v = nullptr;
            value->get_CurrentValue(&v);
            std::wstring vs = Bstr(v);
            d += L" value=[" + vs.substr(0, 80) + L"]";
        }
        CComPtr<IUIAutomationSelectionItemPattern> item;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&item))) && item) {
            BOOL sel = FALSE;
            item->get_CurrentIsSelected(&sel);
            d += L" selected=" + std::to_wstring(sel);
        }
        return d;
    }

    void Walk(IUIAutomationTreeWalker* w, IUIAutomationElement* e, const RECT& origin, int depth,
        std::vector<std::wstring>& out, std::vector<CComPtr<IUIAutomationElement>>* all)
    {
        out.push_back(std::wstring(depth * 2, L' ') + Describe(e, origin));
        if (all) all->push_back(e);
        CComPtr<IUIAutomationElement> child;
        w->GetFirstChildElement(e, &child);
        while (child) {
            // Owned popups of the input method installed on the machine
            // (DIME.NotifyWindow etc. appear once a window gets focus) are
            // not part of the settings window.
            BSTR cls = nullptr;
            child->get_CurrentClassName(&cls);
            std::wstring c = Bstr(cls);
            if (c.rfind(L"DIME.", 0) != 0)
                Walk(w, child, origin, depth + 1, out, all);
            CComPtr<IUIAutomationElement> next;
            w->GetNextSiblingElement(child, &next);
            child = next;
        }
    }

    std::vector<std::wstring> Tree(IUIAutomation* uia, HWND main, std::vector<CComPtr<IUIAutomationElement>>* all = nullptr)
    {
        std::vector<std::wstring> out;
        CComPtr<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(main, &root)) || !root) {
            out.push_back(L"no element");
            return out;
        }
        RECT origin = {};
        root->get_CurrentBoundingRectangle(&origin);
        CComPtr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        Walk(walker, root, origin, 0, out, all);
        return out;
    }

    struct Side {
        bool rust;
        FakeApp app;
        HWND main = nullptr;
        size_t logPos = 0;

        void Create(const std::wstring& dir)
        {
            app.dir = dir;
            std::wstring sfx = rust ? L"_R" : L"_C";
            std::wstring mainCls = L"DIMEUiaMain" + sfx, contentCls = L"DIMEUiaContent" + sfx;
            const wchar_t* caption = L"DIME 設定 v1.3.0";
            if (rust) {
                main = (HWND)dime_settings_window_create(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                    static_cast<SettingsApp::Host*>(&app), GetModuleHandleW(nullptr), 0, (const uint16_t*)caption,
                    (const uint16_t*)mainCls.c_str(), (const uint16_t*)contentCls.c_str(), (const uint16_t*)kTestKey,
                    (const uint16_t*)L"R", 10);
            } else {
                main = SettingsWindow::CreateForTest(&app, GetModuleHandleW(nullptr), IME_MODE::IME_MODE_DAYI, caption,
                    mainCls.c_str(), contentCls.c_str(), kTestKey, L"C");
            }
            Assert::IsNotNull(main);
            // UIA leaves out the children of hidden windows: show the window
            // fully transparent (layered, alpha 0) and without activating it.
            SetWindowLongW(main, GWL_EXSTYLE, GetWindowLongW(main, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
            SetLayeredWindowAttributes(main, 0, 0, LWA_ALPHA);
            SetWindowPos(main, nullptr, 50, 50, 1100, 900, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            Pump();
        }

        void Navigate(int mode, int sel, int level, int card)
        {
            if (rust) dime_settings_window_navigate(main, mode, sel, level, card);
            else SettingsWindow::NavigateForTest(main, (IME_MODE)mode, sel, level, card);
            Pump();
        }

        std::wstring NewLog()
        {
            std::wstring s = app.log.substr(logPos);
            logPos = app.log.size();
            return s;
        }
    };

    void CompareTrees(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b, const std::wstring& what)
    {
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
            if (a[i] != b[i])
                Assert::Fail((what + L" element " + std::to_wstring(i) + L"\nC++:  " + a[i] + L"\nRust: " + b[i]).c_str());
        Assert::AreEqual(a.size(), b.size(), (what + L": element count").c_str());
    }

    std::wstring Dir()
    {
        wchar_t base[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, base);
        std::wstring dir = std::wstring(base) + L"DIMEUia" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    struct Page {
        int mode, sel, level, card;
        std::wstring Name() const
        {
            return L"mode " + std::to_wstring(mode) + L" sel " + std::to_wstring(sel) + L" level " + std::to_wstring(level)
                + L" card " + std::to_wstring(card);
        }
    };

    std::vector<Page> Pages()
    {
        int imeCount = 0;
        GetIMEPageLayout(&imeCount);
        std::vector<Page> out;
        for (int mode = 0; mode < 4; mode++) {
            out.push_back({ mode, mode, 0, 0 });
            for (int card = 0; card < imeCount; card++) out.push_back({ mode, mode, 1, card });
        }
        out.push_back({ 0, 5, 0, 0 });
        return out;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsUiaTest)
    {
    public:
        TEST_CLASS_CLEANUP(Cleanup)
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
        }

        // Every page: the two UIA trees match.
        TEST_METHOD(PagesMatch)
        {
            std::wstring dir = Dir();
            SettingsSnapshot snap = SettingsModel::LoadFromConfig();
            Side c{ false }, r{ true };
            for (Side* s : { &c, &r }) { s->app.snap = snap; s->app.snap.colorMode = 3; s->app.colorMode = 3; }
            c.Create(dir);
            r.Create(dir);
            int elements = 0, unnamed = 0;
            std::wstring unnamedList;
            for (const Page& p : Pages()) {
                c.Navigate(p.mode, p.sel, p.level, p.card);
                r.Navigate(p.mode, p.sel, p.level, p.card);
                Assert::AreEqual(c.NewLog(), r.NewLog(), (p.Name() + L": app calls").c_str());
                std::vector<std::wstring> tc, tr;
                OnUiaThread([&](IUIAutomation* uia) {
                    tc = Tree(uia, c.main);
                    tr = Tree(uia, r.main);
                });
                CompareTrees(tc, tr, p.Name());
                elements += (int)tc.size();
                // Accessibility report (not an assertion): focusable,
                // on-screen controls without a name.
                for (const std::wstring& e : tc) {
                    if (e.find(L"focus=1") != std::wstring::npos && e.find(L"name=[]") != std::wstring::npos
                        && e.find(L"off=0") != std::wstring::npos) {
                        unnamed++;
                        if (unnamedList.size() < 1500) unnamedList += p.Name() + L": " + e + L"\n";
                    }
                }
            }
            Logger::WriteMessage((L"elements " + std::to_wstring(elements) + L", focusable without a name "
                + std::to_wstring(unnamed) + L"\n" + unnamedList).c_str());
            Assert::IsTrue(elements > 200, std::to_wstring(elements).c_str());
            DestroyWindow(c.main);
            DestroyWindow(r.main);
            Pump();
            RemoveDirectoryW(dir.c_str());
        }

        // Every page: drive each check box (Toggle), combo box item (Select)
        // and edit box (SetValue) through UIA; app calls and trees match.
        TEST_METHOD(PatternsMatch)
        {
            std::wstring dir = Dir();
            SettingsSnapshot snap = SettingsModel::LoadFromConfig();
            Side c{ false }, r{ true };
            for (Side* s : { &c, &r }) s->app.snap = snap;
            c.Create(dir);
            r.Create(dir);
            int actions = 0, effective = 0, comboActions = 0, comboChanged = 0;
            for (const Page& p : Pages()) {
                c.Navigate(p.mode, p.sel, p.level, p.card);
                r.Navigate(p.mode, p.sel, p.level, p.card);
                c.NewLog();
                r.NewLog();
                // The actions, by index into the C++ tree (same shape on both sides).
                std::vector<std::pair<int, int>> todo;   // (element index, pattern)
                OnUiaThread([&](IUIAutomation* uia) {
                    std::vector<CComPtr<IUIAutomationElement>> all;
                    Tree(uia, c.main, &all);
                    for (int i = 0; i < (int)all.size(); i++) {
                        CComPtr<IUIAutomationTogglePattern> t;
                        CComPtr<IUIAutomationSelectionItemPattern> s;
                        CComPtr<IUIAutomationValuePattern> v;
                        if (SUCCEEDED(all[i]->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&t))) && t)
                            todo.push_back({ i, UIA_TogglePatternId });
                        else if (SUCCEEDED(all[i]->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&s))) && s)
                            todo.push_back({ i, UIA_SelectionItemPatternId });
                        else {
                            CONTROLTYPEID type = 0;
                            all[i]->get_CurrentControlType(&type);
                            if (type == UIA_ComboBoxControlTypeId)
                                todo.push_back({ i, UIA_ExpandCollapsePatternId });
                            else if (type == UIA_EditControlTypeId
                                && SUCCEEDED(all[i]->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&v))) && v)
                                todo.push_back({ i, UIA_ValuePatternId });
                        }
                    }
                });
                std::vector<std::wstring> before;
                OnUiaThread([&](IUIAutomation* uia) { before = Tree(uia, c.main); });
                for (auto [index, pattern] : todo) {
                    std::wstring what = p.Name() + L" element " + std::to_wstring(index) + L" pattern " + std::to_wstring(pattern);
                    for (Side* s : { &c, &r }) {
                        OnUiaThread([&](IUIAutomation* uia) {
                            std::vector<CComPtr<IUIAutomationElement>> all;
                            Tree(uia, s->main, &all);
                            if (index >= (int)all.size()) return;
                            IUIAutomationElement* e = all[index];
                            if (pattern == UIA_TogglePatternId) {
                                CComPtr<IUIAutomationTogglePattern> t;
                                if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&t))) && t) t->Toggle();
                            } else if (pattern == UIA_ExpandCollapsePatternId) {
                                // Select the item after the current one. The list is
                                // reachable without dropping it down; expanding through
                                // UIA is asynchronous (popup timing) and not repeatable.
                                CComPtr<IUIAutomationCondition> isItem;
                                CComVariant itemType(UIA_ListItemControlTypeId);
                                uia->CreatePropertyCondition(UIA_ControlTypePropertyId, itemType, &isItem);
                                CComPtr<IUIAutomationElementArray> items;
                                e->FindAll(TreeScope_Descendants, isItem, &items);
                                int n = 0;
                                if (items) items->get_Length(&n);
                                int current = -1;
                                for (int k = 0; k < n; k++) {
                                    CComPtr<IUIAutomationElement> it;
                                    items->GetElement(k, &it);
                                    CComPtr<IUIAutomationSelectionItemPattern> si;
                                    BOOL selected = FALSE;
                                    if (SUCCEEDED(it->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&si))) && si)
                                        si->get_CurrentIsSelected(&selected);
                                    if (selected) current = k;
                                }
                                if (n > 0) {
                                    CComPtr<IUIAutomationElement> it;
                                    items->GetElement((current + 1) % n, &it);
                                    CComPtr<IUIAutomationSelectionItemPattern> si;
                                    if (SUCCEEDED(it->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&si))) && si) si->Select();
                                }
                            } else if (pattern == UIA_SelectionItemPatternId) {
                                CComPtr<IUIAutomationSelectionItemPattern> si;
                                if (SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&si))) && si) si->Select();
                            } else {
                                CComPtr<IUIAutomationValuePattern> v;
                                if (SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&v))) && v) {
                                    CComBSTR text(L"6");
                                    v->SetValue(text);
                                }
                            }
                        });
                        Pump();
                    }
                    std::wstring lc = c.NewLog(), lr = r.NewLog();
                    if (lc != lr) Assert::Fail((what + L": app calls\nC++:\n" + lc + L"Rust:\n" + lr).c_str());
                    if (!lc.empty()) effective++;
                    actions++;
                    std::vector<std::wstring> tc, tr;
                    OnUiaThread([&](IUIAutomation* uia) {
                        tc = Tree(uia, c.main);
                        tr = Tree(uia, r.main);
                    });
                    CompareTrees(tc, tr, what);
                    if (pattern == UIA_ExpandCollapsePatternId) {
                        comboActions++;
                        if (tc != before) comboChanged++;
                    }
                    before = tc;
                }
            }
            Logger::WriteMessage((L"actions " + std::to_wstring(actions) + L", with app calls " + std::to_wstring(effective)
                + L"; combo selections " + std::to_wstring(comboActions) + L", changed the tree " + std::to_wstring(comboChanged)).c_str());
            Assert::IsTrue(actions > 30, std::to_wstring(actions).c_str());
            Assert::IsTrue(effective > 5, std::to_wstring(effective).c_str());
            DestroyWindow(c.main);
            DestroyWindow(r.main);
            Pump();
            RemoveDirectoryW(dir.c_str());
        }
    };
}
