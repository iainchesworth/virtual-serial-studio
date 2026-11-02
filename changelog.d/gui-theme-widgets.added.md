Added the design-token theme and the shared widget library for the
redesigned GUI. `Theme` loads `tokens/tokens.json` and `style.qss.in` from a
Qt resource, renders the style sheet per theme, maps the palette, honours the
system light/dark scheme, density and reduced motion, and exposes only tokens
to the rest of the GUI. `IconProvider` renders the 26 `currentColor` SVGs
through the approved `Qt6::Svg` module and flushes its cache on a theme
change; `FocusRingStyle` paints the 2px keyboard-only focus ring over Fusion.
The widget library now contains StatusChip, DirectionBadge, Tag, Badge,
StateDot, KeyCap, NoteBanner, ResultSentence, CodeBlock, CopyButton,
SegmentedControl, PickCard, Switch, Stepper, Disclosure, FormRow, Toast,
ToastManager, ConfirmDialog, ErrorDialog and a WidgetGallery opened with
`VSS_WIDGET_GALLERY=1`. Thirteen tests check token resolution, QSS
placeholders, WCAG contrast, the no-colour-literal scan, the icon cache, the
gallery in both themes, accessible names, 24 px targets and the widget
behaviours. The contrast and QSS tools now live in `tools/` and the design
job runs them in CI. GUI builds pin Qt 6.12 because it carries Qt6::Svg.
