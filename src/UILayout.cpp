// UILayout.cpp - see UILayout.h

#include "Private.h"
#include "Globals.h"
#include "Define.h"
#include "UILayout.h"
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
#endif
} // namespace UILayout
