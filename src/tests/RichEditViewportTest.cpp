// RichEditViewportTest.cpp — Layer 3: in-process RichEdit GUI tests (IT_RE_*)
//
// Verifies EM_EXLIMITTEXT, TOM caret stability, ValidateViewport-style painting,
// auto-jump, and EN_CHANGE-mute correctness. All tests host a bare RICHEDIT50W
// in-process — no Settings UI needed.
//
// TOM helpers are defined locally here using the same ITextDocument / ITextRange
// API as the production code in SettingsWindow.cpp, so these tests verify the
// RichEdit + TOM contract rather than the production wrapper's identity.

#include "pch.h"
#include <richedit.h>
#include <tom.h>
#include "../DIMESettings/SettingsController.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEIntegratedTests
{

TEST_CLASS(RichEditViewportTest)
{
    static HWND    s_hRE;
    static HMODULE s_hMsftedit;

    TEST_CLASS_INITIALIZE(SetupClass)
    {
        s_hMsftedit = LoadLibraryW(L"Msftedit.dll");
        s_hRE = CreateWindowExW(0, L"RICHEDIT50W", L"",
            WS_POPUP | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 800, 600, GetDesktopWindow(), nullptr, nullptr, nullptr);
        Assert::IsNotNull(s_hRE, L"RICHEDIT50W must be created (Msftedit.dll not loaded?)");
        SendMessageW(s_hRE, EM_EXLIMITTEXT, 0, (LPARAM)(100LL * 1024 * 1024));
    }

    TEST_CLASS_CLEANUP(TeardownClass)
    {
        if (s_hRE)       { DestroyWindow(s_hRE);      s_hRE       = nullptr; }
        if (s_hMsftedit) { FreeLibrary(s_hMsftedit);  s_hMsftedit = nullptr; }
    }

    // Set window text with EN_CHANGE muted so tests start from a clean state.
    static void SetTextQ(HWND hRE, const wchar_t* text)
    {
        LRESULT prev = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
        SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);
        SetWindowTextW(hRE, text);
        SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prev);
    }

    // Acquire ITextDocument (caller must Release).
    static ITextDocument* GetTOM(HWND hRE)
    {
        IUnknown* pUnk = nullptr;
        SendMessageW(hRE, EM_GETOLEINTERFACE, 0, (LPARAM)&pUnk);
        if (!pUnk) return nullptr;
        ITextDocument* pDoc = nullptr;
        pUnk->QueryInterface(__uuidof(ITextDocument), (void**)&pDoc);
        pUnk->Release();
        return pDoc;
    }

    // Paint foreground colour over [cpMin, cpMax) WITHOUT moving the selection
    // (same TOM logic as SettingsWindow.cpp PaintRangeColor).
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

    // Read back the foreground colour of the character at offset cp.
    static LONG GetCharColor(ITextDocument* pDoc, LONG cp)
    {
        ITextRange* pRange = nullptr;
        if (FAILED(pDoc->Range(cp, cp + 1, &pRange)) || !pRange) return -1;
        ITextFont* pFont  = nullptr;
        LONG       clr    = -1;
        if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont) {
            pFont->GetForeColor(&clr);
            pFont->Release();
        }
        pRange->Release();
        return clr;
    }

