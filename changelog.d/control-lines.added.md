RS-232 control line modelling: DTR, RTS and BREAK are driven per port, CTS,
DSR, DCD and RI are reported, and a null-modem link crosses DTR→DSR and
RTS→CTS so control-line handling can be exercised without hardware.

Windows uses `GetCommModemStatus`/`EscapeCommFunction`; POSIX uses
`ioctl(TIOCMGET)` and `TIOCMBIS`.
