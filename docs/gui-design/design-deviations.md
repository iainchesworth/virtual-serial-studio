# Design deviations and recorded decisions

The design bundle is the contract. Where the bundle is silent, or where its
wording conflicts with the repository, the choice and its reason are recorded
here. Entries name the work package that owns the decision.

## WP0

### Deviations file path

`docs/09-qa-and-definition-of-done.md` says `docs/design-deviations.md`;
`AGENTS.md`, WP0 and WP9 say `docs/gui-design/design-deviations.md`. This file
uses the bundle path. The QA reference is treated as a typo.

### GUI logic tests use a Qt-aware runner

WP1's prompt says to test with the in-tree framework. The in-tree framework in
`tests/framework` is deliberately Qt-free and has no `QCoreApplication`. The
backend has signals and a `QTimer`, and widgets need a `QApplication`, so both
test executables reuse the in-tree `TEST_CASE` and `REQUIRE` macros with a
small Qt-aware runner (`vss_gui_backend_tests`, `vss_gui_theme_tests`). The
whole-window walk stays in `vss_gui --selftest`. No new third-party test
framework is added, so the intent of the rule is met.

### No per-write callback from the engine thread

The design says failures must be visible locally. A per-write completion
callback would have to touch a `QObject` from the endpoint strand and risks a
use-after-free when the backend is destroyed with a write pending. The adapter
therefore returns an immediate error for a missing or closed port, queues the
write, and surfaces a queue-full rejection as the engine's error event through
the coalesced batch. The Send panel can show "queued" and the event; it does
not need a per-write signal.

### Integration branch

Work packages merge into `gui/integration`, not `develop`, until WP9. The
design replaces the shell and leaves placeholders between WP2 and WP8, so
merging each package to trunk would make `develop` unusable for the duration.
This is a delivery decision, not a change to the design.

### Qt version pin for builds

The machine has Qt 6.10.3 under `C:\Qt` and Qt 6.12 under
`C:\Repos\common\qt`. A `QT_ROOT` value with a trailing space makes CMake miss
one and silently select the other; the binary then imports a version tag that
the runtime does not export (`qt_version_tag_6_12` against 6.10). All builds
pin `QT_ROOT` without a trailing space and also pass
`-DCMAKE_PREFIX_PATH`.

WP0 used 6.10.3. WP1 needs the approved `Qt6::Svg` module, which the 6.10.3
installation does not have; the 6.12 installation does. GUI builds therefore
pin 6.12.0 from `C:\Repos\common\qt\6.12.0\msvc2022_64`. This is an
environment decision, not a product dependency change: `Qt6::Svg` is the
dependency the bundle already approved.

## WP1

### Offscreen gallery and the system font

The offscreen platform on Windows has no native font database, and the
project forbids bundling a font. `vss_gui_theme_tests` points
`QT_QPA_FONTDIR` at `C:/Windows/Fonts` so the review PNGs show real text
instead of missing-glyph boxes. This is test-only; the product continues to
use the system UI font through `QFontDatabase`, and the test adds no font to
the repository or the build output.

### SegmentedControl uses QRadioButton

The segmented control is a group of exclusive `QRadioButton`s with the
indicator hidden by QSS. That gives screen readers radio-group semantics and
arrow-key behaviour without inventing an accessible interface. The selected
segment is still an accent fill with `onAccent` text, as the design specifies.

### CopyButton paints itself

A 24 px `QToolButton` inherited the 10 px toolbar padding and clipped the
16 px copy glyph. `CopyButton` is now a custom-painted `QAbstractButton` with
the same accessible name, tooltip, focus ring and 24 px target.

## WP2

### The old shell stays compiled until WP9

`main.cpp` now constructs `ShellWindow` from `src/shell/`, but the flat
`main_window.cpp`, the old panels and dialogs, `theme.cpp`, and `selftest.cpp`
are still in the executable's source list and still compile. They are not
referenced by `main.cpp`. Deleting 24 files in the same commit as the first
working frame would have made the WP2 diff much harder to review and risked a
link surprise while the panes were still placeholders. WP9 owns the deletion,
the compatibility `theme.hpp` removal and the whole-tree literal scan. The
new shell does not link to or instantiate the old `MainWindow`.

### Placeholder handlers show a message

Actions and launcher cards whose dialog or pane belongs to WP3 to WP8 are
registered, have their final menu/palette/toolbar placement and level, and
show a status-bar sentence that names the work package boundary. They do not
simulate a result and do not disable the rest of the frame. The handlers are
the only temporary part; WP3 to WP8 replace them in place.

### Unsaved-changes prompt uses QMessageBox

`ConfirmDialog` has two buttons. Discarding a setup needs Save, Don't save and
Cancel, so WP2 uses a `QMessageBox` with the spec's title, body and button
labels and honours `ui/askOnDiscard`. WP8 replaces it with the shared dialog
and the Settings toggle. The decision protects user data in the interim.

### Compact layout at 960x640

The design says that at the 960x640 minimum the right column becomes one
tabbed dock and the toolbar drops to icons. The implementation switches at
width < 1100 px or height < 700 px, so a 960-wide window is already compact,
the toolbar is icon-only, Inspector and Send are tabbed, and Monitor starts at
156 px. Qt's toolbar extension handles overflow; there is no hand-built More
menu. The shell requests the token sizes for the ports, inspector and monitor
docks. With the current one-line placeholder panes, QMainWindow's size hints
settle the split nearer the minimum; WP3 and WP4 set the final pane content and
the reviewed 360/340/196 split. The minimum sizes are already the token values.

### Menu and icon colours after a theme switch

