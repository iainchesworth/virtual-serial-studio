# Testing

The repository ships unit, system and performance suites, all driven by CTest.

```bash
ctest --test-dir build --output-on-failure
./build/bin/vss_unit_tests --filter=hub --verbose
./build/bin/vss_unit_tests --list
```

* **Unit** (149 cases) — bytes, control lines, serial settings, errors,
  traffic log, the loopback transport, endpoints, topology, events, serial
  transport, virtual ports, enumeration, com0com parsing, JSON documents and
  the studio facade.
* **System** (12 cases) — a real serial pair (a com0com pair on Windows, or
  `VSS_LOOPBACK_A`/`VSS_LOOPBACK_B` elsewhere) carries data both ways, survives
  a 256 KB integrity transfer, shares a port with listeners, drives the native
  control-line layer, rolls back a failed bridge, and live TCP **and UDP**
  peers exercise the network bridges. Every case skips cleanly when no hardware
  exists, unless `VSS_REQUIRE_LOOPBACK=1` is set — which CI does, so a missing
  pair fails the job instead of quietly reducing what is tested. Linux and
  macOS CI create the pair with `tools/pty_loopback.py`, so the physical
  backend runs on every platform.
* **Polled bus** - one system case runs the whole bus scenario on every
  operating system, with no hardware: a master replayed from a recording, two
  devices answering from scripts (one labelled as a real serial device), and a
  device on another computer played by an RFC 2217 client over loopback, all on
  one shared bus. It checks that each reply lands inside its window with
  margin, 200 ms for two devices and 450 ms for one, and that no member hears
  itself. A run whose times miss is repeated up to three times, because another
  process taking the CPU can spoil a time; the byte checks run every time.
* **Performance** — routing, event dispatch, JSON round-trips and mock writes,
  gated against `perf/floors.json`.
* **Profiling** — `VSS_TRACY=ON` instruments the pipeline with Tracy from
  vcpkg; CI captures a trace of a real socket and pseudo-terminal round trip,
  exports it with `tracy-csvexport` and publishes the per-zone table. See
  [the profiling page](profiling.md).

## Coverage

Coverage uses LLVM profile instrumentation and is gated at **90% statements**
and **80% branches**:

```bash
export CXX=clang++
cmake --preset linux-coverage
cmake --build --preset linux-coverage
mkdir -p build/linux-coverage/profiles
LLVM_PROFILE_FILE="$PWD/build/linux-coverage/profiles/%p-%m.profraw" \
  ./build/linux-coverage/bin/vss_unit_tests
LLVM_PROFILE_FILE="$PWD/build/linux-coverage/profiles/%p-%m.profraw" \
  ./build/linux-coverage/bin/vss_system_tests
python tools/coverage_report.py --build build/linux-coverage \
  --exclude libs/vss_testkit \
  --gate-exclude libs/vss_ports/src/vcom_driver.cpp \
  --min-lines 90 --min-branches 80
```

Measured on Windows 11, clang-cl 23, Debug: **90.15% statements**, **82.28%
branches**. The report lists every file and marks the ones outside the gated
set; `vcom_driver.cpp` is outside it until the driver exists, because its
detection path needs an installed kernel driver to exercise.

## Performance

```bash
vss_perf --output=perf-results.json
python tools/perf_report.py perf-results.json --floors perf/floors.json
```

Indicative results (Windows 11, MSVC 19.51, x64, Release, `vss_perf`):

| Benchmark | Result |
|---|---|
| `hub_route` | ~2.4 GB/s |
| `event_dispatch` | ~1,230,000 events/s |
| `config_roundtrip` | ~2,850 documents/s (23 KB each) |
| `mock_write` | ~359 MB/s |

## Profiling

Tracy is compiled into Debug builds by default, and can be compiled into a
Release build too (`VSS_TRACY=ON`, or a `*-tracy*` preset). In a Release build
it is dormant: no profiler thread, no socket, and each zone costs one relaxed
load until the program is started with `--profile` (or `VSS_PROFILE=1`, or the
switch in Settings, Performance, Profiling). The listener is bound to this
computer, never broadcasts, and sends nothing until a profiler connects. Once
started it runs in on-demand mode, so nothing is recorded until a profiler
connects and attaching to an already running process is the normal workflow.

```bash
# Debug: Tracy is on when configured with the vcpkg toolchain
cmake --preset linux-debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Release with the profiler compiled in but dormant
cmake --preset linux-tracy-gui
cmake --build --preset linux-tracy-gui

# Capture a run, then export it
tracy-capture -a 127.0.0.1 -o trace.tracy &
./build/linux-tracy-gui/bin/vss_perf --tracy --profile-wait=30
wait
tracy-csvexport trace.tracy > trace.csv
python tools/tracy_report.py trace.csv --areas
```

