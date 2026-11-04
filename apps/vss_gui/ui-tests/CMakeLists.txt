# SPDX-License-Identifier: GPL-3.0-or-later
#
# Journey tests: the tasks of docs/gui-design/docs/screens/01, done with key
# presses and mouse clicks sent to the real window through QTest. The tests
# use the in-tree framework; QTest supplies only the input simulation.
#

add_executable(vss_gui_journey_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/shell/tests/main.cpp
  test_journeys.cpp)

target_include_directories(vss_gui_journey_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/framework)
target_compile_definitions(vss_gui_journey_tests PRIVATE QT_NO_KEYWORDS)
target_link_libraries(vss_gui_journey_tests PRIVATE
  vss_gui_backend
  vss_gui_shell
  vss_gui_flows
  Vss::studio
  Qt6::Widgets
  Qt6::Test)
vss_set_warnings(vss_gui_journey_tests)
