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
#include "../BaseWindow.h"
#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

// Tear a test window down as production does (UIPresenter: _Destroy, then delete), so no
// HWND, visible window or timer of it outlives the test (KeyFlow tests pump messages later).
template <class W>
inline void DestroyAndDelete(W*& w)
{
    if (w)
    {
        w->_Destroy();
        delete w;
        w = nullptr;
    }
}

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

#ifdef DIME_RUST_AVAILABLE
// A window class registered by one implementation (CBaseWindow's window procedure or dime-ui's),
// whatever the build registered for the real classes; child / shadow class styles.
inline ATOM TestClassAtomKind(const wchar_t* name, bool rust, bool popup)
{
    EnsureWindowClassesRegistered();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    ATOM atom = (ATOM)GetClassInfoExW(Global::dllInstanceHandle, name, &wc);
    if (atom) return atom;
    if (rust) return dime_ui_register_window_class(Global::dllInstanceHandle, (const uint16_t*)name, popup ? 1 : 0);
    if (popup) CBaseWindow::_InitPopupWindowClass(name, &atom);
    else CBaseWindow::_InitWindowClass(name, &atom);
    return atom;
}
inline ATOM TestClassAtom(const wchar_t* name, bool rust) { return TestClassAtomKind(name, rust, false); }
inline ATOM CppScrollBarTestAtom() { return TestClassAtom(L"DIME.Test.CppScrollBarWindow", false); }
inline ATOM RustScrollBarTestAtom() { return TestClassAtom(L"DIME.Test.RustScrollBarWindow", true); }
#endif
