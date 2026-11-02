# SPDX-License-Identifier: GPL-3.0-or-later
#
# Basic-flow tests: real dialogs over FakeStudioBackend, accessible names,
# keyboard flow, both themes and error/cancel paths.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_flows_tests
    main.cpp
    test_flows.cpp)

  target_include_directories(vss_gui_flows_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_flows_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_flows_tests PRIVATE
    vss_gui_shell
    vss_gui_flows
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_flows_tests)
endif()
