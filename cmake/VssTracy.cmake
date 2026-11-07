# SPDX-License-Identifier: GPL-3.0-or-later
#
# Optional Tracy instrumentation, provided by vcpkg.
#
#   cmake -DVSS_TRACY=ON \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#
# The default is ON for Debug builds configured through the vcpkg toolchain
# and OFF otherwise. Turning it on in a Release build is supported and is how
# a field problem gets triaged: the instrumentation is compiled in but dormant,
# and the program starts the profiler when it is asked to (--profile, the
# VSS_PROFILE environment variable, or the GUI's Diagnostics toggle).
#
# The manifest asks vcpkg for three features of the overlay port in
# cmake/vcpkg-ports/tracy:
#
#   on-demand        zones only record while a profiler is connected, which is
#                    what allows attaching to a process that is already running
#   manual-lifetime  no profiler thread or socket exists until the program
#                    calls tracy::StartupProfiler (vss::profiling::enable)
#   no-broadcast     the client never announces itself on the network
#
# The definitions below must match the client library the port builds, which is
# why Tracy is only supported through vcpkg.
#
# Debug builds start profiling by themselves (libs/vss_core defines
# VSS_PROFILING_DEFAULT_ON for them, which vss::profiling::default_on reports)
# and listen on this computer only. Nothing else
# does, and a shipped build is never Debug.

include_guard(GLOBAL)

if(NOT VSS_TRACY)
  return()
endif()

find_package(Tracy CONFIG QUIET)

if(TARGET Tracy::TracyClient)
  set(VSS_TRACY_TARGET Tracy::TracyClient)
elseif(TARGET tracy::tracy)
  set(VSS_TRACY_TARGET tracy::tracy)
elseif(TARGET tracy)
  set(VSS_TRACY_TARGET tracy)
else()
  message(FATAL_ERROR
    "VSS_TRACY is ON but the Tracy package was not found.\n"
    "  Install it with vcpkg (the manifest already lists it) and configure "
    "with -DCMAKE_TOOLCHAIN_FILE=\${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake.\n"
    "  Without the vcpkg toolchain the option defaults to OFF.")
endif()

add_library(Vss::Tracy INTERFACE IMPORTED GLOBAL)
set_target_properties(Vss::Tracy PROPERTIES
  INTERFACE_LINK_LIBRARIES "${VSS_TRACY_TARGET}"
  INTERFACE_COMPILE_DEFINITIONS
    "VSS_TRACY=1;TRACY_ENABLE=1;TRACY_ON_DEMAND=1;TRACY_MANUAL_LIFETIME=1;TRACY_NO_BROADCAST=1")

message(STATUS "Vss: Tracy instrumentation enabled (${VSS_TRACY_TARGET}, on-demand, manual lifetime)")
