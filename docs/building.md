# Building

Requirements: CMake 3.21+, a **C++23** compiler, Boost 1.74+ headers and git.
CI builds with the versions pinned in `.github/toolchain-versions.json`: GCC 16,
Clang 23, MSVC 19.51 and Qt 6.12.
CMake fetches [Boost.VirtualSerial](https://github.com/iainchesworthlabs/virtual-serial) at a release tag.
Qt 6 is optional and only needed for the GUI. CMake looks for it through
Qt's own config package first, then looks in `VSS_QT_ROOT`, `ICL_QT_ROOT`,
`QT_ROOT_DIR`, `QTDIR` and the usual install locations. The
`*-gui` presets make a missing Qt an error instead of silently building the
CLI alone.

```bash
# Boost headers, if you do not already have them
python tools/fetch_boost.py --dest .cache/boost-root

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVSS_BOOST_ROOT="$PWD/.cache/boost-root" \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/msvc2022_64   # optional; Qt is auto-detected

cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, run the configure and build from a Visual Studio developer prompt
(`vcvars64.bat`) so CMake can find `cl.exe`. To build against a local checkout
of the library, set `VSS_VIRTUAL_SERIAL_DIR` to it, for example in a
`CMakeUserPresets.json` (Git ignores that file). `VSS_FETCH_VIRTUAL_SERIAL`,
`VSS_VIRTUAL_SERIAL_GIT_REPO` and `VSS_VIRTUAL_SERIAL_GIT_TAG` control the
fetched release.

Options: `VSS_BUILD_TESTS`, `VSS_BUILD_GUI`, `VSS_GUI_REQUIRED`,
`VSS_BUILD_TOOLS`, `VSS_BUILD_PERF`, `VSS_TRACY`, `VSS_COVERAGE`,
`VSS_INSTALL`, `ICL_ENABLE_FUZZING`.

`CMakePresets.json` provides:

| Preset | What it builds |
|---|---|
| `linux-debug`, `linux-release`, `macos-debug`, `macos-release`, `windows-msvc-debug`, `windows-msvc-release` | Tests and tools, no GUI. The release presets also build the performance suite |
| the same names with `-gui` | The same plus the GUI; **fails** if Qt is missing |
| `linux-coverage`, `macos-coverage`, `windows-llvm-coverage` | Instrumented libraries (Clang, LLVM profiles), no GUI |
| `linux-asan-ubsan`, `linux-tsan`, `windows-msvc-asan` | AddressSanitizer and UBSan, ThreadSanitizer, or MSVC AddressSanitizer, at -O1 (`ICL_SANITIZERS`); no GUI. MSVC has no UBSan or TSan |
| the same three with `-gui` | The same plus the GUI, so the GUI test suites run under the sanitizer too; **fails** if Qt is missing. These are the presets the pull request gate and the nightly run use; see [Sanitizers](testing.md#sanitizers) |
| `linux-fuzz` | The libFuzzer targets with AddressSanitizer and UBSan at -O1 (Clang); see [Fuzzing](fuzzing.md) |
| `linux-tracy`, `windows-msvc-tracy` | Performance suite with Tracy, no GUI; needs `VCPKG_ROOT` |
| `linux-tracy-gui`, `windows-msvc-tracy-gui` | Release with Tracy compiled in but dormant: the GUI, CLI, tests and `vss_perf`; needs `VCPKG_ROOT` and Qt |

A preset only exists on the operating system it names. Each builds in `build/<preset>`. The compiler is the one in `CC` and `CXX`, or the platform default.

Set `VSS_QT_ROOT` (or `-DCMAKE_PREFIX_PATH=...`) only when the automatic search
picks the wrong kit; otherwise no environment variable is needed. `ICL_QT_ROOT`
is the per-machine setting shared with the other projects of the organisation:

```bash
# Linux example
export VSS_QT_ROOT=$HOME/Qt/6.12.0/gcc_64
cmake --preset linux-release-gui
cmake --build --preset linux-release-gui
ctest --preset linux-release-gui

# Windows (from a vcvars64 prompt)
set VSS_QT_ROOT=C:\Qt\6.12.0\msvc2022_64
cmake --preset windows-msvc-release-gui
cmake --build --preset windows-msvc-release-gui
```

`VSS_QT_ROOT` is the directory that contains `lib/cmake/Qt6`. If it is unset and
Qt is not on `CMAKE_PREFIX_PATH`, the `*-gui` presets stop with an explanatory
error; `-DVSS_GUI_REQUIRED=OFF` restores the old "build the CLI only"
behaviour.

`VSS_TRACY` is on by default in Debug builds configured through the vcpkg
toolchain and off in Release. Pass `-DVSS_TRACY=ON` (or use a `*-tracy*`
preset) to a Release build when a release binary needs to be profiled. The
instrumentation is compiled in but dormant: no profiler thread and no socket
exist until the program is told to start profiling (`--profile`,
`VSS_PROFILE=1`, or Settings, Performance in the GUI). Debug builds start
profiling by themselves, on this computer only. See [profiling](profiling.md).
Tracy comes from vcpkg through a small overlay port in `cmake/vcpkg-ports`,
which turns on the manual lifetime and no-broadcast builds of the client.

## Language standard

The application targets **C++23** (`std::span`, `std::string_view`, defaulted
comparisons, designated initialisers, concepts, `std::to_underlying`) and builds
clean with MSVC `/std:c++latest` and clang-cl `-std=c++23`, with warnings as
errors.

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
