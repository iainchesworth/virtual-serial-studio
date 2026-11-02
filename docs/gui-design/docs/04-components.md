# 04 Component library

Build these once in `apps/vss_gui/src/widgets/` (or the equivalent module chosen in WP0) and reuse them everywhere. Mockups: `reference/mockups/F4Components.html`.

| Widget | Anatomy and states | Accessibility | Qt notes |
|---|---|---|---|
| **StatusChip** | Pill, height 22 (20 small). Dot plus text. Kinds: ok (filled dot), waiting (warn, hollow dot), connected (ok, filled), studioOnly (neutral, hollow), closed (neutral, hollow), failed (bad, cross glyph instead of dot), fixture (info, no dot). Text is the state name. | Accessible name is the text. Never colour alone. | Custom paint. Colours from Theme. `sizeHint` from font metrics. |
| **DirectionBadge** | RX (panel fill, ink), TX (accentSoft, accentStrong), DROP (badSoft, bad). Mono 11px, 4px radius, 1px edge. | Name "received", "sent", "dropped". | Paint in the monitor delegate. |
| **Tag, Badge** | Tag 20 high, 11px bold. Variants: s (accentSoft), a (amberSoft), n (dashed edge), ok. Badge uppercase "ADVANCED". | Plain text label. | QLabel with painted background. |
| **NoteBanner** | Icon plus text plus optional action link or buttons. Kinds info, ok, warn, danger. 6px radius, 1px edge, 10 by 12 padding. | `QAccessible::Role::StaticText` with the kind prefixed in the name ("Warning: ..."). Danger and warn use an alert role where available. | QFrame with `note` property. |
| **CodeBlock** | Mono text on `sunken`, 1px `line` edge, trailing CopyButton. Wraps. | Name "Address" or "Command". Copy announces "Copied". | QFrame `code` property, selectable QLabel. |
| **CopyButton** | 24 to 28 square icon button. Click copies, tooltip "Copy <thing>", brief "Copied" toast. | Name "Copy <thing>". | `size="icon"`. |
| **SegmentedControl** | Buttons joined in one 1px `control` frame, 32 high, selected = accent fill with onAccent text. | Exclusive group, radiogroup semantics, arrow keys move. | QButtonGroup of checkable buttons. |
| **PickCard** | Card with a real radio button, title (14 600), description, optional meta. Selected: accentSoft fill and accent edge plus the checked radio. | The radio is the control. Card is its label. Whole card clickable. | QFrame `pick` plus QRadioButton. |
| **Switch** | 34 by 18 track, 14 knob, label text beside it showing the state ("DTR on"). Off track uses `trackOff`. | Role switch, name "DTR", checked state. Space toggles. | Custom QAbstractButton. |
| **Stepper** | Numbered circles joined by lines. Done = check, current = accent fill, next = outline. | Announce "Step 2 of 3, Programs". | Custom widget used by wizard dialogs. |
| **Disclosure** | Chevron button plus label, toggles a section over 150 ms. Collapsed rows with dashed edges show an Advanced badge in Simple mode. | `aria-expanded` equivalent via accessible state. | QToolButton plus QPropertyAnimation. |
| **ResultSentence** | An info NoteBanner whose text updates live with the form. Bold port names. | Live region, polite. | Reuse NoteBanner. |
| **Toast** | Dark bar, 6px radius, text plus optional action (Undo, Open monitor). Six seconds, pauses on hover and focus. Stack bottom centre. | Announced politely. Never the only way to do something. | `ToastManager` owns a stack of QFrame#Toast. |
| **FormRow** | Label above field (12 600 ink2), hint below (12 ink3), error below in `bad` with an icon. Invalid field gets a 2px bad border and `aria-invalid`. | Label buddy set. Error text linked as description. | Helper that builds the rows. |
| **StateDot, KeyCap** | KeyCap: mono 11, 1px edge, 2px bottom edge. | Plain text. | QLabel. |
| **PortDelegate** | Two or three lines per port: name (600), role (12 ink3), mono path with CopyButton, StatusChip right. Selected row uses accentSoft. | Row name "terminal, COM7, waiting for program". | QStyledItemDelegate. Compact mode uses 28px rows. |
| **LauncherCard** | 20 padding, 44 glyph tile (accentSoft), title 16 600, description, result line (12 ink3). | A button with a clear name. | Clickable QFrame with Enter and Space handling. |
| **ConfirmDialog, ErrorDialog** | See `screens/06`. Primary action rightmost. Escape cancels. Destructive action is never default. | Initial focus on the safest action. | QDialog with `QDialogButtonBox`-like custom footer. |

Every widget must: use `Theme` for colour and fonts, set an accessible name and role, show the focus ring via the shared style, handle both themes, and appear in a `WidgetGallery` developer dialog (hidden behind an environment variable) that the offscreen self-test instantiates.
