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

#include "BaseWindow.h"
#include "ButtonWindow.h"

class CScrollButtonWindow;

enum SCROLLBAR_DIRECTION
{
    SCROLL_NONE_DIR,
    SCROLL_PAGEDOWN,    // or page left
    SCROLL_PAGEUP       // or page right
};

enum SHELL_MODE
{
    STOREAPP,
    DESKTOP,
};

struct CScrollInfo
{
    CScrollInfo()
    {
        nMax = 0;
        nPage = 0;
        nPos = 0;
    }

    int  nMax;
    int  nPage;
    int  nPos;
};

class CScrollBarWindow;

class CScrollBarWindowFactory
{
public:
    static CScrollBarWindowFactory* Instance();
    CScrollBarWindow* MakeScrollBarWindow(SHELL_MODE shellMode);
    void Release();

protected:
    CScrollBarWindowFactory();

private:
    static CScrollBarWindowFactory* _instance;

};
//////////////////////////////////////////////////////////////////////
//
// CScrollBarWindow declaration.
//
//////////////////////////////////////////////////////////////////////

enum SCROLLBAR_HITTEST
{
    SB_HIT_NONE,
    SB_HIT_BTN_UP,
    SB_HIT_BTN_DOWN,
    SB_HIT_TRACK_ABOVE,
    SB_HIT_TRACK_BELOW,
    SB_HIT_THUMB,
};

// Thin scrollbar width constants (DPI-scaled at runtime)
constexpr int SCROLLBAR_THIN_WIDTH = 6;     // idle width in pixels at 96 DPI
constexpr int SCROLLBAR_HOVER_WIDTH = 16;   // expanded width on hover at 96 DPI (even number for pixel-perfect centering)
constexpr int SCROLLBAR_THUMB_MIN_H = 12;   // minimum thumb height in pixels at 96 DPI
constexpr int SCROLLBAR_BTN_HEIGHT = 14;    // arrow button height in pixels at 96 DPI; also defines gap between triangle and thumb
constexpr int SCROLLBAR_IDLE_THUMB_W = 2;   // idle thin-line thumb width at 96 DPI
constexpr UINT BTNREPEAT_TIMER_ID = 39779;
constexpr UINT BTNREPEAT_DELAY_MS = 400;    // initial delay before repeat starts
constexpr UINT BTNREPEAT_SPEED_MS = 50;     // repeat interval after initial delay
constexpr UINT FADE_TIMER_ID = 39780;
constexpr UINT FADE_DELAY_MS = 1500;        // delay before fade starts
constexpr UINT FADE_STEP_MS = 30;           // fade animation step interval
constexpr BYTE FADE_ALPHA_VISIBLE = 200;    // full visibility alpha
constexpr BYTE FADE_ALPHA_STEP = 20;        // alpha decrease per step

class CScrollBarWindow : public CBaseWindow
{
public:
    CScrollBarWindow();
    virtual ~CScrollBarWindow();

    BOOL _Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, CBaseWindow *pParent = nullptr, int wndWidth = 0, int wndHeight = 0);

    void _Resize(int x, int y, int cx, int cy);
    void _Show(BOOL isShowWnd);
    void _Enable(BOOL isEnable);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    virtual void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    virtual void _OnLButtonDown(POINT pt);
    virtual void _OnLButtonUp(POINT pt);
    virtual void _OnMouseMove(POINT pt);
    virtual void _OnTimer() { };
    virtual void _OnTimerID(UINT_PTR timerID);

    virtual void _OnOwnerWndMoved(BOOL isResized);

    virtual void _SetScrollInfo(_In_ CScrollInfo *lpsi);
    virtual void _GetScrollInfo(_Out_ CScrollInfo *lpsi);

    BOOL _GetBtnUpRect(_Out_ RECT *prc);
    BOOL _GetBtnDnRect(_Out_ RECT *prc);
    BOOL _GetTrackRect(_Out_ RECT *prc);
    BOOL _GetThumbRect(_Out_ RECT *prc);
    SCROLLBAR_HITTEST _HitTest(POINT pt);

    virtual void _ShiftLine(int nLine, BOOL isNotify)
    {
        DWORD dwSB = 0;

        dwSB = (nLine > 0 ? SB_LINEDOWN :
            nLine < 0 ? SB_LINEUP : 0);

        _SetCurPos(_scrollInfo.nPos + nLine, isNotify ? dwSB : -1);
    }

    virtual void _ShiftPage(int nPage, BOOL isNotify)
    {
        DWORD dwSB = 0;

        dwSB = (nPage > 0 ? SB_PAGEDOWN :
            nPage < 0 ? SB_PAGEUP : 0);

        _SetCurPos(_scrollInfo.nPos + _scrollInfo.nPage * nPage, isNotify ? dwSB : -1);
    }

    virtual void _ShiftPosition(int iPos, BOOL isNotify)
    {
        _SetCurPos(iPos, isNotify ? SB_THUMBPOSITION : -1);
    }

    virtual void _GetScrollArea(_Out_ RECT *prc);
    virtual void _SetCurPos(int nPos, int dwSB);

    // Auto-hide: trigger visibility on scroll activity
    void _OnScrollActivity();
    int _GetIdleWidth();
    int _GetHoverWidth();

    // Called by candidate window when mouse enters/leaves the scrollbar column.
    // TRUE  → show full scrollbar immediately; cancels any pending collapse timer.
    // FALSE → schedule collapse to thin line after SCROLLBAR_COLLAPSE_DELAY_MS.
    void _SetCandidateMouseIn(BOOL isIn);

