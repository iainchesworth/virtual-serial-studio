The studio core: a hub that owns endpoints, links and the event dispatcher.
Endpoints are built on `boost::virtual_serial::any_serial_port`, so the same
topology engine drives real UARTs, RFC 2217 sessions, TCP/UDP bridges and
scriptable mock devices.

The public surface is `vss::hub` (endpoints and links), `vss::endpoint`
(queues, counters, control lines) and `vss::event_sink` (a filtered callback
interface that never runs on a transport thread by accident).
