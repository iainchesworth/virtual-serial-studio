# Testing in the VSS guests

Anything that loads a driver, turns on test signing or needs a disposable kernel runs in a VMware Workstation guest, never on
the development machine. A snapshot is the unit of disposal.

## The guests

The guests are being rebuilt from the install media; this page describes the result. `tools/vm/matrix.json` lists them. The harness works on these three and refuses every other VM.

| Guest | System | Used for |
|---|---|---|
| `VSS - Windows 10` | Windows 10 22H2, BIOS | The Windows virtual COM driver on a down-level stack, the installed package, a screenshot |
| `VSS - Windows 11` | Windows 11 25H2, UEFI, Secure Boot off, no vTPM | The same on the newest stack |
| `VSS - Ubuntu 26.04` | Ubuntu 26.04.1 LTS | Pseudo-terminal ports, `vport`, what third-party serial programs see |

The Windows guests have test signing on, VMware Tools, auto-logon (so a screenshot shows a desktop) and an empty `C:\vss`.
The Ubuntu guest has `open-vm-tools`, `python3`, `python3-serial`, `socat`, `picocom`, `minicom`, `screen` and `udev`, and
passwordless `sudo`, as the hosted CI runners do. None has a compiler: CI builds the package and the harness copies it in.
Each has a snapshot named `clean`, taken after setup.

## Credentials

The guest user name and password never enter the repository. Set them in your own shell:

```powershell
$env:VSS_VM_USER = 'vss'
$env:VSS_VM_PASSWORD = Read-Host -AsSecureString | ConvertFrom-SecureString -AsPlainText
```

`vmrun` takes them as arguments, so they are visible to other processes on the host while a guest command runs. The harness
prints `<hidden>` in place of the password. Under WSL, add both names to `WSLENV`.

## The loop

```powershell
$vm = 'D:\Virtual Machines\VSS - Windows 10\VSS - Windows 10.vmx'

./tools/vm/vmrun.ps1 revert -Vm $vm            # always start from the prepared state
./tools/vm/vmrun.ps1 start  -Vm $vm
./tools/vm/vmrun.ps1 wait-tools -Vm $vm
./tools/vm/vmrun.ps1 copy-to -Vm $vm -Local build\package.zip -Guest 'C:\vss\package.zip'
./tools/vm/vmrun.ps1 run -Vm $vm -CommandLine 'powershell -File C:\vss\test.ps1'
"guest exit code: $LASTEXITCODE"
./tools/vm/vmrun.ps1 screenshot -Vm $vm -Local out\windows10.png
./tools/vm/vmrun.ps1 stop -Vm $vm
```

`run` exits with the exit code of the guest command, and fails if the guest does not report one. `snapshot` creates a
snapshot once and fails if the name exists. `revert` fails if the snapshot is missing. `-DryRun` prints the `vmrun` command
lines without running them. `tools/vm/test-vmrun.ps1` checks all of this against a stand-in for `vmrun`, and runs in CI.

## Safety

- Never load a driver or turn on test signing on the development machine.
- Revert to `clean` before each run. A failed install can leave a guest in a state that is tedious to repair.
- The snapshot, the VM files and the seed ISOs hold the guest password. Keep them out of every repository and every
  artifact upload.
