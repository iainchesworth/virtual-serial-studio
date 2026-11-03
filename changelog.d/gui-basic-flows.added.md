Added the basic GUI flows: Add a port with its Virtual port, Linked pair and
Real device choices; the copyable Port ready dialog with live peer state and
remembered details; Connect two programs; Attach a real device with device
enumeration, reasons for unavailable rows, settings, a manual path and an
Advanced address override; and Watch traffic, which creates a Studio-only
loopback watcher and a monitor link. The Ports and Topology actions and the
launcher cards now open these dialogs instead of showing placeholder status,
and every backend failure is reported in `ErrorDialog` with copyable details.
The adapter gained a `device_view::in_use` flag, `ResultSentence` gained a rich
text form with a plain accessible sentence, and `ErrorDialog` gained recovery
actions. `tests/CMakeLists.txt` registers `vss_gui_flows_tests`, 20 tests (one of them
against the real engine adapter), covering validation,
unique suffixes, cancel and no-op paths, ready variants, peer capability,
pair/elevation failures, attach refresh and recovery, watch rollback,
accessible names, 24 px targets, keyboard flow and both themes.
