Added the Ports, Inspector, Send and Monitor panes to the redesigned GUI.
Ports has painted three-line Simple rows with state chips and a 24 px path copy
target, and an Advanced table with filtering and RX/TX/Dropped columns. The
Inspector shows the live state, path, traffic, control-line chips and the
Advanced DTR/RTS/Break controls; the exact waiting sentence appears only when
the adapter reports a definite NotOpen peer, not for Unknown. Send validates
hex live, adds CRLF by default and adds Advanced Repeat and saved sends. Monitor
consumes the coalesced event batches into a bounded 10,000-row model with ASCII
and hex previews, real DROP rows from link counters, pause/follow/clear,
Advanced port/direction/search filters, export and a Log tab. One selection
service now keeps Ports, Inspector, Send, Monitor and the later topology view on
the same port. The shell gained structural dirty tracking, confirmed port
removal, real copy handlers and steady dock sizing. Seven pane tests cover
state/role mapping, the 10,000-row burst and ring cap, ASCII/hex formatting,
filter combinations, Send hex/CRLF validation, selection sync, the exact empty
state and the no-per-row-widget rule. The legacy flat GUI files left the
executable because the new pane classes reuse their names; WP9 deletes them.
