# SPDX-License-Identifier: GPL-3.0-or-later
#
# Virtual Serial Studio - Qt 6 desktop GUI.
#
# The widgets application is a thin presentation layer over vss::studio: all
# topology, transport and configuration work stays in the libraries, the GUI
# snapshots the studio on a timer and never touches widgets from hub threads.
#

find_package(Qt6 6.2 QUIET COMPONENTS Widgets Svg Network)
if(NOT Qt6_FOUND)
  if(VSS_GUI_REQUIRED)
    message(FATAL_ERROR
      "Vss: the GUI was requested but Qt 6 with the Widgets, Svg and Network modules "
      "was not found. It was searched for in CMAKE_PREFIX_PATH, VSS_QT_ROOT, "
      "ICL_QT_ROOT, QT_ROOT_DIR, QTDIR and the usual install locations. Set "
      "VSS_QT_ROOT or CMAKE_PREFIX_PATH to a Qt 6 installation that includes "
      "Qt6::Svg, or configure with -DVSS_GUI_REQUIRED=OFF "
      "-DVSS_BUILD_GUI=OFF for a command-line-only build.")
  endif()
  message(STATUS "Vss: Qt 6 not found in CMAKE_PREFIX_PATH, VSS_QT_ROOT or the "
                 "usual install locations; the GUI target is skipped. Use a "
                 "*-gui preset to make this an error.")
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

# Build identity for the About window and for `environment.txt` in the
# diagnostic bundle.  Resolved before the support target so its compile
# definitions can carry the values.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE VSS_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE VSS_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT VSS_GIT_COMMIT)
  set(VSS_GIT_COMMIT "unknown")
endif()
if(NOT VSS_GIT_BRANCH)
  set(VSS_GIT_BRANCH "unknown")
endif()
string(TIMESTAMP VSS_BUILD_DATE "%Y-%m-%d")
set(VSS_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
set(VSS_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# The adapter is a separate library so the backend tests can link it without
# Qt Widgets.
add_subdirectory(src/backend)

# Tracy: the instrumented QApplication and the profiling switch.
add_subdirectory(src/profiling)

# Design tokens, resources, icons and the focus style.
add_subdirectory(src/theme)

# Shared widgets built from those tokens.
add_subdirectory(src/widgets)

# Ports, Inspector, Send and Monitor.
add_subdirectory(src/panes)

# The topology dock: nodes, links, layout and the accessible links list.
add_subdirectory(src/topology)

# The Advanced tools: owner, wiring and faults, fixtures, JSON, benchmark.
add_subdirectory(src/advanced)

# Which catalogue the system's languages select, and the layout direction.
add_subdirectory(src/language)

# Support and system surfaces: setup check, troubleshooting, bundle,
# settings, About, shortcuts.
add_subdirectory(src/support)

# Connect, share, network and template dialogs (WP6).
add_subdirectory(src/dialogs)

# Basic flows: add, pair, attach and watch (WP5).
add_subdirectory(src/flows)

# The redesigned window frame.
add_subdirectory(src/shell)

add_executable(vss_gui
  main.cpp)

target_compile_features(vss_gui PRIVATE cxx_std_23)
target_link_libraries(vss_gui PRIVATE
  Vss::studio
  Qt6::Widgets
  vss_gui_backend
  vss_gui_profiling
  vss_gui_theme
  vss_gui_widgets
  vss_gui_panes
  vss_gui_dialogs
  vss_gui_flows
  vss_gui_support
  vss_gui_language
  vss_gui_shell)

target_compile_definitions(vss_gui PRIVATE
  VSS_GIT_COMMIT="${VSS_GIT_COMMIT}"
  VSS_GIT_BRANCH="${VSS_GIT_BRANCH}"
  VSS_BUILD_DATE="${VSS_BUILD_DATE}"
  VSS_BUILD_TYPE="${VSS_BUILD_TYPE}"
  VSS_COMPILER="${VSS_COMPILER}"
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

# The packaged program. The deploy script copies the Qt libraries and plugins
# next to the program (and builds the bundle on macOS), so the installed tree
# runs on a machine without Qt.
if(VSS_INSTALL)
  set_target_properties(vss_gui PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iainchesworthlabs.virtualserialstudio"
    MACOSX_BUNDLE_BUNDLE_NAME "Virtual Serial Studio"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Virtual Serial Studio contributors. GPL-3.0-or-later."
    # The default template plus NSLocalNetworkUsageDescription, which macOS 15
    # needs before it asks the user whether the app may use the local network.
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in")

  # The deploy script puts the Qt libraries in lib/, beside bin/. On macOS
  # macdeployqt copies the frameworks into the bundle, and it finds them through
  # the program's run path, so the installed program keeps the directories it
  # was linked from (INSTALL_RPATH_USE_LINK_PATH) until macdeployqt has run.
  if(APPLE)
    set_target_properties(vss_gui PROPERTIES
      INSTALL_RPATH "@executable_path/../Frameworks"
      INSTALL_RPATH_USE_LINK_PATH ON)
  elseif(UNIX)
    set_target_properties(vss_gui PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
  endif()

  install(TARGETS vss_gui
    BUNDLE DESTINATION . COMPONENT runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)

  qt_generate_deploy_app_script(
    TARGET vss_gui
    OUTPUT_SCRIPT vss_gui_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    NO_TRANSLATIONS)
  install(SCRIPT "${vss_gui_deploy_script}" COMPONENT runtime)
endif()

# Translations. English is the source language; every other language of the
# standard has a catalogue translations/vss_gui_<code>.ts, which lrelease turns
# into a .qm file inside the program (:/i18n/vss_gui_<code>.qm). The
# `update_translations` target refreshes the .ts files from the sources, and
# `release_translations` builds the .qm files. See docs/i18n.md.
find_package(Qt6 6.2 QUIET COMPONENTS LinguistTools)
file(GLOB VSS_GUI_TS_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/translations/vss_gui_*.ts")
if(VSS_GUI_TS_FILES)
  if(NOT TARGET Qt6::lupdate)
    message(FATAL_ERROR
      "Vss: translations/ holds catalogues but Qt6 LinguistTools was not "
      "found. Install the Qt Linguist tools with the Qt kit.")
  endif()
  file(GLOB_RECURSE VSS_GUI_TRANSLATABLE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
  list(FILTER VSS_GUI_TRANSLATABLE EXCLUDE REGEX "/tests/")
  qt_add_translations(vss_gui
    TS_FILES ${VSS_GUI_TS_FILES}
    SOURCES ${VSS_GUI_TRANSLATABLE}
    RESOURCE_PREFIX "/i18n")
endif()

# Journey tests drive the real window with QTest key and mouse events; they
# need the Qt Test module on top of Widgets.
if(VSS_BUILD_TESTS)
  find_package(Qt6 6.2 QUIET COMPONENTS Test)
  if(TARGET Qt6::Test)
    add_subdirectory(ui-tests)
  else()
    message(STATUS "Vss: Qt6::Test not found; the GUI journey tests are skipped.")
  endif()
endif()
