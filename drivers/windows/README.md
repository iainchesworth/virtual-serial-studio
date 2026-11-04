# Windows virtual COM port driver

`vsscom` is a KMDF function driver that gives Windows a virtual `COMn` port with
no com0com and no hardware. The design and the reasons for it are in
[docs/windows-com-driver.md](../../docs/windows-com-driver.md).

## State

| Piece | State |
|---|---|
| Package: `vsscom.sys`, `vsscom.inf`, catalog, for x64 and ARM64 | builds in CI; `InfVerif` and `Inf2Cat` pass |
| COM name: device interface, `\DosDevices` link, `SERIALCOMM` | in the driver; not yet run in a guest |
| Loopback port: waiting reads with the `SetCommTimeouts` rules, `WaitCommEvent`, purge and cancel, modem lines, exclusive open | in the driver; not yet run in a guest |
| Paired ports with a hub end, ports on demand | not yet |
| Signing | not done: the package is unsigned |

## Build

You need Visual Studio 2026 with the C++ desktop workload (the driver build
targets come with it) and access to NuGet. The Windows SDK and the WDK are NuGet
packages named in `vsscom/vsscom.vcxproj`, so nothing else is installed.

```powershell
./drivers/windows/build.ps1 -Platform x64      # or ARM64
```

The package is written to `drivers/windows/artifacts/package/<platform>/`:
`vsscom.sys`, `vsscom.inf` and `vsscom.cat`. CI runs the same script and uploads
the package as the artifact `vsscom-<platform>`.

## Layout

| Path | What |
|---|---|
| `vsscom/driver.c` | driver entry, device creation, COM name registration |
| `vsscom/io.c` | reads, writes and the serial control requests |
| `vsscom/vsscom.h` | the device context shared by the two |
| `vsscom/public.h` | what user mode sees: interface GUID, control codes |
| `vsscom/vsscom.inf` | the package description |
| `vsscom/core/` | the serial rules in plain C; built for the host and unit-tested |
| `build.ps1` | restore, build, `InfVerif`, `Inf2Cat` |
| `test/` | guest scripts: `install-driver.ps1`, `test-driver.ps1` (tests D1 to D7), `uninstall-driver.ps1` |