Every test case runs in a zone named after it, so `VSS_PROFILE=1` on any test
program (or `--tracy` on `vss_unit_tests` and `vss_system_tests`) gives each
test's duration and the zones it called. `vss_perf` adds a micro-benchmark per
component; `perf/floors.json` gates them, with `max` limits for costs such as
the dormant cost of a zone.

CI captures the performance suite, the offscreen GUI self-test and the unit
tests, checks each capture against its profile in `perf/required-zones.json`
and publishes the tables; see [the profiling page](profiling.md) for the zone
list, the options and the caveats.

## Sanitizers

Every CTest suite runs under a sanitizer, the GUI suites included, at -O1 (/O1 on
MSVC), and the first finding fails the run.

| Preset | Sanitizers | Runs | Where |
|---|---|---|---|
| `linux-asan-ubsan-gui` | AddressSanitizer, LeakSanitizer, UBSan | Everything: `unit`, `system`, `headers`, every `gui_*` suite, the fuzz replays | Pull request gate (Clang); nightly (GCC) |
| `linux-tsan-gui` | ThreadSanitizer | The same list | Gate when threading code changed, and every merge-queue entry (Clang); nightly |
| `windows-msvc-asan-gui` | AddressSanitizer | The same list | Nightly |

MSVC has no UBSan, ThreadSanitizer or LeakSanitizer. The plain presets
(`linux-asan-ubsan`, `linux-tsan`, `windows-msvc-asan`) build without the GUI and are
the quick local check. A new test is picked up with no change here, as the presets run
the whole of CTest. The GUI runs on `QT_QPA_PLATFORM=offscreen`.

```bash
cmake --preset linux-asan-ubsan-gui        # or linux-tsan-gui, windows-msvc-asan-gui
cmake --build --preset linux-asan-ubsan-gui
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 ctest --preset linux-asan-ubsan-gui
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:ignore_noninstrumented_modules=1:suppressions=$PWD/tests/tsan.supp \
  ctest --preset linux-tsan-gui
```

Run the system tests with a serial pair, as CI does (`tools/pty_loopback.py`, then
`VSS_LOOPBACK_A`, `VSS_LOOPBACK_B` and `VSS_REQUIRE_LOOPBACK=1`). Use the pinned Qt
(`.github/toolchain-versions.json`): a distribution Qt of another version can differ in
what it compiles, and CMake may pick it up in place of the kit in `QT_ROOT_DIR` unless
you pass `-DQt6_DIR=<kit>/lib/cmake/Qt6`.

* **Timeouts.** A sanitizer build runs several times slower, and the GUI self-test takes
  about ten minutes under MSVC AddressSanitizer. `VSS_SANITIZER_TIMEOUT_FACTOR` (default 5)
  multiplies every test timeout in a sanitizer build. The timing tests (`system: a polled
  bus ...`) measure real replies, so run them on an idle machine.
* **Qt and ThreadSanitizer.** Qt comes as a prebuilt kit that is not instrumented, and its
  locks and queues use futexes and atomics that ThreadSanitizer cannot see. Without help it
  reports races between Qt's own allocations in its thread pool, and in events that a
  worker posts to the GUI thread through a queued connection, though Qt orders them
  correctly. `ignore_noninstrumented_modules=1` stops it tracking accesses made inside Qt
  and keeps checking every access made by this project's code, so a race between the hub
  or endpoint threads and the GUI thread in our own code is reported. A deliberate race in
  a small program still fails under the same setting.
* **Suppressions.** `tests/tsan.supp` (passed as `suppressions=` in the CI jobs) has one
  entry, for the construction of Qt's `QCallableObject` slot object, which a worker builds
  for a queued call and the GUI thread runs. The header template is compiled into our code
  and so is instrumented, but the queue lock that orders the two is inside Qt. The entry
  matches only that constructor, not the lambda bodies, so a race on the data a queued
  lambda touches is still reported. Add an entry only with a reason that holds for every
  report it can match.
* **Found and fixed by this work.** `announce_name_change` leaked a `QAccessibleEvent` on
  every name change, thousands per GUI test run (LeakSanitizer); `ToastManager` downcast a
  toast that was being destroyed (UBSan); and `reconnector::run` waited on a time point
  that lives in a map entry another thread could erase, a heap-use-after-free
  (AddressSanitizer).

## Fuzzing and the GUI journeys

The topology and endpoint URI parsers are fuzzed; see [Fuzzing](fuzzing.md).
The `gui_journeys` test sends key presses and mouse clicks to the real
window through QTest and runs wherever Qt and its Test module are installed.
