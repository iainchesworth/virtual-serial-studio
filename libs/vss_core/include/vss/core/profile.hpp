//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_PROFILE_HPP
#define VSS_CORE_PROFILE_HPP

#include <cstdint>

/// Instrumentation seam.
///
/// `VSS_TRACY` pulls in Tracy and turns every macro into a real zone. Without
/// it the macros compile to nothing, so production builds carry no cost and
/// the headers stay dependency free.
///
/// Zone names must be string literals: Tracy stores the pointer, not a copy.
#if defined(VSS_TRACY)

// vcpkg installs the upstream tree under include/tracy/tracy; a source build
// puts Tracy.hpp on the include path directly.
#  if defined(__has_include) && __has_include(<tracy/Tracy.hpp>)
#    include <tracy/Tracy.hpp>
#  else
#    include <Tracy.hpp>
#  endif

#  define VSS_PROFILE_ZONE(name) ZoneScopedN(name)
#  define VSS_PROFILE_FRAME() FrameMark
#  define VSS_PROFILE_PLOT(name, value) \
      TracyPlot(name, static_cast<std::int64_t>(value))
#  define VSS_PROFILE_MESSAGE(text) TracyMessageL(text)
#  define VSS_PROFILE_THREAD(name) ::tracy::SetThreadName(name)

#else

#  define VSS_PROFILE_ZONE(name) ((void)0)
#  define VSS_PROFILE_FRAME() ((void)0)
#  define VSS_PROFILE_PLOT(name, value) ((void)0)
#  define VSS_PROFILE_MESSAGE(text) ((void)0)
#  define VSS_PROFILE_THREAD(name) ((void)0)

#endif

#endif // VSS_CORE_PROFILE_HPP
