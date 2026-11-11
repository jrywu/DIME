// SettingsWindow2.cpp — CLEAN REWRITE of the settings window
//
// Architecture rules:
// 1. Every interactive element is a REAL Win32 control (BUTTON, COMBOBOX, EDIT, RICHEDIT, STATIC)
// 2. Controls are created in RebuildContentArea() at absolute Y positions
// 3. On scroll: RepositionControlsForScroll() moves ALL controls via SetWindowPos
// 4. Owner-draw (toggle switches, color swatches, row icons) paints ON TOP of real controls
// 5. Every child control is subclassed to forward WM_MOUSEWHEEL to content area
// 6. All geometry from g_geo, all colors from wd->theme (system-derived)

#include "framework.h"
#include <CommCtrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <strsafe.h>
#include <windowsx.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <math.h>
#include <tom.h>          // ITextDocument / ITextRange — for issue-#130 caret-safe painting
#include <richedit.h>     // EM_GETOLEINTERFACE
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#include "..\Globals.h"
#include "..\Config.h"
#include "..\CompositionProcessorEngine.h"
#include "..\TfInputProcessorProfile.h"
#include "..\BuildInfo.h"
#include "SettingsWindow.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsGeometry.h"
#include "SettingsWindowLayout.h"
#include "SettingsWindowPaint.h"
#include "SettingsMsgBox.h"
#include "SettingsNav.h"

#include <Richedit.h>
#include <MLang.h>
#include <Shlwapi.h>
#include <regex>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define DWMWA_MICA_EFFECT 1029

// Content area child window
#define IDC_CONTENT_AREA 2000
static const wchar_t CONTENT_WND_CLASS[] = L"DIMESettingsContentArea";
static bool s_contentClassRegistered = false;

// ThemedMessageBox — owner-drawn MessageBox replacement (SettingsMsgBox.*).
static int ThemedMessageBox(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    return SettingsMsgBox::Show(hParent, text, caption, type);
}

// ============================================================================
// Child control WM_MOUSEWHEEL forwarder
// ============================================================================
static LRESULT CALLBACK ChildWheelSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_MOUSEWHEEL)
        return SendMessage(GetParent(hWnd), uMsg, wParam, lParam);
    if (uMsg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, ChildWheelSubclassProc, uIdSubclass);
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ============================================================================
// ValidateCustomTableBuffer — fast in-memory validator (issue #130).
// Iterates a UTF-16 buffer line by line and runs SettingsModel::ValidateLine.
// O(N) over text size; does NOT touch RichEdit (so no O(N²) EM_GETTEXTRANGE
// pass and no per-line repaint). Used by Import and Save where we already
// have the full buffer in memory; the per-keystroke EN_CHANGE path keeps
// using ValidateCustomTableRE because it also paints error highlights.
// Returns total error count. Fills *outFirstErrLine with the 1-based line
// number of the first error (0 if none). Appends "  第 N 行\n" entries to
// outSummary up to maxReport entries (caller responsible for buffer sizing).
// ============================================================================
static int ValidateCustomTableBuffer(const wchar_t* text, size_t wlen,
    IME_MODE mode, UINT maxCodes, CCompositionProcessorEngine* pEng,
    int* outFirstErrLine, wchar_t* outSummary, size_t summaryCch, int maxReport)
{
    return SettingsWindowLayout::ValidateBuffer(text, wlen, mode, maxCodes, pEng,
        outFirstErrLine, outSummary, summaryCch, maxReport);
}

// ============================================================================
// Issue #130 Phase 3: foreground viewport pass + chunked background scan +
// Save-button gating. Replaces the LIVE_VALIDATE_LINE_CAP all-or-nothing
// heuristic with a design that stays responsive at any document size.
// Design + rationale: docs/RICHEDIT_FG_BG_VAL.md and
//                     docs/CUSTOM_TABLE_VALIDATION.md §2.2.
// ============================================================================
static const int      BG_SCAN_CHUNK        = 500;     // lines per scan-tick
static const UINT     BG_SCAN_INTERVAL_MS  = 50;      // scan tick period
static const UINT_PTR BG_SCAN_TIMER_ID     = 0xC130;  // unique scan-timer ID

// Forward declarations (call sites earlier in the file reference these).
static void ValidateViewport(HWND hRE, SettingsWindow::WindowData* wd);
static void StartBgScan(HWND hOwnerWnd, SettingsWindow::WindowData* wd);
static void CancelBgScan(HWND hOwnerWnd, SettingsWindow::WindowData* wd);
static void EnableSaveButton(SettingsWindow::WindowData* wd, BOOL enable);
static void SetSaveButtonText(SettingsWindow::WindowData* wd, LPCWSTR text);
static BOOL SnapshotEditorForBgScan(HWND hRE, SettingsWindow::WindowData* wd);
static void FreeBgSnapshot(SettingsWindow::WindowData* wd);

// Compute the inclusive [firstVis, lastVis] line range currently visible in
// the RichEdit. Returns 0,0 for an empty editor.
static void GetViewportLineRange(HWND hRE, int* outFirstVis, int* outLastVis)
{
    int firstVis = (int)SendMessageW(hRE, EM_GETFIRSTVISIBLELINE, 0, 0);
    RECT rc; GetClientRect(hRE, &rc);
    POINTL pt = { rc.right - 1, rc.bottom - 1 };
    LRESULT lastChar = SendMessageW(hRE, EM_CHARFROMPOS, 0, (LPARAM)&pt);
    int lastVis = (lastChar >= 0)
        ? (int)SendMessageW(hRE, EM_LINEFROMCHAR, (WPARAM)lastChar, 0)
        : firstVis;
    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lastVis >= lineCount) lastVis = lineCount - 1;
    if (lastVis < firstVis) lastVis = firstVis;
    if (outFirstVis) *outFirstVis = firstVis;
    if (outLastVis)  *outLastVis  = lastVis;
}

// Issue #130 Phase 3 (final fix for live highlighting without caret jumps):
// the previous EM_SETSEL + EM_SETCHARFORMAT(SCF_SELECTION) approach moved the
// caret on every per-line / per-error paint, even with WM_SETREDRAW + HIDESEL
// suppression — RichEdit-50 doesn't reliably honour the restore inside that
// window. The proper fix is to use the Text Object Model (ITextDocument /
// ITextRange) which sets CHARFORMAT on arbitrary ranges WITHOUT touching the
// selection at all.

// Acquire ITextDocument from the RichEdit. Caller releases.
// Returns nullptr if the control doesn't support TOM (very old RichEdit
// versions); callers should fall back gracefully (skip painting).
static ITextDocument* GetRichEditTextDocument(HWND hRE)
{
    if (!hRE) return nullptr;
    IUnknown* pUnk = nullptr;
    SendMessageW(hRE, EM_GETOLEINTERFACE, 0, (LPARAM)&pUnk);
    if (!pUnk) return nullptr;
    ITextDocument* pDoc = nullptr;
    pUnk->QueryInterface(__uuidof(ITextDocument), (void**)&pDoc);
    pUnk->Release();
    return pDoc;
}

// Paint a foreground colour over [cpMin, cpMax) WITHOUT moving the selection.
static void PaintRangeColor(ITextDocument* pDoc, LONG cpMin, LONG cpMax, COLORREF color)
{
    if (!pDoc || cpMax <= cpMin) return;
    ITextRange* pRange = nullptr;
    if (FAILED(pDoc->Range(cpMin, cpMax, &pRange)) || !pRange) return;
    ITextFont* pFont = nullptr;
    if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont) {
        pFont->SetForeColor((LONG)color);
        pFont->Release();
    }
    pRange->Release();
}

// Paint per-error colours over the line whose absolute char offset is
// lineStart. Uses ITextRange — does not touch the selection.
static void PaintErrorRange(ITextDocument* pDoc, LONG lineStart,
    const SettingsModel::LineValidation& v, bool isDark)
{
    if (!pDoc || v.valid || v.errors.empty()) return;

    COLORREF errorFormat = isDark ? CUSTOM_TABLE_DARK_ERROR_FORMAT : CUSTOM_TABLE_LIGHT_ERROR_FORMAT;
    COLORREF errorLength = isDark ? CUSTOM_TABLE_DARK_ERROR_LENGTH : CUSTOM_TABLE_LIGHT_ERROR_LENGTH;
    COLORREF errorChar   = isDark ? CUSTOM_TABLE_DARK_ERROR_CHAR   : CUSTOM_TABLE_LIGHT_ERROR_CHAR;

    for (const auto& err : v.errors) {
        COLORREF color;
        switch (err.error) {
        case SettingsModel::LineError::Format:      color = errorFormat; break;
        case SettingsModel::LineError::KeyTooLong:  color = errorLength; break;
        case SettingsModel::LineError::InvalidChar: color = errorChar;   break;
        default: continue;
        }
        PaintRangeColor(pDoc,
            lineStart + err.errorStart,
            lineStart + err.errorStart + err.errorLen,
            color);
    }
}

// Validate one line by index. Reads the line via EM_GETTEXTRANGE, validates
// in-memory, then paints valid-colour first (clears stale error colours from
// a previous pass) followed by per-error colours — ALL via ITextRange,
// so the caret never moves.
static void ValidateCustomTableRE_OneLine(HWND hRE, ITextDocument* pDoc, int li,
    IME_MODE mode, UINT maxCodes, bool isDark, CCompositionProcessorEngine* pEng)
{
    if (!pDoc) return;
    LONG start = (LONG)SendMessageW(hRE, EM_LINEINDEX, (WPARAM)li, 0);
    if (start < 0) return;
    LONG len = (LONG)SendMessageW(hRE, EM_LINELENGTH, (WPARAM)start, 0);
    if (len <= 0) return;
    std::vector<WCHAR> buf((size_t)len + 1);
    TEXTRANGEW tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = start + len;
    tr.lpstrText = buf.data();
    if (SendMessageW(hRE, EM_GETTEXTRANGE, 0, (LPARAM)&tr) <= 0) return;

    auto v = SettingsModel::ValidateLine(buf.data(), (int)len, mode, maxCodes, pEng);

    COLORREF validColor = isDark ? CUSTOM_TABLE_DARK_VALID : CUSTOM_TABLE_LIGHT_VALID;
    PaintRangeColor(pDoc, start, start + len, validColor);
    if (!v.valid) PaintErrorRange(pDoc, start, v, isDark);
}

// Validate ONLY the current line (the one the caret is on). Used by EN_CHANGE
// and WM_USER+1 — per-keystroke cost is constant regardless of document size,
// so typing stays responsive even on 70k+ line buffers.
static void ValidateCurrentLine(HWND hRE, SettingsWindow::WindowData* wd)
{
    if (!hRE || !wd) return;
    CHARRANGE sel = {0, 0};
    SendMessageW(hRE, EM_EXGETSEL, 0, (LPARAM)&sel);
    int li = (int)SendMessageW(hRE, EM_LINEFROMCHAR, (WPARAM)sel.cpMin, 0);
    if (li < 0) return;

    ITextDocument* pDoc = GetRichEditTextDocument(hRE);
    if (!pDoc) return;
    LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);

    ValidateCustomTableRE_OneLine(hRE, pDoc, li, wd->currentMode,
        wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);

    SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
    pDoc->Release();
}

// Foreground viewport pass — validates only the lines currently visible in
// the editor. Selection is never touched (TOM-based painting), so no caret
// jumps and no flicker. Used at one-shot moments (initial-load, Import,
// scroll) where painting the full visible range is necessary; per-keystroke
// EN_CHANGE uses ValidateCurrentLine instead so cost stays constant.
//
// Wraps the painting block in EM_SETEVENTMASK(0) → restore. RichEdit fires
// EN_CHANGE on some CHARFORMAT-only operations even though strictly only the
// formatting changed; without the mute we'd recurse infinitely (EN_CHANGE
// handler → ValidateViewport → SetForeColor → EN_CHANGE → ...) which pegs
// the message pump and makes the editor non-editable.
static void ValidateViewport(HWND hRE, SettingsWindow::WindowData* wd)
{
    if (!hRE || !wd) return;
    int firstVis, lastVis;
    GetViewportLineRange(hRE, &firstVis, &lastVis);
    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return;

    ITextDocument* pDoc = GetRichEditTextDocument(hRE);
    if (!pDoc) return;  // No TOM support → silently skip live highlighting.

    LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);

    for (int li = firstVis; li <= lastVis && li < lineCount; ++li) {
        ValidateCustomTableRE_OneLine(hRE, pDoc, li, wd->currentMode,
            wd->snapshot.maxCodes, wd->isDarkTheme, wd->pEngine);
    }

    SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
    pDoc->Release();
}

