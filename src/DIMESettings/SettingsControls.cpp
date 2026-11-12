// SettingsControls.cpp — see SettingsControls.h. Cpp::BuildSpecs follows the
// code that used to be CreateRowControl / CreateColorGridControls /
// CreateRichEditorControl; dime-settings (controls.rs) mirrors it.

#include <windows.h>
#include <richedit.h>
#include "..\BaseStructure.h"
#include "SettingsControls.h"
#include "SettingsGeometry.h"
#include "SettingsPageLayout.h"
#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)
#include "dime_ffi.h"
#endif

namespace SettingsControls {

const wchar_t* ClassName(int controlClass)
{
    switch (controlClass) {
    case CLS_COMBOBOX: return L"COMBOBOX";
    case CLS_EDIT:     return L"EDIT";
    case CLS_STATIC:   return L"STATIC";
    case CLS_RICHEDIT: return L"RICHEDIT50W";
    default:           return L"BUTTON";
    }
}

namespace Cpp {

namespace {

ControlSpec Spec(int ctrlId, int menuId, int role, int cls, DWORD exStyle, DWORD style, const wchar_t* text,
    const SettingsWindowLayout::CtrlRect& create, const SettingsWindowLayout::CtrlRect& add, int visH,
    bool setFont, int dark)
{
    ControlSpec s = {};
    s.ctrlId = ctrlId;
    s.menuId = menuId;
    s.role = role;
    s.controlClass = cls;
    s.exStyle = exStyle;
    s.style = style;
    s.hasText = text != nullptr;
    if (text) s.text = text;
    s.create = { create.x, create.y, create.w, create.h };
    s.add = { add.x, add.y, add.w, add.h };
    s.visH = visH;
    s.setFont = setFont;
    s.darkTheme = dark;
    return s;
}

// CreateRowControl
void RowControl(std::vector<ControlSpec>& out, const LayoutRow& lr, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi)
{
    SettingsWindowLayout::CtrlRect rc = {};
    if (!SettingsWindowLayout::Cpp::RowControlRect(lr.type, ctrlRight, cy, ctrlH, btnH, dpi, &rc))
        return;
    int menu = 3000 + lr.id;
    SettingsWindowLayout::CtrlRect zero = {};
    switch (lr.type) {
    case RowType::Toggle:
        out.push_back(Spec(lr.id, menu, ROLE_PLAIN, CLS_BUTTON, 0, WS_CHILD | BS_AUTOCHECKBOX, lr.label,
            zero, rc, -1, false, DARK_NONE));
        break;
    case RowType::ComboBox:
        out.push_back(Spec(lr.id, menu, ROLE_PLAIN, CLS_COMBOBOX, 0, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            nullptr, rc, rc, rc.visH, true, DARK_CFD));
        break;
    case RowType::Edit:
        out.push_back(Spec(lr.id, menu, ROLE_PLAIN, CLS_EDIT, WS_EX_CLIENTEDGE, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            nullptr, rc, rc, -1, true, DARK_CFD));
        break;
    case RowType::Button:
    case RowType::Clickable: {
        const wchar_t* btnText = (lr.action == RowAction::LoadTable) ? L"載入" :
                                 (lr.action == RowAction::OpenFontDialog) ? L"變更字型" :
                                 (lr.action == RowAction::ExportCustomTable) ? L"匯出" :
                                 (lr.action == RowAction::ImportCustomTable) ? L"匯入" :
                                 (lr.action == RowAction::ResetDefaults) ? L"還原預設值" : lr.label;
        out.push_back(Spec(lr.id, menu, ROLE_PLAIN, CLS_BUTTON, 0, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            btnText, rc, rc, -1, true, DARK_NONE));
        break;
    }
    default:
        break;
    }
}

} // namespace

std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    std::vector<ControlSpec> out;
    UINT dpi = in.dpi;
    int margin = ScaleForDpi(g_geo.contentMargin, dpi);
    int pad = ScaleForDpi(g_geo.cardPad, dpi);
    int btnW = ScaleForDpi(g_geo.buttonWidth, dpi);
    int ctrlH = ScaleForDpi(g_geo.ctrlHeight, dpi);
    int btnH = ScaleForDpi(g_geo.buttonHeight, dpi);
    bool isLevel1 = (in.navLevel == 1 && in.selectedModeIndex < 4);
    int ctrlRight = SettingsWindowLayout::Cpp::CtrlRight(margin, cardW, dpi, isLevel1);

