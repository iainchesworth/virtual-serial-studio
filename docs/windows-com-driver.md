# Replacing com0com: virtual COM ports on Windows

The goal is for Virtual Serial Studio to be the only thing a user installs. On
Linux and macOS that is already true — the application creates real pseudo
terminals. On Windows it is not, and cannot be, until a kernel driver exists.

This note explains why, what the driver must do, and how the application is
already shaped to accept it.

## The hard constraint

A Windows `COMx` is a **kernel device**, not a name. When an application does

```c
HANDLE port = CreateFileW(L"\\\\.\\COM5", ...);
SetupComm(port, 4096, 4096);
GetCommState(port, &dcb);
ReadFile(port, buffer, count, &read, &overlapped);
```

it is talking to a device object enumerated under `GUID_DEVCLASS_PORTS`, whose
driver implements the serial IRP/IOCTL contract. `CreateFile`, `GetCommState`,
`SetupComm`, `SetCommTimeouts`, `WaitCommEvent`, `EscapeCommFunction` and the
overlapped read/write paths all map onto `IOCTL_SERIAL_*` requests.

There is no user-mode API that registers a COM port. Registry entries and
symbolic links on their own produce a name that `CreateFile` resolves to
nothing. So:

* **A driver is required.** Nothing in user space can substitute for it.
* **It must implement the serial contract**, not just pass bytes through,
  because applications configure timeouts, purge queues, wait on line events
  and query modem status before they exchange a single byte.
* **It must be signed** for any machine that is not in test-signing mode.

## Platform matrix

| Platform | Mechanism | Driver needed? |
|---|---|---|
| Linux | Pseudo terminal (`/dev/pts/N`); a full tty with termios | No |
| macOS | Pseudo terminal (`/dev/ttysNNN`); a full tty | No |
| Windows | `COMx` device object + serial IOCTLs | **Yes** |

### Linux

A PTY is a tty. `tcsetattr`, `poll`, `select`, `read`, `write` and the termios
flags work as they do on a UART. The modem lines do not: the kernel's PTY has
no `TIOCMGET` or `TIOCMSET`, so a program that opens the slave cannot read or
set DTR, RTS, CTS or DSR. The studio owns the master and holds the slave open,
so the port exists before an application connects.

Two gaps remain. The name is `/dev/pts/N` and changes on every run; see
`posix-virtual-ports.md` for the stable names that `vss_cli vport` makes. And
programs that offer a list of detected ports (pyserial's `list_ports`, Qt's
`QSerialPortInfo`) do not list a PTY; a program that takes a typed or
configured path works.

### macOS

Same answer, with two caveats:

* Applications that insist on `/dev/cu.*` or list ports through IOKit will not
  find a PTY. A stable link from `vss_cli vport` helps programs that take a
  path. Whether a root-owned alias in `/dev` works is not known yet.
* A kext is not a viable distribution path. Apple requires reduced security
  mode and notarisation, and Apple Silicon makes third-party kexts a support
  problem. SerialDriverKit (`IOUserSerial`) is a possible later route to a
  port that IOKit lists; it needs an Apple entitlement and a Mac to try, and it
  is not planned. PTY is the supported answer.

## Options considered for Windows

| Option | Verdict |
|---|---|
| Bundle com0com and drive `setupc.exe` | Works today, and com0com is GPL like this project, but it keeps a third-party driver, an elevation prompt and a dependency we set out to remove. Useful as the transitional fallback. |
| Named pipe presented as a "port" | Not a COM port. Only applications that accept a pipe path can use it. |
| USB/IP with a virtual CDC device | Requires `usbip-win` (another third-party driver) plus a server; fragile under suspend and reconnects. |
| `SerCx2` (serial controller extension) | Designed for real UART controllers and DMA; far more ceremony than a software port needs. |
| UMDF serial device | The serial class and its installer expect kernel-mode IRP handling; UMDF cannot implement the contract cleanly. |
| **KMDF virtual serial function driver** | **Recommended.** A control device creates ports on demand; each port is a PDO implementing the serial contract with a shared-memory data path to the hub. |

## Recommended architecture

