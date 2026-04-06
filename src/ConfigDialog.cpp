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
//efine DEBUG_PRINT
#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <MLang.h>
#include <shellscalingapi.h>
#include "Globals.h"
#include "resource.h"
#include "DIME.h"
#include "DictionarySearch.h"
#include "File.h"
#include "TableDictionaryEngine.h"
#include "Aclapi.h"
#include "CompositionProcessorEngine.h"
#ifndef DIMESettings
#include "UIPresenter.h"
#endif
#include <Richedit.h>
#include <string>
#include <vector>
#include <regex>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#include <CommCtrl.h>
#pragma comment(lib, "ComCtl32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif


#pragma comment(lib, "Shlwapi.lib")

// Tracks which palette is currently displayed in the dialog color boxes
static IME_COLOR_MODE _colorDisplayedMode = IME_COLOR_MODE::IME_COLOR_MODE_LIGHT;


// Read the IME_COLOR_MODE stored as item data in the currently selected combo item.
IME_COLOR_MODE CConfig::GetComboSelectedMode(HWND hCombo)
{
	int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
	if (sel == CB_ERR) return IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM;
	LRESULT data = SendMessage(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
	if (data == CB_ERR) return IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM;
	return static_cast<IME_COLOR_MODE>(data);
}

// Per-property-sheet subclass data
struct PropSheetThemeData {
	HBRUSH hBrushBg;
};

static LRESULT CALLBACK PropSheetSubclassProc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	PropSheetThemeData* pData = reinterpret_cast<PropSheetThemeData*>(dwRefData);
	switch (uMsg) {
	case WM_ERASEBKGND:
		if (pData && pData->hBrushBg) {
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect((HDC)wParam, &rc, pData->hBrushBg);
			return 1;
		}
		break;
	case WM_CTLCOLORDLG:
		if (pData && pData->hBrushBg) {
			SetBkColor((HDC)wParam, DARK_DIALOG_BG);
			return (LRESULT)pData->hBrushBg;
		}
		break;
	case WM_CTLCOLORSTATIC:
		if (pData && pData->hBrushBg) {
			SetTextColor((HDC)wParam, DARK_TEXT);
			SetBkColor((HDC)wParam, DARK_DIALOG_BG);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (LRESULT)pData->hBrushBg;
		}
		break;
	case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
			if (pdis && pdis->CtlType == ODT_BUTTON) {
				CConfig::DrawDarkButton(pdis);
				return (LRESULT)TRUE;
			}
			if (pdis && pdis->CtlType == ODT_TAB) {
				HDC hdc = pdis->hDC;
				RECT rc  = pdis->rcItem;
				bool isSelected = (pdis->itemState & ODS_SELECTED) != 0;
				// Selected tab is slightly raised – expand rect to merge with page area
				if (isSelected) InflateRect(&rc, 2, 2);
				COLORREF bgColor = isSelected ? DARK_DIALOG_BG : RGB(50, 50, 53);
				HBRUSH hBr = CreateSolidBrush(bgColor);
				FillRect(hdc, &rc, hBr);
				DeleteObject(hBr);
				// Tab text
				WCHAR text[64] = {0};
				TCITEMW tci = {};
				tci.mask = TCIF_TEXT;
				tci.pszText = text;
				tci.cchTextMax = _countof(text);
				TabCtrl_GetItem(pdis->hwndItem, pdis->itemID, &tci);
				SetTextColor(hdc, DARK_TEXT);
				SetBkMode(hdc, TRANSPARENT);
				HFONT hFont    = (HFONT)SendMessageW(pdis->hwndItem, WM_GETFONT, 0, 0);
				HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
				DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				if (hOldFont) SelectObject(hdc, hOldFont);
				return (LRESULT)TRUE;
			}
		}
		break;
	case WM_NCDESTROY:
		if (pData) {
			if (pData->hBrushBg) DeleteObject(pData->hBrushBg);
			delete pData;
		}
		RemoveWindowSubclass(hWnd, PropSheetSubclassProc, uIdSubclass);
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// PROPSHEETHEADER callback — wire with PSH_USECALLBACK.
int CALLBACK CConfig::PropSheetCallback(HWND hwndDlg, UINT uMsg, LPARAM /*lParam*/)
{
	if (uMsg == PSCB_INITIALIZED) {
		if (!CConfig::IsSystemDarkTheme()) return 0;

		// Dark title bar
		BOOL useDark = TRUE;
		DwmSetWindowAttribute(hwndDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));

		// Allow dark mode for this window (ordinal 133)
		typedef BOOL (WINAPI* FnAllowDarkModeForWindow)(HWND, BOOL);
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnAllowDarkModeForWindow>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
			if (pFn) pFn(hwndDlg, TRUE);
		}

		// Convert push buttons on the property sheet chrome to owner-draw
		CConfig::ApplyDialogDarkTheme(hwndDlg, true);

		// Subclass the property sheet window to handle WM_CTLCOLORxxx / WM_DRAWITEM
		PropSheetThemeData* pData = new (std::nothrow) PropSheetThemeData();
		if (pData) {
			pData->hBrushBg = CreateSolidBrush(DARK_DIALOG_BG);
			SetWindowSubclass(hwndDlg, PropSheetSubclassProc, 1, (DWORD_PTR)pData);
		}
		// Force the host window and all children to repaint with new theme
		RedrawWindow(hwndDlg, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
	}
	return 0;
}

// Apply dark theme to all child controls (buttons, checkboxes, groupboxes, comboboxes)
static BOOL CALLBACK ThemeChildControlsProc(HWND hwndChild, LPARAM lParam)
{
	bool isDark = (lParam != 0);
	WCHAR className[256] = {0};
	GetClassNameW(hwndChild, className, _countof(className));

	// Helper to call AllowDarkModeForWindow (uxtheme ordinal 133)
	auto CallAllowDarkMode = [](HWND hwnd, BOOL allow) {
		typedef BOOL (WINAPI* FnAllowDarkModeForWindow)(HWND, BOOL);
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnAllowDarkModeForWindow>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133))
			);
			if (pFn) pFn(hwnd, allow);
		}
	};

	// Apply dark theme to buttons, checkboxes, radio buttons, groupboxes
	if (_wcsicmp(className, L"Button") == 0) {
		LONG style   = GetWindowLong(hwndChild, GWL_STYLE);
		LONG btnType = style & 0xF;

		// Retrieve stored original button type (stored as origType+1 so 0 == unset)
		LONG origType = (LONG)(ULONG_PTR)GetProp(hwndChild, L"DIME_ORIG_BTNTYPE");
		bool isPushButton = false;
		if (origType) {
			// Already saved: decode
			origType--;
			isPushButton = (origType == BS_PUSHBUTTON || origType == BS_DEFPUSHBUTTON);
		} else {
			// First visit: save original type if it is a push button
			isPushButton = (btnType == BS_PUSHBUTTON || btnType == BS_DEFPUSHBUTTON);
			if (isPushButton) {
				origType = btnType;
				SetProp(hwndChild, L"DIME_ORIG_BTNTYPE", (HANDLE)(ULONG_PTR)(origType + 1));
			}
		}

		if (isPushButton) {
			// Push buttons use BS_OWNERDRAW — AllowDarkMode not needed
			if (isDark) {
				// Replace button type with BS_OWNERDRAW so WM_DRAWITEM is sent
				SetWindowLong(hwndChild, GWL_STYLE, (style & ~0xFL) | BS_OWNERDRAW);
				SetWindowTheme(hwndChild, L"", NULL);
			} else {
				// Restore original push button type and visual theme
				SetWindowLong(hwndChild, GWL_STYLE, (style & ~0xFL) | origType);
				SetWindowTheme(hwndChild, L"Explorer", NULL);
			}
			InvalidateRect(hwndChild, NULL, TRUE);
		} else if (btnType == BS_GROUPBOX) {
			// Groupboxes: the classic Win32 BS_GROUPBOX WM_PAINT resets the DC text colour to
			// GetSysColor(COLOR_WINDOWTEXT) after calling WM_CTLCOLORSTATIC, so our parent
			// handler is ineffective.  We subclass the control and owner-paint the caption
			// and border ourselves in dark colours.
			CallAllowDarkMode(hwndChild, FALSE);
			SetWindowTheme(hwndChild, L"", NULL);
			if (isDark && !GetProp(hwndChild, L"DIME_GB_SUBCLASSED")) {
				struct GroupBoxProc {
					static LRESULT CALLBACK Proc(HWND hWnd, UINT uMsg, WPARAM wParam,
						LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR)
					{
						if (uMsg == WM_PAINT) {
							PAINTSTRUCT ps;
							HDC hdc = BeginPaint(hWnd, &ps);

							RECT rcClient;
							GetClientRect(hWnd, &rcClient);

							// Fill background
							HBRUSH hBgBr = CreateSolidBrush(DARK_DIALOG_BG);
							FillRect(hdc, &rcClient, hBgBr);
							DeleteObject(hBgBr);

							// Get caption
							WCHAR caption[256] = {};
							GetWindowTextW(hWnd, caption, _countof(caption));

							// Measure caption text height to position the frame
							HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
							HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
							SIZE textSize = {};
							if (caption[0])
								GetTextExtentPoint32W(hdc, caption, (int)wcslen(caption), &textSize);
							else
								textSize.cy = 8;

							// Draw the border frame (below the caption mid-line)
							RECT rcBox = rcClient;
							rcBox.top += textSize.cy / 2;
							HPEN hPen = CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
							HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
							HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
							Rectangle(hdc, rcBox.left, rcBox.top, rcBox.right - 1, rcBox.bottom - 1);
							SelectObject(hdc, hOldPen);
							SelectObject(hdc, hOldBr);
							DeleteObject(hPen);

							// Draw caption text, with a small background fill to break the border line
							if (caption[0]) {
								const int textX = 9;
								const int textY = 0;
								RECT rcText = { textX - 2, textY, textX + textSize.cx + 2, textY + textSize.cy };
								HBRUSH hCapBr = CreateSolidBrush(DARK_DIALOG_BG);
								FillRect(hdc, &rcText, hCapBr);
								DeleteObject(hCapBr);
								SetTextColor(hdc, DARK_TEXT);
								SetBkMode(hdc, TRANSPARENT);
								TextOutW(hdc, textX, textY, caption, (int)wcslen(caption));
							}

							if (hOldFont) SelectObject(hdc, hOldFont);
							EndPaint(hWnd, &ps);
							return 0;
						}
						if (uMsg == WM_NCDESTROY) {
							RemoveProp(hWnd, L"DIME_GB_SUBCLASSED");
							RemoveWindowSubclass(hWnd, Proc, uIdSubclass);
						}
						return DefSubclassProc(hWnd, uMsg, wParam, lParam);
					}
				};
				SetProp(hwndChild, L"DIME_GB_SUBCLASSED", (HANDLE)1);
				SetWindowSubclass(hwndChild, GroupBoxProc::Proc, 0, 0);
			}
			InvalidateRect(hwndChild, NULL, TRUE);
		} else if (btnType == BS_RADIOBUTTON || btnType == BS_AUTORADIOBUTTON) {
			// Radio buttons: like groupboxes, the classic BS_AUTORADIOBUTTON WM_PAINT
			// resets the DC text colour to GetSysColor(COLOR_WINDOWTEXT) before drawing
			// the label, so WM_CTLCOLORSTATIC alone is insufficient.
			// Fix: subclass and let the system draw the radio indicator, then overpaint
			// the label text area in DARK_TEXT ourselves.
			CallAllowDarkMode(hwndChild, FALSE);
			SetWindowTheme(hwndChild, isDark ? L"" : L"Explorer", NULL);
			if (isDark && !GetProp(hwndChild, L"DIME_RB_SUBCLASSED")) {
				struct RadioProc {
					static LRESULT CALLBACK Proc(HWND hWnd, UINT uMsg, WPARAM wParam,
						LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR)
					{
						if (uMsg == WM_PAINT) {
							// Let the system paint the radio indicator + text in classic colours
							LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);

							// The system has validated the update region; use GetDC to
							// overpaint the text area with the correct dark colour.
							RECT rc;
							GetClientRect(hWnd, &rc);

							// The radio indicator occupies the left side; its width matches
							// the system check-mark metric.
							int indicatorW = GetSystemMetrics(SM_CXMENUCHECK);
							int gap = 2; // pixels between indicator and text
							RECT rcText = { rc.left + indicatorW + gap, rc.top, rc.right, rc.bottom };

							HDC hdc = GetDC(hWnd);

							// Erase old (black) text with background colour
							HBRUSH hBg = CreateSolidBrush(DARK_DIALOG_BG);
							FillRect(hdc, &rcText, hBg);
							DeleteObject(hBg);

							// Redraw text in dark-mode colour
							WCHAR text[256] = {};
							GetWindowTextW(hWnd, text, _countof(text));
							if (text[0]) {
								HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
								HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
								SetTextColor(hdc, IsWindowEnabled(hWnd) ? DARK_TEXT : DARK_DISABLED_TEXT);
								SetBkMode(hdc, TRANSPARENT);
								DrawTextW(hdc, text, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
								if (hOld) SelectObject(hdc, hOld);
							}

							ReleaseDC(hWnd, hdc);
							return res;
						}
						if (uMsg == WM_NCDESTROY) {
							RemoveProp(hWnd, L"DIME_RB_SUBCLASSED");
							RemoveWindowSubclass(hWnd, Proc, uIdSubclass);
						}
						return DefSubclassProc(hWnd, uMsg, wParam, lParam);
					}
				};
				SetProp(hwndChild, L"DIME_RB_SUBCLASSED", (HANDLE)1);
				SetWindowSubclass(hwndChild, RadioProc::Proc, 0, 0);
			}
			InvalidateRect(hwndChild, NULL, TRUE);
		} else {
			// Checkboxes and other button types: enable AllowDarkMode + DarkMode_Explorer theme
			CallAllowDarkMode(hwndChild, isDark);
			SetWindowTheme(hwndChild, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
		}
	}
	// Apply dark theme to comboboxes
	else if (_wcsicmp(className, L"ComboBox") == 0) {
		CallAllowDarkMode(hwndChild, isDark);
		SetWindowTheme(hwndChild, isDark ? L"DarkMode_CFD" : L"CFD", NULL);
	}
	// Apply dark theme to listviews
	else if (_wcsicmp(className, L"SysListView32") == 0) {
		CallAllowDarkMode(hwndChild, isDark);
		SetWindowTheme(hwndChild, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
	}
	// Apply dark theme to tab controls
	else if (_wcsicmp(className, L"SysTabControl32") == 0) {
		if (isDark) {
			// Owner-draw so the parent (prop-sheet subclass) can paint nubs dark
			LONG style = GetWindowLong(hwndChild, GWL_STYLE);
			if (!(style & TCS_OWNERDRAWFIXED))
				SetWindowLong(hwndChild, GWL_STYLE, style | TCS_OWNERDRAWFIXED);
		} else {
			LONG style = GetWindowLong(hwndChild, GWL_STYLE);
			SetWindowLong(hwndChild, GWL_STYLE, style & ~TCS_OWNERDRAWFIXED);
			SetWindowTheme(hwndChild, L"Explorer", NULL);
		}
		if (isDark && !GetProp(hwndChild, L"DIME_TAB_SUBCLASSED")) {
			struct TabProc {
				static LRESULT CALLBACK Proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
					UINT_PTR uIdSubclass, DWORD_PTR)
				{
					if (uMsg == WM_ERASEBKGND) {
						RECT rc;
						GetClientRect(hWnd, &rc);
						HBRUSH hBr = CreateSolidBrush(DARK_DIALOG_BG);
						FillRect((HDC)wParam, &rc, hBr);
						DeleteObject(hBr);
						return 1;
					}
					if (uMsg == WM_PAINT) {
						LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
						// Overdraw the 3 thin body border strips (left/right/bottom around
						// the page area) that the system draws light even in dark mode.
						// Do NOT touch the tab nub row (top strip) or the page interior.
						RECT rcClient, rcPage;
						GetClientRect(hWnd, &rcClient);
						rcPage = rcClient;
						TabCtrl_AdjustRect(hWnd, FALSE, &rcPage);
						HDC hdc = GetDC(hWnd);
						int saved = SaveDC(hdc);
						// Exclude tab-nub row (top of client down to top of page area)
						ExcludeClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcPage.top);
						// Exclude page interior (covered by property page HWND)
						ExcludeClipRect(hdc, rcPage.left, rcPage.top, rcPage.right, rcPage.bottom);
						// What remains is the 3 border strips: fill them dark
						HBRUSH hBr = CreateSolidBrush(DARK_DIALOG_BG);
						FillRect(hdc, &rcClient, hBr);
						DeleteObject(hBr);
						RestoreDC(hdc, saved);
						ReleaseDC(hWnd, hdc);
						return res;
					}
					if (uMsg == WM_NCDESTROY) {
						RemoveProp(hWnd, L"DIME_TAB_SUBCLASSED");
						RemoveWindowSubclass(hWnd, Proc, uIdSubclass);
					}
					return DefSubclassProc(hWnd, uMsg, wParam, lParam);
				}
			};
			SetWindowSubclass(hwndChild, TabProc::Proc, 2, 0);
			SetProp(hwndChild, L"DIME_TAB_SUBCLASSED", (HANDLE)1);
		}
		InvalidateRect(hwndChild, NULL, TRUE);
	}

	return TRUE;
}