    std::vector<SettingsWindowLayout::Placement> placements;
    SettingsWindowLayout::Cpp::PlanControls(in, ime, imeCount, load, loadCount, placements);
    const LayoutCard* cards = (in.selectedModeIndex == 5) ? load : ime;

    for (const auto& pl : placements) {
        const LayoutRow& lr = cards[pl.card].rows[pl.row];
        switch (pl.kind) {
        case 0:
            RowControl(out, lr, ctrlRight, pl.y, ctrlH, btnH, dpi);
            break;
        case 1: {
            SettingsWindowLayout::CtrlRect re = {}, save = {};
            SettingsWindowLayout::Cpp::RichEditorRects(margin, cardW, pad, pl.y, pl.h, ctrlRight, btnW, btnH, dpi, &re, &save);
            out.push_back(Spec(lr.id, 3000 + lr.id, ROLE_RICH_EDITOR, CLS_RICHEDIT, WS_EX_CLIENTEDGE,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                nullptr, re, re, -1, true, DARK_EXPLORER));
            out.push_back(Spec(CTRL_CUSTOM_TABLE_SAVE, 3000 + CTRL_CUSTOM_TABLE_SAVE, ROLE_SAVE_BUTTON, CLS_BUTTON, 0,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED, L"儲存", save, save, -1, true, DARK_NONE));
            break;
        }
        case 2:
            for (int si = 0; si < 6; si++) {
                SettingsWindowLayout::CtrlRect rc = SettingsWindowLayout::Cpp::SwatchRect(si, margin, pl.y, ctrlRight, dpi);
                out.push_back(Spec(CTRL_COLOR_FR + si, 4000 + si, ROLE_PLAIN, CLS_STATIC, 0,
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY, nullptr, rc, rc, -1, false, DARK_NONE));
            }
            break;
        default:
            break;
        }
    }
    return out;
}

} // namespace Cpp

#if defined(DIME_RUST_AVAILABLE) || defined(DIME_USE_RUST_CONFIG)

static_assert(sizeof(DimeControlSpec) == 80, "DimeControlSpec");

std::vector<ControlSpec> BuildSpecsRust(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    size_t count = 0, textLen = 0;
    dime_settings_control_specs(&in, cardW, ime, imeCount, load, loadCount, nullptr, 0, &count, nullptr, 0, &textLen);
    std::vector<DimeControlSpec> raw(count);
    std::vector<uint16_t> text(textLen + 1);
    dime_settings_control_specs(&in, cardW, ime, imeCount, load, loadCount, raw.data(), raw.size(), &count,
        text.data(), text.size(), &textLen);
    std::vector<ControlSpec> out;
    for (const DimeControlSpec& r : raw) {
        ControlSpec s = {};
        s.ctrlId = r.ctrl_id;
        s.menuId = r.menu_id;
        s.role = r.role;
        s.controlClass = r.control_class;
        s.exStyle = r.ex_style;
        s.style = r.style;
        s.hasText = r.has_text != 0;
        if (r.text_len > 0 && (size_t)r.text_offset + (size_t)r.text_len <= text.size())
            s.text.assign(reinterpret_cast<const wchar_t*>(text.data()) + r.text_offset, (size_t)r.text_len);
        s.create = { r.create[0], r.create[1], r.create[2], r.create[3] };
        s.add = { r.add[0], r.add[1], r.add[2], r.add[3] };
        s.visH = r.vis_h;
        s.setFont = r.set_font != 0;
        s.darkTheme = r.dark_theme;
        out.push_back(s);
    }
    return out;
}

#endif

#ifdef DIME_USE_RUST_CONFIG
std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    return BuildSpecsRust(in, cardW, ime, imeCount, load, loadCount);
}
#else
std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    return Cpp::BuildSpecs(in, cardW, ime, imeCount, load, loadCount);
}
#endif

} // namespace SettingsControls
