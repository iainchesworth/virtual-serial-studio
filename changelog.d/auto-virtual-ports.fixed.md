`auto` virtual ports now select a pseudo terminal on Linux and macOS instead
of falling back to an in-process loopback device, which is what the
documentation always claimed. The driver, com0com and loopback fallbacks are
unchanged on Windows.

The sharing system test now asserts fan-out from the shared port instead of
relying on loopback echo, so it exercises the real behaviour on every
platform.
