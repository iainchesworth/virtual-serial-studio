<!--
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Overlay ports

`tracy/` is the vcpkg `tracy` port at the baseline in `vcpkg.json` (0.14.1,
port-version 1), with two extra features:

| Feature | Tracy option | Why |
|---|---|---|
| `manual-lifetime` | `TRACY_MANUAL_LIFETIME` | Nothing starts at load time. The profiler thread and its socket come into existence when the application calls `tracy::StartupProfiler`, which is how a release build stays dormant until someone asks for a profile. |
| `no-broadcast` | `TRACY_NO_BROADCAST` | The client never announces itself on the network. A profiler connects by address. |

Nothing else differs. `vcpkg-configuration.json` at the root of the repository
points vcpkg at this directory; when the baseline moves, copy the upstream port
again and re-apply the two features.
