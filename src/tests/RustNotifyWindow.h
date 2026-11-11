// RustNotifyWindow.h - test-only stand-in for the removed product type
// (phase 4b-3). Notify parity drives dime_ui_notifywin_*.
// The product presenter owns its own Rust notify windows.

#pragma once

#include "NotifyWindow.h"

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

class CRustNotifyWindow
{
public:
    CRustNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void* pv, _In_ enum NOTIFY_TYPE notifyType);
    ~CRustNotifyWindow();
    CRustNotifyWindow(const CRustNotifyWindow&) = delete;
    CRustNotifyWindow& operator=(const CRustNotifyWindow&) = delete;

    // CNotifyWindow::_Create (fontSize is unused there too); TRUE also when the window could
    // not be created, as there.
    BOOL _Create(_In_ UINT fontSize, _In_opt_ HWND parentWndHandle, _In_opt_ CStringRange* notifyText = nullptr);
    BOOL _IsCreated() const { return _created; }   // CNotifyWindow: _GetUIWnd() != nullptr
    void _Destroy();   // CBaseWindow::_Destroy

    HWND _GetWnd() const;
    BOOL _IsWindowVisible() const;
    BOOL _GetWindowRect(_Inout_ LPRECT lpRect) const;
    BOOL _GetClientRect(_Inout_ LPRECT lpRect) const;
    HRESULT _GetWindowExtent(_In_ const RECT* prcTextExtent, _In_opt_ RECT* prcCandidateExtent, _Inout_ POINT* pptCandidate) const;
    UINT _GetWidth() const;
    UINT _GetHeight() const;
    void _Move(int x, int y);
    void _Show(BOOL isShowWnd, UINT delayShow = 0, UINT timeToHide = 0);
    void _InvalidateRect();
    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF crFiColor);
    void _SetString(_Inout_ const CStringRange* pNotifyText);
    NOTIFY_TYPE GetNotifyType() const { return _notifyType; }

    DimeNotify* _GetRustWindow() const { return _n; }
#ifdef DIME_UNIT_TESTING
    // The classes the window is created with (before _Create; default: Global::Atom*).
    void _SetClassesForTest(const DimeNotifyClasses& classes) { _classesForTest = classes; _hasClassesForTest = TRUE; }
#endif

private:
    static void Invoke(void* ctx, int32_t action, uintptr_t wParam, intptr_t lParam);

    NOTIFYWNDCALLBACK _pfnCallback;
    void* _pObj;
    NOTIFY_TYPE _notifyType;
    DimeNotify* _n;
    BOOL _created;
#ifdef DIME_UNIT_TESTING
    DimeNotifyClasses _classesForTest = {};
    BOOL _hasClassesForTest = FALSE;
#endif
};
#endif
