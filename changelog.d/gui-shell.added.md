Added the redesigned GUI shell: a single `ActionRegistry` behind the menus,
toolbar and `Ctrl+K` command palette; the Simple/Advanced level service; the
four-job launcher with readiness and saved setups; the status bar with ports,
links, rates, hub state, readiness and level switch; and the dock frame with
placeholder panes for the work packages that follow. `ShellWindow` is now the
application window, opens on the launcher for an empty studio, loads and saves
the existing JSON document format, persists geometry/dock state, and keeps the
old shell files building but unused until WP9 removes them. The palette groups
actions into Available now and Needs Advanced mode, explains an empty group by
mode, and turns Advanced mode on before running a hidden action. Six shell
tests cover action levels, palette filtering and activation, launcher and
workspace transitions, layout persistence, level persistence, accessible
toolbar names, shortcut uniqueness and the headless self-test. The theme gained
explicit menu text colours for dark mode, and action icons are rebuilt on a
theme change.
