# SPDX-License-Identifier: GPL-3.0-or-later
#
# Advanced tools: owner control, line matrix and faults, mock fixtures,
# topology JSON, benchmark and export as command.  Every control that needs a
# missing engine capability is disabled with the shared degraded tooltip.
#

add_library(vss_gui_advanced STATIC
  add_fixture_dialog.cpp
  add_fixture_dialog.hpp
  benchmark_dialog.cpp
  benchmark_dialog.hpp
  chrome.cpp
  chrome.hpp
  command_panel.cpp
  command_panel.hpp
  event_log.cpp
  event_log.hpp
  export_command_dialog.cpp
  export_command_dialog.hpp
  json_editor.cpp
  json_editor.hpp
  json_validation.cpp
  json_validation.hpp
  line_matrix_panel.cpp
  line_matrix_panel.hpp
  line_presets.cpp
  line_presets.hpp
  owner_bar.cpp
  owner_bar.hpp
  wiring_dialog.cpp
  wiring_dialog.hpp)

target_include_directories(vss_gui_advanced PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(vss_gui_advanced PUBLIC
  vss_gui_widgets
  vss_gui_backend
  vss_gui_theme
  Qt6::Widgets)

target_compile_features(vss_gui_advanced PUBLIC cxx_std_23)
target_compile_definitions(vss_gui_advanced PRIVATE QT_NO_KEYWORDS)
set_target_properties(vss_gui_advanced PROPERTIES AUTOMOC ON)
vss_set_warnings(vss_gui_advanced)

add_subdirectory(tests)
