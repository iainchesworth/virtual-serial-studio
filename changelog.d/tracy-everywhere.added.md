Added Tracy profiling across the whole application, switchable at run time.

Zones now cover the engine and every transport (virtual, loopback, serial,
TCP, UDP, RFC 2217, the Windows driver and helper IPC, permission probes), the
studio service, the CLI, the GUI (every event the application dispatches with
its receiver and type, painting, the event loop, panes, dialogs, layout and
link routing), the test runners (one zone per test) and the new per-component
benchmarks in `vss_perf`. Rates and queue depths are plotted, state changes are
marked, and `tools/tracy_report.py` can group a capture by area and fail when
an area or a named zone goes missing.

The instrumentation is compiled in but dormant in a Release build of
`VSS_TRACY=ON`: Tracy is built with a manual lifetime, so there is no profiler
thread, no socket and no buffer until profiling is turned on, and each zone
costs one relaxed load. Turn it on with `--profile` (GUI, CLI, `vss_perf`),
`VSS_PROFILE=1`, or Settings, Performance, Profiling in the GUI, which also
shows the state in the status bar and About. The listener is bound to
127.0.0.1 unless `--profile-remote` is given, never broadcasts, and sends
nothing until a profiler connects. Debug builds still start profiling by
themselves. Turning it off stops recording at once; Tracy cannot close its
socket before the program exits.

`vss_perf` reports the cost of a zone while dormant, armed and recording, and
perf/floors.json now accepts a `max` limit for costs. New presets
`linux-tracy-gui` and `windows-msvc-tracy-gui` build a Release GUI with the
profiler compiled in.