The bundle's QSS template leaves `QMenuBar` and `QMenu` text colour to the
palette. On Windows after a runtime switch to dark that rendered the menu text
dark-on-dark. The product template now binds those three selectors to the
`ink` token. Action icons had the same problem because `QAction` stores the
rendered `QIcon`; `ActionRegistry` flushes `IconProvider` and rebuilds every
registered icon on `Theme::themeChanged`. No token value changed.

### Shell library name

The plan proposed one `vss_gui_ui` library. WP2 used `vss_gui_shell`, which
links the widget, backend and theme libraries and is what the shell tests
link with `FakeStudioBackend`. The old executable retains its flat sources.
The dependency direction matches the plan; only the target name and the
deletion timing differ.

### Toolbar Share is deferred to WP6

Share stays in the Topology menu in Simple, matching the IA. The design's
Advanced toolbar adds Share, but WP2 has no share dialog; WP6 owns that flow
and can add the toolbar-only advanced action (or change the toolbar policy)
without making the menu action Advanced. Mock and Fit are present in Advanced
so the mode difference is already visible.

### Review-only screenshot switches

`VSS_SCREENSHOT_EMPTY=1` renders the launcher instead of the sample topology,
and `VSS_SCREENSHOT_THEME=light|dark` selects the theme. They exist only on
the documentation `--screenshot` path; normal startup ignores them and they do
not write `QSettings`. The default screenshots use two loopback ports, one
link, one mock port and a short write so the frame shows real data shapes.

## WP5

### Linked pair names are chosen by the adapter, not typed

The design lets the user edit First port and Second port on Windows. The
current adapter cannot advertise a chosen driver port name: `studio` creates a
virtual port from a free com0com pair and advertises the pair's other end, and
there is no API to select the half or override the advertised path. Honouring
the fields would either lie about the path or require a `vss_studio` change,
which WP5 is not allowed to make. The pair form therefore shows the real free
COM ports the adapter found, read-only, and creates only when at least two
complete com0com pairs are free. On Linux and macOS the fields are read-only
placeholders because the pseudo-terminal paths do not exist until creation.
The inline uniqueness message still exists for a stale or duplicate free-pair
value, but a user normally cannot edit the fields to reach it; the free-pair
filter and the disabled "There are not enough free driver pairs" state are the
visible degradation. The pair result sentence still names the free ports, and
the status-bar summary after creation names the paths the engine actually
advertised.

### Linked pairs use existing driver pairs and do not elevate

The mockup says Windows will ask for administrator permission. This WP5 path
never runs `setupc.exe`: it claims two free com0com pairs that are already
installed, then links the two Studio ends. Showing the warning would be false,
so the form shows "Windows uses free com0com pairs for this pair, so it does
not ask for administrator permission." instead. `permission_denied` from the
adapter is still mapped to the error pattern with copyable details and recovery
buttons, so a future adapter that creates a new pair can reuse the dialog.

### Add a port does not duplicate the Attach form

WP5 builds the full Attach a real device dialog, but the Real device choice in
Add a port shows a one-sentence continuation note and the primary "Next" opens
that dialog. Embedding a second copy of the device list and settings would
double the state, validation and accessibility surface for the same flow. The
Linked pair form is embedded because it is the same dialog the launcher calls
with Linked pair selected.

### Attach opens the device and creates a separate port

The spec says programs should open the port Studio creates, not the physical
device. `studio::attach_serial` alone exposes the physical path as the
endpoint's path, so AttachDialog composes three real adapter calls: attach the
device under `<name>-input`, create an automatic virtual port under `<name>`,
and split the device to it. A failure at any step removes the ports already
created. The physical endpoint remains visible to the future Ports pane; this
is the same shape the Share scenario uses.

### Device-in-use detection is supplied by the adapter, not the engine

The engine's serial enumeration cannot prove that another process holds a
device without trying to open it. A `device_view::in_use` flag was added to the
adapter; `EngineStudioBackend` leaves it false and tests can set it. The Attach
dialog always disables and explains virtual/com0com rows, and shows an "In use"
row with its reason when a backend reports it. This is the degradation the
engine-gap table asks for instead of probing the device.

### Watch focuses the Monitor but cannot filter it yet

WP5 creates and links the watcher through the adapter, selects the watched
port and opens, raises and focuses the Monitor dock. The Monitor itself belongs
to WP3, so there is no filter API to call. The filter request is recorded here
and must be wired when WP3's monitor model lands; the success status names the
source and watcher so the action is not silent.

### Pair success has no separate ready dialog

The copy deck defines Port ready for a single port. The pair form's result
sentence and the status-bar summary name both advertised paths and the Main
window takes over; inventing a second ready screen would add copy that the
bundle does not define. The pair's two ports and link are real adapter state
and are selected in the shared selection service.

### Setup check and Connect ports stay placeholders

The Port ready actions "Open setup check" and "Connect COM7 to something"
emit the shell's existing placeholder status. WP8 owns the setup check dialog
and WP6 owns Connect ports, so WP5 does not simulate either result.

### Rich result sentences and ErrorDialog recovery actions

`ResultSentence` gained a two-argument `set_result_text` so the visible sentence
can bold port names while screen readers receive the plain sentence. The rich
HTML is never used as the accessible name. `ErrorDialog` gained `add_action` so
Attach can offer "Try again" and "Choose another" before the existing Copy
details and Close; the destructive action order and initial focus are
unchanged.

### Add a port Advanced types are placeholders

Network endpoint and Mock fixture are other work packages. The cards appear in
Advanced with their spec descriptions, select a clear placeholder page and
disable the primary action; no backend call is made and no success is claimed.
