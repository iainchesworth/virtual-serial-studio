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

## Watch traffic without disturbing the device

`monitor` links are one-way and do not emulate modem lines, so the observer
cannot change what the device sees.

```bash
vss_cli loopback      # a console port and a monitor port, no hardware
```

The GUI monitor reads from the hub's ring buffer and shows both directions with
ASCII or hex previews. The send panel writes into any open endpoint.

## One master, several programs and a silent listener

One program polls and several others answer, as on a shared serial line, and
you want a record of the whole conversation with the exact time of every
message. Each program opens its own port. Studio joins the ports and records
them; nothing needs to be added to any of the programs.

```json
{
  "name": "poll-lab",
  "ports": [
    { "name": "master",   "kind": "virtual", "virtual_kind": "auto" },
    { "name": "device-1", "kind": "virtual", "virtual_kind": "auto" },
    { "name": "device-2", "kind": "virtual", "virtual_kind": "auto" },
    { "name": "listener", "kind": "virtual", "virtual_kind": "auto" }
  ],
  "links": [
    { "mode": "split",   "sources": ["master"],
      "targets": ["device-1", "device-2"] },
    { "mode": "monitor", "sources": ["master"], "targets": ["listener"] }
  ]
}
```

The master's port is the line as the master sees it: it receives what the
master sends and is written everything the devices answer. The `monitor` link
gives a fourth program a read-only copy of what the master sends; that program
cannot send anything back. Start the programs on the ports `vss_cli` prints,
then record the master's port:

```bash
vss_cli capture poll-lab.json --out poll.cap --port master --seconds=120
```

`poll.cap` has a line for each message in either direction:

```
[      1204] R 0x02|0x31|0x03
[      1251] W 0x02|0x31|0x41|0x03
```

`R` is a poll the master sent and `W` is an answer written to it, 47 ms
later. For the whole picture with the names of the ports, record every port to
JSON Lines and read it with any tool:

```bash
vss_cli capture poll-lab.json --out poll.jsonl
```

One message shows up once for each port it crossed: `R` on the sender and `W`
on each receiver. Filter on `port` and `direction` to pick the rows you want.
The `.cap` file has no port column, so keep one port per file with `--port`.

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
