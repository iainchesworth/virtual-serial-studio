Added the seam for a first-party Windows COM port driver, so Virtual Serial
Studio can eventually replace com0com rather than depend on it.

`vss::virtual_port_kind::windows_driver`, `vss::driver_available()`,
`vss::driver_version()` and `vss::create_driver_port()` exist now; auto
selection prefers the driver when it is installed, then com0com, then loopback.
Topologies accept `"virtual_kind": "windows-driver"`.

The driver itself is not implemented. `docs/windows-com-driver.md` defines the
control-device ABI, the shared-ring data path, the serial semantics the driver
must implement, the signing path and the phased effort.
