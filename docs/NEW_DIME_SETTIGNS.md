# DIMESettings Modernization Plan — Windows 11 Redesign

## Context

The DIME settings UI currently uses Win32 PropertySheet with standard dialog controls, visually stuck in the Windows XP/7 era. The candidate window has already been redesigned with Windows 11 styling (rounded corners, SDF shadow, dark mode). The settings UI should follow suit.

**Decision**: Pure Win32 + GDI owner-draw (not WinUI 3 / XAML Islands). Zero new dependencies, backward compatible to Win7, consistent with the rest of the DIME codebase.

**Current progress**: Sidebar + card layout + Mica title bar + two-level navigation + toggle switches are partially implemented. This plan rewrites the approach with proper Win11 visual fidelity, declarative layout offload, and complete feature coverage.

---

## Visual Design

### Visual Hierarchy

```
Window
├─ Sidebar (fixed left, flat list with icons)
└─ Content Area (scrollable right)
   ├─ Section Title (20pt semibold, fixed on scroll)
   └─ Cards (rounded rect, 67px per row, 3px gap, GDI+ shadow in light mode)
      └─ Rows (icon + label + desc + control)
```

**5 layers, outside-in**:

1. **Window** — title bar color matched to content bg (`DWMWA_CAPTION_COLOR`), dark mode support
2. **Sidebar** — fixed left, icons from .ico resources, accent bar on selected, hover highlight. Collapses to hamburger menu in narrow windows (below `sidebarCollapseBreak` = 740px)
3. **Content Area** — scrollable, section title fixed on scroll (repainted on top)
4. **Card** — 8px corner radius, GDI+ shadow (light mode only), hover overlay, dark border in dark mode
5. **Row** — MDL2 icon (20px) + label (10pt) + description (9pt) + control right-aligned left of chevron area

### Colors

All colors derived from `GetSysColor()` / `BlendColor()` at runtime — no hardcoded RGB. Adapts to high-contrast and custom themes automatically. Light/dark mode detected via `CConfig::IsSystemDarkTheme()`. Theme switch handled by `WM_SETTINGCHANGE`.

Key elements: content bg, card bg, card border, text primary/secondary, row hover, sidebar selected/hover, toggle ON/OFF, button fill/border/text, title bar caption color. See `SettingsGeometry.cpp:ComputeThemeColors()` for full table.

| Element | Light | Dark |
|---------|-------|------|
| Content bg | `COLOR_3DFACE` | `DARK_DIALOG_BG` |
| Card bg | `BlendColor(WINDOW, FACE, 30)` | `DARK_CONTROL_BG` |
| Card border | `BlendColor(FACE, black, 3)` | `BlendColor(cardBg, black, 40)` |
| Card shadow | GDI+ alpha=20, dark | None |
| Row hover | `BlendColor(WINDOW, black, 10)` | `BlendColor(cardBg, white, 15)` |
| Buttons | Accent fill (`COLOR_HOTLIGHT`) | Same |
| Toggle ON | Accent track, white knob (GDI+) | Same |
### Geometry (measured from Win11 at 96 DPI)

| Element | Value |
|---------|-------|
| Card height | 67px (cardPad 10 + rowHeight 49 + cardPad 10 - border) |
| Card gap | 3px |
| Card corner radius | 8px |
| Card shadow | 1px expand, 0px Y offset, alpha=20 (light only) |
| Row icon | 20px, MDL2 Assets font |
| Row icon left padding | 20px |
| Font: title | 20pt semibold |
| Font: row label | 10pt regular |
| Font: description | 9pt regular |
| Sidebar start Y | 56px (= topMargin, aligns with first card) |
| Content margin | 36px |
| Toggle | 40x20px, 12px knob, GDI+ anti-aliased |
| Chevron | MDL2 8pt bold (fallback: text `>` `∧` `∨` on Win7/8) |

All values in `SettingsGeometry.h`. DPI-scaled via `ScaleForDpi(value, dpi) = MulDiv(value, dpi, 96)`.
### Icons

Segoe MDL2 Assets glyphs (Win10+). Fallback: no icons on Win7/8 (`hasMDL2` detection). Icons colored `textPrimary`. Chevrons use bold 8pt MDL2 or text fallback.
### Icon codepoints

| Row | Glyph | Codepoint |
|-----|-------|-----------|
| 外觀設定 (card + row) | Color | U+E790 |
| 聲音與通知 (card) | Ringer | U+EA8F |
| 輸入設定 (card) | Keyboard | U+E765 |
| 自建詞庫 (card) | Edit | U+E70F |
| 字型設定 | Font | U+E8D2 |
| 色彩模式 | Color | U+E790 |
| 在桌面模式顯示浮動中英切換視窗 | QWERTYOn | U+E982 |
| 還原預設值 | Refresh | U+E72C |
| 錯誤提示音 | Ringer | U+EA8F |
| 錯誤提示音顯示通知 | Important | U+E814 |
| 候選字提示音 | Volume2 | U+E994 |
| 字集查詢範圍 | CharacterAppearance | U+E774 |
| 九宮格數字鍵盤 | KeyboardClassic | U+E75F |
| 鍵盤對應選擇 | Keyboard | U+E765 |
| 組字區最大長度 | FontSize | U+E8C1 |
| 以方向鍵換頁 | ChevronRight | U+E76C |
| 以空白鍵換頁 | Page | U+E75D |
| 空白鍵為第一選字鍵 | SelectAll | U+E8C4 |
| 打字時同步組字 | Sync | U+E8D3 |
| 錯誤組字時清除字根 | Clear | U+E894 |
| 提示聯想字詞 | Lightbulb | U+E82F |
| 自建詞優先 | DictionaryAdd | U+E82E |
| 中英切換熱鍵 | Switch | U+E8AB |
| 全半形輸入模式 | FullAlpha | U+E97F |
| 預設輸入模式 | QWERTYOff | U+E983 |
| 輸出字元 | ChinesePinyin | U+E98A |
| 反查輸入字根 | Search | U+E721 |
| 地址鍵輸入符號 | Map | U+E8AC |
| 僅接受輸入特別碼 | CheckMark | U+E73E |
| 特別碼提示 | Info | U+E7E7 |
| 以'鍵查詢自建詞庫 | LeftQuote | U+E848 |
| 大易主碼表 | Dictionary | U+E82D |
| 行列主碼表 | Dictionary | U+E82D |
| 注音主碼表 | Dictionary | U+E82D |
| 自建主碼表 | Dictionary | U+E82D |
| 聯想詞彙表 | Dictionary | U+E82D |
| 行列擴充碼表 | Library | U+E8F1 |
| 匯出自建詞庫 | Export | U+EDE1 |
| 匯入自建詞庫 | Import | U+E8B5 |

### DIME Settings Pages

**Sidebar**: 大易輸入法, 行列輸入法, 傳統注音輸入法, 自建輸入法, separator, 自定碼表

**Level 0 (IME mode page)**: 4 cards — 外觀設定 `>`, 聲音與通知 `∨` (default collapsed), 輸入設定 `∨` (default expanded), 自建詞庫 `>`

**Level 1 (sub-pages)**:
- 外觀設定: candidate preview (owner-draw), font chooser, color mode combo + expandable color grid, toggle, reset button
- 自建詞庫: RichEdit editor (owner-draw) + export/import buttons

**自定碼表**: 6 cards — 大易/行列/注音/自建/聯想 main table load buttons + 行列擴充碼表 expandable group

### Layout Specification

Complete row layout for every page, matching `SettingsPageLayout.cpp`.

#### 外觀設定 (NavigateToCard `>`)

