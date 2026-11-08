/********************************************************************************
* CandidatePaintParityTest: the candidate window drawing, C++
* (CCandidateWindow::_OnPaintCpp) vs Rust (_OnPaintRust, dime-ui::candpaint).
* Both paint a real candidate window into a 32-bpp DIB section; the pixels, the
* layout members painting updates (_cxTitle, _cyRow, _padding, _wndWidth), the
* window size (_ResizeWindow) and the scroll bar's selection highlight must be
* identical. Scenarios: several fonts and sizes, candidate widths, page sizes,
* item lengths (truncation and widening), selections on every page, colors,
* missing items. The scroll bar likewise (ScrollBarPaintMatchesCpp).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../CandidateWindow.h"
#include "../BaseStructure.h"
#include "../Config.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE

namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }


    struct PaintResult
    {
        std::vector<DWORD> pixels;
        CCandidateWindow::LayoutStateForTest state = {};
        RECT window = {};
        RECT highlight = {};
        int highlightRadius = 0;
        COLORREF highlightColor = 0;
    };

    // Paint like WM_PAINT does (off-screen, rcPaint = client), with either implementation.
    PaintResult Paint(CCandidateWindow* w, bool rust, const CCandidateWindow::LayoutStateForTest& start)
    {
        // Same starting point for both: layout members, window size, scroll bar highlight.
        w->_SetLayoutStateForTest(start);
        w->_ResizeWindow();
        CScrollBarWindow* sb = w->_GetScrollBarForTest();
        if (sb)
        {
            RECT sentinel = { -7, -7, 7, 7 };
            sb->_SetSelectionHighlight(sentinel, 77, RGB(1, 2, 3));
        }

        RECT rc = { 0, 0, 0, 0 };
        GetClientRect(w->_GetWnd(), &rc);
        int cx = max(1, (int)(rc.right - rc.left)), cy = max(1, (int)(rc.bottom - rc.top));

        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = -cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(mem, dib);
        RECT all = { 0, 0, cx, cy };
        HBRUSH sentinelBrush = CreateSolidBrush(RGB(0x12, 0x34, 0x56));
        FillRect(mem, &all, sentinelBrush);
        DeleteObject(sentinelBrush);

        PAINTSTRUCT ps = {};
        ps.hdc = mem;
        ps.rcPaint = rc;
        if (rust) w->_OnPaintRust(mem, &ps);
        else w->_OnPaintCpp(mem, &ps);
        GdiFlush();

        PaintResult r;
        r.pixels.assign((DWORD*)bits, (DWORD*)bits + (size_t)cx * cy);
        r.state = w->_GetLayoutStateForTest();
        GetWindowRect(w->_GetWnd(), &r.window);
        if (sb) sb->_GetSelectionHighlightForTest(&r.highlight, &r.highlightRadius, &r.highlightColor);

        SelectObject(mem, oldBmp);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return r;
    }

    std::wstring Describe(const CCandidateWindow::LayoutStateForTest& s)
    {
        return L"cxTitle=" + std::to_wstring(s.cxTitle) + L" cyRow=" + std::to_wstring(s.cyRow) +
            L" padding=" + std::to_wstring(s.padding) + L" wndWidth=" + std::to_wstring(s.wndWidth);
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(CandidatePaintParityTest)
    {
    public:
        TEST_METHOD(PaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;

            const wchar_t* faces[] = { L"Microsoft JhengHei", L"PMingLiU", L"Segoe UI" };
            const int heights[] = { -16, -24, -37 };
            // A pool of candidate strings (CJK, ASCII, long ones that get truncated / widen the window).
            const std::wstring pool[] = {
                L"中", L"國", L"美麗", L"台灣大學", L"a", L"Wi-Fi", L"…", L"ㄅㄆㄇ",
                L"一二三四五六七八九十甲乙丙", L"一二三四五六七八九十甲乙丙丁戊己庚辛",
                L"abcdefghijklmnopqrstuvwxyz", L"長長長長長長長長長長長長長長長長長長長長長長長",
                L"x　y", L"\U00020000\U00020001", L"ab", L"龜鱉",
            };

            std::mt19937 rng(31);
            int scenarios = 0, compared = 0, withIndicator = 0, truncatedOrWidened = 0;
            for (const wchar_t* face : faces)
            for (int height : heights)
            {
                HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
                Assert::IsNotNull(font);
                Global::defaultlFontHandle = font;

                for (int s = 0; s < 12; s++)
                {
                    int rowsInPage = 1 + (int)(rng() % 9);
                    UINT wndWidth = 1 + rng() % 6;
                    UINT count = rng() % 30;

                    CCandidateRange range;
                    const wchar_t keys[] = L"1234567890";
                    for (int k = 0; k < rowsInPage; k++)
                        range.Append()->CandIndex = keys[k % 10];

                    CCandidateWindow* w = new CCandidateWindow(NoCallback, nullptr, &range, FALSE);
                    if (!w->_Create(wndWidth, 12, nullptr))
                    {
                        delete w;
                        Logger::WriteMessage("SKIP: window creation failed (no GUI context)");
                        Global::defaultlFontHandle = savedFont;
                        DeleteObject(font);
                        return;
                    }
                    Assert::IsNotNull(w->_GetWnd(), L"a real window (not a virtual one)");
                    w->_SetCandStringLength(wndWidth);

                    std::vector<CCandidateListItem> items(count);
                    for (UINT i = 0; i < count; i++)
                    {
                        const std::wstring& t = pool[rng() % ARRAYSIZE(pool)];
                        items[i]._ItemString.Set(t.c_str(), t.size());
                        w->_AddString(&items[i], FALSE);
                    }
                    UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
                    std::vector<UINT> pageIndex(pages);
                    for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
                    if (pages) w->_SetPageIndex(pageIndex.data(), pages);
                    w->_SetScrollInfo(count, rowsInPage);

                    auto color = [&]() { return (COLORREF)(rng() & 0xFFFFFF); };
                    w->_SetNumberColor(color(), color());
                    w->_SetTextColor(color(), color());
                    w->_SetSelectedTextColor(color(), color());
                    w->_SetFillColor(color());

                    // A few selections per window: first item, random ones (any page), none.
                    for (int sel = 0; sel < 4; sel++)
                    {
                        if (count)
                            w->_SetSelection(sel == 0 ? 0 : (int)(rng() % count), FALSE);
                        CCandidateWindow::LayoutStateForTest start = { (int)(rng() % 300), 20, 10, wndWidth };

                        PaintResult c = Paint(w, false, start);
                        PaintResult r = Paint(w, true, start);

                        std::wstring where = std::wstring(face) + L" " + std::to_wstring(height) + L" rows=" + std::to_wstring(rowsInPage) +
                            L" count=" + std::to_wstring(count) + L" width=" + std::to_wstring(wndWidth) + L" sel=" + std::to_wstring(w->_GetSelection());
                        Assert::IsTrue(memcmp(&c.state, &r.state, sizeof(c.state)) == 0,
                            (where + L": state C++ " + Describe(c.state) + L" / Rust " + Describe(r.state)).c_str());
                        Assert::IsTrue(EqualRect(&c.window, &r.window) != FALSE, (where + L": window size").c_str());
                        Assert::IsTrue(EqualRect(&c.highlight, &r.highlight) && c.highlightRadius == r.highlightRadius &&
                            c.highlightColor == r.highlightColor, (where + L": scroll bar highlight").c_str());
                        Assert::AreEqual(c.pixels.size(), r.pixels.size(), (where + L": bitmap size").c_str());
                        for (size_t i = 0; i < c.pixels.size(); i++)
                        {
                            if (c.pixels[i] != r.pixels[i])
                                Assert::Fail((where + L": pixel " + std::to_wstring(i) + L" differs").c_str());
                        }
                        if (pages > 1) withIndicator++;
                        if (c.state.wndWidth != wndWidth) truncatedOrWidened++;
                        compared++;
                    }
                    delete w;
                    scenarios++;
                }
                Global::defaultlFontHandle = savedFont;
                DeleteObject(font);
            }
            Logger::WriteMessage((std::to_wstring(scenarios) + L" windows, " + std::to_wstring(compared) + L" paints compared, " +
                std::to_wstring(withIndicator) + L" with a page indicator, " + std::to_wstring(truncatedOrWidened) + L" widened").c_str());
            Assert::IsTrue(withIndicator > 0 && truncatedOrWidened > 0, L"coverage");
        }

        // The scroll bar (CScrollBarWindow::_OnPaintCpp vs _OnPaintRust): background (color key /
        // the candidate's brush), hovered arrows + pill thumb or the idle line, the RC26 highlight;
        // light / dark, layered / not, scroll ranges including "everything fits".
        TEST_METHOD(ScrollBarPaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;

            std::mt19937 rng(37);
            int compared = 0, layeredPaints = 0, plainPaints = 0;
            for (int s = 0; s < 20; s++)
            {
                int rowsInPage = 1 + (int)(rng() % 9);
                CCandidateRange range;
                for (int k = 0; k < rowsInPage; k++) range.Append()->CandIndex = L'1' + (wchar_t)k;
                CCandidateWindow* w = new CCandidateWindow(NoCallback, nullptr, &range, FALSE);
                Assert::IsTrue(w->_Create(1 + rng() % 4, 12, nullptr) != FALSE);
                Assert::IsNotNull(w->_GetWnd(), L"a real window (not a virtual one)");
                w->_SetFillColor((COLORREF)(rng() & 0xFFFFFF));
                w->_ResizeWindow();
                CScrollBarWindow* sb = w->_GetScrollBarForTest();
                Assert::IsNotNull(sb);
                HWND sbh = sb->_GetWnd();
                LONG style = GetWindowLong(sbh, GWL_EXSTYLE);

                for (int v = 0; v < 16; v++)
                {
                    CScrollInfo si;
                    si.nMax = (int)(rng() % 60);
                    si.nPage = 1 + (int)(rng() % 12);
                    si.nPos = (int)(rng() % 70);
                    sb->_SetScrollInfo(&si);
                    sb->_SetHoveredForTest(rng() % 2);
                    CConfig::SetColorMode((rng() % 2) ? IME_COLOR_MODE::IME_COLOR_MODE_DARK : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
                    RECT hl = { -(LONG)(rng() % 200), (LONG)(rng() % 100), (LONG)(rng() % 40), 0 };
                    hl.bottom = hl.top + 10 + (LONG)(rng() % 30);
                    sb->_SetSelectionHighlight(hl, (rng() % 3) ? (int)(rng() % 9) : -1, (COLORREF)(rng() & 0xFFFFFF));
                    SetWindowLong(sbh, GWL_EXSTYLE, (rng() % 2) ? (style | WS_EX_LAYERED) : (style & ~WS_EX_LAYERED));
                    bool layered = (GetWindowLong(sbh, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
                    (layered ? layeredPaints : plainPaints)++;
                    bool withPaintStruct = (v != 0);

                    RECT rc = { 0, 0, 0, 0 };
                    GetClientRect(sbh, &rc);
                    int cx = max(1, (int)rc.right), cy = max(1, (int)rc.bottom);
                    std::vector<DWORD> pixels[2];
                    for (int impl = 0; impl < 2; impl++)
                    {
                        HDC screen = GetDC(nullptr);
                        HDC mem = CreateCompatibleDC(screen);
                        BITMAPINFO bi = {};
                        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
                        bi.bmiHeader.biWidth = cx;
                        bi.bmiHeader.biHeight = -cy;
                        bi.bmiHeader.biPlanes = 1;
                        bi.bmiHeader.biBitCount = 32;
                        bi.bmiHeader.biCompression = BI_RGB;
                        void* bits = nullptr;
                        HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                        HGDIOBJ oldBmp = SelectObject(mem, dib);
                        RECT all = { 0, 0, cx, cy };
                        HBRUSH sentinel = CreateSolidBrush(RGB(0x12, 0x34, 0x56));
                        FillRect(mem, &all, sentinel);
                        DeleteObject(sentinel);
                        PAINTSTRUCT ps = {};
                        ps.hdc = mem;
                        ps.rcPaint = rc;
                        PAINTSTRUCT* pps = withPaintStruct ? &ps : nullptr;
                        if (impl == 0) sb->_OnPaintCpp(mem, pps);
                        else sb->_OnPaintRust(mem, pps);
                        GdiFlush();
                        pixels[impl].assign((DWORD*)bits, (DWORD*)bits + (size_t)cx * cy);
                        SelectObject(mem, oldBmp);
                        DeleteObject(dib);
                        DeleteDC(mem);
                        ReleaseDC(nullptr, screen);
                    }
                    for (size_t i = 0; i < pixels[0].size(); i++)
                    {
                        if (pixels[0][i] != pixels[1][i])
                            Assert::Fail((L"scroll bar " + std::to_wstring(s) + L"/" + std::to_wstring(v) + L" max=" + std::to_wstring(si.nMax) +
                                L" page=" + std::to_wstring(si.nPage) + L" layered=" + std::to_wstring(layered) + L": pixel " + std::to_wstring(i) + L" differs").c_str());
                    }
                    compared++;
                }
                SetWindowLong(sbh, GWL_EXSTYLE, style);
                delete w;
            }
            CConfig::SetColorMode(savedMode);
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(compared) + L" scroll bar paints compared (" + std::to_wstring(layeredPaints) +
                L" layered, " + std::to_wstring(plainPaints) + L" not)").c_str());
            Assert::IsTrue(layeredPaints > 0 && plainPaints > 0, L"coverage");
        }
    };
}
#endif
