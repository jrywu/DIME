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


#include "Private.h"
#include "Globals.h"
#include "UIConstants.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"

// All-around soft shadow (VS Code style)
// SHADOW_SPREAD defined in ShadowWindow.h
#define SHADOW_MAX_ALPHA 35 // peak alpha for light shadow on dark bg (~14%)
#define SHADOW_MAX_ALPHA_DARK 45 // peak alpha for dark shadow on light bg (~18%)


//+---------------------------------------------------------------------------
//
// _Create
//
//----------------------------------------------------------------------------

BOOL CShadowWindow::_Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow *pParent, int wndWidth, int wndHeight)
{
    if (!CBaseWindow::_Create(atom, dwExStyle, dwStyle, pParent, wndWidth, wndHeight))
    {
        return FALSE;
    }

    return _Initialize();
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Shadow window proc.
//----------------------------------------------------------------------------

LRESULT CALLBACK CShadowWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
        {
            HDC dcHandle;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);

            HBRUSH hBrush = CreateSolidBrush(_color);
            FillRect(dcHandle, &ps.rcPaint, hBrush);
            DeleteObject(hBrush);
            EndPaint(wndHandle, &ps);
        }
        return 0;

    case WM_SETTINGCHANGE:
        _OnSettingChange();
        break;

    case WM_APP + 1:
        // Deferred shadow init — lets candidate window paint first
        _InitShadow();
        return 0;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _OnSettingChange
//
//----------------------------------------------------------------------------

void CShadowWindow::_OnSettingChange()
{
    _InitSettings();

    DWORD dwWndStyleEx = GetWindowLong(_GetWnd(), GWL_EXSTYLE);

    if (_isGradient)
    {
        SetWindowLong(_GetWnd(), GWL_EXSTYLE, (dwWndStyleEx | WS_EX_LAYERED));
    }
    else
    {
        SetWindowLong(_GetWnd(), GWL_EXSTYLE, (dwWndStyleEx & ~WS_EX_LAYERED));
    }

    _AdjustWindowPos();
    _InitShadow();
}

//+---------------------------------------------------------------------------
//
// _OnOwnerWndMoved
//
//----------------------------------------------------------------------------

void CShadowWindow::_OnOwnerWndMoved(BOOL isResized)
{
    if (IsWindow(_GetWnd()) && _IsWindowVisible())
    {
        _AdjustWindowPos();
        if (isResized)
        {
            _InitShadow();
        }
    }
}


//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CShadowWindow::_Show(BOOL isShowWnd)
{
    // Position shadow before it appears (or before hiding).
    _AdjustWindowPos();
    // Show/hide the window — on show, this enters the DWM composition tree.
    CBaseWindow::_Show(isShowWnd);
    if (isShowWnd)
    {
        // Call UpdateLayeredWindow AFTER the window is visible so DWM applies the bitmap.
        // Calling it on a hidden window is silently discarded by DWM (root cause #8).
        _InitShadow();
    }
}

//+---------------------------------------------------------------------------
//
// _Initialize
//
//----------------------------------------------------------------------------

