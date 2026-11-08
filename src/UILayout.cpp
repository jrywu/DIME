// UILayout.cpp - see UILayout.h

#include "Private.h"
#include "Globals.h"
#include "Define.h"
#include "UILayout.h"
#include "ScrollBarWindow.h"
#ifdef DIME_USE_RUST_ENGINE
#include "dime_ffi.h"
static_assert(sizeof(RECT) == sizeof(DimeRect) && sizeof(POINT) == sizeof(DimePoint), "RECT / POINT layout");
static_assert(sizeof(UILayout::NotifySlot) == sizeof(DimeNotifySlot), "NotifySlot layout");
#endif

namespace UILayout
{
namespace Cpp
{
//+---------------------------------------------------------------------------
//
// RectInRect (from CBaseWindow)
//
//----------------------------------------------------------------------------

DWORD RectInRect(_In_ const RECT *prcLimit, _In_ const RECT *prcTarget)
{
	if(prcLimit == nullptr || prcTarget == nullptr) return 0;
    DWORD dwFlags = 0;
    // Check if prcTarget is entirely inside prcLimit
    if (prcLimit->left <= prcTarget->left && prcTarget->right  <= prcLimit->right &&
        prcLimit->top  <= prcTarget->top  && prcTarget->bottom <= prcLimit->bottom)
    {
        return RECT_INSIDE;
    }

    // Check horizontal range.  Target can be
    // - wider than the limit (assert here since it should never happen)
    // - entirely outside the limit (RECT_OVERLEFT or RECT_OVERRIGHT)
    // - partially inside the limit (RECT_OVERLEFT or RECT_OVERRIGHT)
    if (prcTarget->left < prcLimit->left && prcTarget->right > prcLimit->right)
    {
        assert(FALSE);
        dwFlags |= RECT_TOO_WIDE;
    }
    else if (prcTarget->left < prcLimit->left) 
    {
        dwFlags |= RECT_OVER_LEFT;
    }
    else if (prcTarget->right > prcLimit->right)
    {
        dwFlags |= RECT_OVER_RIGHT;
    }

    // Check vertical range.  Target can be
    // - taller than the limit (assert here since it should never happen)
    // - entirely outside the limit (RECT_OVERTOP or RECT_OVERBOTTOM)
    // - partially inside the limit (RECT_OVERTOP or RECT_OVERBOTTOM)
    if (prcTarget->top < prcLimit->top && prcTarget->bottom > prcLimit->bottom)
    {
        assert(FALSE);
        dwFlags |= RECT_TOO_TALL;
    }
    else if (prcTarget->top < prcLimit->top)
    {
        dwFlags |= RECT_OVER_TOP;
    }
    else if (prcTarget->bottom > prcLimit->bottom)
    {
        dwFlags |= RECT_OVER_BOTTOM;
    }

    return dwFlags;
}

// _GetWindowExtent (from CBaseWindow): a text extent wider than the window is a
// large area rather than a caret or composition range (e.g. 32-bit Firefox).
POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow)
{
	if ((prcTextExtent->right - prcTextExtent->left) > (prcWindow->right - prcWindow->left))
		return *(LPPOINT)&prcTextExtent->left;
	else
		return *(LPPOINT)&prcTextExtent->right;
}

//+---------------------------------------------------------------------------
//
// CalcFitPointAroundTextExtent (from CBaseWindow)
//
//----------------------------------------------------------------------------

void CalcFitPointAroundTextExtent(_In_ const RECT *prcTextExtent, _In_ const RECT *prcWorkArea, _In_ const RECT *prcWindow, _Out_ POINT *ppt)
{
	debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, prcWorkArea top = %d, bottom = %d, right = %d, left = %d",
		prcWorkArea->top, prcWorkArea->bottom, prcWorkArea->right, prcWorkArea->left);
	
	if(prcTextExtent == nullptr || prcWindow == nullptr || ppt == nullptr) return;
    RECT rcTargetWindow[2];
    DWORD dwFlags[2];

    // set rcTargetWindow[0] which rectangle attached on bottom side of text extent
    rcTargetWindow[0] = *prcWindow;
	int baseX = prcTextExtent->right;
	if ((prcTextExtent->right - prcTextExtent->left) > (prcWindow->right - prcWindow->left))
	// the return text extent area is a large area instead as careet or composition range, like firefox 32bit.
		baseX = prcTextExtent->left;
	
	OffsetRect(&rcTargetWindow[0], baseX, prcTextExtent->bottom + SHADOW_SPREAD / 2);

	debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, attach top side: top = %d, bottom = %d, right = %d, left = %d",
		rcTargetWindow[0].top, rcTargetWindow[0].bottom, rcTargetWindow[0].right, rcTargetWindow[0].left);
	

    // set rcTargetWindow[1] which rectangle attached on top side of text extent
    rcTargetWindow[1] = *prcWindow;
	OffsetRect(&rcTargetWindow[1], baseX, prcTextExtent->top - (prcWindow->bottom - prcWindow->top) - SHADOW_SPREAD / 2);

	debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, attach bottom side: top = %d, bottom = %d, right = %d, left = %d",
		rcTargetWindow[1].top, rcTargetWindow[1].bottom, rcTargetWindow[1].right, rcTargetWindow[1].left);
	

    //
    // check target rectangle fit in workarea
    //
    for (DWORD i = 0; i < ARRAYSIZE(rcTargetWindow); i++)
    {
        dwFlags[i] = RectInRect(prcWorkArea, &rcTargetWindow[i]);
		
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[%d] = %x",i, dwFlags[i]);

        // generally, rcTargetWindow[0] or rcTargetWindow[1] have neither of these flags
        // but in the case where window is just complete way off the screen e.g. user
        // starts input, then drag window way off screen, both of these will be true
        if ((dwFlags[i] & RECT_OVER_TOP) || (dwFlags[i] & RECT_OVER_BOTTOM))
        {
			debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[%d] over top or over bottom ", i);
            continue;
        }

        if (dwFlags[i] == RECT_INSIDE)
        {
            *ppt = *(POINT*)&rcTargetWindow[i].left;
			debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[%d] inside rect and ppt.x= %d, ppt.y=%d ", i, ppt->x, ppt->y);
            return;
        }
        else if ((dwFlags[i] & RECT_OVER_LEFT) != 0)
        {
            ppt->x = 0;
            ppt->y = rcTargetWindow[i].top;
			debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[%d] over left and ppt.x= %d, ppt.y=%d ", i, ppt->x, ppt->y);
            return;
        }
        else if ((dwFlags[i] & RECT_OVER_RIGHT) != 0)
        {
			ppt->x = prcTextExtent->left - (prcWindow->right - prcWindow->left);
            ppt->y = rcTargetWindow[i].top;
			debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[%d] over right and ppt.x= %d, ppt.y=%d ", i, ppt->x, ppt->y);
            return;
        }
    }

    // if both rcTargetWindow have RECT_OVER_TOP or RECT_OVER_BOTTOM,
    // then "dock" the window to top or bottom of working area.
    if ((dwFlags[0] & RECT_OVER_TOP) != 0)
    {
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[0] over top and ppt.y=0 (dock top) ");
        ppt->y = 0;
    }
    else 
    {
        ppt->y = prcWorkArea->bottom - (prcWindow->bottom - prcWindow->top);
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[0] is over bottom and ppt.y = %d ", ppt->y);
    }

    // dock to left/right edge if RECT_OVER_LEFT or RECT_OVER_RIGHT.
    // else just stay where we are
    if ((dwFlags[0] & RECT_OVER_LEFT) != 0)
    {
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[0] over left and ppt.x=0 (dock left) ");
        ppt->x = 0;
    }
    else if ((dwFlags[0] & RECT_OVER_RIGHT) != 0)
    {
		ppt->x = prcTextExtent->left - (prcWindow->right - prcWindow->left);
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[0] over right and ppt.x= %d ", ppt->x);
    }
    else
    {
        ppt->x = prcTextExtent->right;
		debugPrint(L"CBaseWindow::CalcFitPointAroundTextExtent, dwFlags[0] is not over right and ppt.x= %d ", ppt->x);
    }

    return;
}

// _DecideStackGrowUp (from CUIPresenter)
BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom)
{
	if (candidateVisible)
		return (candidateTop < notifyY) ? TRUE : FALSE;
	// No candidate: only flip up if downward stack would overflow screen bottom.
	int totalH = 0;
	for (size_t i = 0; i < count; i++)
	{
		if (slots[i].visible)
			totalH += slots[i].height + SHADOW_SPREAD / 2;
	}
	return (notifyY + totalH > screenBottom) ? TRUE : FALSE;
}

// _ComputeStackPosForNewSlot (from CUIPresenter)
POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp)
{
	POINT p = notifyLocation;
	int offset = 0;
	for (size_t i = 0; i < count; ++i)
	{
		if (i == newSlot) continue;
		if (slots[i].visible)
			offset += slots[i].height + SHADOW_SPREAD / 2;
	}
	int h = newSlot < count ? slots[newSlot].height : 0;
	p.y = growUp ? (notifyLocation.y - h - offset)
	             : (notifyLocation.y + offset);
	return p;
}

