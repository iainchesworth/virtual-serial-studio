Virtual ports that other applications can open: pseudo terminals on POSIX
(`/dev/pts/N`), com0com null-modem pairs on Windows, and an in-process loopback
device everywhere.

`vss::studio::create_virtual_port` chooses the best available kind, advertises
the path a third party application should open, and refuses to wire the same
OS port into the hub twice.
