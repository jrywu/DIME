// SettingsPopulate.cpp — see SettingsPopulate.h. Cpp::Populate is the code
// that used to be SettingsWindow::PopulateControls; dime-settings
// (populate.rs) produces the same control operations.

#include <windows.h>
#include <strsafe.h>
#include <string>
#include "..\Define.h"
#include "..\BaseStructure.h"
#include "SettingsPopulate.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsPopulate {

namespace Cpp {

namespace {
void ComboAddAndSelect(HWND hCombo, const wchar_t* items[], int count, int sel)
{
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
    if (sel >= 0 && sel < count)
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
}

void Check(HWND h, BOOL on)
{
    SendMessage(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
} // namespace

void Populate(const Inputs& in, FindControlFn find, void* ctx)
{
    const SettingsSnapshot& s = *in.snapshot;
    HWND h;

    if ((h = find(ctx, CTRL_COLOR_MODE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        // Must match ConfigDialog.cpp: "跟隨系統模式" only on Win10 1809+
        if (in.windows1809OrLater) {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"跟隨系統模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"淡色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"深色模式");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK));
        }
        {
            int idx = (int)SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"自訂");
            SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM));
        }
        int count = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < count; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.colorMode) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0);
                break;
            }
        }
    }
    if ((h = find(ctx, CTRL_SHOW_NOTIFY))) Check(h, s.showNotifyDesktop);
    if ((h = find(ctx, CTRL_ARRAY_SCOPE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        struct { const wchar_t* text; int data; } arrayScopeItems[] = {
            { L"行列30 Big5 (繁體中文)", (int)ARRAY_SCOPE::ARRAY30_BIG5 },
            { L"行列30 Unicode Ext-A",  (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A },
            { L"行列30 Unicode Ext-AB", (int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB },
            { L"行列30 Unicode Ext-A~D",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD },
            { L"行列30 Unicode Ext-A~J",(int)ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A_TO_J },
            { L"行列40 Big5",           (int)ARRAY_SCOPE::ARRAY40_BIG5 },
        };
        for (auto& item : arrayScopeItems) {
            LRESULT idx = SendMessage(h, CB_ADDSTRING, 0, (LPARAM)item.text);
            SendMessage(h, CB_SETITEMDATA, idx, (LPARAM)item.data);
        }
        int cnt = (int)SendMessage(h, CB_GETCOUNT, 0, 0);
        for (int ci = 0; ci < cnt; ci++) {
            if ((int)SendMessage(h, CB_GETITEMDATA, (WPARAM)ci, 0) == s.arrayScope) {
                SendMessage(h, CB_SETCURSEL, (WPARAM)ci, 0); break;
            }
        }
    }
    if ((h = find(ctx, CTRL_CHARSET_SCOPE))) {
        const wchar_t* f[] = { L"完整字集", L"繁體中文" };
        ComboAddAndSelect(h, f, 2, s.big5Filter ? 1 : 0);
    }
    if ((h = find(ctx, CTRL_NUMERIC_PAD))) {
        const wchar_t* p[] = { L"數字鍵盤輸入數字符號", L"數字鍵盤輸入字根", L"僅用數字鍵盤輸入字根" };
        ComboAddAndSelect(h, p, 3, s.numericPad);
    }
    if ((h = find(ctx, CTRL_SHIFT_ENGLISH_DIGIT_MODE))) {
        const wchar_t* d[] = { L"CapsLock 關輸出符號", L"CapsLock 關輸出數字", L"永遠輸出符號" };
        ComboAddAndSelect(h, d, 3, s.shiftEnglishDigitMode);
    }
    if ((h = find(ctx, CTRL_DAYI_SELKEY_MODE))) {
        const wchar_t* k[] = { L"依碼表設定", L"數字鍵 1~0", L"大易選擇鍵 ' [ ] - \\" };
        ComboAddAndSelect(h, k, 3, s.dayiSelkeyMode);
    }
    if ((h = find(ctx, CTRL_PHONETIC_KB))) {
        const wchar_t* l[] = { L"標準鍵盤", L"倚天鍵盤" };
        ComboAddAndSelect(h, l, 2, s.phoneticKeyboardLayout);
    }
    if ((h = find(ctx, CTRL_MAX_CODES))) {
        WCHAR buf[16]; StringCchPrintfW(buf, 16, L"%u", s.maxCodes);
        SetWindowTextW(h, buf);
    }
    if ((h = find(ctx, CTRL_ARROW_KEY_SW_PAGES))) Check(h, s.arrowKeySWPages);
    if ((h = find(ctx, CTRL_SPACE_AS_PAGEDOWN))) Check(h, s.spaceAsPageDown);
    if ((h = find(ctx, CTRL_SPACE_AS_FIRST_CAND))) Check(h, s.spaceAsFirstCandSelkey);
    if ((h = find(ctx, CTRL_AUTO_COMPOSE))) Check(h, s.autoCompose);
    if ((h = find(ctx, CTRL_CLEAR_ON_BEEP))) Check(h, s.clearOnBeep);
    if ((h = find(ctx, CTRL_MAKE_PHRASE))) Check(h, s.makePhrase);
    if ((h = find(ctx, CTRL_CUSTOM_TABLE_PRIORITY))) Check(h, s.customTablePriority);
    if ((h = find(ctx, CTRL_DO_BEEP))) Check(h, s.doBeep);
    if ((h = find(ctx, CTRL_DO_BEEP_NOTIFY))) Check(h, s.doBeepNotify);
    if ((h = find(ctx, CTRL_DO_BEEP_CANDI))) Check(h, s.doBeepOnCandi);
    if ((h = find(ctx, CTRL_IME_SHIFT_MODE))) {
        const wchar_t* m[] = { L"左右SHIFT鍵", L"右SHIFT鍵", L"左SHIFT鍵", L"無(僅Ctrl-Space鍵)" };
        ComboAddAndSelect(h, m, 4, s.imeShiftMode);
    }
    if ((h = find(ctx, CTRL_DOUBLE_SINGLE_BYTE))) {
        const wchar_t* d[] = { L"以 Shift-Space 熱鍵切換", L"半型", L"全型" };
        ComboAddAndSelect(h, d, 3, s.doubleSingleByteMode);
    }
    if ((h = find(ctx, CTRL_REVERSE_CONVERSION))) {
        // The provider list is enumerated at startup and filtered by LoadConfig.
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"(無)");
        if (in.reverseIsNull)
            SendMessage(h, CB_SETCURSEL, (WPARAM)0, 0);
        for (int i = 0; i < in.reverseCount; i++) {
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)in.reverse[i].description);
            if (in.reverse[i].selected)
                SendMessage(h, CB_SETCURSEL, (WPARAM)i + 1, 0);
        }
    }
    if ((h = find(ctx, CTRL_KEYBOARD_OPEN_CLOSE))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"中文模式");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"英數模式");
        SendMessage(h, CB_SETCURSEL, s.activatedKeyboardMode ? 0 : 1, 0);
    }
    if ((h = find(ctx, CTRL_OUTPUT_CHT_CHS))) {
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"不轉換");
        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)L"繁轉簡");
        SendMessage(h, CB_SETCURSEL, s.doHanConvert ? 1 : 0, 0);
    }
    if ((h = find(ctx, CTRL_DAYI_ARTICLE))) Check(h, s.dayiArticleMode);
    if ((h = find(ctx, CTRL_SENTENCE_MODE))) Check(h, s.sentenceMode);
    if ((h = find(ctx, CTRL_ARRAY_FORCE_SP))) Check(h, s.arrayForceSP);
    if ((h = find(ctx, CTRL_ARRAY_NOTIFY_SP))) Check(h, s.arrayNotifySP);
    if ((h = find(ctx, CTRL_ARRAY_SINGLE_QUOTE))) Check(h, s.arraySingleQuoteCustomPhrase);
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(DimePopulateOp) == 28, "DimePopulateOp");