// _RestackVisibleSlots (from CUIPresenter)
void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
	_In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed)
{
	int candLeft = 0;
	int anchorY = notifyLocation.y;
	if (candidateVisible)
	{
		candLeft = candidateLocation.x - SHADOW_SPREAD / 2;
		anchorY = growUp ? candidateRect.bottom : candidateLocation.y;
	}
	int offset = 0;
	for (size_t i = 0; i < count; ++i)
	{
		positions[i] = { 0, 0 };
		placed[i] = FALSE;
		if (!slots[i].visible) continue;
		int h = slots[i].height;
		int slotX = candidateVisible ? (candLeft - slots[i].width) : notifyLocation.x;
		int slotY = growUp ? (anchorY - h - offset) : (anchorY + offset);
		positions[i] = { slotX, slotY };
		placed[i] = TRUE;
		offset += h + SHADOW_SPREAD / 2;
	}
}

//+---------------------------------------------------------------------------
//
// Candidate window (from CCandidateWindow)
//
//----------------------------------------------------------------------------

int MulDivInt(int a, int b, int c)
{
	return ::MulDiv(a, b, c);
}

int ScaleForDpi(int value, UINT dpi)
{
	return ::MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
}

static const int StringPosition = 2;

void CandInitialMetrics(SIZE measured, UINT wndWidth, _Out_ int* pcxTitle, _Out_ int* pcyRow, _Out_ int* pPadding)
{
	// WM_CREATE (candSize.cx / _wndWidth is an unsigned division).
	int cxTitle = measured.cx + StringPosition * (measured.cx / wndWidth);
	int cyRow = measured.cy * 5 / 4;
	*pcxTitle = cxTitle;
	*pcyRow = cyRow;
	*pPadding = cyRow / 2;
}

CandMetrics CandMeasureMetrics(SIZE measured, int aveCharWidth, UINT wndWidth)
{
	CandMetrics m = {};
	m.cxLine = max(aveCharWidth, (int)measured.cx / (int)wndWidth);
	m.cyLine = measured.cy * 5 / 4;
	m.firstLineOffset = m.cyLine / 3;
	m.leftPadding = m.cyLine / 2;
	m.cxTitle = measured.cx + (m.leftPadding + m.cxLine);
	m.padding = m.cyLine / 2;
	m.cyOffset = measured.cy / 8 + m.firstLineOffset; //offset in line + blank before 1st line.
	return m;
}

SIZE CandWindowSize(int cxTitle, int cyRow, int padding, int rows, int sbWidth)
{
	int topPadding = cyRow / 3;
	int bottomPadding = padding;
	SIZE size;
	size.cx = cxTitle + sbWidth + CANDWND_BORDER_WIDTH * 2;
	size.cy = topPadding + cyRow * rows + bottomPadding + CANDWND_BORDER_WIDTH * 2;
	return size;
}

RECT CandScrollBarRect(const RECT& client, int sbWidth, UINT dpi, int cornerRadiusBase)
{
	int arcRadius = ::MulDiv(cornerRadiusBase / 2, dpi, USER_DEFAULT_SCREEN_DPI);
	int left = client.right - sbWidth;
	int top = client.top + arcRadius;
	int width = sbWidth;
	int height = client.bottom - client.top - arcRadius * 2;
	return { left, top, left + width, top + height };
}

CandRow CandRowLayout(const RECT& paint, int indexInPage, const CandMetrics& m, int sbWidth, UINT dpi)
{
	CandRow r = {};
	int top = paint.top + indexInPage * m.cyLine + m.firstLineOffset;
	int bottom = top + m.cyLine;
	int vInset = ::MulDiv(1, dpi, USER_DEFAULT_SCREEN_DPI);
	r.cornerRadius = ::MulDiv(4, dpi, USER_DEFAULT_SCREEN_DPI);
	r.row = { paint.left, top, paint.right, bottom };
	// Symmetric margins: same as padding on both sides
	r.highlight = { paint.left + m.leftPadding, top + vInset, paint.right - m.leftPadding, bottom - vInset };
	r.number = { paint.left + m.leftPadding, top, paint.left + (m.leftPadding + m.cxLine), bottom };
	r.text = { paint.left + (m.leftPadding + m.cxLine), top, paint.right - sbWidth, bottom };
	r.numberX = m.leftPadding;
	r.textX = m.leftPadding + m.cxLine;
	r.textY = indexInPage * m.cyLine + m.cyOffset;
	return r;
}

int CandTruncateItem(int length, _Out_ int* pTailOffset)
{
	// if the length of item text > MAX_CAND_ITEM_LENGTH (13) , truncate the item text as
	// "First 6 chars of itemtext"+"..."+"last 6 chars of itemtext"
	if (length > MAX_CAND_ITEM_LENGTH)
	{
		*pTailOffset = length - 6;
		return MAX_CAND_ITEM_LENGTH;
	}
	*pTailOffset = -1;
	return length;
}

