// SettingsPopulate.h — fill the settings controls from the snapshot
//
// Phase 5b-8e of docs/RUST_MIGRATION_PLAN.md. PopulateControls (combo items,
// item data and selection, checkboxes, the max-codes text) was lifted out of
// SettingsWindow.cpp; the C++ original is SettingsPopulate::Cpp::Populate.
// With DIME_USE_RUST_CONFIG, Populate asks dime-settings (populate.rs) for
// the list of control operations and applies them. Change both together.

#pragma once

#include <windows.h>
#include <vector>
#include "SettingsController.h"

namespace SettingsPopulate {

// One entry of the reverse-conversion combo (CConfig's provider list).
struct ReverseEntry {
    const wchar_t* description;
    bool selected;           // its profile GUID is the configured one
};

struct Inputs {
    const SettingsSnapshot* snapshot;
    bool windows1809OrLater;                  // offer 跟隨系統模式
    bool reverseIsNull;                       // configured profile is CLSID_NULL
    const ReverseEntry* reverse;
    int reverseCount;
};

// The control for a SettingsControlId, or null when it is not on the page.
typedef HWND (*FindControlFn)(void* ctx, SettingsControlId id);

// ---- Dispatched (Rust under DIME_USE_RUST_CONFIG) -------------------------
void Populate(const Inputs& in, FindControlFn find, void* ctx);

namespace Cpp {
void Populate(const Inputs& in, FindControlFn find, void* ctx);
} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
void PopulateRust(const Inputs& in, FindControlFn find, void* ctx);
#endif

} // namespace SettingsPopulate