// Draw a push button in dark mode (called from WM_DRAWITEM handlers).
void CConfig::DrawDarkButton(LPDRAWITEMSTRUCT pdis)
{
	if (!pdis) return;
	HDC  hdc = pdis->hDC;
	RECT rc  = pdis->rcItem;

	bool isPressed  = (pdis->itemState & ODS_SELECTED) != 0;
	bool isFocused  = (pdis->itemState & ODS_FOCUS)    != 0;
	bool isDisabled = (pdis->itemState & ODS_DISABLED)  != 0;

	COLORREF bgColor     = isPressed ? DARK_DIALOG_BG : RGB(55, 55, 58);
	COLORREF textColor   = isDisabled ? DARK_DISABLED_TEXT : DARK_TEXT;
	COLORREF borderColor = isFocused  ? RGB(0, 120, 215)   : RGB(80, 80, 80);

	// Background
	HBRUSH hBrush = CreateSolidBrush(bgColor);
	FillRect(hdc, &rc, hBrush);
	DeleteObject(hBrush);

	// Border
	HPEN   hPen    = CreatePen(PS_SOLID, 1, borderColor);
	HPEN   hOldPen = (HPEN)  SelectObject(hdc, hPen);
	HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
	Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
	SelectObject(hdc, hOldPen);
	SelectObject(hdc, hOldBr);
	DeleteObject(hPen);

	// Text
	WCHAR text[256] = {0};
	GetWindowTextW(pdis->hwndItem, text, _countof(text));
	SetTextColor(hdc, textColor);
	SetBkMode(hdc, TRANSPARENT);
	HFONT hFont    = (HFONT)SendMessageW(pdis->hwndItem, WM_GETFONT, 0, 0);
	HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
	RECT rcText = rc;
	InflateRect(&rcText, -4, -4);
	DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (hOldFont) SelectObject(hdc, hOldFont);

	// Focus rectangle
	if (isFocused) {
		RECT rcFocus = rc;
		InflateRect(&rcFocus, -3, -3);
		DrawFocusRect(hdc, &rcFocus);
	}
}

void CConfig::ApplyDialogDarkTheme(HWND hDlg, bool isDark)
{
	// Enable app-wide dark mode preference (undocumented uxtheme ordinal 135)
	// PreferredAppMode: 0=Default, 1=AllowDark, 2=ForceDark, 3=ForceLight
	static bool appModeSet = false;
	if (!appModeSet && isDark) {
		typedef void (WINAPI* FnSetPreferredAppMode)(int);
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnSetPreferredAppMode>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135))
			);
			if (pFn) {
				pFn(1); // AllowDark
				appModeSet = true;
			}
		}
	}

	// Enable dark mode for the dialog window itself
	if (isDark) {
		typedef BOOL (WINAPI* FnAllowDarkModeForWindow)(HWND, BOOL);
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnAllowDarkModeForWindow>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133))
			);
			if (pFn) pFn(hDlg, TRUE);
		}
	}
	// Theme all child controls
	EnumChildWindows(hDlg, ThemeChildControlsProc, (LPARAM)isDark);
	// Force window to redraw with new theme
	if (isDark) {
		InvalidateRect(hDlg, NULL, TRUE);
		UpdateWindow(hDlg);
	}
}

// Lightweight subclass to prevent RichEdit auto select-all on focus and trigger smart validation.
static LRESULT CALLBACK CustomTable_SubclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC oldProc = (WNDPROC)GetProp(hwnd, L"RE_OLDPROC");

	if (uMsg == WM_SETFONT)
	{
		// Log whenever the property sheet framework sends WM_SETFONT to the RichEdit.
		HFONT hF = (HFONT)wParam;
		if (hF)
		{
			LOGFONT lf = {};
			if (GetObject(hF, sizeof(lf), &lf) && lf.lfHeight != 0)
				debugPrint(L"RichEdit WM_SETFONT: %s lfHeight=%d redraw=%d", lf.lfFaceName, lf.lfHeight, (int)lParam);
			else
				debugPrint(L"RichEdit WM_SETFONT: hFont=%p GetObject failed or lfHeight=0", (void*)hF);
		}
		else
			debugPrint(L"RichEdit WM_SETFONT: hFont=NULL");
	}
	else if (uMsg == WM_SETFOCUS)
	{
		// Collapse any selection immediately when control receives focus.
		CHARRANGE cr = { 0, 0 };
		SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
		SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
	}
	else if (uMsg == WM_KEYDOWN)
	{
		// Validate immediately on Enter or Space (key-value separator)
		if (wParam == VK_RETURN || wParam == VK_SPACE)
		{
			HWND hDlg = GetParent(hwnd);
			// Use PostMessage to validate AFTER the key character is inserted
			// Otherwise EN_CHANGE (after key insertion) will clear validation colors
			PostMessageW(hDlg, WM_USER + 1, 0, 0);
			debugPrint(L"Validation scheduled for %s key", (wParam == VK_RETURN) ? L"Enter" : L"Space");
		}
		// Detect large deletions
		else if (wParam == VK_DELETE || wParam == VK_BACK)
		{
			CHARRANGE sel;
			SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);

			if (sel.cpMax - sel.cpMin > CUSTOM_TABLE_LARGE_DELETION_THRESHOLD)
			{
				HWND hDlg = GetParent(hwnd);
				PostMessageW(hDlg, WM_USER + 1, 0, 0);
				debugPrint(L"Large deletion detected: %d chars", sel.cpMax - sel.cpMin);
			}
		}
	}
	else if (uMsg == WM_PASTE)
	{
		// Validate after paste completes
		HWND hDlg = GetParent(hwnd);
		PostMessageW(hDlg, WM_USER + 1, 0, 0);
		debugPrint(L"Paste detected, validation scheduled");
	}
	else if (uMsg == WM_CHAR)
	{
		// Log the char format at cursor position after each typed character
		LRESULT r = CallWindowProc(oldProc, hwnd, uMsg, wParam, lParam);
		CHARFORMAT2W cfNew = {};
		cfNew.cbSize = sizeof(cfNew);
		SendMessageW(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfNew);
		debugPrint(L"WM_CHAR 0x%x: face=%s charset=%d yHeight=%d", (UINT)wParam, cfNew.szFaceName, cfNew.bCharSet, cfNew.yHeight);
		return r;
	}
	else if (uMsg == WM_NCDESTROY)
	{
		// restore original wndproc
		if (oldProc)
		{
			SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)oldProc);
			RemoveProp(hwnd, L"RE_OLDPROC");
			return CallWindowProc(oldProc, hwnd, uMsg, wParam, lParam);
		}
	}

	if (oldProc)
		return CallWindowProc(oldProc, hwnd, uMsg, wParam, lParam);
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void InstallCustomTableSubclass(HWND hEdit)
{
    if (!hEdit) return;
    if (GetProp(hEdit, L"RE_OLDPROC")) return; // already installed
    WNDPROC old = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)CustomTable_SubclassWndProc);
    if (old) SetProp(hEdit, L"RE_OLDPROC", (HANDLE)old);
}

