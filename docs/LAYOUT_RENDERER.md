# Layout Extraction and Unified Renderer

## Architecture

The settings UI follows a declarative layout + generic renderer pattern. Layout data defines **what** appears; the renderer draws it without per-control knowledge.

```
SettingsPageLayout.cpp          SettingsWindow.cpp
┌─────────────────────┐        ┌──────────────────────────────────┐
│ LayoutCard[]         │───────▶│ Geometry helpers                 │
│ LayoutRow[]          │        │   CalcCardW()                    │
│ SidebarItemDef[]     │        │   CalcCtrlRight()                │
│                      │        │   CalcRowHeight()                │
│ Static data:         │        │                                  │
│  - title, desc       │        │ Card rendering                   │
│  - RowType, action   │        │   DrawCardWithShadow()           │
│  - iconGlyph         │        │                                  │
│  - visibleModes      │        │ Row rendering                    │
│  - expandParent      │        │   PaintRow()                     │
│  - defaultExpanded   │        │                                  │
└─────────────────────┘        │ Control creation                  │
                               │   CreateRowControl()              │
                               │   AddControl()                    │
                               │                                  │
                               │ Hit testing                       │
                               │   HitTestCardList()               │
                               │   HitTestRowCards()               │
                               │                                  │
                               │ Click handling                    │
                               │   HandleCardListClick()           │
                               │   HandleRowCardClick()            │
                               └──────────────────────────────────┘
```

### Data flow

```
Layout data (SettingsPageLayout.cpp)
    │
    ▼
RebuildContentArea()  ──▶  CreateRowControl() per row  ──▶  Win32 HWND controls
    │                                                         (combo, edit, button, toggle checkbox)
    ▼
PaintContent()
    ├─▶ PaintCardList()        ──▶  DrawCardWithShadow() + PaintRow() per row
    │     (Level 0 + 載入碼表)
    └─▶ PaintSettingsDetail()  ──▶  DrawCardWithShadow() + PaintRow() per row
          (Level 1 sub-pages)

WM_MOUSEMOVE  ──▶  HitTestCardList() / HitTestRowCards()  ──▶  hoverRowIndex
WM_LBUTTONDOWN ──▶ HandleCardListClick() / HandleRowCardClick() ──▶ toggle/expand/navigate
```

## Geometry helpers

These static functions provide a single source of truth for layout calculations. Used by paint, rebuild, hover, and click — guaranteeing consistent positioning.

### `CalcCardW(HWND hMainWnd, UINT dpi) → int`

Computes card width from main window width. Always reserves scrollbar space so cards never shift when the scrollbar appears/hides.

```
cardW = mainWindowWidth - sidebarWidth - 2 * contentMargin - scrollbarWidth
```

### `CalcCtrlRight(int margin, int cardW, UINT dpi, bool hasChevron) → int`

Right edge X for all controls (combo, button, toggle, swatch). When `hasChevron=true` (Level 1 sub-pages), reserves space for the expand/collapse chevron.

```
ctrlRight = margin + cardW - cardPad - controlRightMargin - (hasChevron ? chevronArea : 0)
```

### `CalcRowHeight(HDC hdc, const LayoutRow& lr, const SettingsSnapshot& s, UINT dpi) → int`

Returns height in pixels for any `RowType`. Special cases:
- `CandidatePreview`: calls `CalcCandidatePreviewH()` (font-dependent dynamic height)
- `ColorGrid`: `2 * (swatchSize + swatchCellGap)`
- `RichEditor`: `editorCardH` from geometry
- All others: `rowHeight` (two-line) or `rowHeightSingle` (single-line)

## Card rendering

### `DrawCardWithShadow(HDC, x, y, w, h, cornerR, cardBg, cardBorder, contentBg, isHover, hoverColor)`

Draws a card background with:
1. **Shadow** (light theme only): GDI+ anti-aliased rounded rect, 1px expand, 1px Y offset, alpha=20, dark color. Skipped in dark theme.
2. **Card fill**: GDI `RoundRect` with card bg + border pen. Dark mode uses darker border (`BlendColor(cardBg, black, 40)`); light mode uses subtle border.
3. **Hover overlay**: if `isHover`, draws another `RoundRect` with hover color.

