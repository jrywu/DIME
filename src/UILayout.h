// UILayout.h - pure layout calculations of the UI windows.
//
// Window placement around the composition (from CBaseWindow) and the notify
// window stack (from CUIPresenter). The UILayout:: functions use the Rust
// port (rust/crates/dime-ui/src/layout.rs) under DIME_USE_RUST_ENGINE and the
// C++ originals (UILayout::Cpp) otherwise. Parity: UILayoutParityTest.

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

    // The C++ originals.
    namespace Cpp
    {
        DWORD RectInRect(_In_ const RECT* prcLimit, _In_ const RECT* prcTarget);
        POINT WorkAreaProbePoint(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWindow);
        void CalcFitPointAroundTextExtent(_In_ const RECT* prcTextExtent, _In_ const RECT* prcWorkArea, _In_ const RECT* prcWindow, _Out_ POINT* ppt);
        BOOL DecideStackGrowUp(BOOL candidateVisible, LONG candidateTop, LONG notifyY, _In_reads_(count) const NotifySlot* slots, size_t count, int screenBottom);
        POINT ComputeStackPosForNewSlot(POINT notifyLocation, _In_reads_(count) const NotifySlot* slots, size_t count, size_t newSlot, BOOL growUp);
        void RestackVisibleSlots(BOOL candidateVisible, const RECT& candidateRect, POINT candidateLocation, POINT notifyLocation,
            _In_reads_(count) const NotifySlot* slots, size_t count, BOOL growUp, _Out_writes_(count) POINT* positions, _Out_writes_(count) BOOL* placed);
    }
}
