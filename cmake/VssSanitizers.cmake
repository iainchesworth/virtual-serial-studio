# SPDX-License-Identifier: GPL-3.0-or-later
#
# Optional AddressSanitizer / UndefinedBehaviorSanitizer instrumentation for
# the test binaries.  MSVC does not support these flags; the option is a
# no-op there with an explanatory warning.
#

include_guard(GLOBAL)

function(vss_enable_sanitizers target)
  if(NOT VSS_SANITIZE)
    return()
  endif()

  if(MSVC)
    message(WARNING
      "Vss: VSS_SANITIZE is not supported by MSVC; use clang-cl or the "
      "GCC/Clang toolchains.")
    return()
  endif()

  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
