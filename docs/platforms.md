# Platforms and the test matrix

Virtual Serial Studio is built and tested on three operating systems. This page lists the cells that are tested, what each test
proves, how to run a cell, and the last result of each. A test that cannot run reports `not_run`, never `pass`.

## Cells

| Cell | Kind | Runs |
|---|---|---|
| `ubuntu-24.04`, `ubuntu-26.04` | hosted runner | every night |
| `windows-2025` | hosted runner | every night |
| `macos-15`, `macos-26` (Apple silicon), `macos-15-intel`, `macos-26-intel` | hosted runner | every night |
| `VSS - Windows 10`, `VSS - Windows 11` | VMware guest on the owner's machine | on request, with the driver |
| `VSS - Ubuntu 26.04` | VMware guest on the owner's machine | on request |
| `wsl-debian` | WSL2 Debian on the owner's machine | during development |

The hosted cells are the workflow `.github/workflows/platform-matrix.yml`. The guests are described in
[`vm-testing.md`](vm-testing.md). A hosted macOS or Windows cell proves the build and the tests on a real installation of
that system. It does not prove that anyone has seen the program on a real display.

## Tests

| Id | What it proves |
|---|---|
| T1 | The `ctest` suites (unit, system, GUI) pass. On Linux and macOS the system tests use a pseudo terminal pair from `tools/pty_loopback.py`. |
| T2 | A separate process opens two virtual ports and exchanges 4 KB each way, in order, through the hub. |
| T3 | A split topology (one device, two listeners) and a join topology (two publishers, one device), loaded from JSON, route every byte, each publisher's bytes in order. |
| T4 | `vss_cli selftest` passes. |
| T5 | `vss_gui --selftest` passes on the offscreen platform. |
| T6 | The main window renders in the light and the dark theme (`vss_gui --screenshot`). The images are artifacts for a person to look at; they are never committed. |
| T7 | What the system and common tools show for a virtual port: the device node and its mode, `stty`, `/proc/tty/drivers` or `ioreg`, and pyserial's port list. These are recorded observations. |
| T8 | `vss_cli vport` makes two stable links that carry 1 KB each way, a second `vport` is refused the names a running one holds, links left by a killed process are replaced, and SIGTERM removes them and exits 0. |
| T9 | A release archive (`--package`) is unpacked with no build environment, and `vss_cli selftest` and `vss_gui --version` run from it. On Linux every library the GUI is linked to must be found, and the GUI starts under Xvfb when the machine has no display. A cell given no package skips T9. |
| T10 | `vss_cli bench` moves 4 MB over a pseudo terminal pair. The throughput is recorded; the floor is set after three runs agree. |
| T11 | An experiment with a kernel serial gadget in the Ubuntu guest. |

Windows cells skip T2, T3, T7, T8 and T10, which need pseudo terminals. The Windows guests run them against the driver once it
exists.

## Running a cell

```bash
cmake --preset linux-release-gui && cmake --build --preset linux-release-gui
ctest --preset linux-release-gui
python3 tools/matrix/run_cell.py --cell my-machine --build-dir build/linux-release-gui --out out
python3 tools/matrix/merge_results.py out --check
```

`run_cell.py` writes `result-<cell>.json` and one log per test into `out`. The JSON has one record per test with `pass`, `fail`,
`skip` or `not_run`, and an `observations` object with what T7 and T10 measured. `merge_results.py` prints the table below, and with
`--check` exits with 1 when any applicable test failed or did not run.

## Last results

