// SettingsWindowPaint.cpp — painting of the settings window. See
// SettingsWindowPaint.h. The Cpp:: functions are the code that used to live
// in SettingsWindow.cpp; dime-settings (paint.rs) mirrors them call by call.

#include <windows.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <math.h>
#include "..\BaseStructure.h"
#include "SettingsWindowPaint.h"
#include "SettingsPageLayout.h"
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#include <cstddef>
#endif
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

namespace SettingsWindowPaint {

namespace Cpp {

static HFONT MakeFont(int ptSize, int dpi, int weight)
{
    return CreateFontW(-MulDiv(ptSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft JhengHei UI");
}

void CreateFonts(UINT dpiU, Fonts* out)
{
    int dpi = (int)dpiU;
    out->title      = MakeFont(g_geo.fontTitlePt, dpi, FW_SEMIBOLD);
    out->cardHeader = MakeFont(g_geo.fontCardHeaderPt, dpi, FW_NORMAL);
    out->body       = MakeFont(g_geo.fontBodyPt, dpi, FW_NORMAL);
    out->desc       = MakeFont(g_geo.fontDescPt, dpi, FW_NORMAL);
    // Segoe MDL2 Assets — small for chevrons
    out->mdl2 = CreateFontW(-MulDiv(g_geo.fontBodyPt - 2, dpi, 72), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // Segoe MDL2 Assets — large for row icons (sized to rowIconSize)
    out->mdl2Icon = CreateFontW(-ScaleForDpi(g_geo.rowIconSize, dpiU), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // Check if MDL2 is actually available (Win10+). If not, fallback to text chevrons.
    out->hasMDL2 = 0;
    if (out->mdl2) {
        HDC hdcChk = GetDC(nullptr);
        HFONT hOld = (HFONT)SelectObject(hdcChk, out->mdl2);
        WCHAR faceName[LF_FACESIZE] = {};
        GetTextFaceW(hdcChk, LF_FACESIZE, faceName);
        out->hasMDL2 = (wcscmp(faceName, L"Segoe MDL2 Assets") == 0) ? 1 : 0;
        SelectObject(hdcChk, hOld);
        ReleaseDC(nullptr, hdcChk);
    }
}

// Card background with a subtle Win11-style bottom shadow (light themes only).
void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor)
{
    int lum = (299 * GetRValue(contentBg) + 587 * GetGValue(contentBg) + 114 * GetBValue(contentBg)) / 1000;
    bool bgIsLight = (lum > 128);

    if (bgIsLight) {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        Gdiplus::REAL r = (Gdiplus::REAL)cornerR;
        int expand = 0;
        int yOff = 1;
        Gdiplus::GraphicsPath sp;
        Gdiplus::RectF srf((Gdiplus::REAL)(x - expand), (Gdiplus::REAL)(y - expand + yOff),
            (Gdiplus::REAL)(w + 2 * expand), (Gdiplus::REAL)(h + 2 * expand));
        sp.AddArc(srf.X, srf.Y, r, r, 180, 90);
        sp.AddArc(srf.X + srf.Width - r, srf.Y, r, r, 270, 90);
        sp.AddArc(srf.X + srf.Width - r, srf.Y + srf.Height - r, r, r, 0, 90);
        sp.AddArc(srf.X, srf.Y + srf.Height - r, r, r, 90, 90);
        sp.CloseFigure();
        Gdiplus::SolidBrush sb(Gdiplus::Color(20, 0, 0, 0));
        g.FillPath(&sb, &sp);
    }

    HPEN hPen = CreatePen(PS_SOLID, 1, cardBorder);
    HBRUSH hBr = CreateSolidBrush(cardBg);
    SelectObject(hdc, hPen); SelectObject(hdc, hBr);
    RoundRect(hdc, x, y, x + w, y + h, cornerR, cornerR);
    DeleteObject(hBr); DeleteObject(hPen);

    if (isHover) {
        HBRUSH hHov = CreateSolidBrush(hoverColor);
        HPEN hHovPen = CreatePen(PS_SOLID, 1, hoverColor);
        SelectObject(hdc, hHov); SelectObject(hdc, hHovPen);
        RoundRect(hdc, x, y, x + w, y + h, cornerR, cornerR);
        DeleteObject(hHov); DeleteObject(hHovPen);
    }
}

void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc)
{
    UNREFERENCED_PARAMETER(isHover);
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    int trackW = rc.right - rc.left;
    int trackH = rc.bottom - rc.top;
    int knobSize = trackH - 8;   // Win11: 12px knob in 20px track
    int knobInset = 4;
    Gdiplus::REAL radius = (Gdiplus::REAL)trackH;

    auto toColor = [](COLORREF c, BYTE a = 255) {
        return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
    };

    Gdiplus::GraphicsPath trackPath;
    Gdiplus::RectF trackRect((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)trackW, (Gdiplus::REAL)trackH);
    trackPath.AddArc(trackRect.X, trackRect.Y, radius, radius, 90, 180);
    trackPath.AddArc(trackRect.X + trackRect.Width - radius, trackRect.Y, radius, radius, 270, 180);
    trackPath.CloseFigure();

    if (isOn) {
        Gdiplus::SolidBrush trackBrush(toColor(tc.toggleOnTrack));
        g.FillPath(&trackBrush, &trackPath);
        Gdiplus::SolidBrush knobBrush(toColor(tc.toggleOnKnob));
        g.FillEllipse(&knobBrush,
            (Gdiplus::REAL)(rc.right - knobSize - knobInset - 1),
            (Gdiplus::REAL)(rc.top + knobInset),
            (Gdiplus::REAL)knobSize, (Gdiplus::REAL)knobSize);
    } else {
        Gdiplus::Pen trackPen(toColor(tc.toggleOffBorder), 1.5f);
        g.DrawPath(&trackPen, &trackPath);
        Gdiplus::SolidBrush knobBrush(toColor(tc.toggleOffKnob));
        g.FillEllipse(&knobBrush,
            (Gdiplus::REAL)(rc.left + knobInset + 1),
            (Gdiplus::REAL)(rc.top + knobInset),
            (Gdiplus::REAL)knobSize, (Gdiplus::REAL)knobSize);
    }
}

// Uses GetTextExtentPoint32 — same API as the preview's drawing code, so the
// controls below the preview line up with its painted bottom.
int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    HFONT hF = CreateFontW(-MulDiv(s.fontSize, dpi, 72), 0, 0, 0,
        s.fontWeight, s.fontItalic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, s.fontFaceName);
    if (!hF) return ScaleForDpi(172, dpi);
    HFONT hOld = (HFONT)SelectObject(hdc, hF);
    WCHAR test[] = L"國國國國";
    SIZE sz; GetTextExtentPoint32(hdc, test, 4, &sz);
    int cyLine = sz.cy * 5 / 4;
    int fistLineOffset = cyLine / 3;
    int candWndH = 6 * cyLine + fistLineOffset + cyLine / 3;
    SelectObject(hdc, hOld);
    DeleteObject(hF);
    return candWndH + ScaleForDpi(g_geo.previewGap, dpi);
}

// SDF shadow (same algorithm as ShadowWindow.cpp) around a w x h box at x, y.
static void PreviewShadow(HDC hdc, const SettingsSnapshot& s, int x, int y, int w, int h,
    int corner, UINT dpi, COLORREF sysFace)
{
    int sp = ScaleForDpi(14, dpi); // SHADOW_SPREAD
    int shadowW = w + sp * 2;
    int shadowH = h + sp * 2;
    int shadowX = x - sp;
    int shadowY = y - sp;

    int imeLum = (299 * GetRValue(s.itemBGColor) + 587 * GetGValue(s.itemBGColor) + 114 * GetBValue(s.itemBGColor)) / 1000;
    BOOL imeIsDark = (imeLum <= 128);
    COLORREF contentBg = sysFace;
    int contentLum = (299 * GetRValue(contentBg) + 587 * GetGValue(contentBg) + 114 * GetBValue(contentBg)) / 1000;
    BOOL bgIsLight = (contentLum > 128);
    BYTE shadowR = imeIsDark ? 200 : 0;
    BYTE shadowG = imeIsDark ? 200 : 0;
    BYTE shadowB = imeIsDark ? 200 : 0;
    BYTE maxAlpha = 35;
    if (imeIsDark && bgIsLight) {
        shadowR = 0; shadowG = 0; shadowB = 0;
        maxAlpha = 45;
    } else if (!imeIsDark && !bgIsLight) {
        shadowR = 200; shadowG = 200; shadowB = 200;
    }

    double r = corner * 0.5;
    double half_w = w * 0.5;
    double half_h = h * 0.5;
    double inner_rx = half_w - r;
    double inner_ry = half_h - r;

    HDC dcMem = CreateCompatibleDC(hdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = shadowW;
    bi.bmiHeader.biHeight = -shadowH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (hBmp && pBits) {
        memset(pBits, 0, (size_t)shadowW * shadowH * 4);
        HBITMAP hOld = (HBITMAP)SelectObject(dcMem, hBmp);
        struct RGBALPHA { BYTE b, g, r, a; };
        for (int yy = 0; yy < shadowH; yy++) {
            for (int xx = 0; xx < shadowW; xx++) {
                double ox = xx - sp;
                double oy = yy - sp;
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
                RGBALPHA* px = (RGBALPHA*)pBits + ((size_t)yy * shadowW + xx);
                px->r = (BYTE)((DWORD)shadowR * alpha / 255);
                px->g = (BYTE)((DWORD)shadowG * alpha / 255);
                px->b = (BYTE)((DWORD)shadowB * alpha / 255);
                px->a = alpha;
            }
        }
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, shadowX, shadowY, shadowW, shadowH, dcMem, 0, 0, shadowW, shadowH, bf);
        SelectObject(dcMem, hOld);
        DeleteObject(hBmp);
    }
    DeleteDC(dcMem);
}

// Candidate + notify preview — metrics are an exact copy of
// CCandidateWindow::_DrawList and CNotifyWindow::_DrawText.
void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace)
{
    UNREFERENCED_PARAMETER(cardW);
    UNREFERENCED_PARAMETER(totalH);
    int prevTop = curY;
    int padLeft = ScaleForDpi(g_geo.rowIconPadLeft, dpi);

    HFONT hCandFont = CreateFontW(-MulDiv(s.fontSize, dpi, 72), 0, 0, 0,
        s.fontWeight, s.fontItalic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, s.fontFaceName);
    if (!hCandFont) hCandFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HFONT hOldF = (HFONT)SelectObject(hdc, hCandFont);

    const int wndWidth = 4;
    WCHAR testStr[5] = L"國國國國";
    SIZE candSize = {};
    GetTextExtentPoint32(hdc, testStr, wndWidth, &candSize);
    TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
    int cxLine = max((int)tm.tmAveCharWidth, (int)candSize.cx / wndWidth);
    int cyLine = candSize.cy * 5 / 4;
    int fistLineOffset = cyLine / 3;
    int leftPadding = cyLine / 2;
    int cxTitle = candSize.cx + leftPadding + cxLine;

    const wchar_t* candNums[] = { L"1", L"2", L"3", L"4", L"5", L"6" };
    const wchar_t* candTexts[] = { L"國", L"過", L"鍋", L"裹", L"鴿", L"擱" };
    int candCount = 6;
    int selectedIdx = 0;
    int candWndH = candCount * cyLine + fistLineOffset + cyLine / 3;
    int cornerRadius = MulDiv(8, dpi, 96);

    int prevX = margin + padLeft;

    PreviewShadow(hdc, s, prevX, prevTop, cxTitle, candWndH, cornerRadius, dpi, sysFace);

    HBRUSH hBkBr = CreateSolidBrush(s.itemBGColor);
    HPEN hBkPen = CreatePen(PS_SOLID, 1, s.itemBGColor);
    SelectObject(hdc, hBkBr); SelectObject(hdc, hBkPen);
    RoundRect(hdc, prevX, prevTop, prevX + cxTitle, prevTop + candWndH, cornerRadius, cornerRadius);
    DeleteObject(hBkBr); DeleteObject(hBkPen);

    int cyOffset = candSize.cy / 8 + fistLineOffset;
    for (int pi = 0; pi < candCount; pi++) {
        RECT rc;
        rc.top = prevTop + pi * cyLine + fistLineOffset;
        rc.bottom = rc.top + cyLine;
        bool isSelected = (pi == selectedIdx);

        if (isSelected) {
            RECT rcFullRow = { prevX, rc.top, prevX + cxTitle, rc.bottom };
            HBRUSH hFRBr = CreateSolidBrush(s.itemBGColor);
            FillRect(hdc, &rcFullRow, hFRBr);
            DeleteObject(hFRBr);

            int hMargin = leftPadding;
            int vInset = MulDiv(1, dpi, 96);
            int selCornerR = MulDiv(4, dpi, 96);
            RECT rcHL = { prevX + hMargin, rc.top + vInset, prevX + cxTitle - hMargin, rc.bottom - vInset };
            HBRUSH hSelBr = CreateSolidBrush(s.selectedBGColor);
            SelectObject(hdc, hSelBr); SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            RoundRect(hdc, rcHL.left, rcHL.top, rcHL.right, rcHL.bottom, selCornerR, selCornerR);
            DeleteObject(hSelBr);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, s.selectedColor);
            rc.left = prevX + leftPadding;
            rc.right = prevX + leftPadding + cxLine;
            ExtTextOut(hdc, prevX + leftPadding, prevTop + pi * cyLine + cyOffset, 0, &rc, candNums[pi], 1, NULL);
            rc.left = prevX + leftPadding + cxLine;
            rc.right = prevX + cxTitle;
            ExtTextOut(hdc, prevX + leftPadding + cxLine, prevTop + pi * cyLine + cyOffset, 0, &rc, candTexts[pi], (DWORD)wcslen(candTexts[pi]), NULL);
        } else {
            rc.left = prevX + leftPadding;
            rc.right = prevX + leftPadding + cxLine;
            SetTextColor(hdc, s.numberColor);
            SetBkColor(hdc, s.itemBGColor);
            SetBkMode(hdc, OPAQUE);
            ExtTextOut(hdc, prevX + leftPadding, prevTop + pi * cyLine + cyOffset, ETO_OPAQUE, &rc, candNums[pi], 1, NULL);
            rc.left = prevX + leftPadding + cxLine;
            rc.right = prevX + cxTitle;
            SetTextColor(hdc, s.itemColor);
            SetBkColor(hdc, s.itemBGColor);
            ExtTextOut(hdc, prevX + leftPadding + cxLine, prevTop + pi * cyLine + cyOffset, ETO_OPAQUE, &rc, candTexts[pi], (DWORD)wcslen(candTexts[pi]), NULL);
        }
        if (isSelected) SetBkMode(hdc, OPAQUE);
    }

