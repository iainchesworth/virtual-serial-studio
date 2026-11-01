# SPDX-License-Identifier: GPL-3.0-or-later
#
# Performance floors.  Opt in with -DVSS_BUILD_PERF=ON; CI runs the resulting
# binary in Release and gates it with tools/perf_report.py.
#

add_executable(vss_perf perf_main.cpp)
target_compile_features(vss_perf PRIVATE cxx_std_23)
target_link_libraries(vss_perf PRIVATE Vss::studio Vss::ports)
vss_set_warnings(vss_perf)

add_custom_target(vss_perf_run
  COMMAND vss_perf --output=${CMAKE_BINARY_DIR}/perf-results.json
  DEPENDS vss_perf
  COMMENT "Running the performance suite")

add_test(NAME perf_smoke COMMAND vss_perf --quick)
set_tests_properties(perf_smoke PROPERTIES TIMEOUT 300)