The first complete hosted run is [run 41919800221](https://github.com/iainchesworthlabs/virtual-serial-studio/actions/runs/41919800221), on commit `c3d4472` of the matrix pull request, on 2026-11-05. Every cell passed every applicable test.

| Cell | System | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T10 |
|---|---|---|---|---|---|---|---|---|---|
| `ubuntu-24.04` | Linux 7.0 (Azure), glibc | pass | pass | pass | pass | pass | pass | pass | 55.6 MB/s |
| `ubuntu-26.04` | Linux 7.0 (Azure), glibc | pass | pass | pass | pass | pass | pass | pass | 111.1 MB/s |
| `windows-2025` | Windows Server 2025 (10.0.26100) | pass | skip | skip | pass | pass | pass | skip | skip |
| `macos-15` | macOS 15.8, Apple silicon | pass | pass | pass | pass | pass | pass | pass | 5.1 MB/s |
| `macos-26` | macOS 26.6, Apple silicon | pass | pass | pass | pass | pass | pass | pass | 7.0 MB/s |
| `macos-15-intel` | macOS 15.7, Intel | pass | pass | pass | pass | pass | pass | pass | 2.1 MB/s |
| `macos-26-intel` | macOS 26.6, Intel | pass | pass | pass | pass | pass | pass | pass | 1.6 MB/s |
| `wsl-debian` (owner's machine) | WSL2 Debian, GCC 13, Qt 6.12.0 | not given | pass | pass | pass | pass | pass | pass | 20.4 MB/s |
| `VSS - Ubuntu 26.04` (guest) | Ubuntu 26.04, Linux 7.0.0-42-generic | pass | pass | pass | pass | pass | pass | pass | 25.3 MB/s |
| `VSS - Windows 11` (guest) | Windows 11 Pro 10.0.26200 | pass | skip | skip | pass | pass | pass | skip | skip |
| `VSS - Windows 10` (guest) | Windows 10 Pro 10.0.19045 | pass | skip | skip | pass | pass | pass | skip | skip |

T8, T9 and T11 are skipped everywhere, and T7 observations are in each cell's `result-<cell>.json` artifact. What the first run showed:

- The GUI had never been built on macOS or Windows in CI. It failed on eight lambdas that capture `this` without using it (AppleClang) and on a missing `aqtsource` pin for Qt 6.12 on Windows. Both are fixed.
- macOS refuses 921600 baud on a pseudo terminal, so T10 asks for 115200.
- On macOS, `ls /dev/cu.* /dev/tty.*` finds nothing and `ioreg -c IOSerialBSDClient` is empty while a studio pseudo terminal exists, so programs that list ports through IOKit do not see it.
- Pseudo terminal throughput on the Intel macOS runners is two to four times lower than on Apple silicon, which is why T10 records the rate and holds it to no floor yet.

## The guest cells

The three guest rows ran on 2026-11-05 on commit `e0ece52`, each from the `clean` snapshot, with the tests of the commit run inside the guest. The programs were built outside the guests (the Linux one in WSL Debian with GCC 13, the Windows one on the owner's machine with MSVC and Qt 6.12.0) and copied in with their Qt libraries, so a guest cell proves that the build runs and passes on that system, not that the guest can build it. All 25 `ctest` suites passed on both Windows guests, and all 21 on Ubuntu (which has no `vm_*` tests).

- **T11 cannot run on the stock Ubuntu 26.04 kernel.** `g_serial`, `libcomposite` and `usb_f_acm` are there, but `dummy_hcd`, which a gadget needs to attach to, is not in the `-generic` kernel and no `linux-modules-extra` package exists for it. The test stays skipped until the guest runs a kernel that has it.
- **The driver tests passed on both Windows guests.** The `virtual-ports` session ran `drivers/windows/test` on the same guests on 2026-11-05: install, D0 to D13 and uninstall passed on Windows 10 and Windows 11. Those results are theirs; this page does not repeat them as a matrix cell.
- A guest needs more than the program. The Ubuntu guest needed `cmake` for `ctest` and the OpenGL, EGL and fontconfig libraries for Qt. The Windows guests needed the `offscreen` platform plugin (`windeployqt` ships only `windows`), PowerShell 7 for the `vm_*` suites, and the build drive path, because some GUI tests read source files through the absolute path they were built with.
- T8 and T9 wait for `vport` and the package.

## macOS

The macOS build is compiled and tested only on GitHub-hosted macOS runners (macOS 15 and 26, Apple silicon and Intel). It has
not been run on a Mac that anyone looks at. The graphical interface has not been seen on a real display, the application bundle
is not signed or notarised and Gatekeeper behaviour has not been checked, and no third-party serial program has been tried
against a macOS virtual port. Virtual ports on macOS are pseudo terminals (`/dev/ttysNNN`), which programs that list ports through
IOKit do not show.

## Windows

Virtual ports on Windows need the `vsscom` driver. Until it is installed, `auto` falls back to com0com when present, and to an
in-process loopback device otherwise. See [`windows-com-driver.md`](windows-com-driver.md).
