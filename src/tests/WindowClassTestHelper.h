// WindowClassTestHelper.h - register DIME's window classes once per test process.
//
// Global::RegisterWindowClass() fails when a class is already registered and then
// leaves that class's atom at 0; CBaseWindow::_Create with atom 0 silently creates
// a "virtual" window without an HWND. Tests that need real windows call
// EnsureWindowClassesRegistered(), which recovers the atoms of classes that are
// already registered.
#pragma once

#include <Windows.h>
#include "../Globals.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

inline void EnsureWindowClassesRegistered()
{
    if (!Global::dllInstanceHandle)
        Global::dllInstanceHandle = reinterpret_cast<HINSTANCE>(&__ImageBase);
    Global::RegisterWindowClass();

    struct ClassAtom { const wchar_t* name; ATOM* atom; };
    const ClassAtom classes[] = {
        { L"DIME.CandidateWindow", &Global::AtomCandidateWindow },
        { L"DIME.CandidateShadowWindow", &Global::AtomCandidateShadowWindow },
        { L"DIME.CandidateScrollBarWindow", &Global::AtomCandidateScrollBarWindow },
        { L"DIME.NotifyWindow", &Global::AtomNotifyWindow },
        { L"DIME.NotifyShadowWindow", &Global::AtomNotifyShadowWindow },
    };
    for (const ClassAtom& c : classes)
    {
        if (*c.atom != 0) continue;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        *c.atom = static_cast<ATOM>(GetClassInfoExW(Global::dllInstanceHandle, c.name, &wc));
    }
}
