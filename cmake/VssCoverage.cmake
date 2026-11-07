# SPDX-License-Identifier: GPL-3.0-or-later
#
# Statement and branch coverage.
#
# Clang (including clang-cl on Windows) uses LLVM profile instrumentation so
# the same llvm-profdata/llvm-cov pipeline runs on every platform.  GCC falls
# back to --coverage and gcov.
#
#   cmake -DVSS_COVERAGE=ON ...
#   cmake --build ...
#   ctest --test-dir build
#   python tools/coverage_report.py --build build
#

include_guard(GLOBAL)

set(VSS_COVERAGE_COMPILER "" CACHE STRING
  "Coverage toolchain: auto, clang or gcc" )

function(vss_enable_coverage target)
  if(NOT VSS_COVERAGE)
    return()
  endif()

  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING
      "Vss: coverage requested with MSVC; use clang-cl "
      "(-T ClangCL) or the GCC/Clang toolchains for real coverage data.")
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping)
    target_link_options(${target} PRIVATE -fprofile-instr-generate)
  else()
    target_compile_options(${target} PRIVATE
      --coverage
      -O0
      -fno-inline
      -fno-inline-small-functions)
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

# Instruments every target created from now on in this directory and below. The
# GUI has some twenty libraries and test programs, so they are marked together
# instead of one `vss_enable_coverage` call each.
function(vss_enable_coverage_directory)
  if(NOT VSS_COVERAGE)
    return()
  endif()
  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
  else()
    add_compile_options(--coverage -O0 -fno-inline -fno-inline-small-functions)
    add_link_options(--coverage)
  endif()
endfunction()

# Marks a target whose own sources are the subject under test; lets the
# report hide executables and the test framework itself.
function(vss_mark_coverage_subject target)
  set_property(GLOBAL APPEND PROPERTY VSS_COVERAGE_SUBJECTS ${target})
endfunction()
