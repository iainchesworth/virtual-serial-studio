# SPDX-License-Identifier: GPL-3.0-or-later
#
# Language choice: which catalogue a list of system languages selects, and
# which languages are written right to left.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_language_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shell/tests/main.cpp
    test_language_choice.cpp)

  target_include_directories(vss_gui_language_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_language_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_language_tests PRIVATE
    vss_gui_language
    vss_gui_theme
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_language_tests)
endif()
