# SPDX-License-Identifier: GPL-3.0-or-later
#
# WP6 dialog tests: connect modes, share wizard, network endpoint, templates,
# accessible names, sizes, keyboard flow and review screenshots.
#

if(VSS_BUILD_TESTS)
  add_executable(vss_gui_dialogs_tests
    main.cpp
    test_connect_ports.cpp
    test_share_wizard.cpp
    test_network_endpoint.cpp
    test_templates.cpp
    test_accessibility.cpp)

  target_include_directories(vss_gui_dialogs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/framework)
  target_compile_definitions(vss_gui_dialogs_tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(vss_gui_dialogs_tests PRIVATE
    vss_gui_dialogs
    Vss::studio
    Qt6::Widgets)
  vss_set_warnings(vss_gui_dialogs_tests)
endif()