Used by both `PaintCardList` and `PaintSettingsDetail`.

## Row type dispatch model

`RowType` determines how each row is rendered, what controls are created, and how height is calculated. Types fall into two categories:

**Standard types** — the generic renderer handles paint + control creation uniformly:
- `Toggle`, `ComboBox`, `Edit`, `Button`, `Clickable`
- Paint: icon → label → description → type-specific graphic (toggle switch, chevron)
- Controls: `CreateRowControl()` creates the appropriate Win32 HWND

**Custom types** — the renderer delegates to type-specific owner-draw functions:
- `CandidatePreview`, `ColorGrid`, `RichEditor`
- Each provides its own paint function, control creation function, and height calculator
- The renderer's switch statement dispatches; the custom functions encapsulate all type-specific logic

There is no explicit `isCustom` flag — `RowType` is the dispatch key. To add a new custom card type: add an enum value, implement 3 functions (paint, create, height), add cases to `PaintRow`/`CreateRowControl`/`CalcRowHeight`.

## Row rendering

### `PaintRow(HDC, WindowData*, LayoutRow&, margin, curY, cardW, thisH, ctrlRightEdge, dpi, isChild)`

Single function that renders ANY row type. Called by both paint paths. Dispatches by `RowType`:

| Category | RowType | Rendering |
|----------|---------|-----------|
| Custom | `CandidatePreview` | Delegates to `PaintCandidatePreviewCard()` — candidate + notify window preview with SDF shadow |
| Custom | `RichEditor` | Card header label + description only (RichEdit HWND handles its own content) |
| Custom | `ColorGrid` | 3x2 swatch label grid, right-aligned to column edges. Swatch HWND controls drawn by `WM_DRAWITEM` |
| Standard | `Toggle` | Label + description + toggle switch graphic (`PaintToggleSwitch()` via GDI+) + "開啟"/"關閉" text |
| Standard | `ComboBox` | Label + description (combo HWND drawn by Windows) |
| Standard | `Edit` | Label + description (edit HWND drawn by Windows) |
| Standard | `Button` / `Clickable` | Label + description (button HWND drawn by `WM_DRAWITEM` with GDI+ rounded corners) |

Common elements drawn for standard types:
- **Icon**: MDL2 glyph via `hFontMDL2Icon` (skipped on Win7/8 where `hasMDL2=false`)
- **Label**: `hFontCardHeader` (top-level) or `hFontBody` (child), left-aligned
- **Description**: `hFontDesc`, secondary color, below label
- **ExpandSection chevron**: MDL2 `∧`/`∨` or text fallback, right of controls

### `PaintCandidatePreviewCard(HDC, SettingsSnapshot&, margin, curY, cardW, totalH, dpi)`

Renders candidate window + notify window preview. Transparent card background (no card `RoundRect`). SDF shadow using per-pixel alpha blending via `AlphaBlend()` — same algorithm as `ShadowWindow.cpp`. Shadow color adapts to IME colors vs content background luminance.

### `PaintToggleSwitch(HDC, RECT, isOn, isHover, ThemeColors&)`

Renders toggle switch via GDI+ for anti-aliased rounded track and circular knob. ON state: accent-colored filled track, white knob. OFF state: border-only track, gray knob.

## Control creation

### `CreateRowControl(WindowData*, LayoutRow&, ctrlRight, cy, ctrlH, btnH, dpi)`

Creates the Win32 HWND control for a given row. Called by all 3 `RebuildContentArea` paths (Level 0 children, Level 1 sub-page, 載入碼表). Dispatches by `RowType`:

| RowType | Win32 control | Style |
|---------|--------------|-------|
| `Toggle` | `BUTTON` | `BS_AUTOCHECKBOX`, hidden (paint draws the toggle graphic) |
| `ComboBox` | `COMBOBOX` | `CBS_DROPDOWNLIST`, dark theme via `SetWindowTheme("DarkMode_CFD")` |
| `Edit` | `EDIT` | `WS_EX_CLIENTEDGE`, dark theme via `SetWindowTheme("DarkMode_CFD")` |
| `Button` / `Clickable` | `BUTTON` | `BS_OWNERDRAW`, button text from `RowAction` mapping |

