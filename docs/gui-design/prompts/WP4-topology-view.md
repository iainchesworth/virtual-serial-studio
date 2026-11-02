# WP4: topology view
Model: deepseek-v4-pro (thinking on). Branch: `gui/wp4-topology`. Depends on WP2.

## Read first
`AGENTS.md`, `docs/screens/03-topology-view.md`, `docs/06-accessibility.md`. Mockups: `Workspace.html`, `Advanced.html`, `Compact.html` (read the node and line markup for sizes).

## Task
Implement the topology dock with `QGraphicsView` and `QGraphicsScene`.
1. Node items and link items with the exact sizes, strokes, selection treatment and labels in the spec. Tokens only.
2. Deterministic automatic layout for split (one to many), join (many to one), two-way, one-way, monitor and unlinked nodes, with 48 px minimum gaps and the compact sizes at the minimum window.
3. Selection, hover, double-click to focus the Inspector, context menu, Delete, zoom with Ctrl+wheel and Ctrl plus or minus, Fit (Ctrl+0, Advanced), session-only nudging.
4. **Keyboard operation is mandatory**: Tab through nodes then links, Enter, Delete, arrow keys move a node (Shift for 32 px), Shift+F10 opens the context menu.
5. **Accessibility**: view name "Topology", a generated accessible description summarising ports and links that updates on change, and an optional "Links" list (View, Show links as a list) exposing every link with names and states. Consider `QAccessibleInterface` for items.
6. Cycle refusal feedback: "Show the loop" highlights the cycle. The loop message itself is `ErrorDialog` or the NoteBanner pattern from WP1.
7. Selection sync with the Ports dock via the shared selection service from WP3 (if WP3 has not merged, define the interface and a stub).
8. Tests: layout positions for each link kind, hit testing, keyboard-only flow, description text generation, theme switch repaint.

## Constraints
Do not change the JSON schema. Node positions are session-only. Nothing may require dragging.

## Acceptance
Every operation is possible without a pointer. Summary text matches the examples in the spec. No literal colours.
