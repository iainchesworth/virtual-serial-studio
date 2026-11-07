# SPDX-License-Identifier: GPL-3.0-or-later
#
# Simple-mode tests: setup kinds, building and changing setups, the job forms
# and the setup page, against the fake backend.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_simple_tests
    main.cpp
    test_simple.cpp)

  target_include_directories(vss_gui_simple_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_simple_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_simple_tests PRIVATE
    vss_gui_shell
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_simple_tests)
endif()
