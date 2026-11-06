# SPDX-License-Identifier: GPL-3.0-or-later
#
# The redesigned window shell: actions, level, launcher, command palette,
# status bar, the dock frame, and the Simple-mode page in ../simple.
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
  setup_group.cpp
  setup_group.hpp
  shell_window.cpp
  shell_window.hpp
  status_bar.cpp
  status_bar.hpp
  undo_service.cpp
  undo_service.hpp
  undo_steps.cpp
  undo_steps.hpp
  ../simple/job_form.cpp
  ../simple/job_form.hpp
  ../simple/job_grid.cpp
  ../simple/job_grid.hpp
  ../simple/job_plan.cpp
  ../simple/job_plan.hpp
  ../simple/setup_kinds.cpp
  ../simple/setup_kinds.hpp
  ../simple/setup_page.cpp
  ../simple/setup_page.hpp
  ../simple/simple_page.cpp
  ../simple/simple_page.hpp)

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
add_subdirectory(../simple/tests simple_tests)
