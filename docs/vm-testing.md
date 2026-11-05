# Testing in the VSS guests

Anything that loads a driver, turns on test signing or needs a disposable kernel runs in a VMware Workstation guest, never on
the development machine. A snapshot is the unit of disposal.

## The guests

The guests are being rebuilt from the install media; this page describes the result. `tools/vm/matrix.json` lists them. The harness works on these three and refuses every other VM.

| Guest | System | Used for |
|---|---|---|
| `VSS - Windows 10` | Windows 10 22H2, UEFI, Secure Boot off | The Windows virtual COM driver on a down-level stack, the installed package, a screenshot |
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

For guests that only exist to test this software, `. ./tools/vm/credentials.ps1 -Create` makes the user `vss` with a random
password in `$HOME/.vss/guest-credentials.json` (readable by your user only, never replaced once it exists) and loads both variables
without printing the password. Use it for these disposable guests and for nothing else.

`vmrun` takes them as arguments, so they are visible to other processes on the host while a guest command runs. The harness
prints `<hidden>` in place of the password. Under WSL, add both names to `WSLENV`.

## Building a guest

The install media are in `E:\ISOs`. `tools/vm/iso-hashes.json` holds the SHA-256 of each, and `new-vm.ps1` checks it.

```powershell
# 1. The answer disc for the unattended install, written outside the repository.
#    It reads VSS_VM_USER and VSS_VM_PASSWORD from your shell and prints neither.
./tools/vm/new-seed.ps1 -Guest windows11

# 2. The disk and the VMX. This does not start the guest, and it never overwrites one.
./tools/vm/new-vm.ps1 -Guest windows11

# 3. Install it, verify it and take the snapshot, with nobody at the console. This needs the credentials
#    in the environment (credentials.ps1) and takes between half an hour and two hours.
./tools/vm/install-guest.ps1 -Guest windows11
```

The guest ids are `windows10`, `windows11` and `ubuntu2604`. For Ubuntu, `new-seed.ps1` hashes the password with `openssl passwd -6`
in WSL Debian, from standard input, and reads your SSH public key from `%USERPROFILE%\.vss`. The Windows answer file skips the TPM,
Secure Boot, RAM and CPU checks of Windows 11 setup, so the guest needs no virtual TPM and no encryption password. After the first
logon, `tools/vm/guest/setup-guest.ps1` installs VMware Tools, turns on test signing, creates `C:\vss` and powers the guest off.
`install-guest.ps1` runs the whole unattended install. Windows Setup restarts the machine once, and the install ISO would then boot again and start over, so the script stops the guest at that moment, disconnects the install ISO and starts it from the disk. When the guest has powered itself off, the script starts it, waits for VMware Tools, runs a check in it with the credentials, and only then takes the `clean` snapshot. Any other result takes no snapshot.

Two installers wait for a key that nobody can press: Subiquity asks "Continue with autoinstall?", and the Windows 10 media ask "Press any key to boot from CD". `new-vm.ps1` boots patched copies of those two ISOs, made by `patch_ubuntu_iso.py` and `patch_windows_iso.py` (they need `pycdlib`) and kept in `_iso` next to the VMs. The originals are never modified. The Windows disks are SATA, because the virtual NVMe disk stalled Setup.

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

`run` exits with the exit code of the guest command, and fails if the guest does not report one. On a Windows guest the command line is PowerShell syntax; on Linux it is bash. `snapshot` creates a
snapshot once and fails if the name exists. `revert` fails if the snapshot is missing. `-DryRun` prints the `vmrun` command
lines without running them. `tools/vm/test-vmrun.ps1` checks all of this against a stand-in for `vmrun`, and `test-vm.ps1` and `test-seed.ps1` check the guest builders with placeholders. All three run in CI.

## Safety

- Never load a driver or turn on test signing on the development machine.
- Revert to `clean` before each run. A failed install can leave a guest in a state that is tedious to repair.
- The snapshot, the VM files and the seed ISOs hold the guest password. Keep them out of every repository and every
  artifact upload.
