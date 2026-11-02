# SPDX-License-Identifier: GPL-3.0-or-later
#
# Unit and system suites.  Both use the in-tree framework in framework/.
#

set(VSS_UNIT_TEST_SOURCES
  framework/main.cpp
  unit/test_bytes.cpp
  unit/test_control_lines.cpp
  unit/test_document.cpp
  unit/test_endpoint.cpp
  unit/test_error_and_log.cpp
  unit/test_hub_events.cpp
  unit/test_hub_topology.cpp
  unit/test_loopback_transport.cpp
  unit/test_native_and_com0com.cpp
  unit/test_serial_settings.cpp
  unit/test_serial_transport.cpp
  unit/test_studio.cpp
  unit/test_virtual_ports.cpp)

add_executable(vss_unit_tests ${VSS_UNIT_TEST_SOURCES})
target_include_directories(vss_unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/framework
  ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(vss_unit_tests PRIVATE vss_studio vss_testkit)
vss_set_warnings(vss_unit_tests)
vss_enable_coverage(vss_unit_tests)

add_test(NAME unit COMMAND vss_unit_tests
  --verbose --junit=${CMAKE_BINARY_DIR}/unit-tests.xml)
set_tests_properties(unit PROPERTIES TIMEOUT 300)

set(VSS_SYSTEM_TEST_SOURCES
  framework/main.cpp
  system/test_system_serial.cpp
  system/test_system_tcp.cpp
  system/test_system_udp.cpp)

add_executable(vss_system_tests ${VSS_SYSTEM_TEST_SOURCES})
target_include_directories(vss_system_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/framework
  ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(vss_system_tests PRIVATE vss_studio vss_testkit)
vss_set_warnings(vss_system_tests)
vss_enable_coverage(vss_system_tests)

add_test(NAME system COMMAND vss_system_tests
  --verbose --junit=${CMAKE_BINARY_DIR}/system-tests.xml)
set_tests_properties(system PROPERTIES TIMEOUT 600)

# A packaging smoke test: the public headers compile on their own.
add_executable(vss_header_selftest compile/header_selftest.cpp)
target_link_libraries(vss_header_selftest PRIVATE vss_studio vss_testkit)
vss_set_warnings(vss_header_selftest)
add_test(NAME headers COMMAND vss_header_selftest)

if(TARGET vss_gui)
  add_test(NAME gui_selftest COMMAND vss_gui --selftest)
  set_tests_properties(gui_selftest PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

# The backend tests exist only when Qt and the tests are both enabled.
if(TARGET vss_gui_backend_tests)
  add_test(NAME gui_backend COMMAND vss_gui_backend_tests --verbose)
  set_tests_properties(gui_backend PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

# Theme and widget tests need a QApplication but stay offscreen.
if(TARGET vss_gui_theme_tests)
  add_test(NAME gui_theme COMMAND vss_gui_theme_tests --verbose)
  set_tests_properties(gui_theme PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

if(TARGET vss_gui_shell_tests)
  add_test(NAME gui_shell COMMAND vss_gui_shell_tests --verbose)
  set_tests_properties(gui_shell PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

# Ports, Inspector, Send and Monitor models and panels.
if(TARGET vss_gui_panes_tests)
  add_test(NAME gui_panes COMMAND vss_gui_panes_tests --verbose)
  set_tests_properties(gui_panes PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

if(TARGET vss_gui_topology_tests)
  add_test(NAME gui_topology COMMAND vss_gui_topology_tests --verbose)
  set_tests_properties(gui_topology PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

# Advanced tools need a QApplication; every case runs offscreen.
if(TARGET vss_gui_advanced_tests)
  add_test(NAME gui_advanced COMMAND vss_gui_advanced_tests --verbose)
  set_tests_properties(gui_advanced PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()

if(TARGET vss_gui_support_tests)
  add_test(NAME gui_support COMMAND vss_gui_support_tests --verbose)
  set_tests_properties(gui_support PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${VSS_QT_BIN_DIR};$ENV{PATH}")
endif()