```
  ┌──────────────────────────── user mode ────────────────────────────┐
  │  vss_gui / vss_cli                                                │
  │        │                                                          │
  │   vss::studio ── vss_ports::driver_transport                      │
  │        │              │  shared section + events                  │
  └────────┼──────────────┼───────────────────────────────────────────┘
           │              │
  ┌────────┼──────────────┼──────────────── kernel mode ──────────────┐
  │        │        vserial.sys                                       │
  │        │              │                                           │
  │        │         ┌────┴─────┐      ┌──────────────┐               │
  │        │         │ PDO COM5 │ ...  │ PDO COM6     │               │
  │        │         └────┬─────┘      └──────┬───────┘               │
  └────────┼──────────────┼───────────────────┼───────────────────────┘
           │              │                   │
           └──────────────┴───────────────────┴── applications
```

### Control device

`vserial.sys` exposes `\Device\VSerialCtl` with the symlink `\\.\VSerialCtl`.
The hub opens it once and keeps it open for the process lifetime.

| IOCTL | Purpose |
|---|---|
| `IOCTL_VSERIAL_GET_VERSION` | ABI major/minor; the client refuses a major mismatch |
| `IOCTL_VSERIAL_CREATE_PORT` | Create a port; returns instance id, `COMx`, section and event names |
| `IOCTL_VSERIAL_DESTROY_PORT` | Remove a port (also on last handle close, configurable) |
| `IOCTL_VSERIAL_LIST_PORTS` | Diagnostics and the GUI's "driver ports" view |

`CREATE_PORT` takes a request struct: desired COM number (`0` = auto), ring
sizes, queue depth, and flags. The driver creates the PDO, chooses a free COM
number when asked, writes `PortName` under the device's hardware key, creates
`\DosDevices\COMx`, and returns names for the shared section and events. Names
are derived from the instance id so two processes cannot collide.

### Data path

Per port, one named section holding two single-producer/single-consumer rings:

| Ring | Direction | Producer | Consumer |
|---|---|---|---|
| `to_device` | application → hub | driver `IRP_MJ_WRITE` | user-mode transport |
| `from_device` | hub → application | user-mode transport | driver `IRP_MJ_READ` |

Each ring is a fixed-size byte array plus aligned head/tail indices, a
`space_available` event and a `data_available` event. The driver maps the
section in kernel virtual address space; the client maps it with
`MapViewOfFile`. A read IRP consumes what is available and pends when the ring
is empty; a write IRP copies what fits and pends the remainder until space is
signalled.

This keeps the interrupt-free data path to a memory copy on each side and
avoids per-byte IOCTLs.

### Serial semantics the driver must implement

| Area | What is required |
|---|---|
| Queues | `SET_QUEUE_SIZE`, `PURGE` (abort read/write and reset rings), `GET_COMMSTATUS` sizes |
| Configuration | `GET/SET_COMMCONFIG` backed by the hub's `serial_settings`; reject unsupported values with `STATUS_INVALID_PARAMETER` |
| Timeouts | `SET/GET_TIMEOUTS` with `ReadIntervalTimeout`, `ReadTotalTimeoutMultiplier` and `ReadTotalTimeoutConstant`; the driver must own the timers, or applications block forever |
| Modem lines | `SET/GET_DTR`, `SET/GET_RTS`, `GET_MODEMSTATUS`, `SET_HANDSHAKE` forwarded to the hub over the shared flags page |
| Break | `SET_BREAK`, `SET_XOFF/XON`, `IMMEDIATE_CHAR` |
| Events | `WAIT_ON_MASK` for `EV_RXCHAR`, `EV_TXEMPTY`, `EV_CTS/DSR/RLSD/RING`, `EV_BREAK`, `EV_ERR` |
| Errors | framing, parity, overrun and break injected by the hub surface as `COMMSTATUS` error bits and `EV_ERR` on the next read |
| Lifecycle | cancel pended IRPs on purge, close, surprise removal and destroy; power transitions D0/D3 |

