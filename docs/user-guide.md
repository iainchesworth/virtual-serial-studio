# Using Virtual Serial Studio

This guide is for people using the desktop application. It covers both
interface modes, the everyday jobs, the expert tools, and what to check when
something does not work.

The application is a window over the `vss::studio` engine: it creates ports,
wires them together, and shows what flows through them. Everything it can do
can also be scripted with `vss_cli`; the command equivalents are listed at the
end.

## The two modes

The window has a **Simple** mode and an **Advanced** mode. Simple mode is the
default and is designed for the most common job: "I need a serial port, now."
Advanced mode adds the tools an integrator or diagnostician needs.

| | Simple | Advanced |
|---|---|---|
| Audience | Application users, test operators | Integrators, QA, developers |
| Add virtual ports | Yes | Yes |
| Attach a real serial device | Yes | Yes |
| Connect ports (two-way, redirect, monitor, split, join) | Yes | Yes |
| Remove ports and links | Yes | Yes |
| Monitor, send, inspector, topology | Yes | Yes |
| Saved topologies (open/save) | Yes | Yes |
| First-run hint | Yes | No |
| Traffic log dock | Hidden | Shown |
| Mock fixture device | Hidden | Shown |
| Null-modem demo | Hidden | Shown |
| Share / join / bridge over the network | Hidden | Shown |
| Fit topology (link removal stays available) | Hidden | Shown |

Switch modes with **View → Advanced mode**, or in **Settings**. The choice is
remembered between runs, and the status bar always shows which mode is active.

Simple mode is not a reduced engine: every port and link it creates is a real
one. It simply hides the controls that need an understanding of URIs, fixtures
or the topology graph.

## The window at a glance

```
┌──────────────────────────────────────────────────────────────────────┐
│ File   Edit…   Ports   Topology   Tools   View   Help                │
├──────────────────────────────────────────────────────────────────────┤
│ [toolbar: New Open Save | Add port Attach Connect Remove | …]         │
├───────────────┬──────────────────────────────────┬───────────────────┤
│ Ports         │  Topology                        │ Inspector         │
│ name kind     │   ┌────────┐      ┌────────┐     │ details, counters │
│ state lines   │   │ port A │──────│ port B │     │ control lines     │
│ path rx tx    │   └────────┘      └────────┘     ├───────────────────┤
│               │                                  │ Send              │
├───────────────┴──────────────────────────────────┴───────────────────┤
│ Monitor / Log (tabs)                                                 │
├──────────────────────────────────────────────────────────────────────┤
│ Ports: 2  Links: 1  RX/TX rates  Hub: running  Advanced mode          │
└──────────────────────────────────────────────────────────────────────┘
```

- **Ports** (left): every endpoint with its name, kind, state, control lines,
  advertised path and counters. The path column is what other programs open.
- **Topology** (centre): one box per port and one edge per link. Click a box to
  inspect it, click an edge to select the link, `Delete` removes the selection.
- **Inspector** (right): details for the selected port, live counters, and — for
  real serial endpoints — DTR/RTS/break toggles plus CTS/DSR/DCD/RI indicators.
- **Send** (right): write bytes into an open port.
- **Monitor** (bottom): traffic rows with time, endpoint, direction, length and
  payload preview. `Log` is a second tab that records state changes; it is
  shown in advanced mode.
- **Status bar**: port and link counts, aggregate throughput, hub state and the
  current mode.

## Quick start

1. **Ports → Add virtual port…**, accept the suggested name and *Auto* kind.
2. Find the new row in the Ports table and read the **Path** column — for
   example `COM7` on Windows or `/dev/pts/3` on Linux.
3. Open that path in the other program (a terminal, a test harness, your
   application). Writes from that program appear in the monitor.
4. To wire two ports together, select **Ports → Connect ports…**, choose
   *Two-way (connect)*, pick source and target, press **Connect**.
5. To test without another program, use the **Send** panel: pick the open port,
   type bytes, press **Send**, and watch the monitor.

## Creating virtual ports

**Ports → Add virtual port…**

- **Name**: a unique suffix is added automatically if the name is taken. With
  **How many** above one, names become `name-1`, `name-2`, … so a batch is easy
  to recognise.
