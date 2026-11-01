# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# Virtual Serial Studio
#
# A playground for real, virtual and networked serial ports, built on
# Boost.Asio and the Boost.VirtualSerial extension library.
#
#   libs/vss_core        topology, routing, control lines, events
#   libs/vss_ports       OS virtual ports (PTY, named pipe), serial transports
#   libs/vss_config      JSON configuration documents
#   libs/vss_studio      the application service used by the CLI and the GUI
#   libs/vss_testkit     reusable fakes for downstream tests
#   apps/                command line tool and Qt 6 GUI
#   tests/               unit, system and integration suites
#   perf/                throughput and latency floors
#   changelog.d/         one fragment per unreleased change

cmake_minimum_required(VERSION 3.21)

project(
  virtual_serial_studio
  VERSION 0.1.0
  DESCRIPTION "Playground for real, virtual and networked serial ports"
  HOMEPAGE_URL "https://github.com/boostorg/virtual_serial"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

set(VSS_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VSS_IS_TOP_LEVEL ON)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(VSS_BUILD_TESTS
  "Build the unit, system and integration test suites" ${VSS_IS_TOP_LEVEL})
option(VSS_BUILD_GUI
  "Build the Qt 6 configuration GUI when Qt is found" ON)
option(VSS_BUILD_PERF
  "Build the performance measurement suite" OFF)
option(VSS_BUILD_TOOLS
  "Build the auxiliary developer tools" ${VSS_IS_TOP_LEVEL})
option(VSS_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors" OFF)
option(VSS_COVERAGE
  "Instrument the libraries for statement and branch coverage" OFF)
option(VSS_SANITIZE
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(VSS_INSTALL
  "Generate install and package configuration rules" ${VSS_IS_TOP_LEVEL})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
  # The serial APIs need a Windows 7 baseline; NOMINMAX keeps std::min intact.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

# ---------------------------------------------------------------------------
# Dependencies and shared settings
# ---------------------------------------------------------------------------

include(VssWarnings)
include(VssVirtualSerial)
include(VssCoverage)
include(VssSanitizers)

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------

add_subdirectory(libs)

if(VSS_BUILD_TOOLS)
  add_subdirectory(apps/vss_cli)
endif()

if(VSS_BUILD_GUI)
  add_subdirectory(apps/vss_gui)
endif()

if(VSS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(VSS_BUILD_PERF)
  add_subdirectory(perf)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

if(VSS_INSTALL)
  install(FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/virtual_serial_studio)
  install(FILES LICENSES/BSL-1.0.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/virtual_serial_studio/LICENSES)

  set(VSS_INSTALL_TARGETS "")
  foreach(_vss_target vss_core vss_ports vss_config vss_studio)
    if(TARGET ${_vss_target})
      list(APPEND VSS_INSTALL_TARGETS ${_vss_target})
      get_target_property(_vss_dir ${_vss_target} SOURCE_DIR)
      if(EXISTS "${_vss_dir}/include")
        install(DIRECTORY "${_vss_dir}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
      endif()
    endif()
  endforeach()

  if(VSS_INSTALL_TARGETS)
    install(TARGETS ${VSS_INSTALL_TARGETS}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "Virtual Serial Studio ${PROJECT_VERSION}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Boost        : ${VSS_BOOST_INCLUDE_DIR}")
message(STATUS "  virtualserial: ${VSS_VIRTUAL_SERIAL_DIR}")
message(STATUS "  tests        : ${VSS_BUILD_TESTS}")
message(STATUS "  gui          : ${VSS_BUILD_GUI} (Qt found: ${VSS_QT_FOUND})")
message(STATUS "  perf         : ${VSS_BUILD_PERF}")
message(STATUS "  coverage     : ${VSS_COVERAGE}")
message(STATUS "")