public:
    // IT_RE_01: after EM_EXLIMITTEXT(100 MB), EM_REPLACESEL succeeds on a large buffer
    TEST_METHOD(IT_RE_01_LargeImportAcceptsInput)
    {
        // Build ~1.5 MB content: 200,000 lines × "a 中\n"
        std::wstring big;
        big.reserve(200000 * 4);
        for (int i = 0; i < 200000; ++i) big += L"a 中\n";

        SetTextQ(s_hRE, big.c_str());
        int lenBefore = GetWindowTextLengthW(s_hRE);
        Assert::IsTrue(lenBefore > 0, L"Buffer must have content");

        SendMessageW(s_hRE, EM_SETSEL, (WPARAM)lenBefore, (LPARAM)lenBefore);
        SendMessageW(s_hRE, EM_REPLACESEL, FALSE, (LPARAM)L"X");

        int lenAfter = GetWindowTextLengthW(s_hRE);
        Assert::IsTrue(lenAfter > lenBefore,
            L"EM_EXLIMITTEXT(100 MB) raised — EM_REPLACESEL must succeed on a 1.5 MB buffer");
    }

    // IT_RE_02: EM_EXLIMITTEXT raises the text limit above the default;
    // without the raise a 1.5 MB import silently blocks further user input
    // because the default cap (~64 KB) is far below the imported content size
    // (issue #130 fix #5).
    TEST_METHOD(IT_RE_02_ExLimitText_RaisesAboveDefault)
    {
        HWND hRaw = CreateWindowExW(0, L"RICHEDIT50W", L"",
            WS_POPUP | ES_MULTILINE,
            0, 0, 400, 300, GetDesktopWindow(), nullptr, nullptr, nullptr);
        Assert::IsNotNull(hRaw, L"second RICHEDIT50W must be created");

        // Read the default limit BEFORE any explicit raise.
        LRESULT defaultLimit = SendMessageW(hRaw, EM_GETLIMITTEXT, 0, 0);

        // Now raise the limit exactly as the production code does.
        const LRESULT TARGET = 100LL * 1024 * 1024;  // 100 MB in chars
        SendMessageW(hRaw, EM_EXLIMITTEXT, 0, (LPARAM)TARGET);
        LRESULT raisedLimit = SendMessageW(hRaw, EM_GETLIMITTEXT, 0, 0);
        DestroyWindow(hRaw);

        // After the raise the limit must reach 100 MB.
        Assert::IsTrue(raisedLimit >= TARGET,
            L"After EM_EXLIMITTEXT(100 MB), EM_GETLIMITTEXT must return at least 100 MB");

        // The default must be below 100 MB, confirming the raise is necessary
        // (the 73 K-line import in issue #130 was ~700 K wchars, already above 64 KB).
        Assert::IsTrue(defaultLimit < TARGET,
            L"Default RichEdit limit must be below 100 MB — EM_EXLIMITTEXT raise is necessary");
    }

    // IT_RE_03: TOM PaintRangeColor does not move the caret/selection
    TEST_METHOD(IT_RE_03_TOMPaintDoesNotMoveSelection)
    {
        SetTextQ(s_hRE, L"abc\r\ndefgh\r\nijk");

        // Place caret at offset 5 (inside "defgh")
        SendMessageW(s_hRE, EM_SETSEL, 5, 5);

        ITextDocument* pDoc = GetTOM(s_hRE);
        Assert::IsNotNull(pDoc, L"TOM must be available on RICHEDIT50W");

        // Paint chars 0..100 red — must not disturb the (5,5) caret
        PaintRangeColor(pDoc, 0, 100, RGB(255, 0, 0));
        pDoc->Release();

        CHARRANGE sel = {-1, -1};
        SendMessageW(s_hRE, EM_EXGETSEL, 0, (LPARAM)&sel);
        Assert::AreEqual(5L, sel.cpMin, L"cpMin must remain 5 after TOM paint");
        Assert::AreEqual(5L, sel.cpMax, L"cpMax must remain 5 after TOM paint");
    }

    // IT_RE_04: ValidateViewport-style paint marks the invalid line in error colour
    TEST_METHOD(IT_RE_04_ValidateViewport_PaintsErrors)
    {
        // 4 valid lines + 1 invalid line (3 tokens → Format error)
        SetTextQ(s_hRE, L"a 中\r\nb 語\r\nc 台\r\nd 灣\r\nfoo bar baz");

        const COLORREF VALID_CLR = RGB(0, 180, 0);  // green
        const COLORREF ERROR_CLR = RGB(200, 0, 0);  // red

        ITextDocument* pDoc = GetTOM(s_hRE);
        Assert::IsNotNull(pDoc, L"TOM must be available");

        int lineCount = (int)SendMessageW(s_hRE, EM_GETLINECOUNT, 0, 0);
        for (int li = 0; li < lineCount; ++li) {
            LONG start = (LONG)SendMessageW(s_hRE, EM_LINEINDEX, (WPARAM)li, 0);
            LONG len   = (LONG)SendMessageW(s_hRE, EM_LINELENGTH, (WPARAM)start, 0);
            if (len <= 0) continue;

            std::vector<WCHAR> buf((size_t)len + 1, 0);
            TEXTRANGEW tr;
            tr.chrg.cpMin = start;
            tr.chrg.cpMax = start + len;
            tr.lpstrText  = buf.data();
            SendMessageW(s_hRE, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

            auto v = SettingsModel::ValidateLine(buf.data(), (int)len,
                IME_MODE::IME_MODE_DAYI, 4);
            PaintRangeColor(pDoc, start, start + len,
                v.valid ? VALID_CLR : ERROR_CLR);
        }

        // Read back colours via TOM
        LONG clrLine0 = GetCharColor(pDoc, (LONG)SendMessageW(s_hRE, EM_LINEINDEX, 0, 0));
        LONG clrLine4 = GetCharColor(pDoc, (LONG)SendMessageW(s_hRE, EM_LINEINDEX, 4, 0));
        pDoc->Release();

        Assert::AreEqual((LONG)VALID_CLR, clrLine0, L"Line 0 (valid) must have VALID_CLR");
        Assert::AreEqual((LONG)ERROR_CLR, clrLine4, L"Line 4 (invalid 'foo bar baz') must have ERROR_CLR");
    }

    // IT_RE_05: auto-jump via EM_LINEINDEX + EM_SETSEL + EM_SCROLLCARET
    TEST_METHOD(IT_RE_05_AutoJumpAfterSaveError)
    {
        // 60 lines; simulate Save-handler auto-jump to error at line 50 (1-based)
        std::wstring content;
        content.reserve(60 * 6);
        for (int i = 0; i < 60; ++i) content += L"a 中\r\n";
        SetTextQ(s_hRE, content.c_str());

        const int errorLine1 = 50;  // 1-based (0-based index: 49)
        LRESULT cp = SendMessageW(s_hRE, EM_LINEINDEX, (WPARAM)(errorLine1 - 1), 0);
        Assert::IsTrue(cp >= 0, L"EM_LINEINDEX must return a valid character offset");

        SendMessageW(s_hRE, EM_SETSEL, (WPARAM)cp, (LPARAM)cp);
        SendMessageW(s_hRE, EM_SCROLLCARET, 0, 0);

        CHARRANGE sel = {};
        SendMessageW(s_hRE, EM_EXGETSEL, 0, (LPARAM)&sel);
        Assert::AreEqual((LONG)cp, sel.cpMin, L"Caret must land at start of error line after auto-jump");
        Assert::AreEqual((LONG)cp, sel.cpMax, L"Selection must be empty (insertion point only)");
    }

    // IT_RE_06: EM_SETEVENTMASK(0) during TOM paint brackets prevents EN_CHANGE
    // recursion; the mask must be fully restored afterward.
    TEST_METHOD(IT_RE_06_ENChangeMuteSurvivesPaint)
    {
        HWND hTest = CreateWindowExW(0, L"RICHEDIT50W", L"",
            WS_POPUP | ES_MULTILINE,
            0, 0, 400, 300, GetDesktopWindow(), nullptr, nullptr, nullptr);
        Assert::IsNotNull(hTest, L"test RichEdit must be created");
        SendMessageW(hTest, EM_EXLIMITTEXT, 0, (LPARAM)(10 * 1024 * 1024));
        SendMessageW(hTest, EM_SETEVENTMASK, 0, (LPARAM)ENM_CHANGE);
        SetWindowTextW(hTest, L"a 中\r\nb 語\r\nfoo bar baz");

        // Verify the starting mask
        LRESULT prevMask = SendMessageW(hTest, EM_GETEVENTMASK, 0, 0);
        Assert::AreEqual((LRESULT)ENM_CHANGE, prevMask,
            L"Event mask must be ENM_CHANGE before the paint bracket");

        // Production pattern: mute → TOM paint → restore
        SendMessageW(hTest, EM_SETEVENTMASK, 0, 0);

        ITextDocument* pDoc = GetTOM(hTest);
        if (pDoc) {
            PaintRangeColor(pDoc, 0, 30, RGB(0, 128, 0));
            pDoc->Release();
        }

        SendMessageW(hTest, EM_SETEVENTMASK, 0, (LPARAM)prevMask);

        LRESULT restoredMask = SendMessageW(hTest, EM_GETEVENTMASK, 0, 0);
        DestroyWindow(hTest);

        Assert::AreEqual((LRESULT)ENM_CHANGE, restoredMask,
            L"EM_SETEVENTMASK must be restored to ENM_CHANGE after the TOM paint bracket");
    }
};

HWND    RichEditViewportTest::s_hRE       = nullptr;
HMODULE RichEditViewportTest::s_hMsftedit = nullptr;

} // namespace DIMEIntegratedTests