// Note: uninstall is handled by the subclass on WM_NCDESTROY; no explicit caller required.

// (debounce timer removed) validation will run immediately on EN_CHANGE but preserves selection

// Validate each non-empty line in the custom table rich edit control.
// 3-level validation hierarchy:
//  Level 1 (RED): Format error - no key-value pair with separator
//  Level 2 (ORANGE): Key length error - key exceeds maxCodes limit
//  Level 3 (LIGHT RED): Invalid character - character not in composition key range
// On failure, marks errors with appropriate color and returns FALSE.
BOOL CConfig::ValidateCustomTableLines(HWND hDlg, IME_MODE imeMode, CCompositionProcessorEngine* pEngine, UINT maxCodes, bool showAlert)
{
	debugPrint(L"CConfig::ValidateCustomTableLines(), imeMode=%d", (int)imeMode);
    HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
    if (!hEdit) return TRUE;

    // Save current selection/caret so we can restore it when re-validating
    CHARRANGE origSel = {0,0};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&origSel);
    bool movedCaret = false;

    int totalLen = GetWindowTextLengthW(hEdit);
    std::vector<WCHAR> buf((size_t)totalLen + 1);
    if (totalLen > 0) {
        GetWindowTextW(hEdit, buf.data(), totalLen + 1);
    }
    std::wstring text(buf.data());

    // Derive dark/light validation colors from the active IME background color (covers CUSTOM mode too)
    COLORREF bg = CConfig::GetActiveItemBGColor();
    int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000;
    bool bgIsDark = (lum < 128);

    COLORREF errorFormat = bgIsDark ? CUSTOM_TABLE_DARK_ERROR_FORMAT : CUSTOM_TABLE_LIGHT_ERROR_FORMAT;
    COLORREF errorLength = bgIsDark ? CUSTOM_TABLE_DARK_ERROR_LENGTH : CUSTOM_TABLE_LIGHT_ERROR_LENGTH;
    COLORREF errorChar   = bgIsDark ? CUSTOM_TABLE_DARK_ERROR_CHAR   : CUSTOM_TABLE_LIGHT_ERROR_CHAR;
    COLORREF validColor  = bgIsDark ? CUSTOM_TABLE_DARK_VALID        : CUSTOM_TABLE_LIGHT_VALID;

    // Reset all text color to default (light for dark theme, black for light theme)
    CHARFORMAT2W cfClear;
    ZeroMemory(&cfClear, sizeof(cfClear));
    cfClear.cbSize = sizeof(cfClear);
    cfClear.dwMask = CFM_COLOR;
    cfClear.crTextColor = validColor;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfClear);

    BOOL allOk = TRUE;
    int lineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);

    for (int li = 0; li < lineCount; ++li)
    {
        LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, (WPARAM)li, 0);
        if (start == -1) continue;
        LONG len = (LONG)SendMessageW(hEdit, EM_LINELENGTH, (WPARAM)start, 0);
        if (len <= 0) continue;

        // Retrieve the physical line text reliably using EM_GETTEXTRANGE
        // (works consistently for RichEdit regardless of CR/LF handling).
        std::vector<WCHAR> linebuf((size_t)max( (LONG)len, 1L ) + 1);
        TEXTRANGEW tr;
        tr.chrg.cpMin = start;
        tr.chrg.cpMax = start + len;
        tr.lpstrText = linebuf.data();
        LRESULT got = (LRESULT)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        if (got <= 0) continue;
        std::wstring line(linebuf.data(), (size_t)got);

        // trim
        size_t s = 0;
        while (s < line.size() && iswspace(line[s])) ++s;
        size_t e = line.size();
        while (e > s && iswspace(line[e-1])) --e;
        if (s >= e) continue; // empty line -> skip
        std::wstring trimmed = line.substr(s, e - s);

        // Use regex to validate key/value pattern first: key (no whitespace) + whitespace + value
        // Only if regex matches do we further validate the key characters.
        static const std::wregex kv_re(L"^([^\\s]+)\\s+(.+)$");
        std::wsmatch kv_match;
        BOOL valid = TRUE;
        std::wstring key;
        if (!std::regex_match(trimmed, kv_match, kv_re))
        {
            // pattern didn't match (no key/value separator or empty key)
            valid = FALSE;
			debugPrint(L"Line %d regex validation failed: pattern match failed", li);
        }
		else
		{
			key = kv_match[1].str();

			// LEVEL 2: Check key length
			UINT maxKeyLength = (imeMode == IME_MODE::IME_MODE_PHONETIC) ? MAX_KEY_LENGTH : maxCodes;

			if (key.length() > maxKeyLength)
			{
				// Mark entire key ORANGE (warning: key too long)
				allOk = FALSE;
				valid = FALSE;

				LONG keyStart = start + (LONG)s;
				LONG keyEnd = keyStart + (LONG)key.length();

				SendMessageW(hEdit, EM_SETSEL, (WPARAM)keyStart, (LPARAM)keyEnd);
				CHARFORMAT2W cf;
				ZeroMemory(&cf, sizeof(cf));
				cf.cbSize = sizeof(cf);
				cf.dwMask = CFM_COLOR;
				cf.crTextColor = errorLength;  // ORANGE / bright orange
				SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

				debugPrint(L"Line %d: Key too long: %zu > %u (Level 2)", li, key.length(), maxKeyLength);
				continue; // Skip character validation if key already too long
			}

			// LEVEL 3: Validate each character in key
			LONG keyStart = start + (LONG)s;
			bool hasInvalidChar = false;

			if (imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				for (size_t i = 0; i < key.length(); ++i)
				{
					WCHAR c = key[i];
					// Validate: printable ASCII excluding space, AND cannot be '*' or '?'
					if (!(c >= L'!' && c <= L'~') || (c == L'*' || c == L'?'))
					{
						// Mark single invalid character
						allOk = FALSE;
						hasInvalidChar = true;

						LONG charPos = keyStart + (LONG)i;
						SendMessageW(hEdit, EM_SETSEL, (WPARAM)charPos, (LPARAM)(charPos + 1));
						CHARFORMAT2W cf;
						ZeroMemory(&cf, sizeof(cf));
						cf.cbSize = sizeof(cf);
						cf.dwMask = CFM_COLOR;
						cf.crTextColor = errorChar;  // CYAN / cyan-blue
						SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

						debugPrint(L"Line %d: Invalid char '%c' (0x%04X) at pos %zu (Level 3)", li, c, c, i);
					}
				}
			}
			else
			{
				// Quick accept: if the key contains only letters
				bool allAsciiLetters = true;
				for (WCHAR c : key) {
					WCHAR cu = towupper(c);
					if (!(cu >= L'A' && cu <= L'Z')) {
						allAsciiLetters = false;
						break;
					}
				}

				if (allAsciiLetters) {
					valid = TRUE;
					debugPrint(L"Line %d quick accept: all ASCII letters, key = %s", li, key.c_str());
				}
				else
				{
					// Rule 3: validate each character with composition engine if available
					for (size_t i = 0; i < key.length(); ++i)
					{
						WCHAR c = key[i];
						BOOL ok = FALSE;

						if (pEngine)
						{
							// Normal build: use engine's full validation logic
							ok = pEngine->ValidateCompositionKeyCharFull(c) ? TRUE : FALSE;
							debugPrint(L"Line %d engine validation: char '%c' (0x%04X) is %s",
									  li, c, c, ok ? L"valid" : L"invalid");
						}
						else
						{
							// fallback: basic range check same as inline ValidateCompositionKeyChar
							WCHAR cu = towupper(c);
							ok = (cu >= 32 && cu <= 32 + MAX_RADICAL) ? TRUE : FALSE;
							debugPrint(L"Line %d fallback validation: char '%c' (0x%04X) is %s",
									  li, c, c, ok ? L"valid" : L"invalid");
						}

						if (!ok)
						{
							// Mark single invalid character
							allOk = FALSE;
							hasInvalidChar = true;

							LONG charPos = keyStart + (LONG)i;
							SendMessageW(hEdit, EM_SETSEL, (WPARAM)charPos, (LPARAM)(charPos + 1));
							CHARFORMAT2W cf;
							ZeroMemory(&cf, sizeof(cf));
							cf.cbSize = sizeof(cf);
							cf.dwMask = CFM_COLOR;
							cf.crTextColor = errorChar;  // CYAN / cyan-blue
							SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

							debugPrint(L"Line %d: Invalid char '%c' (0x%04X) at pos %zu (Level 3)",
									  li, c, c, i);
						}
					}
				}
			}
		}

		// Level 1: Mark entire line RED only for format errors (regex didn't match)
		// Don't overwrite Level 2 (orange key) or Level 3 (light red chars) marking
		if (!std::regex_match(trimmed, kv_match, kv_re)) {
			allOk = FALSE;
			SendMessageW(hEdit, EM_SETSEL, (WPARAM)start, (LPARAM)(start + len));
			CHARFORMAT2W cf;
			ZeroMemory(&cf, sizeof(cf));
			cf.cbSize = sizeof(cf);
			cf.dwMask = CFM_COLOR;
			cf.crTextColor = errorFormat;  // RED / light red
			SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
		}
	}

	if (!allOk) {
		if (showAlert) {
			// move caret to first error (any color: red/orange/light red)
			for (int li = 0; li < lineCount; ++li) {
				LONG start = (LONG)SendMessageW(hEdit, EM_LINEINDEX, (WPARAM)li, 0);
				if (start == -1) continue;
				LONG len = (LONG)SendMessageW(hEdit, EM_LINELENGTH, (WPARAM)start, 0);
				if (len <= 0) continue;
				CHARRANGE cr = { start, start + 1 };
				SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
				CHARFORMAT2W cfGet;
				ZeroMemory(&cfGet, sizeof(cfGet));
				cfGet.cbSize = sizeof(cfGet);
				SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfGet);
				// Check if color is not black (has any error)
				if (cfGet.crTextColor != validColor) {
					// place caret at line start and bring into view
					SendMessageW(hEdit, EM_SETSEL, (WPARAM)start, (LPARAM)start);
					SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
					movedCaret = true;
					break;
				}
			}
			// Enhanced error message explaining all 3 levels, colors match active theme
			const wchar_t* msgText = bgIsDark
				? L"自建詞庫格式錯誤：\n\n"
				  L"• 淡洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
				  L"• 亮橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
				  L"• 淡紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
				  L"請修正所有標示的錯誤後再套用"
				: L"自建詞庫格式錯誤：\n\n"
				  L"• 洋紅色整行 = 格式錯誤（需要：鍵碼 空格 詞彙）\n"
				  L"• 橙色鍵碼 = 鍵碼過長（超過最大長度限制）\n"
				  L"• 紅色字元 = 無效字元（不在輸入法字根範圍內）\n\n"
				  L"請修正所有標示的錯誤後再套用";
			MessageBoxW(hDlg, msgText, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
		}
        // restore original selection if we did not intentionally move the caret
        if (!movedCaret) {
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&origSel);
        }
        return FALSE;
    }

    // successful validation, restore original selection/caret then clear any selection
    // (some hosts leave the control selected after formatting; ensure no text remains selected)
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&origSel);
    // place caret at original caret position and remove selection
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)origSel.cpMin, (LPARAM)origSel.cpMin);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}


