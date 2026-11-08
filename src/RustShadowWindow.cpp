// RustShadowWindow.cpp - see RustShadowWindow.h

#include "Private.h"
#include "Globals.h"
#include "Config.h"
#include "RustShadowWindow.h"

#ifdef DIME_RUST_AVAILABLE

namespace
{
    uint32_t DpiForHwnd(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
    int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    const DimeShadowEnv s_env = { DpiForHwnd, Dark };
}

CRustShadowWindow::~CRustShadowWindow()
{
    dime_ui_shadow_free(_shadow);
}

BOOL CRustShadowWindow::_Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow* pParent)
{
    HWND owner = pParent ? pParent->_GetWnd() : nullptr;
    int radiusBase = _pWndOwner ? _pWndOwner->_GetCornerRadiusBase() : 10;
    _shadow = dime_ui_shadow_create(Global::dllInstanceHandle, atom, dwExStyle, dwStyle, owner, radiusBase, &s_env);
    return _shadow != nullptr;
}

HWND CRustShadowWindow::_GetWnd() const
{
    return (HWND)dime_ui_shadow_hwnd(_shadow);
}

void CRustShadowWindow::_Show(BOOL isShowWnd)
{
    dime_ui_shadow_show(_shadow, isShowWnd ? 1 : 0);
}

void CRustShadowWindow::_OnOwnerWndMoved(BOOL isResized)
{
    dime_ui_shadow_owner_moved(_shadow, isResized ? 1 : 0);
}

void CRustShadowWindow::_OnSettingChange()
{
    dime_ui_shadow_setting_change(_shadow);
}

COLORREF CRustShadowWindow::_GetColorForTest() const
{
    uint32_t color = 0;
    dime_ui_shadow_state(_shadow, &color, nullptr, nullptr);
    return color;
}

SIZE CRustShadowWindow::_GetSizeShiftForTest() const
{
    DimePoint shift = { 0, 0 };
    dime_ui_shadow_state(_shadow, nullptr, &shift, nullptr);
    return { shift.x, shift.y };
}

BOOL CRustShadowWindow::_IsGradientForTest() const
{
    int32_t gradient = 0;
    dime_ui_shadow_state(_shadow, nullptr, nullptr, &gradient);
    return gradient ? TRUE : FALSE;
}

#endif
