// KeyFlowHarness.h - drive the real CDIME through real TSF with key sequences
//
// Rust migration phase 2a (docs/RUST_MIGRATION_PLAN.md): record what the IME does
// for a key sequence -- eaten or not, committed text, composition, candidate list --
// so the C++ key handling can be refactored (and later ported) with the current
// behaviour pinned down by golden files.
//
// How it works
//   * A dedicated STA thread hosts a real TSF thread manager (CLSID_TF_ThreadMgr)
//     and a document whose context is backed by CKeyFlowTextStore, a small
//     ITextStoreACP holding plain text and one selection.
//   * CDIME is activated with CDIME::_TestActivate (engine, UI presenter and
//     compartments as in ActivateEx, without the system sinks).
//   * Each key goes through OnTestKeyDown / OnKeyDown / OnTestKeyUp / OnKeyUp the way
//     ITfKeystrokeMgr delivers application keys, with the thread keyboard state
//     (Shift, CapsLock) set to match and the US keyboard layout active.
//   * %APPDATA% (Global::testAppDataOverride) and %ProgramW6432% point into a
//     private temp directory, so the user's DIME settings and tables are never used.

#pragma once

#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
#include <functional>

#include "../BaseStructure.h"

class CDIME;

// One key event.
struct KeyFlowKey
{
    UINT vk = 0;
    bool shift = false;
    std::wstring label;      // how it appears in the trace, e.g. "a", "Space", "Shift+1"
};

// Parse a key script: whitespace-separated tokens.
//   a..z 0..9 and punctuation ` - = [ ] \ ; ' , . /   -> that key
//   Space Enter Esc Back Del Tab Left Right Up Down Home End PgUp PgDn  -> named keys
//   Shift+<key>                                                         -> with Shift held
std::vector<KeyFlowKey> ParseKeyScript(const std::wstring& script);

// What the IME looks like after one key.
struct KeyFlowSnapshot
{
    bool eaten = false;
    std::wstring text;           // whole document
    LONG selStart = 0, selEnd = 0;
    bool composing = false;
    std::wstring composition;    // text covered by the active composition
    int candidateMode = 0;       // CANDIDATE_MODE
    std::vector<std::wstring> candidates;
    int candidateSelection = -1;

    std::wstring Format() const;
};

struct KeyFlowOptions
{
    IME_MODE mode = IME_MODE::IME_MODE_DAYI;
    // Files placed in %APPDATA%\DIME before activation (name -> UTF-16 content written with BOM,
    // or a path to copy when the value starts with "@").
    std::vector<std::pair<std::wstring, std::wstring>> appDataFiles;
    // INI lines written to the mode's config file (e.g. L"AutoCompose = 1").
    std::vector<std::wstring> config;
    bool capsLock = false;
};

// Runs a whole scenario on the harness thread and returns one formatted line per key
// ("<key> -> <snapshot>"), preceded by an "init" line.
std::vector<std::wstring> RunKeyFlow(const KeyFlowOptions& options, const std::wstring& script);
