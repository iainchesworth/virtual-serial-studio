# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The test client for the vsscom guests (see vss_com_client.c). A separate project, not part
# of the main build: the driver job in CI and tools/vm build it with MSVC, once per
# architecture, so a guest runs the same source as a 32-bit program (Win32, under WOW64) and
# as a native one.
#
#   cmake -S drivers/windows/test/client32 -B build/client32-x86 -A Win32
#   cmake --build build/client32-x86 --config Release
#
# The output is vss_com_client_<arch>.exe with arch x86, x64 or arm64. The C runtime is linked
# in, so the program runs in a guest that has no Visual C++ runtime installed.

cmake_minimum_required(VERSION 3.25)
project(vss_com_client LANGUAGES C)

if(NOT MSVC)
  message(FATAL_ERROR "vss_com_client is built with MSVC")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(CMAKE_GENERATOR_PLATFORM)
  set(_platform "${CMAKE_GENERATOR_PLATFORM}")
else()
  set(_platform "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
endif()
string(TOLOWER "${_platform}" _platform)
if(_platform MATCHES "^(win32|x86)$")
  set(_arch x86)
elseif(_platform MATCHES "^(x64|amd64)$")
  set(_arch x64)
elseif(_platform MATCHES "arm64")
  set(_arch arm64)
else()
  message(FATAL_ERROR "unknown platform '${_platform}'")
endif()

add_executable(vss_com_client vss_com_client.c)
set_target_properties(vss_com_client PROPERTIES OUTPUT_NAME "vss_com_client_${_arch}")
target_compile_options(vss_com_client PRIVATE /W4 /WX /sdl)

# The architecture name and the windows.h switches come from here, so the source has no
# preprocessor conditionals (the repository's preprocessor policy counts them).
target_compile_definitions(vss_com_client PRIVATE "VSS_CLIENT_ARCH=\"${_arch}\"" WIN32_LEAN_AND_MEAN NOMINMAX)
