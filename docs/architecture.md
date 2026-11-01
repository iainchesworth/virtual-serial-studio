# Architecture

This note explains how the pieces fit and, more importantly, why the pieces
that look odd are the way they are. The threading rules and the shutdown order
are not incidental; both were written after a real failure.

## Layers

```
apps/vss_cli   apps/vss_gui
        \         /
         vss_studio        scenarios, persistence, advertised paths
        /     |      \
 vss_config  vss_ports  (both depend on) vss_core
```

`vss_core` knows nothing about serial hardware. It defines:

* `vss::transport`, the smallest useful abstraction: `async_read_some`,
  `async_write_some`, `close`, and optional control lines.
* `vss::endpoint`, one transport plus a strand, a write queue, counters and a
  control-line model.
* `vss::hub`, which owns the `io_context`, the endpoint registry, the routing
  table and the event dispatcher.

`vss_ports` implements transports: a serial port through
`boost::virtual_serial::any_serial_port`, a pseudo terminal on POSIX, a named
loopback device, and the native control-line calls. `vss_config` is pure data.
`vss_studio` composes all three into the operations a user actually asked for.

The dependency direction matters: `vss_core` could be reused for a completely
different kind of byte transport without dragging serial code along.

## Threading

Each endpoint has its own `asio::strand`. Its transport is constructed on the
hub's executor, and two rules keep the transport single-threaded:

1. The endpoint binds every completion handler it passes down to its strand.
2. Transports complete handlers on the handler's *associated* executor, not on
   their own. `loopback_transport` uses
   `get_associated_executor(handler, fallback)` for exactly this reason, and
   `serial_transport` passes the handler through untouched so the serial
   library can honour it.

Without rule 2, a transport would complete on an arbitrary hub thread while
`close()` ran on the strand, which is a data race inside the serial library.
That failure showed up as an intermittent access violation in a repeated
high-speed test, and the fix is the reason the rule is written down.

The hub's own event dispatcher runs on a single dedicated strand. Sinks
therefore never run inline with the operation that published the event, and
never on a transport thread.

### Subscriber lifetime

`unsubscribe` stops future deliveries but cannot recall one that is already
running. A sink must capture state that outlives the hub: a `shared_ptr`, or a
variable declared before the hub in the same scope. Capturing a stack local of
the scope that created the subscription is a use-after-free waiting to happen,
and the system tests demonstrate the correct shape.

### Shutdown

`hub::stop()` does not close transports from the calling thread. It asks every
endpoint to shut down on its own strand and counts the completions; the last
one stops the `io_context`. Two properties follow:

* no `close()` can race a completion handler, and
* a transport that never completes its final operation cannot hang shutdown.

`hub::join()` only joins the worker threads. Calling `stop()` and `join()` twice
is safe, and a stopped hub can be started again (`io_context::restart`).

## Routing

The router looks at the routes originating from a source endpoint and copies
each chunk to every target's write queue. Routes are derived from links:

| Link | Routes |
|---|---|
| `connect(a, b)` | a→b and b→a |
| `split(a, [b, c])` | a→b, a→c, b→a, c→a |
| `join([a, b], c)` | a→c and b→c |
| `redirect(a, b)` | a→b |
| `monitor(a, b)` | a→b with modem emulation disabled |

Adding a link is refused when it would close a cycle in the graph, because a
cycle in the routing table is an infinite loop. Cycles through *hardware* (the
two ends of a cable) cannot be detected and are documented as a user error.

`merge_policy::owner` links keep an atomic owner; a publisher that does not own
the link has its bytes counted as dropped instead of forwarded. `interleave`
links forward everything.

## Control lines

`control_lines` is a bit set split into outputs (DTR, RTS, BREAK) and inputs
(CTS, DSR, DCD, RI). When an endpoint's outputs change, the hub computes the
peer's inputs from the null-modem mapping and pushes them to every linked
endpoint:

```cpp
control_lines peer = null_modem_peer(local);
target->set_peer_inputs(peer);   // replaces the whole input set
```

The inputs are replaced rather than merged so the result does not depend on
which line moved last. Transports that can drive the device apply the change;
the rest keep the model and report it through events.

## Why loopback devices echo

A loopback transport is a loopback *plug*: what you write comes back on the
receive side. That makes it a useful demo device and a good test double, and it
means two loopback devices wired duplex are an echo chamber. This is a property
of the device, not a routing bug, and it is why the GUI's null-modem demo is a
one-way link.

## Events and capture

`endpoint::on_read` routes data on the I/O thread and then publishes an event.
Routing does not wait for the dispatcher, so the fast path stays on one thread
and copies once. Events carry a `shared_ptr<const byte_buffer>`, so fan-out to
many monitors is cheap, and the hub keeps a bounded ring buffer for late
subscribers. Endpoints can opt out of data events entirely for throughput.

## Extending it

* A new transport: implement `vss::transport`, observe the associated-executor
  and `close()` contracts, and add it to `vss_ports`.
* A new backend: the serial library already selects by URI; `serial_transport`
  classifies the scheme for labelling.
* A new scenario: compose `studio` primitives, then save the result through
  `studio::snapshot()` so it can be replayed and tested.
