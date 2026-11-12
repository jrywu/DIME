// TestAppData.h — keep DIMETests away from the real %APPDATA%\DIME.
//
// TEST_MODULE_INITIALIZE (TestAppData.cpp) points Global::testAppDataOverride
// at a per-process temp folder for the whole test module, so code that uses
// Global::GetAppDataPath (CConfig, the CLI, engine setup, ...) never reads or
// writes the user's real settings. Tests that set their own override must
// call Restore() when done instead of clearing it — clearing it would send
// the next test to the real %APPDATA%.
#pragma once

namespace DIMETestAppData {

// The module-wide stand-in for %APPDATA% (Global::GetAppDataPath appends \DIME).
const wchar_t* Default();

// Point Global::testAppDataOverride back at Default().
void Restore();

} // namespace DIMETestAppData
