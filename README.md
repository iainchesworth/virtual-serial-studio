# Virtual Serial Studio

**A playground for real, virtual and networked serial ports.**

Virtual Serial Studio opens one serial port and gives it to as many applications
as you like. It splits a device across several virtual ports, merges several
publishers into one, redirects traffic, bridges a COM port to TCP, RFC 2217 or
UDP, drives the modem control lines, and records everything it sees. It is
built on [Boost.Asio](https://boost.org/libs/asio) and on
[Boost.VirtualSerial](../boostasio-virtualserial), the drop-in serial stream
abstraction that puts the transport behind a seam.

![Virtual Serial Studio](docs/images/studio.png)

## What it can do

| Capability | How |
|---|---|
| Share one serial port with many applications | Create N virtual ports and `split` the real one across them |
| Merge several writers into one device | `join` two or more publishers into one target |
| Give an application its own serial port | Pseudo terminal on Linux/macOS, com0com pair on Windows, first-party driver planned |
| Replace com0com entirely | Seam in place; the Windows driver is specified in [docs/windows-com-driver.md](docs/windows-com-driver.md) |
| Bridge a device to the network | `tcp://`, `udp://` and `rfc2217://` endpoints are first-class ports |
| Test RS-232 control lines | DTR/RTS/BREAK are drivable, CTS/DSR/DCD/RI are reported, a null-modem link crosses them |
| Watch traffic without disturbing it | One-way `monitor` links and a bounded traffic ring buffer |
| Script a device | The serial library's mock backend is exposed as an endpoint |
| Save and reload a topology | Validated JSON documents, opened by the GUI and by `vss_cli run` |
| Measure the data path | `vss_cli bench` and a CI performance suite with enforced floors |

## Building

Requirements: CMake 3.21+, a **C++23** compiler, Boost 1.74+ headers, and a
checkout of [Boost.VirtualSerial](../boostasio-virtualserial). Qt 6 is optional
and only needed for the GUI.

```bash
# Boost headers, if you do not already have them
python tools/fetch_boost.py --dest .cache/boost-root

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVSS_BOOST_ROOT="$PWD/.cache/boost-root" \
  -DVSS_VIRTUAL_SERIAL_DIR=/path/to/boostasio-virtualserial \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/msvc2022_64   # optional, for the GUI

cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, run the configure and build from a Visual Studio developer prompt
(`vcvars64.bat`) so CMake can find `cl.exe`. The sibling checkout is picked up
automatically when it is named `boostasio-virtualserial` next to this
repository, and its `.cache/boost-root` is used for Boost.

Options: `VSS_BUILD_TESTS`, `VSS_BUILD_GUI`, `VSS_BUILD_TOOLS`,
`VSS_BUILD_PERF`, `VSS_COVERAGE`, `VSS_WARNINGS_AS_ERRORS`, `VSS_INSTALL`.
`CMakePresets.json` provides `debug`, `release`, `coverage` and `perf`.

### Language standard

The application targets **C++23** (`std::span`, `std::string_view`, defaulted
comparisons, designated initialisers, concepts, `std::to_underlying`) and builds
clean with MSVC `/std:c++latest` and clang-cl `-std=c++23` under
`-DVSS_WARNINGS_AS_ERRORS=ON`.

C23 is the *C* standard and does not apply to a C++/Boost.Asio application; if
you meant "the newest standard", C++23 is that target. Boost.VirtualSerial
itself keeps a C++14 baseline with C++20 concepts, so consuming it from a
C++23 translation unit is strictly compatible — the library is never asked to
compile as anything newer than it already supports.

Two deliberate deviations from what a greenfield C++23 codebase might do:

* `vss::result<T>` is `boost::system::result`, not `std::expected`. The serial
  library, Boost.Asio and the OS all speak `boost::system::error_code`; using
  `std::expected` would put a conversion at every boundary and lose the
  `error_code` category messages. `boost::system::result` is the idiomatic
  choice inside the Boost ecosystem and already offers `and_then`/`transform`.
* The event sink is type-erased behind `std::function` at the API boundary
  (constrained by the `vss::EventSink` concept) rather than templated through
  the hub. One indirection per event buys a stable, non-template `hub` type and
  a dispatcher that does not have to be recompiled for every callable.

## Quick start

### The GUI

```bash
vss_gui
```

`Ports -> Add virtual port` creates a port and tells you the path another
application should open. `Ports -> Attach serial port` opens a real device,
an RFC 2217 server or a TCP/UDP bridge. `Topology` links ports, `Scenarios`
builds the common shapes in one click, and the monitor shows the traffic as it
flows. `File -> Save` stores the topology as JSON.

The GUI can also prove itself without a display or a user:

```bash
QT_QPA_PLATFORM=offscreen vss_gui --selftest
```

### The command line

```bash
vss_cli devices                     # serial ports and com0com pairs
vss_cli virtual                     # one virtual port; prints what to open
vss_cli loopback                    # a virtual port plus a monitor, no hardware
vss_cli share serial://COM3 --listeners=3 --seconds=30
vss_cli bridge serial://COM3 tcp://192.168.1.50:4001 --seconds=30
vss_cli run lab.json --seconds=60
vss_cli bench --from=serial://COM3 --to=serial://COM5 --bytes=10MB
vss_cli selftest
```

Run `vss_cli --help` for the full list.

### A topology file

```json
{
  "version": 1,
  "name": "lab",
  "ports": [
    { "name": "device", "kind": "physical",
      "uri": "serial://COM3?baud=115200",
      "serial": { "baud_rate": 115200, "parity": "none" } },
    { "name": "terminal", "kind": "virtual", "virtual_kind": "auto" },
    { "name": "logger",   "kind": "virtual", "virtual_kind": "loopback" }
  ],
  "links": [
    { "mode": "split", "sources": ["device"], "targets": ["terminal", "logger"] }
  ]
}
```

`vss_cli run lab.json` applies it; the GUI opens the same file.

## The model

Three ideas carry the whole application.

**An endpoint** is one transport with a name: a real UART, a pseudo terminal, a
com0com end, a TCP socket, an RFC 2217 session, or a mock device. It owns its
read loop, write queue, counters and control-line state, and it is driven on a
strand so a transport is never touched by two threads at once.

**A link** connects endpoints. `duplex` links carry traffic both ways, `one_way`
links carry it one way, and `monitor` links are passive taps that do not drive
the peer's modem lines. A single link can fan out to many targets (`split`) or
accept many sources (`join`), and every link can arbitrate between publishers:
either `interleave` them or give one publisher `owner`ship.

**A hub** owns the `io_context`, the endpoints, the routing table and the event
dispatcher. The `studio` layer above it adds the bits applications need:
advertised paths, composite scenarios, JSON persistence and com0com detection.

```
vss_cli / vss_gui
      │
   vss_studio      studio: scenarios, persistence, advertised paths
      │
  ┌───┴──────────────┬─────────────────┐
vss_config       vss_ports          vss_core
 JSON documents   PTY, com0com,     hub, endpoint, links,
                  native lines,     control lines, events
                  serial transport
```

## Virtual ports by platform

| Platform | Kind | What a third-party application opens |
|---|---|---|
| Linux, macOS | `pty` | `/dev/pts/N` — a real terminal that behaves like a serial port |
| Windows | `auto` with com0com installed | One end of a com0com null-modem pair, e.g. `COM5` |
| Any | `loopback` | An in-process device: useful for demos, monitoring and tests |

`auto` picks the best available kind and reports the path it chose. A pair is
never wired into the hub twice: once an endpoint claims a com0com pair, later
listeners fall back to loopback instead of fighting over the same device.

On Windows, creating a **new** com0com pair needs administrator rights, because
`setupc.exe` carries an elevation manifest. `vss::detect_com0com()` and the pair
list work without elevation, and the GUI reports the installation state.

## Control lines

Every endpoint models DTR, RTS, CTS, DSR, DCD, RI and BREAK. The front end
drives DTR, RTS and BREAK, and reports the rest. When two endpoints are linked
with `emulate_modem_lines` (the default), the link behaves like a null-modem
cable:

```
A.DTR ──► B.DSR        A.RTS ──► B.CTS
B.DTR ──► A.DSR        B.RTS ──► A.CTS
```

That is enough to exercise software that watches carrier detect, requires
hardware flow control, or treats a dropped DTR as a hang-up. The inspector in
the GUI drives the lines directly; `endpoint::set_line` does the same from code.
On a device that cannot support a line the call reports `errc::unsupported`
rather than pretending to work.

## Testing

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
* **Performance** — routing, event dispatch, JSON round-trips and mock writes,
  gated against `perf/floors.json`.

### Coverage

Coverage uses LLVM profile instrumentation and is gated at **90% statements**
and **80% branches**:

```bash
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  -DVSS_COVERAGE=ON -DVSS_BUILD_GUI=OFF
cmake --build build-cov
mkdir -p build-cov/profiles
LLVM_PROFILE_FILE="$PWD/build-cov/profiles/%p-%m.profraw" \
  ./build-cov/bin/vss_unit_tests
LLVM_PROFILE_FILE="$PWD/build-cov/profiles/%p-%m.profraw" \
  ./build-cov/bin/vss_system_tests
python tools/coverage_report.py --build build-cov \
  --exclude libs/vss_testkit \
  --gate-exclude libs/vss_ports/src/vcom_driver.cpp \
  --min-lines 90 --min-branches 80
```

Measured on Windows 11, clang-cl 23, Debug: **90.15% statements**, **82.28%
branches**. The report lists every file and marks the ones outside the gated
set; `vcom_driver.cpp` is outside it until the driver exists, because its
detection path needs an installed kernel driver to exercise.

### Performance

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

## Continuous integration

`.github/workflows/ci.yml` builds with GCC, Clang, AppleClang and MSVC, runs
the GUI self-test on Linux with Qt, enforces the coverage gates, runs the
performance floors, and validates every changelog fragment. Boost is fetched by
`tools/fetch_boost.py`, so no job depends on a system Boost.

Set the `VIRTUAL_SERIAL_REPO` repository variable once Boost.VirtualSerial is
published; until then the workflows clone it from the default URL.

## Changelog

Every change lands as one fragment in [`changelog.d/`](changelog.d/README.md):

```bash
python tools/changelog.py --check
python tools/changelog.py --dry-run
python tools/changelog.py --release 0.2.0
```

## Repository layout

```
libs/vss_core        hub, endpoints, links, control lines, events
libs/vss_ports       serial transport, PTY, loopback, com0com, enumeration
libs/vss_config      JSON topology documents
libs/vss_studio      the application service shared by the CLI and the GUI
libs/vss_testkit     reusable fakes and helpers for tests
apps/vss_cli         headless command line
apps/vss_gui         Qt 6 desktop application
tests/               in-tree framework, unit and system suites
perf/                performance suite and floors
cmake/               dependency, warning, coverage and sanitizer helpers
tools/               Boost fetch, changelog, coverage and performance reports,
                     the PTY pair helper and the commit-message checker
docs/                architecture notes and scenario recipes
```

## Known limits

* A real Windows `COMx` still comes from com0com. Windows has no user-mode COM
  port API, so replacing com0com needs the kernel driver described in
  [docs/windows-com-driver.md](docs/windows-com-driver.md). The application
  already detects that driver, prefers it when present, and accepts
  `"virtual_kind": "windows-driver"` in saved topologies.
* A named-pipe virtual port is not implemented.
* Two loopback virtual ports cannot be linked duplex: a loopback device echoes
  its writes into its own receive path, which is a feedback loop. The GUI's
  null-modem demo therefore uses a one-way link, and real devices do not have
  the problem.
* Creating a com0com pair needs elevation; everything else runs unprivileged.
* Linking the two ends of a real null-modem cable through the hub is a physical
  loop, exactly as it would be with any serial forwarder. Attach one end and
  let the peer be the application.

## Roadmap

1. **Windows COM driver** — the one piece that cannot be user-mode. Phase 0
   (detection, selection order, configuration, ABI design) is done; the next
   milestone is a test-signed driver that creates a port and carries bytes,
   then the full serial contract (timeouts, purge, wait mask, line control),
   then signing and an installer.
2. **Named-pipe virtual ports** — a cross-process port for applications that
   accept a pipe path, useful on machines without a driver.
3. **macOS `/dev/cu.*` aliases** — an opt-in helper that creates a symlink with
   authorisation, for software that hardcodes the name.

## License

Virtual Serial Studio is free software: you can redistribute it and/or modify
it under the terms of the **GNU General Public License, version 3 or later**
(`GPL-3.0-or-later`). It is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. The full text is in [LICENSE](LICENSE).

Every first-party source file carries an `SPDX-License-Identifier:
GPL-3.0-or-later` tag.

The dependencies keep their own terms, all compatible with the GPL:

* [Boost.VirtualSerial](../boostasio-virtualserial) and Boost itself are
  **BSL-1.0**; BSL-1.0 is a permissive licence that can be combined with GPLv3.
* Qt 6 is **LGPL-3.0 / GPL-3.0 / commercial**; the GUI links it dynamically.
* `com0com` is an optional, separately installed Windows driver, invoked rather
  than linked.
* Three tools in `tools/` are derived from the Boost.VirtualSerial repository
  and remain **BSL-1.0**.

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) records the details.

If you distribute binaries, section 6 of the GPL requires you to make the
Corresponding Source available to the people you distribute to.
