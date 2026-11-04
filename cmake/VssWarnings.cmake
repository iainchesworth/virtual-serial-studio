# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiler warning policy: the shared icl::warnings set (cmake/icl), applied
# per target so third party code and generated sources can opt out. Warnings
# are errors.
#
# Two settings here differ from the shared set:
#   * GCC's -Wnull-dereference is off. It reports potential null dereferences
#     inside Boost.Asio and libstdc++ templates once they are inlined into our
#     code (GCC 13 and later, optimised builds); the code is correct and the
#     headers are not ours to change.
#   * MSVC also gets the switches the project has always used for the
#     language level, the source encoding and the conforming preprocessor.
#

include_guard(GLOBAL)

include(IclWarnings)

# Appended after the shared options, so it wins over the -Wnull-dereference in
# icl::warnings.
target_compile_options(icl_warnings INTERFACE
  "$<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>")

set(VSS_EXTRA_WARNING_OPTIONS "")
if(MSVC)
  list(APPEND VSS_EXTRA_WARNING_OPTIONS /Zc:__cplusplus /utf-8)
  # /Zc:preprocessor is a cl.exe switch; clang-cl warns that it is unused.
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND VSS_EXTRA_WARNING_OPTIONS /Zc:preprocessor)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  list(APPEND VSS_EXTRA_WARNING_OPTIONS -Wno-unknown-pragmas)
endif()

function(vss_set_warnings target)
  target_link_libraries(${target} PRIVATE icl::warnings)
  target_compile_options(${target} PRIVATE ${VSS_EXTRA_WARNING_OPTIONS})
  # moc output is compiled without the strict set.
  get_target_property(_vss_automoc ${target} AUTOMOC)
  if(_vss_automoc OR CMAKE_AUTOMOC)
    icl_relax_generated_warnings(${target})
  endif()
endfunction()
