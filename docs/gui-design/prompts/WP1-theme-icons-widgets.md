# WP1: theme, icons and widget library
Model: deepseek-v4-pro (thinking on). Branch: `gui/wp1-theme-widgets`. Depends on WP0.

## Read first
`AGENTS.md`, `docs/02-design-tokens.md`, `docs/04-components.md`, `docs/06-accessibility.md`, `tokens/tokens.json`, `tokens/style.qss.in`, and the markup of `reference/mockups/F4Components.html` and `F5States.html`.

## Task
1. **Theme.** Build `Theme` exactly as described in `docs/02-design-tokens.md`: tokens loaded from `tokens.json` (embed as a Qt resource), `colour()`, `font()`, `metric()`, `spacing()`, `palette()`, `styleSheet()` (renders `style.qss.in` with `%name%` replacement and fails loudly on an unknown name), follow-the-system colour scheme with a change signal, forced Light and Dark, density (comfortable, compact), reduced motion from the OS.
2. **FocusRingStyle** (QProxyStyle over Fusion): 2px `focus` ring, 2px offset, keyboard focus only.
3. **IconProvider** with `icons/*.svg` as resources (`Qt6::Svg`): replace `currentColor`, cache by name, size, device pixel ratio and colour, flush on theme change.
4. **Widgets** in `docs/04-components.md`: StatusChip, DirectionBadge, Tag and Badge, NoteBanner, CodeBlock, CopyButton, SegmentedControl, PickCard, Switch, Stepper, Disclosure, ResultSentence, Toast and ToastManager, FormRow, KeyCap. Plus `ConfirmDialog` and `ErrorDialog` (title, body, buttons, Copy details, safest-button focus, Escape cancels). The `ActionRegistry` belongs to WP2, not you.
5. **WidgetGallery** dialog (opened only when `VSS_WIDGET_GALLERY=1`) showing every widget in every state, with a theme switch.
6. **Tests** with the in-tree framework: token resolution; every `%name%` in the template exists in both themes; a C++ contrast test over the pairs in `tools/check_contrast.py` (same thresholds); no hex colour literals in `apps/vss_gui` outside `Theme` (a scan test); accessible names present on gallery widgets; icon cache flush on theme change; the gallery instantiates in light and dark under the offscreen platform and passes the 24 px target check.
7. Copy `tools/check_contrast.py` and `tools/gen_qss.py` into the repo `tools/` if they are not there, and wire the contrast check into CI.

## Constraints
Never set a font family or a literal font size outside `Theme`. Never bundle a font. No colour literals outside `Theme`. Status is text plus shape plus colour. All custom painting reads `Theme`.

## Acceptance
Gallery renders both themes with no unreadable text. Contrast test and QSS placeholder test pass. Theme switch at runtime restyles everything with no restart. Keyboard focus shows the ring on every widget, and mouse clicks on buttons do not.
