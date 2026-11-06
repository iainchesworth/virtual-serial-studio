# Screens 03: topology view
Mockups: Workspace, Advanced, Compact. Implemented with `QGraphicsView`/`QGraphicsScene`.

## Visuals
- One node per port, drawn as a card: name (600) over a second line (mono path or role), optional inline chips (for example Owner, Dropped). Size 168 by 56 (the real device, or the target of a join, is 168 by 64). Compact window: 120 by 48. 6px radius, 1px `control` edge, `panel` fill. A real device uses `sunken` fill. Unlinked nodes use a dashed edge.
- Selected node: 1px `accent` edge plus a 2px `accentSoft` halo.
- Links: 2px lines in `ink3` with round caps. One-way and redirect links end in a filled arrow head. Monitor links are dashed. Two-way and split links have no heads. The selected link is `accent`, 3px.
- Link labels (12px, ink3): "Split", "Join", "two-way", "owner", "monitor". Keep 8px clear of lines and nodes.
- Split draws one to many: a line from the source to a mid-gap vertical bar, then one line to each target. Join mirrors it. Layout is automatic and deterministic (sources left, targets right, centred vertically, 48px minimum gaps). Users may nudge nodes, and the nudge is kept for the session.
- Empty: centred text "Add a port to see it here." Footer hint: "Click a box to inspect it. Click a line to select the link."

## Behaviour
- Click a node: select the port. Click a line (8px hit tolerance): select the link. Double-click a node: focus the Inspector.
- Keyboard (required, WCAG 2.5.7): Tab moves through nodes then links. Enter inspects. Delete removes with confirmation and Undo. Arrow keys move the focused node by 8px (Shift 32px). Ctrl+0 fits. Shift+F10 or the menu key opens the context menu: Inspect, the port actions (Send bytes, Start or Stop repeat in Advanced, Open in monitor, and for a mock fixture in Advanced Start or Stop generator and Echo received bytes; see `screens/02-workspace.md`), Copy name, Rename, Connect from here, Remove.
- Zoom with Ctrl+wheel and Ctrl+plus or minus. Fit centres and scales to the content.
- Nothing may require dragging. Every link can be made in the Connect ports dialog.

## Accessibility
`QGraphicsItem` content is not exposed to screen readers by default. Required: set the view's accessible name "Topology" and accessible description to a generated summary, for example "4 ports, 1 link: split from device to terminal, test-tool and logger". Give each node a `QAccessibleInterface` if practical. In any case the Ports dock plus an optional "Links" list (View, Show links as a list) must expose every port and link with names, roles and states, so the topology is never the only way to read or change the setup.

## Acceptance
Select, nudge, remove and inspect work with the keyboard alone. The summary text updates when links change. Colours come from `Theme`, so the dark theme needs no extra work.
