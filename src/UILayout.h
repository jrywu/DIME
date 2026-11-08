// UILayout.h - pure layout calculations of the UI windows.
//
// Window placement around the composition (from CBaseWindow) and the notify
// window stack (from CUIPresenter); the candidate window and scroll bar layout
// (from CCandidateWindow / CScrollBarWindow). The UILayout:: functions use the
// Rust port (rust/crates/dime-ui/src/layout.rs, candwnd.rs) under
// DIME_USE_RUST_ENGINE and the C++ originals (UILayout::Cpp) otherwise.
// Parity: UILayoutParityTest.

#pragma once

#include <windows.h>

// RECT_* flags of RectInRect.
#define RECT_INSIDE      (0x0001)
#define RECT_OVER_LEFT   (0x0002)
#define RECT_OVER_TOP    (0x0004)
#define RECT_OVER_RIGHT  (0x0008)
#define RECT_OVER_BOTTOM (0x0010)
#define RECT_TOO_WIDE    (0x0020)
#define RECT_TOO_TALL    (0x0040)
#define RECT_ERROR       (0x0080)

namespace UILayout
{
    // One notify window slot; a missing window is not visible and has height 0.
    struct NotifySlot
    {
        int visible;
        int width;
        int height;
    };

    DWORD RectInRect(_In_ const RECT* prcLimit, _In_ const RECT* prcTarget);
    // The point whose monitor work area _GetWindowExtent uses.
    POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow);
    void CalcFitPointAroundTextExtent(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWorkArea, _In_ const RECT* prcWindow, _Out_ POINT* ppt);

    BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom);
    POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp);
    // positions[i] / placed[i] for each slot (placed: visible).
    void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
        _In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed);

    // ---- Candidate window and scroll bar (CCandidateWindow, CScrollBarWindow) ----

    // The row metrics _DrawList derives from the font.
    struct CandMetrics
    {
        int cxLine;           // width of the number column
        int cyLine;           // row height
        int firstLineOffset;  // blank above the first row
        int leftPadding;
        int cxTitle;          // content width (without the scroll bar and the border)
        int padding;          // bottom padding
        int cyOffset;         // text offset inside a row + firstLineOffset
    };

    // One row of _DrawList (paint-rect coordinates; the ExtTextOut origins are relative to the client).
    struct CandRow
    {
        RECT row;        // the whole row
        RECT highlight;  // the rounded selection highlight
        RECT number;     // the number column (also the filler of an empty row)
        RECT text;       // the candidate text clip rectangle
        int cornerRadius;
        int numberX;
        int textX;
        int textY;
    };

    // CScrollInfo.
    struct ScrollInfo
    {
        int nMax;
        int nPage;
        int nPos;
    };

    struct ScrollParts
    {
        RECT up;
        RECT down;
        RECT track;
        RECT thumb;
    };

    // The triangles and the pill thumb of the hovered scroll bar.
    struct ScrollArrows
    {
        POINT up[3];
        POINT down[3];
        int pillLeft;
        int pillRight;
        int pillRadius;
    };

#define UILAYOUT_CANDIDATE_FUNCTIONS \
    int MulDivInt(int a, int b, int c); /* MulDiv */ \
    int ScaleForDpi(int value, UINT dpi); /* MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI) */ \
    /* WM_CREATE: from the extent of wndWidth test characters (CJK). */ \
    void CandInitialMetrics(SIZE measured, UINT wndWidth, _Out_ int* pcxTitle, _Out_ int* pcyRow, _Out_ int* pPadding); \
    /* _DrawList: from the extent of wndWidth test characters and tmAveCharWidth. */ \
    CandMetrics CandMeasureMetrics(SIZE measured, int aveCharWidth, UINT wndWidth); \
    SIZE CandWindowSize(int cxTitle, int cyRow, int padding, int rows, int sbWidth); \
    /* The scroll bar inside the candidate client area, clear of the rounded corners. */ \
    RECT CandScrollBarRect(const RECT& client, int sbWidth, UINT dpi, int cornerRadiusBase); \
    CandRow CandRowLayout(const RECT& paint, int indexInPage, const CandMetrics& m, int sbWidth, UINT dpi); \
    /* Shown length of an item; *pTailOffset is where the last 6 characters start, -1 when not truncated. */ \
    int CandTruncateItem(int length, _Out_ int* pTailOffset); \
    BOOL CandItemNeedsMeasure(int displayLength, UINT wndWidth); \
    /* measuredCx: extent of the whole item. Updates *pcxTitle / *pWndWidth and returns TRUE when it widens. */ \
    BOOL CandWidenForItem(int displayLength, int measuredCx, int textLeft, _Inout_ int* pcxTitle, _Inout_ UINT* pWndWidth); \
    BOOL CandNeedsResize(int cxTitle, const RECT& paint, int sbWidth); \
    /* The row (0-based in the page) under pt, or -1. */ \
    int CandHitRow(const RECT& client, int padding, int sbWidth, int cyRow, UINT rowsInPage, UINT pageStart, UINT count, POINT pt); \
    RECT CandScrollColumnRect(const RECT& client, int sbWidth); \
    RECT CandListAreaRect(const RECT& client, int sbWidth); \
    RECT CandPageIndicatorRect(const RECT& client, int cyRow, int rows, int sbWidth, UINT dpi); \
    int CandPageIndicatorFontHeight(int lfHeight); \
    ScrollParts ScrollBarParts(const RECT& client, UINT dpi, const ScrollInfo& si); \
    int ScrollBarHitTest(const RECT& client, UINT dpi, const ScrollInfo& si, POINT pt); /* SCROLLBAR_HITTEST */ \
    /* The idle thin-line thumb; FALSE for an empty client. */ \
    BOOL ScrollBarIdleThumb(const RECT& client, UINT dpi, const ScrollInfo& si, _Out_ RECT* prc); \
    ScrollArrows ScrollBarArrows(const RECT& client, UINT dpi); \
    /* The position while dragging the thumb; FALSE when the thumb cannot slide. */ \
    BOOL ScrollBarDragPos(const RECT& client, UINT dpi, const ScrollInfo& si, int startPos, int deltaY, _Out_ int* pPos); \
    int ScrollBarClampPos(int pos, int nMax); \
    RECT ScrollBarAttachedRect(int parentW, int parentH, int sbWidth);

    UILAYOUT_CANDIDATE_FUNCTIONS

    // The C++ originals.
    namespace Cpp
    {
        UILAYOUT_CANDIDATE_FUNCTIONS

        DWORD RectInRect(_In_ const RECT* prcLimit, _In_ const RECT* prcTarget);
        POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow);
        void CalcFitPointAroundTextExtent(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWorkArea, _In_ const RECT* prcWindow, _Out_ POINT* ppt);
        BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom);
        POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp);
        void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
            _In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed);
    }
}
