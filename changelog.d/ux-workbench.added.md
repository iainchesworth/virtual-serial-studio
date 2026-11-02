The desktop shell now has a settings-driven interface. Simple mode, the
default, shows the everyday jobs — create a port, attach a real device,
connect ports, watch traffic — and hides fixtures, the topology tools and the
raw-URI scenarios until Advanced mode is enabled.

**Connect ports…** (`Ctrl+L`) replaces hand-built links with a guided dialog:
pick two-way, one-way redirect, monitor, split to many targets or join many
publishers, with a plain-language summary and validation.

**Settings** (`Ctrl+,`) stores advanced mode, the refresh interval, the theme
(follow the desktop, light or dark) and the default baud rate in `QSettings`
and applies them immediately.

The **About** window gained Environment, Licences and Fonts tabs alongside the
GPLv3 notice. Environment reports the commit, branch, build date and type,
compiler, Qt runtime/build versions, OS and kernel; the window is designed to
be copied into a bug report. Licences lists Qt, Boost, Boost.VirtualSerial,
Tracy and the optional com0com driver; Fonts states that no fonts are bundled
and names the system font in use.

Adding virtual ports can now create several at once (up to 32 per step), with numeric name suffixes, so a machine can be filled with ports without repeating the dialog.