// Find and enable/disable the Save button. Centralised so callers don't repeat
// the FindControl boilerplate.
static void EnableSaveButton(SettingsWindow::WindowData* wd, BOOL enable)
{
    if (!wd) return;
    HWND hSaveBtn = SettingsWindow::FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    if (hSaveBtn) EnableWindow(hSaveBtn, enable);
}

// Set the Save button caption and force a repaint. Used to give the user
// visible feedback while the BG scan is running ("驗證中 N%") and after it
// completes with errors ("第 N 行有誤"). Restored to "儲存" on clean completion
// and after a successful Save.
static void SetSaveButtonText(SettingsWindow::WindowData* wd, LPCWSTR text)
{
    if (!wd || !text) return;
    HWND hSaveBtn = SettingsWindow::FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    if (!hSaveBtn) return;
    // Skip the SetWindowText if the caption is unchanged — avoids needless
    // WM_DRAWITEM churn when the BG-scan progress percentage hasn't moved.
    wchar_t curr[64] = {};
    GetWindowTextW(hSaveBtn, curr, _countof(curr));
    if (wcscmp(curr, text) == 0) return;
    SetWindowTextW(hSaveBtn, text);
    InvalidateRect(hSaveBtn, nullptr, TRUE);
}

// Free any snapshot owned by the WindowData. Idempotent.
static void FreeBgSnapshot(SettingsWindow::WindowData* wd)
{
    if (!wd) return;
    if (wd->ctBgBuffer)      { delete[] wd->ctBgBuffer;      wd->ctBgBuffer = nullptr; }
    if (wd->ctBgLineOffsets) { delete[] wd->ctBgLineOffsets; wd->ctBgLineOffsets = nullptr; }
    wd->ctBgBufferLen = 0;
}

// Cancel any in-progress background scan and free the in-memory snapshot.
// Does NOT touch the Save button — Save's enabled state is dirty-bit driven
// (EN_CHANGE marks dirty; successful Save clears it).
static void CancelBgScan(HWND hOwnerWnd, SettingsWindow::WindowData* wd)
{
    if (!wd) return;
    if (wd->ctBgScanTimer) {
        KillTimer(hOwnerWnd, BG_SCAN_TIMER_ID);
        wd->ctBgScanTimer = 0;
    }
    FreeBgSnapshot(wd);
}

// Start a one-shot background scan: snapshot the editor content, then walk it
// in chunks via WM_TIMER ticks. Triggered ONLY by Import button + initial-load
// (one-shot visual feedback for bulk content). EN_CHANGE never starts a BG
// scan — typing only runs the foreground viewport pass and cancels any
// in-flight BG. Save validates independently at click time.
static void StartBgScan(HWND hOwnerWnd, SettingsWindow::WindowData* wd)
{
    if (!wd) return;
    CancelBgScan(hOwnerWnd, wd);
    HWND hRE = SettingsWindow::FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
    if (!hRE) return;
    if (!SnapshotEditorForBgScan(hRE, wd)) return;  // OOM — silently skip
    wd->ctBgScanTimer = SetTimer(hOwnerWnd, BG_SCAN_TIMER_ID,
                                 BG_SCAN_INTERVAL_MS, nullptr);
}

// Take a one-shot snapshot of the RichEdit content into wd->ctBgBuffer and
// compute the per-line char-offset table. Called from the debounce-timer fire
// after typing settles. Returns FALSE on allocation failure.
static BOOL SnapshotEditorForBgScan(HWND hRE, SettingsWindow::WindowData* wd)
{
    if (!hRE || !wd) return FALSE;
    DWORD dwLen = GetWindowTextLengthW(hRE);
    LPWSTR buf = new (std::nothrow) WCHAR[dwLen + 1];
    if (!buf) return FALSE;
    ZeroMemory(buf, (dwLen + 1) * sizeof(WCHAR));
    GetWindowTextW(hRE, buf, dwLen + 1);

    // Walk the buffer once to record line-start offsets. Lines split on '\n';
    // a trailing '\r' on the previous line is excluded by the validator below.
    // Pre-sized via EM_GETLINECOUNT for an exact allocation.
    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lineCount < 1) lineCount = 1;
    int* offsets = new (std::nothrow) int[lineCount];
    if (!offsets) { delete[] buf; return FALSE; }
    int li = 0;
    offsets[0] = 0;
    for (int i = 0; i < (int)dwLen && li + 1 < lineCount; ++i) {
        if (buf[i] == L'\n') {
            offsets[++li] = i + 1;
        }
    }
    // If actual line count differs (rare — EM_GETLINECOUNT counts wrap lines
    // too on some RichEdit versions), cap to what we filled.
    int actualLines = li + 1;
    if (actualLines < lineCount) lineCount = actualLines;

    wd->ctBgBuffer               = buf;
    wd->ctBgBufferLen            = (int)dwLen;
    wd->ctBgLineOffsets          = offsets;
    wd->ctBgScanLineCountAtStart = lineCount;
    wd->ctBgScanCursor           = 0;
    wd->ctFirstBgErrorLine       = -1;
    return TRUE;
}

// ValidateCustomTableRE removed (issue #130 Phase 3) — replaced by the
// foreground viewport pass + background WM_TIMER scan above. Live edit
// highlighting is now O(viewport) instead of O(N²) over RichEdit lines.

// Custom table path helpers — delegated to SettingsModel
#define GetCustomTableTxtPath  SettingsModel::GetCustomTableTxtPath
#define GetCustomTableCINPath  SettingsModel::GetCustomTableCINPath

// ============================================================================
// Window placement persistence (registry)
// ============================================================================
static const wchar_t SETTINGS_UI_REG_KEY[] = L"SOFTWARE\\DIME\\SettingsUI";
static const wchar_t SETTINGS_UI_WP_VALUE[] = L"WindowPlacement";

static bool SaveWindowPlacement(HWND hWnd)
{
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(hWnd, &wp)) return false;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, 0, nullptr,
        0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, SETTINGS_UI_WP_VALUE, 0, REG_BINARY, (const BYTE*)&wp, sizeof(wp));
    RegCloseKey(hKey);
    return true;
}

static bool RestoreWindowPlacement(HWND hWnd)
{
    WINDOWPLACEMENT wp = {};
    DWORD cbData = sizeof(wp);
    DWORD dwType = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, SETTINGS_UI_WP_VALUE,
        RRF_RT_REG_BINARY, &dwType, &wp, &cbData) != ERROR_SUCCESS) return false;
    if (cbData != sizeof(wp) || wp.length != sizeof(wp)) return false;
    // Verify saved position is on a connected monitor
    if (!MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL)) return false;
    SetWindowPlacement(hWnd, &wp);
    return true;
}

static void DeleteWindowPlacement()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_UI_REG_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, SETTINGS_UI_WP_VALUE);
        RegCloseKey(hKey);
    }
}

// Subclass proc for the custom table RichEdit:
// - Prevents select-all on focus
// - Posts WM_USER+1 to parent (hContentArea) for deferred validation on Enter/Space/Paste/large-delete
static LRESULT CALLBACK CustomTableRE_SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    if (uMsg == WM_SETFOCUS) {
        CHARRANGE cr = {0, 0};
        SendMessageW(hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hWnd, EM_SCROLLCARET, 0, 0);
    } else if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
        } else if (wParam == VK_DELETE || wParam == VK_BACK) {
            CHARRANGE sel;
            SendMessageW(hWnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMax - sel.cpMin > CUSTOM_TABLE_LARGE_DELETION_THRESHOLD)
                PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
        }
    } else if (uMsg == WM_PASTE) {
        PostMessageW(GetParent(hWnd), WM_USER + 1, 0, 0);
    } else if (uMsg == WM_MOUSEWHEEL) {
        // Cursor-aware routing: if cursor is over the RichEdit, let it scroll internally;
        // otherwise forward to the outer content area scrollbar.
        POINT pt;
        GetCursorPos(&pt);
        RECT rcRE;
        GetWindowRect(hWnd, &rcRE);
        if (!PtInRect(&rcRE, pt))
            return SendMessageW(GetParent(hWnd), uMsg, wParam, lParam);
        // Cursor is inside RichEdit → RichEdit scrolls internally. Forward a
        // WM_USER+3 to the parent so the foreground viewport pass repaints
        // whatever is now visible. Does NOT restart the BG scan (no buffer change).
        PostMessageW(GetParent(hWnd), WM_USER + 3, 0, 0);
        // Fall through to DefSubclassProc to let RichEdit do the actual scroll.
    } else if (uMsg == WM_VSCROLL) {
        // Scrollbar drag / arrow / page key — forward FG-only repaint trigger.
        PostMessageW(GetParent(hWnd), WM_USER + 3, 0, 0);
        // Fall through to DefSubclassProc.
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, CustomTableRE_SubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ============================================================================
// Mica backdrop (title bar only)
// ============================================================================
static bool ApplyMicaBackdrop(HWND hWnd)
{
    int backdropType = 2;
    if (SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(int))))
        return true;
    BOOL enable = TRUE;
    if (SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_MICA_EFFECT, &enable, sizeof(BOOL))))
        return true;
    return false;
}

// ============================================================================
// Helper: create a font
// ============================================================================
// ============================================================================
// Theme brushes + colors
// ============================================================================
void SettingsWindow::CreateThemeBrushes(WindowData* wd)
{
    DestroyThemeBrushes(wd);
    wd->theme = ComputeThemeColors(wd->isDarkTheme);
    wd->hBrushSidebarBg = CreateSolidBrush(wd->theme.sidebarBg);
    wd->hBrushContentBg = CreateSolidBrush(wd->theme.contentBg);
    wd->hBrushCardBg    = CreateSolidBrush(wd->theme.cardBg);
}

void SettingsWindow::DestroyThemeBrushes(WindowData* wd)
{
    if (wd->hBrushSidebarBg)  { DeleteObject(wd->hBrushSidebarBg);  wd->hBrushSidebarBg = nullptr; }
    if (wd->hBrushContentBg)  { DeleteObject(wd->hBrushContentBg);  wd->hBrushContentBg = nullptr; }
    if (wd->hBrushCardBg)     { DeleteObject(wd->hBrushCardBg);     wd->hBrushCardBg = nullptr; }
}

void SettingsWindow::CreateFonts(WindowData* wd, int dpi)
{
    DestroyFonts(wd);
    SettingsWindowPaint::Fonts f = {};
    SettingsWindowPaint::CreateFonts((UINT)dpi, &f);
    wd->hFontTitle      = f.title;
    wd->hFontCardHeader = f.cardHeader;
    wd->hFontBody       = f.body;
    wd->hFontDesc       = f.desc;
    wd->hFontMDL2       = f.mdl2;
    wd->hFontMDL2Icon   = f.mdl2Icon;
    wd->hasMDL2         = f.hasMDL2 != 0;
}

void SettingsWindow::DestroyFonts(WindowData* wd)
{
    if (wd->hFontTitle)      { DeleteObject(wd->hFontTitle);      wd->hFontTitle = nullptr; }
    if (wd->hFontCardHeader) { DeleteObject(wd->hFontCardHeader); wd->hFontCardHeader = nullptr; }
    if (wd->hFontBody)       { DeleteObject(wd->hFontBody);       wd->hFontBody = nullptr; }
    if (wd->hFontDesc)       { DeleteObject(wd->hFontDesc);       wd->hFontDesc = nullptr; }
    if (wd->hFontMDL2)       { DeleteObject(wd->hFontMDL2);       wd->hFontMDL2 = nullptr; }
    if (wd->hFontMDL2Icon)   { DeleteObject(wd->hFontMDL2Icon);   wd->hFontMDL2Icon = nullptr; }
}

// Mode helpers — delegated to SettingsModel
#define IndexToMode  SettingsModel::IndexToMode
#define ModeToIndex  SettingsModel::ModeToIndex

COLORREF SettingsWindow::GetAccentColor() { return ComputeThemeColors(false).accent; }