void PopulateRust(const Inputs& in, FindControlFn find, void* ctx)
{
    std::vector<const uint16_t*> descs;
    std::vector<int32_t> selected;
    for (int i = 0; i < in.reverseCount; i++) {
        descs.push_back(reinterpret_cast<const uint16_t*>(in.reverse[i].description));
        selected.push_back(in.reverse[i].selected ? 1 : 0);
    }
    size_t opCount = 0, textLen = 0;
    dime_settings_populate_ops(in.snapshot, in.windows1809OrLater ? 1 : 0, in.reverseIsNull ? 1 : 0,
        descs.data(), selected.data(), (int32_t)descs.size(), nullptr, 0, &opCount, nullptr, 0, &textLen);
    std::vector<DimePopulateOp> ops(opCount);
    std::vector<uint16_t> text(textLen + 1);
    dime_settings_populate_ops(in.snapshot, in.windows1809OrLater ? 1 : 0, in.reverseIsNull ? 1 : 0,
        descs.data(), selected.data(), (int32_t)descs.size(), ops.data(), ops.size(), &opCount,
        text.data(), text.size(), &textLen);

    for (const DimePopulateOp& op : ops) {
        HWND h = find(ctx, (SettingsControlId)op.ctrl_id);
        if (!h) continue;
        std::wstring str;
        if (op.text_len > 0 && (size_t)op.text_offset + (size_t)op.text_len <= text.size())
            str.assign(reinterpret_cast<const wchar_t*>(text.data()) + op.text_offset, (size_t)op.text_len);
        switch (op.kind) {
        case DIME_POPULATE_RESET:
            SendMessage(h, CB_RESETCONTENT, 0, 0);
            break;
        case DIME_POPULATE_ADD: {
            LRESULT idx = SendMessage(h, CB_ADDSTRING, 0, (LPARAM)str.c_str());
            if (op.has_data) SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)op.data);
            break;
        }
        case DIME_POPULATE_SELECT:
            SendMessage(h, CB_SETCURSEL, (WPARAM)op.value, 0);
            break;
        case DIME_POPULATE_CHECK:
            SendMessage(h, BM_SETCHECK, op.value ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case DIME_POPULATE_TEXT:
            SetWindowTextW(h, str.c_str());
            break;
        default:
            break;
        }
    }
}

#endif

#ifdef DIME_USE_RUST_CONFIG
void Populate(const Inputs& in, FindControlFn find, void* ctx) { PopulateRust(in, find, ctx); }
#else
void Populate(const Inputs& in, FindControlFn find, void* ctx) { Cpp::Populate(in, find, ctx); }
#endif

} // namespace SettingsPopulate
