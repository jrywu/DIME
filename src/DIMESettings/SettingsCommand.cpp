// SettingsCommand.cpp — see SettingsCommand.h. Cpp::Handle is the WM_COMMAND
// logic that used to live in SettingsWindow::ContentWndProc, with every side
// effect routed through Host. dime-settings (command.rs) mirrors it.

#include <windows.h>
#include <strsafe.h>
#include "..\BaseStructure.h"
#include "SettingsCommand.h"
#include "SettingsPageLayout.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#include <cstddef>
#endif

namespace SettingsCommand {

static_assert(sizeof(CommandEvent) == 52, "CommandEvent must match dime-settings");

namespace Cpp {

namespace {

const int MAX_REPORT = 10;

RowAction FindAction(SettingsControlId btnId)
{
    RowAction action = RowAction::None;
    auto findAction = [&](const LayoutRow* rows, int count) {
        for (int i = 0; i < count; i++)
            if (rows[i].id == btnId) { action = rows[i].action; return; }
    };
    int layoutCount = 0;
    const LayoutCard* cards = GetIMEPageLayout(&layoutCount);
    for (int ci = 0; ci < layoutCount && action == RowAction::None; ci++)
        if (cards[ci].rows) findAction(cards[ci].rows, cards[ci].rowCount);
    if (action == RowAction::None) {
        int ltCount = 0;
        const LayoutCard* ltCards = GetLoadTablePageLayout(&ltCount);
        for (int ci = 0; ci < ltCount && action == RowAction::None; ci++)
            if (ltCards[ci].rows) findAction(ltCards[ci].rows, ltCards[ci].rowCount);
    }
    // Programmatically created button not in the layout tree
    if (action == RowAction::None && btnId == CTRL_CUSTOM_TABLE_SAVE)
        action = RowAction::SaveCustomTable;
    return action;
}

void JumpToLine(Host& host, int firstErrorLine)
{
    LONG cp = (LONG)host.EditorLineIndex(firstErrorLine - 1);
    if (cp >= 0) host.EditorFocusAt(cp);
}

void FontDialog(Host& host, SettingsSnapshot& s)
{
    int lpY = host.LogPixelsY();
    LOGFONTW lf = {};
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, s.fontFaceName);
    lf.lfHeight = -MulDiv(s.fontSize, lpY, 72);
    lf.lfWeight = s.fontWeight;
    lf.lfItalic = (BYTE)s.fontItalic;
    lf.lfCharSet = DEFAULT_CHARSET;
    int pointSize10 = 0;
    if (host.PickFont(lf, pointSize10)) {
        StringCchCopyW(s.fontFaceName, LF_FACESIZE, lf.lfFaceName);
        s.fontSize = pointSize10 / 10;
        s.fontWeight = lf.lfWeight;
        s.fontItalic = lf.lfItalic;
        host.ApplyAndSave();
        host.Rebuild();
        host.InvalidateContent(false);
        host.UpdateContent();
    }
}

void ResetDefaults(Host& host, SettingsSnapshot& s, int mode)
{
    host.ResetAllDefaults(mode);
    host.WriteConfig(mode);
    host.LoadConfig(mode);
    s = host.LoadSnapshot();
    host.Rebuild();
    host.InvalidateContent(false);
    host.UpdateContent();
}

void LoadTable(Host& host, SettingsControlId btnId)
{
    const wchar_t* targetSuffix = SettingsModel::GetLoadTableSuffix(btnId);
    if (!targetSuffix) return;
    std::wstring pathToLoad;
    if (!host.OpenFileDialog(0, pathToLoad)) return;
    std::wstring appData = host.AppDataPath();
    WCHAR pathToWrite[MAX_PATH] = {};
    StringCchPrintfW(pathToWrite, MAX_PATH, L"%s%s", appData.c_str(), targetSuffix);
    if (host.ParseCin(pathToLoad, pathToWrite, false))
        host.ShowMessage(L"自訂碼表載入完成。", L"DIME 自訂碼表", MB_ICONINFORMATION);
    else
        host.ShowMessage(L"自訂碼表載入發生錯誤 !!", L"DIME 自訂碼表", MB_ICONERROR);
}

void SaveCustomTable(Host& host, int mode)
{
    if (!host.EditorExists()) return;
    std::wstring text = host.EditorText();
    int firstErrorLine = 0;
    std::wstring errSummary;
    int errCount = host.ValidateBuffer(text, MAX_REPORT, firstErrorLine, errSummary);
    if (errCount > 0) {
        wchar_t msg[2048];
        StringCchPrintfW(msg, _countof(msg),
            L"儲存失敗：偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s%s按確定後將跳到第一個錯誤行。",
            errCount, firstErrorLine, errSummary.c_str(),
            (errCount > MAX_REPORT) ? L"  ...(更多錯誤已省略)\n" : L"");
        host.ShowMessage(msg, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
        if (firstErrorLine > 0) JumpToLine(host, firstErrorLine);
        return;
    }
    std::wstring txtPath = host.CustomTableTxtPath(mode);
    if (host.WriteBomFile(txtPath, text)) {
        std::wstring cinPath = host.CustomTableCinPath(mode);
        if (!host.ParseCin(txtPath, cinPath, true))
            host.ShowMessage(L"自建詞庫載入發生錯誤！", L"DIME 自建詞庫", MB_ICONERROR);
    }
    host.EnableSave(false);   // not dirty until the next edit
}

void ExportCustomTable(Host& host)
{
    if (!host.EditorExists()) return;
    {
        std::wstring vText = host.EditorText();
        int firstErrorLine = 0;
        std::wstring errSummary;
        int errCount = host.ValidateBuffer(vText, MAX_REPORT, firstErrorLine, errSummary);
        if (errCount > 0) {
            wchar_t msg[2048];
            StringCchPrintfW(msg, _countof(msg),
                L"匯出失敗：偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s請修正後再匯出。",
                errCount, firstErrorLine, errSummary.c_str());
            host.ShowMessage(msg, L"自建詞庫格式錯誤", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    WCHAR pathToWrite[MAX_PATH] = {};
    std::wstring docs = host.DocumentsPath();
    StringCchPrintfW(pathToWrite, MAX_PATH, L"%s\\customTable.txt", docs.c_str());
    std::wstring path = pathToWrite;
    if (host.SaveFileDialog(path)) {
        if (host.EditorExists()) {
            std::wstring text = host.EditorText();
            host.WriteBomFile(path, text);
        }
    }
}

void ImportCustomTable(Host& host)
{
    std::wstring pathToLoad;
    if (!host.OpenFileDialog(1, pathToLoad)) return;
    if (!host.EditorExists()) return;
    std::wstring text;
    if (!host.LoadTextFile(pathToLoad, text)) {
        host.ShowMessage(L"無法讀取或解碼檔案，請確認檔案編碼為 UTF-8 / UTF-16 / ANSI(Big5)。",
            L"DIME 自建詞庫", MB_ICONERROR);
        return;
    }
    // Load even with errors so the user can fix them; Save is the gate.
    int firstErrorLine = 0;
    std::wstring errSummary;
    int errCount = host.ValidateBuffer(text, MAX_REPORT, firstErrorLine, errSummary);
    host.SetEditorTextMuted(text);
    host.EnableSave(true);
    host.ValidateViewport();
    if (errCount > 0 && firstErrorLine > 0) {
        wchar_t msg[2048];
        StringCchPrintfW(msg, _countof(msg),
            L"匯入完成，但偵測到 %d 個格式錯誤行，第一個錯誤在第 %d 行。\n\n%s%s按確定後將跳到第一個錯誤行，請修正後再儲存。",
            errCount, firstErrorLine, errSummary.c_str(),
            (errCount > MAX_REPORT) ? L"  ...(更多錯誤已省略)\n" : L"");
        host.ShowMessage(msg, L"DIME 自建詞庫匯入", MB_ICONWARNING);
        JumpToLine(host, firstErrorLine);
    }
}

} // namespace

void Handle(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    int notifyCode = ev.notifyCode;
    int ctrlId = ev.ctrlId;

    if (notifyCode == BN_CLICKED) {
        SettingsControlId btnId = (SettingsControlId)(ctrlId - 3000);
        switch (FindAction(btnId)) {
        case RowAction::OpenFontDialog:    FontDialog(host, s); break;
        case RowAction::ResetDefaults:     ResetDefaults(host, s, ev.mode); break;
        case RowAction::LoadTable:         LoadTable(host, btnId); break;
        case RowAction::SaveCustomTable:   SaveCustomTable(host, ev.mode); break;
        case RowAction::ExportCustomTable: ExportCustomTable(host); break;
        case RowAction::ImportCustomTable: ImportCustomTable(host); break;
        default: break;
        }
    }

    if (notifyCode == EN_CHANGE) {
        int settingsId = ctrlId - 3000;
        if ((SettingsControlId)settingsId == CTRL_CUSTOM_TABLE_EDITOR) {
            host.ValidateViewport();
            host.CancelBgScan();
            host.EnableSave(true);   // mark dirty
            host.UpdateLineCount();
        } else if ((SettingsControlId)settingsId == CTRL_MAX_CODES) {
            UINT val;
            if (SettingsModel::ParseMaxCodes(ev.editText, val)) {
                s.maxCodes = val;
                host.ApplyAndSave();
            }
        }
    }

    if (notifyCode == CBN_SELCHANGE) {
        int settingsId = ctrlId - 3000;
        int sel = ev.comboSel;
        if (sel != CB_ERR) {
            switch ((SettingsControlId)settingsId) {
            case CTRL_COLOR_MODE:         s.colorMode = ev.comboItemData; break;
            case CTRL_ARRAY_SCOPE:        s.arrayScope = ev.comboItemData; break;
            case CTRL_REVERSE_CONVERSION: host.SetReverseConversionSelection(sel); break;
            default: SettingsModel::SyncComboToSnapshot(s, (SettingsControlId)settingsId, sel); break;
            }
            if ((SettingsControlId)settingsId == CTRL_COLOR_MODE) {
                // Auto-expand the color grid when switching to custom; save only
                // the mode (not the palette), then preview the mode's palette.
                colorGridExpanded = (s.colorMode == 3) ? 1 : 0;
                host.SetColorMode(s.colorMode);
                host.WriteConfig(ev.mode);
                host.LoadColorsForMode(s.colorMode);
                COLORREF clrs[6] = {};
                host.GetColors(clrs);
                s.itemColor       = clrs[0];
                s.selectedColor   = clrs[1];
                s.itemBGColor     = clrs[2];
                s.phraseColor     = clrs[3];
                s.numberColor     = clrs[4];
                s.selectedBGColor = clrs[5];
                host.Rebuild();
            } else {
                host.ApplyAndSave();
            }
            host.InvalidateContent(false);
            host.UpdateContent();
        }
    }

    // Color swatch click (STN_CLICKED == BN_CLICKED == 0)
    if (notifyCode == STN_CLICKED || (notifyCode == BN_CLICKED && ctrlId >= 4000 && ctrlId < 4006)) {
        int swIdx = ctrlId - 4000;
        if (swIdx >= 0 && swIdx < 6) {
            COLORREF* fields[] = {
                &s.itemColor, &s.selectedColor, &s.itemBGColor,
                &s.phraseColor, &s.numberColor, &s.selectedBGColor
            };
            COLORREF rgb = *fields[swIdx];
            if (host.PickColor(rgb)) {
                *fields[swIdx] = rgb;
                host.LoadColorsForMode(s.colorMode);
                host.SetColor(swIdx, rgb);
                host.SaveColorsForMode(s.colorMode);
                host.ApplyAndSave();
                host.InvalidateSwatch(swIdx);
                host.InvalidateContent(false);
                host.UpdateContent();
            }
        }
    }
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

// ---- C vtable over Host for dime-settings ---------------------------------
namespace {

struct Bridge {
    Host* host;
    std::wstring keep;     // last string returned to Rust
};

Host& H(void* ctx) { return *static_cast<Bridge*>(ctx)->host; }
std::wstring W(const uint16_t* p, size_t n) { return std::wstring(reinterpret_cast<const wchar_t*>(p), n); }
const uint16_t* Keep(void* ctx, std::wstring s, size_t* len)
{
    Bridge* b = static_cast<Bridge*>(ctx);
    b->keep = std::move(s);
    if (len) *len = b->keep.size();
    return reinterpret_cast<const uint16_t*>(b->keep.c_str());
}

// Every thunk swallows exceptions: host callbacks must not throw into Rust.
#define GUARD(expr, fallback) try { expr; } catch (...) { return fallback; }

int32_t LogPixelsY(void* c) { GUARD(return H(c).LogPixelsY(), 96) }
int32_t ChooseFontT(void* c, void* lf, int32_t* pt) { GUARD({ int p = 0; bool ok = H(c).PickFont(*static_cast<LOGFONTW*>(lf), p); *pt = p; return ok ? 1 : 0; }, 0) }
int32_t ChooseColorT(void* c, uint32_t* rgb) { GUARD({ COLORREF v = *rgb; bool ok = H(c).PickColor(v); *rgb = v; return ok ? 1 : 0; }, 0) }
const uint16_t* GetOpenFileT(void* c, int32_t filter, int32_t* ok, size_t* len) { GUARD({ std::wstring p; *ok = H(c).OpenFileDialog(filter, p) ? 1 : 0; return Keep(c, p, len); }, nullptr) }
const uint16_t* GetSaveFileT(void* c, const uint16_t* def, size_t n, int32_t* ok, size_t* len) { GUARD({ std::wstring p = W(def, n); *ok = H(c).SaveFileDialog(p) ? 1 : 0; return Keep(c, p, len); }, nullptr) }
int32_t MessageBoxT(void* c, const uint16_t* t, size_t n, const uint16_t* cap, uint32_t type) { GUARD(return H(c).ShowMessage(W(t, n), reinterpret_cast<const wchar_t*>(cap), type), IDCANCEL) }
const uint16_t* AppDataPathT(void* c, size_t* len) { GUARD(return Keep(c, H(c).AppDataPath(), len), nullptr) }
const uint16_t* DocumentsPathT(void* c, size_t* len) { GUARD(return Keep(c, H(c).DocumentsPath(), len), nullptr) }
const uint16_t* TxtPathT(void* c, int32_t mode, size_t* len) { GUARD(return Keep(c, H(c).CustomTableTxtPath(mode), len), nullptr) }
const uint16_t* CinPathT(void* c, int32_t mode, size_t* len) { GUARD(return Keep(c, H(c).CustomTableCinPath(mode), len), nullptr) }
int32_t WriteBomFileT(void* c, const uint16_t* p, size_t pn, const uint16_t* t, size_t tn) { GUARD(return H(c).WriteBomFile(W(p, pn), W(t, tn)) ? 1 : 0, 0) }
const uint16_t* LoadTextFileT(void* c, const uint16_t* p, size_t pn, int32_t* ok, size_t* len) { GUARD({ std::wstring t; *ok = H(c).LoadTextFile(W(p, pn), t) ? 1 : 0; return Keep(c, t, len); }, nullptr) }
int32_t ParseCinT(void* c, const uint16_t* s, size_t sn, const uint16_t* d, size_t dn, int32_t custom) { GUARD(return H(c).ParseCin(W(s, sn), W(d, dn), custom != 0) ? 1 : 0, 0) }
void ResetAllDefaultsT(void* c, int32_t m) { try { H(c).ResetAllDefaults(m); } catch (...) {} }
void WriteConfigT(void* c, int32_t m) { try { H(c).WriteConfig(m); } catch (...) {} }
void LoadConfigT(void* c, int32_t m) { try { H(c).LoadConfig(m); } catch (...) {} }
void LoadSnapshotT(void* c, void* out) { try { *static_cast<SettingsSnapshot*>(out) = H(c).LoadSnapshot(); } catch (...) {} }
void ApplyAndSaveT(void* c) { try { H(c).ApplyAndSave(); } catch (...) {} }
void SetColorModeT(void* c, int32_t m) { try { H(c).SetColorMode(m); } catch (...) {} }
void LoadColorsForModeT(void* c, int32_t m) { try { H(c).LoadColorsForMode(m); } catch (...) {} }
void GetColorsT(void* c, uint32_t* out) { try { COLORREF v[6] = {}; H(c).GetColors(v); for (int i = 0; i < 6; i++) out[i] = v[i]; } catch (...) {} }
void SetColorT(void* c, int32_t i, uint32_t v) { try { H(c).SetColor(i, v); } catch (...) {} }
void SaveColorsForModeT(void* c, int32_t m) { try { H(c).SaveColorsForMode(m); } catch (...) {} }
void SetReverseSelT(void* c, int32_t sel) { try { H(c).SetReverseConversionSelection(sel); } catch (...) {} }
void RebuildT(void* c) { try { H(c).Rebuild(); } catch (...) {} }
void InvalidateContentT(void* c, int32_t e) { try { H(c).InvalidateContent(e != 0); } catch (...) {} }
void UpdateContentT(void* c) { try { H(c).UpdateContent(); } catch (...) {} }
void InvalidateSwatchT(void* c, int32_t i) { try { H(c).InvalidateSwatch(i); } catch (...) {} }
int32_t EditorExistsT(void* c) { GUARD(return H(c).EditorExists() ? 1 : 0, 0) }
const uint16_t* EditorTextT(void* c, size_t* len) { GUARD(return Keep(c, H(c).EditorText(), len), nullptr) }
void SetEditorTextMutedT(void* c, const uint16_t* t, size_t n) { try { H(c).SetEditorTextMuted(W(t, n)); } catch (...) {} }
int32_t EditorLineIndexT(void* c, int32_t line) { GUARD(return H(c).EditorLineIndex(line), -1) }
void EditorFocusAtT(void* c, int32_t cp) { try { H(c).EditorFocusAt(cp); } catch (...) {} }
void ValidateViewportT(void* c) { try { H(c).ValidateViewport(); } catch (...) {} }
void CancelBgScanT(void* c) { try { H(c).CancelBgScan(); } catch (...) {} }
void EnableSaveT(void* c, int32_t e) { try { H(c).EnableSave(e != 0); } catch (...) {} }
void UpdateLineCountT(void* c) { try { H(c).UpdateLineCount(); } catch (...) {} }
const uint16_t* ValidateBufferT(void* c, const uint16_t* t, size_t n, int32_t maxReport, int32_t* count, int32_t* first, size_t* len)
{
    GUARD({ int f = 0; std::wstring sum; *count = H(c).ValidateBuffer(W(t, n), maxReport, f, sum); *first = f; return Keep(c, sum, len); }, nullptr)
}
#undef GUARD

const DimeSettingsCmdHostVtbl kVtbl = {
    LogPixelsY, ChooseFontT, ChooseColorT, GetOpenFileT, GetSaveFileT, MessageBoxT,
    AppDataPathT, DocumentsPathT, TxtPathT, CinPathT, WriteBomFileT, LoadTextFileT, ParseCinT,
    ResetAllDefaultsT, WriteConfigT, LoadConfigT, LoadSnapshotT, ApplyAndSaveT,
    SetColorModeT, LoadColorsForModeT, GetColorsT, SetColorT, SaveColorsForModeT, SetReverseSelT,
    RebuildT, InvalidateContentT, UpdateContentT, InvalidateSwatchT,
    EditorExistsT, EditorTextT, SetEditorTextMutedT, EditorLineIndexT, EditorFocusAtT,
    ValidateViewportT, CancelBgScanT, EnableSaveT, UpdateLineCountT, ValidateBufferT,
};

} // namespace

void HandleRust(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    Bridge bridge{ &host, {} };
    // The flag is written in place: Host::Rebuild reads it during the call.
    dime_settings_command_handle(&kVtbl, &bridge, &s, &colorGridExpanded, &ev);
}

#endif

#ifdef DIME_USE_RUST_CONFIG
void Handle(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    HandleRust(host, s, colorGridExpanded, ev);
}
#else
void Handle(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    Cpp::Handle(host, s, colorGridExpanded, ev);
}
#endif

} // namespace SettingsCommand
