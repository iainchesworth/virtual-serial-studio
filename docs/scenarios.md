# Scenario recipes

Each recipe can be built in the GUI, scripted with `vss_cli`, or written as a
JSON topology that both accept. The URIs are examples.

## Share one serial port with several applications

The device is opened once; every listener gets its own virtual port.

```bash
vss_cli share serial://COM3 --listeners=3 --seconds=60
```

```
listener-1  COM5      <- open this in PuTTY
listener-2  loopback:listener-2
listener-3  loopback:listener-3
```

On Windows the first listener takes the other end of the com0com pair that
`COM3` belongs to. Once that pair is claimed, further listeners fall back to
loopback, because a null-modem pair can only be wired into the hub once.
On Linux and macOS, listeners are pseudo terminals.

In JSON:

```json
{
  "name": "share",
  "ports": [
    { "name": "device", "kind": "physical", "uri": "serial://COM3?baud=115200" },
    { "name": "app",    "kind": "virtual", "virtual_kind": "auto" }
  ],
  "links": [
    { "mode": "split", "sources": ["device"], "targets": ["app"] }
  ]
}
```

## Bridge a device to the network

```bash
vss_cli bridge serial://COM3 tcp://192.168.1.50:4001
```

The same command accepts `udp://host:port` and `rfc2217://host:7001`. The
bridge is a duplex link, so data from the network reaches the device and vice
versa. Both directions are visible in the monitor.

## Merge several publishers into one device

```bash
vss_cli join serial://COM3 serial://COM5 --target=tcp://10.0.0.2:4001
```

By default the publishers interleave. To hand the link to one publisher at a
time, use an `owner` policy:

```json
{ "mode": "join", "sources": ["a", "b"], "targets": ["merged"],
  "policy": "owner" }
```

`studio` exposes `set_owner`/`release_owner` through the hub; while one
publisher owns the link the other publishers' bytes are counted as dropped, not
silently interleaved.

## Put several programs on one bus

A polled protocol such as Modbus RTU runs on a single line with one master and
several devices, and every station hears every byte. A shared bus gives each
program its own port and joins them like that line: what one writes goes to all
the others and never back to itself.

```bash
vss_cli bus --virtual=3 --tap=virtual --seconds=60
```

```
member-1 -> /dev/pts/4          <- open this in the master
member-2 -> /dev/pts/5          <- and these in the devices
member-3 -> /dev/pts/6
tap -> /dev/pts/7               <- a listener that cannot transmit
bus of 3 members (link 1)
```

Add `<uri>` arguments to put real or network ports on the bus as well, for
example `vss_cli bus serial://COM3 tcp://10.0.0.2:4001 --virtual=1`.

In JSON the bus lists its members, and the listener is a join from them that
carries no modem lines:

```json
{
  "name": "bus",
  "ports": [
    { "name": "master",   "kind": "virtual" },
    { "name": "device-1", "kind": "virtual" },
    { "name": "device-2", "kind": "virtual" },
    { "name": "listener", "kind": "virtual" }
  ],
  "links": [
    { "mode": "bus", "members": ["master", "device-1", "device-2"] },
    { "mode": "join", "sources": ["master", "device-1", "device-2"],
      "targets": ["listener"], "emulate_modem_lines": false }
  ]
}
```

A bus has no modem lines, no owner policy, and a port can be on one bus only.
A route out of the bus and back into it is refused as a loop. The listener is
outside the bus on purpose: a port that is not a member cannot send onto it.

## Watch traffic without disturbing the device

`monitor` links are one-way and do not emulate modem lines, so the observer
cannot change what the device sees.

```bash
vss_cli loopback      # a console port and a monitor port, no hardware
```

The GUI monitor reads from the hub's ring buffer and shows both directions with
ASCII or hex previews. The send panel writes into any open endpoint.

## Test software that watches the control lines

1. Attach a device (or create a pair of virtual ports).
2. Link them duplex with modem emulation on.
3. Toggle DTR in the inspector.
4. The peer's DSR and DCD follow, which is exactly what a null-modem cable does.

From code:

```cpp
studio app;
const auto a = app.create_virtual_port("a", vss::virtual_port_kind::loopback);
const auto b = app.create_virtual_port("b", vss::virtual_port_kind::loopback);
app.connect("a", "b");

(*a)->set_line(vss::control_line::dtr, true);
// b's DSR is now asserted
```

Note that two loopback devices are echo devices; use a one-way link (`monitor`
or `redirect`) when the point is observation rather than full duplex.

## Measure a device end to end

```bash
vss_cli bench --from=serial://COM3 --to=serial://COM5 --bytes=10MB --chunk=4KB
```

`sender` and `receiver` are deliberately not linked: the bytes travel through
the cable, the receiver counts what actually arrived, and the command fails if
the count is short. That is the number the performance floors are based on.

## Reproduce a failure

The serial library's mock backend is reachable as an endpoint:

```cpp
studio app;
const auto device = app.create_mock_port("modem");
```

The fixture can inject parity errors, framing errors, overruns, breaks,
hang-ups and disconnects, and the hub reports them as endpoint error events.
That makes cable-pull and noisy-line behaviour reproducible without hardware.

## Save and replay

The GUI saves whatever topology it is showing. `vss_cli run lab.json` applies
it again, so a support case, a demo or a benchmark can be reproduced with a
file:

```bash
vss_cli run lab.json --seconds=30 --verbose
```