BOOL CandItemNeedsMeasure(int displayLength, UINT wndWidth)
{
	return displayLength > (int)wndWidth;
}

BOOL CandWidenForItem(int displayLength, int measuredCx, int textLeft, _Inout_ int* pcxTitle, _Inout_ UINT* pWndWidth)
{
	if (*pcxTitle < measuredCx + textLeft)
	{
		*pcxTitle = measuredCx + textLeft;
		*pWndWidth = displayLength;
		return TRUE;
	}
	return FALSE;
}

BOOL CandNeedsResize(int cxTitle, const RECT& paint, int sbWidth)
{
	return cxTitle != paint.right - paint.left - sbWidth;
}

int CandHitRow(const RECT& client, int padding, int sbWidth, int cyRow, UINT rowsInPage, UINT pageStart, UINT count, POINT pt)
{
	RECT rc = { 0, 0, 0, 0 };
	rc.left = client.left + padding;
	rc.right = client.right - sbWidth - CANDWND_BORDER_WIDTH;
	UINT index = pageStart;
	for (UINT pageCount = 0; (index < count) && (pageCount < rowsInPage); index++, pageCount++)
	{
		rc.top = client.top + cyRow / 3 + (pageCount * cyRow);
		rc.bottom = client.top + cyRow / 3 + ((pageCount + 1) * cyRow);
		if (PtInRect(&rc, pt))
			return (int)pageCount;
	}
	return -1;
}

RECT CandScrollColumnRect(const RECT& client, int sbWidth)
{
	return { client.right - sbWidth - CANDWND_BORDER_WIDTH, client.top, client.right, client.bottom };
}

RECT CandListAreaRect(const RECT& client, int sbWidth)
{
	return { client.left, client.top, client.right - sbWidth - CANDWND_BORDER_WIDTH, client.bottom };
}

RECT CandPageIndicatorRect(const RECT& client, int cyRow, int rows, int sbWidth, UINT dpi)
{
	RECT rc;
	rc.left = client.left;
	rc.right = client.right;
	rc.top = cyRow / 3 + rows * cyRow;
	rc.bottom = client.bottom;
	int hPad = ::MulDiv(4, dpi, USER_DEFAULT_SCREEN_DPI);
	rc.right -= (sbWidth + hPad);
	return rc;
}

int CandPageIndicatorFontHeight(int lfHeight)
{
	return ::MulDiv(lfHeight, 70, 100);
}

//+---------------------------------------------------------------------------
//
// Scroll bar (from CScrollBarWindow)
//
//----------------------------------------------------------------------------

static RECT BtnUpRect(const RECT& rc, UINT dpi)
{
	int btnH = ::MulDiv(SCROLLBAR_BTN_HEIGHT, dpi, USER_DEFAULT_SCREEN_DPI);
	return { rc.left, rc.top, rc.right, rc.top + min(btnH, (rc.bottom - rc.top) / 3) };
}

static RECT BtnDnRect(const RECT& rc, UINT dpi)
{
	int btnH = ::MulDiv(SCROLLBAR_BTN_HEIGHT, dpi, USER_DEFAULT_SCREEN_DPI);
	return { rc.left, rc.bottom - min(btnH, (rc.bottom - rc.top) / 3), rc.right, rc.bottom };
}

// thumbH / thumbY of a proportionally-sized thumb in a track of trackH.
static void ThumbSpan(int trackH, UINT dpi, const ScrollInfo& si, int* pThumbH, int* pThumbY)
{
	int thumbMinH = ::MulDiv(SCROLLBAR_THUMB_MIN_H, dpi, USER_DEFAULT_SCREEN_DPI);
	int thumbH = ::MulDiv(trackH, si.nPage, si.nMax);
	thumbH = max(thumbH, thumbMinH);
	thumbH = min(thumbH, trackH);

	int posRange = si.nMax - si.nPage;
	int thumbY = (posRange > 0) ? ::MulDiv(trackH - thumbH, si.nPos, posRange) : 0;
	thumbY = max(thumbY, 0);
	thumbY = min(thumbY, trackH - thumbH);
	*pThumbH = thumbH;
	*pThumbY = thumbY;
}

ScrollParts ScrollBarParts(const RECT& client, UINT dpi, const ScrollInfo& si)
{
	ScrollParts p = {};
	p.up = BtnUpRect(client, dpi);
	p.down = BtnDnRect(client, dpi);
	p.track = { client.left, p.up.bottom, client.right, p.down.top };

	int trackH = p.track.bottom - p.track.top;
	if (trackH <= 0 || si.nMax <= 0 || si.nMax <= si.nPage)
	{
		// Thumb fills entire track when everything fits in one page
		p.thumb = p.track;
		return p;
	}
	int thumbH = 0, thumbY = 0;
	ThumbSpan(trackH, dpi, si, &thumbH, &thumbY);
	p.thumb = { p.track.left, p.track.top + thumbY, p.track.right, p.track.top + thumbY + thumbH };
	return p;
}

