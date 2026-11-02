# SPDX-License-Identifier: GPL-3.0-or-later
#
# Advanced tools tests: owner bar, wiring and faults, fixtures, JSON, event
# log, benchmark and export command.  Offscreen, in-tree framework.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_advanced_tests
    main.cpp
    test_advanced.cpp)

  target_include_directories(vss_gui_advanced_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_advanced_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_advanced_tests PRIVATE
    vss_gui_shell
    vss_gui_advanced
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_advanced_tests)
endif()
