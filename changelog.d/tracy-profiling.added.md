Added optional Tracy instrumentation that is part of a normal Debug build.
`VSS_TRACY` is on by default for Debug configurations made with the vcpkg
toolchain and off for Release, so release binaries stay lean; pass
`-DVSS_TRACY=ON` to profile one when a field problem needs triage.

The manifest asks vcpkg for Tracy's `on-demand` feature, so nothing is
recorded until a profiler connects and a profiler can attach to a process that
is already running. Zones cover hub routing and dispatch, endpoint reads and
writes, JSON conversion and the benchmark phases. `vss_perf --tracy` adds two
end-to-end phases — a socket echo through the hub and a pseudo-terminal round
trip — and emits a frame per chunk.

A CI job captures a trace, exports it with `tracy-csvexport`, publishes the
per-zone table in the job summary and uploads the trace, CSV and report.
`tools/tracy_report.py` renders the table and fails when an expected zone is
missing.