int ScrollBarHitTest(const RECT& client, UINT dpi, const ScrollInfo& si, POINT pt)
{
	ScrollParts p = Cpp::ScrollBarParts(client, dpi, si);
	if (PtInRect(&p.up, pt)) return SB_HIT_BTN_UP;
	if (PtInRect(&p.down, pt)) return SB_HIT_BTN_DOWN;
	if (PtInRect(&p.thumb, pt)) return SB_HIT_THUMB;
	if (PtInRect(&p.track, pt))
		return (pt.y < p.thumb.top) ? SB_HIT_TRACK_ABOVE : SB_HIT_TRACK_BELOW;
	return SB_HIT_NONE;
}

BOOL ScrollBarIdleThumb(const RECT& client, UINT dpi, const ScrollInfo& si, _Out_ RECT* prc)
{
	// Idle/hiding: thin centered line only, no arrows, full-height track.
	*prc = { 0, 0, 0, 0 };
	int trackH = client.bottom - client.top;
	if (trackH <= 0) return FALSE;

	int thumbH = 0, thumbY = 0;
	ThumbSpan(trackH, dpi, si, &thumbH, &thumbY);

	// Center a narrow line within the idle scrollbar width
	int lineW = max(::MulDiv(SCROLLBAR_IDLE_THUMB_W, dpi, USER_DEFAULT_SCREEN_DPI), 2);
	int cx = (client.left + client.right) / 2;
	prc->left = cx - lineW / 2;
	prc->right = prc->left + lineW;
	prc->top = client.top + thumbY;
	prc->bottom = prc->top + thumbH;
	return TRUE;
}

ScrollArrows ScrollBarArrows(const RECT& client, UINT dpi)
{
	ScrollArrows a = {};
	RECT rcUp = BtnUpRect(client, dpi);
	int btnW = rcUp.right - rcUp.left;
	// Win11 Explorer style: triangle spans ~60% of button width, flat wedge aspect
	// Triangle fits within button width with 1px margin each side; equilateral proportions
	int aw = max(2, btnW / 3);
	int ah = max(::MulDiv(2, dpi, USER_DEFAULT_SCREEN_DPI), aw * 87 / 100); // equilateral: ah = aw * sqrt(3)/2
	int acx = (rcUp.left + rcUp.right) / 2;
	int acy = rcUp.top + ah + 1;             // apex 1px inside top edge
	a.up[0] = { acx, acy - ah };
	a.up[1] = { acx - aw, acy + ah };
	a.up[2] = { acx + aw, acy + ah };

	RECT rcDn = BtnDnRect(client, dpi);
	acx = (rcDn.left + rcDn.right) / 2;
	acy = rcDn.bottom - ah - 1;              // apex 1px inside bottom edge
	a.down[0] = { acx, acy + ah };
	a.down[1] = { acx - aw, acy - ah };
	a.down[2] = { acx + aw, acy - ah };

	// Pill narrower than triangle: use (aw-1) as half-width so pill insets 1px inside each triangle side.
	// GDI half-integer rule: center=(left+right-1)/2 must be acx+0.5 → left+right=2*acx+2.
	// With half-width pw=aw-1: left=acx-pw+1, right=acx+pw+1. Topmost pixel at x=acx.
	int pw = max(2, aw - 1);
	a.pillLeft = acx - pw + 1;
	a.pillRight = acx + pw + 1;
	a.pillRadius = 2 * pw;  // full-width corner ellipse → capsule shape
	return a;
}

BOOL ScrollBarDragPos(const RECT& client, UINT dpi, const ScrollInfo& si, int startPos, int deltaY, _Out_ int* pPos)
{
	*pPos = 0;
	ScrollParts p = Cpp::ScrollBarParts(client, dpi, si);
	int trackH = p.track.bottom - p.track.top;
	int thumbH = p.thumb.bottom - p.thumb.top;
	int slideRange = trackH - thumbH;
	if (slideRange <= 0) return FALSE;

	int posRange = si.nMax - si.nPage;
	int newPos = startPos + ::MulDiv(deltaY, posRange, slideRange);
	newPos = max(newPos, 0);
	newPos = min(newPos, posRange);
	*pPos = newPos;
	return TRUE;
}

int ScrollBarClampPos(int pos, int nMax)
{
	pos = min(pos, nMax - 1);
	pos = max(pos, 0);
	return pos;
}

