# SPDX-License-Identifier: GPL-3.0-or-later
#
# vss_cli - headless access to the same studio the GUI drives.
#

add_executable(vss_cli main.cpp)
target_compile_features(vss_cli PRIVATE cxx_std_23)
target_link_libraries(vss_cli PRIVATE Vss::studio vss_testkit)
vss_set_warnings(vss_cli)

install(TARGETS vss_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
