// DpiScalingTest.cpp
// Unit tests for DPI scaling helper and font size conversion math.
// These tests are pure in-process logic tests; they do NOT require a display
// or window handle.

#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{

// ---------------------------------------------------------------------------
// ScaleForDpi helper tests
// ---------------------------------------------------------------------------
TEST_CLASS(DpiHelperTests)
{
public:
    TEST_METHOD(ScaleForDpi_At96Dpi_ReturnsOriginal)
    {
        Assert::AreEqual(16, ScaleForDpi(16, 96));
    }

    TEST_METHOD(ScaleForDpi_At120Dpi_Scales125Percent)
    {
        // 120/96 = 1.25x
        Assert::AreEqual(20, ScaleForDpi(16, 120));
    }

    TEST_METHOD(ScaleForDpi_At144Dpi_Scales150Percent)
    {
        // 144/96 = 1.5x
        Assert::AreEqual(24, ScaleForDpi(16, 144));
    }

    TEST_METHOD(ScaleForDpi_At192Dpi_Scales200Percent)
    {
        // 192/96 = 2.0x
        Assert::AreEqual(32, ScaleForDpi(16, 192));
    }

    TEST_METHOD(ScaleForDpi_ZeroDpi_ReturnsZero)
    {
        Assert::AreEqual(0, ScaleForDpi(16, 0));
    }

    TEST_METHOD(ScaleForDpi_ZeroBase_ReturnsZero)
    {
        Assert::AreEqual(0, ScaleForDpi(0, 144));
    }

    TEST_METHOD(ScaleForDpi_OddValues_MatchesMulDiv)
    {
        // Verify ScaleForDpi matches MulDiv rounding for non-round values
        Assert::AreEqual(MulDiv(5, 144, 96), ScaleForDpi(5, 144));
        Assert::AreEqual(MulDiv(7, 120, 96), ScaleForDpi(7, 120));
        Assert::AreEqual(MulDiv(3, 192, 96), ScaleForDpi(3, 192));
    }

    TEST_METHOD(ScaleForDpi_LargeValue_Scales)
    {
        // Shadow width 5 at 200% = 10
        Assert::AreEqual(10, ScaleForDpi(5, 192));
    }
};

// ---------------------------------------------------------------------------
// Font point-to-pixel conversion tests
// Validates the MulDiv(fontSize, dpi, 72) formula used throughout DIME.
// ---------------------------------------------------------------------------
TEST_CLASS(FontDpiConversionTests)
{
public:
    TEST_METHOD(PointToPixel_12pt_At96Dpi)
    {
        // 12 * 96 / 72 = 16
        Assert::AreEqual(16, MulDiv(12, 96, 72));
    }

    TEST_METHOD(PointToPixel_12pt_At120Dpi)
    {
        // 12 * 120 / 72 = 20
        Assert::AreEqual(20, MulDiv(12, 120, 72));
    }

    TEST_METHOD(PointToPixel_12pt_At144Dpi)
    {
        // 12 * 144 / 72 = 24
        Assert::AreEqual(24, MulDiv(12, 144, 72));
    }

    TEST_METHOD(PointToPixel_10pt_At96Dpi)
    {
        // 10 * 96 / 72 = 13.33 -> MulDiv rounds to 13
        Assert::AreEqual(13, MulDiv(10, 96, 72));
    }

    TEST_METHOD(PointToPixel_10pt_At120Dpi)
    {
        // 10 * 120 / 72 = 16.67 -> MulDiv rounds to 17
        Assert::AreEqual(17, MulDiv(10, 120, 72));
    }

    TEST_METHOD(TwipsConversion_16px_At96Dpi)
    {
        // RichEdit twips: MulDiv(abs(lfHeight), 72 * 20, dpi)
        // 16 * 1440 / 96 = 240 twips = 12pt
        Assert::AreEqual(240, MulDiv(16, 1440, 96));
    }

    TEST_METHOD(TwipsConversion_20px_At120Dpi)
    {
        // 20 * 1440 / 120 = 240 twips = 12pt (same point size at different DPI)
        Assert::AreEqual(240, MulDiv(20, 1440, 120));
    }

    TEST_METHOD(TwipsConversion_24px_At144Dpi)
    {
        // 24 * 1440 / 144 = 240 twips = 12pt
        Assert::AreEqual(240, MulDiv(24, 1440, 144));
    }
};

} // namespace DIMEUnitTests
