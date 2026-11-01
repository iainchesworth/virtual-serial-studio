# SPDX-License-Identifier: GPL-3.0-or-later
#
# Saved topology documents (JSON), validated and round-trippable.
#

add_library(vss_boost_json STATIC src/boost_json_impl.cpp)
add_library(Vss::BoostJson ALIAS vss_boost_json)
target_link_libraries(vss_boost_json PUBLIC Vss::Boost)
# Header-only mode: do not try to auto-link the compiled Boost.JSON library.
target_compile_definitions(vss_boost_json PUBLIC BOOST_JSON_NO_LIB)
target_include_directories(vss_boost_json PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

add_library(vss_config src/document.cpp)
add_library(Vss::config ALIAS vss_config)

target_include_directories(vss_config PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(vss_config PUBLIC cxx_std_23)
target_link_libraries(vss_config PUBLIC vss_core Vss::Boost Vss::BoostJson)

vss_set_warnings(vss_config)
vss_enable_coverage(vss_config)
vss_mark_coverage_subject(vss_config)
