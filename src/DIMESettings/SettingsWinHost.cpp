// SettingsWinHost.cpp — see SettingsWinHost.h. The Cpp functions are the
// code that used to be in WindowHost (SettingsWindow::HandleCommand);
// dime-settings (winhost.rs) mirrors them.

#include <windows.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlobj.h>
#include <strsafe.h>
#include "SettingsWinHost.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsWinHost {

namespace Cpp {

int LogPixelsY(HWND hWnd)
{
    HDC hdcScr = GetDC(hWnd);
    int lpY = GetDeviceCaps(hdcScr, LOGPIXELSY);
    ReleaseDC(hWnd, hdcScr);
    return lpY;
}

bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10)
{
    bool ok = false;
    HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
    if (hDlgLib) {
        auto pCF = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSEFONTW)>(GetProcAddress(hDlgLib, "ChooseFontW"));
        if (pCF) {
            CHOOSEFONTW cf = {};
            cf.lStructSize = sizeof(cf);
            cf.hwndOwner = owner;
            cf.lpLogFont = &lf;
            cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
            if (pCF(&cf)) { pointSize10 = cf.iPointSize; ok = true; }
        }
        FreeLibrary(hDlgLib);
    }
    return ok;
}

bool PickColor(HWND owner, COLORREF& rgb)
{
    static COLORREF custColors[16] = {};
    bool ok = false;
    HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
    if (hDlgLib) {
        auto pCC = reinterpret_cast<BOOL(WINAPI*)(LPCHOOSECOLORW)>(GetProcAddress(hDlgLib, "ChooseColorW"));
        if (pCC) {
            CHOOSECOLORW cc = {};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = owner;
            cc.rgbResult = rgb;
            cc.lpCustColors = custColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (pCC(&cc)) { rgb = cc.rgbResult; ok = true; }
        }
        FreeLibrary(hDlgLib);
    }
    return ok;
}

