# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reusable fakes and helpers for tests, inside this repository and for
# downstream consumers.
#

add_library(vss_testkit src/harness.cpp)
add_library(Vss::testkit ALIAS vss_testkit)

target_include_directories(vss_testkit PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(vss_testkit PUBLIC cxx_std_23)
target_link_libraries(vss_testkit PUBLIC vss_core vss_ports)

vss_set_warnings(vss_testkit)
vss_enable_coverage(vss_testkit)