// ============================================================================
// Scroll helpers
// ============================================================================
void SettingsWindow::UpdateScrollInfo(WindowData* wd)
{
    if (!wd->hContentArea) return;
    RECT rc; GetClientRect(wd->hContentArea, &rc);
    wd->scrollMax = SettingsModel::ComputeScrollRange(wd->totalContentHeight, rc.bottom);
    wd->scrollPos = SettingsModel::ClampScrollPos(wd->scrollPos, wd->scrollMax);
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, wd->totalContentHeight, (UINT)rc.bottom, wd->scrollPos };
    SetScrollInfo(wd->hContentArea, SB_VERT, &si, TRUE);
}

void SettingsWindow::RepositionControlsForScroll(WindowData* wd)
{
    if (!wd->hContentArea) return;
    UINT dpi = CConfig::GetDpiForHwnd(wd->hContentArea);
    int titleBottom = ScaleForDpi(g_geo.topMargin, dpi);
    for (auto& ch : wd->controlHandles) {
        if (!(ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0)))
            continue;
        int newY = ch.origY - wd->scrollPos;
        SetWindowPos(ch.hWnd, nullptr, ch.origX, newY,
            ch.origW, ch.origH, SWP_NOZORDER | SWP_NOACTIVATE);
        // Snap-hide any control whose top enters the title cover band.
        // Settings rows are short (~24 px) — partial clipping looks like a
        // floating control above the title rather than a graceful reveal.
        if (newY < titleBottom) {
            if (IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
        } else {
            SetWindowRgn(ch.hWnd, nullptr, TRUE);
            if (!IsWindowVisible(ch.hWnd)) ShowWindow(ch.hWnd, SW_SHOW);
        }
    }
}

void SettingsWindow::ShowAllControls(WindowData* wd)
{
    for (auto& ch : wd->controlHandles) {
        if (ch.hWnd && IsWindow(ch.hWnd) && (ch.origW > 0 || ch.origH > 0))
            ShowWindow(ch.hWnd, SW_SHOW);
    }
}

// Forward declarations
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi);

// Navigation state <-> WindowData.
static SettingsNav::NavState LoadNav(const SettingsWindow::WindowData* wd)
{
    SettingsNav::NavState n = {};
    n.selectedModeIndex = wd->selectedModeIndex;
    n.hoverSidebarIndex = wd->hoverSidebarIndex;
    n.navLevel = wd->navLevel;
    n.currentCardIndex = wd->currentCardIndex;
    n.hoverRowIndex = wd->hoverRowIndex;
    n.currentMode = (int)wd->currentMode;
    n.scrollPos = wd->scrollPos;
    n.scrollMax = wd->scrollMax;
    n.totalContentHeight = wd->totalContentHeight;
    n.sidebarCollapsed = wd->sidebarCollapsed ? 1 : 0;
    n.sidebarNarrowMode = wd->sidebarNarrowMode ? 1 : 0;
    n.colorGridExpanded = wd->colorGridExpanded ? 1 : 0;
    memcpy(n.cardExpanded, wd->cardExpanded, sizeof(n.cardExpanded));
    return n;
}

static void StoreNav(SettingsWindow::WindowData* wd, const SettingsNav::NavState& n)
{
    wd->selectedModeIndex = n.selectedModeIndex;
    wd->hoverSidebarIndex = n.hoverSidebarIndex;
    wd->navLevel = n.navLevel;
    wd->currentCardIndex = n.currentCardIndex;
    wd->hoverRowIndex = n.hoverRowIndex;
    wd->currentMode = (IME_MODE)n.currentMode;
    wd->scrollPos = n.scrollPos;
    wd->scrollMax = n.scrollMax;
    wd->totalContentHeight = n.totalContentHeight;
    wd->sidebarCollapsed = n.sidebarCollapsed != 0;
    wd->sidebarNarrowMode = n.sidebarNarrowMode != 0;
    wd->colorGridExpanded = n.colorGridExpanded != 0;
    memcpy(wd->cardExpanded, n.cardExpanded, sizeof(wd->cardExpanded));
}

