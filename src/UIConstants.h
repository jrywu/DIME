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

#pragma once

//---------------------------------------------------------------------
// UI Layout Constants for DIME Input Method Framework
//
// These are baseline values defined at 96 DPI. The actual rendered
// sizes are scaled at runtime based on the system DPI setting to
// provide proper high-DPI support on 2K/4K displays.
//
// Note: The DPI scaling is already implemented throughout the codebase.
//       These constants are centralized here for maintainability only.
//---------------------------------------------------------------------

namespace UI {
    //---------------------------------------------------------------------
    // Candidate Window Layout
    //---------------------------------------------------------------------
    constexpr int CANDIDATE_ROW_WIDTH           = 30;    // Base row height
    constexpr int CANDIDATE_BORDER_WIDTH        = 1;     // Border width
    constexpr int CANDIDATE_ITEM_PADDING        = 2;     // Padding around items
    constexpr int CANDIDATE_MARGIN              = 4;     // Window margins

    //---------------------------------------------------------------------
    // Notify Window Layout
    //---------------------------------------------------------------------
    constexpr int NOTIFY_BORDER_WIDTH           = 1;     // Border width
    constexpr int NOTIFY_TEXT_PADDING           = 4;     // Text padding

    //---------------------------------------------------------------------
    // Composition Window Layout
    //---------------------------------------------------------------------
    constexpr int COMPOSITION_BORDER_WIDTH      = 1;     // Border width
    constexpr int COMPOSITION_TEXT_PADDING      = 4;     // Text padding

    //---------------------------------------------------------------------
    // Scrollbar Layout
    //---------------------------------------------------------------------
    constexpr int SCROLLBAR_WIDTH               = 16;    // Scrollbar width
    constexpr int SCROLLBAR_BUTTON_HEIGHT       = 16;    // Up/down button height
    constexpr int SCROLLBAR_THUMB_MIN_HEIGHT    = 8;     // Minimum thumb height

    //---------------------------------------------------------------------
    // Shadow Window Constants
    //---------------------------------------------------------------------
    constexpr int SHADOW_WIDTH                  = 5;     // Shadow width in pixels
    constexpr int SHADOW_ALPHA_LEVELS           = 5;     // Number of alpha gradient levels
    constexpr int SHADOW_OFFSET_X               = 2;     // Shadow offset horizontal
    constexpr int SHADOW_OFFSET_Y               = 2;     // Shadow offset vertical

    //---------------------------------------------------------------------
    // Font Size Limits
    //---------------------------------------------------------------------
    constexpr int DEFAULT_FONT_SIZE             = 12;    // Default font size (points)
    constexpr int MIN_FONT_SIZE                 = 8;     // Minimum font size (points)
    constexpr int MAX_FONT_SIZE                 = 72;    // Maximum font size (points)

    //---------------------------------------------------------------------
    // Animation Constants
    //---------------------------------------------------------------------
    constexpr int ANIMATION_STEP_TIME_MS        = 8;     // CandidateWindow animation step duration (milliseconds)
    constexpr int NOTIFY_ANIMATION_STEP_TIME_MS = 15;    // NotifyWindow animation step duration (milliseconds) - intentionally slower for different UX
    constexpr int ANIMATION_ALPHA_START         = 5;     // Starting alpha percentage (5%)
    constexpr int ANIMATION_ALPHA_END           = 100;   // Ending alpha percentage (100%)

    //---------------------------------------------------------------------
    // Timer IDs (must be unique across the application)
    //---------------------------------------------------------------------
    constexpr UINT_PTR ANIMATION_TIMER_ID       = 39773; // Animation fade-in timer
    constexpr UINT_PTR DELAY_SHOW_TIMER_ID      = 39775; // Delayed window show timer
    constexpr UINT_PTR TIME_TO_HIDE_TIMER_ID    = 39776; // Auto-hide timer

    //---------------------------------------------------------------------
    // Window Positioning
    //---------------------------------------------------------------------
    constexpr int DEFAULT_WINDOW_X              = -32768; // Off-screen initial X position
    constexpr int DEFAULT_WINDOW_Y              = -32768; // Off-screen initial Y position

} // namespace UI


