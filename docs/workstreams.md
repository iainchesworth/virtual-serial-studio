# Virtual port workstreams

Creating a serial port should never require a physical one. This document is
the map for the branches that close the remaining gaps, and the worktree and
sub-session protocol used to work on them in parallel.

## Where the platforms stand

| Platform | Today | Gap |
|---|---|---|
| Linux | A pseudo terminal per virtual port (`/dev/pts/N`), advertised by the studio and used by CI on every run | Paths are ephemeral and owned by the process; no stable name and no standalone port command |
| macOS | Same PTY mechanism (`/dev/ttysN`), exercised by CI | Same as Linux, plus DriverKit is the long-term route for a non-PTY device |
| Windows | com0com pairs when installed, in-process loopback otherwise, and a seam (`vcom_driver.hpp`) for a first-party driver | No first-party driver: `create_driver_port()` returns `unsupported` and real COM ports need com0com |

So Linux and macOS already create real devices; what they lack is a stable,
scriptable lifetime. Windows genuinely needs the driver.

## Branches and worktrees

| Branch | Worktree | Scope |
|---|---|---|
| `main` | `C:\Repos\boostasio-virtualserial-demoapp` | Shared CI, VM harness, docs |
| `feature/windows-com-driver` | `C:\Repos\vss-worktrees\windows-com-driver` | KMDF virtual COM driver, INF, test signing, VMware validation |
| `feature/posix-virtual-ports` | `C:\Repos\vss-worktrees\posix-virtual-ports` | Stable named PTYs and a headless port command for Linux and macOS |

Linux and macOS share one branch because they share the implementation: a
`posix_openpt` port plus a symlink. Their CI coverage differs (Linux runs the
pty pair through `tools/pty_loopback.py`; macOS runs the PTY API directly), and
the branch's tests must pass on both before it merges.

## Working in a sub-session

Each worktree is a full checkout on its own branch. Start a session with the
worktree as the working directory; nothing else in the repository needs to
change.

```powershell
cd C:\Repos\vss-worktrees\windows-com-driver
cmake -S . -B build-tracy `
  -DVSS_VIRTUAL_SERIAL_DIR=C:/Repos/boostasio-virtualserial `
  -DVSS_BOOST_ROOT=C:/Repos/boostasio-virtualserial/.cache/boost-root `
  -DVSS_BUILD_GUI=OFF -DVSS_BUILD_TESTS=ON
```

Rules that apply to every sub-session:

- The maintainer authors and commits every change; no trailers, attribution or
  tool markers. `.githooks/commit-msg` enforces it.
- Rebase on `main` before pushing; the shared harness and CI live there.
- Keep the branch's CI green before asking for a review. Feature branches do
  not run CI on push, so dispatch it explicitly:

  ```bash
  gh workflow run ci.yml --ref feature/windows-com-driver
  ```

- Only `main` merges; nothing lands on a branch that main cannot consume.

## Definition of done

### Windows driver

- The KMDF project builds in CI on a Windows runner with the WDK, produces
  `vsscom.sys` and a stamped `vsscom.cat`, and passes `InfVerif`.
- `pnputil /add-driver vsscom.inf /install` creates two COM ports from the INF,
  named `COM<n>` by the Ports class.
- A loopback mode returns written bytes on read; control-line IOCTLs and the
  common `IOCTL_SERIAL_*` set are answered.
- The VMware test script installs the driver in a clean snapshot, runs a
  PowerShell serial test (open, set baud, write, read, DTR/RTS, purge), and
  reports a machine-readable result.
- `vss::driver_available()`, `driver_version()` and `create_driver_port()`
  return the driver's real values, and auto selection prefers it over com0com.

### POSIX virtual ports

- A stable name (`/run/vss/ttyVSS0` on Linux, `/tmp/vss/ttyVSS0` on macOS, both
  overridable) points at the live PTY for the lifetime of the port.
- A headless command owns one or more ports until interrupted, prints their
  paths, and removes the links on exit, including on SIGINT/SIGTERM.
- Tests cover link creation, replacement of a stale link, cleanup on shutdown
  and the permission rules.
- Documentation shows how an application opens the stable path.

## VM testing

See [vm-testing.md](vm-testing.md) for the VMware harness and the driver test
loop. The harness is shared; the driver branch owns the guest test script.