INT_PTR CALLBACK CConfig::CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	HWND hwnd;
	HDC hdc;
	HFONT hFont;
	CHOOSEFONT cf;
	LOGFONT lf = { 0 };
	int fontpoint = 12, fontweight = FW_NORMAL, x, y;
	size_t i;
	BOOL fontitalic = FALSE;
	WCHAR fontname[LF_FACESIZE] = { 0 };
	WCHAR* pwszFontFaceName;
	WCHAR num[16] = { 0 };
	RECT rect;
	POINT pt = { 0,0 };
	UINT sel = 0;
	CHOOSECOLORW cc = { 0 };
	static COLORREF colCust[16];
	PAINTSTRUCT ps;

	HINSTANCE dllDlgHandle = NULL;
	dllDlgHandle = LoadLibrary(L"comdlg32.dll");
	_T_ChooseColor _ChooseColor = NULL;
	_T_ChooseFont _ChooseFont = NULL;
	if (dllDlgHandle)
	{
		_ChooseColor = reinterpret_cast<_T_ChooseColor> (GetProcAddress(dllDlgHandle, "ChooseColorW"));
		_ChooseFont = reinterpret_cast<_T_ChooseFont> (GetProcAddress(dllDlgHandle, "ChooseFontW"));
	}
	IME_MODE imeMode = _imeMode;
	//Retrieve DialogContext* stored in GWLP_USERDATA by caller during WM_INITDIALOG
	if (message != WM_INITDIALOG)
	{
		DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
		if (pCtx)
			imeMode = pCtx->imeMode;
	}

	switch (message)
	{
	case WM_DESTROY:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx) {
				if (pCtx->hBrushBackground) {
					DeleteObject(pCtx->hBrushBackground);
					pCtx->hBrushBackground = nullptr;
				}
				if (pCtx->hBrushEditControl) {
					DeleteObject(pCtx->hBrushEditControl);
					pCtx->hBrushEditControl = nullptr;
				}
				if (pCtx->engineOwned && pCtx->pEngine)
					delete pCtx->pEngine;
				delete pCtx;
				SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
			}
		}
		break;
	case WM_INITDIALOG:
		{
			DialogContext* pCtx = nullptr;
			if (lParam != 0) {
				LPPROPSHEETPAGE psp = (LPPROPSHEETPAGE)lParam;
				if (psp)
					SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)psp->lParam);
				pCtx = (DialogContext*)psp->lParam;
				if (pCtx)
					imeMode = pCtx->imeMode;
			}
			LoadConfig(imeMode);
			ParseConfig(hDlg, imeMode, TRUE);
			// Theme detection and brush creation
			if (pCtx) {
				pCtx->isDarkTheme = CConfig::IsSystemDarkTheme();
				COLORREF bgColor = pCtx->isDarkTheme ? DARK_DIALOG_BG : GetSysColor(COLOR_BTNFACE);
				pCtx->hBrushBackground = CreateSolidBrush(bgColor);
				// Create separate brush for edit controls
				if (pCtx->isDarkTheme) {
					pCtx->hBrushEditControl = CreateSolidBrush(DARK_CONTROL_BG);
				}
				// Apply dark theme to child controls only when dark — property pages are
				// created fresh each time so light-mode controls need no restoration.
				// Calling ApplyDialogDarkTheme(false) triggers SetWindowTheme broadcasts
				// that can flip the DIMESettings title bar.
				if (pCtx->isDarkTheme)
					CConfig::ApplyDialogDarkTheme(hDlg, true);
			}
			ret = TRUE;
		}
		break;
	case WM_CTLCOLORDLG:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx && pCtx->isDarkTheme && pCtx->hBrushBackground) {
				SetBkColor((HDC)wParam, DARK_DIALOG_BG);
				return (INT_PTR)pCtx->hBrushBackground;
			}
		}
		break;
	case WM_CTLCOLORSTATIC:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx && pCtx->isDarkTheme && pCtx->hBrushBackground) {
				SetTextColor((HDC)wParam, DARK_TEXT);
				SetBkColor((HDC)wParam, DARK_DIALOG_BG);
				SetBkMode((HDC)wParam, TRANSPARENT);
				return (INT_PTR)pCtx->hBrushBackground;
			}
		}
		break;
	case WM_CTLCOLOREDIT:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx && pCtx->isDarkTheme) {
				SetTextColor((HDC)wParam, DARK_TEXT);
				SetBkColor((HDC)wParam, DARK_CONTROL_BG);
				if (pCtx->hBrushEditControl) {
					return (INT_PTR)pCtx->hBrushEditControl;
				}
			}
		}
		break;
	case WM_DRAWITEM:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
			if (!pdis) break;
			if (pdis->CtlType == ODT_STATIC)
			{
				// Color box controls: draw solid color, dimmed when disabled
				for (int ci = 0; ci < _countof(colors); ci++)
				{
					if (pdis->hwndItem == GetDlgItem(hDlg, colors[ci].id))
					{
						COLORREF col = colors[ci].color;
						if (pdis->itemState & ODS_DISABLED)
							col = RGB((GetRValue(col) + 255) / 2,
							          (GetGValue(col) + 255) / 2,
							          (GetBValue(col) + 255) / 2);
						SelectObject(pdis->hDC, GetStockObject(BLACK_PEN));
						SetDCBrushColor(pdis->hDC, col);
						SelectObject(pdis->hDC, GetStockObject(DC_BRUSH));
						Rectangle(pdis->hDC,
						          pdis->rcItem.left, pdis->rcItem.top,
						          pdis->rcItem.right, pdis->rcItem.bottom);
						return (INT_PTR)TRUE;
					}
				}
			}
			if (pdis->CtlType == ODT_BUTTON && pCtx && pCtx->isDarkTheme) {
				CConfig::DrawDarkButton(pdis);
				return (INT_PTR)TRUE;
			}
		}
		break;
	case WM_THEMECHANGED:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx) {
				pCtx->isDarkTheme = CConfig::IsSystemDarkTheme();
				if (pCtx->hBrushBackground) {
					DeleteObject(pCtx->hBrushBackground);
					pCtx->hBrushBackground = nullptr;
				}
				if (pCtx->hBrushEditControl) {
					DeleteObject(pCtx->hBrushEditControl);
					pCtx->hBrushEditControl = nullptr;
				}
				COLORREF bgColor = pCtx->isDarkTheme ? DARK_DIALOG_BG : GetSysColor(COLOR_BTNFACE);
				pCtx->hBrushBackground = CreateSolidBrush(bgColor);
				if (pCtx->isDarkTheme) {
					pCtx->hBrushEditControl = CreateSolidBrush(DARK_CONTROL_BG);
				}
				CConfig::ApplyDialogDarkTheme(hDlg, pCtx->isDarkTheme);
				HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
				if (pCtx->isDarkTheme && hEdit) {
					SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, DARK_CONTROL_BG);
					SetWindowTheme(hEdit, L"DarkMode_Explorer", NULL);
				} else if (hEdit) {
					SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
					SetWindowTheme(hEdit, L"Explorer", NULL);
				}
				if (pCtx->pEngine) {
					ValidateCustomTableLines(hDlg, pCtx->imeMode, pCtx->pEngine, pCtx->maxCodes, false);
				}
				InvalidateRect(hDlg, NULL, TRUE);
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON_CHOOSEFONT:

			hdc = GetDC(hDlg);
			{
			int _logPixelY = GetDeviceCaps(hdc, LOGPIXELSY);
			if (_GetDpiForMonitor)
			{
				HMONITOR _monitor = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
				UINT monDpiX, monDpiY;
				_GetDpiForMonitor(_monitor, MDT_EFFECTIVE_DPI, &monDpiX, &monDpiY);
				if (monDpiY > 0) _logPixelY = monDpiY;
			}

			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			lf.lfHeight = -MulDiv(GetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, NULL, FALSE), _logPixelY, 72);
			lf.lfCharSet = DEFAULT_CHARSET;

			ZeroMemory(&cf, sizeof(cf));
			cf.lStructSize = sizeof(CHOOSEFONT);
			cf.hwndOwner = hDlg;
			cf.lpLogFont = &lf;
			cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;// should not include CF_SELECTSCRIPT so as user can change the characterset

			if (_ChooseFont && ((*_ChooseFont)(&cf) == TRUE))
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);

				SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, lf.lfFaceName);
				lf.lfHeight = -MulDiv(10, _logPixelY, 72);
				fontweight = lf.lfWeight;
				fontitalic = lf.lfItalic;
				hFont = CreateFontIndirect(&lf);
				SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
				SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, cf.iPointSize / 10, FALSE);
			}

			}
			ReleaseDC(hDlg, hdc);
			ret = TRUE;
			break;
		case IDC_BUTTON_RESTOREDEFAULT:
			wcsncpy_s(fontname, { L"微軟正黑體" }, _TRUNCATE);

			fontpoint = 12;
			fontweight = FW_NORMAL;
			fontitalic = FALSE;
			SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, fontname);
			hdc = GetDC(hDlg);
			{
			int _logPixelY = GetDeviceCaps(hdc, LOGPIXELSY);
			if (_GetDpiForMonitor)
			{
				HMONITOR _monitor = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
				UINT monDpiX, monDpiY;
				_GetDpiForMonitor(_monitor, MDT_EFFECTIVE_DPI, &monDpiX, &monDpiY);
				if (monDpiY > 0) _logPixelY = monDpiY;
			}
			hFont = CreateFont(-MulDiv(10, _logPixelY, 72), 0, 0, 0,
				fontweight, fontitalic, FALSE, FALSE, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, fontname);
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
			}
			ReleaseDC(hDlg, hdc);
			SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, fontpoint, FALSE);


			// Reset only the currently displayed palette to factory defaults
			switch (_colorDisplayedMode)
			{
			case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:
				_lightItemColor       = CANDWND_ITEM_COLOR;
				_lightPhraseColor     = CANDWND_PHRASE_COLOR;
				_lightNumberColor     = CANDWND_NUM_COLOR;
				_lightItemBGColor     = GetSysColor(COLOR_3DHIGHLIGHT);
				_lightSelectedColor   = CANDWND_SELECTED_ITEM_COLOR;
				_lightSelectedBGColor = CANDWND_SELECTED_BK_COLOR;
				break;
			case IME_COLOR_MODE::IME_COLOR_MODE_DARK:
				_darkItemColor       = CANDWND_DARK_ITEM_COLOR;
				_darkPhraseColor     = CANDWND_DARK_PHRASE_COLOR;
				_darkNumberColor     = CANDWND_DARK_NUM_COLOR;
				_darkItemBGColor     = CANDWND_DARK_ITEM_BG_COLOR;
				_darkSelectedColor   = CANDWND_DARK_SELECTED_COLOR;
				_darkSelectedBGColor = CANDWND_DARK_SELECTED_BG_COLOR;
				break;
			case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:
				_itemColor       = CANDWND_ITEM_COLOR;
				_phraseColor     = CANDWND_PHRASE_COLOR;
				_numberColor     = CANDWND_NUM_COLOR;
				_itemBGColor     = GetSysColor(COLOR_3DHIGHLIGHT);
				_selectedColor   = CANDWND_SELECTED_ITEM_COLOR;
				_selectedBGColor = CANDWND_SELECTED_BK_COLOR;
				break;
			case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
			default:
				break; // font already reset above; no palette to reset for SYSTEM
			}
			LoadColorsForMode(_colorDisplayedMode);

			for (i = 0; i < _countof(colors); i++)
				InvalidateRect(GetDlgItem(hDlg, colors[i].id), NULL, TRUE);
			PropSheet_Changed(GetParent(hDlg), hDlg);
			ret = TRUE;
			break;
		case IDC_COMBO_COLOR_MODE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				{
					HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
					IME_COLOR_MODE newMode = GetComboSelectedMode(hCombo);
					// Save the current color boxes back to the palette that was displayed
					SaveColorsForMode(_colorDisplayedMode);
					// Switch color boxes to the new mode's palette
					LoadColorsForMode(newMode);
					_colorDisplayedMode = newMode;
					{
						BOOL enableColors = (newMode != IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM) ? TRUE : FALSE;
						for (int ci = 0; ci < _countof(colors); ci++)
							EnableWindow(GetDlgItem(hDlg, colors[ci].id), enableColors);
						static const int labelIds[] = {
							IDC_LABEL_COL_FR, IDC_LABEL_COL_SEFR, IDC_LABEL_COL_BG,
							IDC_LABEL_COL_PHRASE, IDC_LABEL_COL_NU, IDC_LABEL_COL_SEBG
						};
						for (int id : labelIds)
							EnableWindow(GetDlgItem(hDlg, id), enableColors);
					}
					for (int ci = 0; ci < _countof(colors); ci++)
					InvalidateRect(GetDlgItem(hDlg, colors[ci].id), NULL, TRUE);
					PropSheet_Changed(GetParent(hDlg), hDlg);
					ret = TRUE;
				}
				break;
			}
			break;

		case IDC_EDIT_MAXWIDTH:
			switch (HIWORD(wParam))
			{
			case EN_CHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;


		case IDC_COMBO_IME_SHIFT_MODE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_DOUBLE_SINGLE_BYTE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_REVERSE_CONVERSION:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				_reloadReverseConversion = TRUE;
				break;
			default:
				break;
			}
			break;

		case IDC_COMBO_ARRAY_SCOPE:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
				_arrayScope = (ARRAY_SCOPE)SendMessage(hwnd, CB_GETITEMDATA, SendMessage(hwnd, CB_GETCURSEL, 0, 0), 0);
				//reset autocompose mode if ARRAY_SCOPE::ARRAY40 is selected.
				if (_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)
				{
					_autoCompose = FALSE;
					_spaceAsPageDown = TRUE;
					CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, BST_UNCHECKED);
					CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_CHECKED);
				}
				else // autocompose is always true in ARRAY30
					_autoCompose = TRUE;

				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE),
					(_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);

				debugPrint(L"selected array scope item is %d", _arrayScope);
				break;
			default:
				break;
			}
			break;
		case IDC_COMBO_CHARSET_SCOPE:
			if (HIWORD(wParam) == CBN_SELCHANGE)
			{
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
			}
			break;
		case IDC_COMBO_NUMERIC_PAD:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;
		case IDC_COMBO_PHONETIC_KEYBOARD:
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE:
				PropSheet_Changed(GetParent(hDlg), hDlg);
				ret = TRUE;
				break;
			default:
				break;
			}
			break;
		case IDC_CHECKBOX_AUTOCOMPOSE:
			if(IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED)
			{
				_spaceAsPageDown = FALSE;
				CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_UNCHECKED);
			}
			else
			{
				_spaceAsPageDown = TRUE;
				CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, BST_CHECKED);
			}
		case IDC_CHECKBOX_CLEAR_ONBEEP:
		case IDC_CHECKBOX_DOBEEP:
		case IDC_CHECKBOX_DOBEEPNOTIFY:
		case IDC_RADIO_KEYBOARD_OPEN:
		case IDC_RADIO_KEYBOARD_CLOSE:
		case IDC_RADIO_OUTPUT_CHT:
		case IDC_RADIO_OUTPUT_CHS:
		case IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY:
		case IDC_CHECKBOX_DAYIARTICLEMODE:
		case IDC_CHECKBOX_ARRAY_FORCESP:
		case IDC_CHECKBOX_ARRAY_NOTIFYSP:
		case IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE:
		case IDC_CHECKBOX_PHRASE:
		case IDC_CHECKBOX_ARROWKEYSWPAGES:
		case IDC_CHECKBOX_SPACEASPAGEDOWN:
		case IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY:
		case IDC_CHECKBOX_SHOWNOTIFY:
			PropSheet_Changed(GetParent(hDlg), hDlg);
			ret = TRUE;
			break;
		case IDOK:
			WriteConfig(imeMode, TRUE);
			LoadConfig(imeMode); // refresh _initTimeStamp after our own write
			ret = TRUE;
			break;
		default:
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		if (_colorDisplayedMode == IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM) break; // no color picking in system-follow mode
		for (i = 0; i < _countof(colors); i++)
		{
			hwnd = GetDlgItem(hDlg, colors[i].id);
			GetWindowRect(hwnd, &rect);
			pt.x = x = GET_X_LPARAM(lParam);
			pt.y = y = GET_Y_LPARAM(lParam);
			ClientToScreen(hDlg, &pt);

			if (rect.left <= pt.x && pt.x <= rect.right &&
				rect.top <= pt.y && pt.y <= rect.bottom)
			{
				cc.lStructSize = sizeof(cc);
				cc.hwndOwner = hDlg;
				cc.hInstance = NULL;
				cc.rgbResult = colors[i].color;
				cc.lpCustColors = colCust;
				cc.Flags = CC_FULLOPEN | CC_RGBINIT;
				cc.lCustData = NULL;
				cc.lpfnHook = NULL;
				cc.lpTemplateName = NULL;

				if (_ChooseColor && ((*_ChooseColor)(&cc)))
				{
					colors[i].color = cc.rgbResult;
					InvalidateRect(hwnd, NULL, TRUE);
					PropSheet_Changed(GetParent(hDlg), hDlg);
					ret = TRUE;
				}
				break;
			}
		}
		break;

	case WM_PAINT:
		hdc = BeginPaint(hDlg, &ps);
		EndPaint(hDlg, &ps);
		ret = TRUE;
		break;





	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:
			_autoCompose = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_AUTOCOMPOSE) == BST_CHECKED;
			_customTablePriority = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY) == BST_CHECKED;
			_dayiArticleMode = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE) == BST_CHECKED;
			_clearOnBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_CLEAR_ONBEEP) == BST_CHECKED;
			_doBeep = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP) == BST_CHECKED;
			_doBeepNotify = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY) == BST_CHECKED;
			_doBeepOnCandi = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_DOBEEP_CANDI) == BST_CHECKED;
			_makePhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_PHRASE) == BST_CHECKED;
			_activatedKeyboardMode = IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN) == BST_CHECKED;
			_doHanConvert = IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS) == BST_CHECKED;
			_showNotifyDesktop = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SHOWNOTIFY) == BST_CHECKED;
			_spaceAsPageDown = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN) == BST_CHECKED;
			_spaceAsFirstCandSelkey = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY) == BST_CHECKED;
			_arrowKeySWPages = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES) == BST_CHECKED;
			_arrayForceSP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_FORCESP) == BST_CHECKED;
			_arrayNotifySP = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP) == BST_CHECKED;
			_arraySingleQuoteCustomPhrase = IsDlgButtonChecked(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE) == BST_CHECKED;


			GetDlgItemText(hDlg, IDC_EDIT_MAXWIDTH, num, _countof(num));
			_maxCodes = _wtol(num);

			GetDlgItemText(hDlg, IDC_EDIT_FONTPOINT, num, _countof(num));
			_fontSize = _wtol(num);

			hFont = (HFONT)SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_GETFONT, 0, 0);
			GetObject(hFont, sizeof(LOGFONT), &lf);
			_fontWeight = lf.lfWeight;
			_fontItalic = lf.lfItalic;

		pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
		if (pwszFontFaceName)
		{
			GetDlgItemText(hDlg, IDC_EDIT_FONTNAME, pwszFontFaceName, LF_FACESIZE);
			if (_pFontFaceName)
				StringCchCopy(_pFontFaceName, LF_FACESIZE, pwszFontFaceName);
			delete[] pwszFontFaceName;
		}

			hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
			_colorMode = GetComboSelectedMode(hwnd);
			SaveColorsForMode(_colorMode);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_IME_SHIFT_MODE);
			_imeShiftMode = (IME_SHIFT_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected IME shift hotkey mode is %d", _imeShiftMode);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_DOUBLE_SINGLE_BYTE);
			_doubleSingleByteMode = (DOUBLE_SINGLE_BYTE_MODE)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected double single byte mode is %d", _doubleSingleByteMode);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_NUMERIC_PAD);
			_numericPad = (NUMERIC_PAD)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected Numeric pad mode is %d", _numericPad);

			hwnd = GetDlgItem(hDlg, IDC_COMBO_REVERSE_CONVERSION);
			sel = (UINT)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			debugPrint(L"selected reverse conversion item is %d", sel);
			SetReverseConversionSelection(sel);

			if (imeMode == IME_MODE::IME_MODE_ARRAY)
			{
				hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
				_arrayScope = (ARRAY_SCOPE)SendMessage(hwnd, CB_GETITEMDATA, SendMessage(hwnd, CB_GETCURSEL, 0, 0), 0);
				if (_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5)
					_autoCompose = TRUE;

				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)?SW_HIDE:SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP),
					(_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE),
					(_arrayScope != ARRAY_SCOPE::ARRAY40_BIG5) ? SW_HIDE : SW_SHOW);

				debugPrint(L"selected array scope item is %d", _arrayScope);
			}

			if (imeMode != IME_MODE::IME_MODE_ARRAY)
			{
				hwnd = GetDlgItem(hDlg, IDC_COMBO_CHARSET_SCOPE);
				_big5Filter = (SendMessage(hwnd, CB_GETCURSEL, 0, 0) == 1) ? TRUE : FALSE;
			}

			if (imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				hwnd = GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD);
				_phoneticKeyboardLayout = (PHONETIC_KEYBOARD_LAYOUT)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
				debugPrint(L"selected phonetic keyboard layout is %d", _phoneticKeyboardLayout);
			}


			WriteConfig(imeMode, TRUE);
			LoadConfig(imeMode); // refresh _initTimeStamp after our own write
			ParseConfig(hDlg, imeMode, FALSE);
			ret = TRUE;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	FreeLibrary(dllDlgHandle);
	return ret;


}


