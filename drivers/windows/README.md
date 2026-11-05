# Windows virtual COM port driver

`vsscom` is a KMDF function driver that gives Windows a virtual `COMn` port with
no com0com and no hardware. The design and the reasons for it are in
[docs/windows-com-driver.md](../../docs/windows-com-driver.md).

## State

| Piece | State |
|---|---|
| Package: `vsscom.sys`, `vsscom.inf` and `vsscomctl.inf` with a catalog each, for x64 and ARM64 | builds in CI; `InfVerif` and `Inf2Cat` pass |
| COM name: device interface, `\DosDevices` link, `SERIALCOMM` | runs in a Windows 10 and a Windows 11 guest |
| Loopback port: waiting reads with the `SetCommTimeouts` rules, `WaitCommEvent`, purge and cancel, modem lines, one handle per end | runs in both guests (tests D1 to D7) |
| Hub end: a second handle on each port that carries its bytes, line settings and lines, with flow control both ways | runs in both guests (tests D8 to D13) |
| Control device: ports on demand, removed with the handle that made them | tests D14 to D19 |
| The studio opening a port (`create_driver_port`) | in the studio; not yet run against a driver in a guest |
| Signing | not done: the package is unsigned |

## Build

You need Visual Studio 2026 with the C++ desktop workload (the driver build
targets come with it) and access to NuGet. The Windows SDK and the WDK are NuGet
packages named in `vsscom/vsscom.vcxproj`, so nothing else is installed.

```powershell
./drivers/windows/build.ps1 -Platform x64      # or ARM64
```

The package is written to `drivers/windows/artifacts/package/<platform>/`:
`vsscom.sys`, `vsscom.inf`, `vsscomctl.inf` and the catalogs `vsscom.cat` and
`vsscomctl.cat`. CI runs the same script and uploads the package as the artifact
`vsscom-<platform>`.

## Layout

| Path | What |
|---|---|
| `vsscom/driver.c` | driver entry, port creation, COM name registration; tells a port from the control device |
| `vsscom/io.c` | the application end: reads, writes and the serial control requests, and the pump that moves bytes between the ends |
| `vsscom/hub.c` | the hub end: opening, reading, writing, the state the hub watches |
| `vsscom/control.c` | the control device: the bus that makes ports on demand |
| `vsscom/vsscom.h` | the device context shared by the four |
| `vsscom/public.h` | what user mode sees: interface GUIDs, control codes, the hub state, the control requests |
| `vsscom/vsscom.inf` | the ports (class Ports) |
| `vsscom/vsscomctl.inf` | the control device (class System) |
| `vsscom/core/` | the serial rules in plain C; built for the host and unit-tested |
| `build.ps1` | restore, build, `InfVerif`, `Inf2Cat` |
| `test/` | guest scripts: `install-driver.ps1` (the control device and two ports made by hand, `-Count` for more), `test-driver.ps1` (tests D1 to D19), `uninstall-driver.ps1` |

## The two ends of a port

Each port is one device with two ends. The application opens `\\.\COMn` as it
would any serial port. The studio opens the hub end through the device interface
`GUID_DEVINTERFACE_VSSCOM` with the reference string `hub`, so the path ends in
`\hub`, and it opens it for overlapped I/O.

| | Application end | Hub end |
|---|---|---|
| Handles | one at a time | one at a time |
| Write | goes to the hub; waits when the 64 KB queue is full | goes to the application; waits when its 64 KB queue is full |
| Read | what the hub wrote, under `SetCommTimeouts` | what the application wrote, as soon as there is any |
| Lines | DTR, RTS out; CTS, DSR, DCD, RI in | sees DTR, RTS, break, baud rate and line control; sets CTS, DSR, DCD, RI |

`IOCTL_VSSCOM_HUB_WAIT_STATE` waits until the application changes something, so
the hub never polls. While no hub handle is open the port loops back, so a port
that nothing has attached still behaves like a port with a loopback plug.

The hub end has the same access rights as the COM port, so any process that can
open the port can take the hub end while nobody has it. Narrowing that is part of
the hardening phase.

## Ports on demand

Install the package once and the control device appears in Device Manager under
System devices as "Virtual Serial Studio Port Control". It makes no port by
itself. A program opens its interface `GUID_DEVINTERFACE_VSSCOMCTL`, sends
`IOCTL_VSSCOM_CTL_CREATE_PORT`, and Windows builds a device
`VSSCOM\PORT\<slot>` that `vsscom.inf` drives. A second or two later the port has
its `COMn` name and its two interfaces, and the program opens the hub end as
above. The control requests and their structures are in `public.h`.

| Rule | Why |
|---|---|
| A port lives as long as the handle that made it | A studio that crashes leaves no port behind. Closing the handle, or the process ending, removes the port. |
| `IOCTL_VSSCOM_CTL_DESTROY_PORT` works on the caller's own ports only | One program cannot remove another's port. |
| A slot is the lowest free number, from 0 to 31 | A port that comes back gets the same device node, so the same COM name. |
| At most 32 ports exist at once | A limit for the whole machine. A slot is free again only when Windows has removed the device. |
| The control device is open to SYSTEM, administrators and signed-in users | A normal user can make a port without elevation. Any signed-in user can use up the 32 slots; the hardening phase decides whether to narrow this. |

The slot, and not a name, identifies the port while Windows builds it, because
the `COMn` name comes from the Ports class installer at the end of the build.
Programs find the port by its device instance id.

## Try it in a guest

Only in a disposable test guest that reverts to a clean snapshot. Copy the
package (`vsscom.sys`, `vsscom.inf`, `vsscomctl.inf` and the two catalogs) and
the scripts from `test/` into one folder in the guest, then, in an elevated
PowerShell:

```powershell
./prepare-test-signing.ps1 -ConfirmDisposableGuest -EnableTestSigning   # makes a throwaway certificate, signs, turns on test signing
# restart the guest
./install-driver.ps1 -ResultPath C:\vss\install.json
./test-driver.ps1 -ResultPath C:\vss\test.json
./uninstall-driver.ps1 -ResultPath C:\vss\uninstall.json
```

`prepare-test-signing.ps1` creates the certificate in the guest with a key that
cannot be exported and trusts it only there; the revert discards it. Test
signing needs Secure Boot off. A release package is signed with an organisation
certificate instead, which is a separate step.
