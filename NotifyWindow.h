/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"
#include "BaseStructure.h"

enum class NOTIFY_WND
{
    SWITCH_CHN_ENG,
	SHOW_NOTIFY
};



typedef HRESULT (*NOTIFYWNDCALLBACK)(void *pv, enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam);

class CNotifyWindow : public CBaseWindow
{
public:
    CNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void *pv, _In_ enum NOTIFY_TYPE notifyType);
    virtual ~CNotifyWindow();

	BOOL _Create(_In_ UINT fontHeight,  _In_opt_ HWND parentWndHandle, _In_opt_ CStringRange* notifytext=nullptr);

    void _Move(int x, int y);
    void _Show(BOOL isShowWnd, UINT delayShow = 0, UINT timeToHide = 0);

    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF crFiColor);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
	virtual void _OnTimerID(UINT_PTR timerID);
    
	void _OnLButtonDown(POINT pt);
    void _OnRButtonDown(POINT pt);
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

	UINT _timeToHide;
	UINT _delayShow;

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

    std::unique_ptr<CShadowWindow> _pShadowWnd;

	BYTE _animationStage;
};