private:
    void _AdjustWindowPos();

    CScrollInfo _scrollInfo;
    SCROLLBAR_DIRECTION _scrollDir;

    // Thumb dragging state
    BOOL  _isDragging;
    int   _dragStartY;
    int   _dragStartPos;

    // Auto-hide state
    BYTE  _currentAlpha;      // current opacity (0-255)
    BOOL  _isFading;          // TRUE when fade-out timer is running
    DWORD _lastActivityTick;  // GetTickCount() of last scroll/hover

    // Hover state
    BOOL  _isHovered;         // mouse is over scrollbar
    BOOL  _isTrackingMouse;   // TrackMouseEvent registered

    // Button auto-repeat state
    SCROLLBAR_HITTEST _heldButton;  // SB_HIT_BTN_UP or SB_HIT_BTN_DOWN while held, else SB_HIT_NONE
    BOOL _repeatStarted;            // TRUE after initial delay, now in fast repeat
};


//////////////////////////////////////////////////////////////////////
//
// CScrollBarNullWindow declaration.
//
//////////////////////////////////////////////////////////////////////

class CScrollBarNullWindow : public CScrollBarWindow
{
public:
    CScrollBarNullWindow() { };
    virtual ~CScrollBarNullWindow() { };

    virtual BOOL _Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow *pParent = nullptr, int wndWidth = 0, int wndHeight = 0)
    { 
        atom; dwExStyle; dwStyle; pParent; wndWidth; wndHeight;
        return TRUE; 
    };

    virtual void _Destroy() { };

    virtual void _Move(int x, int y) { x;y; };
    virtual void _Resize(int x, int y, int cx, int cy) {  x; y; cx; cy; };
    virtual void _Show(BOOL isShowWnd) { isShowWnd; };
    virtual BOOL _IsWindowVisible() { return TRUE; };
    virtual void _Enable(BOOL isEnable) { isEnable; };
    virtual BOOL _IsEnabled() { return TRUE; };
    virtual void _InvalidateRect() { };
    virtual BOOL _GetWindowRect(_Out_ LPRECT lpRect) { lpRect->bottom = 0; lpRect->left = 0; lpRect->right = 0; lpRect->top = 0; return TRUE; };
    virtual BOOL _GetClientRect(_Out_ LPRECT lpRect) { lpRect->bottom = 0; lpRect->left = 0; lpRect->right = 0; lpRect->top = 0; return TRUE; };

    virtual LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) 
    { 
        wndHandle; uMsg; wParam; lParam;
        return 0; 
    };
    virtual void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps) { dcHandle;pps; };
    virtual void _OnLButtonDown(POINT pt) { pt; };
    virtual void _OnLButtonUp(POINT pt) { pt; };
    virtual void _OnMouseMove(POINT pt) { pt; };
    virtual void _OnTimer() { };

    virtual void _OnOwnerWndMoved(BOOL isResized) { isResized; };

    virtual void _SetScrollInfo(_In_ CScrollInfo *lpsi) { lpsi; };
    virtual void _GetScrollInfo(_In_ CScrollInfo *lpsi) { lpsi; };

    virtual BOOL _GetBtnUpRect(_Out_ RECT *prc) { prc->bottom = 0; prc->left = 0; prc->right = 0; prc->top = 0; return TRUE; };
    virtual BOOL _GetBtnDnRect(_Out_ RECT *prc) { prc->bottom = 0; prc->left = 0; prc->right = 0; prc->top = 0; return TRUE; };

    virtual void _ShiftLine(int nLine, BOOL isNotify) { nLine; isNotify; };
    virtual void _ShiftPage(int nPage, BOOL isNotify) { nPage; isNotify; };
    virtual void _ShiftPosition(int iPos, BOOL isNotify) { iPos; isNotify; };
    virtual void _GetScrollArea(_Out_ RECT *prc) { prc->bottom = 0; prc->left = 0; prc->right = 0; prc->top = 0; };
    virtual void _SetCurPos(int nPos, DWORD dwSB) { nPos; dwSB; };
};

//////////////////////////////////////////////////////////////////////
//
// CScrollButtonWindow declaration.
//
//////////////////////////////////////////////////////////////////////

class CScrollButtonWindow : public CButtonWindow
{
public:
    CScrollButtonWindow(UINT uSubType)
    {
        subTypeOfControl = uSubType;
    }
    virtual ~CScrollButtonWindow()
    {
    }

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) 
    { 
        wndHandle;uMsg;wParam;lParam;
        return 0; 
    }
    virtual void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    virtual void _OnLButtonDown(POINT pt);
    virtual void _OnLButtonUp(POINT pt);
    virtual void _OnTimer();

private:
    UINT subTypeOfControl;        // DFCS_SCROLLxxx for DrawFrameControl
};
