# SPDX-License-Identifier: GPL-3.0-or-later
#
# The redesigned window shell: actions, level, launcher, command palette,
# status bar and the dock frame.  Panes are placeholders until WP3 to WP8.
#

add_library(vss_gui_shell STATIC
  accessibility.cpp
  accessibility.hpp
  action_registry.cpp
  action_registry.hpp
  command_palette.cpp
  command_palette.hpp
  format.hpp
  launcher.cpp
  launcher.hpp
  level_service.cpp
  level_service.hpp
  port_warnings.cpp
  port_warnings.hpp
  shell_window.cpp
  shell_window.hpp
  status_bar.cpp
  status_bar.hpp
  undo_service.cpp
  undo_service.hpp
  undo_steps.cpp
  undo_steps.hpp)

target_link_libraries(vss_gui_shell PUBLIC
  vss_gui_panes
  vss_gui_topology
  vss_gui_advanced
  vss_gui_dialogs
  vss_gui_widgets
  vss_gui_flows
  vss_gui_backend
  vss_gui_theme
  vss_gui_support
  Qt6::Widgets)

target_compile_features(vss_gui_shell PUBLIC cxx_std_23)
target_compile_definitions(vss_gui_shell PRIVATE QT_NO_KEYWORDS)
set_target_properties(vss_gui_shell PROPERTIES AUTOMOC ON)
vss_set_warnings(vss_gui_shell)

add_subdirectory(tests)
