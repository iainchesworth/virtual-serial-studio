//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_PROFILE_HPP
#define VSS_CORE_PROFILE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/// Instrumentation hooks.
///
/// `VSS_TRACY` pulls in Tracy and turns every macro into a real zone. Without
/// it the macros compile to nothing, so a build without the option carries no
/// cost and the headers stay dependency free.
///
/// With `VSS_TRACY` the instrumentation is compiled in but dormant: Tracy is
/// built with a manual lifetime, so no profiler thread and no socket exist
/// until the application calls `vss::profiling::enable()` (see profiling.hpp).
/// Until then every macro below is one relaxed load of a flag and a branch.
///
/// Zone, plot, frame and message names must be string literals: Tracy keeps
/// the pointer, not a copy.
///
/// Naming: `area.thing`, lower case, for example `hub.route`, `gui.paint`,
/// `ports.tcp.accept`. The area is the first word of a zone name and the
/// profiling report groups on it.

namespace vss::profiling
{

/// The compile-time switch, for code that wants to say so.
#if defined(VSS_TRACY)
inline constexpr bool compiled_in = true;
#else
inline constexpr bool compiled_in = false;
#endif

} // namespace vss::profiling

#if defined(VSS_TRACY)

// cmake/VssTracy.cmake links the Tracy client target, whose include directory
// makes this path resolve both for the vcpkg layout (include/tracy/tracy) and
// for the upstream tree (public/tracy).
#include <tracy/Tracy.hpp>

namespace vss::profiling::detail
{

/// True while recording is switched on. Written by `enable`/`disable`.
inline std::atomic<bool> armed{false};

/// How many instrumented sections are inside Tracy right now, per shard so
/// threads do not fight over one cache line. `shutdown` waits for it to reach
/// zero before it lets Tracy free its queues.
inline constexpr std::size_t shard_count = 16;

// Padded to a cache line by hand: alignas would trip MSVC's C4324.
struct shard
{
    std::atomic<std::int32_t> inside{0};
    std::byte padding[64 - sizeof(std::atomic<std::int32_t>)]{};
};

inline shard shards[shard_count];

/// Names a thread that asked for a name while the profiler was dormant, once
/// recording has started. Defined in profiling.cpp.
void name_current_thread() noexcept;

/// Bumped by `enable` so every thread renames itself once per start.
inline std::atomic<std::uint32_t> epoch{1};

inline thread_local std::uint32_t named_epoch = 0;

[[nodiscard]] inline std::size_t shard_index() noexcept
{
    static thread_local const char marker = 0;
    return (reinterpret_cast<std::uintptr_t>(&marker) >> 6) % shard_count;
}

/// Keeps `shutdown` from freeing Tracy's state while a thread is inside it.
class section
{
public:
    section() noexcept
    {
        if (!armed.load(std::memory_order_relaxed))
        {
            return;
        }
        const auto index = shard_index();
        shards[index].inside.fetch_add(1, std::memory_order_seq_cst);
        if (!armed.load(std::memory_order_seq_cst))
        {
            shards[index].inside.fetch_sub(1, std::memory_order_release);
            return;
        }
        shard_ = static_cast<std::int8_t>(index);
        if (named_epoch != epoch.load(std::memory_order_relaxed))
        {
            name_current_thread();
        }
    }

    ~section()
    {
        if (shard_ >= 0)
        {
            shards[static_cast<std::size_t>(shard_)].inside.fetch_sub(
                1, std::memory_order_release);
        }
    }

    section(const section&) = delete;
    section& operator=(const section&) = delete;

    [[nodiscard]] bool active() const noexcept { return shard_ >= 0; }

private:
    std::int8_t shard_{-1};
};

} // namespace vss::profiling::detail

namespace vss::profiling
{

/// A Tracy zone that costs one relaxed load while profiling is off.
class scoped_zone
{
public:
    explicit scoped_zone(const tracy::SourceLocationData* location) noexcept
        : zone_(location, TRACY_CALLSTACK, section_.active())
    {
    }

    /// Attaches text to the zone. Only call it when `active()` is true, so the
    /// text is not built for nothing.
    void text(std::string_view value) noexcept
    {
        zone_.Text(value.data(), value.size());
    }

    /// Renames this instance of the zone, so the report groups it under the
    /// new name. The text is copied. Only call it when `active()` is true.
    void name(std::string_view value) noexcept
    {
        zone_.Name(value.data(), value.size());
    }

    void value(std::uint64_t number) noexcept { zone_.Value(number); }

    [[nodiscard]] bool active() const noexcept { return section_.active(); }

private:
    detail::section section_;
    tracy::ScopedZone zone_;
};

} // namespace vss::profiling

namespace vss::profiling
{

/// A count that moves whether or not anyone is recording, for a queue depth
/// that is plotted: it stays balanced when recording starts part way through.
/// An instrumented build pays one atomic add per `up` or `down`; a build
/// without Tracy pays nothing.
class depth_counter
{
public:
    std::size_t up() noexcept { return n_.fetch_add(1) + 1; }
    std::size_t down() noexcept { return n_.fetch_sub(1) - 1; }

private:
    std::atomic<std::size_t> n_{0};
};

} // namespace vss::profiling

#define VSS_PROFILE_JOIN_(a, b) VSS_PROFILE_JOIN2_(a, b)
#define VSS_PROFILE_JOIN2_(a, b) a##b

