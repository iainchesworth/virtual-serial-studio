# 02 Design tokens and Qt implementation

Authoritative values: `tokens/tokens.json`. This page explains how to use them in Qt.

## Colour roles (light / dark)
| Token | Role | Light | Dark |
|---|---|---|---|
| ground | Window background | #F2F4F7 | #11161D |
| panel | Docks, dialogs, inputs | #FFFFFF | #1A212B |
| sunken | Headers, wells, code, alternate rows | #F7F8FA | #151B23 |
| hover / pressed | Fill of a button, toolbar button, menu row or tab under the pointer, and while pressed | #ECF0F4 / #DFE5EC | #232C39 / #2D3949 |
| line | Decorative dividers | #D8DDE4 | #2B3543 |
| line2 | Soft borders on cards, scrollbar thumbs | #BCC4CF | #445064 |
| control | Boundaries of controls (3:1 rule) | #748090 | #7E8A9C |
| ink / ink2 / ink3 | Text: primary, secondary, caption | #18212D / #465263 / #566273 | #E8EDF3 / #B8C3D1 / #9EABBB |
| accent | Primary action, selection edge | #0B6E8A | #59B9D4 |
| accentStrong | Accent text, links, hover | #085870 | #86CDE2 |
| accentSoft / accentLine | Selection fill and its edge | #E2F0F4 / #B3D4DD | #163640 / #2F6171 |
| ok, okSoft, okLine | Healthy | #1B7447 ... | #6FD29C ... |
| amber, amberSoft, amberLine | Attention | #9A4F00 ... | #F2B764 ... |
| bad, badSoft, badLine | Failure | #B3261E ... | #FF8D86 ... |
| focus | Focus ring | #0A5C75 | #86CDE2 |
| onAccent, onDanger | Label on accent and danger fills | #FFFFFF | #06222B, #3C0A07 |

Run `python3 tools/check_contrast.py` after any colour change. It must report `FAILURES: 0`.

## Typography
- Never bundle fonts and never set a font family. Use `QFontDatabase::systemFont(QFontDatabase::GeneralFont)` and `QFontDatabase::systemFont(QFontDatabase::FixedFont)` for data, paths, addresses and payloads.
- Windows: keep Segoe UI for text. Qt 6 draws Segoe UI Variable at the wrong weight through DirectWrite (every regular label comes out semi-bold), so do not select it. Use Consolas for the fixed font when present (the Qt default is Courier New). Both ship with the OS, nothing is bundled. See design-deviations.
- Request the unhinted outline (`QFont::PreferNoHinting`) and `QFont::PreferAntialias`, and set `Qt::HighDpiScaleFactorRoundingPolicy::PassThrough` before the `QApplication` is built, so 125 and 150 percent displays are not rounded and resampled.
- Every style lands on a whole number of logical pixels (body 13, caption and label 12, large 14, subtitle 16, title 20, display 26, port name 44). A fractional size such as 11.08 px is the cause of soft, uneven text. Convert to points with the screen's logical DPI so Qt gets the same whole pixel size back.
- Implement the scale as ratios of the system font's size (never below the 13px design body) so OS text size is honoured: caption and label 0.923, body 1.0, body large 1.077, subtitle 1.231, title 1.538, display 2.0, port name 3.385 (fixed font, weight 500), data 0.923 (fixed font). Weights: labels, titles and display 600, button 500.
- Minimum text is 12px equivalent, except key caps and uppercase badges at 11px.

## Size, spacing, shape
Use the `size`, `space`, `radius` and `layout` blocks in tokens.json. Radii: control 6, inner 8 (cards, menus), dialog 12, chip 11. The style sheet template reads them as `%radius.control%` and `%space.2%`, so change the token, not a widget. Spacing unit is 4px (4, 8, 12, 16, 24, 32, 48). Buttons are 34px high (28 small), inputs 34, toolbar buttons 34, dock headers 32, tab bars 32, status bar 30, menu bar 28, toolbar 48 (40 at the minimum window). Any pointer target is at least 24 by 24. Depth comes from 1px borders. Only dialogs, menus and toasts cast shadows, and those come from the platform.

## Required Qt architecture
```
Theme (singleton, QObject)
  - tokens(): current colour set (light or dark)
  - colour(Role), font(TextStyle), metric(Size), spacing(Space)
  - styleSheet(): QString   // renders tokens/style.qss.in for the current theme
  - palette(): QPalette     // maps tokens to QPalette roles
  - signal themeChanged()
IconProvider
  - icon(name, ColourRole, logicalSize) -> QIcon   // loads :/icons/<name>.svg, replaces currentColor, caches by (name, size, dpr, colour)
  - clears its cache on Theme::themeChanged
FocusRingStyle : QProxyStyle    // paints the 2px ring with 2px offset for keyboard focus
```
1. Base style: `Fusion`, so the palette and QSS render the same on every platform. Keep native menu bar, native file dialogs and native message-box behaviour.
2. QPalette mapping: Window=ground, Base=panel, AlternateBase=sunken, WindowText and Text=ink, PlaceholderText=ink3, Highlight=accent (selected text uses onAccent), HighlightedText=onAccent, Link=accentStrong, ToolTipBase=toastBg, ToolTipText=toastText, Button=panel, ButtonText=ink, Disabled group text=disabledText.
3. Follow the OS colour scheme with `QGuiApplication::styleHints()->colorScheme()` and its change signal. Settings offers Follow the system, Light and Dark. When forced, apply your own palette (the colour scheme override API only exists in newer Qt, so do not depend on it).
4. Re-polish on theme change: `qApp->setPalette(...)`, `qApp->setStyleSheet(...)`, then rebuild icons and repaint custom widgets.
5. Focus ring: QSS cannot do it. In `FocusRingStyle::drawPrimitive`, for `PE_FrameFocusRect` draw a 2px `focus` rounded rectangle expanded by 2px, and only when the widget has keyboard focus (`Qt::TabFocusReason`, `BacktabFocusReason`, `ShortcutFocusReason`). Never show the ring for mouse clicks on buttons.
6. Custom painted widgets (StatusChip, Switch, Stepper, Topology nodes) read colours from `Theme`, never from literals. Add a unit test that fails if a hex literal appears in `apps/vss_gui` outside `Theme`.
7. Dynamic properties drive QSS: `role` (primary, quiet, danger), `size` (small, icon), `invalid`, `note` (info, ok, warn, danger), `card`, `pick`, `selected`, `code`, `muted`.
8. High DPI: Qt 6 scales automatically. Use logical pixels, `devicePixelRatioF()` for icon pixmaps, and never round positions yourself.
9. Reduced motion: if the OS requests it use 0 ms durations. Otherwise 100 ms hover, 150 ms disclosure, 200 ms dialogs, ease-out. Live counters never animate.

## Icons
`icons/*.svg` are 16px, 1.5 stroke, round caps, `currentColor`. Always paired with a text label, or a tooltip and accessible name when alone. Use Qt SVG (`Qt6::Svg`, approved) or hand-parse. `IconProvider` returns a `QIcon` backed by an icon engine that renders on demand at any size and device pixel ratio, so the icon stays sharp on every screen and at 200 percent text. An icon asked for by colour role follows the theme without being rebuilt, and its disabled mode is drawn in `disabledText`. Every action, button and field the specs give an icon shows one (mock-ups under `reference/mockups`).
