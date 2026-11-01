# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiler warning policy, applied per target so third party code and
# generated sources can opt out.
#

include_guard(GLOBAL)

set(VSS_WARNING_OPTIONS "")

if(MSVC)
  set(VSS_WARNING_OPTIONS
    /W4
    /permissive-
    /Zc:__cplusplus
    /utf-8)
  # /Zc:preprocessor is a cl.exe switch; clang-cl warns that it is unused.
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND VSS_WARNING_OPTIONS /Zc:preprocessor)
  endif()
  if(VSS_WARNINGS_AS_ERRORS)
    list(APPEND VSS_WARNING_OPTIONS /WX)
  endif()
else()
  set(VSS_WARNING_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wcast-align
    -Wunused)
  # Deliberately not -Wnull-dereference: GCC 13 reports false positives inside
  # Boost.Asio and libstdc++ when the build is optimised.
  if(VSS_WARNINGS_AS_ERRORS)
    list(APPEND VSS_WARNING_OPTIONS -Werror)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
     OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    list(APPEND VSS_WARNING_OPTIONS -Wno-unknown-pragmas)
  endif()
endif()

function(vss_set_warnings target)
  target_compile_options(${target} PRIVATE ${VSS_WARNING_OPTIONS})
endfunction()
