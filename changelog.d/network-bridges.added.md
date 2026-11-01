Network bridging: a serial port can be paired with `tcp://`, `udp://` or
`rfc2217://` endpoints, and any endpoint can be split across several listeners
or merged from several publishers. Links carry arbitration (`interleave` or
`owner`) and bridge creation rejects topologies that would form a cycle.
