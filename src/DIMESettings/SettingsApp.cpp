// SettingsApp.cpp — see SettingsApp.h. ConfigHost is what SettingsWindow.cpp
// used to call directly; the thunks are DimeSettingsAppVtbl.

#include "framework.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsApp.h"
#include "SettingsMsgBox.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsApp {

ConfigHost::~ConfigHost() { SettingsModel::DestroyEngine(_engine); }

UINT ConfigHost::DpiForWindow(HWND hWnd) { return CConfig::GetDpiForHwnd(hWnd); }
bool ConfigHost::SystemDark() { return CConfig::IsSystemDarkTheme() != FALSE; }
int ConfigHost::ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    return SettingsMsgBox::Show(parent, text, caption, type);
}
void ConfigHost::LoadConfig(int mode) { CConfig::LoadConfig((IME_MODE)mode); }
void ConfigHost::WriteConfig(int mode) { CConfig::WriteConfig((IME_MODE)mode); }
void ConfigHost::ResetAllDefaults(int mode) { CConfig::ResetAllDefaults((IME_MODE)mode); }
int ConfigHost::ColorMode() { return (int)CConfig::GetColorMode(); }
void ConfigHost::SetColorMode(int m) { CConfig::SetColorMode((IME_COLOR_MODE)m); }
void ConfigHost::LoadColorsForMode(int m) { CConfig::LoadColorsForMode((IME_COLOR_MODE)m); }
void ConfigHost::SaveColorsForMode(int m) { CConfig::SaveColorsForMode((IME_COLOR_MODE)m); }
void ConfigHost::GetColors(COLORREF out[6])
{
    const ColorInfo* clrs = CConfig::GetColors();
    for (int i = 0; i < 6; i++) out[i] = clrs[i].color;
}
void ConfigHost::SetColor(int index, COLORREF color)
{
    const_cast<ColorInfo*>(CConfig::GetColors())[index].color = color;
}
void ConfigHost::SetReverseSelection(int sel) { CConfig::SetReverseConversionSelection(sel); }
void ConfigHost::EnumerateReverseProviders() { CConfig::EnumerateReverseConversionProviders(1028); }
int ConfigHost::ReverseCount()
{
    CDIMEArray<LanguageProfileInfo>* list = CConfig::GetReverseConvervsionInfoList();
    return list ? (int)list->Count() : 0;
}
std::wstring ConfigHost::ReverseDescription(int i)
{
    CDIMEArray<LanguageProfileInfo>* list = CConfig::GetReverseConvervsionInfoList();
    if (!list || i < 0 || (UINT)i >= list->Count()) return std::wstring();
    const wchar_t* d = list->GetAt(i)->description;
    return d ? std::wstring(d) : std::wstring();
}
bool ConfigHost::ReverseIsCurrent(int i)
{
    CDIMEArray<LanguageProfileInfo>* list = CConfig::GetReverseConvervsionInfoList();
    if (!list || i < 0 || (UINT)i >= list->Count()) return false;
    return IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), list->GetAt(i)->guidProfile) != FALSE;
}
bool ConfigHost::ReverseIsNull()
{
    return IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL) != FALSE;
}
SettingsSnapshot ConfigHost::LoadSnapshot() { return SettingsModel::LoadFromConfig(); }
void ConfigHost::ApplySnapshot(const SettingsSnapshot& s) { SettingsModel::ApplyToConfig(s); }
bool ConfigHost::ParseCin(const wchar_t* src, const wchar_t* dst, bool customTable)
{
    return CConfig::parseCINFile(src, dst, customTable ? TRUE : FALSE) != FALSE;
}
void ConfigHost::RecreateEngine(int mode)
{
    SettingsModel::DestroyEngine(_engine);
    _engine = SettingsModel::CreateEngine((IME_MODE)mode);
}
void ConfigHost::DestroyEngine() { SettingsModel::DestroyEngine(_engine); }
bool ConfigHost::EngineCharOk(wchar_t c)
{
    return _engine && _engine->ValidateCompositionKeyCharFull(c);
}
std::wstring ConfigHost::CustomTableTxtPath(int mode)
{
    WCHAR buf[MAX_PATH] = {};
    SettingsModel::GetCustomTableTxtPath((IME_MODE)mode, buf, MAX_PATH);
    return buf;
}
std::wstring ConfigHost::CustomTableCinPath(int mode)
{
    WCHAR buf[MAX_PATH] = {};
    SettingsModel::GetCustomTableCINPath((IME_MODE)mode, buf, MAX_PATH);
    return buf;
}
std::wstring ConfigHost::AppDataPath()
{
    WCHAR buf[MAX_PATH] = {};
    Global::GetAppDataPath(buf, TRUE);
    return buf;
}
bool ConfigHost::Windows1809OrLater() { return Global::isWindows1809OrLater != FALSE; }

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

