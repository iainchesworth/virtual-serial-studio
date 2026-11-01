# Profiling with Tracy

The data path is profiled with [Tracy](https://github.com/wolfpld/tracy),
pulled from [vcpkg](https://vcpkg.io). Instrumentation is compiled in only when
`VSS_TRACY` is on; without it every macro in
[`profile.hpp`](../libs/vss_core/include/vss/core/profile.hpp) is a no-op.

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

`vss_perf --tracy` adds two end-to-end phases: a real socket echo through the
hub (`tcp://127.0.0.1`, one connection) and, when `VSS_LOOPBACK_A` and
`VSS_LOOPBACK_B` name a pair of pseudo terminals, a write on one side read on
the other. Each phase emits a frame per chunk, so a Tracy timeline shows the
chunk cadence as well as the totals.

## Building it

Debug, with the toolchain that supplies the package:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset debug -DVSS_BUILD_PERF=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset debug
```

Release, opting in for a triage build:

```bash
cmake -S . -B build-tracy -DCMAKE_BUILD_TYPE=Release -DVSS_TRACY=ON \
  -DVSS_BUILD_PERF=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-tracy
```

The manifest ([`vcpkg.json`](../vcpkg.json)) pins `tracy` and the vcpkg
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
./build-tracy/bin/vss_perf --tracy   # `vss_perf` needs -DVSS_BUILD_PERF=ON
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
2. builds `tracy-capture` and `tracy-csvexport` from Tracy v0.14.1,
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