- **Kind**:
  - *Auto (best available)* — uses a com0com pair when the driver is installed
    on Windows; otherwise an in-process loopback. On Linux and macOS it creates
    a PTY.
  - *PTY (pseudo terminal)* — a real operating-system terminal another
    application can open.
  - *Loopback (in-process)* — everything written is echoed back to the same
    port; useful for protocol tests that do not need a second process.

**How many** creates up to 32 ports in one step. Repeat the action to create
more; there is no fixed ceiling beyond the operating system's resources.

Remove a port with **Ports → Remove port** or `Delete`; its links are removed
with it.

## Attaching a real serial device

**Ports → Attach serial port…**

1. Pick the device from the list. It is enumerated when the dialog opens,
   so reopen it after plugging a device in.
2. Choose the baud rate, data bits, parity, stop bits and flow control that the
   device expects.
3. Keep **Capture traffic in the monitor** ticked if you want its traffic
   recorded; clear it to keep the monitor quiet.
4. Give it a name and press **Attach**. The URI field updates as you choose,
   and advanced users can type a URI directly.

The device is opened once by the studio; your applications then open the
virtual port that the studio advertises, not the hardware itself.

## Connecting ports

**Ports → Connect ports…** (`Ctrl+L`) is the guided way to create links.

| Mode | Use it when | What the other side sees |
|---|---|---|
| **Two-way (connect)** | Two ports should behave like a null-modem cable. | Everything, both directions. |
| **One-way redirect** | Data must flow from A to B only. | Writes from A; B cannot reply. |
| **Monitor** | You want to observe traffic without disturbing it. | A one-way copy, marked as a monitor so it never drives modem lines. |
| **Split one port to many** | Several applications must see the same device. | Each target receives a copy of the source's data. |
| **Join many ports into one** | Several publishers should feed one application. | The target receives every publisher's data. |

The dialog lists the ports that currently exist, validates that a port is not
connected to itself, requires at least two targets for a split and two
publishers for a join, and prints a plain-language summary before you commit.

Remove a link by selecting its edge in the topology and pressing `Delete`, or
**Topology → Remove selected link**.

## Monitoring and sending

The **Monitor** shows a row per transfer:

- **Time**, **Endpoint**, **Direction** (`rx`/`tx`), **Length**, **Preview**.
- **Pause** stops appending; traffic continues to flow and is counted.
- **Clear** empties the view.
- **Hex** renders payloads as hexadecimal; **Follow** keeps the newest row
  visible.

The **Send** panel writes bytes into an open endpoint: choose the endpoint,
type the payload, tick **Hex** if the text is hexadecimal, tick **CRLF** to
append a line ending, and press **Send**. Errors appear in the status bar and
the log.

## Control lines

Select a real serial port to see its control-line state in the **Inspector**:

- **DTR** and **RTS** can be toggled; **Break** can be asserted.
- **CTS**, **DSR**, **DCD** and **RI** are displayed as inputs.

Virtual ports accept the operations and report their own state; they do not
have physical wires.

## Saving and opening topologies

A topology is a JSON document listing ports and links.

- **File → Save** / **Save As…** writes the current studio state.
- **File → Open…** applies a saved document.
- The title bar shows a `*` while there are unsaved changes; closing asks
  first (this prompt can be turned off in Settings).
- **File → New** clears the studio.

The same file can be replayed headlessly:

```bash
vss_cli run topology.json --seconds=60
```

## Network tools (advanced mode)

These live under **Scenarios** and are visible in advanced mode only.

### Share one port with several listeners

**Scenarios → Share a serial port…** opens one endpoint and gives every
listener its own virtual port. Enter the endpoint URI (for example
`serial://COM3?baud=115200`) and the number of listeners.

On Windows the first listener takes the other end of the com0com pair; further
listeners fall back to loopback because a pair can only be wired once. On Linux
and macOS, listeners are PTYs.

### Join publishers by URI

**Scenarios → Join publishers…** merges several publisher URIs into one target
URI. Each publisher is a line in the dialog.

### Bridge two transports

**Scenarios → Bridge to network…** connects two URIs back to back, for example
a serial device to a TCP server:

```text
from: serial://COM3?baud=115200
to:   tcp://192.168.1.50:4001
```

URI forms accepted by the engine:

| URI | Meaning |
|---|---|
| `serial://COM3?baud=115200` | A real serial device with framing options. |
| `tcp://host:port` | TCP client or server endpoints. |
| `udp://host:port` | UDP datagrams. |
| `rfc2217://host:port` | RFC 2217 serial-over-network. |
| `loopback:name` | An in-process loopback endpoint. |

