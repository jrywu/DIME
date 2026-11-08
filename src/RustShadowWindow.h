// RustShadowWindow.h - the drop shadow window owned by Rust (dime-ui::shadow,
// phase 3c-5b; docs/RUST_WINDOW_OWNERSHIP.md).
//
// CRustShadowWindow has the members of CShadowWindow that the candidate and
// notify windows use; DimeShadowWindow is the type they hold: CRustShadowWindow
// under DIME_USE_RUST_ENGINE, CShadowWindow otherwise.

#pragma once

#include "ShadowWindow.h"

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

class CRustShadowWindow
{
public:
    explicit CRustShadowWindow(_In_ CBaseWindow* pWndOwner) : _pWndOwner(pWndOwner), _shadow(nullptr) { }
    ~CRustShadowWindow();
    CRustShadowWindow(const CRustShadowWindow&) = delete;
    CRustShadowWindow& operator=(const CRustShadowWindow&) = delete;

    // CShadowWindow::_Create: the window around the owner (atom: a class registered by
    // dime_ui_register_window_class).
    BOOL _Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow* pParent = nullptr);
    HWND _GetWnd() const;
    void _Show(BOOL isShowWnd);
    void _OnOwnerWndMoved(BOOL isResized);
    void _OnSettingChange();

    // The state (ShadowParityTest).
    COLORREF _GetColorForTest() const;
    SIZE _GetSizeShiftForTest() const;
    BOOL _IsGradientForTest() const;

private:
    CBaseWindow* _pWndOwner;
    DimeShadow* _shadow;
};
#endif

#ifdef DIME_USE_RUST_ENGINE
typedef CRustShadowWindow DimeShadowWindow;
#else
typedef CShadowWindow DimeShadowWindow;
#endif