bool OpenFileDialog(HWND owner, int filter, std::wstring& path)
{
    bool ok = false;
    HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
    if (hDlgLib) {
        auto pGetOpen = reinterpret_cast<BOOL(WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(hDlgLib, "GetOpenFileNameW"));
        if (pGetOpen) {
            WCHAR pathToLoad[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.hwndOwner = owner;
            ofn.lpstrFile = pathToLoad;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            ofn.lpstrFilter = (filter == 0) ? L"CIN TXT Files(*.txt, *.cin)\0*.cin;*.txt\0\0"
                                            : L"詞庫文字檔(*.txt)\0*.txt\0\0";
            if (pGetOpen(&ofn)) { path = pathToLoad; ok = true; }
        }
        FreeLibrary(hDlgLib);
    }
    return ok;
}

bool SaveFileDialog(HWND owner, std::wstring& path)
{
    bool ok = false;
    HINSTANCE hDlgLib = LoadLibraryW(L"comdlg32.dll");
    if (hDlgLib) {
        auto pGetSave = reinterpret_cast<BOOL(WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(hDlgLib, "GetSaveFileNameW"));
        if (pGetSave) {
            WCHAR pathToWrite[MAX_PATH] = {};
            StringCchCopyW(pathToWrite, MAX_PATH, path.c_str());
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.hwndOwner = owner;
            ofn.lpstrFile = pathToWrite;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            ofn.lpstrFilter = L"詞庫文字檔(*.txt)\0*.txt\0\0";
            if (pGetSave(&ofn)) { path = pathToWrite; ok = true; }
        }
        FreeLibrary(hDlgLib);
    }
    return ok;
}

bool WriteBomFile(const std::wstring& path, const std::wstring& text)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WCHAR bom = 0xFEFF;
    WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr);
    WriteFile(hFile, text.c_str(), (DWORD)(text.size() * sizeof(WCHAR)), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

std::wstring DocumentsPath()
{
    WCHAR buf[MAX_PATH] = {};
    SHGetSpecialFolderPathW(nullptr, buf, CSIDL_PERSONAL, FALSE);
    return buf;
}

std::wstring EditorText(HWND hRE)
{
    DWORD dwLen = GetWindowTextLengthW(hRE);
    std::wstring text(dwLen + 1, L'\0');
    GetWindowTextW(hRE, &text[0], dwLen + 1);
    text.resize(dwLen);
    return text;
}

int SetEditorTextMuted(HWND hRE, const std::wstring& text)
{
    LRESULT prevMask = SendMessageW(hRE, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, 0);
    SetWindowTextW(hRE, text.c_str());
    int lines = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    SendMessageW(hRE, EM_SETEVENTMASK, 0, (LPARAM)prevMask);
    return lines;
}

int EditorLineIndex(HWND hRE, int line)
{
    return (int)(LONG)SendMessageW(hRE, EM_LINEINDEX, (WPARAM)line, 0);
}

void EditorFocusAt(HWND hRE, int cp)
{
    // SetFocus first: the subclass's WM_SETFOCUS resets the selection.
    SetFocus(hRE);
    SendMessageW(hRE, EM_SETSEL, (WPARAM)cp, (LPARAM)cp);
    SendMessageW(hRE, EM_SCROLLCARET, 0, 0);
}

int EditorLineCount(HWND hRE)
{
    return (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

namespace Rust {

static const uint16_t* U(const wchar_t* s) { return reinterpret_cast<const uint16_t*>(s); }

int LogPixelsY(HWND wnd) { return dime_settings_win_log_pixels_y(wnd); }

bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10)
{
    return dime_settings_win_pick_font(owner, &lf, reinterpret_cast<int32_t*>(&pointSize10)) != 0;
}

bool PickColor(HWND owner, COLORREF& rgb)
{
    return dime_settings_win_pick_color(owner, reinterpret_cast<uint32_t*>(&rgb)) != 0;
}

bool OpenFileDialog(HWND owner, int filter, std::wstring& path)
{
    wchar_t buf[MAX_PATH] = {};
    if (!dime_settings_win_open_file(owner, filter, reinterpret_cast<uint16_t*>(buf))) return false;
    path = buf;
    return true;
}

bool SaveFileDialog(HWND owner, std::wstring& path)
{
    wchar_t buf[MAX_PATH] = {};
    StringCchCopyW(buf, MAX_PATH, path.c_str());
    if (!dime_settings_win_save_file(owner, reinterpret_cast<uint16_t*>(buf))) return false;
    path = buf;
    return true;
}

bool WriteBomFile(const std::wstring& path, const std::wstring& text)
{
    return dime_settings_win_write_bom_file(U(path.c_str()), U(text.c_str()), text.size()) != 0;
}

std::wstring DocumentsPath()
{
    wchar_t buf[MAX_PATH] = {};
    dime_settings_win_documents_path(reinterpret_cast<uint16_t*>(buf));
    return buf;
}

std::wstring EditorText(HWND re)
{
    size_t len = 0;
    dime_settings_win_editor_text(re, nullptr, 0, &len);
    std::wstring text(len + 1, L'\0');
    size_t got = 0;
    dime_settings_win_editor_text(re, reinterpret_cast<uint16_t*>(&text[0]), text.size(), &got);
    text.resize(len < got ? len : got);
    return text;
}

int SetEditorTextMuted(HWND re, const std::wstring& text)
{
    return dime_settings_win_set_editor_text_muted(re, U(text.c_str()));
}

int EditorLineIndex(HWND re, int line) { return dime_settings_win_editor_line_index(re, line); }
void EditorFocusAt(HWND re, int cp) { dime_settings_win_editor_focus_at(re, cp); }
int EditorLineCount(HWND re) { return dime_settings_win_editor_line_count(re); }

} // namespace Rust

#endif

#ifdef DIME_USE_RUST_CONFIG
namespace Impl = Rust;
#else
namespace Impl = Cpp;
#endif

int LogPixelsY(HWND wnd) { return Impl::LogPixelsY(wnd); }
bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10) { return Impl::PickFont(owner, lf, pointSize10); }
bool PickColor(HWND owner, COLORREF& rgb) { return Impl::PickColor(owner, rgb); }
bool OpenFileDialog(HWND owner, int filter, std::wstring& path) { return Impl::OpenFileDialog(owner, filter, path); }
bool SaveFileDialog(HWND owner, std::wstring& path) { return Impl::SaveFileDialog(owner, path); }
bool WriteBomFile(const std::wstring& path, const std::wstring& text) { return Impl::WriteBomFile(path, text); }
std::wstring DocumentsPath() { return Impl::DocumentsPath(); }
std::wstring EditorText(HWND re) { return Impl::EditorText(re); }
int SetEditorTextMuted(HWND re, const std::wstring& text) { return Impl::SetEditorTextMuted(re, text); }
int EditorLineIndex(HWND re, int line) { return Impl::EditorLineIndex(re, line); }
void EditorFocusAt(HWND re, int cp) { Impl::EditorFocusAt(re, cp); }
int EditorLineCount(HWND re) { return Impl::EditorLineCount(re); }

} // namespace SettingsWinHost