When sharing on a LAN, allow the port through the host firewall and bind to
the interface other machines can reach (`0.0.0.0` for all interfaces).

## Mock devices and demos (advanced mode)

- **Ports → Add mock device…** creates a fixture endpoint with a name, baud
  rate and target, useful for scripted tests without hardware.
- **Scenarios → Null-modem loopback** creates two loopback ports and a
  one-way link between them, so a fresh machine can demonstrate the whole path
  in one click.

## Settings

**Tools → Settings…** (`Ctrl+,`):

- **Advanced mode** — show the expert tools.
- **Refresh interval** — how often counters and state are read (50–5000 ms).
  Shorter feels live; longer is lighter on a busy machine.
- **Theme** — follow the desktop, light or dark.
- **Default baud rate** — offered by the port dialogs.
- **Ask before discarding an unsaved topology**.

Settings are stored with `QSettings` (the registry on Windows, a config file on
Linux/macOS) and applied immediately.

## About and bug reports

**Help → About Virtual Serial Studio** has four tabs:

- **About** — version, purpose and the GPLv3 notice.
- **Environment** — commit, branch, build date and type, compiler, Qt runtime
  and build versions, operating system, architecture and kernel.
- **Licences** — Qt, Boost, Boost.VirtualSerial, Tracy and the optional com0com
  driver.
- **Fonts** — no fonts are bundled; the system UI font and its licence are
  named.

Copy the Environment tab into a bug report; it answers the "which build?"
question.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New topology |
| `Ctrl+O` | Open topology |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save as |
| `Ctrl+L` | Connect ports |
| `Ctrl+,` | Settings |
| `F5` | Refresh now |
| `Ctrl+0` | Fit topology |
| `Delete` | Remove selected port or link |
| `Ctrl+Q` | Quit |

## Troubleshooting

**No COM ports appear on Windows.** A virtual COM port needs a pair driver.
Install com0com (or use the optional Windows COM driver from the driver
branch); *Auto* falls back to an in-process loopback, whose path is
`loopback:<name>` and which other applications cannot open. PTYs do not exist
on Windows.

**A PTY path is shown but another program cannot open it.** On Linux the path
is `/dev/pts/N`; it exists only while the studio is running. Check permissions
for the user running the studio.

**A real device cannot be opened.** Another program may already hold it. Close
that program, then attach again. On Linux, add your user to `dialout` and log
in again.

**Network peers cannot connect.** Check the firewall, that you bound to an
address the peer can reach, and that the port number matches. Test locally with
`127.0.0.1` first.

**The counters do not move.** Check the port state in the Ports table; a port
that is `failed` or `closed` will not carry traffic. The log tab records state
changes and errors.

**Where are the logs?** The **Log** dock in advanced mode, the status bar, and
`vss_cli --verbose` for the command line.

## Command line equivalents

| Task | Command |
|---|---|
| Create one virtual port | `vss_cli virtual --kind=auto` |
| Create and hold a topology | `vss_cli run topology.json` |
| Share a device | `vss_cli share serial://COM3 --listeners=3` |
| Join publishers | `vss_cli join tcp://a:1 tcp://b:2 --target=serial://COM4` |
| Bridge two transports | `vss_cli bridge serial://COM3 tcp://host:4001` |
| Loopback demo | `vss_cli loopback` |
| List devices | `vss_cli devices` |
| Measure throughput | `vss_cli bench --from=… --to=…` |
| Self-test the wiring | `vss_cli selftest` |

## Deliberate limits

These remain unavailable in this build, with a visible reason in the window:

- the network endpoint can connect to a server but cannot wait for one (the
  engine has no listening transport);
- a real device's "in use" state is reported only when the adapter can prove
  it; the engine does not probe devices;
- the real adapter cannot select a com0com pair name, so linked-pair names are
  read-only;
- undo/redo need an exact restore that the engine cannot prove;
- real-device templates need a device picker before they can be enabled;
- named bundles of links and baud-rate throttling are not implemented.

The Log and Topology JSON tools are Advanced docks; the main Monitor carries
the everyday traffic view. See [ux.md](ux.md) for the design and roadmap, and
[scenarios.md](scenarios.md) for more worked examples.
