# Testing the Windows driver in VMware

The driver cannot be loaded on the development machine: it needs test signing,
a reboot and a kernel that is disposable. A VMware snapshot is the unit of
disposal.

## Inventory

`vmrun` is part of VMware Workstation and lives in
`C:\Program Files\VMware\VMware Workstation\vmrun.exe`; the harness finds it,
or uses `VSS_VMRUN`. The machine already has these VMs under
`D:\Virtual Machines`:

| VM | Suggested use |
|---|---|
| `Developer VM` | Driver test guest (take a snapshot before changing it) |
| `Atmos Driver Test` | Spare driver guest |
| `Boost Build Machine` | Leave alone; it is a build cache |

Use one VM for driver testing and keep it disposable.

## Prepare the guest once

1. Install/update VMware Tools so `vmrun` guest operations work.
2. Enable test signing and reboot:

   ```cmd
   bcdedit /set testsigning on
   bcdedit /set nointegritychecks on
   shutdown /r /t 0
   ```

3. Confirm the boot screen shows **Test Mode**.
4. Create `C:\vss` and copy the test script there when running a test.
5. Shut the guest down cleanly and take the snapshot:

   ```powershell
   ./tools/vm/vmrun.ps1 snapshot -Vm 'D:\Virtual Machines\Developer VM\Developer VM.vmx' -Name clean
   ```

Credentials are never committed. Export them for the session:

```powershell
$env:VSS_VM_USER = 'tester'
$env:VSS_VM_PASSWORD = '<password>'
```

## The loop

```powershell
$vm = 'D:\Virtual Machines\Developer VM\Developer VM.vmx'

# Always start from the prepared state.
./tools/vm/vmrun.ps1 revert -Vm $vm
./tools/vm/vmrun.ps1 start  -Vm $vm

# Driver package from the CI artifact or a local build.
./tools/vm/vmrun.ps1 copy-to -Vm $vm -Local build/vsscom -Guest 'C:\vss'

# Install (test signing makes the unsigned/test-signed package loadable).
./tools/vm/vmrun.ps1 run -Vm $vm -CommandLine `
  'cmd.exe /c C:\vss\install-driver.cmd'

# Exercise the port, then collect the result.
$code = ./tools/vm/vmrun.ps1 run -Vm $vm -CommandLine `
  'powershell -ExecutionPolicy Bypass -File C:\vss\test-port.ps1'
if ($code -ne 0) { throw "guest test failed: $code" }

./tools/vm/vmrun.ps1 stop -Vm $vm
```

`vmrun.ps1 run` wraps the guest command so its exit code comes back to the
host; `copy-to` needs the guest credentials above. `list`, `status`, `start`,
`stop`, `snapshot` and `revert` need no credentials.

## Install and remove

```cmd
:: install (from C:\vss)
pnputil /add-driver vsscom.inf /install

:: remove every package published from this INF
pnputil /enum-drivers | findstr /i vsscom
pnputil /delete-driver oem<NN>.inf /uninstall /force
```

Install creates the device from the INF's hardware ID; the Ports class
installer assigns the `COM<n>` name. `devcon` from the WDK is an alternative.

## What the guest test must cover

- The device enumerates and appears under **Ports (COM & LPT)**.
- Opening `\\.\COM<n>` succeeds and `SetCommState` accepts 115200 8N1.
- Write followed by read returns the same bytes (loopback mode).
- `EscapeCommFunction(SETDTR)`, `SETRTS`, `CLRDTR`, `CLRRTS` succeed and
  `GetCommModemStatus` reports CTS/DSR.
- `PurgeComm` empties the buffer.
- The package uninstalls without leaving the device behind.

## CI

The driver branch adds a workflow that builds the package on a Windows runner
with the WDK (the hosted image ships it) and uploads `vsscom.sys`, the INF and
the catalog. The VM harness consumes that artifact, so the guest never needs a
compiler.

## Safety

- Never run this on the development machine's own installation.
- Revert to `clean` before each run; a failed install can leave the guest in a
  state that is tedious to repair.
- Keep guest credentials in the environment, never in a file that git sees.
