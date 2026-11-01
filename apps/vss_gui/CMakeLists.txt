# SPDX-License-Identifier: GPL-3.0-or-later
#
# Virtual Serial Studio - Qt 6 desktop GUI.
#
# The widgets application is a thin presentation layer over vss::studio: all
# topology, transport and configuration work stays in the libraries, the GUI
# snapshots the studio on a timer and never touches widgets from hub threads.
#

find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
  message(STATUS "Vss: Qt 6 not found, the GUI target is skipped "
                 "(set CMAKE_PREFIX_PATH to your Qt installation)")
  set(VSS_QT_FOUND FALSE PARENT_SCOPE)
  return()
endif()

set(VSS_QT_FOUND TRUE PARENT_SCOPE)

# The runtime DLL directory, so ctest can run the GUI self-test without the
# caller having to fix PATH.
get_target_property(_vss_qmake Qt6::qmake IMPORTED_LOCATION)
if(_vss_qmake)
  get_filename_component(VSS_QT_BIN_DIR "${_vss_qmake}" DIRECTORY)
  set(VSS_QT_BIN_DIR "${VSS_QT_BIN_DIR}" PARENT_SCOPE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

add_executable(vss_gui
  main.cpp
  main_window.cpp
  selftest.cpp
  port_model.cpp
  monitor_model.cpp
  topology_view.cpp
  inspector_panel.cpp
  send_panel.cpp
  dialogs.cpp
  main_window.hpp
  port_model.hpp
  monitor_model.hpp
  topology_view.hpp
  inspector_panel.hpp
  send_panel.hpp
  dialogs.hpp
  theme.hpp)

target_compile_features(vss_gui PRIVATE cxx_std_23)
target_link_libraries(vss_gui PRIVATE Vss::studio Qt6::Widgets)
target_compile_definitions(vss_gui PRIVATE
  VSS_GUI_VERSION="${PROJECT_VERSION}"
  # vss::endpoint has a member function called `emit`; Qt's keyword macro
  # would rewrite it to nothing inside the moc translation unit.  Using the
  # Q_* spellings everywhere keeps both worlds intact.
  QT_NO_KEYWORDS)

# Keep a console subsystem: --selftest has to print its diagnostics to the
# caller's terminal (and its exit code drives CI).
set_target_properties(vss_gui PROPERTIES
  WIN32_EXECUTABLE OFF
  OUTPUT_NAME vss_gui)

vss_set_warnings(vss_gui)