| Row | Type | Action | Label | Description | Modes | Icon |
|-----|------|--------|-------|-------------|-------|------|
| CTRL_CANDIDATE_PREVIEW | CandidatePreview | None | 預覽 | — | ALL | — |
| CTRL_FONT_NAME | Clickable | OpenFontDialog | 字型設定 | 候選字視窗顯示字型 | ALL | U+E8D2 |
| CTRL_COLOR_MODE | ComboBox | ExpandSection | 色彩模式 | 設定候選字視窗色彩主題 | ALL | U+E790 |
| CTRL_COLOR_GRID | ColorGrid | OpenColorDialog | 色彩 | — | ALL | — |
| CTRL_RESTORE_DEFAULT | Button | ResetDefaults | 還原預設值 | 將所有外觀設定還原為預設值 | ALL | U+E72C |

#### 聲音與通知 (ExpandSection `∨`, default collapsed)

| Row | Type | Action | Label | Description | Modes | Icon |
|-----|------|--------|-------|-------------|-------|------|
| CTRL_DO_BEEP | Toggle | ToggleValue | 錯誤提示音 | 輸入錯誤字根時發出提示音 | ALL | U+EA8F |
| CTRL_DO_BEEP_NOTIFY | Toggle | ToggleValue | 錯誤提示音顯示通知 | 提示音發出時同時顯示桌面通知視窗 | ALL | U+E814 |
| CTRL_DO_BEEP_CANDI | Toggle | ToggleValue | 候選字提示音 | 顯示候選字視窗時發出提示音 | DAYI | U+E994 |

#### 輸入設定 (ExpandSection `∨`, default expanded)

| Row | Type | Action | Label | Description | Modes | Icon |
|-----|------|--------|-------|-------------|-------|------|
| CTRL_SHOW_NOTIFY | Toggle | ToggleValue | 在桌面模式顯示浮動中英切換視窗 | — | ALL | U+E982 |
| CTRL_ARRAY_SCOPE | ComboBox | None | 字集查詢範圍 | — | ARRAY | — |
| CTRL_CHARSET_SCOPE | ComboBox | None | 字集查詢範圍 | — | NON_ARRAY | — |
| CTRL_NUMERIC_PAD | ComboBox | None | 九宮格數字鍵盤 | — | ALL | — |
| CTRL_PHONETIC_KB | ComboBox | None | 鍵盤對應選擇 | — | PHONETIC | — |
| CTRL_MAX_CODES | Edit | None | 組字區最大長度 | — | ALL | — |
| CTRL_ARROW_KEY_SW_PAGES | Toggle | ToggleValue | 以方向鍵換頁 | — | ALL | — |
| CTRL_SPACE_AS_PAGEDOWN | Toggle | ToggleValue | 以空白鍵換頁 | — | ALL | — |
| CTRL_SPACE_AS_FIRST_CAND | Toggle | ToggleValue | 空白鍵為第一選字鍵 | — | GENERIC | — |
| CTRL_AUTO_COMPOSE | Toggle | ToggleValue | 打字時同步組字 | — | ALL | — |
| CTRL_CLEAR_ON_BEEP | Toggle | ToggleValue | 錯誤組字時清除字根 | — | ALL | — |
| CTRL_MAKE_PHRASE | Toggle | ToggleValue | 提示聯想字詞 | — | ALL | — |
| CTRL_CUSTOM_TABLE_PRIORITY | Toggle | ToggleValue | 自建詞優先 | — | ALL | — |
| CTRL_IME_SHIFT_MODE | ComboBox | None | 中英切換熱鍵 | — | ALL | — |
| CTRL_DOUBLE_SINGLE_BYTE | ComboBox | None | 全半形輸入模式 | — | ALL | — |
| CTRL_KEYBOARD_OPEN_CLOSE | ComboBox | None | 預設輸入模式 | — | ALL | — |
| CTRL_OUTPUT_CHT_CHS | ComboBox | None | 輸出字元 | — | ALL | — |
| CTRL_REVERSE_CONVERSION | ComboBox | None | 反查輸入字根 | — | ALL | — |
| CTRL_DAYI_ARTICLE | Toggle | ToggleValue | 地址鍵輸入符號 | — | DAYI | — |
| CTRL_ARRAY_FORCE_SP | Toggle | ToggleValue | 僅接受輸入特別碼 | — | ARRAY | — |
| CTRL_ARRAY_NOTIFY_SP | Toggle | ToggleValue | 特別碼提示 | — | ARRAY | — |
| CTRL_ARRAY_SINGLE_QUOTE | Toggle | ToggleValue | 以'鍵查詢自建詞庫 | — | ARRAY | — |

### Combo Options Specification

Each ComboBox control's dropdown items, matching `PopulateControls()` in SettingsWindow.cpp:

**CTRL_COLOR_MODE** (色彩模式):

| Index | Label | Value | Note |
|-------|-------|-------|------|
| 0* | 跟隨系統模式 | IME_COLOR_MODE_SYSTEM (0) | *Win10 1809+ only |
| 1 | 淡色模式 | IME_COLOR_MODE_LIGHT (1) | |
| 2 | 深色模式 | IME_COLOR_MODE_DARK (2) | |
| 3 | 自訂 | IME_COLOR_MODE_CUSTOM (3) | Expands color grid |

**CTRL_ARRAY_SCOPE** (字集查詢範圍 — ARRAY only):

| Index | Label | Value |
|-------|-------|-------|
| 0 | 行列30 Big5 (繁體中文) | ARRAY30_BIG5 |
| 1 | 行列30 Unicode Ext-A | ARRAY30_UNICODE_EXT_A |
| 2 | 行列30 Unicode Ext-AB | ARRAY30_UNICODE_EXT_AB |
| 3 | 行列30 Unicode Ext-A~D | ARRAY30_UNICODE_EXT_ABCD |
| 4 | 行列30 Unicode Ext-A~J | ARRAY30_UNICODE_EXT_A_TO_J |
| 5 | 行列40 Big5 | ARRAY40_BIG5 |

**CTRL_CHARSET_SCOPE** (字集查詢範圍 — NON_ARRAY):

| Index | Label | Snapshot value |
|-------|-------|---------------|
| 0 | 完整字集 | big5Filter = false |
| 1 | 繁體中文 | big5Filter = true |

**CTRL_NUMERIC_PAD** (九宮格數字鍵盤):

| Index | Label |
|-------|-------|
| 0 | 數字鍵盤輸入數字符號 |
| 1 | 數字鍵盤輸入字根 |
| 2 | 僅用數字鍵盤輸入字根 |

**CTRL_PHONETIC_KB** (鍵盤對應選擇 — PHONETIC only):

| Index | Label |
|-------|-------|
| 0 | 標準鍵盤 |
| 1 | 倚天鍵盤 |

**CTRL_IME_SHIFT_MODE** (中英切換熱鍵):

| Index | Label |
|-------|-------|
| 0 | 左右SHIFT鍵 |
| 1 | 右SHIFT鍵 |
| 2 | 左SHIFT鍵 |
| 3 | 無(僅Ctrl-Space鍵) |

**CTRL_DOUBLE_SINGLE_BYTE** (全半形輸入模式):

| Index | Label |
|-------|-------|
| 0 | 以 Shift-Space 熱鍵切換 |
| 1 | 半型 |
| 2 | 全型 |

**CTRL_KEYBOARD_OPEN_CLOSE** (預設輸入模式):

| Index | Label | Snapshot value |
|-------|-------|---------------|
| 0 | 中文模式 | activatedKeyboardMode = true |
| 1 | 英數模式 | activatedKeyboardMode = false |

**CTRL_OUTPUT_CHT_CHS** (輸出字元):

| Index | Label | Snapshot value |
|-------|-------|---------------|
| 0 | 繁體中文 | doHanConvert = false |
| 1 | 簡體中文 | doHanConvert = true |

**CTRL_REVERSE_CONVERSION** (反查輸入字根):

| Index | Label | Note |
|-------|-------|------|
| 0 | (無) | CLSID_NULL |
| 1+ | (dynamic) | Enumerated from TSF ITfInputProcessorProfile, self filtered by SetIMEMode |

