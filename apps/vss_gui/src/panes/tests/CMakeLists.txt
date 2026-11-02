# SPDX-License-Identifier: GPL-3.0-or-later
#
# Data pane tests: models, formatting, filters and the Send/Ports behaviour.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_panes_tests
    main.cpp
    test_panes.cpp)

  target_include_directories(vss_gui_panes_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_panes_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_panes_tests PRIVATE
    vss_gui_panes
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_panes_tests)
endif()