    const wchar_t* notifyText = L"中文";
    int notifyLen = (int)wcslen(notifyText);
    SIZE notifySize;
    GetTextExtentPoint32(hdc, notifyText, notifyLen, &notifySize);
    int notifyW = notifySize.cx + notifySize.cx / notifyLen;
    int notifyHt = notifySize.cy * 3 / 2;
    int notifyX = prevX + cxTitle + ScaleForDpi(20, dpi);
    int notifyY = prevTop + (candWndH - notifyHt) / 2;
    int nCorner = MulDiv(8, dpi, 96);

    PreviewShadow(hdc, s, notifyX, notifyY, notifyW, notifyHt, nCorner, dpi, sysFace);

    HBRUSH hNBr = CreateSolidBrush(s.itemBGColor);
    HPEN hNPen = CreatePen(PS_SOLID, 1, s.itemBGColor);
    SelectObject(hdc, hNBr); SelectObject(hdc, hNPen);
    RoundRect(hdc, notifyX, notifyY, notifyX + notifyW, notifyY + notifyHt, nCorner, nCorner);
    DeleteObject(hNBr); DeleteObject(hNPen);

    // TRANSPARENT: ETO_OPAQUE would overwrite the rounded corners.
    SetTextColor(hdc, s.itemColor);
    SetBkMode(hdc, TRANSPARENT);
    ExtTextOut(hdc, notifyX + notifySize.cx / notifyLen / 2,
        notifyY + notifyHt / 5, 0, nullptr,
        notifyText, (DWORD)notifyLen, NULL);