Button text mapping: `LoadTable` → "載入", `OpenFontDialog` → "變更字型", `ExportCustomTable` → "匯出", `ImportCustomTable` → "匯入", `ResetDefaults` → "還原預設值".

### `AddControl(WindowData*, id, HWND, x, y, w, h)`

Registers a created control in `controlHandles[]` and applies `ChildWheelSubclassProc` for mouse wheel forwarding to the content area.

### Special cases (not in `CreateRowControl`)

- **RichEditor**: complex initialization — loads `msftedit.dll`, reads custom table from file, sets `CHARFORMAT2W`, dark mode background/text color, removes wheel subclass
- **ColorGrid**: creates 6 `SS_OWNERDRAW | SS_NOTIFY` static controls in a 3x2 grid with measured label width positioning

## Hit testing

### `HitTestCardList(y, cards, count, modeMask, cardExpanded, expandBaseIdx, dpi, scrollPos) → int`

Card-level hover hit-test for Level 0 pages (IME mode + 載入碼表). Walks `LayoutCard[]`, computes each card's height including expanded children, returns card index if `y` falls within a card's bounds. Returns -1 if no hit.

### `HitTestRowCards(y, hWnd, WindowData*, card, dpi) → int`

Per-row card hover hit-test for Level 1 sub-pages. Walks rows in a `LayoutCard`, groups top-level rows + children into per-row cards, returns the start row index of the hit card. Handles visibility filtering (mode mask, color mode expand state).

## Click handling

### `HandleCardListClick(y, hWnd, hParent, WindowData*, cards, count, modeMask, expandBaseIdx, dpi) → bool`

Unified click handler for Level 0 pages. Dispatches by `RowAction`:
- `NavigateToCard` → calls `NavigateToCategory()`
- `ExpandSection` → toggles `cardExpanded[i]`, rebuilds content
- Child toggle row → flips checkbox, syncs snapshot, saves config

### `HandleRowCardClick(y, hWnd, WindowData*, card, dpi) → bool`

Unified click handler for Level 1 sub-pages. Walks per-row cards, hit-tests each row:
- `ExpandSection` combo (color mode custom) → toggles `colorGridExpanded`
- `Toggle` → flips checkbox via `SyncToggleToSnapshot()`, saves config

## Rendering chains

### Level 0 (IME mode page)

```
PaintCardList()
  for each LayoutCard:
    DrawCardWithShadow()          ← card bg + shadow + hover
    PaintRow(parent row)          ← icon + label + desc + chevron
    if expanded:
      for each child row:
        separator line
        PaintRow(child row)       ← label + toggle/combo/edit
```

### Level 1 (sub-page)

```
PaintSettingsDetail()
  section title cover (fixed, non-scrolling)
  for each card span:
    if CandidatePreview:
      PaintRow() → PaintCandidatePreviewCard()
    else:
      DrawCardWithShadow()
      for each row in span:
        PaintRow()                ← icon + label + desc + control
```

### 載入碼表

Same as Level 0 — uses `PaintCardList()` with load-table layout data and `expandBaseIdx=4`.

### Control creation (all levels)

```
RebuildContentArea()
  MoveWindow(contentArea)
  destroy old controls
  for each visible row:
    CalcRowHeight()
    if Toggle/Combo/Edit/Button/Clickable:
      CreateRowControl()          ← unified
    elif RichEditor:
      inline creation             ← special (file load, charformat)
    elif ColorGrid:
      inline creation             ← special (3x2 swatch grid)
  PopulateControls()              ← fills combo items, sets toggle states
```

## Win7/8 compatibility

- `hasMDL2` flag: detected at font creation by checking if `Segoe MDL2 Assets` face name matches
- Icons: skipped when `!hasMDL2` (no garbled PUA characters)
- Chevrons: fall back to text `>` / `∧` / `∨` with `hFontBody`
