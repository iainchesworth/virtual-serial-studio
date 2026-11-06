**The responder poll-list test no longer fails on a slow build.** It read the
recorded transmit events as soon as the responder had sent its sixth poll,
before the hub's dispatcher had recorded the sixth event. It now waits until
the recorder holds all six.