#### 自建詞庫 (NavigateToCard `>`)

| Row | Type | Action | Label | Description | Modes | Icon |
|-----|------|--------|-------|-------------|-------|------|
| CTRL_CUSTOM_TABLE_EDITOR | RichEditor | None | 自訂碼表 | 每行格式：輸入碼 空白 詞彙 | ALL | — |
| CTRL_CUSTOM_TABLE_EXPORT | Button | ExportCustomTable | 匯出自建詞庫 | 匯出自建詞庫至檔案 | ALL | U+EDE1 |
| CTRL_CUSTOM_TABLE_IMPORT | Button | ImportCustomTable | 匯入自建詞庫 | 從檔案匯入（覆蓋現有內容） | ALL | U+E8B5 |

#### 自定碼表 (separate page)

| Card | Description | Action | Icon |
|------|-------------|--------|------|
| 大易主碼表 | 載入或更新大易輸入法主碼表 | None (button) | U+E82D |
| 行列主碼表 | 載入或更新行列輸入法主碼表 | None (button) | U+E82D |
| 行列擴充碼表 | 特碼、簡碼、ExtB/CD/EFG、行列40、行列詞庫 | ExpandSection `∨` | U+E8F1 |
| 注音主碼表 | 載入或更新注音輸入法主碼表 | None (button) | U+E82D |
| 自建主碼表 | 載入或更新自建輸入法主碼表 | None (button) | U+E82D |
| 聯想詞彙表 | 載入所有輸入法共用的聯想詞彙表 | None (button) | U+E82D |

**行列擴充碼表 child rows** (expanded under parent):

| Row | Label |
|-----|-------|
| CTRL_LOAD_ARRAY_SP | 特別碼碼表  |
| CTRL_LOAD_ARRAY_SC | 簡碼碼表 |
| CTRL_LOAD_ARRAY_EXT_B | Unicode Ext. B 碼表 |
| CTRL_LOAD_ARRAY_EXT_CD | Unicode Ext. CD 碼表 |
| CTRL_LOAD_ARRAY_EXT_E_TO_J | Unicode Ext. E-J 碼表 |
| CTRL_LOAD_ARRAY40 | 行列40碼表 |
| CTRL_LOAD_ARRAY_PHRASE | 行列詞庫碼表 |

### Card Behaviors

**Shadow**: Light mode only. GDI+ rounded rect, 1px expand, 0px Y offset, alpha=20, dark color. No shadow in dark mode — dark border (`BlendColor(cardBg, black, 40)`) provides edge definition instead.

**Hover**: `RoundRect` overlay with `rowHover` color on the hovered card. For expanded cards (Level 0), hover applies only to the parent row area, not children. Tracked via `WM_MOUSEMOVE` → `HitTestCardList()` / `HitTestRowCards()`.

**Chevrons**: MDL2 glyphs `>` (U+E76C), `∨` (U+E70D), `∧` (U+E70E) drawn at the right edge of the card. Bold 8pt font. Win7/8 fallback: text characters `>` `∨` `∧`. All controls (combo, button, toggle, swatch) are right-aligned LEFT of the chevron area.

**Row separators**: Expanded child rows inside a card have 1px separator lines spanning full card width. Color: `BlendColor(cardBg, textSecondary, 40)` — darker than card border.

**Section title cover**: The section title area is repainted ON TOP of scrolled cards to prevent cards from showing through when scrolling.

### Responsive Sidebar

When the window width drops below `sidebarCollapseBreak` (740px at 96 DPI), the sidebar collapses:

- **Hamburger button** replaces the sidebar. In Level 0, drawn inside the content area at `(0, 0)` within a `hamburgerSize × hamburgerSize` rect. In Level 1, drawn in the breadcrumb strip (main window area above content area). Icon: MDL2 `U+E700` (GlobalNavigationButton), fallback: `≡` (U+2261) for Win7/8.
- **Section title** shifts right to `hamburgerSize` (36px) to sit beside the hamburger, vertically centered within the hamburger rect.
- **Breadcrumb** (Level 1) also starts at `hamburgerSize` instead of `contentMargin`.
- **Sidebar overlay** opens on hamburger click, painted on top of the content area (and breadcrumb strip in Level 1) using `PaintSidebar()` with a clip region. In Level 1, the content area DC origin is shifted by `-bcH` so sidebar item positions align with main window coordinates.
- **Auto-collapse** on sidebar item selection or click outside the sidebar area.
- **Hover tracking** in overlay mode adjusts Y coordinates by `bcH` offset for Level 1.

State tracked via `WindowData::sidebarCollapsed` (true = hamburger visible, sidebar hidden) and `WindowData::sidebarNarrowMode` (true = window is narrow).

### Row Behaviors

**Toggle click**: Entire row is clickable — `HandleCardListClick()` / `HandleRowCardClick()` dispatches toggle flip via `SyncToggleToSnapshot()` + `ApplyAndSave()`.

**Expand/collapse**: Cards with `RowAction::ExpandSection` toggle `cardExpanded[i]` on click. Level 0: rebuilds content. Level 1 color mode: toggles `colorGridExpanded` + shows/hides swatch grid.

**Font chooser**: Clicking the font row opens `ChooseFontW` dialog. After selection, `RebuildContentArea()` is called to reposition controls for the new preview height.

**Color picker**: Clicking a color swatch opens `ChooseColorW` dialog. Updates snapshot + `SaveColorsForMode()` + invalidates swatch control.

**Color mode change**: Loads new palette via `CConfig::LoadColorsForMode()`, copies `colors[]` into snapshot for preview rendering. Does NOT call `ApplyToConfig` for color fields (preserves custom palette).

### Custom Owner-Draw Cards

Three `RowType` values use type-specific rendering instead of the generic row renderer:

**`CandidatePreview`**: Candidate window + notify window preview with SDF shadow. Transparent card background (no `RoundRect`). Shadow uses per-pixel alpha blending via `AlphaBlend()` — same algorithm as `ShadowWindow.cpp`. Left padding via `rowIconPadLeft`. Height is font-dependent (dynamic).

**`ColorGrid`**: 3x2 grid of labeled color swatches. Labels right-aligned to column edges (3 equal columns), swatch right-aligned within each column. Label width measured from 5-char reference string. Swatches are real `SS_OWNERDRAW | SS_NOTIFY` static controls — click opens `ChooseColorW`. Only visible when `colorMode == CUSTOM && colorGridExpanded`.

**`RichEditor`**: Multi-line `RICHEDIT50W` control for custom phrase editing. Card header (label + description) painted by renderer, editor content handled by RichEdit. Real-time format validation with 3-level color highlighting (magenta/orange/red). Dark mode: `EM_SETBKGNDCOLOR` + `CHARFORMAT2W` for text color.

For rendering implementation details see [docs/LAYOUT_RENDERER.md](LAYOUT_RENDERER.md).

### ThemedMessageBox