RECT ScrollBarAttachedRect(int parentW, int parentH, int sbWidth)
{
	int left = parentW - sbWidth - CANDWND_BORDER_WIDTH;
	int top = CANDWND_BORDER_WIDTH;
	return { left, top, left + sbWidth, top + parentH - CANDWND_BORDER_WIDTH * 2 };
}
} // namespace Cpp

#ifdef DIME_USE_RUST_ENGINE
DWORD RectInRect(_In_ const RECT* prcLimit, _In_ const RECT* prcTarget)
{
	if (prcLimit == nullptr || prcTarget == nullptr) return 0;
	return dime_ui_rect_in_rect((const DimeRect*)prcLimit, (const DimeRect*)prcTarget);
}

POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow)
{
	POINT pt = { 0, 0 };
	dime_ui_work_area_probe((const DimeRect*)prcTextExtent, (const DimeRect*)prcWindow, (DimePoint*)&pt);
	return pt;
}

void CalcFitPointAroundTextExtent(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWorkArea, _In_ const RECT* prcWindow, _Out_ POINT* ppt)
{
	if (prcTextExtent == nullptr || prcWorkArea == nullptr || prcWindow == nullptr || ppt == nullptr) return;
	dime_ui_fit_point((const DimeRect*)prcTextExtent, (const DimeRect*)prcWorkArea, (const DimeRect*)prcWindow, (DimePoint*)ppt);
}

BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom)
{
	return dime_ui_stack_grow_up(candidateVisible ? 1 : 0, candidateTop, notifyY, (const DimeNotifySlot*)slots, count, screenBottom) ? TRUE : FALSE;
}

POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp)
{
	DimePoint out = { 0, 0 };
	dime_ui_stack_pos_for_new_slot({ notifyLocation.x, notifyLocation.y }, (const DimeNotifySlot*)slots, count, newSlot, growUp ? 1 : 0, &out);
	return { out.x, out.y };
}

void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
	_In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed)
{
	DimeRect rect = { candidateRect.left, candidateRect.top, candidateRect.right, candidateRect.bottom };
	dime_ui_restack(candidateVisible ? 1 : 0, rect, { candidateLocation.x, candidateLocation.y }, { notifyLocation.x, notifyLocation.y },
		(const DimeNotifySlot*)slots, count, growUp ? 1 : 0, (DimePoint*)positions, (int32_t*)placed);
}

static_assert(sizeof(UILayout::CandMetrics) == sizeof(DimeCandMetrics), "CandMetrics layout");
static_assert(sizeof(UILayout::CandRow) == sizeof(DimeCandRow), "CandRow layout");
static_assert(sizeof(UILayout::ScrollInfo) == sizeof(DimeScrollInfo), "ScrollInfo layout");
static_assert(sizeof(UILayout::ScrollParts) == sizeof(DimeScrollParts), "ScrollParts layout");
static_assert(sizeof(UILayout::ScrollArrows) == sizeof(DimeScrollArrows), "ScrollArrows layout");

int MulDivInt(int a, int b, int c) { return dime_ui_mul_div(a, b, c); }
int ScaleForDpi(int value, UINT dpi) { return dime_ui_mul_div(value, (int)dpi, USER_DEFAULT_SCREEN_DPI); }

void CandInitialMetrics(SIZE measured, UINT wndWidth, _Out_ int* pcxTitle, _Out_ int* pcyRow, _Out_ int* pPadding)
{
	dime_ui_cand_initial_metrics(measured.cx, measured.cy, wndWidth, pcxTitle, pcyRow, pPadding);
}

CandMetrics CandMeasureMetrics(SIZE measured, int aveCharWidth, UINT wndWidth)
{
	CandMetrics m = {};
	dime_ui_cand_metrics(measured.cx, measured.cy, aveCharWidth, wndWidth, (DimeCandMetrics*)&m);
	return m;
}

SIZE CandWindowSize(int cxTitle, int cyRow, int padding, int rows, int sbWidth)
{
	int32_t w = 0, h = 0;
	dime_ui_cand_window_size(cxTitle, cyRow, padding, rows, sbWidth, &w, &h);
	return { w, h };
}

RECT CandScrollBarRect(const RECT& client, int sbWidth, UINT dpi, int cornerRadiusBase)
{
	RECT rc = { 0, 0, 0, 0 };
	dime_ui_cand_scrollbar_rect((const DimeRect*)&client, sbWidth, dpi, cornerRadiusBase, (DimeRect*)&rc);
	return rc;
}

CandRow CandRowLayout(const RECT& paint, int indexInPage, const CandMetrics& m, int sbWidth, UINT dpi)
{
	CandRow r = {};
	dime_ui_cand_row((const DimeRect*)&paint, indexInPage, (const DimeCandMetrics*)&m, sbWidth, dpi, (DimeCandRow*)&r);
	return r;
}