INT_PTR CALLBACK CConfig::DictionaryPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL ret = FALSE;
	//HWND hwnd;
	OPENFILENAMEW ofn;


	HINSTANCE dllDlgHandle = NULL;
	dllDlgHandle = LoadLibrary(L"comdlg32.dll");
	_T_GetOpenFileName _GetOpenFileName = NULL;
	_T_GetSaveFileName _GetSaveFileName = NULL;

	WCHAR customTableName[MAX_PATH] = L"\0";
	WCHAR targetName[MAX_PATH] = L"\0";
	WCHAR wszAppData[MAX_PATH] = L"\0";
	WCHAR wszUserDoc[MAX_PATH] = L"\0";
	WCHAR pathToLoad[MAX_PATH] = L"\0";
	WCHAR pathToWrite[MAX_PATH] = L"\0";

	IME_MODE imeMode = _imeMode;
	// Retrieve DialogContext* stored in GWLP_USERDATA by caller during WM_INITDIALOG
	if (message != WM_INITDIALOG)
	{
		DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
		if (pCtx)
			imeMode = pCtx->imeMode;
	}

	enum {
		LOAD_CIN_TABLE,
		IMPORT_CUSTOM_TABLE,
		EXPORT_CUSTOM_TABLE,
		OPEN_NULL
	}openFileType = OPEN_NULL;

	//CSIDL_APPDATA  personal roaming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);
	SHGetSpecialFolderPath(NULL, wszUserDoc, CSIDL_MYDOCUMENTS, TRUE);

	if (dllDlgHandle)
	{
		_GetOpenFileName = reinterpret_cast<_T_GetOpenFileName> (GetProcAddress(dllDlgHandle, "GetOpenFileNameW"));
		_GetSaveFileName = reinterpret_cast<_T_GetSaveFileName> (GetProcAddress(dllDlgHandle, "GetSaveFileNameW"));
	}

	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
			InstallCustomTableSubclass(hEdit);
			imeMode = _imeMode; // fallback
			DialogContext* pCtx = nullptr;
			if (lParam != 0) {
				LPPROPSHEETPAGE psp = (LPPROPSHEETPAGE)lParam;
				if (psp)
					SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)psp->lParam);
				pCtx = (DialogContext*)psp->lParam;
				if (pCtx)
					imeMode = pCtx->imeMode;
			}
			LoadConfig(imeMode);
			// Theme detection and brush creation
			if (pCtx) {
				pCtx->isDarkTheme = CConfig::IsSystemDarkTheme();
				COLORREF bgColor = pCtx->isDarkTheme ? DARK_DIALOG_BG : GetSysColor(COLOR_BTNFACE);
				pCtx->hBrushBackground = CreateSolidBrush(bgColor);
				// Create separate brush for controls and buttons
				if (pCtx->isDarkTheme) {
					pCtx->hBrushEditControl = CreateSolidBrush(DARK_CONTROL_BG);
				}
				// RichEdit dark background and scrollbars
				if (pCtx->isDarkTheme && hEdit) {
					SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, DARK_CONTROL_BG);
					SetWindowTheme(hEdit, L"DarkMode_Explorer", NULL);
				}
				// Apply dark theme to child controls only in dark mode.
				// In light mode, calling ApplyDialogDarkTheme calls SetWindowTheme on
				// every child which broadcasts WM_THEMECHANGED to all top-level windows.
				if (pCtx->isDarkTheme) {
					CConfig::ApplyDialogDarkTheme(hDlg, true);
					// Dark title bar for parent property sheet (Windows 10 20H1+).
					// NOTE: GetParent(propertySheet) returns the owner window (DIMESettings).
					// Guard with isDarkTheme: without the guard this unconditionally sets
					// DWMWA_USE_IMMERSIVE_DARK_MODE=TRUE on DIMESettings in light mode.
					HWND hSheet = GetParent(hDlg);
					if (hSheet) hSheet = GetParent(hSheet);
					if (hSheet) {
						BOOL useDark = TRUE;
						DwmSetWindowAttribute(hSheet, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
						// Apply dark theme to property sheet buttons (OK, Cancel, Apply)
						CConfig::ApplyDialogDarkTheme(hSheet, true);
					}
				}
			}
			if (imeMode == IME_MODE::IME_MODE_DAYI)
				StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-Custom.txt");
			else if (imeMode == IME_MODE::IME_MODE_ARRAY)
				StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-Custom.txt");
			else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
				StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-Custom.txt");
			else if (imeMode == IME_MODE::IME_MODE_GENERIC)
				StringCchPrintf(customTableName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-Custom.txt");
			importCustomTableFile(hDlg, customTableName);
			// Initialize smart validation tracking
			if (pCtx)
			{
				pCtx->lastEditedLine = -1;
				pCtx->keystrokesSinceValidation = 0;
				hEdit = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
				if (hEdit)
				{
					pCtx->lastLineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
				// Set RichEdit text color to light for dark theme
				if (pCtx->isDarkTheme) {
					CHARFORMAT2W cf;
					ZeroMemory(&cf, sizeof(cf));
					cf.cbSize = sizeof(cf);
					cf.dwMask = CFM_COLOR;
					cf.crTextColor = DARK_TEXT;
					SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
				}
				}
			}
			// Enable EN_CHANGE notifications for Rich Edit control (not sent by default unlike standard edit controls)
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE), EM_SETEVENTMASK, 0, ENM_CHANGE);
			// Enable EN_CHANGE notifications for Rich Edit control (not sent by default unlike standard edit controls)
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE), EM_SETEVENTMASK, 0, ENM_CHANGE);
			_customTableChanged = FALSE;

			if (!(_loadTableMode || imeMode == IME_MODE::IME_MODE_GENERIC))
			{
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_MAIN), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_PHRASE), SW_HIDE);
			}
			if (!(_loadTableMode && imeMode == IME_MODE::IME_MODE_ARRAY))
			{
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SC), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_SP), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_B), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_CD), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_EXT_EFG), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY_PHRASE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BUTTON_LOAD_ARRAY40), SW_HIDE);
			}
			if (imeMode == IME_MODE::IME_MODE_PHONETIC)
			{
				SetDlgItemText(hDlg, IDC_STATIC,
					L"自建詞庫: 注音模式下需以\\鍵開啓自建詞輸入模式 \n每組自訂字詞一行，以空白間隔輸入碼與自訂字詞");
			}
			ret = TRUE;
		}
		break;

	case WM_CTLCOLORDLG:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx && pCtx->isDarkTheme && pCtx->hBrushBackground) {
				SetBkColor((HDC)wParam, DARK_DIALOG_BG);
				return (INT_PTR)pCtx->hBrushBackground;
			}
		}
		break;

	case WM_CTLCOLORSTATIC:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx && pCtx->isDarkTheme && pCtx->hBrushBackground) {
				SetTextColor((HDC)wParam, DARK_TEXT);
				SetBkColor((HDC)wParam, DARK_DIALOG_BG);
				SetBkMode((HDC)wParam, TRANSPARENT);
				return (INT_PTR)pCtx->hBrushBackground;
			}
		}
		break;

	case WM_DRAWITEM:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
			if (pdis && pdis->CtlType == ODT_BUTTON && pCtx && pCtx->isDarkTheme) {
				CConfig::DrawDarkButton(pdis);
				return (INT_PTR)TRUE;
			}
		}
		break;

	case WM_THEMECHANGED:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx) {
				pCtx->isDarkTheme = CConfig::IsSystemDarkTheme();
				if (pCtx->hBrushBackground) {
					DeleteObject(pCtx->hBrushBackground);
					pCtx->hBrushBackground = nullptr;
				}
				if (pCtx->hBrushEditControl) {
					DeleteObject(pCtx->hBrushEditControl);
					pCtx->hBrushEditControl = nullptr;
				}
				COLORREF bgColor = pCtx->isDarkTheme ? DARK_DIALOG_BG : GetSysColor(COLOR_BTNFACE);
				pCtx->hBrushBackground = CreateSolidBrush(bgColor);
				if (pCtx->isDarkTheme) {
					pCtx->hBrushEditControl = CreateSolidBrush(DARK_CONTROL_BG);
				}
				// Apply dark theme to all child controls
				CConfig::ApplyDialogDarkTheme(hDlg, pCtx->isDarkTheme);
				if (pCtx->pEngine) {
					ValidateCustomTableLines(hDlg, pCtx->imeMode, pCtx->pEngine, pCtx->maxCodes, false);
				}
				// Set RichEdit text color for dark theme
				HWND hEditTheme = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
				if (pCtx->isDarkTheme && hEditTheme) {
					CHARFORMAT2W cf;
					ZeroMemory(&cf, sizeof(cf));
					cf.cbSize = sizeof(cf);
					cf.dwMask = CFM_COLOR;
					cf.crTextColor = DARK_TEXT;
					SendMessageW(hEditTheme, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
				}
				InvalidateRect(hDlg, NULL, TRUE);
			}
		}
		break;

	case WM_DESTROY:
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx) {
				if (pCtx->hBrushBackground) {
					DeleteObject(pCtx->hBrushBackground);
					pCtx->hBrushBackground = nullptr;
				}
				if (pCtx->hBrushEditControl) {
					DeleteObject(pCtx->hBrushEditControl);
					pCtx->hBrushEditControl = nullptr;
				}
				if (pCtx->engineOwned && pCtx->pEngine)
					delete pCtx->pEngine;
				delete pCtx;
				SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON_LOAD_MAIN:
			openFileType = LOAD_CIN_TABLE;
			if (imeMode == IME_MODE::IME_MODE_DAYI)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Dayi.cin");
			else if (imeMode == IME_MODE::IME_MODE_ARRAY)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array.cin");
			else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phone.cin");
			else if (imeMode == IME_MODE::IME_MODE_GENERIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Generic.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_PHRASE:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Phrase.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SC:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-shortcode.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_SP:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-special.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_B:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-B.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_CD:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-CD.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_EXT_EFG:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Ext-EF.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY40:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array40.cin");
			goto LoadFile;
		case IDC_BUTTON_LOAD_ARRAY_PHRASE:
			openFileType = LOAD_CIN_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\DIME\\Array-Phrase.cin");
			goto LoadFile;
		case IDC_BUTTON_EXPORT_CUSTOM:
			openFileType = EXPORT_CUSTOM_TABLE;
			StringCchCopy(targetName, MAX_PATH, L"\\CUSTOM.txt");
			goto LoadFile;
		case IDC_BUTTON_IMPORT_CUSTOM:
			openFileType = IMPORT_CUSTOM_TABLE;
			_customTableChanged = TRUE;
			if (imeMode == IME_MODE::IME_MODE_DAYI)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\DAYI-CUSTOM.txt");
			else if (imeMode == IME_MODE::IME_MODE_ARRAY)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\ARRAY-CUSTOM.txt");
			else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\PHONETIC-CUSTOM.txt");
			else
				StringCchCopy(targetName, MAX_PATH, L"\\DIME\\GENERIC-CUSTOM.txt");
			goto LoadFile;
		LoadFile:
			pathToLoad[0] = L'\0';
			if (openFileType == EXPORT_CUSTOM_TABLE)
				StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszUserDoc, L"\\customTable.txt");
			//else
			//StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszUserDoc, L"\\");
			ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
			ofn.lStructSize = sizeof(OPENFILENAMEW);
			ofn.hwndOwner = hDlg;
			ofn.lpstrFile = pathToLoad;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = (openFileType != EXPORT_CUSTOM_TABLE) ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT;
			ofn.lpstrFilter = (openFileType == LOAD_CIN_TABLE) ? L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0" : L"詞庫文字檔(*.txt)\0*.txt\0\0";


			if (openFileType == EXPORT_CUSTOM_TABLE && _GetSaveFileName && (_GetSaveFileName)(&ofn) != 0)
			{
				exportCustomTableFile(hDlg, imeMode, pathToLoad);
			}
			else if (openFileType != EXPORT_CUSTOM_TABLE  && _GetOpenFileName && (_GetOpenFileName)(&ofn) != 0)
			{
				//PropSheet_Changed(GetParent(hDlg), hDlg);
				debugPrint(L"file name: %s selected", pathToLoad);

				StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, targetName);

				if (openFileType == IMPORT_CUSTOM_TABLE)
				{
					importCustomTableFile(hDlg, pathToLoad);
				}
				else
				{
					if (parseCINFile(pathToLoad, pathToWrite))
						MessageBox(GetFocus(), L"自訂碼表載入完成。", L"DIME 自訂碼表", MB_ICONINFORMATION);
					else
						MessageBox(GetFocus(), L"自訂碼表載入發生錯誤 !!", L"DIME 自訂碼表", MB_ICONERROR);


				}

			}
			break;
		case IDC_EDIT_CUSTOM_TABLE:
			switch (HIWORD(wParam))
			{
			case EN_CHANGE:
				{
					PropSheet_Changed(GetParent(hDlg), hDlg);
					_customTableChanged = TRUE;

					HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);

					// Smart validation logic (colors cleared inside ValidateCustomTableLines)
					DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					if (pCtx && hEdit)
					{
						// Get current caret line number (pass -1 to get line containing caret)
						CHARRANGE sel;
						SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
						int currentLine = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, (WPARAM)sel.cpMin, 0);
						int currentLineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);

						// DETECT STRUCTURAL CHANGES (delete/paste multiple lines)
						bool structuralChange = false;
						if (pCtx->lastLineCount > 0 && currentLineCount != pCtx->lastLineCount)
						{
							structuralChange = true;
							debugPrint(L"Structural change: %d lines → %d lines", pCtx->lastLineCount, currentLineCount);
						}

						// Update tracked line count
						pCtx->lastLineCount = currentLineCount;

						if (structuralChange)
						{
							// Validate immediately on paste/delete
							CConfig::ValidateCustomTableLines(hDlg, imeMode, pCtx->pEngine, pCtx->maxCodes, false);
							pCtx->keystrokesSinceValidation = 0;
							pCtx->lastEditedLine = currentLine;
						}
						else
						{
							// Normal typing flow
							pCtx->keystrokesSinceValidation++;

							// User moved to different line?
							if (currentLine != pCtx->lastEditedLine)
							{
								// Validate previous line
								if (pCtx->lastEditedLine >= 0)
								{
									CConfig::ValidateCustomTableLines(hDlg, imeMode, pCtx->pEngine, pCtx->maxCodes, false);
								}
								pCtx->lastEditedLine = currentLine;
								pCtx->keystrokesSinceValidation = 0;
							}
							// Validate every N keystrokes on same line
							else if (pCtx->keystrokesSinceValidation >= CUSTOM_TABLE_VALIDATE_KEYSTROKE_THRESHOLD)
							{
								CConfig::ValidateCustomTableLines(hDlg, imeMode, pCtx->pEngine, pCtx->maxCodes, false);
								pCtx->keystrokesSinceValidation = 0;
							}
						}
					}

					ret = TRUE;
				}
				break;
			default:
				break;
			}
			break;

		default:
			break;
		}
		break;

	case WM_USER + 1: // Custom message: validate after paste/large delete
		{
			DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (pCtx)
			{
				CConfig::ValidateCustomTableLines(hDlg, imeMode, pCtx->pEngine, pCtx->maxCodes, false);
				pCtx->keystrokesSinceValidation = 0;
				debugPrint(L"Post-action validation completed");
			}
			ret = TRUE;
		}
		break;

	case WM_APP + 1: // Deferred font sync for IDC_EDIT_CUSTOM_TABLE
		// Runs after WM_SETFONT has been delivered to all controls.
		// GetObject return value is checked; SPI_GETNONCLIENTMETRICS used as fallback
		// so the block always runs and loaded text / typed text share the same font.
		{
			HWND hEditSync = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
			if (hEditSync)
			{
				LOGFONT lf = {};
				bool gotLF = false;
				HFONT hF = (HFONT)SendMessageW(hEditSync, WM_GETFONT, 0, 0);
				if (!hF) hF = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
				if (hF) gotLF = (GetObject(hF, sizeof(lf), &lf) != 0 && lf.lfHeight != 0);
				if (!gotLF) {
					NONCLIENTMETRICSW ncm = {};
					ncm.cbSize = sizeof(ncm);
					if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
						lf = ncm.lfMessageFont;
						gotLF = true;
					}
				}
				debugPrint(L"WM_APP+1 font sync: gotLF=%d face=%s lfHeight=%d",
					(int)gotLF, lf.lfFaceName, (int)lf.lfHeight);
				if (gotLF)
				{
					HDC hdc = GetDC(hEditSync);
					int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
					ReleaseDC(hEditSync, hdc);
					CHARFORMAT2W cf = {};
					cf.cbSize = sizeof(cf);
					cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT | CFM_ITALIC | CFM_CHARSET;
					StringCchCopyW(cf.szFaceName, LF_FACESIZE, lf.lfFaceName);
					cf.yHeight = MulDiv(abs(lf.lfHeight), 72 * 20, dpi);
					cf.wWeight = (WORD)lf.lfWeight;
					if (lf.lfItalic) cf.dwEffects |= CFE_ITALIC;
					cf.bCharSet = lf.lfCharSet; // match loaded text charset so SCF_DEFAULT and loaded runs agree
					debugPrint(L"WM_APP+1 applying: %s %d twips charset=%d at %d dpi", cf.szFaceName, cf.yHeight, cf.bCharSet, dpi);
					// Set default format for newly typed characters
					SendMessageW(hEditSync, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
					// Apply to all existing text (select-all covers final paragraph mark)
					CHARRANGE crOld;
					SendMessageW(hEditSync, EM_EXGETSEL, 0, (LPARAM)&crOld);
					SendMessageW(hEditSync, EM_SETSEL, 0, -1);
					SendMessageW(hEditSync, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
					SendMessageW(hEditSync, EM_EXSETSEL, 0, (LPARAM)&crOld);
					// Disable automatic font changes when user types Latin characters
					LRESULT langOpts = SendMessageW(hEditSync, EM_GETLANGOPTIONS, 0, 0);
					debugPrint(L"WM_APP+1 EM_GETLANGOPTIONS=0x%x", (int)langOpts);
					langOpts &= ~IMF_AUTOFONT;
					SendMessageW(hEditSync, EM_SETLANGOPTIONS, 0, langOpts);
					// Readback: verify what format position 0 actually has now
					CHARFORMAT2W cfRead = {};
					cfRead.cbSize = sizeof(cfRead);
					CHARRANGE crRead = {0, 1};
					SendMessageW(hEditSync, EM_EXSETSEL, 0, (LPARAM)&crRead);
					SendMessageW(hEditSync, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfRead);
					SendMessageW(hEditSync, EM_EXSETSEL, 0, (LPARAM)&crOld);
					debugPrint(L"WM_APP+1 readback pos0: face=%s yHeight=%d charset=%d", cfRead.szFaceName, cfRead.yHeight, cfRead.bCharSet);
				}
			}
			ret = TRUE;
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code)
		{
		case PSN_APPLY:
			if (_customTableChanged)
			{
				// Before exporting and parsing, validate the custom table lines in the rich edit control.
				// GWLP_USERDATA now stores a pointer to DialogContext (set by caller).
				DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
				CCompositionProcessorEngine* pEngine = (pCtx) ? pCtx->pEngine : nullptr;
				UINT maxCodes = (pCtx) ? pCtx->maxCodes : 4;

				// Validate and mark failing lines in red; if validation fails, show message and abort apply.
				if (!CConfig::ValidateCustomTableLines(hDlg, imeMode, pEngine, maxCodes))
				{
					// keep dialog open
					ret = TRUE;
					break;
				}

				if (imeMode == IME_MODE::IME_MODE_DAYI)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-CUSTOM.cin");
				}
				else if (imeMode == IME_MODE::IME_MODE_ARRAY)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-CUSTOM.cin");

				}
				else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-CUSTOM.cin");
				}
				else
				{
					StringCchPrintf(pathToLoad, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-CUSTOM.txt");
					StringCchPrintf(pathToWrite, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-CUSTOM.cin");
				}
				// export and parse as before
				exportCustomTableFile(hDlg, imeMode, pathToLoad);
				// parse custom.txt file to UTF-16 and internal format
				if (!parseCINFile(pathToLoad, pathToWrite, TRUE))
					MessageBox(GetFocus(), L"自建詞庫載入發生錯誤 !!", L"DIME 自訂詞庫", MB_ICONERROR);
			}

			//CConfig::WriteConfig();
			ret = TRUE;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	FreeLibrary(dllDlgHandle);
	return ret;


}


