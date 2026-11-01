Fixed the CI matrix on GCC and Clang: deriving from
`boost::system::error_category` no longer trips `-Wnon-virtual-dtor`, the
native control-line test skips on devices that cannot report modem status (a
PTY slave on Linux), and CTest runs with `-C Release` on multi-config
generators.

macOS builds are opt-in through the `ENABLE_MACOS` repository variable,
because a private repository consumes paid macOS minutes.
