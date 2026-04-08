# Fix: CHN/ENG Notify Popup Shown on Non-Input Windows

## Problem

When `ShowNotifyDesktop` is enabled, the CHN/ENG notification popup ("中文"/"英數") appears every time **any** window or dialog receives focus — even windows/dialogs with no input fields (e.g., About dialogs, message boxes).

## Root Cause

In `src/ThreadMgrEventSink.cpp`, the `OnSetFocus()` flow at lines 94-117:

1. `_UpdateLanguageBarOnSetFocus(pDocMgrFocus)` only checks if a TSF context *exists* — even non-editable dialogs have TSF contexts, so this passes for nearly all windows.
2. `showChnEngNotify(_isChinese, 500)` at line 115 is called with no editability check.
3. `showChnEngNotify()` in `src/LanguageBar.cpp:1158-1164` only checks `GetShowNotifyDesktop()` — no further filtering.

There is already diagnostic code at lines 125-146 that queries `TS_SS_TRANSITORY` from the context's `TF_STATUS`, but it is gated behind `#if defined(DEBUG_PRINT) && defined(VERBOSE)` and only prints debug output — it does not control the notification.

## Fix

In `src/ThreadMgrEventSink.cpp`, inside the `OnSetFocus()` block (lines 106-117), query the context's `TF_STATUS` before calling `showChnEngNotify()`. Skip the notification when the context is read-only or transitory.

### Before

```cpp
if (pContext && !(_newlyActivated && !Global::isWindows8 && isWin7IEProcess()))
{
    debugPrint(L"CDIME::OnSetFocus() Set isChinese = lastkeyboardMode = %d,", _isChinese);
    _lastKeyboardMode = _isChinese;
    debugPrint(L"CDIME::OnSetFocus() Set keyboard mode to last state = %d", _lastKeyboardMode);
    ConversionModeCompartmentUpdated(_pThreadMgr, &_lastKeyboardMode);
    debugPrint(L"CDIME::OnSetFocus() show chi/eng notify _isChinese = %d", _isChinese);
    if (!isBlackListedProcessForProbeComposition())
        showChnEngNotify(_isChinese, 500);
}
```

### After

```cpp
if (pContext && !(_newlyActivated && !Global::isWindows8 && isWin7IEProcess()))
{
    debugPrint(L"CDIME::OnSetFocus() Set isChinese = lastkeyboardMode = %d,", _isChinese);
    _lastKeyboardMode = _isChinese;
    debugPrint(L"CDIME::OnSetFocus() Set keyboard mode to last state = %d", _lastKeyboardMode);
    ConversionModeCompartmentUpdated(_pThreadMgr, &_lastKeyboardMode);
    debugPrint(L"CDIME::OnSetFocus() show chi/eng notify _isChinese = %d", _isChinese);

    // Skip notification for read-only or transitory contexts (dialogs without input fields, tooltips, menus)
    BOOL skipNotify = FALSE;
    TF_STATUS tfStatus;
    if (SUCCEEDED(pContext->GetStatus(&tfStatus)))
    {
        if ((tfStatus.dwDynamicFlags & TF_SD_READONLY) ||
            (tfStatus.dwStaticFlags & TS_SS_TRANSITORY))
        {
            skipNotify = TRUE;
            debugPrint(L"CDIME::OnSetFocus() skip notify: readonly=%d, transitory=%d",
                (tfStatus.dwDynamicFlags & TF_SD_READONLY) != 0,
                (tfStatus.dwStaticFlags & TS_SS_TRANSITORY) != 0);
        }
    }

    if (!skipNotify && !isBlackListedProcessForProbeComposition())
        showChnEngNotify(_isChinese, 500);
}
```

### Flags used

| Flag | Source | Meaning |
|------|--------|---------|
| `TF_SD_READONLY` | `tfStatus.dwDynamicFlags` | Context does not accept text input |
| `TS_SS_TRANSITORY` | `tfStatus.dwStaticFlags` | Transitory system-managed context (tooltips, menus) |

## Files to Modify

- `src/ThreadMgrEventSink.cpp` — add status check before `showChnEngNotify()` call (around line 114)

## Verification

1. Build the project
2. Enable `ShowNotifyDesktop` in settings
3. Click on a text editor (Notepad, etc.) — notification **should** appear
4. Click on a dialog with no input field (About dialog, message box) — notification should **not** appear
5. Click on a read-only text view — notification should **not** appear
6. Switch between editable apps — notification should still appear normally

## Caveat

Some apps may not properly report `TF_SD_READONLY`. If this causes false negatives (notifications suppressed where they should show), we can fall back to checking only `TS_SS_TRANSITORY`, which is more reliably set by the system for non-input contexts.
