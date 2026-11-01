# SPDX-License-Identifier: GPL-3.0-or-later
#
# Optional Tracy instrumentation, provided by vcpkg.
#
#   vcpkg install --triplet x64-windows        (manifest mode, picks tracy)
#   cmake -DVSS_TRACY=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#
# The client library is built with TRACY_ENABLE by the vcpkg port; consumers
# only need the headers, the library and the same definition.

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
    "VSS_TRACY is ON but the tracy package was not found.\n"
    "  Install it with vcpkg (the manifest already lists it) and configure "
    "with -DCMAKE_TOOLCHAIN_FILE=\${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake.")
endif()

add_library(Vss::Tracy INTERFACE IMPORTED GLOBAL)
set_target_properties(Vss::Tracy PROPERTIES
  INTERFACE_LINK_LIBRARIES "${VSS_TRACY_TARGET}"
  INTERFACE_COMPILE_DEFINITIONS "VSS_TRACY=1;TRACY_ENABLE=1")

message(STATUS "Vss: Tracy instrumentation enabled (${VSS_TRACY_TARGET})")