// Feed one event to SettingsNav, then carry out its effects in order.
void SettingsWindow::RunNav(HWND hMain, WindowData* wd, SettingsNav::NavEvent ev)
{
    HWND hContent = wd->hContentArea;
    ev.dpi = CConfig::GetDpiForHwnd(hMain);
    ev.colorMode = wd->snapshot.colorMode;
    HWND hSaveBtn = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
    ev.customTableDirty = (hSaveBtn && IsWindowEnabled(hSaveBtn)) ? 1 : 0;
    ev.previewH = 0;
    if (wd->navLevel == 1 && wd->selectedModeIndex < 4 && hContent) {
        HDC hdcTmp = GetDC(hContent);
        ev.previewH = CalcCandidatePreviewH(hdcTmp, wd->snapshot, ev.dpi);
        ReleaseDC(hContent, hdcTmp);
    }

    SettingsNav::NavState nav = LoadNav(wd);
    std::vector<SettingsNav::NavEffect> effects;
    SettingsNav::Handle(nav, ev, effects);
    StoreNav(wd, nav);

    for (const auto& fx : effects) {
        switch (fx.kind) {
        case SettingsNav::FX_INVALIDATE_MAIN:
            InvalidateRect(hMain, fx.hasRect ? &fx.rc : nullptr, fx.erase ? TRUE : FALSE);
            break;
        case SettingsNav::FX_INVALIDATE_CONTENT:
            if (hContent) InvalidateRect(hContent, nullptr, fx.erase ? TRUE : FALSE);
            break;
        case SettingsNav::FX_TRACK_LEAVE_MAIN:
        case SettingsNav::FX_TRACK_LEAVE_CONTENT: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE,
                fx.kind == SettingsNav::FX_TRACK_LEAVE_MAIN ? hMain : hContent };
            TrackMouseEvent(&tme);
            break;
        }
        case SettingsNav::FX_SCROLL_CHANGED:
            UpdateScrollInfo(wd);
            RepositionControlsForScroll(wd);
            RedrawWindow(hContent, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            break;
        case SettingsNav::FX_SHOW_ALL_CONTROLS:
            ShowAllControls(wd);
            break;
        case SettingsNav::FX_HIDE_ALL_CONTROLS:
            for (auto& ch : wd->controlHandles)
                if (ch.hWnd && IsWindow(ch.hWnd)) ShowWindow(ch.hWnd, SW_HIDE);
            break;
        case SettingsNav::FX_SWITCH_MODE:
            SwitchMode(hMain, wd, (IME_MODE)fx.arg);
            break;
        case SettingsNav::FX_REBUILD:
            RebuildContentArea(hMain, wd);
            break;
        case SettingsNav::FX_TOGGLE_CONTROL: {
            SettingsControlId id = (SettingsControlId)fx.arg;
            HWND hCtrl = FindControl(wd, id);
            if (hCtrl) {
                BOOL checked = (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool newState = !checked;
                SendMessage(hCtrl, BM_SETCHECK, newState ? BST_CHECKED : BST_UNCHECKED, 0);
                SettingsModel::SyncToggleToSnapshot(wd->snapshot, id, newState);
                ApplyAndSave(wd);
                InvalidateRect(hContent, nullptr, FALSE);
                UpdateWindow(hContent);
            }
            break;
        }
        case SettingsNav::FX_ASK_SAVE_THEN_BACK: {
            // 自建詞庫 has unsaved changes: ask before leaving the sub-page.
            int result = ThemedMessageBox(hMain,
                L"自建詞庫尚未儲存，是否要儲存後離開？\n\n[是] 儲存並離開\n[否] 不儲存離開\n[取消] 繼續編輯",
                L"自建詞庫未儲存", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (result == IDYES) {
                HWND hSave = FindControl(wd, CTRL_CUSTOM_TABLE_SAVE);
                SendMessageW(hContent, WM_COMMAND,
                    MAKEWPARAM(3000 + CTRL_CUSTOM_TABLE_SAVE, BN_CLICKED), (LPARAM)hSave);
            }
            if (result == IDYES || result == IDNO) {
                SettingsNav::NavEvent back = {};
                back.kind = SettingsNav::EV_BACK;
                RunNav(hMain, wd, back);
            }
            break;
        }
        case SettingsNav::FX_MOVE_CONTENT:
            MoveWindow(hContent, fx.rc.left, fx.rc.top, fx.rc.right, fx.rc.bottom, FALSE);
            break;
        case SettingsNav::FX_SET_FOCUS_CONTENT:
            SetFocus(hContent);
            break;
        case SettingsNav::FX_UPDATE_CONTENT:
            UpdateWindow(hContent);
            break;
        case SettingsNav::FX_UPDATE_SCROLL_INFO:
            UpdateScrollInfo(wd);
            break;
        default:
            break;
        }
    }
}

// ============================================================================
// Shared geometry helpers — used by ALL paint paths and RebuildContentArea
// ============================================================================

// Card width from main window — stable regardless of scrollbar visibility
static int CalcCardW(HWND hMainWnd, UINT dpi, bool sidebarTakesSpace)
{
    RECT rcMain; GetClientRect(hMainWnd, &rcMain);
    return SettingsWindowLayout::CardWidth(rcMain.right, dpi, sidebarTakesSpace,
        GetSystemMetrics(SM_CXVSCROLL));
}

// Right edge X for controls (combo, button, toggle, swatch) — always left of chevron area
static int CalcCtrlRight(int margin, int cardW, UINT dpi, bool hasChevron)
{
    return SettingsWindowLayout::CtrlRight(margin, cardW, dpi, hasChevron);
}

// Candidate preview height; RebuildContentArea and the painter must agree.
static int CalcCandidatePreviewH(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    return SettingsWindowPaint::CandidatePreviewHeight(hdc, s, dpi);
}

HWND SettingsWindow::FindControl(WindowData* wd, SettingsControlId id)
{
    for (auto& ch : wd->controlHandles)
        if (ch.id == id) return ch.hWnd;
    return nullptr;
}

// Map toggle control ID → snapshot bool field, update snapshot from checkbox state
// SyncToggleToSnapshot moved to SettingsController.cpp — use SettingsModel::SyncToggleToSnapshot()

void SettingsWindow::ApplyAndSave(WindowData* wd)
{
    SettingsModel::ApplyToConfig(wd->snapshot);
    CConfig::WriteConfig(wd->currentMode);
}

void SettingsWindow::UpdateTheme(HWND hWnd, WindowData* wd)
{
    wd->isDarkTheme = CConfig::IsSystemDarkTheme();
    CreateThemeBrushes(wd);
    CreateFonts(wd, CConfig::GetDpiForHwnd(hWnd));
    if (wd->isDarkTheme) {
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
    }
    // Match title bar color to content background (DWMWA_CAPTION_COLOR = 36)
    COLORREF captionColor = wd->theme.contentBg;
    DwmSetWindowAttribute(hWnd, 36, &captionColor, sizeof(COLORREF));
    InvalidateRect(hWnd, nullptr, TRUE);
    if (wd->hContentArea) InvalidateRect(wd->hContentArea, nullptr, TRUE);
}

// ============================================================================
// TODO: The rest of the clean implementation follows
// Each function will be written step by step
// ============================================================================

// Placeholder stubs — will be replaced one by one
void SettingsWindow::SwitchMode(HWND hWnd, WindowData* wd, IME_MODE mode)
{
    bool modeChanged = (mode != wd->currentMode);
    wd->currentMode = mode;
    wd->selectedModeIndex = ModeToIndex(mode);
    wd->navLevel = 0;
    // Initialize expand state from layout defaultExpanded
    memset(wd->cardExpanded, 0, sizeof(wd->cardExpanded));
    {
        int imeCount = 0;
        const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
        for (int i = 0; i < imeCount && i < 16; i++)
            wd->cardExpanded[i] = imeCards[i].defaultExpanded;
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        for (int i = 0; i < ltCount && (4 + i) < 16; i++)
            wd->cardExpanded[4 + i] = ltCards[i].defaultExpanded;
    }
    // Re-enumerate and reload only on actual mode change.
    // SetIMEMode() filters self from the list but skips if mode is unchanged.
    // Don't re-enumerate when staying on the same mode (e.g., sidebar click from
    // Level 1 back to Level 0) — the list is already correctly filtered.
    if (modeChanged)
        CConfig::EnumerateReverseConversionProviders(1028);
    CConfig::LoadConfig(mode);
    CConfig::LoadColorsForMode((IME_COLOR_MODE)CConfig::GetColorMode());
    wd->snapshot = SettingsModel::LoadFromConfig();
    // Recreate composition engine for the new mode (custom table validation)
    SettingsModel::DestroyEngine(wd->pEngine);
    wd->pEngine = SettingsModel::CreateEngine(mode);
    RebuildContentArea(hWnd, wd);
    InvalidateRect(hWnd, nullptr, TRUE);
}

// Everything the painters read. hPaint is the window painted into (its client
// size); dpi and card width come from the main window.
static SettingsWindowPaint::PaintContext MakePaintContext(HWND hPaint, HWND hMain,
    SettingsWindow::WindowData* wd)
{
    SettingsWindowPaint::PaintContext ctx = {};
    ctx.theme = wd->theme;
    ctx.snapshot = wd->snapshot;
    ctx.fontTitle = wd->hFontTitle;
    ctx.fontCardHeader = wd->hFontCardHeader;
    ctx.fontBody = wd->hFontBody;
    ctx.fontDesc = wd->hFontDesc;
    ctx.fontMDL2 = wd->hFontMDL2;
    ctx.fontMDL2Icon = wd->hFontMDL2Icon;
    ctx.brushSidebarBg = wd->hBrushSidebarBg;
    ctx.brushContentBg = wd->hBrushContentBg;
    ctx.iconModule = GetModuleHandleW(nullptr);
    ctx.dpi = CConfig::GetDpiForHwnd(hMain);
    ctx.hasMDL2 = wd->hasMDL2 ? 1 : 0;
    RECT rc; GetClientRect(hPaint, &rc);
    ctx.clientW = rc.right;
    ctx.clientH = rc.bottom;
    ctx.cardW = CalcCardW(hMain, ctx.dpi, !wd->sidebarNarrowMode && !wd->sidebarCollapsed);
    ctx.sysFace = GetSysColor(COLOR_3DFACE);
    ctx.selectedModeIndex = wd->selectedModeIndex;
    ctx.hoverSidebarIndex = wd->hoverSidebarIndex;
    ctx.navLevel = wd->navLevel;
    ctx.currentCardIndex = wd->currentCardIndex;
    ctx.hoverRowIndex = wd->hoverRowIndex;
    ctx.currentMode = (int)wd->currentMode;
    ctx.scrollPos = wd->scrollPos;
    ctx.sidebarNarrowMode = wd->sidebarNarrowMode ? 1 : 0;
    ctx.sidebarCollapsed = wd->sidebarCollapsed ? 1 : 0;
    ctx.colorGridExpanded = wd->colorGridExpanded ? 1 : 0;
    memcpy(ctx.cardExpanded, wd->cardExpanded, sizeof(ctx.cardExpanded));
    // Toggle state of the first control with each id (FindControl order).
    bool seen[SettingsWindowPaint::kToggleSlots] = {};
    for (auto& ch : wd->controlHandles) {
        if (ch.id < 0 || ch.id >= SettingsWindowPaint::kToggleSlots || seen[ch.id]) continue;
        seen[ch.id] = true;
        ctx.toggleOn[ch.id] = (ch.hWnd && SendMessage(ch.hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    }
    return ctx;
}

void SettingsWindow::PaintSidebar(HWND hWnd, HDC hdc, WindowData* wd)
{
    SettingsWindowPaint::PaintContext ctx = MakePaintContext(hWnd, hWnd, wd);
    SettingsWindowPaint::PaintSidebar(hdc, ctx);
}

void SettingsWindow::PaintContent(HWND hWnd, HDC hdc, WindowData* wd)
{
    SettingsWindowPaint::PaintContext ctx = MakePaintContext(hWnd, GetParent(hWnd), wd);
    SettingsWindowPaint::PaintContent(hdc, ctx);
}

void SettingsWindow::PaintHamburger(HWND hMain, HDC hdc, WindowData* wd, RECT rc, HBRUSH bg)
{
    SettingsWindowPaint::PaintContext ctx = MakePaintContext(hMain, hMain, wd);
    SettingsWindowPaint::PaintHamburger(hdc, ctx, rc, bg);
}

void SettingsWindow::AddControl(WindowData* wd, SettingsControlId id, HWND h, int x, int y, int w, int ht, int visH)
{
    WindowData::ControlHandle ch = {};
    ch.id = id; ch.hWnd = h; ch.origX = x; ch.origY = y; ch.origW = w; ch.origH = ht;
    ch.visH = (visH > 0) ? visH : ht;
    wd->controlHandles.push_back(ch);
    if (h) SetWindowSubclass(h, ChildWheelSubclassProc, 1, 0);
}

// ============================================================================
// CreateRowControl — unified control creation for any RowType
// ============================================================================
void SettingsWindow::CreateRowControl(WindowData* wd, const LayoutRow& lr,
    int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi)
{
    SettingsWindowLayout::CtrlRect rc = {};
    if (!SettingsWindowLayout::RowControlRect(lr.type, ctrlRight, cy, ctrlH, btnH, dpi, &rc))
        return;

    switch (lr.type) {
    case RowType::Toggle:
    {
        HWND h = CreateWindowExW(0, L"BUTTON", lr.label,
            WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        AddControl(wd, lr.id, h, rc.x, rc.y, rc.w, rc.h);
        break;
    }
    case RowType::ComboBox:
    {
        HWND h = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            rc.x, rc.y, rc.w, rc.h,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) {
            SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
            if (wd->isDarkTheme) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        }
        AddControl(wd, lr.id, h, rc.x, rc.y, rc.w, rc.h, rc.visH);
        break;
    }
    case RowType::Edit:
    {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            rc.x, rc.y, rc.w, rc.h,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) {
            SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
            if (wd->isDarkTheme) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        }
        AddControl(wd, lr.id, h, rc.x, rc.y, rc.w, rc.h);
        break;
    }
    case RowType::Button:
    case RowType::Clickable:
    {
        const wchar_t* btnText = (lr.action == RowAction::LoadTable) ? L"載入" :
                                 (lr.action == RowAction::OpenFontDialog) ? L"變更字型" :
                                 (lr.action == RowAction::ExportCustomTable) ? L"匯出" :
                                 (lr.action == RowAction::ImportCustomTable) ? L"匯入" :
                                 (lr.action == RowAction::ResetDefaults) ? L"還原預設值" : lr.label;
        HWND h = CreateWindowExW(0, L"BUTTON", btnText,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            rc.x, rc.y, rc.w, rc.h,
            wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
        if (h) SendMessage(h, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
        AddControl(wd, lr.id, h, rc.x, rc.y, rc.w, rc.h);
        break;
    }
    default: break;
    }
}

// ============================================================================
// CreateColorGridControls — owner-draw swatch grid for RowType::ColorGrid
// ============================================================================
void SettingsWindow::CreateColorGridControls(WindowData* wd, int margin, int curY, int ctrlRight, UINT dpi)
{
    for (int si = 0; si < 6; si++) {
        SettingsWindowLayout::CtrlRect rc = SettingsWindowLayout::SwatchRect(si, margin, curY, ctrlRight, dpi);
        HWND h = CreateWindowExW(0, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            rc.x, rc.y, rc.w, rc.h,
            wd->hContentArea, (HMENU)(UINT_PTR)(4000 + si), wd->hInstance, nullptr);
        AddControl(wd, (SettingsControlId)(CTRL_COLOR_FR + si), h, rc.x, rc.y, rc.w, rc.h);
    }
}

// ============================================================================
// CreateRichEditorControl — owner-draw editor for RowType::RichEditor
// ============================================================================
void SettingsWindow::CreateRichEditorControl(WindowData* wd, const LayoutRow& lr,
    int margin, int cardW, int pad, int curY, int thisH, int ctrlRight, int btnW, int btnH, UINT dpi)
{
    static HMODULE hMsftEdit = LoadLibraryW(L"msftedit.dll");
    SettingsWindowLayout::CtrlRect rcRE = {}, rcSave = {};
    SettingsWindowLayout::RichEditorRects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi,
        &rcRE, &rcSave);
    int reX = rcRE.x, reY = rcRE.y, reW = rcRE.w, reH = rcRE.h;
    HWND hRE = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        reX, reY, reW, reH,
        wd->hContentArea, (HMENU)(UINT_PTR)(3000 + lr.id), wd->hInstance, nullptr);
    if (hRE) {
        // Issue #130: raise the text limit. RichEdit50W defaults to ~32K-64K
        // characters; SetWindowTextW bypasses the cap (large bulk loads succeed)
        // but the editor then silently rejects user input because the buffer is
        // already over-limit. Symptom: caret moves but typing does nothing after
        // a 1.6 MB Big5 import. EM_EXLIMITTEXT raises the cap to 100 MB chars.
        SendMessageW(hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100 * 1024 * 1024));
        SendMessage(hRE, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
        if (wd->isDarkTheme) {
            SetWindowTheme(hRE, L"DarkMode_Explorer", nullptr);
            SendMessage(hRE, EM_SETBKGNDCOLOR, 0, (LPARAM)wd->theme.cardBg);
            CHARFORMAT2W cf2 = {};
            cf2.cbSize = sizeof(cf2);
            cf2.dwMask = CFM_COLOR;
            cf2.crTextColor = wd->theme.textPrimary;
            SendMessage(hRE, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf2);
        }
    }
    AddControl(wd, lr.id, hRE, reX, reY, reW, reH);
    if (hRE) RemoveWindowSubclass(hRE, ChildWheelSubclassProc, 1);

    // Load existing custom table from file. Issue #130: use the encoding-aware
    // helper. The file we wrote ourselves is UTF-16LE+BOM, but be defensive in
    // case a user externally edited the file with a non-UTF-16 editor.
    if (hRE) {
        WCHAR txtPath[MAX_PATH] = {};
        GetCustomTableTxtPath(wd->currentMode, txtPath, MAX_PATH);
        if (PathFileExistsW(txtPath)) {
            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
            StringCchCopyW(cf.szFaceName, LF_FACESIZE, L"Microsoft JhengHei UI");
            cf.yHeight = 10 * 20;  // 10pt in twips — RichEdit applies DPI itself
            cf.wWeight = FW_NORMAL;
            SendMessageW(hRE, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

            size_t wlen = 0;
            LPWSTR text = SettingsModel::LoadTextFileAsUtf16(txtPath, &wlen);
            if (text) {
                SetWindowTextW(hRE, text);
                delete[] text;
            }
        }
        SetWindowSubclass(hRE, CustomTableRE_SubclassProc, 0, 0);
        SendMessageW(hRE, EM_SETEVENTMASK, 0, ENM_CHANGE);
        wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
        wd->ctLastEditedLine = -1;
        wd->ctKeystrokeCount = 0;
        // Issue #130 Phase 3 (final): paint visible range immediately (TOM-based,
        // no caret jump). NO background scan — even with TOM, ITextDocument::Range()
        // walks the gap buffer per call, so painting all 73K lines O(N²) starves
        // the message pump and makes the editor unresponsive. Save's full in-memory
        // validation catches errors anywhere in the file with line numbers.
        // Save button stays disabled (WS_DISABLED) — content matches disk.
        ValidateViewport(hRE, wd);
    }

    // [儲存] button below RichEdit
    HWND hSave = CreateWindowExW(0, L"BUTTON", L"儲存",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
        rcSave.x, rcSave.y, rcSave.w, rcSave.h,
        wd->hContentArea, (HMENU)(UINT_PTR)(3000 + CTRL_CUSTOM_TABLE_SAVE), wd->hInstance, nullptr);
    if (hSave) SendMessage(hSave, WM_SETFONT, (WPARAM)wd->hFontBody, TRUE);
    AddControl(wd, CTRL_CUSTOM_TABLE_SAVE, hSave, rcSave.x, rcSave.y, rcSave.w, rcSave.h);
}

// Candidate + Notify preview — owner-draw card for RowType::CandidatePreview
void SettingsWindow::PaintBreadcrumb(HWND hWnd, HDC hdc, WindowData* wd)
{
    SettingsWindowPaint::PaintContext ctx = MakePaintContext(hWnd, GetParent(hWnd), wd);
    SettingsWindowPaint::PaintBreadcrumb(hdc, ctx);
}
void SettingsWindow::RebuildContentArea(HWND hWnd, WindowData* wd)
{
    if (!wd->hContentArea) return;

    // Destroy all existing child controls
    for (auto& ch : wd->controlHandles) {
        if (ch.hWnd) DestroyWindow(ch.hWnd);
    }
    wd->controlHandles.clear();

    // Position content area based on navLevel:
    // navLevel==1 (sub-page): start below the breadcrumb strip painted in main WndProc
    // navLevel==0: start at y=0
    bool sidebarTakesSpace = !wd->sidebarNarrowMode && !wd->sidebarCollapsed;
    {
        RECT rcMain; GetClientRect(hWnd, &rcMain);
        UINT dpiPos = CConfig::GetDpiForHwnd(hWnd);
        bool hasBc = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
        SettingsWindowLayout::CtrlRect rcArea = SettingsWindowLayout::ContentAreaRect(
            rcMain.right, rcMain.bottom, dpiPos, sidebarTakesSpace, hasBc);
        MoveWindow(wd->hContentArea, rcArea.x, rcArea.y, rcArea.w, rcArea.h, FALSE);
    }

    UINT dpi = CConfig::GetDpiForHwnd(hWnd);
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cardW = CalcCardW(hWnd, dpi, sidebarTakesSpace);
    int btnW = ScaleForDpi(g_geo.buttonWidth, dpi);
    int ctrlH = ScaleForDpi(g_geo.ctrlHeight, dpi);
    int btnH = ScaleForDpi(g_geo.buttonHeight, dpi);
    // Level 0 children: no chevron. Level 1 sub-pages: has chevron.
    bool isLevel1 = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
    int ctrlRight = CalcCtrlRight(margin, cardW, dpi, isLevel1);

    int imeCount = 0;
    const LayoutCard* imeCards = GetIMEPageLayout(&imeCount);
    int ltCount = 0;
    const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);

    SettingsWindowLayout::PlanInput in = {};
    in.dpi = dpi;
    in.selectedModeIndex = wd->selectedModeIndex;
    in.navLevel = wd->navLevel;
    in.currentCardIndex = wd->currentCardIndex;
    in.modeMask = SettingsModel::GetModeBitmask(wd->currentMode);
    in.colorMode = wd->snapshot.colorMode;
    in.gridExpanded = wd->colorGridExpanded ? 1 : 0;
    in.previewH = 0;
    if (wd->navLevel == 1 && wd->selectedModeIndex < 4) {
        HDC hdcTmp = GetDC(wd->hContentArea);
        in.previewH = CalcCandidatePreviewH(hdcTmp, wd->snapshot, dpi);
        ReleaseDC(wd->hContentArea, hdcTmp);
    }
    memcpy(in.expanded, wd->cardExpanded, sizeof(in.expanded));

    std::vector<SettingsWindowLayout::Placement> placements;
    SettingsWindowLayout::PlanResult plan = SettingsWindowLayout::PlanControls(
        in, imeCards, imeCount, ltCards, ltCount, placements);

    for (const auto& pl : placements) {
        const LayoutCard* cards = (wd->selectedModeIndex == 5) ? ltCards : imeCards;
        const LayoutRow& lr = cards[pl.card].rows[pl.row];
        switch (pl.kind) {
        case 0: CreateRowControl(wd, lr, ctrlRight, pl.y, ctrlH, btnH, dpi); break;
        case 1: CreateRichEditorControl(wd, lr, margin, cardW, pad, pl.y, pl.h, ctrlRight, btnW, btnH, dpi); break;
        case 2: CreateColorGridControls(wd, margin, pl.y, ctrlRight, dpi); break;
        default: break;
        }
    }
    if (plan.setTotal) wd->totalContentHeight = plan.totalHeight;
    if (plan.populate) PopulateControls(wd);

    wd->scrollPos = 0;
    UpdateScrollInfo(wd);
    InvalidateRect(wd->hContentArea, nullptr, TRUE);
}
static void ComboAddAndSelect(HWND hCombo, const wchar_t* items[], int count, int sel)
{
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
    if (sel >= 0 && sel < count)
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
}

void SettingsWindow::PopulateControls(WindowData* wd)
{
    const SettingsSnapshot& s = wd->snapshot;
    HWND h;

    if ((h = FindControl(wd, CTRL_COLOR_MODE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        // Must match ConfigDialog.cpp exactly: "跟隨系統模式" only on Win10 1809+
        if (Global::isWindows1809OrLater) {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"跟隨系統模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"淡色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"深色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"自訂");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM));
        }
        // Select by item data matching colorMode
        int count = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < count; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.colorMode) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0);
                break;
            }
        }
    }
    if ((h = FindControl(wd, CTRL_SHOW_NOTIFY)))
        SendMessage(h, BM_SETCHECK, s.showNotifyDesktop ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_SCOPE))) {
        // Exact from ConfigDialog.cpp — uses CB_SETITEMDATA
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        struct { const wchar_t* text; int data; } arrayScopeItems[] = {
            { L"行列30 Big5 (繁體中文)", (int)ARRAY_SCOPE::ARRAY30_BIG5 },
            { L"行列30 Unicode Ext-A",  (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A },
            { L"行列30 Unicode Ext-AB", (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB },
            { L"行列30 Unicode Ext-A~D",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD },
            { L"行列30 Unicode Ext-A~J",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A_TO_J },
            { L"行列40 Big5",           (int)ARRAY_SCOPE::ARRAY40_BIG5 },
        };
        for (auto& item : arrayScopeItems) {
            LRESULT idx = SendMessage(h, CB_ADDSTRING, 0, (LPARAM)item.text);
            SendMessage(h, CB_SETITEMDATA, idx, (LPARAM)item.data);
        }
        // Select by item data
        int cnt = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < cnt; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.arrayScope) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0); break;
            }
        }
    }
    if ((h = FindControl(wd, CTRL_CHARSET_SCOPE))) {
        const wchar_t* f[] = { L"完整字集", L"繁體中文" };
        ComboAddAndSelect(h, f, 2, s.big5Filter ? 1 : 0);
    }
    if ((h = FindControl(wd, CTRL_NUMERIC_PAD))) {
        const wchar_t* p[] = { L"數字鍵盤輸入數字符號", L"數字鍵盤輸入字根", L"僅用數字鍵盤輸入字根" };
        ComboAddAndSelect(h, p, 3, s.numericPad);
    }
    if ((h = FindControl(wd, CTRL_SHIFT_ENGLISH_DIGIT_MODE))) {
        const wchar_t* d[] = {
            L"CapsLock 關輸出符號",
            L"CapsLock 關輸出數字",
            L"永遠輸出符號"
        };
        ComboAddAndSelect(h, d, 3, s.shiftEnglishDigitMode);
    }
    if ((h = FindControl(wd, CTRL_DAYI_SELKEY_MODE))) {
        const wchar_t* k[] = { L"依碼表設定", L"數字鍵 1~0", L"大易選擇鍵 ' [ ] - \\" };
        ComboAddAndSelect(h, k, 3, s.dayiSelkeyMode);
    }
    if ((h = FindControl(wd, CTRL_PHONETIC_KB))) {
        const wchar_t* l[] = { L"標準鍵盤", L"倚天鍵盤" };
        ComboAddAndSelect(h, l, 2, s.phoneticKeyboardLayout);
    }
    if ((h = FindControl(wd, CTRL_MAX_CODES))) {
        WCHAR buf[16]; StringCchPrintfW(buf, 16, L"%u", s.maxCodes);
        SetWindowTextW(h, buf);
    }
    if ((h = FindControl(wd, CTRL_ARROW_KEY_SW_PAGES)))
        SendMessage(h, BM_SETCHECK, s.arrowKeySWPages ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_SPACE_AS_PAGEDOWN)))
        SendMessage(h, BM_SETCHECK, s.spaceAsPageDown ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_SPACE_AS_FIRST_CAND)))
        SendMessage(h, BM_SETCHECK, s.spaceAsFirstCandSelkey ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_AUTO_COMPOSE)))
        SendMessage(h, BM_SETCHECK, s.autoCompose ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_CLEAR_ON_BEEP)))
        SendMessage(h, BM_SETCHECK, s.clearOnBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_MAKE_PHRASE)))
        SendMessage(h, BM_SETCHECK, s.makePhrase ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_CUSTOM_TABLE_PRIORITY)))
        SendMessage(h, BM_SETCHECK, s.customTablePriority ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP)))
        SendMessage(h, BM_SETCHECK, s.doBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP_NOTIFY)))
        SendMessage(h, BM_SETCHECK, s.doBeepNotify ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_DO_BEEP_CANDI)))
        SendMessage(h, BM_SETCHECK, s.doBeepOnCandi ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_IME_SHIFT_MODE))) {
        const wchar_t* m[] = { L"左右SHIFT鍵", L"右SHIFT鍵", L"左SHIFT鍵", L"無(僅Ctrl-Space鍵)" };
        ComboAddAndSelect(h, m, 4, s.imeShiftMode);
    }
    if ((h = FindControl(wd, CTRL_DOUBLE_SINGLE_BYTE))) {
        const wchar_t* d[] = { L"以 Shift-Space 熱鍵切換", L"半型", L"全型" };
        ComboAddAndSelect(h, d, 3, s.doubleSingleByteMode);
    }
    if ((h = FindControl(wd, CTRL_REVERSE_CONVERSION))) {
        // List is populated at startup and filtered by LoadConfig in SwitchMode
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"(無)");
        if (IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL))
            SendMessage(h, CB_SETCURSEL, (WPARAM)0, 0);
        CDIMEArray<LanguageProfileInfo>* infoList = CConfig::GetReverseConvervsionInfoList();
        if (infoList) {
            for (UINT i = 0; i < infoList->Count(); i++) {
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)infoList->GetAt(i)->description);
                if (IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), infoList->GetAt(i)->guidProfile))
                    SendMessage(h, CB_SETCURSEL, (WPARAM)i + 1, 0);
            }
        }
    }
    if ((h = FindControl(wd, CTRL_KEYBOARD_OPEN_CLOSE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"中文模式");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"英數模式");
        SendMessage(h, CB_SETCURSEL, s.activatedKeyboardMode ? 0 : 1, 0);
    }
    if ((h = FindControl(wd, CTRL_OUTPUT_CHT_CHS))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"不轉換");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"繁轉簡");
        SendMessage(h, CB_SETCURSEL, s.doHanConvert ? 1 : 0, 0);
    }
    if ((h = FindControl(wd, CTRL_DAYI_ARTICLE)))
        SendMessage(h, BM_SETCHECK, s.dayiArticleMode ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_SENTENCE_MODE)))
        SendMessage(h, BM_SETCHECK, s.sentenceMode ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_FORCE_SP)))
        SendMessage(h, BM_SETCHECK, s.arrayForceSP ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_NOTIFY_SP)))
        SendMessage(h, BM_SETCHECK, s.arrayNotifySP ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((h = FindControl(wd, CTRL_ARRAY_SINGLE_QUOTE)))
        SendMessage(h, BM_SETCHECK, s.arraySingleQuoteCustomPhrase ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ============================================================================
// Window class registration
// ============================================================================
bool SettingsWindow::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = SETTINGS_WND_CLASS;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DIMESETTINGS));
    wcex.hIconSm = wcex.hIcon;
    return RegisterClassExW(&wcex) != 0;
}

