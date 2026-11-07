The window no longer freezes ("Not responding") while ports are busy. With a
loopback linked to a fixture that sent and echoed bytes, every batch of traffic
made the window ask the operating system about the driver, the serial devices
and the firewall several times over (about 30 ms each), tell every pane that
the ports and links had changed because a byte counter moved, look at the
engine twice, and reset the whole Monitor list. A burst of events was handled
in one go, so the window could be away from its event loop for a second or
more and Windows marked it as not responding.

Now the setup readiness is reused for a few seconds, a change of byte counters
is a signal of its own (`stats_changed`) that only redraws the numbers, the
engine is looked at once per burst, the Monitor adds and removes rows instead
of resetting, and a flood of events is handed to the window in slices with a
pause between them so input and painting keep their share.