void CConfig::ParseConfig(HWND hDlg, IME_MODE imeMode, BOOL initDiag)
{
	HWND hwnd;
	size_t i;
	WCHAR num[16] = { 0 };
	WCHAR fontname[LF_FACESIZE] = { 0 };
	int fontpoint = 12, fontweight = FW_NORMAL, logPixelY, LogFontSize;
	BOOL fontitalic = FALSE;
	HDC hdc;
	HFONT hFont;
	static COLORREF colCust[16];

	wcsncpy_s(fontname, _pFontFaceName, _TRUNCATE);

	fontpoint = _fontSize;
	fontweight = _fontWeight;
	fontitalic = _fontItalic;

	if (fontpoint < 8 || fontpoint > 72)
	{
		fontpoint = 12;
	}
	if (fontweight < 0 || fontweight > 1000)
	{
		fontweight = FW_NORMAL;
	}
	if (fontitalic != TRUE && fontitalic != FALSE)
	{
		fontitalic = FALSE;
	}

	SetDlgItemText(hDlg, IDC_EDIT_FONTNAME, fontname);
	hdc = GetDC(hDlg);
	logPixelY = GetDeviceCaps(hdc, LOGPIXELSY);
	if (_GetDpiForMonitor)
	{
		HMONITOR monitor = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
		UINT dpiX, dpiY;
		_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		if (dpiY > 0) logPixelY = dpiY;
	}
	LogFontSize = -MulDiv(10, logPixelY, 72);

	hFont = CreateFont(LogFontSize, 0, 0, 0,
		fontweight, fontitalic, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, fontname);
	SendMessage(GetDlgItem(hDlg, IDC_EDIT_FONTNAME), WM_SETFONT, (WPARAM)hFont, 0);
	ReleaseDC(hDlg, hdc);

	SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, fontpoint, FALSE);

	ZeroMemory(&colCust, sizeof(colCust));

	// Load color boxes for the current color mode.
	// On Windows older than 1809 the SYSTEM combo item is not added, so _colorMode
	// may be SYSTEM while the combo falls back to LIGHT.  Snap _colorDisplayedMode
	// to the actual combo selection after the combo is built (see below).
	if (initDiag) _colorDisplayedMode = _colorMode;

	// Color mode combo
	hwnd = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
	if (initDiag)
	{
		int idx;
		if (Global::isWindows1809OrLater)
		{
			idx = (int)SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"跟隨系統模式");
			SendMessage(hwnd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM));
		}
		idx = (int)SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"淡色模式");
		SendMessage(hwnd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT));
		idx = (int)SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"深色模式");
		SendMessage(hwnd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK));
		idx = (int)SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"自訂");
		SendMessage(hwnd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM));
	}
	// Select the combo item that matches _colorMode (graceful fallback to first item)
	{
		int count = (int)SendMessage(hwnd, CB_GETCOUNT, 0, 0);
		int toSelect = 0;
		for (int ci = 0; ci < count; ci++)
		{
			if ((int)SendMessage(hwnd, CB_GETITEMDATA, (WPARAM)ci, 0) == static_cast<int>(_colorMode))
			{
				toSelect = ci;
				break;
			}
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)toSelect, 0);
	}
	// Sync _colorDisplayedMode to the actual combo selection.
	// If _colorMode == SYSTEM but SYSTEM wasn't added (old Windows), the combo
	// fell back to LIGHT, so we must track LIGHT — otherwise SaveColorsForMode
	// would be a no-op and any edits would be silently discarded.
	if (initDiag)
	{
		IME_COLOR_MODE actualMode = GetComboSelectedMode(hwnd);
		if (actualMode != _colorDisplayedMode)
			_colorDisplayedMode = actualMode;
	}
	LoadColorsForMode(_colorDisplayedMode);
	// Enable color boxes and labels for all modes except SYSTEM
	{
		BOOL enableColors = (GetComboSelectedMode(hwnd) != IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM) ? TRUE : FALSE;
		for (int ci = 0; ci < _countof(colors); ci++)
			EnableWindow(GetDlgItem(hDlg, colors[ci].id), enableColors);
		static const int labelIds[] = {
			IDC_LABEL_COL_FR, IDC_LABEL_COL_SEFR, IDC_LABEL_COL_BG,
			IDC_LABEL_COL_PHRASE, IDC_LABEL_COL_NU, IDC_LABEL_COL_SEBG
		};
		for (int id : labelIds)
			EnableWindow(GetDlgItem(hDlg, id), enableColors);
	}
	hwnd = GetDlgItem(hDlg, IDC_COMBO_REVERSE_CONVERSION);

	if(initDiag)
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"(無)");

	if (IsEqualCLSID(_reverseConversionGUIDProfile, CLSID_NULL))
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)0, 0);
	for (i = 0; i < _reverseConvervsionInfoList->Count(); i++)
	{
		if (initDiag)
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)_reverseConvervsionInfoList->GetAt(i)->description);

		if (IsEqualCLSID(_reverseConversionGUIDProfile, _reverseConvervsionInfoList->GetAt(i)->guidProfile))
			SendMessage(hwnd, CB_SETCURSEL, (WPARAM)i + 1, 0);
	}


	_snwprintf_s(num, _TRUNCATE, L"%d", _maxCodes);
	SetDlgItemTextW(hDlg, IDC_EDIT_MAXWIDTH, num);
	CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWNOTIFY, (_showNotifyDesktop) ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, (_autoCompose) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_CLEAR_ONBEEP, (_clearOnBeep) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, (_doBeep) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY, (_doBeepNotify) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP_CANDI, (_doBeepOnCandi) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY, (_customTablePriority) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE, (_arraySingleQuoteCustomPhrase) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE, (_dayiArticleMode) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_FORCESP, (_arrayForceSP) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP, (_arrayNotifySP) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, (_makePhrase) ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_OPEN, (_activatedKeyboardMode) ? BST_CHECKED : BST_UNCHECKED);
	if (!IsDlgButtonChecked(hDlg, IDC_RADIO_KEYBOARD_OPEN))
	{
		CheckDlgButton(hDlg, IDC_RADIO_KEYBOARD_CLOSE, BST_CHECKED);
	}
	CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHS, (_doHanConvert) ? BST_CHECKED : BST_UNCHECKED);
	if (!IsDlgButtonChecked(hDlg, IDC_RADIO_OUTPUT_CHS))
	{
		CheckDlgButton(hDlg, IDC_RADIO_OUTPUT_CHT, BST_CHECKED);
	}
	CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASPAGEDOWN, (_spaceAsPageDown) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY, (_spaceAsFirstCandSelkey) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECKBOX_ARROWKEYSWPAGES, (_arrowKeySWPages) ? BST_CHECKED : BST_UNCHECKED);

	hwnd = GetDlgItem(hDlg, IDC_COMBO_IME_SHIFT_MODE);
	if(initDiag)
	{
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"左右SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"右SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"左SHIFT鍵");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"無(僅Ctrl-Space鍵)");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_imeShiftMode, 0);

	hwnd = GetDlgItem(hDlg, IDC_COMBO_DOUBLE_SINGLE_BYTE);
	if (initDiag)
	{
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"以 Shift-Space 熱鍵切換");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"半型");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"全型");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_doubleSingleByteMode, 0);

	// initial Numeric pad combobox
	hwnd = GetDlgItem(hDlg, IDC_COMBO_NUMERIC_PAD);
	if (initDiag)
	{
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"數字鍵盤輸入數字符號");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"數字鍵盤輸入字根");
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"僅用數字鍵盤輸入字根");
	}
	SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_numericPad, 0);

	if (imeMode != IME_MODE::IME_MODE_GENERIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_SPACEASFIRSTCANDSELKEY), SW_HIDE);
	}
	if (imeMode != IME_MODE::IME_MODE_PHONETIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PHONETIC_KEYBOARD), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD), SW_HIDE);
	}
	if (imeMode != IME_MODE::IME_MODE_DAYI)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_DOBEEP_CANDI), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_DAYIARTICLEMODE), SW_HIDE);
	}
	if (imeMode != IME_MODE::IME_MODE_ARRAY)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_ARRAY_SCOPE), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE), SW_HIDE);

		// charset scope combo (Big5 filter) for non-Array modes
		hwnd = GetDlgItem(hDlg, IDC_COMBO_CHARSET_SCOPE);
		if (initDiag)
		{
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"完整字集");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"繁體中文");
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)(_big5Filter ? 1 : 0), 0);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_CHARSET_SCOPE), SW_SHOW);
		ShowWindow(hwnd, SW_SHOW);
	}

	if (imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_CUSTOM_TABLE_PRIORITY), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_EDIT_MAXWIDTH), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_EDIT_MAXWIDTH), SW_HIDE);
		hwnd = GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD);
		if (initDiag)
		{
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"標準鍵盤");
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"倚天鍵盤");
		}
		SendMessage(hwnd, CB_SETCURSEL, (WPARAM)_phoneticKeyboardLayout, 0);
	}

	if (imeMode == IME_MODE::IME_MODE_ARRAY)
	{
		ShowWindow(GetDlgItem(hDlg, IDC_EDIT_MAXWIDTH), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_EDIT_MAXWIDTH), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_STATIC_CHARSET_SCOPE), SW_HIDE);
		ShowWindow(GetDlgItem(hDlg, IDC_COMBO_CHARSET_SCOPE), SW_HIDE);
		if (_arrayScope == ARRAY_SCOPE::ARRAY40_BIG5)
		{
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_SINGLEQUOTE_CUSTOM_PHRASE), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_FORCESP), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_ARRAY_NOTIFYSP), SW_HIDE);
		}
		else
			ShowWindow(GetDlgItem(hDlg, IDC_CHECKBOX_AUTOCOMPOSE), SW_HIDE);
		// initial Array scope combobox
		hwnd = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
		if (initDiag)
		{
			LRESULT idx;
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Big5 (繁體中文)");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY30_BIG5);
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-AB");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB);
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A~D");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD);
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列30 Unicode Ext-A~G");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCDE);
			idx = SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)L"行列40 Big5");
			SendMessage(hwnd, CB_SETITEMDATA, idx, (LPARAM)ARRAY_SCOPE::ARRAY40_BIG5);
		}
		{
			LRESULT count = SendMessage(hwnd, CB_GETCOUNT, 0, 0);
			for (LRESULT j = 0; j < count; j++)
			{
				if ((ARRAY_SCOPE)SendMessage(hwnd, CB_GETITEMDATA, j, 0) == _arrayScope)
				{
					SendMessage(hwnd, CB_SETCURSEL, j, 0);
					break;
				}
			}
		}

	}
}


