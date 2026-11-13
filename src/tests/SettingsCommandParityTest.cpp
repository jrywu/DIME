/********************************************************************************
* SettingsCommandParityTest: WM_COMMAND of the settings content area, C++
* (SettingsCommand::Cpp::Handle, lifted out of SettingsWindow.cpp) vs Rust
* (dime-settings command.rs through SettingsCommand::HandleRust and the C
* host vtable). A recording fake Host answers every dialog, file, CConfig,
* and editor call from a seeded script; the call logs (names and arguments,
* including the snapshot at each ApplyAndSave), the final snapshot, and the
* color grid flag must match for random buttons, combos, edits, and swatches.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsCommand.h"
#include "../DIMESettings/SettingsPageLayout.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SC = SettingsCommand;

namespace
{
    std::wstring Hex(const void* p, size_t n)
    {
        static const wchar_t* d = L"0123456789ABCDEF";
        std::wstring s;
        for (size_t i = 0; i < n; i++) {
            BYTE b = ((const BYTE*)p)[i];
            s += d[b >> 4];
            s += d[b & 15];
        }
        return s;
    }

    // Every answer the host gives, drawn once per case so both sides see the same.
    struct Script
    {
        int lpY;
        bool fontOk; LOGFONTW fontOut; int pointSize10;
        bool colorOk; COLORREF colorOut;
        bool openOk; std::wstring openPath;
        bool saveOk; std::wstring saveOverride;
        std::wstring appData, docs, txtPath, cinPath;
        bool writeOk, loadOk, parseOk;
        std::wstring loadText;
        SettingsSnapshot loadSnap;
        COLORREF colors[6];
        bool editorExists;
        std::wstring editorText;
        int lineIndex;
        int vCount, vFirst; std::wstring vSummary;
        int msgResult;
    };

    std::wstring RandomText(std::mt19937& rng, int maxLen)
    {
        static const wchar_t alphabet[] = L"ab;/ \n中文\\:.x0123456789";
        std::wstring s;
        int n = (int)(rng() % (maxLen + 1));
        for (int i = 0; i < n; i++) s += alphabet[rng() % (_countof(alphabet) - 1)];
        return s;
    }

    Script RandomScript(std::mt19937& rng)
    {
        auto b = [&]() { return (rng() & 1) != 0; };
        Script s = {};
        s.lpY = 96 + 24 * (int)(rng() % 5);
        s.fontOk = b();
        s.fontOut.lfWeight = (rng() % 2) ? FW_BOLD : 350;
        s.fontOut.lfItalic = (BYTE)(rng() % 2);
        std::wstring face = (rng() % 3 == 0) ? std::wstring(40, L'F') : L"Microsoft JhengHei";
        wcsncpy_s(s.fontOut.lfFaceName, face.c_str(), _TRUNCATE);
        s.pointSize10 = (int)(rng() % 400) - 5;
        s.colorOk = b();
        s.colorOut = (COLORREF)(rng() & 0xFFFFFF);
        s.openOk = b();
        s.openPath = L"C:\\in\\" + RandomText(rng, 20);
        s.saveOk = b();
        s.saveOverride = b() ? std::wstring() : L"D:\\out\\x.txt";
        s.appData = L"C:\\Users\\u\\AppData\\Roaming\\DIME\\" + std::wstring(rng() % 3 == 0 ? 250 : 5, L'a');
        s.docs = L"C:\\Users\\u\\Documents" + std::wstring(rng() % 3 == 0 ? 245 : 0, L'd');
        s.txtPath = L"C:\\t\\" + RandomText(rng, 10);
        s.cinPath = L"C:\\c\\" + RandomText(rng, 10);
        s.writeOk = b();
        s.loadOk = b();
        s.parseOk = b();
        s.loadText = RandomText(rng, 60);
        s.loadSnap.fontSize = (UINT)(rng() % 40);
        s.loadSnap.colorMode = (int)(rng() % 4);
        s.loadSnap.maxCodes = (UINT)(rng() % 8);
        wcscpy_s(s.loadSnap.fontFaceName, L"Snap");
        for (COLORREF& c : s.colors) c = (COLORREF)(rng() & 0xFFFFFF);
        s.editorExists = (rng() % 5) != 0;
        s.editorText = RandomText(rng, 60);
        s.lineIndex = (int)(rng() % 50) - 2;
        s.vCount = (rng() % 3) ? 0 : (int)(rng() % 20);
        s.vFirst = s.vCount ? (int)(rng() % 30) : 0;
        s.vSummary = (rng() % 4 == 0) ? std::wstring(1100, L's') : RandomText(rng, 40);
        s.msgResult = (int)(rng() % 7) + 1;
        return s;
    }

    class FakeHost : public SC::Host {
    public:
        const Script& sc;
        const SettingsSnapshot* snap = nullptr;
        const int* grid = nullptr;      // the window's flag, read by Rebuild
        std::vector<std::wstring> log;
        explicit FakeHost(const Script& s) : sc(s) {}

        void L(const std::wstring& line) { log.push_back(line); }

        int LogPixelsY() override { L(L"LogPixelsY"); return sc.lpY; }
        bool PickFont(LOGFONTW& lf, int& pointSize10) override
        {
            L(L"PickFont in=" + Hex(&lf, sizeof(lf)));
            if (!sc.fontOk) return false;
            lf.lfWeight = sc.fontOut.lfWeight;
            lf.lfItalic = sc.fontOut.lfItalic;
            memcpy(lf.lfFaceName, sc.fontOut.lfFaceName, sizeof(lf.lfFaceName));
            pointSize10 = sc.pointSize10;
            return true;
        }
        bool PickColor(COLORREF& rgb) override
        {
            L(L"PickColor " + std::to_wstring(rgb));
            if (!sc.colorOk) return false;
            rgb = sc.colorOut;
            return true;
        }
        bool OpenFileDialog(int filter, std::wstring& path) override
        {
            L(L"Open " + std::to_wstring(filter));
            if (!sc.openOk) return false;
            path = sc.openPath;
            return true;
        }
        bool SaveFileDialog(std::wstring& path) override
        {
            L(L"Save default=" + path);
            if (!sc.saveOk) return false;
            if (!sc.saveOverride.empty()) path = sc.saveOverride;
            return true;
        }
        int ShowMessage(const std::wstring& text, const wchar_t* caption, UINT type) override
        {
            L(L"Msg [" + text + L"] [" + caption + L"] " + std::to_wstring(type));
            return sc.msgResult;
        }
        std::wstring AppDataPath() override { L(L"AppData"); return sc.appData; }
        std::wstring DocumentsPath() override { L(L"Docs"); return sc.docs; }
        std::wstring CustomTableTxtPath(int mode) override { L(L"TxtPath " + std::to_wstring(mode)); return sc.txtPath; }
        std::wstring CustomTableCinPath(int mode) override { L(L"CinPath " + std::to_wstring(mode)); return sc.cinPath; }
        bool WriteBomFile(const std::wstring& path, const std::wstring& text) override { L(L"Write " + path + L" | " + text); return sc.writeOk; }
        bool LoadTextFile(const std::wstring& path, std::wstring& text) override
        {
            L(L"Load " + path);
            if (!sc.loadOk) return false;
            text = sc.loadText;
            return true;
        }
        bool ParseCin(const std::wstring& src, const std::wstring& dst, bool custom) override
        {
            L(L"Parse " + src + L" -> " + dst + L" " + std::to_wstring(custom));
            return sc.parseOk;
        }
        void ResetAllDefaults(int m) override { L(L"Reset " + std::to_wstring(m)); }
        void WriteConfig(int m) override { L(L"WriteConfig " + std::to_wstring(m)); }
        void LoadConfig(int m) override { L(L"LoadConfig " + std::to_wstring(m)); }
        SettingsSnapshot LoadSnapshot() override { L(L"LoadSnapshot"); return sc.loadSnap; }
        void ApplyAndSave() override { L(L"ApplyAndSave " + Hex(snap, sizeof(*snap))); }
        void SetColorMode(int m) override { L(L"SetColorMode " + std::to_wstring(m)); }
        void LoadColorsForMode(int m) override { L(L"LoadColors " + std::to_wstring(m)); }
        void GetColors(COLORREF out[6]) override { L(L"GetColors"); for (int i = 0; i < 6; i++) out[i] = sc.colors[i]; }
        void SetColor(int i, COLORREF c) override { L(L"SetColor " + std::to_wstring(i) + L" " + std::to_wstring(c)); }
        void SaveColorsForMode(int m) override { L(L"SaveColors " + std::to_wstring(m)); }
        void SetReverseConversionSelection(int sel) override { L(L"Reverse " + std::to_wstring(sel)); }
        void Rebuild() override { L(L"Rebuild grid=" + std::to_wstring(grid ? *grid : -1)); }
        void InvalidateContent(bool erase) override { L(L"Invalidate " + std::to_wstring(erase)); }
        void UpdateContent() override { L(L"Update"); }
        void InvalidateSwatch(int i) override { L(L"InvalidateSwatch " + std::to_wstring(i)); }
        bool EditorExists() override { L(L"EditorExists"); return sc.editorExists; }
        std::wstring EditorText() override { L(L"EditorText"); return sc.editorText; }
        void SetEditorTextMuted(const std::wstring& t) override { L(L"SetEditor " + t); }
        int EditorLineIndex(int line) override { L(L"LineIndex " + std::to_wstring(line)); return sc.lineIndex; }
        void EditorFocusAt(int cp) override { L(L"FocusAt " + std::to_wstring(cp)); }
        void ValidateViewport() override { L(L"Viewport"); }
        void CancelBgScan() override { L(L"CancelBg"); }
        void EnableSave(bool e) override { L(L"EnableSave " + std::to_wstring(e)); }
        void UpdateLineCount() override { L(L"LineCount"); }
        int ValidateBuffer(const std::wstring& text, int maxReport, int& first, std::wstring& summary) override
        {
            L(L"Validate " + std::to_wstring(maxReport) + L" | " + text);
            first = sc.vFirst;
            summary = sc.vSummary;
            return sc.vCount;
        }
    };

    std::vector<int> ButtonIds()
    {
        std::vector<int> ids = { CTRL_CUSTOM_TABLE_SAVE, CTRL_CUSTOM_TABLE_EDITOR, CTRL_MAX_CODES, CTRL_COLOR_MODE,
            CTRL_ARRAY_SCOPE, CTRL_REVERSE_CONVERSION, 0, 999 };
        for (auto page : { 0, 1 }) {
            int n = 0;
            const LayoutCard* cards = page ? GetLoadTablePageLayout(&n) : GetIMEPageLayout(&n);
            for (int c = 0; c < n; c++)
                for (int r = 0; cards[c].rows && r < cards[c].rowCount; r++)
                    ids.push_back(cards[c].rows[r].id);
        }
        return ids;
    }

    SettingsSnapshot RandomSnapshot(std::mt19937& rng)
    {
        SettingsSnapshot s = {};
        BYTE* p = (BYTE*)&s;
        for (size_t i = 0; i < sizeof(s); i++) p[i] = (BYTE)rng();
        // Keep the face NUL terminated, as CConfig does.
        s.fontFaceName[rng() % LF_FACESIZE] = 0;
        s.fontSize = rng() % 72;
        s.colorMode = (int)(rng() % 4);
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsCommandParityTest)
    {
    public:
        TEST_METHOD(RandomCommands)
        {
            std::mt19937 rng(2024);
            std::vector<int> ids = ButtonIds();
            static const wchar_t* edits[] = { L"4", L"7", L"0", L"12", L"\xFF15", L"x", L"", L" 3", L"99999999999" };
            int calls = 0, messages = 0, applies = 0;
            for (int iter = 0; iter < 20000; iter++) {
                Script sc = RandomScript(rng);
                SC::CommandEvent ev = {};
                switch (rng() % 5) {
                case 0: ev.notifyCode = BN_CLICKED; ev.ctrlId = 3000 + ids[rng() % ids.size()]; break;
                case 1: ev.notifyCode = BN_CLICKED; ev.ctrlId = 3995 + (int)(rng() % 15); break;   // swatches 4000..4005
                case 2: ev.notifyCode = EN_CHANGE; ev.ctrlId = 3000 + ((rng() & 1) ? CTRL_MAX_CODES : CTRL_CUSTOM_TABLE_EDITOR); break;
                case 3: ev.notifyCode = CBN_SELCHANGE; ev.ctrlId = 3000 + ids[rng() % ids.size()]; break;
                default: ev.notifyCode = (int)(rng() % 0x400); ev.ctrlId = 3000 + ids[rng() % ids.size()]; break;
                }
                ev.mode = (int)(rng() % 4);
                ev.comboSel = (int)(rng() % 12) - 1;
                ev.comboItemData = (int)(rng() % 6) - 1;
                wcscpy_s(ev.editText, edits[rng() % _countof(edits)]);

                SettingsSnapshot start = RandomSnapshot(rng);
                int startGrid = (int)(rng() & 1);

                SettingsSnapshot sa = start, sb = start;
                int ga = startGrid, gb = startGrid;
                FakeHost ha(sc), hb(sc);
                ha.snap = &sa;
                hb.snap = &sb;
                ha.grid = &ga;
                hb.grid = &gb;
                SC::Cpp::Handle(ha, sa, ga, ev);
                SC::HandleRust(hb, sb, gb, ev);

                std::wstring ctx = L"iter " + std::to_wstring(iter) + L" notify " + std::to_wstring(ev.notifyCode)
                    + L" id " + std::to_wstring(ev.ctrlId);
                Assert::AreEqual(ha.log.size(), hb.log.size(), (L"log length: " + ctx).c_str());
                for (size_t i = 0; i < ha.log.size(); i++) {
                    if (ha.log[i] != hb.log[i])
                        Assert::Fail((ctx + L" call " + std::to_wstring(i) + L"\nC++:  " + ha.log[i] + L"\nRust: " + hb.log[i]).c_str());
                    if (ha.log[i].rfind(L"Msg", 0) == 0) messages++;
                    if (ha.log[i].rfind(L"ApplyAndSave", 0) == 0) applies++;
                }
                calls += (int)ha.log.size();
                Assert::IsTrue(memcmp(&sa, &sb, sizeof(sa)) == 0, (L"snapshot: " + ctx).c_str());
                Assert::AreEqual(ga, gb, (L"grid: " + ctx).c_str());
            }
            Assert::IsTrue(calls > 20000 && messages > 300 && applies > 1000,
                (L"coverage: calls " + std::to_wstring(calls) + L" messages " + std::to_wstring(messages)
                    + L" applies " + std::to_wstring(applies)).c_str());
        }
    };
}
