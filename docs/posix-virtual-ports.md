# Virtual ports on Linux and macOS

A virtual port on Linux and macOS is a pseudo terminal (PTY). The studio owns
the master side and holds the slave open, so the port exists before any program
connects. The slave is `/dev/pts/N` on Linux and `/dev/ttysNNN` on macOS.

## Stable names

`N` changes on every run, so a script cannot name the port. A stable link is a
symbolic link in a private directory that points at the current slave:

```bash
vss_cli vport --name=ttyVSS0,ttyVSS1 --link=ttyVSS0:ttyVSS1
```

```text
/run/user/1000/vss/ttyVSS0 -> /dev/pts/3
/run/user/1000/vss/ttyVSS1 -> /dev/pts/4
linked ttyVSS0:ttyVSS1
```

- `vport` runs until Ctrl-C or SIGTERM and removes its links when it stops.
  `--seconds=N` stops it after N seconds. `vss_cli virtual` also runs until
  stopped now; it used to exit at once and close the port it had printed.
- `--link=a:b` joins two ports as a null-modem cable does: bytes written to one
  come out of the other. Several links are separated by commas.
- A name is one path component of letters, digits, `.`, `_` and `-`, at most
  64 characters. Another character in a port name becomes `-` in its link.

### Where the links go

The first that applies: `$VSS_VPORT_DIR`, then `$XDG_RUNTIME_DIR/vss`, then
`/run/vss` and `/tmp/vss` on Linux or `/tmp/vss` on macOS. `--stable-dir=<dir>`
names the directory directly.

The directory is created with mode 0700. It is refused, not used, if it
belongs to someone else, is open to the group or others, or is a link. A
directory named by an option or an environment variable has no fallback.

### If a process dies

A process that is killed cannot remove its links. The next start with the same
name replaces a link whose target is gone. It refuses a name that a running
process still holds, and anything in the directory that is not a link.

## What programs see

- A program that takes a typed or configured path works: picocom, minicom,
  screen, socat, Boost.Asio, `serial.Serial(path)` in pyserial and
  `QSerialPort::setPortName`.
- A program that offers a list of detected ports does not list a PTY or a link
  to one. pyserial reads `/proc/tty/drivers` and Qt reads udev (Linux) or IOKit
  (macOS), and none of them has an entry for a PTY.
- The modem lines (DTR, RTS, CTS, DSR) do not reach a program on a Linux PTY,
  because the kernel's PTY has no modem control. The studio shows its own
  state for them.

## Windows

Windows has no stable-link layer. The driver owns the `COMn` names, and
`vss_cli vport` says so and exits with code 2.

## Not planned

- A kernel module such as `tty0tty`, or `dummy_hcd` with `g_serial`, which would
  make a port that every program lists. They need root and a matching kernel
  and cannot be shipped.
- A macOS SerialDriverKit driver. It needs an Apple entitlement and a Mac.