    SelectObject(hdc, hOldF);
    if (hCandFont != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hCandFont);
}

void PaintSidebar(HDC hdc, const PaintContext& ctx)
{
    if (ctx.sidebarCollapsed) return;
    UINT dpi = ctx.dpi;
    const ThemeColors& tc = ctx.theme;
    int sidebarW = ScaleForDpi(g_geo.sidebarWidth, dpi);
    int itemH = ScaleForDpi(g_geo.sidebarItemH, dpi);
    int itemR = ScaleForDpi(g_geo.sidebarItemR, dpi);
    int iconSize = ScaleForDpi(g_geo.sidebarIconSize, dpi);
    int iconGap = ScaleForDpi(g_geo.sidebarIconGap, dpi);
    int padL = ScaleForDpi(g_geo.sidebarItemPadLeft, dpi);

    RECT rcSidebar = { 0, 0, sidebarW, ctx.clientH };
    FillRect(hdc, &rcSidebar, ctx.brushSidebarBg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, ctx.fontBody);

    int sidebarCount = 0;
    const SidebarItemDef* items = ::GetSidebarItems(&sidebarCount);
    int startY = ScaleForDpi(g_geo.sidebarStartY, dpi);

    for (int i = 0; i < sidebarCount; i++) {
        if (items[i].iconResourceId == -1) {
            int sepY = startY + i * itemH + itemH / 2;
            HPEN hSep = CreatePen(PS_SOLID, 1, tc.cardBorder);
            HPEN oldPen = (HPEN)SelectObject(hdc, hSep);
            MoveToEx(hdc, padL, sepY, nullptr);
            LineTo(hdc, sidebarW - padL, sepY);
            SelectObject(hdc, oldPen);
            DeleteObject(hSep);
            continue;
        }

        int itemPadX = ScaleForDpi(g_geo.sidebarItemR, dpi);
        RECT rcItem = { itemPadX, startY + i * itemH, sidebarW - itemPadX, startY + (i + 1) * itemH };
        bool isSelected = (i == ctx.selectedModeIndex);

        if (isSelected || i == ctx.hoverSidebarIndex) {
            COLORREF hlColor = isSelected ? tc.sidebarSelect : tc.sidebarHover;
            HBRUSH hBr = CreateSolidBrush(hlColor);
            HPEN hPen = CreatePen(PS_SOLID, 1, hlColor);
            SelectObject(hdc, hBr); SelectObject(hdc, hPen);
            RoundRect(hdc, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, itemR * 2, itemR * 2);
            DeleteObject(hBr); DeleteObject(hPen);
        }

        if (isSelected) {
            int barW = ScaleForDpi(g_geo.sidebarAccentBarW, dpi);
            int barH = ScaleForDpi(g_geo.sidebarAccentBarH, dpi);
            int barX = rcItem.left + ScaleForDpi(g_geo.sidebarItemR, dpi);
            int barY = rcItem.top + (itemH - barH) / 2;
            int barR = ScaleForDpi(g_geo.sidebarAccentBarR, dpi);
            HBRUSH hAcc = CreateSolidBrush(tc.accent);
            HRGN hRgn = CreateRoundRectRgn(barX, barY, barX + barW + 1, barY + barH + 1, barR, barR);
            FillRgn(hdc, hRgn, hAcc);
            DeleteObject(hRgn); DeleteObject(hAcc);
        }

        int iconX = rcItem.left + padL;
        int iconY = rcItem.top + (itemH - iconSize) / 2;
        if (items[i].iconResourceId > 0) {
            HICON hIcon = (HICON)LoadImageW(ctx.iconModule,
                MAKEINTRESOURCEW(items[i].iconResourceId),
                IMAGE_ICON, iconSize, iconSize, 0);
            if (hIcon) {
                DrawIconEx(hdc, iconX, iconY, hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                DestroyIcon(hIcon);
            }
        }

        SetTextColor(hdc, tc.textPrimary);
        RECT rcText = rcItem;
        rcText.left = iconX + iconSize + iconGap;
        DrawTextW(hdc, items[i].label, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    SelectObject(hdc, oldFont);
}

static int CtrlRightFor(int margin, int cardW, UINT dpi, bool hasChevron)
{
    return SettingsWindowLayout::CtrlRight(margin, cardW, dpi, hasChevron);
}

static void PaintToggleOrNot(HDC hdc, RECT rc, bool isOn, const ThemeColors& tc)
{
    Cpp::PaintToggleSwitch(hdc, rc, isOn, false, tc);
}

// Unified row renderer for all pages.
static void PaintRow(HDC hdc, const PaintContext& ctx, const LayoutRow& lr, int margin, int curY,
    int cardW, int thisH, int ctrlRightEdge, UINT dpi, bool isChild)
{
    const ThemeColors& tc = ctx.theme;
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int iconSz = ScaleForDpi(g_geo.rowIconSize, dpi);
    int iconPadL = ScaleForDpi(g_geo.rowIconPadLeft, dpi);
    int iconGap = ScaleForDpi(g_geo.rowIconGap, dpi);
    int sepIndent = ScaleForDpi(g_geo.rowSepIndent, dpi);

    if (lr.type == RowType::CandidatePreview) {
        Cpp::PaintCandidatePreview(hdc, ctx.snapshot, margin, curY, cardW, thisH, dpi, ctx.sysFace);
        return;
    }

    bool hasIcon = (lr.iconGlyph || lr.iconColor);
    int childIconStart = margin + iconPadL + iconSz + iconGap;

    if (lr.type != RowType::ColorGrid && lr.type != RowType::RichEditor && hasIcon) {
        int ix = isChild ? childIconStart : (margin + iconPadL);
        int iy = curY + (thisH - iconSz) / 2;
        if (lr.iconGlyph && ctx.fontMDL2Icon && ctx.hasMDL2) {
            HFONT hOldMDL2 = (HFONT)SelectObject(hdc, ctx.fontMDL2Icon);
            SetTextColor(hdc, tc.textPrimary);
            wchar_t glyph[2] = { lr.iconGlyph, 0 };
            RECT rcGlyph = { ix, iy, ix + iconSz, iy + iconSz };
            DrawTextW(hdc, glyph, 1, &rcGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            SelectObject(hdc, hOldMDL2);
        } else if (lr.iconColor) {
            HBRUSH hIBr = CreateSolidBrush(lr.iconColor);
            HPEN hIPen = CreatePen(PS_SOLID, 1, lr.iconColor);
            SelectObject(hdc, hIBr); SelectObject(hdc, hIPen);
            RoundRect(hdc, ix, iy, ix + iconSz, iy + iconSz, iconSz / 4, iconSz / 4);
            DeleteObject(hIBr); DeleteObject(hIPen);
        }
    }

    int labelLeft = isChild ? (hasIcon ? (childIconStart + iconSz + iconGap)
                                       : (margin + sepIndent))
                            : (margin + iconPadL + iconSz + iconGap);

    if (lr.type == RowType::RichEditor) {
        int hdrH = ScaleForDpi(g_geo.editorHeaderH / 2, dpi);
        SelectObject(hdc, ctx.fontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcRH = { margin + pad, curY + pad / 2, margin + cardW - pad, curY + hdrH };
        DrawTextW(hdc, lr.label, -1, &rcRH, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        if (lr.description) {
            SelectObject(hdc, ctx.fontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcRI = { margin + pad, curY + hdrH, margin + cardW - pad, curY + ScaleForDpi(g_geo.editorHeaderH, dpi) };
            DrawTextW(hdc, lr.description, -1, &rcRI, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        return;
    }

    if (lr.type == RowType::ColorGrid) {
        const wchar_t* swLabels[] = { L"候選字文字", L"選取文字", L"候選字背景", L"聯想詞文字", L"編號文字", L"選取背景" };
        int swS = ScaleForDpi(g_geo.swatchSize, dpi);
        int swGap = ScaleForDpi(g_geo.swatchCellGap, dpi);
        int cellH = swS + swGap;
        int baseX = margin;
        SelectObject(hdc, ctx.fontDesc);
        SIZE szLbl; GetTextExtentPoint32W(hdc, swLabels[0], 5, &szLbl);
        int maxLblW = szLbl.cx;
        int gap = maxLblW / 5;
        int rightEdge = ctrlRightEdge - margin;
        int colW = rightEdge / 3;
        for (int si = 0; si < 6; si++) {
            int col = si % 3, row2 = si / 3;
            int colRight = baseX + (col + 1) * colW;
            int swatchX = colRight - swS;
            int labelX = swatchX - gap - maxLblW;
            int sy = curY + row2 * cellH;
            SetTextColor(hdc, tc.textSecondary);
            RECT rcSL = { labelX, sy, labelX + maxLblW, sy + swS };
            DrawTextW(hdc, swLabels[si], -1, &rcSL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        return;
    }

    {
        SelectObject(hdc, isChild ? ctx.fontBody : ctx.fontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        int lt = curY + ScaleForDpi(lr.description ? g_geo.rowLabelTopOff : 0, dpi);
        int lb = lr.description ? curY + thisH / 2 + ScaleForDpi(g_geo.rowLabelMidOff, dpi) : curY + thisH;
        RECT rcL = { labelLeft, lt, ctrlRightEdge, lb };

        if (lr.type == RowType::Clickable && lr.action == RowAction::OpenFontDialog) {
            WCHAR preview[128];
            StringCchPrintfW(preview, 128, L"%s  —  %s, %upt", lr.label, ctx.snapshot.fontFaceName, ctx.snapshot.fontSize);
            DrawTextW(hdc, preview, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        } else {
            DrawTextW(hdc, lr.label, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }

        if (lr.description) {
            SelectObject(hdc, ctx.fontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcD = { labelLeft, curY + thisH / 2 - ScaleForDpi(g_geo.rowLabelMidOff, dpi), rcL.right, curY + thisH - ScaleForDpi(g_geo.rowDescBotOff, dpi) };
            DrawTextW(hdc, lr.description, -1, &rcD, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
    }

    if (lr.action == RowAction::ExpandSection && lr.type == RowType::ComboBox
        && ctx.snapshot.colorMode == 3) {
        int chevArea = ScaleForDpi(g_geo.chevronW + 4, dpi);
        if (ctx.hasMDL2 && ctx.fontMDL2) SelectObject(hdc, ctx.fontMDL2);
        else SelectObject(hdc, ctx.fontBody);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcChev2 = { ctrlRightEdge + ScaleForDpi(4, dpi), curY, ctrlRightEdge + chevArea, curY + thisH };
        DrawTextW(hdc, ctx.colorGridExpanded
            ? (ctx.hasMDL2 ? L"\xE70E" : L"\x2227")
            : (ctx.hasMDL2 ? L"\xE70D" : L"\x2228"),
            -1, &rcChev2, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    if (lr.type == RowType::Toggle) {
        int tw = ScaleForDpi(g_geo.toggleW, dpi), th = ScaleForDpi(g_geo.toggleH, dpi);
        int tx = ctrlRightEdge - tw;
        int ty = curY + (thisH - th) / 2;
        bool isOn = (lr.id >= 0 && lr.id < kToggleSlots) ? (ctx.toggleOn[lr.id] != 0) : false;
        RECT rcTog = { tx, ty, tx + tw, ty + th };
        PaintToggleOrNot(hdc, rcTog, isOn, tc);
        SetTextColor(hdc, tc.textPrimary);
        SelectObject(hdc, ctx.fontBody);
        RECT rcSt = { tx - ScaleForDpi(g_geo.toggleLabelW, dpi), ty, tx - ScaleForDpi(g_geo.toggleLabelGapR, dpi), ty + th };
        DrawTextW(hdc, isOn ? L"開啟" : L"關閉", -1, &rcSt, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
    }
}

static void PaintCardList(HDC hdc, const PaintContext& ctx, const wchar_t* pageTitle,
    const LayoutCard* cards, int layoutCount, int modeMask, int expandBaseIdx)
{
    UINT dpi = ctx.dpi;
    const ThemeColors& tc = ctx.theme;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int catRowH = ScaleForDpi(g_geo.categoryRowH, dpi);
    int cornerR = ScaleForDpi(g_geo.cardRadius, dpi);
    int iconSz = ScaleForDpi(g_geo.rowIconSize, dpi);
    int iconPadL = ScaleForDpi(g_geo.rowIconPadLeft, dpi);
    int iconGap = ScaleForDpi(g_geo.rowIconGap, dpi);

    SetBkMode(hdc, TRANSPARENT);

    HFONT oldFont = (HFONT)SelectObject(hdc, ctx.fontTitle);
    SetTextColor(hdc, tc.textPrimary);
    int titleLeft0 = (ctx.sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    int titleY, titleB;
    if (ctx.sidebarNarrowMode) {
        titleY = -ctx.scrollPos;
        titleB = ScaleForDpi(g_geo.hamburgerSize, dpi) - ctx.scrollPos;
    } else {
        titleY = ScaleForDpi(g_geo.topPad, dpi) - ctx.scrollPos;
        titleB = titleY + ScaleForDpi(g_geo.titleAreaH, dpi);
    }
    RECT rcTitle = { titleLeft0, titleY, ctx.clientW - margin, titleB };
    DrawTextW(hdc, pageTitle, -1, &rcTitle, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    int cardW = ctx.cardW;
    int cardGapDpi = ScaleForDpi(g_geo.cardGap, dpi);
    int curY = ScaleForDpi(g_geo.topMargin, dpi) - ctx.scrollPos;

    for (int i = 0; i < layoutCount; i++) {
        bool vis = (cards[i].rowCount == 0);
        if (!vis) for (int r = 0; !vis && r < cards[i].rowCount; r++) {
            if (IsRowVisible(cards[i].rows[r], modeMask)) vis = true;
        }
        if (!vis) continue;

        int cRowH = ScaleForDpi(g_geo.childRowH, dpi);
        int numChildren = 0;
        int expIdx = expandBaseIdx + i;
        if (cards[i].action == RowAction::ExpandSection && ctx.cardExpanded[expIdx] && cards[i].rows) {
            for (int r = 0; r < cards[i].rowCount; r++)
                if (IsRowVisible(cards[i].rows[r], modeMask)) numChildren++;
        }
        int cardH = 2 * pad + catRowH + (numChildren > 0 ? numChildren * (cRowH + 1) : 0);

        bool isHover = (i == ctx.hoverRowIndex);
        if (numChildren > 0) {
            Cpp::DrawCardWithShadow(hdc, margin, curY, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, false, tc.rowHover);
            if (isHover) {
                HBRUSH hHov = CreateSolidBrush(tc.rowHover);
                HPEN hHovPen = CreatePen(PS_SOLID, 1, tc.rowHover);
                SelectObject(hdc, hHov); SelectObject(hdc, hHovPen);
                RoundRect(hdc, margin, curY, margin + cardW, curY + 2 * pad + catRowH, cornerR, cornerR);
                DeleteObject(hHov); DeleteObject(hHovPen);
            }
        } else {
            Cpp::DrawCardWithShadow(hdc, margin, curY, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, isHover, tc.rowHover);
        }

        int rowTop = curY + pad;
        int labelLeft = margin + iconPadL + iconSz + iconGap;

        {
            int ix = margin + iconPadL, iy = rowTop + (catRowH - iconSz) / 2;
            if (cards[i].iconGlyph && ctx.fontMDL2Icon && ctx.hasMDL2) {
                HFONT hOldMDL2 = (HFONT)SelectObject(hdc, ctx.fontMDL2Icon);
                SetTextColor(hdc, tc.textPrimary);
                wchar_t glyph[2] = { cards[i].iconGlyph, 0 };
                RECT rcGlyph = { ix, iy, ix + iconSz, iy + iconSz };
                DrawTextW(hdc, glyph, 1, &rcGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(hdc, hOldMDL2);
            } else if (cards[i].iconColor) {
                COLORREF ic = cards[i].iconColor;
                HBRUSH hIBr = CreateSolidBrush(ic);
                HPEN hIPen = CreatePen(PS_SOLID, 1, ic);
                SelectObject(hdc, hIBr); SelectObject(hdc, hIPen);
                RoundRect(hdc, ix, iy, ix + iconSz, iy + iconSz, iconSz / 4, iconSz / 4);
                DeleteObject(hIBr); DeleteObject(hIPen);
            }
        }

        SelectObject(hdc, ctx.fontCardHeader);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcL = { labelLeft, rowTop + ScaleForDpi(g_geo.rowLabelTopOff, dpi), margin + cardW - pad - ScaleForDpi(g_geo.chevronW + g_geo.rowLabelMidOff, dpi), rowTop + catRowH / 2 + ScaleForDpi(g_geo.rowLabelMidOff, dpi) };
        DrawTextW(hdc, cards[i].title, -1, &rcL, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        if (cards[i].description) {
            SelectObject(hdc, ctx.fontDesc);
            SetTextColor(hdc, tc.textSecondary);
            RECT rcD = { labelLeft, rowTop + catRowH / 2 - ScaleForDpi(g_geo.rowLabelMidOff, dpi), margin + cardW - pad - ScaleForDpi(g_geo.chevronW + g_geo.rowLabelMidOff, dpi), rowTop + catRowH - ScaleForDpi(g_geo.rowDescBotOff, dpi) };
            DrawTextW(hdc, cards[i].description, -1, &rcD, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }

        if (ctx.hasMDL2 && ctx.fontMDL2) SelectObject(hdc, ctx.fontMDL2);
        else SelectObject(hdc, ctx.fontBody);
        SetTextColor(hdc, tc.textPrimary);
        RECT rcChev = { margin + cardW - pad - ScaleForDpi(g_geo.chevronW, dpi), rowTop, margin + cardW - pad, rowTop + catRowH };
        if (cards[i].action == RowAction::NavigateToCard)
            DrawTextW(hdc, ctx.hasMDL2 ? L"\xE76C" : L">", -1, &rcChev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        else if (cards[i].action == RowAction::ExpandSection)
            DrawTextW(hdc, ctx.cardExpanded[expIdx] ? (ctx.hasMDL2 ? L"\xE70E" : L"\x2227") : (ctx.hasMDL2 ? L"\xE70D" : L"\x2228"), -1, &rcChev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        if (cards[i].action == RowAction::ExpandSection && ctx.cardExpanded[expIdx] && cards[i].rows) {
            int childY = rowTop + catRowH + pad;
            for (int r = 0; r < cards[i].rowCount; r++) {
                const LayoutRow& lr = cards[i].rows[r];
                if (!IsRowVisible(lr, modeMask)) continue;
                {
                    HPEN hCSep = CreatePen(PS_SOLID, 1, BlendColor(tc.cardBg, tc.textSecondary, 40));
                    SelectObject(hdc, hCSep);
                    MoveToEx(hdc, margin + 1, childY, nullptr);
                    LineTo(hdc, margin + cardW - 1, childY);
                    DeleteObject(hCSep);
                    childY += 1;
                }
                int childCtrlRight = CtrlRightFor(margin, cardW, dpi, false);
                PaintRow(hdc, ctx, lr, margin, childY, cardW, cRowH, childCtrlRight, dpi, true);
                childY += cRowH;
            }
        }

        curY += cardH + cardGapDpi;
    }

    // Repaint the section title area on top of scrolled cards.
    int titleAreaBottom = ScaleForDpi(g_geo.topMargin, dpi);
    RECT rcTitleCover = { 0, 0, ctx.clientW, titleAreaBottom };
    FillRect(hdc, &rcTitleCover, ctx.brushContentBg);
    SelectObject(hdc, ctx.fontTitle);
    SetTextColor(hdc, tc.textPrimary);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, ctx.fontTitle);
    SetTextColor(hdc, tc.textPrimary);
    int titleLeft = (ctx.sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    RECT rcTitle2;
    if (ctx.sidebarNarrowMode)
        rcTitle2 = { titleLeft, 0, ctx.clientW - margin, ScaleForDpi(g_geo.hamburgerSize, dpi) };
    else
        rcTitle2 = { titleLeft, ScaleForDpi(g_geo.topPad, dpi), ctx.clientW - margin, ScaleForDpi(g_geo.topPad + g_geo.titleAreaH, dpi) };
    DrawTextW(hdc, pageTitle, -1, &rcTitle2, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SelectObject(hdc, oldFont);
}

static void PaintSettingsDetail(HDC hdc, const PaintContext& ctx)
{
    UINT dpi = ctx.dpi;
    const ThemeColors& tc = ctx.theme;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int cornerR = ScaleForDpi(g_geo.cardRadius, dpi);

    SetBkMode(hdc, TRANSPARENT);

    int layoutCount = 0;
    const LayoutCard* allCards = GetIMEPageLayout(&layoutCount);
    if (ctx.currentCardIndex < 0 || ctx.currentCardIndex >= layoutCount) return;
    const LayoutCard& card = allCards[ctx.currentCardIndex];

    int modeMask = SettingsModel::GetModeBitmask((IME_MODE)ctx.currentMode);

    int cardW = ctx.cardW;
    int ctrlRightX = CtrlRightFor(margin, cardW, dpi, true);

    SettingsWindowLayout::RowContext rowCtx = {};
    rowCtx.dpi = dpi;
    rowCtx.scrollPos = ctx.scrollPos;
    rowCtx.modeMask = modeMask;
    rowCtx.colorMode = ctx.snapshot.colorMode;
    rowCtx.gridExpanded = ctx.colorGridExpanded ? 1 : 0;
    rowCtx.previewH = Cpp::CandidatePreviewHeight(hdc, ctx.snapshot, dpi);
    std::vector<SettingsWindowLayout::RowSpan> cardSpans = SettingsWindowLayout::RowSpans(card, rowCtx);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, ctx.fontBody);
    int cardGap = ScaleForDpi(g_geo.cardGap, dpi);
    int cardTop = ScaleForDpi(g_geo.topPad, dpi) - ctx.scrollPos;
    for (const auto& span : cardSpans) {
        int cardH = span.height;
        bool isPreviewCard = (span.startRow < card.rowCount
            && card.rows[span.startRow].type == RowType::CandidatePreview);
        if (!isPreviewCard) {
            bool isHoverL2 = (span.startRow == ctx.hoverRowIndex);
            Cpp::DrawCardWithShadow(hdc, margin, cardTop, cardW, cardH, cornerR,
                tc.cardBg, tc.cardBorder, tc.contentBg, isHoverL2, tc.rowHover);
        }

        int curY = cardTop + pad;
        for (int r = span.startRow; r < span.endRow; r++) {
            const LayoutRow& lr = card.rows[r];
            if (!SettingsWindowLayout::RowShown(lr, modeMask, rowCtx.colorMode, rowCtx.gridExpanded != 0)) continue;
            int thisH = SettingsWindowLayout::RowHeight(lr, dpi, rowCtx.previewH);
            bool isChild = (lr.expandParent != CTRL_NONE);
            PaintRow(hdc, ctx, lr, margin, curY, cardW, thisH, ctrlRightX, dpi, isChild);
            curY += thisH;
        }
        cardTop += cardH + cardGap;
    }
    SelectObject(hdc, oldFont);
}

void PaintContent(HDC hdc, const PaintContext& ctx)
{
    RECT rc = { 0, 0, ctx.clientW, ctx.clientH };
    FillRect(hdc, &rc, ctx.brushContentBg);

    if (ctx.selectedModeIndex == 5) {
        int ltPageCount = 0;
        const LayoutCard* ltPage = GetLoadTablePageLayout(&ltPageCount);
        PaintCardList(hdc, ctx, L"載入碼表", ltPage, ltPageCount, SM_MODE_ALL, 4);
    } else if (ctx.navLevel == 0) {
        int sidebarCount = 0;
        const SidebarItemDef* sItems = ::GetSidebarItems(&sidebarCount);
        const wchar_t* title = (ctx.selectedModeIndex < sidebarCount && sItems[ctx.selectedModeIndex].label)
            ? sItems[ctx.selectedModeIndex].label : L"DIME";
        int layoutCount = 0;
        const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
        int modeMask = SettingsModel::GetModeBitmask((IME_MODE)ctx.currentMode);
        PaintCardList(hdc, ctx, title, cards, layoutCount, modeMask, 0);
    } else {
        PaintSettingsDetail(hdc, ctx);
    }
}

void PaintBreadcrumb(HDC hdc, const PaintContext& ctx)
{
    UINT dpi = ctx.dpi;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
    const ThemeColors& tc = ctx.theme;
    SetBkMode(hdc, TRANSPARENT);

    HFONT oldFont = (HFONT)SelectObject(hdc, ctx.fontTitle);

    SetTextColor(hdc, tc.textPrimary);
    int sbc = 0; const SidebarItemDef* si = ::GetSidebarItems(&sbc);
    const wchar_t* modeName = (ctx.selectedModeIndex < sbc && si[ctx.selectedModeIndex].label)
        ? si[ctx.selectedModeIndex].label : L"DIME";
    int bcLeft = (ctx.sidebarNarrowMode) ? ScaleForDpi(g_geo.hamburgerSize, dpi) : margin;
    RECT rcM = { bcLeft, 0, bcLeft + ScaleForDpi(g_geo.bcModeNameW, dpi), bcH };
    DrawTextW(hdc, modeName, -1, &rcM, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT);
    int modeRight = rcM.right;
    rcM = { bcLeft, 0, modeRight, bcH };
    DrawTextW(hdc, modeName, -1, &rcM, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SetTextColor(hdc, tc.textSecondary);
    RECT rcS = { modeRight, 0, modeRight + ScaleForDpi(g_geo.bcSepW, dpi), bcH };
    DrawTextW(hdc, L"  \x203A  ", -1, &rcS, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_CALCRECT);
    int sepRight = rcS.right;
    rcS = { modeRight, 0, sepRight, bcH };
    DrawTextW(hdc, L"  \x203A  ", -1, &rcS, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    SetTextColor(hdc, tc.textPrimary);
    int lc = 0; const LayoutCard* lCards = GetIMEPageLayout(&lc);
    const wchar_t* catName = (ctx.currentCardIndex >= 0 && ctx.currentCardIndex < lc)
        ? lCards[ctx.currentCardIndex].title : L"";
    RECT rcC = { sepRight, 0, sepRight + ScaleForDpi(g_geo.bcCatNameW, dpi), bcH };
    DrawTextW(hdc, catName, -1, &rcC, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    SelectObject(hdc, oldFont);
}

void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg)
{
    FillRect(hdc, &rc, bg);
    SetBkMode(hdc, TRANSPARENT);
    if (ctx.hasMDL2 && ctx.fontMDL2Icon) {
        SelectObject(hdc, ctx.fontMDL2Icon);
        SetTextColor(hdc, ctx.theme.textPrimary);
        DrawTextW(hdc, L"\xE700", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    } else {
        SelectObject(hdc, ctx.fontCardHeader);
        SetTextColor(hdc, ctx.theme.textPrimary);
        DrawTextW(hdc, L"\x2261", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
}

// Win11 style button: anti-aliased rounded corners via GDI+.
void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi)
{
    Gdiplus::REAL r = (Gdiplus::REAL)ScaleForDpi(4, dpi);

    COLORREF fill   = disabled ? tc.buttonFillDisabled :
                      pressed  ? tc.buttonFillPressed  : tc.buttonFill;
    COLORREF border = tc.buttonBorder;
    COLORREF textC  = disabled ? tc.buttonTextDisabled : tc.buttonText;

    auto toColor = [](COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); };

    // Fill background with card bg to eliminate white corners
    HBRUSH hBgBr = CreateSolidBrush(tc.cardBg);
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF rr((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)(rc.right - rc.left - 1), (Gdiplus::REAL)(rc.bottom - rc.top - 1));
    Gdiplus::REAL d = r * 2;
    path.AddArc(rr.X, rr.Y, d, d, 180, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
    path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
    path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
    path.CloseFigure();

    Gdiplus::SolidBrush fillBrush(toColor(fill));
    g.FillPath(&fillBrush, &path);
    Gdiplus::Pen borderPen(toColor(border), 1.0f);
    g.DrawPath(&borderPen, &path);

    if (text && text[0]) {
        HFONT hOldF = (HFONT)SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textC);
        DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        SelectObject(hdc, hOldF);
    }
}

void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius)
{
    HBRUSH hBr = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    SelectObject(hdc, hBr); SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    DeleteObject(hBr); DeleteObject(hPen);
}

void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi)
{
    HBRUSH hBg = CreateSolidBrush(tc.contentBg);
    FillRect(hdc, &L.rcClient, hBg);
    DeleteObject(hBg);

    if (icon)
        DrawIconEx(hdc, L.iconX, L.iconY, icon, L.iconSize, L.iconSize, 0, nullptr, DI_NORMAL);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, tc.textPrimary);
    HFONT oldF = (HFONT)SelectObject(hdc, font);
    RECT rcText = { L.textX, L.textY, L.textX + L.textW, L.textY + L.textH };
    DrawTextW(hdc, text, -1, &rcText, DT_WORDBREAK | DT_LEFT);

    int btnX = SettingsWindowLayout::MsgBoxFirstButtonX(L, btnCount);
    for (int i = 0; i < btnCount; i++) {
        RECT rcBtn = { btnX, L.btnY, btnX + L.btnW, L.btnY + L.btnH };
        bool isHover = (i == hoverBtn);
        bool isDefault = (i == defaultBtn);
        COLORREF fill = isHover ? tc.buttonFillHover : tc.buttonFill;
        COLORREF border = isDefault ? tc.accent : tc.buttonBorder;

        HBRUSH hBtn = CreateSolidBrush(fill);
        HPEN hPen = CreatePen(PS_SOLID, 1, border);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hBtn);
        HPEN oldPn = (HPEN)SelectObject(hdc, hPen);
        int r = ScaleForDpi(4, dpi);
        RoundRect(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, r, r);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPn);
        DeleteObject(hBtn);
        DeleteObject(hPen);

        SetTextColor(hdc, tc.buttonText);
        DrawTextW(hdc, labels[i], -1, &rcBtn, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        btnX += L.btnW + L.btnGap;
    }
    SelectObject(hdc, oldF);
}

void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent)
{
    Cpp::PaintSidebar(hdc, main);
    if (main.navLevel == 1 && main.selectedModeIndex < 4 && hasContent) {
        UINT dpi = main.dpi;
        int sidebarW = (!main.sidebarNarrowMode && !main.sidebarCollapsed) ? ScaleForDpi(g_geo.sidebarWidth, dpi) : 0;
        int bcH = ScaleForDpi(g_geo.breadcrumbH, dpi);
        if (main.sidebarNarrowMode && !main.sidebarCollapsed) {
            // The open narrow sidebar covers the breadcrumb strip.
            int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpi);
            HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, bcH);
            int savedDC = SaveDC(hdc);
            ExtSelectClipRgn(hdc, hClip, RGN_AND);
            Cpp::PaintSidebar(hdc, main);
            RestoreDC(hdc, savedDC);
            DeleteObject(hClip);
            RECT rcBcRest = { sidebarWOv, 0, 32767, bcH };
            FillRect(hdc, &rcBcRest, main.brushContentBg);
            int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
            RECT rcHbBg = { 0, 0, hbSize, hbSize };
            Cpp::PaintHamburger(hdc, main, rcHbBg, main.brushSidebarBg);
        } else {
            RECT rcBC = { sidebarW, 0, 32767, bcH };
            FillRect(hdc, &rcBC, main.brushContentBg);
            int savedDC = SaveDC(hdc);
            SetViewportOrgEx(hdc, sidebarW, 0, nullptr);
            Cpp::PaintBreadcrumb(hdc, main);
            RestoreDC(hdc, savedDC);
            if (main.sidebarNarrowMode) {
                int hbSize = ScaleForDpi(g_geo.hamburgerSize, dpi);
                RECT rcHb = { sidebarW, 0, sidebarW + hbSize, hbSize };
                Cpp::PaintHamburger(hdc, main, rcHb, main.brushContentBg);
            }
        }
    }
}

void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main)
{
    Cpp::PaintContent(hdc, content);
    if (main.sidebarNarrowMode && !main.sidebarCollapsed) {
        UINT dpiOv = main.dpi;
        int sidebarWOv = ScaleForDpi(g_geo.sidebarWidth, dpiOv);
        bool hasBc = (main.navLevel == 1 && main.selectedModeIndex < 4);
        int bcOff = hasBc ? ScaleForDpi(g_geo.breadcrumbH, dpiOv) : 0;
        HRGN hClip = CreateRectRgn(0, 0, sidebarWOv, 32767);
        int savedOv = SaveDC(hdc);
        ExtSelectClipRgn(hdc, hClip, RGN_AND);
        if (bcOff) SetViewportOrgEx(hdc, 0, -bcOff, nullptr);
        Cpp::PaintSidebar(hdc, main);
        RestoreDC(hdc, savedOv);
        DeleteObject(hClip);
    }
    if (main.sidebarNarrowMode && main.navLevel == 0) {
        int hbSize = ScaleForDpi(g_geo.hamburgerSize, main.dpi);
        RECT rcHb = { 0, 0, hbSize, hbSize };
        Cpp::PaintHamburger(hdc, main, rcHb, main.brushContentBg);
    }
}

} // namespace Cpp

// ============================================================================
// Dispatch
// ============================================================================
#ifdef DIME_USE_RUST_CONFIG

#ifdef _WIN64
static_assert(sizeof(PaintContext) == 520, "PaintContext must match dime-settings");
static_assert(offsetof(PaintContext, dpi) == 376, "PaintContext::dpi");
static_assert(sizeof(Fonts) == 56, "Fonts must match dime-settings");
#else
static_assert(sizeof(PaintContext) == 484, "PaintContext must match dime-settings");
static_assert(offsetof(PaintContext, dpi) == 340, "PaintContext::dpi");
static_assert(sizeof(Fonts) == 28, "Fonts must match dime-settings");
#endif
static_assert(offsetof(PaintContext, snapshot) == 88, "PaintContext::snapshot");
static_assert(offsetof(PaintContext, fontTitle) == 304, "PaintContext::fontTitle");
static_assert(offsetof(PaintContext, toggleOn) == offsetof(PaintContext, cardExpanded) + 16, "PaintContext::toggleOn");

void CreateFonts(UINT dpi, Fonts* out) { dime_settings_create_fonts(dpi, out); }

void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor)
{
    dime_settings_draw_card(hdc, x, y, w, h, cornerR, cardBg, cardBorder, contentBg, isHover ? 1 : 0, hoverColor);
}

void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc)
{
    dime_settings_paint_toggle(hdc, &rc, isOn ? 1 : 0, isHover ? 1 : 0, &tc);
}

int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    return dime_settings_preview_height(hdc, &s, dpi);
}

void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace)
{
    dime_settings_paint_preview(hdc, &s, margin, curY, cardW, totalH, dpi, sysFace);
}

void PaintSidebar(HDC hdc, const PaintContext& ctx) { dime_settings_paint_sidebar(hdc, &ctx); }
void PaintContent(HDC hdc, const PaintContext& ctx) { dime_settings_paint_content(hdc, &ctx); }
void PaintBreadcrumb(HDC hdc, const PaintContext& ctx) { dime_settings_paint_breadcrumb(hdc, &ctx); }

void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg)
{
    dime_settings_paint_hamburger(hdc, &ctx, &rc, bg);
}

void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi)
{
    dime_settings_paint_button(hdc, &rc, reinterpret_cast<const uint16_t*>(text),
        pressed ? 1 : 0, disabled ? 1 : 0, &tc, font, dpi);
}

void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius)
{
    dime_settings_paint_swatch(hdc, &rc, color, border, radius);
}

void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent)
{
    dime_settings_paint_main_frame(hdc, &main, hasContent ? 1 : 0);
}

void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main)
{
    dime_settings_paint_content_frame(hdc, &content, &main);
}

void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi)
{
    dime_settings_paint_msgbox(hdc, &L, reinterpret_cast<const uint16_t*>(text),
        reinterpret_cast<const uint16_t* const*>(labels), btnCount, hoverBtn, defaultBtn, &tc, font, icon, dpi);
}

#else

void CreateFonts(UINT dpi, Fonts* out) { Cpp::CreateFonts(dpi, out); }
void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor)
{
    Cpp::DrawCardWithShadow(hdc, x, y, w, h, cornerR, cardBg, cardBorder, contentBg, isHover, hoverColor);
}
void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc) { Cpp::PaintToggleSwitch(hdc, rc, isOn, isHover, tc); }
int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi) { return Cpp::CandidatePreviewHeight(hdc, s, dpi); }
void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace)
{
    Cpp::PaintCandidatePreview(hdc, s, margin, curY, cardW, totalH, dpi, sysFace);
}
void PaintSidebar(HDC hdc, const PaintContext& ctx) { Cpp::PaintSidebar(hdc, ctx); }
void PaintContent(HDC hdc, const PaintContext& ctx) { Cpp::PaintContent(hdc, ctx); }
void PaintBreadcrumb(HDC hdc, const PaintContext& ctx) { Cpp::PaintBreadcrumb(hdc, ctx); }
void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg) { Cpp::PaintHamburger(hdc, ctx, rc, bg); }
void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi)
{
    Cpp::PaintOwnerButton(hdc, rc, text, pressed, disabled, tc, font, dpi);
}
void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius) { Cpp::PaintSwatch(hdc, rc, color, border, radius); }
void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent) { Cpp::PaintMainFrame(hdc, main, hasContent); }
void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main) { Cpp::PaintContentFrame(hdc, content, main); }
void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi)
{
    Cpp::PaintMsgBox(hdc, L, text, labels, btnCount, hoverBtn, defaultBtn, tc, font, icon, dpi);
}

#endif

} // namespace SettingsWindowPaint
