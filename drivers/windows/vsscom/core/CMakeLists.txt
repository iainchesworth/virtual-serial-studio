# SPDX-License-Identifier: GPL-3.0-or-later
#
# The serial rules of the Windows virtual COM port driver, built for the host
# so that the unit tests can check them. The driver itself is built by the
# Windows driver kit, not by this file.
#

add_library(vsscom_core STATIC vsscom_core.c)
target_include_directories(vsscom_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vsscom_core PUBLIC c_std_11)
vss_set_warnings(vsscom_core)