BOOL CConfig::importCustomTableFile(_In_ HWND hDlg, _In_ LPCWSTR pathToLoad)
{
	debugPrint(L"CConfig::importCustomTableFile() pathToLoad = %s\n", pathToLoad);
	BOOL success = FALSE;

	if (PathFileExists(pathToLoad)) //failed back to try preload Dayi.cin in program files.
	{
		success = TRUE;
		HANDLE hCustomTable = NULL;
		DWORD dwDataLen = 0;
		LPCWSTR customText = nullptr;

		if ((hCustomTable = CreateFile(pathToLoad, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
		{	// Error
			success = FALSE;
			goto Cleanup;
		}
		// Get file size
		if (hCustomTable && (dwDataLen = GetFileSize(hCustomTable, NULL)) == INVALID_FILE_SIZE)
		{	// Error
			success = FALSE;
			goto Cleanup;
		}
		// Create a buffer for the custom table text

		customText = new (std::nothrow) WCHAR[dwDataLen + 1];
		if (customText == nullptr)
		{// Error
			success = FALSE;
			goto Cleanup;
		}
		ZeroMemory((LPVOID)customText, (dwDataLen + 1) * sizeof (WCHAR));
		// Read the file
		if (hCustomTable && !ReadFile(hCustomTable, (LPVOID)customText, dwDataLen, &dwDataLen, NULL))
		{	// Error
			success = FALSE;
			goto Cleanup;
		}

		// Set SCF_DEFAULT before SetDlgItemTextW so loaded text uses the dialog font.
		// GetObject return value is checked; SPI_GETNONCLIENTMETRICS used as fallback
		// so the block always runs even if WM_GETFONT returns a stale handle.
		{
			HWND hEditPre = GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE);
			if (hEditPre)
			{
				LOGFONT lfPre = {};
				bool gotLFPre = false;
				HFONT hFP = (HFONT)SendMessageW(hEditPre, WM_GETFONT, 0, 0);
				if (!hFP) hFP = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
				if (hFP) gotLFPre = (GetObject(hFP, sizeof(lfPre), &lfPre) != 0 && lfPre.lfHeight != 0);
				if (!gotLFPre) {
					NONCLIENTMETRICSW ncm = {};
					ncm.cbSize = sizeof(ncm);
					if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
						lfPre = ncm.lfMessageFont;
						gotLFPre = true;
					}
				}
				if (gotLFPre) {
					HDC hdcPre = GetDC(hEditPre);
					int dpiPre = GetDeviceCaps(hdcPre, LOGPIXELSY);
					ReleaseDC(hEditPre, hdcPre);
					CHARFORMAT2W cfPre = {};
					cfPre.cbSize = sizeof(cfPre);
					cfPre.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT | CFM_ITALIC | CFM_CHARSET;
					StringCchCopyW(cfPre.szFaceName, LF_FACESIZE, lfPre.lfFaceName);
					cfPre.yHeight = MulDiv(abs(lfPre.lfHeight), 72 * 20, dpiPre);
					cfPre.wWeight = (WORD)lfPre.lfWeight;
					if (lfPre.lfItalic) cfPre.dwEffects |= CFE_ITALIC;
					cfPre.bCharSet = lfPre.lfCharSet; // match loaded text charset
					debugPrint(L"importCustomTable pre-load SCF_DEFAULT: %s %d twips", cfPre.szFaceName, cfPre.yHeight);
					SendMessageW(hEditPre, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cfPre);
				}
			}
		}
		if (!IsTextUnicode(customText, dwDataLen, NULL))
		{
			WCHAR* outWStr = nullptr;
			UINT codepage = 0;

			//IMultiLanguage initialization
			if(FAILED(CoInitialize(NULL)))
			{	// Error
				success = FALSE;
				goto Cleanup;
			}
			IMultiLanguage2 *lang = NULL;
			HRESULT hr = CoCreateInstance(CLSID_CMultiLanguage, NULL,
				CLSCTX_ALL, IID_IMultiLanguage2, (LPVOID*)&lang);


			int detectEncCount = 1;
			DetectEncodingInfo detectEnc;
			INT inlen = dwDataLen;
			lang->DetectInputCodepage(MLDETECTCP_HTML, 0, (char *)customText, &inlen, &detectEnc, &detectEncCount);
			codepage = detectEnc.nCodePage;

			DWORD pdwMode = 0;
			UINT outlen = 0;
			UINT uinlen = dwDataLen;
			if (SUCCEEDED(hr)) {
				hr = lang->ConvertStringToUnicode(&pdwMode, codepage, (char *)customText, &uinlen, NULL, &outlen);
				outWStr = new (std::nothrow) WCHAR[outlen + 1];
				if (outWStr == nullptr)
				{// Error
					success = FALSE;
					goto Cleanup;
				}
				ZeroMemory(outWStr, (outlen + 1)*sizeof(WCHAR));
			}

			//convert to unicode
			if (SUCCEEDED(hr)) {
				hr = lang->ConvertStringToUnicode(&pdwMode, codepage, (char *)customText, &uinlen, outWStr, &outlen);
			}

			if (lang)
				lang->Release();
			CoUninitialize();

            if (outWStr)
            {
                // Strip leading BOM (U+FEFF) if present before setting control text
                if (outWStr[0] == 0xFEFF)
                    SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, outWStr + 1);
                else
                    SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, outWStr);
                delete[]outWStr;
            }

		}
        else if(customText)
        {
            // Strip leading BOM (U+FEFF) if present in file buffer
            if (customText[0] == 0xFEFF)
                SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, customText + 1);
            else
                SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_TABLE, customText);
        }

		// Post a deferred font sync (WM_APP+1) that fires after WM_SETFONT has been
		// delivered to all controls, giving the most reliable font handle.
		PostMessageW(hDlg, WM_APP + 1, 0, 0);

	Cleanup:
		if (hCustomTable) CloseHandle(hCustomTable);
		if (customText) delete[]customText;
	}
	return success;
}


