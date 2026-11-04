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
| T8 | The stable link of `vport` appears, is replaced when stale and is removed on exit. Not yet implemented. |
| T9 | The package artifact installs and runs as a user would run it. Not yet built. |
| T10 | `vss_cli bench` moves 4 MB over a pseudo terminal pair. The throughput is recorded; the floor is set after three runs agree. |
| T11 | An experiment with a kernel serial gadget in the Ubuntu guest. |

Windows cells skip T2, T3, T7 and T10, which need pseudo terminals. The Windows guests run them against the driver once it
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

| Cell | Date | Commit | Result | Evidence |
|---|---|---|---|---|
| `wsl-debian` (WSL2 Debian, glibc 2.41, GCC 13, Qt 6.12.0, kernel 6.6.87) | 2026-11-05 | `9d5df03` plus the matrix change | T2 to T7 and T10 pass; T1 not passed to the runner; T8, T9, T11 skipped. T10 measured 20.4 MB/s. | `run_cell.py` output of that day |

The hosted cells have not run yet. This table gains their rows after the first nightly run, each with a link to the run.

## macOS

The macOS build is compiled and tested only on GitHub-hosted macOS runners (macOS 15 and 26, Apple silicon and Intel). It has
not been run on a Mac that anyone looks at. The graphical interface has not been seen on a real display, the application bundle
is not signed or notarised and Gatekeeper behaviour has not been checked, and no third-party serial program has been tried
against a macOS virtual port. Virtual ports on macOS are pseudo terminals (`/dev/ttysNNN`), which programs that list ports through
IOKit do not show.

## Windows

Virtual ports on Windows need the `vsscom` driver. Until it is installed, `auto` falls back to com0com when present, and to an
in-process loopback device otherwise. See [`windows-com-driver.md`](windows-com-driver.md).
