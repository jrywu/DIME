// SettingsEditor.cpp — see SettingsEditor.h. The Cpp:: code is the editor
// logic that used to live in SettingsWindow.cpp; dime-settings (editor.rs)
// mirrors it.

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <tom.h>
#include <vector>
#include "..\Define.h"
#include "..\BaseStructure.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsEditor.h"
#include "SettingsController.h"
#if defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif
#pragma comment(lib, "comctl32.lib")

namespace SettingsEditor {

namespace Cpp {

namespace {

// Acquire ITextDocument from the RichEdit (caller releases); null when the
// control has no TOM support, in which case live highlighting is skipped.
ITextDocument* GetRichEditTextDocument(HWND hRE)
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
void PaintRangeColor(ITextDocument* pDoc, LONG cpMin, LONG cpMax, COLORREF color)
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

// Paint per-error colours over the line that starts at lineStart.
void PaintErrorRange(ITextDocument* pDoc, LONG lineStart,
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

// Validate one line: read it with EM_GETTEXTRANGE, paint the valid colour
// (clearing stale error colours), then the per-error colours — all via TOM.
void ValidateOneLine(HWND hRE, ITextDocument* pDoc, int li, const EditorContext& ctx)
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

    auto v = SettingsModel::ValidateLine(buf.data(), (int)len, (IME_MODE)ctx.mode, ctx.maxCodes, ctx.engine);

    bool isDark = ctx.isDark != 0;
    COLORREF validColor = isDark ? CUSTOM_TABLE_DARK_VALID : CUSTOM_TABLE_LIGHT_VALID;
    PaintRangeColor(pDoc, start, start + len, validColor);
    if (!v.valid) PaintErrorRange(pDoc, start, v, isDark);
}

} // namespace

void ViewportLineRange(HWND hRE, int* outFirstVis, int* outLastVis)
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

// Mutes EN_CHANGE around the painting: RichEdit fires it on some
// CHARFORMAT-only changes, which would recurse through the EN_CHANGE handler.
void ValidateViewport(HWND hRE, const EditorContext& ctx)
{
    if (!hRE) return;
    int firstVis, lastVis;
    ViewportLineRange(hRE, &firstVis, &lastVis);
    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return;

    ITextDocument* pDoc = GetRichEditTextDocument(hRE);
    if (!pDoc) return;

    LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);

    for (int li = firstVis; li <= lastVis && li < lineCount; ++li)
        ValidateOneLine(hRE, pDoc, li, ctx);

    SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
    pDoc->Release();
}

// - Keeps the caret at the start on focus (no select-all).
// - Posts kMsgRevalidate to the parent on Enter / Space / paste / large delete.
// - Wheel outside the editor scrolls the page; inside it scrolls the editor and
//   posts kMsgViewport so the newly visible lines get painted.
LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    if (uMsg == WM_SETFOCUS) {
        CHARRANGE cr = {0, 0};
        SendMessageW(hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hWnd, EM_SCROLLCARET, 0, 0);
    } else if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            PostMessageW(GetParent(hWnd), kMsgRevalidate, 0, 0);
        } else if (wParam == VK_DELETE || wParam == VK_BACK) {
            CHARRANGE sel;
            SendMessageW(hWnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMax - sel.cpMin > CUSTOM_TABLE_LARGE_DELETION_THRESHOLD)
                PostMessageW(GetParent(hWnd), kMsgRevalidate, 0, 0);
        }
    } else if (uMsg == WM_PASTE) {
        PostMessageW(GetParent(hWnd), kMsgRevalidate, 0, 0);
    } else if (uMsg == WM_MOUSEWHEEL) {
        POINT pt;
        GetCursorPos(&pt);
        RECT rcRE;
        GetWindowRect(hWnd, &rcRE);
        if (!PtInRect(&rcRE, pt))
            return SendMessageW(GetParent(hWnd), uMsg, wParam, lParam);
        PostMessageW(GetParent(hWnd), kMsgViewport, 0, 0);
    } else if (uMsg == WM_VSCROLL) {
        PostMessageW(GetParent(hWnd), kMsgViewport, 0, 0);
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace Cpp

#if defined(DIME_USE_RUST_CONFIG)

namespace {
extern "C" int32_t SettingsEditorCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr) return 0;
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}

void ViewportLineRange(HWND hRE, int* firstVis, int* lastVis)
{
    int f = 0, l = 0;
    dime_settings_editor_viewport(hRE, &f, &l);
    if (firstVis) *firstVis = f;
    if (lastVis) *lastVis = l;
}

void ValidateViewport(HWND hRE, const EditorContext& ctx)
{
    dime_settings_editor_validate_viewport(hRE, ctx.mode, ctx.maxCodes, ctx.isDark,
        ctx.engine ? &SettingsEditorCharOk : nullptr, ctx.engine);
}

SUBCLASSPROC SubclassProc()
{
    return reinterpret_cast<SUBCLASSPROC>(dime_settings_editor_subclass_proc());
}

#else

void ViewportLineRange(HWND hRE, int* firstVis, int* lastVis) { Cpp::ViewportLineRange(hRE, firstVis, lastVis); }
void ValidateViewport(HWND hRE, const EditorContext& ctx) { Cpp::ValidateViewport(hRE, ctx); }
SUBCLASSPROC SubclassProc() { return &Cpp::SubclassProc; }

#endif

} // namespace SettingsEditor
