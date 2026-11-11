// RustNotifyWindow.cpp - see RustNotifyWindow.h

#include "Private.h"
#include "Globals.h"
#include "Config.h"
#include "RustNotifyWindow.h"

#ifdef DIME_RUST_AVAILABLE

namespace
{
    uint32_t Dpi(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
    int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    int32_t AlwaysShow() { return CConfig::GetAlwaysShowScrollbars() ? 1 : 0; }
    void* Font() { return Global::defaultlFontHandle; }
    void SetFont(void* hwnd) { CConfig::SetDefaultTextFont((HWND)hwnd); }
    const DimeCandEnv s_env = { Dpi, Dark, AlwaysShow, Font, SetFont };
}

CRustNotifyWindow::CRustNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void* pv, _In_ enum NOTIFY_TYPE notifyType)
    : _pfnCallback(pfnCallback), _pObj(pv), _notifyType(notifyType), _n(nullptr), _created(FALSE)
{
}

CRustNotifyWindow::~CRustNotifyWindow()
{
    _Destroy();
}

void CRustNotifyWindow::Invoke(void* ctx, int32_t action, uintptr_t wParam, intptr_t lParam)
{
    // Copy before calling: the callback may destroy the window and this object.
    CRustNotifyWindow* w = static_cast<CRustNotifyWindow*>(ctx);
    NOTIFYWNDCALLBACK pfnCallback = w->_pfnCallback;
    void* pObj = w->_pObj;
    if (pfnCallback)
        pfnCallback(pObj, action == 0 ? NOTIFY_WND::SWITCH_CHN_ENG : NOTIFY_WND::SHOW_NOTIFY, (WPARAM)wParam, (LPARAM)lParam);
}

BOOL CRustNotifyWindow::_Create(_In_ UINT fontSize, _In_opt_ HWND parentWndHandle, _In_opt_ CStringRange* notifyText)
{
    UNREFERENCED_PARAMETER(fontSize);
    if (_n) return TRUE;
    _created = TRUE;
    DimeNotifyClasses classes = { Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
    if (_hasClassesForTest) classes = _classesForTest;
#endif
    const DimeNotifyCallback callback = { this, Invoke };
    const uint16_t* text = notifyText ? (const uint16_t*)notifyText->Get() : nullptr;
    size_t len = notifyText ? notifyText->GetLength() : 0;
    _n = dime_ui_notifywin_create(Global::dllInstanceHandle, &classes, parentWndHandle, (int32_t)_notifyType, text, len,
                                  &s_env, &callback);
    // As CNotifyWindow::_Create: TRUE even when the window could not be created.
    return TRUE;
}

void CRustNotifyWindow::_Destroy()
{
    DimeNotify* n = _n;
    _n = nullptr;   // before freeing: destroying the window may send messages
    dime_ui_notifywin_free(n);
}

HWND CRustNotifyWindow::_GetWnd() const { return (HWND)dime_ui_notifywin_hwnd(_n); }
BOOL CRustNotifyWindow::_IsWindowVisible() const { return dime_ui_notifywin_is_visible(_n) ? TRUE : FALSE; }

BOOL CRustNotifyWindow::_GetWindowRect(_Inout_ LPRECT lpRect) const
{
    if (!lpRect) return FALSE;
    dime_ui_notifywin_rect(_n, 0, (DimeRect*)lpRect);
    return TRUE;
}

BOOL CRustNotifyWindow::_GetClientRect(_Inout_ LPRECT lpRect) const
{
    if (!lpRect) return FALSE;
    dime_ui_notifywin_rect(_n, 1, (DimeRect*)lpRect);
    return TRUE;
}

HRESULT CRustNotifyWindow::_GetWindowExtent(_In_ const RECT* prcTextExtent, _In_opt_ RECT* prcCandidateExtent, _Inout_ POINT* pptCandidate) const
{
    if (prcTextExtent == nullptr || prcCandidateExtent == nullptr || pptCandidate == nullptr)
        return E_INVALIDARG;
    dime_ui_notifywin_window_extent(_n, (const DimeRect*)prcTextExtent, (const DimeRect*)prcCandidateExtent, (DimePoint*)pptCandidate);
    return S_OK;
}

UINT CRustNotifyWindow::_GetWidth() const
{
    int32_t cx = 0, cy = 0;
    dime_ui_notifywin_title_size(_n, &cx, &cy);
    return (UINT)cx;
}

UINT CRustNotifyWindow::_GetHeight() const
{
    int32_t cx = 0, cy = 0;
    dime_ui_notifywin_title_size(_n, &cx, &cy);
    return (UINT)cy;
}

void CRustNotifyWindow::_Move(int x, int y) { dime_ui_notifywin_op(_n, 1, x, y, 0); }
void CRustNotifyWindow::_Show(BOOL isShowWnd, UINT delayShow, UINT timeToHide)
{
    dime_ui_notifywin_op(_n, 0, isShowWnd ? 1 : 0, (int32_t)delayShow, (int32_t)timeToHide);
}
void CRustNotifyWindow::_InvalidateRect() { dime_ui_notifywin_op(_n, 3, 0, 0, 0); }
VOID CRustNotifyWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor) { dime_ui_notifywin_set_text_color(_n, crColor, crBkColor); }
VOID CRustNotifyWindow::_SetFillColor(_In_ COLORREF crFiColor) { dime_ui_notifywin_op(_n, 2, (int32_t)crFiColor, 0, 0); }

void CRustNotifyWindow::_SetString(_Inout_ const CStringRange* pNotifyText)
{
    if (pNotifyText == nullptr) return;
    dime_ui_notifywin_set_string(_n, (const uint16_t*)pNotifyText->Get(), pNotifyText->GetLength());
}

#endif