BOOL CShadowWindow::_Initialize()
{
    _InitSettings();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _InitSettings
//
//----------------------------------------------------------------------------

void CShadowWindow::_InitSettings()
{
    HDC dcHandle = GetDC(nullptr);
    int cBitsPixelScreen = GetDeviceCaps(dcHandle, BITSPIXEL);
    _isGradient = cBitsPixelScreen > 8;
    ReleaseDC(nullptr, dcHandle);

    _color = RGB(0, 0, 0);
    // Shadow extends equally in all directions, centered behind the owner
    _sizeShift.cx = -SHADOW_SPREAD;
    _sizeShift.cy = -SHADOW_SPREAD;
}

//+---------------------------------------------------------------------------
//
// _AdjustWindowPos
//
//----------------------------------------------------------------------------

void CShadowWindow::_AdjustWindowPos()
{
    if (!IsWindow(_GetWnd()) || _pWndOwner==nullptr)
    {
        return;
    }

    HWND hWndOwner = _pWndOwner->_GetWnd();
    RECT rc = {0, 0, 0, 0};

    GetWindowRect(hWndOwner, &rc);
    int ownerW = rc.right - rc.left;
    int ownerH = rc.bottom - rc.top;
    SetWindowPos(_GetWnd(), hWndOwner,
        rc.left + _sizeShift.cx,
        rc.top  + _sizeShift.cy,
        ownerW + SHADOW_SPREAD * 2,
        ownerH + SHADOW_SPREAD * 2,
        SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

//+---------------------------------------------------------------------------
//
// _InitShadow
//
//----------------------------------------------------------------------------

void CShadowWindow::_InitShadow()
{
    typedef struct _RGBAPLHA {
        BYTE rgbBlue;
        BYTE rgbGreen;
        BYTE rgbRed;
        BYTE rgbAlpha;
    } RGBALPHA;

    if (!_isGradient)
        return;

    SetWindowLong(_GetWnd(), GWL_EXSTYLE, (GetWindowLong(_GetWnd(), GWL_EXSTYLE) | WS_EX_LAYERED));

    RECT rcWindow = {0, 0, 0, 0};
    _GetWindowRect(&rcWindow);
    SIZE size;
    size.cx = rcWindow.right - rcWindow.left;
    size.cy = rcWindow.bottom - rcWindow.top;
    if (size.cx <= 0 || size.cy <= 0) return;

    HDC dcScreenHandle = GetDC(nullptr);
    if (!dcScreenHandle) return;

    HDC dcLayeredHandle = CreateCompatibleDC(dcScreenHandle);
    if (!dcLayeredHandle) { ReleaseDC(nullptr, dcScreenHandle); return; }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size.cx;
    bitmapInfo.bmiHeader.biHeight = size.cy;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pDIBits = nullptr;
    HBITMAP bitmapMemHandle = CreateDIBSection(dcScreenHandle, &bitmapInfo, DIB_RGB_COLORS, &pDIBits, nullptr, 0);
    if (!pDIBits || !bitmapMemHandle) {
        ReleaseDC(nullptr, dcScreenHandle);
        DeleteDC(dcLayeredHandle);
        return;
    }

    memset(pDIBits, 0, (size_t)size.cx * size.cy * 4);

    // Sample background at 4 corners of the shadow window to determine shadow color.
    // The shadow window is visible but UpdateLayeredWindow hasn't been called yet,
    // so the screen shows the true background.
    // Shadow color adapts to background: dark shadow on light bg, light shadow on dark bg.
    // Alpha is boosted only when IME theme mismatches background (e.g. dark IME on light bg).
    BOOL isDarkMode = CConfig::GetEffectiveDarkMode();
    BYTE shadowR = isDarkMode ? 200 : 0;
    BYTE shadowG = isDarkMode ? 200 : 0;
    BYTE shadowB = isDarkMode ? 200 : 0;
    BYTE maxAlpha = SHADOW_MAX_ALPHA;
    {
        // Detect background luminance from system window color (instant, no DWM flush).
        // COLOR_WINDOW reflects the system theme. Legacy apps that don't support dark mode
        // keep COLOR_WINDOW light even when system is dark — exactly the mismatch case.
        COLORREF sysWndColor = GetSysColor(COLOR_WINDOW);
        int lum = (299 * GetRValue(sysWndColor) + 587 * GetGValue(sysWndColor) + 114 * GetBValue(sysWndColor)) / 1000;
        BOOL bgIsLight = (lum > 128);
        if (isDarkMode && bgIsLight)
        {
            // Dark IME on light bg: flip to dark shadow with boosted alpha
            shadowR = 0; shadowG = 0; shadowB = 0;
            maxAlpha = SHADOW_MAX_ALPHA_DARK;
        }
        else if (!isDarkMode && !bgIsLight)
        {
            // Light IME on dark bg: flip to light shadow (keep original alpha)
            shadowR = 200; shadowG = 200; shadowB = 200;
        }
    }

    // All-around soft shadow with proper rounded-corner masking.
    int sp = SHADOW_SPREAD;
    int iL = sp, iT = sp, iR = size.cx - sp, iB = size.cy - sp;
    int ownerW = iR - iL;
    int ownerH = iB - iT;

    // Rounded-rect SDF: gives true distance from the owner's rounded boundary at every pixel,
    // including corner cutout areas where the old PtInRegion+dx/dy code zeroed alpha.
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    int ownerRadius = MulDiv(_pWndOwner->_GetCornerRadiusBase(), dpi, USER_DEFAULT_SCREEN_DPI);
    double r      = ownerRadius * 0.5;   // arc radius (CreateRoundRectRgn takes ellipse size)
    double half_w = ownerW * 0.5;
    double half_h = ownerH * 0.5;
    double inner_rx = half_w - r;
    double inner_ry = half_h - r;

    // Border band bounds: shadow pixels only exist within SHADOW_SPREAD+1 of the owner edges.
    // Skip the interior to avoid evaluating SDF for ~80% of pixels.
    int bandT = iT + 2;  // first interior row (no shadow)
    int bandB = iB - 2;  // last interior row (no shadow)
    int bandL = iL + 2;  // first interior column
    int bandR = iR - 2;  // last interior column

    for (int y = 0; y < size.cy; y++) {
        // Skip interior rows (only left+right bands needed)
        if (y >= bandT && y <= bandB)
        {
            // Left band + right band only
            for (int x = 0; x < bandL; x++) {
                double ox = x - iL;
                double oy = y - iT;
                double qx = fabs(ox - half_w) - inner_rx;
                double qy = fabs(oy - half_h) - inner_ry;
                double len = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
                double sdf = len + min(max(qx, qy), 0.0) - r;
                if (sdf <= -1.0) continue;
                double dist = max(0.0, sdf) / (double)sp;
                if (dist > 1.0) continue;
                double f = 1.0 - dist;
                BYTE alpha = (BYTE)(maxAlpha * f * f);
                if (alpha == 0) continue;
                RGBALPHA* ppxl = (RGBALPHA*)pDIBits + ((size_t)y * size.cx + x);
                ppxl->rgbRed   = (BYTE)((DWORD)shadowR * alpha / 255);
                ppxl->rgbGreen = (BYTE)((DWORD)shadowG * alpha / 255);
                ppxl->rgbBlue  = (BYTE)((DWORD)shadowB * alpha / 255);
                ppxl->rgbAlpha = alpha;
            }
            for (int x = max(bandR, bandL); x < size.cx; x++) {
                double ox = x - iL;
                double oy = y - iT;
                double qx = fabs(ox - half_w) - inner_rx;
                double qy = fabs(oy - half_h) - inner_ry;
                double len = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
                double sdf = len + min(max(qx, qy), 0.0) - r;
                if (sdf <= -1.0) continue;
                double dist = max(0.0, sdf) / (double)sp;
                if (dist > 1.0) continue;
                double f = 1.0 - dist;
                BYTE alpha = (BYTE)(maxAlpha * f * f);
                if (alpha == 0) continue;
                RGBALPHA* ppxl = (RGBALPHA*)pDIBits + ((size_t)y * size.cx + x);
                ppxl->rgbRed   = (BYTE)((DWORD)shadowR * alpha / 255);
                ppxl->rgbGreen = (BYTE)((DWORD)shadowG * alpha / 255);
                ppxl->rgbBlue  = (BYTE)((DWORD)shadowB * alpha / 255);
                ppxl->rgbAlpha = alpha;
            }
            continue;
        }
        for (int x = 0; x < size.cx; x++) {
            double ox = x - iL;
            double oy = y - iT;

            // Signed distance from rounded rect boundary: negative = inside, positive = outside
            double qx = fabs(ox - half_w) - inner_rx;
            double qy = fabs(oy - half_h) - inner_ry;
            double len = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
            double sdf = len + min(max(qx, qy), 0.0) - r;

            if (sdf <= -1.0) continue; // extend shadow 1px under candidate edge to close boundary gap

            double dist = max(0.0, sdf) / (double)sp;
            if (dist > 1.0) dist = 1.0;

            double f = 1.0 - dist;
            BYTE alpha = (BYTE)(maxAlpha * f * f);
            if (alpha == 0) continue;

            RGBALPHA* ppxl = (RGBALPHA*)pDIBits + ((size_t)y * size.cx + x);
            // Pre-multiply RGB by alpha/255 as required by AC_SRC_ALPHA
            ppxl->rgbRed   = (BYTE)((DWORD)shadowR * alpha / 255);
            ppxl->rgbGreen = (BYTE)((DWORD)shadowG * alpha / 255);
            ppxl->rgbBlue  = (BYTE)((DWORD)shadowB * alpha / 255);
            ppxl->rgbAlpha = alpha;
        }
    }

    POINT ptSrc = {0, 0};
    BLENDFUNCTION Blend = {};
    Blend.BlendOp = AC_SRC_OVER;
    Blend.SourceConstantAlpha = 255;
    Blend.AlphaFormat = AC_SRC_ALPHA;

    HBITMAP bitmapOldHandle = (HBITMAP)SelectObject(dcLayeredHandle, bitmapMemHandle);
    UpdateLayeredWindow(_GetWnd(), dcScreenHandle, nullptr, &size, dcLayeredHandle, &ptSrc, 0, &Blend, ULW_ALPHA);
    SelectObject(dcLayeredHandle, bitmapOldHandle);

    ReleaseDC(nullptr, dcScreenHandle);
    DeleteDC(dcLayeredHandle);
    DeleteObject(bitmapMemHandle);
}
