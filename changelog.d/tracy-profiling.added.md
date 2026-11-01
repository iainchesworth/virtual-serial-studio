Added optional Tracy instrumentation. Configure with
`-DVSS_TRACY=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
and the manifest in `vcpkg.json` supplies the client.

Zones cover hub routing and dispatch, endpoint reads and writes, JSON
conversion and the benchmark phases. `vss_perf --tracy` adds two end-to-end
phases — a socket echo through the hub and a pseudo-terminal round trip — and
emits a frame per chunk.

A new CI job captures a trace, exports it with `tracy-csvexport`, publishes the
per-zone table in the job summary and uploads the trace, CSV and report.
`tools/tracy_report.py` renders the table and fails when an expected zone is
missing.
