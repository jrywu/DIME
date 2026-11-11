// SettingsGeometry.cpp — Geometry constants + theme color derivation

#include "SettingsGeometry.h"
#include "..\Define.h"
#include <cstddef>
#ifdef DIME_USE_RUST_CONFIG
#include "dime_ffi.h"
#endif

// SettingsGeometry / ThemeColors must match dime-config::geometry.
static_assert(sizeof(SettingsGeometry) == 260, "SettingsGeometry size");
static_assert(alignof(SettingsGeometry) == 4, "SettingsGeometry align");
static_assert(offsetof(SettingsGeometry, sidebarWidth) == 0, "sidebarWidth");
static_assert(offsetof(SettingsGeometry, windowDefaultH) == 256, "windowDefaultH");
static_assert(sizeof(ThemeColors) == 88, "ThemeColors size");
static_assert(alignof(ThemeColors) == 4, "ThemeColors align");
static_assert(offsetof(ThemeColors, accent) == 40, "accent");
static_assert(offsetof(ThemeColors, buttonTextDisabled) == 84, "buttonTextDisabled");
#ifdef DIME_USE_RUST_CONFIG
static_assert(sizeof(DimeSysColors) == 24, "DimeSysColors size");
#endif

#pragma comment(lib, "dwmapi.lib")

#ifdef DIME_USE_RUST_CONFIG
namespace {
SettingsGeometry LoadRustGeometry()
{
    SettingsGeometry geo = {};
    dime_settings_geometry(&geo);
    return geo;
}
}
const SettingsGeometry g_geo = LoadRustGeometry();
#else
// Single global geometry instance (uses default member initializers)
const SettingsGeometry g_geo = {};
#endif

// ============================================================================
// Blend two colors
// ============================================================================
COLORREF BlendColor(COLORREF c1, COLORREF c2, int alpha)
{
#ifdef DIME_USE_RUST_CONFIG
    return dime_settings_blend_color(c1, c2, alpha);
#else
    if (alpha <= 0) return c1;
    if (alpha >= 255) return c2;
    int r = (GetRValue(c1) * (255 - alpha) + GetRValue(c2) * alpha) / 255;
    int g = (GetGValue(c1) * (255 - alpha) + GetGValue(c2) * alpha) / 255;
    int b = (GetBValue(c1) * (255 - alpha) + GetBValue(c2) * alpha) / 255;
    return RGB(r, g, b);
#endif
}

#ifndef DIME_USE_RUST_CONFIG
// ============================================================================
// Get system accent color
// ============================================================================
static COLORREF GetAccentColor()
{
    // COLOR_HOTLIGHT is the system hyperlink/accent color used in Win11 UI text (e.g. links, toggles).
    // DwmGetColorizationColor gives the window frame tint — too dark/muted for UI text.
    return GetSysColor(COLOR_HOTLIGHT);
}
#endif

