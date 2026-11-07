# Profiling with Tracy

The data path is profiled with [Tracy](https://github.com/wolfpld/tracy),
pulled from [vcpkg](https://vcpkg.io). Instrumentation is compiled in only when
`VSS_TRACY` is on; without it every macro in
[`profile.hpp`](https://github.com/iainchesworthlabs/virtual-serial-studio/blob/develop/libs/vss_core/include/vss/core/profile.hpp) is a no-op.

## When it is on

| Configuration | Default |
|---|---|
| Debug, vcpkg toolchain | **on** |
| Debug, no vcpkg toolchain | off (the package cannot be found) |
| Release, any toolchain | **off** |
| Release with `-DVSS_TRACY=ON` | on |

Debug is the everyday case: the client is in the binary and a profiler can
attach at any time. Release stays lean, but a release binary can be rebuilt
with the option on when a field problem needs triage.

The client runs in **on-demand** mode (the manifest asks vcpkg for the
`on-demand` feature). Nothing is recorded until a profiler connects, and a
profiler can connect to a process that has already been running for hours.
That is what makes "attach when something looks wrong" work.

## What is instrumented

| Zone | Where | Shows |
|---|---|---|
| `hub.route` | `hub::route_data` | fan-out from one endpoint to its links |
| `hub.dispatch` | `hub::dispatch` | event fan-out to subscribers |
| `endpoint.read` | `endpoint::on_read` | transport completion, routing, statistics |
| `endpoint.write` | `endpoint::on_write` | write queue progress |
| `config.to_json` / `config.from_json` | the JSON document | topology round-trips |
| `perf.*` | `perf/perf_main.cpp` | the benchmark phases themselves |
| `gui.bridge.*` | `EventBridge` | draining the engine's events into slices for the window |
| `gui.backend.*` | `EngineStudioBackend` | the look at the engine (`rebuild`), and the time spent in each signal it raises |
| `gui.shell.*`, `gui.status.refresh`, `gui.setup.*` | the main window, the status bar, the setup report | what the window does for a change of ports or a batch of events, and the operating-system probes behind the readiness chip |
| `gui.topology.*`, `gui.monitor.*`, `gui.fixture.tick` | the canvas, the Monitor, the byte generator | layout, link routing, context menus, the traffic list |
| `gui.stall_ms`, `gui.busy_ms` (plots) | `support::StallProbe`, started in `main` when profiling is on | how late the window's event loop ran and how much CPU the window used between two ticks |

## Finding a frozen window

The GUI thread must never be called from, or wait for, the engine's threads, so
a "Not responding" window is always work on that thread: a slot that does too
much, or too many small ones. To find it:

1. Build and run `vss_gui` with Tracy on (`windows-msvc-debug-gui` with the
   vcpkg toolchain has it on by default) and attach a profiler or
   `tracy-capture`.
2. Look at the `gui.stall_ms` plot for the spikes. The zones under a spike are
   the cause; `gui.bridge.emit` contains everything that runs for a batch of
   events and `gui.backend.emit_ports_changed` / `emit_links_changed` /
   `emit_stats_changed` contain the pane refreshes.
3. Reproduce it in a test where you can. `apps/vss_gui/src/shell/tests/
   test_responsiveness.cpp` drives the real engine and the real window with a
   busy loopback and fixture and fails if the GUI thread uses more than the
   budget of CPU time between two ticks of the probe. Set
   `VSS_GUI_STALL_BUDGET_MS=100` to hold it to the strict figure on a quiet
   computer; the default leaves room for a loaded build machine.

A profiling build is for developers: the Tracy client listens for a profiler on
this computer's network port (8086) from the moment the program starts, and
records nothing until one connects. Release builds do not contain it unless
they are configured with `-DVSS_TRACY=ON`.

`vss_perf --tracy` adds two end-to-end phases: a real socket echo through the
hub (`tcp://127.0.0.1`, one connection) and, when `VSS_LOOPBACK_A` and
`VSS_LOOPBACK_B` name a pair of pseudo terminals, a write on one side read on
the other. Each phase emits a frame per chunk, so a Tracy timeline shows the
chunk cadence as well as the totals.

## Building it

Debug, with the toolchain that supplies the package:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-debug -DVSS_BUILD_PERF=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset linux-debug
```

Release, opting in for a triage build:

```bash
cmake --preset linux-tracy
cmake --build --preset linux-tracy
```

The manifest ([`vcpkg.json`](https://github.com/iainchesworthlabs/virtual-serial-studio/blob/develop/vcpkg.json)) pins `tracy` and the vcpkg
baseline. `vcpkg` installs the client; CMake finds it and defines `VSS_TRACY`
and `TRACY_ENABLE` for the whole tree. A Release tree without vcpkg simply has
the option off; asking for it there is a configuration error with a message
that names the missing toolchain.

## Capturing a trace

Tracy's client streams to a profiler: either the GUI, or the headless
`tracy-capture` command line tool. The capture tool is part of Tracy's
`cli-tools` feature (`vcpkg install tracy[cli-tools]`), and CI builds it from
the pinned source tag to keep the Linux job light.

```bash
tracy-capture -o trace.tracy &          # waits for a client, saves on disconnect
./build/linux-tracy/bin/vss_perf --tracy
wait                                    # the client exiting ends the capture
tracy-csvexport trace.tracy > trace.csv
python tools/tracy_report.py trace.csv
```

The report prints a Markdown table of the hottest zones:

```
| Zone | Calls | Total (ms) | Mean (us) | Max (us) | Share |
| --- | ---: | ---: | ---: | ---: | ---: |
| `perf.e2e.tcp` | 1 | 26.85 | 26851.1 | 26851.1 | 3.97% |
| `hub.route` | 8448 | 14.47 | 1.7 | 99.3 | 2.14% |
```

`--require a,b,c` fails when an expected zone is missing, which is how CI
notices that the instrumentation or the workload stopped covering the
pipeline. `--markdown` and `--json` write the same data to files.

## What CI does

The `End-to-end profiling (Tracy)` job:

1. installs vcpkg at the pinned baseline and resolves the manifest,
2. builds `tracy-capture` and `tracy-csvexport` from Tracy v0.14.1, or restores
   them from the cache. Building them downloads CPM.cmake and sources from
   GitHub, so a build retries up to three times, and the finished tools are
   cached under a key that names the Tracy version. The cache fills on pushes
   to `develop`, which pull requests can read. The tools are built with
   `NO_ISA_EXTENSIONS=ON`, because Tracy otherwise builds for the CPU it runs
   on and a cached copy stops with an illegal instruction on another runner.
   Change `TRACY_VERSION` in `ci.yml` to move to another release,
3. configures with `VSS_TRACY=ON`, builds `vss_perf`,
4. creates a pseudo-terminal pair so the serial phase runs too,
5. captures `vss_perf --tracy`, exports the CSV, requires
   `perf.e2e.tcp`, `perf.e2e.pty`, `perf.e2e.pty.chunk`, `hub.route` and
   `config.to_json`, and appends the table to the job summary,
6. uploads the trace, CSV, report and raw benchmark output as artifacts.

## Reading the numbers

- **Share** is the fraction of the captured trace; zones nest, so a parent's
  share includes its children. Compare `total` across zones rather than adding
  them.
- **Mean** is per call. `hub.route` runs thousands of times per phase; the
  end-to-end phase zones run once and are there for the timeline.
- Tracy's overhead is real (both sides timestamp every zone), so an
  instrumented build is slower than the Release binaries the floors measure.
  Use it for shape and attribution, and `vss_perf` with `perf/floors.json` for
  the gated numbers.
