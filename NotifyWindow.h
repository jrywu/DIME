//
//
// Jeremy '13,7,38
//
//



#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"
#include "BaseStructure.h"

enum NOTIFYWND_ACTION
{
    SWITCH_CHN_ENG,
	SHOW_CHN_ENG_NOTIFY
};



typedef HRESULT (*NOTIFYWNDCALLBACK)(void *pv, enum NOTIFYWND_ACTION action);

class CNotifyWindow : public CBaseWindow
{
public:
    CNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void *pv, _In_ enum NOTIFY_TYPE notifyType);
    virtual ~CNotifyWindow();

	BOOL _Create(_In_ UINT fontHeight,  _In_opt_ HWND parentWndHandle, CStringRange* notifytext=nullptr);

    void _Move(int x, int y);
    void _Show(BOOL isShowWnd, int timeToHide);
	void _Show(BOOL isShowWnd);

    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF crBkColor);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
	virtual void _OnTimer();
    
	void _OnLButtonDown(POINT pt);
    //void _OnLButtonUp(POINT pt);
    void _OnMouseMove(POINT pt);
    
    void _AddString(_Inout_ const CStringRange *pNotifyText);
	void _SetString(_Inout_ const CStringRange *pNotifyText);
    void _Clear();

	UINT _GetWidth();
	UINT _GetHeight();

	void SetNotifyType(enum NOTIFY_TYPE notifytype) { _notifyType = notifytype;}
	NOTIFY_TYPE GetNotifyType() { return _notifyType;}
    
private:
    void _HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point);
	void _DrawText(_In_ HDC dcHandle, _In_ RECT *prc);
    void _DrawBorder(_In_ HWND wndHandle, _In_ int cx);
    
	// LightDismiss feature support, it will fire messages lightdismiss-related to the light dismiss layout.
    void _FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos);

    BOOL _CreateMainWindow(_In_opt_ HWND parentWndHandle);
    BOOL _CreateBackGroundShadowWindow();
    
    void _ResizeWindow();
    void _DeleteShadowWnd();
    
    friend COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);

private:
    COLORREF _crTextColor;
    COLORREF _crBkColor;
    HBRUSH _brshBkColor;

	int _timeToHide;

    TEXTMETRIC _TextMetric;
    
    NOTIFYWNDCALLBACK _pfnCallback;
    void* _pObj;
	int _cxTitle;
	int _cyTitle;
	int _x;
	int _y;

	CStringRange _notifyText;

	NOTIFY_TYPE _notifyType;

	UINT _fontSize;

    CShadowWindow* _pShadowWnd;
};