// ============================================================================
// Compute theme colors from system APIs
// ============================================================================
ThemeColors ComputeThemeColors(bool isDark)
{
#ifdef DIME_USE_RUST_CONFIG
    DimeSysColors sys = {};
    sys.hotlight = GetSysColor(COLOR_HOTLIGHT);
    sys.face = GetSysColor(COLOR_3DFACE);
    sys.window = GetSysColor(COLOR_WINDOW);
    sys.window_text = GetSysColor(COLOR_WINDOWTEXT);
    sys.gray_text = GetSysColor(COLOR_GRAYTEXT);
    sys.highlight_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
    ThemeColors tc = {};
    dime_settings_theme_colors(isDark ? 1 : 0, &sys, &tc);
    return tc;
#else
    ThemeColors tc = {};
    COLORREF accent = GetAccentColor();

    if (isDark) {
        // Dark mode: derive from DIME dark-mode base constants + blends
        COLORREF white = RGB(255, 255, 255);
        tc.contentBg      = DARK_DIALOG_BG;
        tc.cardBg         = DARK_CONTROL_BG;
        tc.cardBorder     = BlendColor(DARK_CONTROL_BG, RGB(0,0,0), 40);  // dark border mimics shadow
        tc.textPrimary    = BlendColor(DARK_DIALOG_BG, white, 235);     // near-white
        tc.textSecondary  = BlendColor(DARK_DIALOG_BG, white, 157);     // medium gray
        tc.rowSeparator   = tc.cardBorder;
        tc.rowHover       = BlendColor(DARK_CONTROL_BG, white, 15);
        tc.sidebarBg      = DARK_DIALOG_BG;
        tc.sidebarHover   = BlendColor(DARK_DIALOG_BG, white, 15);
        tc.sidebarSelect  = BlendColor(DARK_DIALOG_BG, white, 18);
        tc.accent         = accent;
        tc.toggleOnTrack  = accent;
        tc.toggleOffBorder= BlendColor(DARK_DIALOG_BG, white, 140);
        tc.toggleOnKnob   = DARK_DIALOG_BG;
        tc.toggleOffKnob  = BlendColor(DARK_DIALOG_BG, white, 180);
        // Buttons: Win11 primary button — accent fill, white text (dark mode same)
        tc.buttonFill         = accent;
        tc.buttonFillHover    = BlendColor(accent, white, 30);
        tc.buttonFillPressed  = BlendColor(accent, DARK_DIALOG_BG, 55);
        tc.buttonFillDisabled = BlendColor(accent, DARK_DIALOG_BG, 160);
        tc.buttonBorder       = BlendColor(accent, DARK_DIALOG_BG, 30);
        tc.buttonText         = BlendColor(DARK_DIALOG_BG, white, 255); // white
        tc.buttonTextDisabled = tc.textSecondary;
    } else {
        // Light mode: all colors derived from system APIs
        COLORREF face   = GetSysColor(COLOR_3DFACE);   // ≈ RGB(240,240,240) on Win11
        COLORREF window = GetSysColor(COLOR_WINDOW);   // ≈ RGB(255,255,255)
        COLORREF black  = RGB(0, 0, 0);

        tc.contentBg      = face;
        tc.cardBg         = BlendColor(window, face, 30);               // slightly off-white like Win11
        tc.cardBorder     = BlendColor(face, black, 3);  // Win11: nearly invisible border
        tc.textPrimary    = GetSysColor(COLOR_WINDOWTEXT);
        tc.textSecondary  = GetSysColor(COLOR_GRAYTEXT);
        tc.rowSeparator   = tc.cardBorder;
        tc.rowHover       = BlendColor(window, black, 10);                // subtle card hover
        tc.sidebarBg      = face;
        tc.sidebarHover   = BlendColor(face, black, 20);                // darker sidebar hover
        tc.sidebarSelect  = BlendColor(face, black, 25);               // darker sidebar selected
        tc.accent         = accent;
        tc.toggleOnTrack  = accent;
        tc.toggleOffBorder= GetSysColor(COLOR_GRAYTEXT);
        tc.toggleOnKnob   = GetSysColor(COLOR_HIGHLIGHTTEXT);
        tc.toggleOffKnob  = BlendColor(GetSysColor(COLOR_WINDOWTEXT), window, 170);
        // Buttons: Win11 primary button — accent fill, white text
        COLORREF hotlight = GetSysColor(COLOR_HOTLIGHT);
        tc.buttonFill         = hotlight;
        tc.buttonFillHover    = BlendColor(hotlight, black, 30);
        tc.buttonFillPressed  = BlendColor(hotlight, black, 55);
        tc.buttonFillDisabled = BlendColor(hotlight, face, 160);
        tc.buttonBorder       = BlendColor(hotlight, black, 30);
        tc.buttonText         = GetSysColor(COLOR_HIGHLIGHTTEXT);       // white
        tc.buttonTextDisabled = GetSysColor(COLOR_GRAYTEXT);
    }

    return tc;
#endif
}
