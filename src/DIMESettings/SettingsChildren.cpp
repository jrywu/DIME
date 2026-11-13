// SettingsChildren.cpp — see SettingsChildren.h. Cpp::Create is what used to
// be SettingsWindow::CreateControls with AddControl, SetupRichEditor,
// LoadRichEditor and ChildWheelSubclassProc; dime-settings (children.rs)
// mirrors it.

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <richedit.h>
#include <strsafe.h>
#include <Shlwapi.h>
#include "..\BaseStructure.h"
#include "SettingsChildren.h"
#include "SettingsController.h"
#include "SettingsEditor.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")

namespace SettingsChildren {

namespace Cpp {

namespace {

// Child controls forward the wheel to the content area so it scrolls.
LRESULT CALLBACK WheelSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR)
{
    if (uMsg == WM_MOUSEWHEEL)
        return SendMessage(GetParent(hWnd), uMsg, wParam, lParam);
    if (uMsg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, WheelSubclassProc, uIdSubclass);
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Right after the RichEdit is created: text limit, font, dark colours.
void SetupRichEditor(const Context& ctx, HWND hRE)
{
    // Issue #130: RichEdit50W defaults to ~32K-64K characters; SetWindowTextW
    // bypasses the cap but typing is then rejected. Raise it to 100 MB.
    SendMessageW(hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100 * 1024 * 1024));
    SendMessage(hRE, WM_SETFONT, (WPARAM)ctx.fontBody, TRUE);
    if (ctx.isDark) {
        SetWindowTheme(hRE, L"DarkMode_Explorer", nullptr);
        SendMessage(hRE, EM_SETBKGNDCOLOR, 0, (LPARAM)ctx.cardBg);
        CHARFORMAT2W cf2 = {};
        cf2.cbSize = sizeof(cf2);
        cf2.dwMask = CFM_COLOR;
        cf2.crTextColor = ctx.textPrimary;
        SendMessage(hRE, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf2);
    }
}

// The saved custom table (encoding-aware reader, issue #130), the editor
// subclass, EN_CHANGE, then the window validates the visible lines.
void LoadRichEditor(Host& host, HWND hRE)
{
    std::wstring path = host.EditorPath();
    if (PathFileExistsW(path.c_str())) {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
        StringCchCopyW(cf.szFaceName, LF_FACESIZE, L"Microsoft JhengHei UI");
        cf.yHeight = 10 * 20;  // 10pt in twips — RichEdit applies DPI itself
        cf.wWeight = FW_NORMAL;
        SendMessageW(hRE, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

        size_t wlen = 0;
        LPWSTR text = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &wlen);
        if (text) {
            SetWindowTextW(hRE, text);
            delete[] text;
        }
    }
    SetWindowSubclass(hRE, SettingsEditor::SubclassProc(), 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, ENM_CHANGE);
    host.EditorReady(hRE, (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0));
}

} // namespace

void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    static HMODULE hMsftEdit = nullptr;
    for (const auto& spec : specs) {
        if (spec.controlClass == SettingsControls::CLS_RICHEDIT && !hMsftEdit)
            hMsftEdit = LoadLibraryW(L"msftedit.dll");
        HWND h = CreateWindowExW(spec.exStyle, SettingsControls::ClassName(spec.controlClass),
            spec.hasText ? spec.text.c_str() : nullptr, spec.style,
            spec.create.left, spec.create.top, spec.create.right, spec.create.bottom,
            ctx.parent, (HMENU)(UINT_PTR)spec.menuId, ctx.instance, nullptr);
        if (spec.role == SettingsControls::ROLE_RICH_EDITOR) {
            // The editor scrolls itself: no wheel forwarder.
            if (h) SetupRichEditor(ctx, h);
            host.Added(spec.ctrlId, h, spec.add.left, spec.add.top, spec.add.right, spec.add.bottom, spec.visH);
            if (h) LoadRichEditor(host, h);
            continue;
        }
        if (h && spec.setFont)
            SendMessage(h, WM_SETFONT, (WPARAM)ctx.fontBody, TRUE);
        if (h && ctx.isDark && spec.darkTheme == SettingsControls::DARK_CFD)
            SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        else if (h && ctx.isDark && spec.darkTheme == SettingsControls::DARK_EXPLORER)
            SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        host.Added(spec.ctrlId, h, spec.add.left, spec.add.top, spec.add.right, spec.add.bottom, spec.visH);
        if (h) SetWindowSubclass(h, WheelSubclassProc, kWheelSubclassId, 0);
    }
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(DimeControlSpec) == 80, "DimeControlSpec");
static_assert(sizeof(DimeSettingsChildContext) == sizeof(Context), "DimeSettingsChildContext");

namespace {

// C thunks for DimeSettingsChildHostVtbl. Host methods must not throw; the
// try blocks keep a stray exception from unwinding into Rust.
void Th_Added(void* c, int32_t ctrlId, void* h, int32_t x, int32_t y, int32_t w, int32_t ht, int32_t visH)
{
    try { static_cast<Host*>(c)->Added(ctrlId, (HWND)h, x, y, w, ht, visH); } catch (...) {}
}

size_t Th_EditorPath(void* c, uint16_t* buf, size_t cap)
{
    try {
        std::wstring p = static_cast<Host*>(c)->EditorPath();
        if (buf && cap > 0) {
            size_t n = p.size() < cap - 1 ? p.size() : cap - 1;
            memcpy(buf, p.c_str(), n * sizeof(wchar_t));
            buf[n] = 0;
        }
        return p.size();
    } catch (...) {
        if (buf && cap > 0) buf[0] = 0;
        return 0;
    }
}

void Th_EditorReady(void* c, void* hRE, int32_t lineCount)
{
    try { static_cast<Host*>(c)->EditorReady((HWND)hRE, lineCount); } catch (...) {}
}

const DimeSettingsChildHostVtbl kVtbl = { &Th_Added, &Th_EditorPath, &Th_EditorReady };

} // namespace

void CreateRust(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    std::vector<DimeControlSpec> raw;
    std::vector<uint16_t> text;
    for (const auto& s : specs) {
        DimeControlSpec r = {};
        r.ctrl_id = s.ctrlId;
        r.menu_id = s.menuId;
        r.role = s.role;
        r.control_class = s.controlClass;
        r.ex_style = s.exStyle;
        r.style = s.style;
        r.has_text = s.hasText ? 1 : 0;
        r.text_offset = (int32_t)text.size();
        r.text_len = s.hasText ? (int32_t)s.text.size() : 0;
        if (s.hasText)
            text.insert(text.end(), s.text.begin(), s.text.end());
        r.create[0] = s.create.left;
        r.create[1] = s.create.top;
        r.create[2] = s.create.right;
        r.create[3] = s.create.bottom;
        r.add[0] = s.add.left;
        r.add[1] = s.add.top;
        r.add[2] = s.add.right;
        r.add[3] = s.add.bottom;
        r.vis_h = s.visH;
        r.set_font = s.setFont ? 1 : 0;
        r.dark_theme = s.darkTheme;
        raw.push_back(r);
    }
    dime_settings_create_children(&kVtbl, &host, &ctx, raw.data(), raw.size(), text.data(), text.size());
}

#endif

#ifdef DIME_USE_RUST_CONFIG
void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    CreateRust(host, ctx, specs);
}
#else
void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    Cpp::Create(host, ctx, specs);
}
#endif

} // namespace SettingsChildren