int CandTruncateItem(int length, _Out_ int* pTailOffset)
{
	*pTailOffset = -1;
	return dime_ui_cand_truncate(length, pTailOffset);
}

BOOL CandItemNeedsMeasure(int displayLength, UINT wndWidth)
{
	return dime_ui_cand_item_needs_measure(displayLength, wndWidth) ? TRUE : FALSE;
}

BOOL CandWidenForItem(int displayLength, int measuredCx, int textLeft, _Inout_ int* pcxTitle, _Inout_ UINT* pWndWidth)
{
	return dime_ui_cand_widen(displayLength, measuredCx, textLeft, pcxTitle, pWndWidth) ? TRUE : FALSE;
}

BOOL CandNeedsResize(int cxTitle, const RECT& paint, int sbWidth)
{
	return dime_ui_cand_needs_resize(cxTitle, (const DimeRect*)&paint, sbWidth) ? TRUE : FALSE;
}

int CandHitRow(const RECT& client, int padding, int sbWidth, int cyRow, UINT rowsInPage, UINT pageStart, UINT count, POINT pt)
{
	return dime_ui_cand_hit_row((const DimeRect*)&client, padding, sbWidth, cyRow, rowsInPage, pageStart, count, { pt.x, pt.y });
}

RECT CandScrollColumnRect(const RECT& client, int sbWidth)
{
	RECT rc = { 0, 0, 0, 0 };
	dime_ui_cand_mouse_area((const DimeRect*)&client, sbWidth, 0, (DimeRect*)&rc);
	return rc;
}

RECT CandListAreaRect(const RECT& client, int sbWidth)
{
	RECT rc = { 0, 0, 0, 0 };
	dime_ui_cand_mouse_area((const DimeRect*)&client, sbWidth, 1, (DimeRect*)&rc);
	return rc;
}

RECT CandPageIndicatorRect(const RECT& client, int cyRow, int rows, int sbWidth, UINT dpi)
{
	RECT rc = { 0, 0, 0, 0 };
	dime_ui_cand_page_indicator((const DimeRect*)&client, cyRow, rows, sbWidth, dpi, (DimeRect*)&rc);
	return rc;
}

int CandPageIndicatorFontHeight(int lfHeight) { return dime_ui_mul_div(lfHeight, 70, 100); }

ScrollParts ScrollBarParts(const RECT& client, UINT dpi, const ScrollInfo& si)
{
	ScrollParts p = {};
	dime_ui_sb_parts((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, (DimeScrollParts*)&p);
	return p;
}

int ScrollBarHitTest(const RECT& client, UINT dpi, const ScrollInfo& si, POINT pt)
{
	return dime_ui_sb_hit_test((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, { pt.x, pt.y });
}

BOOL ScrollBarIdleThumb(const RECT& client, UINT dpi, const ScrollInfo& si, _Out_ RECT* prc)
{
	*prc = { 0, 0, 0, 0 };
	return dime_ui_sb_idle_thumb((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, (DimeRect*)prc) ? TRUE : FALSE;
}

ScrollArrows ScrollBarArrows(const RECT& client, UINT dpi)
{
	ScrollArrows a = {};
	dime_ui_sb_arrows((const DimeRect*)&client, dpi, (DimeScrollArrows*)&a);
	return a;
}

BOOL ScrollBarDragPos(const RECT& client, UINT dpi, const ScrollInfo& si, int startPos, int deltaY, _Out_ int* pPos)
{
	*pPos = 0;
	return dime_ui_sb_drag_pos((const DimeRect*)&client, dpi, (const DimeScrollInfo*)&si, startPos, deltaY, pPos) ? TRUE : FALSE;
}

int ScrollBarClampPos(int pos, int nMax) { return dime_ui_sb_clamp_pos(pos, nMax); }

RECT ScrollBarAttachedRect(int parentW, int parentH, int sbWidth)
{
	RECT rc = { 0, 0, 0, 0 };
	dime_ui_sb_attached_rect(parentW, parentH, sbWidth, (DimeRect*)&rc);
	return rc;
}
#else
DWORD RectInRect(_In_ const RECT* prcLimit, _In_ const RECT* prcTarget) { return Cpp::RectInRect(prcLimit, prcTarget); }
POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow) { return Cpp::WorkAreaProbePoint(prcTextExtent, prcWindow); }
void CalcFitPointAroundTextExtent(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWorkArea, _In_ const RECT* prcWindow, _Out_ POINT* ppt)
{
	Cpp::CalcFitPointAroundTextExtent(prcTextExtent, prcWorkArea, prcWindow, ppt);
}
BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom)
{
	return Cpp::DecideStackGrowUp(candidateVisible, candidateTop, notifyY, slots, count, screenBottom);
}
POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp)
{
	return Cpp::ComputeStackPosForNewSlot(notifyLocation, slots, count, newSlot, growUp);
}
void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
	_In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed)
{
	Cpp::RestackVisibleSlots(candidateVisible, candidateRect, candidateLocation, notifyLocation, slots, count, growUp, positions, placed);
}

