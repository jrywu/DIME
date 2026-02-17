// TestStubs.cpp - Stub implementations for unit testing
// Provides minimal implementations of DIME symbols needed by Config.cpp and related files

#include "pch.h"
#include "DIME.h"
#include "BaseWindow.h"
#include "Globals.h"

// Stub implementations for DLL reference counting
// These are normally in DllMain.cpp but we exclude that from tests
void DllAddRef(void)
{
    // No-op for testing
}

void DllRelease(void)
{
    // No-op for testing
}

// Stub for CDIME::_dwActivateFlags
// This is a private static member that Config.cpp references
// Commenting out since DIME.cpp is now included
// unsigned long CDIME::_dwActivateFlags = 0;

// Initialize Global namespace variables that might be accessed
namespace Global {
    // These are already declared in Globals.cpp, but we ensure they're initialized for tests
    // The actual definitions are in Globals.cpp which is linked
}

// Stub for CBaseWindow::_InitWindowClass
// Config.cpp calls this through Global::RegisterWindowClass() in Globals.cpp
// This is called multiple times for different window classes
// Commenting out since BaseWindow.cpp is now included
/*
int CBaseWindow::_InitWindowClass(const WCHAR* windowClass, ATOM* atom)
{
    // Set a dummy ATOM value for testing
    if (atom != nullptr)
    {
        static ATOM dummyAtom = 0x1000;
        *atom = dummyAtom++;
    }
    // Return success for testing purposes
    return S_OK;
}
*/
