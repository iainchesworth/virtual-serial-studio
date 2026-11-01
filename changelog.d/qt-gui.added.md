A Qt 6 desktop application: a ports table with advertised paths and live
counters, a topology graph, an inspector with control-line toggles, a traffic
monitor with ASCII and hex views, a send panel, and JSON topology open/save.

The GUI snapshots the studio on a timer and never touches widgets from hub
threads. `vss_gui --selftest` runs it headless in CI.