namespace {

Host* H(void* c) { return static_cast<Host*>(c); }

// Copies s plus a NUL into buf (cap units, truncated); returns s.size().
size_t Out(const std::wstring& s, uint16_t* buf, size_t cap)
{
    if (buf && cap > 0) {
        size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        memcpy(buf, s.c_str(), n * sizeof(wchar_t));
        buf[n] = 0;
    }
    return s.size();
}

// Every thunk: Host methods must not throw; a stray exception stops here.
uint32_t Th_Dpi(void* c, void* h) { try { return H(c)->DpiForWindow((HWND)h); } catch (...) { return 96; } }
int32_t Th_SystemDark(void* c) { try { return H(c)->SystemDark() ? 1 : 0; } catch (...) { return 0; } }
int32_t Th_MessageBox(void* c, void* parent, const uint16_t* text, const uint16_t* caption, uint32_t type)
{
    try { return H(c)->ShowMessage((HWND)parent, (const wchar_t*)text, (const wchar_t*)caption, type); }
    catch (...) { return IDCANCEL; }
}
void Th_LoadConfig(void* c, int32_t m) { try { H(c)->LoadConfig(m); } catch (...) {} }
void Th_WriteConfig(void* c, int32_t m) { try { H(c)->WriteConfig(m); } catch (...) {} }
void Th_ResetAllDefaults(void* c, int32_t m) { try { H(c)->ResetAllDefaults(m); } catch (...) {} }
int32_t Th_ColorMode(void* c) { try { return H(c)->ColorMode(); } catch (...) { return 0; } }
void Th_SetColorMode(void* c, int32_t m) { try { H(c)->SetColorMode(m); } catch (...) {} }
void Th_LoadColorsForMode(void* c, int32_t m) { try { H(c)->LoadColorsForMode(m); } catch (...) {} }
void Th_SaveColorsForMode(void* c, int32_t m) { try { H(c)->SaveColorsForMode(m); } catch (...) {} }
void Th_GetColors(void* c, uint32_t* out)
{
    try { COLORREF v[6] = {}; H(c)->GetColors(v); for (int i = 0; i < 6; i++) out[i] = v[i]; } catch (...) {}
}
void Th_SetColor(void* c, int32_t i, uint32_t color) { try { H(c)->SetColor(i, color); } catch (...) {} }
void Th_SetReverseSelection(void* c, int32_t sel) { try { H(c)->SetReverseSelection(sel); } catch (...) {} }
void Th_EnumerateReverse(void* c) { try { H(c)->EnumerateReverseProviders(); } catch (...) {} }
int32_t Th_ReverseCount(void* c) { try { return H(c)->ReverseCount(); } catch (...) { return 0; } }
size_t Th_ReverseDescription(void* c, int32_t i, uint16_t* buf, size_t cap)
{
    try { return Out(H(c)->ReverseDescription(i), buf, cap); } catch (...) { return Out(L"", buf, cap); }
}
int32_t Th_ReverseIsCurrent(void* c, int32_t i) { try { return H(c)->ReverseIsCurrent(i) ? 1 : 0; } catch (...) { return 0; } }
int32_t Th_ReverseIsNull(void* c) { try { return H(c)->ReverseIsNull() ? 1 : 0; } catch (...) { return 1; } }
void Th_LoadSnapshot(void* c, void* out)
{
    try { *static_cast<SettingsSnapshot*>(out) = H(c)->LoadSnapshot(); } catch (...) {}
}
void Th_ApplySnapshot(void* c, const void* s)
{
    try { H(c)->ApplySnapshot(*static_cast<const SettingsSnapshot*>(s)); } catch (...) {}
}
int32_t Th_ParseCin(void* c, const uint16_t* src, const uint16_t* dst, int32_t custom)
{
    try { return H(c)->ParseCin((const wchar_t*)src, (const wchar_t*)dst, custom != 0) ? 1 : 0; } catch (...) { return 0; }
}
void Th_RecreateEngine(void* c, int32_t m) { try { H(c)->RecreateEngine(m); } catch (...) {} }
void Th_DestroyEngine(void* c) { try { H(c)->DestroyEngine(); } catch (...) {} }
int32_t Th_HasEngine(void* c) { try { return H(c)->Engine() ? 1 : 0; } catch (...) { return 0; } }
int32_t Th_EngineCharOk(void* c, uint16_t ch) { try { return H(c)->EngineCharOk((wchar_t)ch) ? 1 : 0; } catch (...) { return 0; } }
size_t Th_TxtPath(void* c, int32_t m, uint16_t* buf, size_t cap)
{
    try { return Out(H(c)->CustomTableTxtPath(m), buf, cap); } catch (...) { return Out(L"", buf, cap); }
}
size_t Th_CinPath(void* c, int32_t m, uint16_t* buf, size_t cap)
{
    try { return Out(H(c)->CustomTableCinPath(m), buf, cap); } catch (...) { return Out(L"", buf, cap); }
}
size_t Th_AppDataPath(void* c, uint16_t* buf, size_t cap)
{
    try { return Out(H(c)->AppDataPath(), buf, cap); } catch (...) { return Out(L"", buf, cap); }
}
int32_t Th_Windows1809(void* c) { try { return H(c)->Windows1809OrLater() ? 1 : 0; } catch (...) { return 0; } }

const DimeSettingsAppVtbl kVtbl = {
    &Th_Dpi, &Th_SystemDark, &Th_MessageBox,
    &Th_LoadConfig, &Th_WriteConfig, &Th_ResetAllDefaults,
    &Th_ColorMode, &Th_SetColorMode, &Th_LoadColorsForMode, &Th_SaveColorsForMode, &Th_GetColors, &Th_SetColor,
    &Th_SetReverseSelection, &Th_EnumerateReverse, &Th_ReverseCount, &Th_ReverseDescription,
    &Th_ReverseIsCurrent, &Th_ReverseIsNull,
    &Th_LoadSnapshot, &Th_ApplySnapshot, &Th_ParseCin,
    &Th_RecreateEngine, &Th_DestroyEngine, &Th_HasEngine, &Th_EngineCharOk,
    &Th_TxtPath, &Th_CinPath, &Th_AppDataPath, &Th_Windows1809,
};

} // namespace

const void* Vtbl() { return &kVtbl; }

#endif

} // namespace SettingsApp
