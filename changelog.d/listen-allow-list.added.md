**A listening address can say who may connect, and sends at once.** A listening address can say who may connect: `?allow=` after the port, then
addresses or ranges such as `192.168.1.0/24`, for `tcp-listen://`,
`udp-listen://` and `rfc2217-listen://`. A TCP or RFC 2217 client outside the
list is closed as soon as it connects, and a UDP datagram from outside it is
ignored. Every accepted TCP connection now has Nagle's algorithm off, so a short
reply on a polled serial bus is not held back, and a test measures the round
trip over loopback. The network dialog and `vss_cli bridge` warn when an RFC 2217
server, which has no password, can be reached from another computer and has no
list.