int MulDivInt(int a, int b, int c) { return Cpp::MulDivInt(a, b, c); }
int ScaleForDpi(int value, UINT dpi) { return Cpp::ScaleForDpi(value, dpi); }
void CandInitialMetrics(SIZE measured, UINT wndWidth, _Out_ int* pcxTitle, _Out_ int* pcyRow, _Out_ int* pPadding)
{
	Cpp::CandInitialMetrics(measured, wndWidth, pcxTitle, pcyRow, pPadding);
}
CandMetrics CandMeasureMetrics(SIZE measured, int aveCharWidth, UINT wndWidth) { return Cpp::CandMeasureMetrics(measured, aveCharWidth, wndWidth); }
SIZE CandWindowSize(int cxTitle, int cyRow, int padding, int rows, int sbWidth) { return Cpp::CandWindowSize(cxTitle, cyRow, padding, rows, sbWidth); }
RECT CandScrollBarRect(const RECT& client, int sbWidth, UINT dpi, int cornerRadiusBase) { return Cpp::CandScrollBarRect(client, sbWidth, dpi, cornerRadiusBase); }
CandRow CandRowLayout(const RECT& paint, int indexInPage, const CandMetrics& m, int sbWidth, UINT dpi) { return Cpp::CandRowLayout(paint, indexInPage, m, sbWidth, dpi); }
int CandTruncateItem(int length, _Out_ int* pTailOffset) { return Cpp::CandTruncateItem(length, pTailOffset); }
BOOL CandItemNeedsMeasure(int displayLength, UINT wndWidth) { return Cpp::CandItemNeedsMeasure(displayLength, wndWidth); }
BOOL CandWidenForItem(int displayLength, int measuredCx, int textLeft, _Inout_ int* pcxTitle, _Inout_ UINT* pWndWidth)
{
	return Cpp::CandWidenForItem(displayLength, measuredCx, textLeft, pcxTitle, pWndWidth);
}
BOOL CandNeedsResize(int cxTitle, const RECT& paint, int sbWidth) { return Cpp::CandNeedsResize(cxTitle, paint, sbWidth); }
int CandHitRow(const RECT& client, int padding, int sbWidth, int cyRow, UINT rowsInPage, UINT pageStart, UINT count, POINT pt)
{
	return Cpp::CandHitRow(client, padding, sbWidth, cyRow, rowsInPage, pageStart, count, pt);
}
RECT CandScrollColumnRect(const RECT& client, int sbWidth) { return Cpp::CandScrollColumnRect(client, sbWidth); }
RECT CandListAreaRect(const RECT& client, int sbWidth) { return Cpp::CandListAreaRect(client, sbWidth); }
RECT CandPageIndicatorRect(const RECT& client, int cyRow, int rows, int sbWidth, UINT dpi) { return Cpp::CandPageIndicatorRect(client, cyRow, rows, sbWidth, dpi); }
int CandPageIndicatorFontHeight(int lfHeight) { return Cpp::CandPageIndicatorFontHeight(lfHeight); }
ScrollParts ScrollBarParts(const RECT& client, UINT dpi, const ScrollInfo& si) { return Cpp::ScrollBarParts(client, dpi, si); }
int ScrollBarHitTest(const RECT& client, UINT dpi, const ScrollInfo& si, POINT pt) { return Cpp::ScrollBarHitTest(client, dpi, si, pt); }
BOOL ScrollBarIdleThumb(const RECT& client, UINT dpi, const ScrollInfo& si, _Out_ RECT* prc) { return Cpp::ScrollBarIdleThumb(client, dpi, si, prc); }
ScrollArrows ScrollBarArrows(const RECT& client, UINT dpi) { return Cpp::ScrollBarArrows(client, dpi); }
BOOL ScrollBarDragPos(const RECT& client, UINT dpi, const ScrollInfo& si, int startPos, int deltaY, _Out_ int* pPos)
{
	return Cpp::ScrollBarDragPos(client, dpi, si, startPos, deltaY, pPos);
}
int ScrollBarClampPos(int pos, int nMax) { return Cpp::ScrollBarClampPos(pos, nMax); }
RECT ScrollBarAttachedRect(int parentW, int parentH, int sbWidth) { return Cpp::ScrollBarAttachedRect(parentW, parentH, sbWidth); }
#endif
} // namespace UILayout
