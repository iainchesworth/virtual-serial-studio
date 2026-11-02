# SPDX-License-Identifier: GPL-3.0-or-later
#
# The WP6 dialogs: connect ports, the share wizard, the network endpoint and
# set-ups/templates.  Every dialog talks to the engine through StudioBackend
# and reads colours and type from Theme only.
#

add_library(vss_gui_dialogs STATIC
  common.cpp
  common.hpp
  connect_ports.cpp
  connect_ports.hpp
  network_endpoint.cpp
  network_endpoint.hpp
  network_uri.cpp
  network_uri.hpp
  recent_files.cpp
  recent_files.hpp
  share_plan.cpp
  share_plan.hpp
  share_wizard.cpp
  share_wizard.hpp
  templates.cpp
  templates.hpp
  templates_dialog.cpp
  templates_dialog.hpp
  templates.qrc)

target_link_libraries(vss_gui_dialogs PUBLIC
  vss_gui_widgets
  vss_gui_backend
  vss_gui_theme
  Qt6::Widgets)

target_compile_features(vss_gui_dialogs PUBLIC cxx_std_23)
target_compile_definitions(vss_gui_dialogs PRIVATE QT_NO_KEYWORDS)
set_target_properties(vss_gui_dialogs PROPERTIES AUTOMOC ON)
vss_set_warnings(vss_gui_dialogs)

if(VSS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