BOOL CConfig::exportCustomTableFile(_In_ HWND hDlg, IME_MODE imeMode, _In_ LPCWSTR pathToWrite)
{
	//write the edittext context into custom.txt
	// Validate contents first; if invalid, abort export so Apply will not continue to parseCINFile.
	// GWLP_USERDATA stores DialogContext* (set by caller)
	DialogContext* pCtx = (DialogContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
	CCompositionProcessorEngine* pEngine = (pCtx) ? pCtx->pEngine : nullptr;
	UINT maxCodes = (pCtx) ? pCtx->maxCodes : 4;
	if (!ValidateCustomTableLines(hDlg, imeMode, pEngine, maxCodes))
	{
		return FALSE;
	}
	BOOL success = TRUE;
	DWORD dwDataLen;
	LPWSTR customText = nullptr;
	HANDLE hCustomTableFile = NULL;
	DWORD lpNumberOfBytesWritten = 0;
	WCHAR byteOrder = 0xFEFF;

	dwDataLen = GetWindowTextLength(GetDlgItem(hDlg, IDC_EDIT_CUSTOM_TABLE));
	customText = new (std::nothrow) WCHAR[dwDataLen + 1];
	if (customText == nullptr)
	{
		// Error
		success = FALSE;
		goto Cleanup;
	}
	ZeroMemory(customText, (dwDataLen + 1)*sizeof(WCHAR));
	GetDlgItemText(hDlg, IDC_EDIT_CUSTOM_TABLE, customText, dwDataLen + 1);

	// Create a file to save custom table
	if ((hCustomTableFile = CreateFile(pathToWrite, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{	// Error
		success = FALSE;
		goto Cleanup;
	}

	//Write Byte order mark to the file if the first byte of buf is not BOM

	if (customText[0] != byteOrder && !WriteFile(hCustomTableFile, (LPCVOID)&byteOrder, (DWORD)sizeof(WCHAR), &lpNumberOfBytesWritten, NULL))
	{	// Error
		success = FALSE;
		goto Cleanup;
	}
	// Write the custom table text to the file
	if (!WriteFile(hCustomTableFile, (LPCVOID)customText, (DWORD)dwDataLen*sizeof(WCHAR), &lpNumberOfBytesWritten, NULL))
	{	// Error
		success = FALSE;
		goto Cleanup;
	}
Cleanup:
	if (hCustomTableFile) CloseHandle(hCustomTableFile);
	if (customText) delete[]customText;

	return success;
}