The timeout rules are the subtle part and the usual reason a home-grown
virtual port fails: a `ReadFile` with `ReadIntervalTimeout = MAXDWORD`,
`ReadTotalTimeoutMultiplier = 0` and `ReadTotalTimeoutConstant = 0` means "return
immediately with whatever is buffered", while other combinations mean "wait for
the first byte, then for a gap". The driver has to implement all of them.

### ABI and versioning

* All IOCTLs use `CTL_CODE(FILE_DEVICE_UNKNOWN, ...)` with a struct that starts
  with `{ uint32_t size; uint16_t major; uint16_t minor; }`.
* The client and driver share one header, `vcom_driver_abi.h`, which is built
  into both the user-mode library and the driver.
* A major mismatch is a hard error with a readable message; a minor mismatch
  means "ignore unknown fields".

### Signing and distribution

| Stage | Requirement |
|---|---|
| Development | `bcdedit /set testsigning on`, self-signed certificate, `inf2cat`, `signtool`, `pnputil /add-driver`, reboot |
| Release | EV code-signing certificate and a Microsoft Hardware Dev Center account; attestation signing is enough (WHQL/HLK is not required for attestation) |
| Architectures | x64 and ARM64 builds, both signed |
| Installer | `pnputil` or a WiX/MSI custom action; uninstall must remove ports and the control device cleanly |
| GPL | The driver is GPL-3.0-or-later like the rest of the project; shipping the binary means offering its source, and this repository is that offer |

The certificate is the long-lead item: procurement and identity validation can
take weeks, and Windows 10+ will not load an unsigned kernel driver.

## Effort

| Phase | Scope | Estimate |
|---|---|---|
| 0 | Application interface, detection, configuration, docs (done) | — |
| 1 | ABI header, shared rings, stub driver creating a port that echoes bytes | 2–3 weeks |
| 2 | Full serial contract: timeouts, purge, wait mask, line control, cancellation, power | 3–5 weeks |
| 3 | INF, installer, upgrade/uninstall, version handshake | 1–2 weeks |
| 4 | Signing, VM test matrix, Driver Verifier runs | 2 weeks + certificate lead time |

Driver development also needs a machine where test signing is enabled and a
reboot is acceptable. GitHub-hosted runners cannot reboot mid-job, so driver
smoke tests want a self-hosted Windows VM.

## What the first driver builds

The first driver is simpler than the architecture above and keeps its shape. It
has no control device and no shared memory. A port is a device that already
exists, created with `drivers/windows/test/install-driver.ps1` for now, and each
port has two ends on one device object: the COM port, and a second handle that
the studio opens through a device interface with the reference string `hub`.
Bytes cross through two 64 KB queues inside the driver, with flow control, so
`ReadFile` and `WriteFile` on the hub handle replace the shared rings. See
`drivers/windows/README.md` for the table of the two ends. Creating ports on
demand and the shared-memory data path remain the later phases.

## What is already in place

* `vss::virtual_port_kind::windows_driver` and
  `vss::supports(...)`, so the GUI, the CLI and stored topologies already
  understand the kind.
* `vss::driver_available()` / `driver_version()` — a registry probe of
  `HKLM\SOFTWARE\Virtual Serial Studio\Driver`, which the INF will write.
* `vss::create_driver_port()` — the single place the client will be
  implemented; today it reports `errc::unsupported` rather than failing late.
* Auto-selection order is already: **driver → com0com → loopback** on Windows,
  and **pty** elsewhere. When the driver installs, it wins without a code
  change elsewhere.
* `vss_config` accepts `"virtual_kind": "windows-driver"`, so a saved topology
  made on a driver machine reloads on one, and fails cleanly on a machine
  without it.

## Decision points

1. **Transitional fallback** — keep com0com support behind the driver, or drop
   it once the driver ships? Keeping it costs little and helps users migrate.
2. **COM numbering** — auto-assign and let the user rename, or honour a
   requested number? Auto-assign with an optional preference is least
   surprising.
3. **Driver signer** — who holds the EV certificate, and does the driver ship
   separately from the application?
4. **Open source** — the driver in this repository under GPL-3.0-or-later, or a
   separate repository? Keeping it here makes the source offer trivial.
