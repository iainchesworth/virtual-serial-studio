# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fuzz targets (standard section 6.6).
#
# Every parser of untrusted input has a harness in libs/<lib>/fuzz/ with a seed
# corpus (corpus/) and the inputs of past crashes (regressions/). A harness
# gives two executables:
#
#   <name>_replay   runs every file in corpus/ and regressions/ through the
#                   harness once. It builds with any compiler, runs as the
#                   CTest test <name> (label "fuzz") and so belongs to the
#                   pull request gate. Name harnesses fuzz_<thing>.
#   <name>          the libFuzzer target, built only with ICL_ENABLE_FUZZING
#                   (the linux-fuzz preset, Clang). libFuzzer brings its own
#                   AddressSanitizer; UBSan is added here.
#
# A fuzz build is not combined with the sanitizer presets, coverage or Tracy.

include_guard(GLOBAL)

option(ICL_ENABLE_FUZZING
  "Build the libFuzzer targets (Clang only) with AddressSanitizer and UBSan" OFF)

if(ICL_ENABLE_FUZZING)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
      "ICL_ENABLE_FUZZING needs Clang with libFuzzer, not ${CMAKE_CXX_COMPILER_ID}.")
  endif()
  if(ICL_SANITIZERS OR VSS_COVERAGE OR VSS_TRACY)
    message(FATAL_ERROR
      "ICL_ENABLE_FUZZING cannot be combined with ICL_SANITIZERS, "
      "VSS_COVERAGE or VSS_TRACY. Use a separate build directory for each.")
  endif()
  # The libraries are built with coverage feedback but without the libFuzzer
  # main(), which only the fuzz targets link. -O1 keeps traces readable.
  add_compile_options(
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -O1)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "Fuzzing enabled: libFuzzer, address, undefined")
endif()

# vss_add_fuzz_target(<name> SOURCE <harness.cpp> LIBS <targets...>)
#
# Call it from a directory where enable_testing() is in effect. The corpus and
# regressions directories sit beside the harness source.
function(vss_add_fuzz_target name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "LIBS")
  if(NOT arg_SOURCE)
    message(FATAL_ERROR "vss_add_fuzz_target(${name}): SOURCE is required")
  endif()
  get_filename_component(_vss_fuzz_dir "${arg_SOURCE}" DIRECTORY)

  if(VSS_BUILD_TESTS)
    add_executable(${name}_replay
      "${arg_SOURCE}" "${PROJECT_SOURCE_DIR}/tests/fuzz/replay_main.cpp")
    target_link_libraries(${name}_replay PRIVATE ${arg_LIBS})
    vss_set_warnings(${name}_replay)
    add_test(NAME ${name}
      COMMAND ${name}_replay
        "${_vss_fuzz_dir}/corpus" "${_vss_fuzz_dir}/regressions")
    set_tests_properties(${name} PROPERTIES LABELS fuzz TIMEOUT 120)
  endif()

  if(ICL_ENABLE_FUZZING)
    add_executable(${name} "${arg_SOURCE}")
    target_link_libraries(${name} PRIVATE ${arg_LIBS})
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    vss_set_warnings(${name})
  endif()
endfunction()
