// SettingsFakeApp.h — a recording SettingsApp::Host for the settings window
// tests (SettingsWindowParityTest, SettingsUiaTest). No CConfig, no real
// %APPDATA%: everything is logged, paths point into the test's folder.
#pragma once

#include <Windows.h>
#include <string>
#include "../DIMESettings/SettingsApp.h"

namespace SettingsTestApp {

// Class names end in _C / _R; logs drop the suffix.
inline std::wstring ClassOf(HWND h)
{
    if (!h) return L"null";
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    std::wstring s = cls;
    if (s.size() > 2 && s[s.size() - 2] == L'_') s.resize(s.size() - 2);
    return s;
}

class FakeApp : public SettingsApp::Host {
public:
    std::wstring log;
    UINT dpi = 96;
    bool dark = false;
    int colorMode = 0;
    COLORREF colors[6] = { RGB(1, 2, 3), RGB(40, 50, 60), RGB(250, 250, 240), RGB(9, 99, 199), RGB(200, 30, 30), RGB(0, 120, 215) };
    SettingsSnapshot snap = {};
    std::wstring dir;

    void L(const std::wstring& s) { log += s + L"\n"; }
    UINT DpiForWindow(HWND h) override { L(L"dpi " + ClassOf(h)); return dpi; }
    bool SystemDark() override { L(L"dark"); return dark; }
    int ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type) override
    {
        L(L"msg " + ClassOf(parent) + L" [" + caption + L"] [" + text + L"] " + std::to_wstring(type));
        return IDNO;
    }
    void LoadConfig(int m) override { L(L"load " + std::to_wstring(m)); }
    void WriteConfig(int m) override { L(L"write " + std::to_wstring(m)); }
    void ResetAllDefaults(int m) override { L(L"reset " + std::to_wstring(m)); }
    int ColorMode() override { L(L"colorMode"); return colorMode; }
    void SetColorMode(int m) override { L(L"setColorMode " + std::to_wstring(m)); colorMode = m; }
    void LoadColorsForMode(int m) override { L(L"loadColors " + std::to_wstring(m)); }
    void SaveColorsForMode(int m) override { L(L"saveColors " + std::to_wstring(m)); }
    void GetColors(COLORREF out[6]) override { L(L"getColors"); memcpy(out, colors, sizeof(colors)); }
    void SetColor(int i, COLORREF c) override { L(L"setColor " + std::to_wstring(i) + L" " + std::to_wstring(c)); }
    void SetReverseSelection(int sel) override { L(L"reverseSel " + std::to_wstring(sel)); }
    void EnumerateReverseProviders() override { L(L"enumReverse"); }
    int ReverseCount() override { L(L"reverseCount"); return 2; }
    std::wstring ReverseDescription(int i) override { L(L"reverseDesc " + std::to_wstring(i)); return i ? L"注音反查" : L"倉頡反查"; }
    bool ReverseIsCurrent(int i) override { L(L"reverseCur " + std::to_wstring(i)); return i == 1; }
    bool ReverseIsNull() override { L(L"reverseNull"); return false; }
    SettingsSnapshot LoadSnapshot() override { L(L"loadSnapshot"); return snap; }
    void ApplySnapshot(const SettingsSnapshot& s) override
    {
        L(L"apply colorMode=" + std::to_wstring(s.colorMode) + L" maxCodes=" + std::to_wstring(s.maxCodes)
            + L" scope=" + std::to_wstring(s.arrayScope) + L" beep=" + std::to_wstring(s.doBeep));
        snap = s;
    }
    bool ParseCin(const wchar_t* src, const wchar_t* dst, bool custom) override
    {
        L(std::wstring(L"parse ") + src + L" -> " + dst + L" " + std::to_wstring(custom));
        return true;
    }
    void RecreateEngine(int m) override { L(L"engine " + std::to_wstring(m)); }
    void DestroyEngine() override { L(L"destroyEngine"); }
    CCompositionProcessorEngine* Engine() override { return nullptr; }
    bool EngineCharOk(wchar_t) override { L(L"charOk"); return true; }
    std::wstring CustomTableTxtPath(int m) override { L(L"txtPath " + std::to_wstring(m)); return dir + L"\\custom" + std::to_wstring(m) + L".txt"; }
    std::wstring CustomTableCinPath(int m) override { L(L"cinPath " + std::to_wstring(m)); return dir + L"\\custom" + std::to_wstring(m) + L".cin"; }
    std::wstring AppDataPath() override { L(L"appData"); return dir + L"\\"; }
    bool Windows1809OrLater() override { L(L"1809"); return true; }
};

} // namespace SettingsTestApp