Win32 `MessageBoxW` does not support dark mode on any Windows version (confirmed: no official API exists, `SetPreferredAppMode` does not affect it, `TaskDialogIndirect` also lacks native dark mode without Detours hooks). See [dotnet/winforms#11896](https://github.com/dotnet/winforms/issues/11896), [WindowsAppSDK#41](https://github.com/microsoft/ProjectReunion/issues/41).

`ThemedMessageBox` is a custom owner-drawn replacement in `SettingsWindow.cpp`:

- **Colors**: reads `ComputeThemeColors(isDark)` at creation time — same `ThemeColors` struct as the rest of the UI. Automatically correct for both light and dark themes.
- **Title bar**: dark via `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` when dark theme active.
- **Buttons**: owner-drawn `RoundRect` with `buttonFill` / `buttonFillHover` / `buttonBorder` from `ThemeColors`. Default button highlighted with `accent` border.
- **Icon**: system icons via `LoadIcon(NULL, IDI_WARNING/ERROR/QUESTION/INFORMATION)`.
- **Modal**: disables parent, runs own message loop, re-enables parent on close.
- **Keyboard**: Enter = default button, Escape = Cancel/No/default, X button = Cancel.
- **Fallback**: if `CreateWindowExW` fails, falls back to standard `MessageBoxW`.
- **Button labels**: localized — 確定, 取消, 是(Y), 否(N).
- **Compatible**: Windows 7+, no undocumented APIs, no external dependencies.

---

## Architecture

### MVC Architecture

```
┌─────────────────────────────────────────────────┐
│  Model: CConfig (persistence layer)              │
│  ─ Config.h / Config.cpp                         │
│  ─ LoadConfig / WriteConfig / Get* / Set*        │
│  ─ INI file read/write                           │
│  ─ EnumerateReverseConversionProviders()         │
│  ─ SetReverseConversionSelection()               │
└──────────────┬──────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────┐
│  Model: SettingsSnapshot (data struct)            │
│  ─ Defined in SettingsController.h               │
│  ─ In-memory copy of all settings values         │
│  ─ Bridge between CConfig and UI controls        │
└──────────────┬──────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────┐
│  Controller: SettingsController.h/cpp             │
│  ─ LoadFromConfig(): CConfig → SettingsSnapshot  │
│  ─ ApplyToConfig(): SettingsSnapshot → CConfig   │
│  ─ SyncToggleToSnapshot(): toggle ID → snapshot  │
│  ─ SyncComboToSnapshot(): combo sel → snapshot   │
│  ─ ValidateLine(): per-line 3-level validation   │
│  ─ CreateEngine()/DestroyEngine(): pEngine mgmt  │
│  ─ IndexToMode()/ModeToIndex(): enum conversion  │
│  ─ ParseMaxCodes(): text → validated UINT        │
│  ─ GetLoadTableSuffix(): ctrl ID → .cin suffix   │
│  ─ GetCustomTableTxtPath/CINPath(): file paths   │
│  ─ No HWND, no GDI — fully unit-testable         │
└──────────────┬──────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────┐
│  View: SettingsPageLayout.h/cpp (layout data)    │
│  ─ LayoutCard[]: card → row tree structure       │
│  ─ LayoutRow[]: row type, action, expand parent  │
│  ─ Defines WHAT goes WHERE (like .rc)            │
│  ─ Adding a setting = adding data, not code      │
└──────────────┬──────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────┐
│  View: SettingsWindow.cpp (renderer + input)     │
│  ─ Walks layout tree to paint cards/rows         │
│  ─ Forwards user input to Controller             │
│  ─ Generic renderer — no per-control logic       │
│  ─ PaintCardList, PaintSettingsDetail, etc.      │
│  ─ EN_CHANGE → calls Controller validation       │
│  ─ Applies CHARFORMAT2 from validation results   │
└──────────────┬──────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────┐
│  View: SettingsGeometry.h/cpp                    │
│  ─ All pixel constants (g_geo)                   │
│  ─ ThemeColors + ComputeThemeColors()            │
│  ─ Numbers and colors only                       │
└─────────────────────────────────────────────────┘
```

#### Controller refactoring (completed)

**Logic moved from SettingsWindow.cpp to SettingsController.cpp:**

| Logic | Status | Controller method |
|-------|--------|-------------------|
| `SyncToggleToSnapshot()` | Done | `SettingsModel::SyncToggleToSnapshot()` |
| `ValidateLine()` (per-line 3-level check) | Done | `SettingsModel::ValidateLine()` |
| `IndexToMode()` / `ModeToIndex()` | Done | `SettingsModel::IndexToMode()` / `ModeToIndex()` |
| `SyncComboToSnapshot()` | Done | `SettingsModel::SyncComboToSnapshot()` |
| `ParseMaxCodes()` | Done | `SettingsModel::ParseMaxCodes()` |
| `GetLoadTableSuffix()` | Done | `SettingsModel::GetLoadTableSuffix()` |
| `GetCustomTableTxtPath()` / `CINPath()` | Done | `SettingsModel::GetCustomTableTxtPath()` / `GetCustomTableCINPath()` |
| Composition engine lifecycle | Done | `SettingsModel::CreateEngine()` / `DestroyEngine()` |
| `PopulateControls()` | Pending | Still in SettingsWindow.cpp |
| `ApplyAndSave()` | Pending | Still in SettingsWindow.cpp |
| Combo item definitions | Pending | Still in SettingsWindow.cpp |

**Realtime format checking** for custom phrase editor (RichEdit):
- **Controller** (`SettingsController`): `SettingsModel::ValidateLine()` validates each line — 3-level severity, returns `LineValidation` with a vector of `LineErrorEntry`:
  - Level 1 (`LineError::Format`): format error — regex `key + whitespace + value` mismatch
  - Level 2 (`LineError::KeyTooLong`): key exceeds `MAX_KEY_LENGTH` (phonetic) or `maxCodes` (others)
  - Level 3 (`LineError::InvalidChar`): invalid character — uses `CCompositionProcessorEngine::ValidateCompositionKeyCharFull()` when engine available, falls back to basic range check. Reports ALL invalid chars (not just first).
  - Quick-accept shortcut: all-ASCII-letter keys skip Level 3 (same as `ConfigDialog.cpp`)
- **View** (`SettingsWindow.cpp`): `ValidateCustomTableRE()` reads RichEdit lines, calls `ValidateLine()` per line, applies `CHARFORMAT2W` error colors. View only knows about colors, not validation rules.
- **Engine** (`WindowData::pEngine`): created by `SettingsModel::CreateEngine(mode)` on mode switch, stored in `WindowData`, passed to `ValidateLine()`. Same setup as legacy `DialogContext::pEngine`.

**Reverse conversion provider enumeration** (completed refactor, see `docs/REVERSE_CONV_REFACTOR.md`):
- **Model** (`CConfig`): `EnumerateReverseConversionProviders(LANGID)` — enumerates installed TSF IMEs that support `ITfReverseConversion`. `SetReverseConversionSelection(UINT sel)` — saves selected provider's CLSID/GUID/description.
- **Controller**: reads `CConfig::GetReverseConvervsionInfoList()` to populate combo items, maps combo selection index to `SetReverseConversionSelection()` call.
- **View**: creates combo, forwards `CBN_SELCHANGE` to controller.

**Candidate/Notify preview as owner-draw card**:

Currently the candidate and notify window previews (外觀設定 sub-page) are rendered inline in `PaintSettingsDetail` with hardcoded paint logic — outside the card/row layout system. When the preview height changes (e.g., user picks a larger font), all cards below must be manually repositioned.

Target: make the preview an owner-draw card via a new `RowType::CandidatePreview`:
- **Layout** (`SettingsPageLayout.cpp`): add a `RowType::CandidatePreview` row as the first row in `s_appearanceRows`. The renderer treats it as a card whose height is dynamic.
- **Controller**: `CalcCandidatePreviewH()` returns the preview height based on current font settings in the snapshot. Pure calculation — no GDI.
- **View**: the generic renderer calls `CalcCandidatePreviewH()` to get the card height, then dispatches to a `PaintCandidatePreview()` function for the actual GDI drawing. Cards below automatically get correct Y positions because the renderer walks the layout tree sequentially.

Benefits:
- Preview height changes automatically reflow all cards below — no manual repositioning
- Preview is just another row type — no special-case code in the renderer's main loop
- `cachedPreviewH` hack becomes unnecessary — the renderer always computes height from the layout

### Declarative Layout Offload

**Principle: one struct, one file, one entry per control.** The layout file is the single source of truth for every control — what it looks like, what it does, where it appears, who can see it. The renderer never names a specific control or hardcodes a label/color/visibility rule.

#### File responsibilities

| File | Role | Responsibility | Never contains |
|------|------|---------------|----------------|
| `CConfig` (`Config.h/cpp`) | Model | Data persistence: INI read/write, `LoadConfig`/`WriteConfig`, `EnumerateReverseConversionProviders`, `SetReverseConversionSelection` | UI logic, GDI, HWND |
| `SettingsPageLayout.h/cpp` | View (layout data) | Declarative layout: `LayoutCard[]`, `LayoutRow[]`, sidebar items. Each row carries `id`, `type`, `action`, `expandParent`, `label`, `description`, `visibleModes`, `defaultExpanded`, `iconGlyph`. Like `.rc` files. | Binding code, GDI, HWND |
| `SettingsController.h/cpp` | Controller | `SettingsSnapshot` struct, `LoadFromConfig()`, `ApplyToConfig()`, `SyncToggleToSnapshot()`, `SyncComboToSnapshot()`, `ValidateLine()` (3-level), `CreateEngine()`/`DestroyEngine()`, `IndexToMode()`/`ModeToIndex()`, `ParseMaxCodes()`, `GetLoadTableSuffix()`, `GetCustomTableTxtPath()`/`CINPath()`. Pure `CConfig` ↔ UI bridge. No HWND — unit-testable. | Labels, descriptions, visibility rules, icon glyphs, pixel values |
| `SettingsGeometry.h/cpp` | View (constants) | All pixel constants (`g_geo`) + `ThemeColors` + `ComputeThemeColors()`. Numbers and colors only. | Control definitions, labels, binding code |
| `SettingsWindow.cpp` | View (renderer) | Generic renderer: walks layout tree, paints cards/rows, forwards user input to controller. Applies `CHARFORMAT2` from validation results. | Specific control names, hardcoded labels/colors, business logic |

#### Data Structures

```cpp
// SettingsPageLayout.h

// RowType determines both rendering and control creation.
// Standard types: generic renderer handles paint + control creation uniformly.
// Custom types: renderer delegates to type-specific owner-draw functions.
enum class RowType {
    // Standard types — handled by generic PaintRow() + CreateRowControl()
    Toggle,           // hidden BS_AUTOCHECKBOX + painted toggle graphic
    ComboBox,         // CBS_DROPDOWNLIST HWND
    Edit,             // ES_AUTOHSCROLL HWND
    Clickable,        // BS_OWNERDRAW button (e.g., font chooser)
    Button,           // BS_OWNERDRAW button (e.g., 載入, 匯出)
    // Custom types — delegate to owner-draw functions
    ColorGrid,        // 3x2 swatch grid: PaintColorGrid() + CreateColorGridControls()
    RichEditor,       // multi-line editor: PaintRichEditorHeader() + CreateRichEditorControl()
    CandidatePreview, // candidate+notify preview: PaintCandidatePreviewCard(), no HWND
};

enum class RowAction {
    None, ToggleValue, OpenFontDialog, OpenColorDialog,
    ExpandSection,       // ∨ — expand/collapse children in-place
    NavigateToCard,      // > — push sub-page
    LoadTable,           // [載入] button
    SaveCustomTable,     // [儲存] button
    ExportCustomTable,   // [匯出] button
    ImportCustomTable,   // [匯入] button
    ResetDefaults,       // [還原預設值]
};

struct LayoutRow {
    SettingsControlId id;           // enum — the data binding key
    RowType type;                   // how to render (standard or custom)
    RowAction action;               // what happens on click
    SettingsControlId expandParent; // CTRL_NONE = always visible
    COLORREF iconColor;             // fallback icon color (0 = use iconGlyph)
    const wchar_t* label;           // primary display text
    const wchar_t* description;     // secondary text (nullable)
    int visibleModes;               // bitmask: SM_MODE_DAYI | SM_MODE_ARRAY | ...
    bool cardBreakBefore;           // true = start a new card before this row
    wchar_t iconGlyph;              // Segoe MDL2 Assets codepoint (0 = use iconColor)
};

struct LayoutCard {
    const wchar_t* title;
    const wchar_t* description;
    RowAction action;               // NavigateToCard or ExpandSection
    const LayoutRow* rows;
    int rowCount;
    COLORREF iconColor;             // fallback icon color
    wchar_t iconGlyph;              // Segoe MDL2 Assets codepoint
    bool defaultExpanded;           // ExpandSection: true = start expanded
};
```

#### Layout Definition

```cpp
// SettingsPageLayout.cpp — THE single source of truth
// To add a setting: add one line here + one snapshot field + two lines of binding code.
// Zero renderer changes.

static const LayoutRow s_appearanceRows[] = {
//  id                   type             action                parent          iconColor label              description                      modes        cardBreak iconGlyph
    { CTRL_CANDIDATE_PREVIEW, RowType::CandidatePreview, RowAction::None, CTRL_NONE, 0, L"預覽",            nullptr,                         SM_MODE_ALL, true,  0         },
    { CTRL_FONT_NAME,       RowType::Clickable, RowAction::OpenFontDialog,  CTRL_NONE, 0, L"字型設定",      L"候選字視窗顯示字型",            SM_MODE_ALL, true,  L'\xE8D2' },
    { CTRL_COLOR_MODE,      RowType::ComboBox,  RowAction::ExpandSection,   CTRL_NONE, 0, L"色彩模式",      L"設定候選字視窗色彩主題",        SM_MODE_ALL, false, L'\xE790' },
    { CTRL_COLOR_GRID,      RowType::ColorGrid, RowAction::OpenColorDialog, CTRL_COLOR_MODE, 0, L"色彩",    nullptr,                         SM_MODE_ALL, false, 0         },
    { CTRL_SHOW_NOTIFY,     RowType::Toggle,    RowAction::ToggleValue,     CTRL_NONE, 0, L"在桌面模式顯示浮動中英切換視窗", nullptr,        SM_MODE_ALL, false, L'\xE9B4' },
    { CTRL_RESTORE_DEFAULT, RowType::Button,    RowAction::ResetDefaults,   CTRL_NONE, 0, L"還原預設值",    L"將所有外觀設定還原為預設值",    SM_MODE_ALL, false, L'\xE72C' },
};

// ... s_soundRows[], s_inputSettingsRows[], s_customTableRows[] ...

static const LayoutCard s_imePageLayout[] = {
    { L"外觀設定",   L"字型、色彩、候選字視窗樣式",
      RowAction::NavigateToCard, s_appearanceRows, kAppearanceRowCount, 0, L'\xE790', false },
    { L"聲音與通知", L"提示音、錯誤通知",
      RowAction::NavigateToCard, s_soundRows,      kSoundRowCount,      0, L'\xEA8F', false },
    { L"輸入設定",   L"字集、組字、候選字、切換、輸入法專屬選項",
      RowAction::ExpandSection,  s_inputSettingsRows, kInputSettingsRowCount, 0, L'\xE765', true },
    { L"自建詞庫",   L"自建詞庫編輯、匯入匯出",
      RowAction::NavigateToCard, s_customTableRows, kCustomTableRowCount, 0, L'\xE70F', false },
};
```

#### Data Binding

Binding lives in `SettingsModel.cpp`. The control `id` (an enum) is the binding key — no string keys.

```cpp
// SettingsModel.cpp — pure CConfig bridge, no labels/visibility/presentation

SettingsSnapshot SettingsModel::LoadFromConfig() {
    SettingsSnapshot snap = {};
    snap.autoCompose = CConfig::IsAutoCompose();
    snap.fontSize    = CConfig::GetFontSize();
    // ... one line per field
    return snap;
}

void SettingsModel::ApplyToConfig(const SettingsSnapshot& snap) {
    CConfig::SetAutoCompose(snap.autoCompose);
    CConfig::SetFontSize(snap.fontSize);
    // ... one line per field
}
```

The renderer triggers binding via the control `id`:

```
UI event (combo change / toggle click)
  → renderer reads id from LayoutRow
  → updates SettingsSnapshot field
  → calls ApplyToConfig() + CConfig::WriteConfig()
```

#### Generic Renderer

The renderer dispatches on `RowType` through unified functions. Standard types share one code path; custom types delegate to owner-draw functions:

```
PaintRow(row)
  ├─ Standard (Toggle/ComboBox/Edit/Button/Clickable):
  │    Icon → Label → Description → Toggle graphic or chevron
  │    (HWND controls painted by Windows / WM_DRAWITEM)
  └─ Custom:
       CandidatePreview → PaintCandidatePreviewCard()
       ColorGrid        → PaintColorGrid() (labels only; swatches are HWND)
       RichEditor       → PaintRichEditorHeader() (editor is HWND)

CreateRowControl(row)
  ├─ Standard: creates appropriate Win32 HWND (BUTTON/COMBOBOX/EDIT)
  └─ Custom:
       ColorGrid  → CreateColorGridControls() (6 SS_OWNERDRAW statics)
       RichEditor → CreateRichEditorControl() (RICHEDIT50W + file load)
       CandidatePreview → no controls (pure paint)

CalcRowHeight(row)
  ├─ Standard: rowHeight or rowHeightSingle
  └─ Custom: type-specific (font-dependent, swatch grid, editor height)
```

For full rendering chain details, see [docs/LAYOUT_RENDERER.md](LAYOUT_RENDERER.md).

#### How to add a new combobox control

Four steps, zero renderer changes:

1. **Add enum id** in `SettingsModel.h`:
   ```cpp
   CTRL_MY_NEW_COMBO,
   ```

2. **Add one line** in `SettingsPageLayout.cpp`:
   ```cpp
   { CTRL_MY_NEW_COMBO, RowType::ComboBox, RowAction::None, CTRL_NONE, RGB(0,150,136),
     L"顯示名稱", L"說明文字", SM_MODE_ALL },
   ```

3. **Add field** to `SettingsSnapshot`:
   ```cpp
   int myNewComboValue;
   ```

4. **Add binding** in `SettingsModel.cpp`:
   ```cpp
   // LoadFromConfig():
   snap.myNewComboValue = CConfig::GetMyNewValue();
   // ApplyToConfig():
   CConfig::SetMyNewValue(snap.myNewComboValue);
   ```

#### What gets deleted (vs old design)

| Old artifact | Why deleted |
|-------------|-------------|
| `ControlDef` struct in `SettingsModel.h` | Merged into `LayoutRow` |
| `s_controls[]` in `SettingsModel.cpp` | Moved to layout row arrays |
| `SettingsControlType` enum | Redundant with `RowType` |
| `CardDef` struct | Redundant with `LayoutCard` |
| `keyName` field | Dead code — binding is by enum `id`, not string |
| `cardIndex` field | Dead code — `LayoutCard` tree provides grouping |
| `SettingsLayout::` namespace | Dead code — replaced by `g_geo` |

#### Action Binding (buttons and clickable rows)

Buttons and clickable rows don't bind to data — they trigger actions. `LayoutRow.action` is the binding key, not the control id.

**Two types of binding in the system:**

| Binding type | Key | Where it lives | Example |
|-------------|-----|----------------|---------|
| **Data binding** | `SettingsControlId` enum | `SettingsModel.cpp` `LoadFromConfig()`/`ApplyToConfig()` — `switch(id)` → typed snapshot field | Toggle changes `snap.autoCompose`, combo changes `snap.colorMode` |
| **Action binding** | `RowAction` enum | Renderer `WM_COMMAND BN_CLICKED` — `switch(row->action)` → action handler | `[載入]` opens file dialog, `[變更字型]` opens `ChooseFontW` |

**The renderer dispatches by action, never by control id:**

```cpp
case BN_CLICKED: {
    SettingsControlId id = (SettingsControlId)(ctrlId - 3000);
    const LayoutRow* row = FindRowInLayout(id);  // walks layout tree
    if (!row) break;

    switch (row->action) {
        case RowAction::OpenFontDialog:    DoOpenFont(wd); break;
        case RowAction::LoadTable:         DoLoadTable(wd, id); break;
        case RowAction::SaveCustomTable:   DoSave(wd); break;
        case RowAction::ExportCustomTable: DoExport(wd); break;
        case RowAction::ImportCustomTable: DoImport(wd); break;
        case RowAction::ResetDefaults:     DoResetDefaults(wd); break;
        case RowAction::ExpandSection:     DoExpandToggle(wd, id); break;
    }
}
```

**Expandable rows (∨/∧) are NOT buttons.** They are `RowType::Clickable` with `RowAction::ExpandSection`. The renderer draws a `∨`/`∧` chevron and handles click-to-expand — no Win32 BUTTON is created. This applies to both 輸入設定 on the IME page and 行列擴充碼表 on the 自定碼表 page.

**Adding a new button row:**

1. Pick an existing `RowAction` (or add a new one if genuinely new action type)
2. Add one line to the layout array with that action
3. Add handler case in the renderer's action dispatch switch

Zero data binding code needed — buttons don't touch `SettingsSnapshot`.

### Geometry Constants

All pixel values in one struct (compiled, not runtime-loaded). **No hardcoded colors** — colors come from system theme APIs at runtime (see Visual Design > Colors).

```cpp
struct SettingsGeometry {
    // Sidebar
    int sidebarWidth=280, sidebarItemH=40, sidebarItemR=4;
    int sidebarIconSize=20, sidebarIconGap=10;
    int sidebarAccentBarW=3, sidebarAccentBarH=16, sidebarAccentBarR=2;
    int sidebarCollapseBreakpoint=680;  // below this width, sidebar collapses to hamburger
    // Content
    int contentMargin=36, topMargin=56, breadcrumbH=44;
    // Card
    int cardRadius=8, cardPad=16, cardGap=8;
    // Row
    int rowHeight=56, rowHeightSingle=44, categoryRowH=60;
    int rowIconSize=24, rowIconGap=14, rowSeparatorIndent=54;
    // Toggle
    int toggleW=44, toggleH=22, toggleKnob=16, toggleKnobBorderW=2;
    // Controls
    int comboWidth=160, editWidth=120, buttonWidth=120;
    int swatchSize=28, swatchRadius=4;
    // Font (pt)
    int fontTitle=20, fontCardHeader=14, fontBody=14, fontDesc=12;
    // RichEdit editor (自建詞庫)
    int editorCardHeight=400;
    // Window
    int windowMinW=700, windowMinH=450;
};
extern const SettingsGeometry g_geo;
```

### Theme Colors

**No COLORREF constants in geometry.** All colors derived from system APIs at runtime:

```cpp
struct ThemeColors {
    COLORREF contentBg, cardBg, cardBorder;
    COLORREF textPrimary, textSecondary;
    COLORREF rowSeparator, rowHover;
    COLORREF sidebarBg, sidebarHover, sidebarSelect;
    COLORREF accent;        // from DwmGetColorizationColor / COLOR_HIGHLIGHT
    COLORREF toggleOnTrack; // same as accent
    COLORREF toggleOffBorder;
};

// Called on init and WM_THEMECHANGED:
ThemeColors ComputeThemeColors(bool isDark);
// Uses GetSysColor(), DwmGetColorizationColor(), BlendColor()
```

### Single-Instance + Ctrl+\ Integration

- All launch paths (exe, Ctrl+\, context menu) converge to one DIMESettings.exe window
- Second instance sends `WM_COPYDATA` with `--mode` to switch sidebar selection
- `CDIME::Show()` launches `DIMESettings.exe --mode <mode>` via `CreateProcessW`. Controlled by `s_useLegacyUI` flag in `ShowConfig.cpp` — set to `false` for new UI, `true` for legacy PropertySheet fallback. If `CreateProcessW` fails (exe not found), falls through to legacy UI automatically.
- INI change detection via timestamp polling on `OnSetFocus`

### Window Position & Size Persistence

Window placement (position, size, maximized state) is persisted in the registry so the window reopens where the user left it.

**Registry key**: `HKEY_CURRENT_USER\SOFTWARE\DIME\SettingsUI`

| Value | Type | Content |
|-------|------|---------|
| `WindowPlacement` | `REG_BINARY` | `WINDOWPLACEMENT` struct (44 bytes) |

**Save** (`WM_DESTROY`): `GetWindowPlacement()` → `RegSetValueExW()`

**Restore** (`Run()`, before `ShowWindow`): `RegGetValueW()` → validate → `SetWindowPlacement()`

**Validation before restore**:
- `wp.length == sizeof(WINDOWPLACEMENT)`
- `MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL)` != NULL (monitor still connected)
- If validation fails → fall back to `CW_USEDEFAULT` + `g_geo.windowDefaultW/H`

**CLI reset**: `DIMESettings.exe --reset-ui` deletes the registry value, then continues normal startup at default position. Can combine with `--mode`.

### CLI Shared Key Registry

`SettingsKeys.h` constants shared by CLI (`g_keyRegistry[]`) and UI (`ControlDef[]`). Consistency enforced by unit tests UT-SM-22/23/24.

---

## Implementation

### Source Files

| File | Role | Description |
|------|------|-------------|
| `SettingsPageLayout.h` | Layout data types | `LayoutRow`, `LayoutCard`, `RowType`, `RowAction`, sidebar items |
| `SettingsPageLayout.cpp` | Layout definitions | Static const card/row arrays for all pages. THE single source of truth. |
| `SettingsController.h` | Controller types | `SettingsSnapshot`, `SettingsControlId` enum, `SettingsModel` class |
| `SettingsController.cpp` | Controller logic | `LoadFromConfig()`, `ApplyToConfig()`, CConfig ↔ UI bridge |
| `SettingsGeometry.h` | Geometry constants | `SettingsGeometry` struct (`g_geo`), all pixel values at 96 DPI |
| `SettingsGeometry.cpp` | Theme colors | `ComputeThemeColors()`, `BlendColor()`, dark/light mode |
| `SettingsWindow.h` | Window types | `WindowData`, `ControlHandle`, method declarations |
| `SettingsWindow.cpp` | Renderer + input | Unified paint/hover/click, control creation, scroll, WndProc |
| `DIMESettings.cpp` | Entry point | CLI parsing, COM init, reverse conversion enumeration, window launch |
| `SettingsControllerTest.cpp` | Unit tests | Layout tree, snapshot, visibility, theme colors, sidebar items |

### Completed Features

- Sidebar with .ico icons, accent bar, hover, separator, aligned with first card
- Two-level navigation (Level 0 category cards, Level 1 sub-pages) + breadcrumb
- Per-card layout: each top-level row is its own card with shadow + hover
- Unified renderer: `PaintRow()`, `CalcRowHeight()`, `CalcCardW()`, `CalcCtrlRight()`
- Unified hit-test: `HitTestCardList()`, `HitTestRowCards()`
- Unified click: `HandleCardListClick()`, `HandleRowCardClick()`
- Unified control creation: `CreateRowControl()`, `AddControl()`
- Custom cards: `CandidatePreview` (SDF shadow), `ColorGrid` (3x2 swatches), `RichEditor`
- GDI+ anti-aliased toggle switches and button corners
- Card shadow (GDI+ light mode, dark border dark mode)
- `ChooseFontW` + `ChooseColorW` dialogs (late-bound from comdlg32.dll)
- Color mode switching with palette load/save
- Expandable sections (輸入設定 default expanded, color grid chevron)
- 自定碼表 page with per-card layout, shared renderer
- Reverse conversion combo (enumerated from TSF, filtered per mode)
- Toggle persistence via `SyncToggleToSnapshot()` + `ApplyAndSave()`
- Dark mode: theme switch via `WM_SETTINGCHANGE`, `SetWindowTheme("DarkMode_CFD")` for combos/edits
- Title bar color matched to content bg (`DWMWA_CAPTION_COLOR`)
- Win7/8 compatibility: MDL2 fallback, no garbled PUA chars
- Stable card width (scrollbar space always reserved)
- Mouse wheel scroll with `MulDiv` (handles high-precision mice)
- Section title cover (fixed on scroll)
- Real-time custom phrase validation (3-level color highlighting)

### Remaining Work

- [x] `SyncToggleToSnapshot` moved to `SettingsController.cpp` (pure logic, no HWND)
- [x] `ValidateLine` moved to `SettingsController.cpp` (pure line validation, returns error level + range)
- [x] `WM_DPICHANGED` handling (resize + font recreation + content rebuild)
- [x] Responsive sidebar collapse (hidden below `sidebarCollapseBreak` width, content takes full width)
- [ ] Move remaining controller logic: PopulateControls, combo items (requires HWND abstraction)

### Related Documentation

- [docs/LAYOUT_RENDERER.md](LAYOUT_RENDERER.md) — Renderer architecture, function reference, rendering chains
- [docs/REVERSE_CONV_REFACTOR.md](REVERSE_CONV_REFACTOR.md) — Reverse conversion model/view separation

---

## Compatibility

### Windows version support

| Feature | Win7 | Win8/8.1 | Win10 1809+ | Win11 |
|---------|------|----------|-------------|-------|
| Core settings UI | Yes | Yes | Yes | Yes |
| Dark mode | No | No | Yes | Yes |
| Mica title bar | No | No | No | Yes (falls back to opaque) |
| `DWMWA_CAPTION_COLOR` | No | No | Partial | Yes |
| Anti-aliased toggles (GDI+) | Yes | Yes | Yes | Yes |
| Per-monitor DPI (initial) | No | Yes | Yes | Yes |
| Per-monitor DPI (runtime `WM_DPICHANGED`) | No | No | No (not implemented) | No (not implemented) |
| `SetWindowTheme("DarkMode_CFD")` combos | No | No | Yes | Yes |
| Segoe MDL2 Assets icons | No (fallback to no icon) | No | Yes | Yes |
| Segoe Fluent Icons | No | No | No | Yes (auto via MDL2 fallback) |
| 跟隨系統模式 color mode | No | No | Yes (`isWindows1809OrLater`) | Yes |
| Reverse conversion enumeration | Yes | Yes | Yes | Yes |
| Legacy UI (`--legacy` flag) | Yes | Yes | Yes | Yes |

### Legacy UI coexistence

- `DIMESettings.exe` defaults to the new card-based UI
- `DIMESettings.exe --legacy` launches the old PropertySheet dialog
- Both UIs read/write the same `CConfig` model — settings are interchangeable
- Both UIs share the same `EnumerateReverseConversionProviders()` in `CConfig`
- The old `ConfigDialog.cpp` (PropertySheet pages) remains fully functional

### DPI awareness

- DIMESettings.exe sets `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` on Win10 1607+
- Falls back to `SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)` on Win8.1+
- All geometry uses `ScaleForDpi(value, dpi)` = `MulDiv(value, dpi, 96)`
- All fonts use `CreateFontW(-MulDiv(pt, dpi, 72), ...)`
- **Not implemented**: `WM_DPICHANGED` handling (font/layout recreation on monitor change)

### High contrast

- All colors derived from `GetSysColor()` which automatically returns high-contrast system colors
- No hardcoded RGB values in the color pipeline
- Toggle switches, buttons, cards all adapt via `ThemeColors` struct

## Verification

### Automated Tests (CI/CD, no HWND)

| Test ID | Description |
|---------|-------------|
| UT-SM-01 | Control definition completeness — every CConfig setting has a ControlDef |
| UT-SM-02–05 | Visibility rules per IME mode (Dayi/Array/Phonetic/Generic) |
| UT-SM-06 | Each layout card has at least one visible row per mode |
| UT-SM-07–10 | ComputeLayout card/row positions, DPI scaling, variable row heights |
| UT-SM-11–12 | SidebarHitTest normal range and out-of-bounds |
| UT-SM-13–14 | ComputeScrollRange, ClampScrollPos boundaries |
| UT-SM-15–17 | SettingsSnapshot Load/Apply/RoundTrip with CConfig |
| UT-SM-18 | ComputeThemeColors: light mode uses GetSysColor, dark mode uses DARK_* constants |
| UT-SM-19–21 | ImeModeToString / StringToImeMode round-trip |
| UT-SM-22–24 | CLI-to-UI key consistency, UI-to-CLI key consistency, mode mask consistency |
| UT-SM-25 | WM_COPYDATA mode parsing |
| UT-SM-26 | Every Toggle/ComboBox ControlDef has non-null description |
| UT-SM-27 | Layout with expandable sections: expanded card increases total height by sum of child rows |
| UT-SM-28 | LayoutCard.action: NavigateToCard cards have no child rows inline; ExpandSection cards do |
| UT-SM-29 | Color swatch visibility: hidden when colorMode != Custom, visible when Custom |
| UT-SM-30 | Layout tree walk visits rows in declared order matching s_xxxRows[] |
| UT-SM-31 | CTRL_FONT replaces old CTRL_FONT_NAME + CTRL_FONT_SIZE + CTRL_FONT_CHOOSE |
| UT-SM-32 | Sidebar items: 4 IME modes + 1 separator + 自定碼表 = 6 items |
| UT-SM-33 | BlendColor: BlendColor(black, white, 128) ≈ RGB(128,128,128) |
| UT-SM-34 | SettingsGeometry: all values > 0; windowMinW > sidebarWidth |
| UT-SM-35 | 自定碼表 layout: Array expandable group has 7 child rows |

### Integration Tests (may require HWND)

| Test ID | Description |
|---------|-------------|
| IT-NS-01 | Window create/destroy lifecycle — no leaks |
| IT-NS-02 | Settings read/write cycle via SettingsSnapshot |
| IT-NS-03 | Four IME modes independent — fontsize per mode |
| IT-NS-04 | Model reload after IME mode switch |
| IT-NS-05 | Custom table validation via ValidateCustomTableLines |
| IT-NS-06 | Single-instance WM_COPYDATA mode switch |

### Manual Testing

#### Visual Verification

| Test Item | Win11 Light | Win11 Dark | Win10 Light | Win10 Dark | Win7 |
|-----------|------------|------------|------------|------------|------|
| Sidebar: icons from .ico, accent bar, rounded highlight | ☐ | ☐ | ☐ | ☐ | ☐ |
| Sidebar: 自定碼表 item with separator above | ☐ | ☐ | ☐ | ☐ | ☐ |
| Card: rounded corners, subtle border from system colors | ☐ | ☐ | ☐ | ☐ | ☐ |
| Row: icon 24px + label + description + control right-aligned | ☐ | ☐ | ☐ | ☐ | ☐ |
| Row: separator line indented past icon area | ☐ | ☐ | ☐ | ☐ | ☐ |
| Row: hover effect (subtle bg change) | ☐ | ☐ | ☐ | ☐ | ☐ |
| Toggle: pill track, "開啟"/"關閉" left of switch, full-row click | ☐ | ☐ | ☐ | ☐ | ☐ |
| Category: `>` and `∨` chevrons correct per card action | ☐ | ☐ | ☐ | ☐ | ☐ |
| Expand/collapse: ∨ rows expand in-place, ∧ collapses | ☐ | ☐ | ☐ | ☐ | ☐ |
| Breadcrumb: clickable back navigation, accent color | ☐ | ☐ | ☐ | ☐ | ☐ |
| Font row: "微軟正黑體, 12pt" preview in selected font | ☐ | ☐ | ☐ | ☐ | ☐ |
| Font [變更字型] button opens ChooseFontW | ☐ | ☐ | ☐ | ☐ | ☐ |
| Color grid: 3x2 swatches visible only in Custom mode | ☐ | ☐ | ☐ | ☐ | ☐ |
| Color swatch click opens ChooseColorW | ☐ | ☐ | ☐ | ☐ | ☐ |
| Candidate window preview in 外觀設定 (vertical layout) | ☐ | ☐ | ☐ | ☐ | ☐ |
| Mica title bar | ☐ | N/A | N/A | N/A | N/A |
| Colors from system theme (not hardcoded) | ☐ | ☐ | ☐ | ☐ | ☐ |
| High contrast mode renders correctly | ☐ | ☐ | N/A | N/A | N/A |
| Scrolling without artifacts (double-buffered) | ☐ | ☐ | ☐ | ☐ | ☐ |
| Window minimum size enforced | ☐ | ☐ | ☐ | ☐ | ☐ |

#### DPI Verification

| Scale | Single Monitor | Multi-Monitor Drag |
|-------|---------------|-------------------|
| 100% | ☐ | ☐ |
| 125% | ☐ | ☐ |
| 150% | ☐ | ☐ |
| 200% | ☐ | ☐ |

#### Functional Verification

| Test Item | Method |
|-----------|--------|
| Launch from DIMESettings.exe | ☐ Direct run |
| Launch from IME Ctrl+\ | ☐ Hotkey while IME active |
| Launch from IME context menu | ☐ System tray right-click |
| Single instance enforcement | ☐ Second launch sends WM_COPYDATA, focuses existing |
| `--mode dayi` opens to Dayi | ☐ Command line |
| `--legacy` opens old PropertySheet | ☐ Command line |
| Toggle settings save immediately | ☐ Toggle → check INI file |
| Combo/edit changes save immediately | ☐ Change → check INI file |
| Live theme switching | ☐ Switch system dark/light while open — colors update |
| Four IME modes independent | ☐ Different settings per mode |
| `>` navigate: 外觀設定 opens sub-page with breadcrumb | ☐ Click |
| `>` navigate: 聲音與通知 opens sub-page | ☐ Click |
| `∨` expand: 輸入行為 shows child rows in-place | ☐ Click |
| `∨` expand: click again collapses (∧) | ☐ Click |
| Font picker: [變更字型] → ChooseFontW → preview updates | ☐ Click button |
| Color mode: switch to 自訂 → 3x2 grid appears | ☐ Change combo |
| Color mode: switch to 淡色 → grid disappears | ☐ Change combo |
| Color picker: click swatch → ChooseColorW → swatch updates | ☐ Click swatch |
| 還原預設值: resets appearance to defaults | ☐ Click button |
| Candidate preview: updates live on font/color change | ☐ Visual |
| 自建詞庫 `>`: opens editor sub-page | ☐ Click |
| 自建詞庫: RichEdit with real-time validation | ☐ Type invalid entry → red highlight |
| 自建詞庫: [儲存] disabled until dirty, enabled on edit | ☐ Edit text |
| 自建詞庫: navigate back while dirty → save prompt | ☐ Click breadcrumb |
| 自建詞庫: 匯出/匯入 action rows work | ☐ Click buttons |
| 自定碼表: sidebar item shows table loading page | ☐ Click sidebar |
| 自定碼表: per-IME main table [載入] buttons work | ☐ Click each |
| 自定碼表: Array ∨ expandable group | ☐ Click expand |
| 自定碼表: 聯想詞彙表 [載入] works | ☐ Click |

#### Coverage Targets

| Component | Target | Test Method |
|-----------|--------|-------------|
| SettingsModel + SettingsPageLayout | >= 90% | Unit tests |
| SettingsSnapshot (CConfig bridge) | >= 90% | Unit + integration tests |
| SettingsView (window lifecycle) | >= 30% | Integration tests |
| GDI rendering | Manual | Visual verification |