static void RegisterContentClass(HINSTANCE hInstance)
{
    if (s_contentClassRegistered) return;
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = SettingsWindow::ContentWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = CONTENT_WND_CLASS;
    RegisterClassExW(&wcex);
    s_contentClassRegistered = true;
}

// ============================================================================
// Run — main entry point
// ============================================================================
int SettingsWindow::Run(HINSTANCE hInstance, int nCmdShow, IME_MODE initialMode)
{
    // Initialize GDI+ for anti-aliased toggle switches
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    RegisterWindowClass(hInstance);
    UINT dpi = 96;
    { HDC hdc = GetDC(nullptr); if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(nullptr, hdc); } }

    WCHAR caption[128];
    StringCchPrintfW(caption, 128, L"DIME 設定 v%d.%d.%d", BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT);

    HWND hWnd = CreateWindowExW(0, SETTINGS_WND_CLASS, caption,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ScaleForDpi(g_geo.windowDefaultW, dpi), ScaleForDpi(g_geo.windowDefaultH, dpi),
        nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return -1;

    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (wd) {
        wd->hInstance = hInstance;
        SwitchMode(hWnd, wd, initialMode);
    }
    // Restore saved window placement, or use default (CW_USEDEFAULT + nCmdShow)
    if (!RestoreWindowPlacement(hWnd))
        ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

// ============================================================================
// Main window procedure
// ============================================================================
LRESULT CALLBACK SettingsWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE:
    {
        wd = new WindowData();
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)wd);
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
        wd->hInstance = hInst;
        wd->isDarkTheme = CConfig::IsSystemDarkTheme();
        UINT dpi = CConfig::GetDpiForHwnd(hWnd);
        CreateThemeBrushes(wd);
        CreateFonts(wd, dpi);
        if (wd->isDarkTheme) {
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
        }
        // Match title bar color to content background (DWMWA_CAPTION_COLOR = 35)
        COLORREF cbg = wd->theme.contentBg;
        DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
        wd->micaActive = false; // Don't use Mica — it makes title bar translucent

        RegisterContentClass(hInst);
        RECT rc; GetClientRect(hWnd, &rc);
        int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
        wd->hContentArea = CreateWindowExW(0, CONTENT_WND_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            sidebarW, 0, rc.right - sidebarW, rc.bottom,
            hWnd, (HMENU)IDC_CONTENT_AREA, hInst, nullptr);
        SetWindowLongPtr(wd->hContentArea, GWLP_USERDATA, (LONG_PTR)wd);
        if (wd->isDarkTheme)
            SetWindowTheme(wd->hContentArea, L"DarkMode_Explorer", nullptr);
        return 0;
    }

    case WM_SIZE:
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_SIZE;
            ev.x = LOWORD(lParam);
            ev.y = HIWORD(lParam);
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);
        HDC hdc = hdcScreen;
        RECT rcWnd; GetClientRect(hWnd, &rcWnd);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBmpMem = CreateCompatibleBitmap(hdcScreen, rcWnd.right, rcWnd.bottom);
        HBITMAP hOldBmpMem = (HBITMAP)SelectObject(hdcMem, hBmpMem);
        if (hdcMem) hdc = hdcMem;
        if (wd) {
            PaintSidebar(hWnd, hdc, wd);
            // Paint breadcrumb (navLevel==1 only)
            if (wd->navLevel == 1 && wd->selectedModeIndex < 4 && wd->hContentArea) {
                UINT dpi = CConfig::GetDpiForHwnd(hWnd);
                int sidebarW = (!wd->sidebarNarrowMode && !wd->sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
                int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
                // Sidebar overlay covers breadcrumb in narrow mode when open
                if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
                    int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpi);
                    // Paint sidebar bg over breadcrumb strip (clipped to sidebar width)
                    HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, bcH);
                    int savedDC = SaveDC(hdc);
                    ExtSelectClipRgn(hdc, hClip, RGN_AND);
                    PaintSidebar(hWnd, hdc, wd);
                    RestoreDC(hdc, savedDC);
                    DeleteObject(hClip);
                    // Fill remainder of breadcrumb strip with content bg
                    RECT rcBcRest = { sidebarWOv, 0, 32767, bcH };
                    FillRect(hdc, &rcBcRest, wd->hBrushContentBg);
                    // Hamburger on top of sidebar overlay (so user can close it)
                    {
                        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
                        RECT rcHbBg = { 0, 0, hbSize, hbSize };
                        PaintHamburger(hWnd, hdc, wd, rcHbBg, wd->hBrushSidebarBg);
                    }
                } else {
                    RECT rcBC = { sidebarW, 0, 32767, bcH };
                    FillRect(hdc, &rcBC, wd->hBrushContentBg);
                    int savedDC = SaveDC(hdc);
                    SetViewportOrgEx(hdc, sidebarW, 0, nullptr);
                    PaintBreadcrumb(wd->hContentArea, hdc, wd);
                    RestoreDC(hdc, savedDC);
                    // Hamburger in breadcrumb strip (Level 1 narrow mode)
                    if (wd->sidebarNarrowMode) {
                        int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
                        RECT rcHb = { sidebarW, 0, sidebarW + hbSize, hbSize };
                        PaintHamburger(hWnd, hdc, wd, rcHb, wd->hBrushContentBg);
                    }
                }
            }
            // Hamburger and sidebar overlay are drawn in ContentWndProc (content area covers main window in narrow mode)
        }
        if (hdcMem) {
            BitBlt(hdcScreen, 0, 0, rcWnd.right, rcWnd.bottom, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOldBmpMem);
            DeleteObject(hBmpMem);
            DeleteDC(hdcMem);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (wd && wd->hContentArea) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_WHEEL;
            ev.delta = GET_WHEEL_DELTA_WPARAM(wParam);
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    {
        // Sidebar click / hover, breadcrumb back link, narrow-mode hamburger
        if (!wd) break;
        SettingsNav::NavEvent ev = {};
        ev.kind = (msg == WM_LBUTTONDOWN) ? SettingsNav::EV_MAIN_LBUTTON : SettingsNav::EV_MAIN_MOUSEMOVE;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        RunNav(hWnd, wd, ev);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_MAIN_MOUSELEAVE;
            RunNav(hWnd, wd, ev);
        }
        return 0;

    case WM_THEMECHANGED:
        if (wd) UpdateTheme(hWnd, wd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        UINT dpi2 = 96;
        HDC hdc2 = GetDC(nullptr);
        if (hdc2) { dpi2 = GetDeviceCaps(hdc2, LOGPIXELSY); ReleaseDC(nullptr, hdc2); }
        mmi->ptMinTrackSize = SettingsNav::MinTrackSize(dpi2);
        return 0;
    }

    case WM_COPYDATA:
    {
        if (!wd) break;
        COPYDATASTRUCT* pCds = (COPYDATASTRUCT*)lParam;
        if (pCds && pCds->lpData) {
            const wchar_t* cmdLine = (const wchar_t*)pCds->lpData;
            const wchar_t* modeStr = wcsstr(cmdLine, L"--mode ");
            if (modeStr) {
                modeStr += 7;
                wchar_t modeName[32] = {};
                for (int j = 0; modeStr[j] && modeStr[j] != L' ' && j < 31; j++)
                    modeName[j] = modeStr[j];
                IME_MODE mode = SettingsModel::StringToImeMode(modeName);
                if (mode != IME_MODE::IME_MODE_NONE)
                    SwitchMode(hWnd, wd, mode);
            }
            SetForegroundWindow(hWnd);
        }
        return TRUE;
    }

    case WM_SETTINGCHANGE:
    {
        if (!wd) break;
        // Detect system theme change (dark/light mode)
        bool wasDark = wd->isDarkTheme;
        wd->isDarkTheme = CConfig::IsSystemDarkTheme();
        if (wasDark != wd->isDarkTheme) {
            UpdateTheme(hWnd, wd);
            // Re-apply dark mode to title bar
            BOOL useDark = wd->isDarkTheme ? TRUE : FALSE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
            // Update title bar color
            COLORREF cbg = wd->theme.contentBg;
            DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
            // Dark mode scrollbar
            if (wd->hContentArea)
                SetWindowTheme(wd->hContentArea, wd->isDarkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
            // Rebuild to re-apply dark theme to child controls (combos, edits)
            RebuildContentArea(hWnd, wd);
        }
        return 0;
    }

    case WM_DPICHANGED:
    {
        if (!wd) break;
        UINT newDpi = HIWORD(wParam);
        // Resize window to suggested rect
        RECT* prcNew = (RECT*)lParam;
        if (prcNew)
            SetWindowPos(hWnd, nullptr, prcNew->left, prcNew->top,
                prcNew->right - prcNew->left, prcNew->bottom - prcNew->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        // Recreate fonts for new DPI
        DestroyFonts(wd);
        CreateFonts(wd, newDpi);
        // Rebuild content with new DPI-scaled positions
        RebuildContentArea(hWnd, wd);
        // Update title bar color
        COLORREF cbg = wd->theme.contentBg;
        DwmSetWindowAttribute(hWnd, 35, &cbg, sizeof(COLORREF));
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }

    case WM_DESTROY:
        SaveWindowPlacement(hWnd);
        if (wd) {
            // Issue #130 Phase 3: kill any in-flight BG scan timers and free
            // the snapshot before tearing down the WindowData. Content-area
            // child window owns the timers; cancel here too in case the
            // content-area WndProc didn't get its own WM_DESTROY first.
            CancelBgScan(wd->hContentArea, wd);
            SettingsModel::DestroyEngine(wd->pEngine);
            DestroyThemeBrushes(wd);
            DestroyFonts(wd);
            for (auto& ch : wd->controlHandles) { if (ch.hWnd) DestroyWindow(ch.hWnd); }
            delete wd;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Content area window procedure
// ============================================================================
LRESULT CALLBACK SettingsWindow::ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WindowData* wd = (WindowData*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);
        if (wd) {
            // Double-buffer: paint to off-screen bitmap, then BitBlt to screen
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            HDC hdc = CreateCompatibleDC(hdcScreen);
            HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, rcClient.right, rcClient.bottom);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);

            PaintContent(hWnd, hdc, wd);
            // Sidebar overlay — painted within off-screen DC, clipped to sidebar width
            if (wd->sidebarNarrowMode && !wd->sidebarCollapsed) {
                HWND hMain = GetParent(hWnd);
                UINT dpiOv = CConfig::GetDpiForHwnd(hMain);
                int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpiOv);
                for (auto& ch : wd->controlHandles) {
                    if (ch.hWnd && IsWindow(ch.hWnd))
                        ShowWindow(ch.hWnd, SW_HIDE);
                }
                bool hasBc = (wd->navLevel == 1 && wd->selectedModeIndex < 4);
                int bcOff = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpiOv) : 0;
                HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, 32767);
                int savedOv = SaveDC(hdc);
                ExtSelectClipRgn(hdc, hClip, RGN_AND);
                if (bcOff) SetViewportOrgEx(hdc, 0, -bcOff, nullptr);
                PaintSidebar(hMain, hdc, wd);
                RestoreDC(hdc, savedOv);
                DeleteObject(hClip);
            }
            // Hamburger in content area (Level 0 only; Level 1 draws it in breadcrumb strip)
            if (wd->sidebarNarrowMode && wd->navLevel == 0) {
                UINT dpiHb = CConfig::GetDpiForHwnd(GetParent(hWnd));
                int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpiHb);
                RECT rcHb = { 0, 0, hbSize, hbSize };
                PaintHamburger(GetParent(hWnd), hdc, wd, rcHb, wd->hBrushContentBg);
            }
            // Blit off-screen buffer to screen (excludes child control areas via WS_CLIPCHILDREN)
            BitBlt(hdcScreen, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
            SelectObject(hdc, hOldBmp);
            DeleteObject(hBmp);
            DeleteDC(hdc);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    {
        if (!wd) break;
        SettingsNav::NavEvent ev = {};
        switch (msg) {
        case WM_VSCROLL:
            ev.kind = SettingsNav::EV_CONTENT_VSCROLL;
            ev.delta = LOWORD(wParam);
            ev.thumb = HIWORD(wParam);
            break;
        case WM_MOUSEWHEEL:
            ev.kind = SettingsNav::EV_CONTENT_WHEEL;
            ev.delta = GET_WHEEL_DELTA_WPARAM(wParam);
            break;
        case WM_MOUSEMOVE:
            ev.kind = SettingsNav::EV_CONTENT_MOUSEMOVE;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            break;
        default:
            ev.kind = SettingsNav::EV_CONTENT_LBUTTON;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            break;
        }
        RunNav(GetParent(hWnd), wd, ev);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (wd) {
            SettingsNav::NavEvent ev = {};
            ev.kind = SettingsNav::EV_CONTENT_MOUSELEAVE;
            RunNav(GetParent(hWnd), wd, ev);
        }
        break;
    }

    case WM_COMMAND:
    {
        if (!wd) break;
        int notifyCode = HIWORD(wParam);
        int ctrlId = LOWORD(wParam);

        if (notifyCode == BN_CLICKED) {
            // Action-based dispatch: find LayoutRow by id, dispatch by row->action
            SettingsControlId btnId = (SettingsControlId)(ctrlId - 3000);
            RowAction action = RowAction::None;

            // Search all layout trees for matching row
            auto findAction = [&](const LayoutRow* rows, int count) {
                for (int i = 0; i < count; i++)
                    if (rows[i].id == btnId) { action = rows[i].action; return; }
            };
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
            for (int ci = 0; ci < layoutCount && action == RowAction::None; ci++)
                if (cards[ci].rows) findAction(cards[ci].rows, cards[ci].rowCount);
            if (action == RowAction::None) {
                int ltCount = 0;
                const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
                for (int ci = 0; ci < ltCount && action == RowAction::None; ci++)
                    if (ltCards[ci].rows) findAction(ltCards[ci].rows, ltCards[ci].rowCount);
            }
            // Programmatically created button not in layout tree
            if (action == RowAction::None && btnId == CTRL_CUSTOM_TABLE_SAVE)
                action = RowAction::SaveCustomTable;

            switch (action) {
            case RowAction::OpenFontDialog:
            {
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pCF = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSEFONTW)>(GetProcAddress(hDlgLib, "ChooseFontW"));
                    if (pCF) {
                        HDC hdcScr = GetDC(hWnd);
                        int lpY = GetDeviceCaps(hdcScr, LOGPIXELSY);
                        ReleaseDC(hWnd, hdcScr);
                        LOGFONTW lf = {};
                        StringCchCopyW(lf.lfFaceName, LF_FACESIZE, wd->snapshot.fontFaceName);
                        lf.lfHeight = -MulDiv(wd->snapshot.fontSize, lpY, 72);
                        lf.lfWeight = wd->snapshot.fontWeight;
                        lf.lfItalic = (BYTE)wd->snapshot.fontItalic;
                        lf.lfCharSet = DEFAULT_CHARSET;
                        CHOOSEFONTW cf = {};
                        cf.lStructSize = sizeof(cf);
                        cf.hwndOwner = GetParent(hWnd);
                        cf.lpLogFont = &lf;
                        cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
                        if (pCF(&cf)) {
                            StringCchCopyW(wd->snapshot.fontFaceName, LF_FACESIZE, lf.lfFaceName);
                            wd->snapshot.fontSize = cf.iPointSize / 10;
                            wd->snapshot.fontWeight = lf.lfWeight;
                            wd->snapshot.fontItalic = lf.lfItalic;
                            ApplyAndSave(wd);
                            // Preview height changed — rebuild controls at new positions
                            RebuildContentArea(GetParent(hWnd), wd);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            case RowAction::ResetDefaults:
            {
                CConfig::ResetAllDefaults(wd->currentMode);
                CConfig::WriteConfig(wd->currentMode);
                CConfig::LoadConfig(wd->currentMode);
                wd->snapshot = SettingsModel::LoadFromConfig();
                RebuildContentArea(GetParent(hWnd), wd);
                InvalidateRect(hWnd, nullptr, FALSE);
                UpdateWindow(hWnd);
                break;
            }
            case RowAction::LoadTable:
            {
                // Determine target .cin filename based on control id
                const wchar_t* targetSuffix = SettingsModel::GetLoadTableSuffix((SettingsControlId)btnId);
                if (targetSuffix) {
                    HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                    if (hDlgLib) {
                        auto pGetOpen = reinterpret_cast<_T_GetOpenFileName>(GetProcAddress(hDlgLib, "GetOpenFileNameW"));
                        if (pGetOpen) {
                            WCHAR pathToLoad[MAX_PATH] = {};
                            OPENFILENAMEW ofn = {};
                            ofn.lStructSize = sizeof(OPENFILENAMEW);
                            ofn.hwndOwner = GetParent(hWnd);
                            ofn.lpstrFile = pathToLoad;
                            ofn.nMaxFile = MAX_PATH;
                            ofn.Flags = OFN_FILEMUSTEXIST;
                            ofn.lpstrFilter = L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0";
                            if (pGetOpen(&ofn)) {
                                WCHAR wszAppData[MAX_PATH] = {};
                                Global::GetAppDataPath(wszAppData, TRUE);
                                WCHAR pathToWrite[MAX_PATH] = {};
                                StringCchPrintfW(pathToWrite, MAX_PATH, L"%s%s", wszAppData, targetSuffix);
                                if (CConfig::parseCINFile(pathToLoad, pathToWrite))
                                    ThemedMessageBox(GetParent(hWnd), L"自訂碼表載入完成。", L"DIME 自訂碼表", MB_ICONINFORMATION);
                                else
                                    ThemedMessageBox(GetParent(hWnd), L"自訂碼表載入發生錯誤 !!", L"DIME 自訂碼表", MB_ICONERROR);
                            }
                        }
                        FreeLibrary(hDlgLib);
                    }
                }
                break;
            }
            case RowAction::SaveCustomTable:
            {
                // Issue #130 Phase 3 (final): Save validates the full buffer
                // synchronously at click time using ValidateCustomTableBuffer
                // (in-memory, O(N), bounded). For 73K-line content this takes
                // ~500 ms — acceptable latency for an explicit Save action.
                // The BG scan (when one runs after Import) is purely visual;
                // Save runs its own authoritative pass here.
                HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                if (!hRE) break;

                DWORD dwLen = GetWindowTextLengthW(hRE);
                LPWSTR text = new (std::nothrow) WCHAR[dwLen + 1];
                if (!text) break;
                ZeroMemory(text, (dwLen + 1) * sizeof(WCHAR));
                GetWindowTextW(hRE, text, dwLen + 1);

                // Pre-persist validation. On error: message box with first N
                // line numbers, abort write. On clean: persist.
                const int MAX_REPORT = 10;
                int firstErrorLine = 0;
                wchar_t errSummary[1024] = {};
                int errCount = ValidateCustomTableBuffer(text, dwLen,
                    wd->currentMode, wd->snapshot.maxCodes, wd->pEngine,
                    &firstErrorLine, errSummary, _countof(errSummary), MAX_REPORT);
                if (errCount > 0) {
                    wchar_t msg[2048];
                    StringCchPrintfW(msg, _countof(msg),
                        L"儲存失敗：偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s%s按確定後將跳到第一個錯誤行。",
                        errCount, firstErrorLine, errSummary,
                        (errCount > MAX_REPORT) ? L"  ...(更多錯誤已省略)\n" : L"");
                    ThemedMessageBox(GetParent(hWnd), msg, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
                    delete[] text;

                    // Jump caret to the first error line so the user can find +
                    // fix it without counting line numbers manually. Order matters:
                    // SetFocus FIRST — the RichEdit subclass's WM_SETFOCUS handler
                    // resets the selection to (0,0). If we set the selection
                    // before SetFocus, the focus reset would clobber it. After
                    // focus is on the editor, set the caret + scroll, then paint
                    // the now-visible viewport so the error highlight shows up.
                    if (firstErrorLine > 0) {
                        LONG cp = (LONG)SendMessageW(hRE, EM_LINEINDEX,
                            (WPARAM)(firstErrorLine - 1), 0);
                        if (cp >= 0) {
                            SetFocus(hRE);
                            SendMessageW(hRE, EM_SETSEL, (WPARAM)cp, (LPARAM)cp);
                            SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
                            ValidateViewport(hRE, wd);
                        }
                    }
                    break;
                }

                // Persist — UTF-16+BOM .txt then parseCINFile(customTableMode=TRUE).
                WCHAR txtPath[MAX_PATH] = {};
                GetCustomTableTxtPath(wd->currentMode, txtPath, MAX_PATH);
                HANDLE hFile = CreateFileW(txtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WCHAR bom = 0xFEFF;
                    WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
                    WriteFile(hFile, text, dwLen * sizeof(WCHAR), &written, nullptr);
                    CloseHandle(hFile);
                    WCHAR cinPath[MAX_PATH] = {};
                    GetCustomTableCINPath(wd->currentMode, cinPath, MAX_PATH);
                    if (!CConfig::parseCINFile(txtPath, cinPath, TRUE))
                        ThemedMessageBox(GetParent(hWnd), L"自建詞庫載入發生錯誤！", L"DIME 自建詞庫", MB_ICONERROR);
                }
                delete[] text;
                EnableSaveButton(wd, FALSE);   // not dirty until next edit
                break;
            }
            case RowAction::ExportCustomTable:
            {
                // Validate before export using the in-memory path (issue #130:
                // ValidateCustomTableRE was O(N²) on the RichEdit gap buffer).
                {
                    HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                    if (!hRE) break;
                    DWORD dwLen = GetWindowTextLengthW(hRE);
                    LPWSTR vText = new (std::nothrow) WCHAR[dwLen + 1];
                    if (!vText) break;
                    ZeroMemory(vText, (dwLen + 1) * sizeof(WCHAR));
                    GetWindowTextW(hRE, vText, dwLen + 1);
                    int firstErrorLine = 0;
                    wchar_t errSummary[1024] = {};
                    int errCount = ValidateCustomTableBuffer(vText, dwLen,
                        wd->currentMode, wd->snapshot.maxCodes, wd->pEngine,
                        &firstErrorLine, errSummary, _countof(errSummary), 10);
                    delete[] vText;
                    if (errCount > 0) {
                        wchar_t msg[2048];
                        StringCchPrintfW(msg, _countof(msg),
                            L"匯出失敗：偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s請修正後再匯出。",
                            errCount, firstErrorLine, errSummary);
                        ThemedMessageBox(GetParent(hWnd), msg, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
                        break;
                    }
                }
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pGetSave = reinterpret_cast<_T_GetSaveFileName>(GetProcAddress(hDlgLib, "GetSaveFileNameW"));
                    if (pGetSave) {
                        WCHAR pathToWrite[MAX_PATH] = {};
                        WCHAR wszDoc[MAX_PATH] = {};
                        SHGetSpecialFolderPathW(nullptr, wszDoc, CSIDL_PERSONAL, FALSE);
                        StringCchPrintfW(pathToWrite, MAX_PATH, L"%s\\customTable.txt", wszDoc);
                        OPENFILENAMEW ofn = {};
                        ofn.lStructSize = sizeof(OPENFILENAMEW);
                        ofn.hwndOwner = GetParent(hWnd);
                        ofn.lpstrFile = pathToWrite;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT;
                        ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
                        if (pGetSave(&ofn)) {
                            // Write RichEdit content to selected file
                            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                            if (hRE) {
                                DWORD dwLen = GetWindowTextLengthW(hRE);
                                LPWSTR text = new (std::nothrow) WCHAR[dwLen + 1];
                                if (text) {
                                    ZeroMemory(text, (dwLen + 1) * sizeof(WCHAR));
                                    GetWindowTextW(hRE, text, dwLen + 1);
                                    HANDLE hFile = CreateFileW(pathToWrite, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                                    if (hFile != INVALID_HANDLE_VALUE) {
                                        DWORD written = 0;
                                        WCHAR bom = 0xFEFF;
                                        WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
                                        WriteFile(hFile, text, dwLen * sizeof(WCHAR), &written, nullptr);
                                        CloseHandle(hFile);
                                    }
                                    delete[] text;
                                }
                            }
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            case RowAction::ImportCustomTable:
            {
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pGetOpen = reinterpret_cast<_T_GetOpenFileName>(GetProcAddress(hDlgLib, "GetOpenFileNameW"));
                    if (pGetOpen) {
                        WCHAR pathToLoad[MAX_PATH] = {};
                        OPENFILENAMEW ofn = {};
                        ofn.lStructSize = sizeof(OPENFILENAMEW);
                        ofn.hwndOwner = GetParent(hWnd);
                        ofn.lpstrFile = pathToLoad;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_FILEMUSTEXIST;
                        ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
                        if (pGetOpen(&ofn)) {
                            // Read file and set into RichEdit. Issue #130:
                            // 1. Use the encoding-aware helper so ANSI/Big5/UTF-8
                            //    sources are properly decoded — not byte-aliased.
                            // 2. Pre-flight ValidateLine in memory before SetWindowTextW.
                            //    Aborts atomically with a message box on errors —
                            //    matches the CLI --import-custom contract.
                            // 3. Suppress ENM_CHANGE around SetWindowTextW so the
                            //    bulk load does not trigger the synchronous full-file
                            //    EN_CHANGE → ValidateCustomTableRE pass (which is
                            //    O(N²) over RichEdit lines on 70k+ rows and froze
                            //    the UI thread before this fix).
                            HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
                            if (hRE) {
                                size_t wlen = 0;
                                LPWSTR text = SettingsModel::LoadTextFileAsUtf16(pathToLoad, &wlen);
                                if (!text) {
                                    ThemedMessageBox(GetParent(hWnd),
                                        L"無法讀取或解碼檔案，請確認檔案編碼為 UTF-8 / UTF-16 / ANSI(Big5)。",
                                        L"DIME 自建詞庫", MB_ICONERROR);
                                    break;
                                }

                                // Issue #130 Phase 3 (final): scan for errors but DO NOT
                                // abort the import on them. Load the content unconditionally
                                // so the user can inspect / fix in the editor. Save's full
                                // pre-persist validation is the gate that prevents broken
                                // content from being persisted to disk.
                                const int MAX_REPORT = 10;
                                int firstErrorLine = 0;
                                wchar_t errSummary[1024] = {};
                                int errCount = ValidateCustomTableBuffer(text, wlen,
                                    wd->currentMode, wd->snapshot.maxCodes, wd->pEngine,
                                    &firstErrorLine, errSummary, _countof(errSummary), MAX_REPORT);

                                // Load content (with EN_CHANGE muted) — always.
                                LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
                                SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);
                                SetWindowTextW(hRE, text);
                                wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
                                wd->ctLastEditedLine = -1;
                                wd->ctKeystrokeCount = 0;
                                SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
                                delete[] text;

                                // Mark Save dirty (Import = new content vs disk) and paint
                                // the visible range. NO BG scan — ITextDocument is O(N)
                                // per call → unresponsive editor on 70k+ lines.
                                EnableSaveButton(wd, TRUE);
                                ValidateViewport(hRE, wd);

                                // If errors were found in pre-scan, surface a non-blocking
                                // warning (the load already succeeded) and auto-jump the
                                // caret to the first error so the user can see it directly.
                                if (errCount > 0 && firstErrorLine > 0) {
                                    wchar_t msg[2048];
                                    StringCchPrintfW(msg, _countof(msg),
                                        L"匯入完成，但偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s%s按確定後將跳到第一個錯誤行，請修正後再儲存。",
                                        errCount, firstErrorLine, errSummary,
                                        (errCount > MAX_REPORT) ? L"  ...(更多錯誤已省略)\n" : L"");
                                    ThemedMessageBox(GetParent(hWnd), msg,
                                        L"DIME 自建詞庫匯入", MB_ICONWARNING);
                                    LONG cp = (LONG)SendMessageW(hRE, EM_LINEINDEX,
                                        (WPARAM)(firstErrorLine - 1), 0);
                                    if (cp >= 0) {
                                        SetFocus(hRE);
                                        SendMessageW(hRE, EM_SETSEL, (WPARAM)cp, (LPARAM)cp);
                                        SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
                                        ValidateViewport(hRE, wd);
                                    }
                                }
                            }
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
                break;
            }
            default: break;
            }
        }

        if (notifyCode == EN_CHANGE) {
            int settingsId = ctrlId - 3000;
            if ((SettingsControlId)settingsId == CTRL_CUSTOM_TABLE_EDITOR) {
                // Issue #130 Phase 3 (final): live foreground viewport pass
                // via TOM (ITextRange). TOM-based painting does NOT touch the
                // selection, so no caret jumps or flicker even on every keystroke.
                // Cancels any in-flight BG scan (its snapshot is now stale).
                HWND hRE = (HWND)lParam;
                ValidateViewport(hRE, wd);
                CancelBgScan(hWnd, wd);
                EnableSaveButton(wd, TRUE);   // mark dirty
                wd->ctLastLineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
            }
            else if ((SettingsControlId)settingsId == CTRL_MAX_CODES) {
                WCHAR buf[16] = {};
                GetWindowTextW((HWND)lParam, buf, 16);
                UINT val;
                if (SettingsModel::ParseMaxCodes(buf, val)) {
                    wd->snapshot.maxCodes = val;
                    ApplyAndSave(wd);
                }
            }
        }

        if (notifyCode == CBN_SELCHANGE) {
            int settingsId = ctrlId - 3000;
            HWND hCombo = (HWND)lParam;
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                switch ((SettingsControlId)settingsId) {
                case CTRL_COLOR_MODE: {
                    // Uses item data, not direct index
                    int itemData = (int)SendMessage(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
                    wd->snapshot.colorMode = itemData;
                    break;
                }
                case CTRL_ARRAY_SCOPE: {
                    int itemData = (int)SendMessage(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
                    wd->snapshot.arrayScope = itemData;
                    break;
                }
                case CTRL_REVERSE_CONVERSION:  CConfig::SetReverseConversionSelection(sel); break;
                default:
                    SettingsModel::SyncComboToSnapshot(wd->snapshot, (SettingsControlId)settingsId, sel);
                    break;
                }
                if ((SettingsControlId)settingsId == CTRL_COLOR_MODE) {
                    // Auto-expand color grid when switching to custom
                    wd->colorGridExpanded = (wd->snapshot.colorMode == 3);
                    // Only save the color mode enum — don't overwrite palette colors
                    CConfig::SetColorMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                    CConfig::WriteConfig(wd->currentMode);
                    // Load the new mode's palette into snapshot for preview
                    CConfig::LoadColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                    const ColorInfo* clrs = CConfig::GetColors();
                    wd->snapshot.itemColor      = clrs[0].color;
                    wd->snapshot.selectedColor  = clrs[1].color;
                    wd->snapshot.itemBGColor    = clrs[2].color;
                    wd->snapshot.phraseColor    = clrs[3].color;
                    wd->snapshot.numberColor    = clrs[4].color;
                    wd->snapshot.selectedBGColor = clrs[5].color;
                    RebuildContentArea(GetParent(hWnd), wd);
                } else {
                    ApplyAndSave(wd);
                }
                InvalidateRect(hWnd, nullptr, FALSE);
                UpdateWindow(hWnd);
            }
        }

        // Color swatch click (STN_CLICKED or BN_CLICKED from swatch statics)
        if (notifyCode == STN_CLICKED || (notifyCode == BN_CLICKED && ctrlId >= 4000 && ctrlId < 4006)) {
            int swIdx = ctrlId - 4000;
            if (swIdx >= 0 && swIdx < 6) {
                static COLORREF custColors[16] = {};
                COLORREF* fields[] = {
                    &wd->snapshot.itemColor, &wd->snapshot.selectedColor, &wd->snapshot.itemBGColor,
                    &wd->snapshot.phraseColor, &wd->snapshot.numberColor, &wd->snapshot.selectedBGColor
                };
                HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
                if (hDlgLib) {
                    auto pCC = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSECOLORW)>(GetProcAddress(hDlgLib, "ChooseColorW"));
                    if (pCC) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = GetParent(hWnd);
                        cc.rgbResult = *fields[swIdx];
                        cc.lpCustColors = custColors;
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (pCC(&cc)) {
                            *fields[swIdx] = cc.rgbResult;
                            // Save custom colors to the current mode's palette
                            CConfig::LoadColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                            // Update colors[] with the new swatch value
                            const_cast<ColorInfo*>(CConfig::GetColors())[swIdx].color = cc.rgbResult;
                            CConfig::SaveColorsForMode((IME_COLOR_MODE)wd->snapshot.colorMode);
                            ApplyAndSave(wd);
                            // Invalidate the swatch control so it redraws with new color
                            InvalidateRect((HWND)lParam, nullptr, TRUE);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            UpdateWindow(hWnd);
                        }
                    }
                    FreeLibrary(hDlgLib);
                }
            }
        }
        break;
    }

    case WM_USER + 1:  // Deferred trigger from subclass after Enter/Space/Paste/large-delete
    {
        if (!wd) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (hRE) {
            ValidateViewport(hRE, wd);
            CancelBgScan(hWnd, wd);
            EnableSaveButton(wd, TRUE);
            wd->ctKeystrokeCount = 0;
        }
        return 0;
    }

    case WM_USER + 3:  // Forwarded from subclass on WM_VSCROLL / WM_MOUSEWHEEL
    {
        if (!wd) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (hRE) ValidateViewport(hRE, wd);   // newly-visible lines need painting
        return 0;
    }

    case WM_TIMER:
    {
        if (!wd || wParam != BG_SCAN_TIMER_ID) break;
        HWND hRE = FindControl(wd, CTRL_CUSTOM_TABLE_EDITOR);
        if (!hRE || !wd->ctBgBuffer) { CancelBgScan(hWnd, wd); return 0; }

        // Acquire ITextDocument once per tick — TOM-based painting doesn't
        // touch the selection, so no caret save/restore needed.
        ITextDocument* pDoc = GetRichEditTextDocument(hRE);
        if (!pDoc) { CancelBgScan(hWnd, wd); return 0; }

        // Compute viewport once per tick — foreground owns those lines, skip them.
        int firstVis, lastVis;
        GetViewportLineRange(hRE, &firstVis, &lastVis);

        int totalLines = wd->ctBgScanLineCountAtStart;
        int end = wd->ctBgScanCursor + BG_SCAN_CHUNK;
        if (end > totalLines) end = totalLines;

        bool autoJumped = false;

        for (int li = wd->ctBgScanCursor; li < end; ++li) {
            // Foreground owns the visible range; skip to avoid double work.
            if (li >= firstVis && li <= lastVis) continue;

            // In-memory read from snapshot — O(1), not O(N) like EM_GETTEXTRANGE.
            int start = wd->ctBgLineOffsets[li];
            int lineLen = (li + 1 < totalLines)
                ? wd->ctBgLineOffsets[li + 1] - start - 1   // exclude '\n'
                : wd->ctBgBufferLen - start;
            if (lineLen <= 0) continue;
            // Strip trailing '\r' if CRLF.
            if (wd->ctBgBuffer[start + lineLen - 1] == L'\r') --lineLen;
            if (lineLen <= 0) continue;

            auto v = SettingsModel::ValidateLine(&wd->ctBgBuffer[start], lineLen,
                wd->currentMode, wd->snapshot.maxCodes, wd->pEngine);
            if (!v.valid) {
                // Char offsets in our snapshot match the RichEdit's offsets.
                PaintErrorRange(pDoc, start, v, wd->isDarkTheme);
                if (wd->ctFirstBgErrorLine == -1) {
                    wd->ctFirstBgErrorLine = li + 1;  // 1-based for UI
                    // Auto-jump uses EM_SETSEL + EM_SCROLLCARET intentionally
                    // (this is the ONLY caret movement we want from a BG tick).
                    SendMessageW(hRE, EM_SETSEL, (WPARAM)start, (LPARAM)start);
                    SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
                    autoJumped = true;
                }
            }
        }
        wd->ctBgScanCursor = end;
        pDoc->Release();

        // After auto-jump, repaint the new viewport so the destination line +
        // neighbours are highlighted immediately (rather than waiting for the
        // BG to walk back to those lines, which it won't).
        if (autoJumped) {
            ValidateViewport(hRE, wd);
        }

        if (wd->ctBgScanCursor >= totalLines) {
            // Scan complete. Free snapshot. Save button state is independent
            // (dirty-bit driven), so don't touch it here. Errors found this
            // pass have already been painted + caret auto-jumped.
            KillTimer(hWnd, BG_SCAN_TIMER_ID);
            wd->ctBgScanTimer = 0;
            FreeBgSnapshot(wd);
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        if (!wd) break;
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pdis) break;

        // Owner-draw buttons (Win11 style: anti-aliased rounded corners via GDI+)
        if (pdis->CtlType == ODT_BUTTON) {
            UINT dpiBtn = CConfig::GetDpiForHwnd(GetParent(hWnd));
            bool pressed  = (pdis->itemState & ODS_SELECTED) != 0;
            bool disabled = (pdis->itemState & ODS_DISABLED) != 0;
            wchar_t txt[128] = {};
            GetWindowTextW(pdis->hwndItem, txt, 128);
            SettingsWindowPaint::PaintOwnerButton(pdis->hDC, pdis->rcItem, txt, pressed, disabled,
                wd->theme, wd->hFontBody, dpiBtn);
            return TRUE;
        }

        // Color swatches
        if (pdis->CtlType == ODT_STATIC && pdis->CtlID >= 4000 && pdis->CtlID < 4006) {
            int idx = pdis->CtlID - 4000;
            COLORREF colors[] = {
                wd->snapshot.itemColor, wd->snapshot.selectedColor, wd->snapshot.itemBGColor,
                wd->snapshot.phraseColor, wd->snapshot.numberColor, wd->snapshot.selectedBGColor
            };
            int r = ScaleForDpi(g_geo.swatchRadius, CConfig::GetDpiForHwnd(GetParent(hWnd)));
            SettingsWindowPaint::PaintSwatch(pdis->hDC, pdis->rcItem, colors[idx], wd->theme.cardBorder, r);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        if (!wd || !wd->isDarkTheme) break;
        HDC hdcCtrl = (HDC)wParam;
        SetTextColor(hdcCtrl, wd->theme.textPrimary);
        SetBkColor(hdcCtrl, wd->theme.cardBg);
        if (!wd->hBrushCardBg) wd->hBrushCardBg = CreateSolidBrush(wd->theme.cardBg);
        return (LRESULT)wd->hBrushCardBg;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
