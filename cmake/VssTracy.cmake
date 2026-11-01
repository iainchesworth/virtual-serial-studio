# SPDX-License-Identifier: GPL-3.0-or-later
#
# Optional Tracy instrumentation, provided by vcpkg.
#
#   cmake -DVSS_TRACY=ON \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#
# The default is ON for Debug builds configured through the vcpkg toolchain
# and OFF otherwise. Turning it on in a Release build is supported and is how
# a field problem gets triaged: the client idles until a profiler attaches.
#
# The manifest asks vcpkg for the "on-demand" feature, so instrumentation only
# records while a profiler is connected; that is what allows attaching to a
# process that is already running. The definition below must match the client
# library the port builds, which is why Tracy is only supported through vcpkg.

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
  INTERFACE_COMPILE_DEFINITIONS "VSS_TRACY=1;TRACY_ENABLE=1;TRACY_ON_DEMAND=1")

message(STATUS "Vss: Tracy instrumentation enabled (${VSS_TRACY_TARGET}, on-demand)")
