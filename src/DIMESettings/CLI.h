// CLI.h — Headless CLI interface for DIMESettings.exe
//
// RunCLI(cmdLine, out) parses cmdLine and executes the command, writing output
// to 'out' (defaults to stdout).
//
// Exit codes: 0 = success, 1 = invalid arg/key/value,
//             2 = I/O error,
//             3 = command-specific semantic error
//                 (--get/--set: key not applicable to mode;
//                  --import-custom: format validation failure).

#pragma once

#include <windows.h>
#include <stdio.h>
#include "..\define.h"
#include "..\BaseStructure.h"

// ---------------------------------------------------------------------------
// CLI command identifiers
// ---------------------------------------------------------------------------
enum CLI_COMMAND
{
    CLI_NONE = 0,
    CLI_GET,
    CLI_GET_ALL,
    CLI_SET,
    CLI_RESET,
    CLI_IMPORT_CUSTOM,
    CLI_EXPORT_CUSTOM,
    CLI_LOAD_MAIN,
    CLI_LOAD_PHRASE,
    CLI_LOAD_ARRAY,
    CLI_LIST_MODES,
    CLI_LIST_KEYS,
    CLI_HELP,
};

// Array supplementary table identifiers (for --load-array)
enum CLI_ARRAY_TABLE
{
    CLI_TABLE_NONE   = -1,
    CLI_TABLE_SP     = 0,  // Array Standard Phonetic  (Array-special.cin)
    CLI_TABLE_SC     = 1,  // Array Simplified Chinese  (Array-shortcode.cin)
    CLI_TABLE_EXT_B  = 2,  // Array Extension B         (Array-Ext-B.cin)
    CLI_TABLE_EXT_CD = 3,  // Array Extensions C+D      (Array-Ext-CD.cin)
    CLI_TABLE_EXT_EFG= 4,  // Array Extensions E+F+G    (Array-Ext-EF.cin)
    CLI_TABLE_ARRAY40= 5,  // Array 40                  (Array40.cin)
    CLI_TABLE_PHRASE = 6,  // Array Phrase table        (Array-Phrase.cin)
};

// One --set key value pair
struct CLISetPair
{
    WCHAR key[64];
    WCHAR value[256];
};

#define CLI_MAX_SET_PAIRS 32

// Parsed CLI arguments
struct CLIArgs
{
    CLI_COMMAND     command;
    IME_MODE        mode;
    BOOL            hasMode;
    BOOL            jsonOutput;
    BOOL            silent;
    BOOL            noValidate;     // --no-validate: skip pre-flight format check on --import-custom
    WCHAR           getKey[64];
    CLISetPair      setPairs[CLI_MAX_SET_PAIRS];
    int             setCount;
    WCHAR           filePath[MAX_PATH];
    CLI_ARRAY_TABLE arrayTable;
};

// ---------------------------------------------------------------------------
// Key type and descriptor (also used by unit tests)
// ---------------------------------------------------------------------------
enum KeyType { BOOL_T, INT_T, COLOR_T, STRING_T, CLSID_T };

static const int CLI_MASK_DAYI      = 0x1;
static const int CLI_MASK_ARRAY     = 0x2;
static const int CLI_MASK_PHONETIC  = 0x4;
static const int CLI_MASK_GENERIC   = 0x8;
static const int CLI_MASK_ALL       = 0xF;
static const int CLI_MASK_NOT_ARRAY = CLI_MASK_DAYI | CLI_MASK_PHONETIC | CLI_MASK_GENERIC;

struct KeyInfo
{
    const wchar_t* name;
    KeyType        type;
    int            minVal;  // INT_T / BOOL_T: inclusive minimum
    int            maxVal;  // INT_T / BOOL_T: inclusive maximum
    int            modeMask;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Parse cmdLine into args.  Errors are written to 'err' (default stderr).
// Returns true on success, false if the command line is syntactically invalid.
bool ParseCLIArgs(_In_ const wchar_t* cmdLine, _Out_ CLIArgs& args,
                  _In_ FILE* err = stderr);

// Execute the command encoded in cmdLine.
// Output (key=value lines, JSON, etc.) is written to 'out' (default stdout).
// Returns 0/1/2/3 as described in the header comment.
int RunCLI(_In_ const wchar_t* cmdLine, _In_ FILE* out = stdout);

// ---------------------------------------------------------------------------
// Internal helpers exposed for unit testing
// ---------------------------------------------------------------------------
#ifdef DIME_UNIT_TESTING
const KeyInfo* CLI_FindKey(const wchar_t* name);
bool CLI_KeyApplicableToMode(const KeyInfo* ki, IME_MODE mode);
bool CLI_ParseColorValue(const wchar_t* str, COLORREF& out);
int  CLI_GetKeyCount();
const KeyInfo* CLI_GetKeyRegistry();
#endif
