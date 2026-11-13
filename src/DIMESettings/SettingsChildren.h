// SettingsChildren.h — creating a page's child windows from ControlSpecs
//
// Phase 5b-9c of docs/RUST_MIGRATION_PLAN.md. What used to be
// SettingsWindow::CreateControls / AddControl / SetupRichEditor /
// LoadRichEditor and the child WM_MOUSEWHEEL forwarder: CreateWindowEx per
// spec, the wheel subclass, the body font, the dark theme, and for the
// 自建詞庫 RichEdit its text limit, colours, saved table, editor subclass and
// event mask. The C++ original is SettingsChildren::Cpp::Create; with
// DIME_USE_RUST_CONFIG, Create runs dime-settings (children.rs) through
// DimeSettingsChildHostVtbl. Change both together.
//
// The window keeps the list of controls (Host::Added) and the editor state
// (Host::EditorReady validates the viewport). Host methods must not throw.

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "SettingsControls.h"

namespace SettingsChildren {

struct Context {
    HWND parent;            // the content area
    HINSTANCE instance;
    HFONT fontBody;
    int isDark;
    COLORREF cardBg;        // RichEdit background when dark
    COLORREF textPrimary;   // RichEdit text when dark
};

class Host {
public:
    virtual ~Host() = default;
    // A control was created (h may be null when CreateWindowEx failed).
    // x, y, w, h and visH are the spec's `add` rectangle and visH.
    virtual void Added(int ctrlId, HWND h, int x, int y, int w, int ht, int visH) = 0;
    // GetCustomTableTxtPath for the current mode.
    virtual std::wstring EditorPath() = 0;
    // The RichEdit holds the saved table and its subclass; lineCount is
    // EM_GETLINECOUNT. The window resets its editor counters and validates.
    virtual void EditorReady(HWND hRE, int lineCount) = 0;
};

// The WM_MOUSEWHEEL forwarder subclass id on plain controls.
constexpr UINT_PTR kWheelSubclassId = 1;

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs);

namespace Cpp {
void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
void CreateRust(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs);
#endif

} // namespace SettingsChildren
