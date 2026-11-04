# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sanitizers. The shared IclSanitizers module (cmake/icl) does the work: set
# ICL_SANITIZERS to address,undefined or thread, as the linux-asan-ubsan and
# linux-tsan presets do. It builds at -O1 with frame pointers and stops at the
# first finding. MSVC supports only the address sanitizer.
#
# A sanitizer build is not combined with coverage or Tracy, whose
# instrumentation changes what the sanitizer sees.

include_guard(GLOBAL)

if(ICL_SANITIZERS AND (VSS_COVERAGE OR VSS_TRACY))
  message(FATAL_ERROR
    "ICL_SANITIZERS='${ICL_SANITIZERS}' cannot be combined with VSS_COVERAGE or "
    "VSS_TRACY. Use a separate build directory for each.")
endif()

include(IclSanitizers)
