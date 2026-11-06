# Screens 03: topology view
Mockups: Workspace, Advanced, Compact, CanvasEditor. Implemented with `QGraphicsView`/`QGraphicsScene`. Building on the canvas in Advanced mode (the toolbox, wiring, Settings, multi-select, Duplicate and undo) is in `07-canvas-editor.md`; where the two differ in Advanced mode, `07` wins.

## Visuals
- One node per port, drawn as a card: name (600) over a second line (mono path or role), optional inline chips (for example Owner, Dropped). Size 168 by 56 (the real device, or the target of a join, is 168 by 64). Compact window: 120 by 48. 6px radius, 1px `control` edge, `panel` fill. A real device uses `sunken` fill. Unlinked nodes use a dashed edge.
- Selected node: 1px `accent` edge plus a 2px `accentSoft` halo.
- Links: 2px lines in `ink3` with round caps. One-way and redirect links end in a filled arrow head. Monitor links are dashed. Two-way and split links have no heads. The selected link is `accent`, 3px.
- Link labels (12px, ink3): "Split", "Join", "two-way", "owner", "monitor". Keep 8px clear of lines and nodes.
- Split draws one to many: a line from the source to a mid-gap vertical bar, then one line to each target. Join mirrors it. Layout is automatic and deterministic (sources left, targets right, centred vertically, 48px minimum gaps).
- Nodes never overlap, and a link never runs under a node it is not attached to. A link that would cross a node is routed around it, keeping 32px clear (room for a label and its 8px). A split or join that cannot keep its bar clear is drawn as one routed line per pair instead.
- Users may move nodes: drag one with the mouse, or press the arrow keys on the focused node. The links that end on it follow while it moves, and a node dropped on another is settled at least 16px clear of it. Positions are kept for the session in the view and never enter the JSON topology (the schema has no place for them).
- Until the user moves a node the whole layout stays automatic, so a new port or link may move a column. After the first move every node keeps its place, and a new node goes into free space next to the node it connects to: right of its source, left of its target, at the nearest height that overlaps nothing and keeps the link clear of other nodes.
- Empty: centred text "Add a port to see it here." Footer hint: "Click a box to inspect it. Click a line to select the link."

## Behaviour
- Click a node: select the port. Click a line (8px hit tolerance): select the link. Double-click a node: focus the Inspector.
- Keyboard (required, WCAG 2.5.7): Tab moves through nodes then links. Enter inspects. Delete removes with confirmation and Undo. Arrow keys move the focused node by 8px (Shift 32px). Ctrl+0 fits. Shift+F10 or the menu key opens the context menu: Inspect, the port actions (Send bytes, Start or Stop repeat in Advanced, Open in monitor, and for a mock fixture in Advanced Start or Stop generator and Echo received bytes; see `screens/02-workspace.md`), Copy name, Rename, Connect from here, Remove.
- Zoom with Ctrl+wheel and Ctrl+plus or minus. Fit (toolbar, Topology menu, Ctrl+0, canvas menu) centres and scales all items into view with a 24px margin, from any zoom or scroll position, and hands control back to automatic fitting. Zoom is limited to 25 to 200 percent; Fit never enlarges past 125 percent.
- Right-click empty canvas, or press Shift+F10 or the menu key with nothing focused: Tidy layout, Fit. Tidy layout (also in the Topology menu and the command palette) throws away every moved position, runs the automatic layout again and fits.

### Resizing the window
The view is either automatic or manual. It is automatic when it opens and after Fit or Tidy layout. It becomes manual as soon as the user zooms (wheel, keys), scrolls, or moves a node.
- Automatic: a resize re-fits, so the whole diagram stays in view and follows the window. Below 500px of width the nodes switch to the compact 120 by 48 size and the layout is recomputed.
- Manual: a resize keeps the zoom and the point at the centre of the view, so nothing jumps and the window shows more or less of the diagram. If the window is left showing none of the diagram, the diagram is brought back to the centre.
- Adding or removing a port in manual mode leaves the view alone, except that a new node is scrolled into view.

Why: re-fitting on every resize is what people expect until they have taken control, and it is wrong after, because it throws away the zoom and position they chose.
- Nothing may require dragging: the arrow keys move a node, and every link can be made in the Connect ports dialog.

## Accessibility
`QGraphicsItem` content is not exposed to screen readers by default. Required: set the view's accessible name "Topology" and accessible description to a generated summary, for example "4 ports, 1 link: split from device to terminal, test-tool and logger". Give each node a `QAccessibleInterface` if practical. In any case the Ports dock plus an optional "Links" list (View, Show links as a list) must expose every port and link with names, roles and states, so the topology is never the only way to read or change the setup.

## Acceptance
Select, move, remove, inspect, Tidy layout and Fit work with the keyboard alone. The summary text updates when links change. Colours come from `Theme`, so the dark theme needs no extra work.
