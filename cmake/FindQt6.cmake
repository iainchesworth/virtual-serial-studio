# SPDX-License-Identifier: GPL-3.0-or-later
#
# FindQt6.cmake - wrapper picked up automatically by find_package(Qt6 ...)
# because cmake/ is on CMAKE_MODULE_PATH (CMake tries module mode before
# config mode).
#
# It never replaces Qt6Config.cmake. It first lets Qt's own config package do
# the work, so an explicit -DCMAKE_PREFIX_PATH=... or -DQt6_DIR=... always
# wins. Only when that fails does it look in the aqtinstall-style locations
# used by the other checkouts (C:/Qt, C:/Repos/common/qt, ~/Qt, /opt/Qt,
# /usr/local/Qt) and re-run the search with the detected kit added to
# CMAKE_PREFIX_PATH.

find_package(Qt6 ${Qt6_FIND_VERSION} CONFIG QUIET
             COMPONENTS ${Qt6_FIND_COMPONENTS})

if(NOT Qt6_FOUND)
    # QT_ROOT first: it is what the docs and presets tell people to set.
    set(_vss_qt_roots
        "$ENV{QT_ROOT}"
        "$ENV{QTDIR}"
        "C:/Qt"
        "C:/Repos/common/qt"
        "$ENV{HOME}/Qt"
        "/opt/Qt"
        "/usr/local/Qt")

    # The kit directory a desktop Qt for this compiler uses.
    if(MSVC)
        set(_vss_qt_arches msvc2022_64 msvc2019_64 msvc2017_64)
    elseif(MINGW)
        set(_vss_qt_arches mingw_64 mingw81_64)
    elseif(APPLE)
        set(_vss_qt_arches macos)
    else()
        set(_vss_qt_arches gcc_64)
    endif()

    set(_vss_qt_candidates "")
    foreach(root IN LISTS _vss_qt_roots)
        if(NOT root OR NOT IS_DIRECTORY "${root}")
            continue()
        endif()

        file(GLOB _vss_qt_version_dirs LIST_DIRECTORIES true "${root}/*")
        foreach(version_dir IN LISTS _vss_qt_version_dirs)
            if(NOT IS_DIRECTORY "${version_dir}")
                continue()
            endif()
            foreach(arch IN LISTS _vss_qt_arches)
                if(EXISTS "${version_dir}/${arch}/lib/cmake/Qt6/Qt6Config.cmake")
                    list(APPEND _vss_qt_candidates "${version_dir}/${arch}")
                endif()
            endforeach()
            # A root may itself be a kit directory (e.g. QT_ROOT).
            if(EXISTS "${version_dir}/lib/cmake/Qt6/Qt6Config.cmake")
                list(APPEND _vss_qt_candidates "${version_dir}")
            endif()
        endforeach()

        # System-wide installs (apt's qt6-base-dev) ship directly under root.
        if(EXISTS "${root}/lib/cmake/Qt6/Qt6Config.cmake")
            list(APPEND _vss_qt_candidates "${root}")
        endif()
    endforeach()

    if(_vss_qt_candidates)
        list(SORT _vss_qt_candidates COMPARE NATURAL)
        list(REVERSE _vss_qt_candidates)
        list(GET _vss_qt_candidates 0 _vss_qt_chosen)

        message(STATUS
            "FindQt6: auto-detected Qt6 at ${_vss_qt_chosen} "
            "(pass -DCMAKE_PREFIX_PATH or QT_ROOT to override)")
        list(APPEND CMAKE_PREFIX_PATH "${_vss_qt_chosen}")

        set(_vss_qt_args CONFIG)
        if(Qt6_FIND_QUIETLY)
            list(APPEND _vss_qt_args QUIET)
        endif()
        if(Qt6_FIND_REQUIRED)
            list(APPEND _vss_qt_args REQUIRED)
        endif()
        if(Qt6_FIND_COMPONENTS)
            list(APPEND _vss_qt_args COMPONENTS ${Qt6_FIND_COMPONENTS})
        endif()

        find_package(Qt6 ${Qt6_FIND_VERSION} ${_vss_qt_args})

        unset(_vss_qt_args)
        unset(_vss_qt_chosen)
    endif()

    unset(_vss_qt_roots)
    unset(_vss_qt_arches)
    unset(_vss_qt_candidates)
    unset(_vss_qt_version_dirs)
endif()

# A find module has to report failure itself: unlike config mode, CMake does
# not turn "not found" into an error for REQUIRED.
if(NOT Qt6_FOUND AND Qt6_FIND_REQUIRED)
    message(FATAL_ERROR
        "Could not find Qt6 (>= ${Qt6_FIND_VERSION}) and no prebuilt Qt was "
        "found in QT_ROOT, CMAKE_PREFIX_PATH or the usual install locations "
        "(C:/Qt, C:/Repos/common/qt, ~/Qt, /opt/Qt, /usr/local/Qt). Pass "
        "-DCMAKE_PREFIX_PATH=<Qt install>/<kit> (for example "
        "C:/Qt/6.10.3/msvc2022_64), set QT_ROOT, or configure with "
        "-DVSS_GUI_REQUIRED=OFF -DVSS_BUILD_GUI=OFF for a build without the "
        "GUI.")
endif()