#define VSS_PROFILE_ZONE_NAMED(var, name)                                      \
    static constexpr tracy::SourceLocationData VSS_PROFILE_JOIN_(              \
        var, _location){name, TracyFunction, TracyFile,                        \
                        static_cast<std::uint32_t>(__LINE__), 0};              \
    ::vss::profiling::scoped_zone var                                          \
    {                                                                          \
        &VSS_PROFILE_JOIN_(var, _location)                                     \
    }

#define VSS_PROFILE_ZONE(name)                                                 \
    VSS_PROFILE_ZONE_NAMED(VSS_PROFILE_JOIN_(vss_zone_, __LINE__), name)

/// Text for a named zone, evaluated only while the zone is recording.
#define VSS_PROFILE_ZONE_TEXT(var, content)                                    \
    do                                                                         \
    {                                                                          \
        if ((var).active())                                                    \
        {                                                                      \
            (var).text(content);                                               \
        }                                                                      \
    } while (false)

/// Names a zone at run time, evaluated only while the zone is recording.
#define VSS_PROFILE_ZONE_NAME(var, content)                                    \
    do                                                                         \
    {                                                                          \
        if ((var).active())                                                    \
        {                                                                      \
            (var).name(content);                                               \
        }                                                                      \
    } while (false)

#define VSS_PROFILE_ZONE_VALUE(var, number)                                    \
    do                                                                         \
    {                                                                          \
        if ((var).active())                                                    \
        {                                                                      \
            (var).value(static_cast<std::uint64_t>(number));                   \
        }                                                                      \
    } while (false)

/// Runs `statement` inside a guard so Tracy is only touched while recording.
#define VSS_PROFILE_GUARDED_(statement)                                        \
    do                                                                         \
    {                                                                          \
        if (::vss::profiling::detail::armed.load(std::memory_order_relaxed))   \
        {                                                                      \
            const ::vss::profiling::detail::section vss_section_;              \
            if (vss_section_.active())                                         \
            {                                                                  \
                statement;                                                     \
            }                                                                  \
        }                                                                      \
    } while (false)

#define VSS_PROFILE_FRAME() VSS_PROFILE_GUARDED_(FrameMark)
#define VSS_PROFILE_FRAME_NAMED(name) VSS_PROFILE_GUARDED_(FrameMarkNamed(name))
#define VSS_PROFILE_FRAME_START(name) VSS_PROFILE_GUARDED_(FrameMarkStart(name))
#define VSS_PROFILE_FRAME_END(name) VSS_PROFILE_GUARDED_(FrameMarkEnd(name))
#define VSS_PROFILE_PLOT(name, value)                                          \
    VSS_PROFILE_GUARDED_(TracyPlot(name, static_cast<std::int64_t>(value)))
#define VSS_PROFILE_PLOT_F(name, value)                                        \
    VSS_PROFILE_GUARDED_(TracyPlot(name, static_cast<double>(value)))
#define VSS_PROFILE_MESSAGE(text) VSS_PROFILE_GUARDED_(TracyMessageL(text))
/// A message whose text is built at run time; `content` is a string
/// expression (copied), evaluated only while recording.
#define VSS_PROFILE_MESSAGE_TEXT(content)                                      \
    VSS_PROFILE_GUARDED_(const std::string vss_text_{(content)};               \
                         TracyMessage(vss_text_.data(), vss_text_.size()))
/// Names the calling thread. It takes effect when recording starts, and again
/// on every restart, so call it first thing in a thread.
#define VSS_PROFILE_THREAD(name) ::vss::profiling::name_thread(name)

namespace vss::profiling
{

/// Remembers `name` for the calling thread and applies it now when recording.
/// `name` must outlive the thread (a literal).
void name_thread(const char* name) noexcept;

} // namespace vss::profiling

#else

namespace vss::profiling
{

/// Stands in for a zone when Tracy is compiled out.
class scoped_zone
{
public:
    constexpr void text(std::string_view) const noexcept {}
    constexpr void name(std::string_view) const noexcept {}
    constexpr void value(std::uint64_t) const noexcept {}
    [[nodiscard]] constexpr bool active() const noexcept { return false; }
};

class depth_counter
{
public:
    constexpr std::size_t up() const noexcept { return 0; }
    constexpr std::size_t down() const noexcept { return 0; }
};

} // namespace vss::profiling

#define VSS_PROFILE_ZONE(name) ((void)0)
#define VSS_PROFILE_ZONE_NAMED(var, name)                                      \
    constexpr ::vss::profiling::scoped_zone var {}
#define VSS_PROFILE_ZONE_TEXT(var, content) ((void)(var))
#define VSS_PROFILE_ZONE_NAME(var, content) ((void)(var))
#define VSS_PROFILE_ZONE_VALUE(var, number) ((void)(var))
#define VSS_PROFILE_FRAME() ((void)0)
#define VSS_PROFILE_FRAME_NAMED(name) ((void)0)
#define VSS_PROFILE_FRAME_START(name) ((void)0)
#define VSS_PROFILE_FRAME_END(name) ((void)0)
#define VSS_PROFILE_PLOT(name, value) ((void)0)
#define VSS_PROFILE_PLOT_F(name, value) ((void)0)
#define VSS_PROFILE_MESSAGE(text) ((void)0)
#define VSS_PROFILE_MESSAGE_TEXT(content) ((void)0)
#define VSS_PROFILE_THREAD(name) ((void)0)

#endif

#endif // VSS_CORE_PROFILE_HPP
