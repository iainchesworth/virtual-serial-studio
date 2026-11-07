//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A deliberately small test framework: one executable, no external
// dependency, deterministic ordering, filtering, JUnit output and skip
// support for hardware dependent cases.  Keeping it in tree means CI needs
// nothing but a compiler and CMake.
//

#ifndef VSS_TEST_FRAMEWORK_HPP
#define VSS_TEST_FRAMEWORK_HPP

#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

// Every test case runs inside a Tracy zone named after the case, so a profile
// of a test run lists each test with its duration and the zones it called. The
// instrumentation is dormant unless the run asks for profiling (VSS_PROFILE=1
// in the environment, or --tracy on the unit and system runners); see
// docs/profiling.md.
#include <vss/core/profile.hpp>
#include <vss/core/profiling.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace vss_test {

struct test_info
{
    const char* name;
    const char* file;
    int line;
    void (*function)();
};

inline std::vector<test_info>& registry()
{
    static std::vector<test_info> tests;
    return tests;
}

struct registrar
{
    registrar(const char* name, const char* file, int line, void (*function)())
    {
        registry().push_back(test_info{name, file, line, function});
    }
};

/// Starts profiling for the whole run when the environment asks for it
/// (VSS_PROFILE=1, VSS_PROFILE_PORT, VSS_PROFILE_WAIT=<seconds> to hold the
/// first test until a profiler is attached). It never starts by itself, not
/// even in Debug, so an ordinary test run opens no port. Being an inline
/// variable it sits in every test executable that includes this header.
class profiling_session
{
public:
    profiling_session()
    {
        const auto request = vss::profiling::request_from_environment(false);
        if (!request.enable)
            return;
        const auto started = vss::profiling::start(request);
        if (started)
            std::fprintf(stderr, "%s\n",
                         vss::profiling::describe(*started).c_str());
        else
            std::fprintf(stderr, "cannot start profiling: %s\n",
                         started.error().message().c_str());
    }

    ~profiling_session() { vss::profiling::shutdown(); }

    profiling_session(const profiling_session&) = delete;
    profiling_session& operator=(const profiling_session&) = delete;
};

inline profiling_session session;

/// Thrown by VSS_SKIP; the runner reports the case as skipped, not failed.
class skip_signal final : public std::exception
{
public:
    explicit skip_signal(std::string reason)
        : reason_(std::move(reason))
    {
    }

    const char* what() const noexcept override { return reason_.c_str(); }
    const std::string& reason() const noexcept { return reason_; }

private:
    std::string reason_;
};

class failure final : public std::exception
{
public:
    explicit failure(std::string message)
        : message_(std::move(message))
    {
    }

    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

template <class T>
std::string to_text(const T& value)
{
    if constexpr (requires(std::ostream& stream) { stream << value; })
    {
        std::ostringstream out;
        out << value;
        return out.str();
    }
    else
    {
        return "<unprintable>";
    }
}

inline std::string to_text(const boost::system::error_code& value)
{
    return std::to_string(value.value()) + " (" + value.message() + ")";
}

template <class T, class E>
std::string to_text(const boost::system::result<T, E>& value)
{
    if (!value)
        return std::string("error(") + to_text(value.error()) + ")";
    if constexpr (std::is_void_v<T>)
        return "success";
    else
        return std::string("success(") + to_text(*value) + ")";
}

inline std::string to_text(const std::exception& value)
{
    return value.what();
}

[[noreturn]] inline void raise(const char* expression, const char* file,
                               int line)
{
    std::ostringstream out;
    out << file << ':' << line << ": requirement failed: " << expression;
    throw failure(out.str());
}

/// True for the integer types std::cmp_equal accepts: not bool, not a
/// character type.
template <class T>
inline constexpr bool is_comparable_integer_v =
    std::is_integral_v<T> && !std::is_same_v<T, bool> &&
    !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> &&
    !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> &&
    !std::is_same_v<T, char32_t>;

/// Equality that is safe between signed and unsigned integers, so a test can
/// compare an identifier with a literal without a sign conversion warning.
template <class Left, class Right>
constexpr bool values_equal(const Left& left, const Right& right)
{
    if constexpr (is_comparable_integer_v<Left> &&
                  is_comparable_integer_v<Right>)
        return std::cmp_equal(left, right);
    else
        return left == right;
}

template <class Left, class Right>
void check_equal(const Left& left, const Right& right, const char* left_text,
                 const char* right_text, const char* file, int line)
{
    if (values_equal(left, right))
        return;

    std::ostringstream out;
    out << file << ':' << line << ": expected " << left_text << " == "
        << right_text << "\n  left  : " << to_text(left) << "\n  right : "
        << to_text(right);
    throw failure(out.str());
}

template <class Left, class Right>
void check_not_equal(const Left& left, const Right& right,
                     const char* left_text, const char* right_text,
                     const char* file, int line)
{
    if (!values_equal(left, right))
        return;

    std::ostringstream out;
    out << file << ':' << line << ": expected " << left_text << " != "
        << right_text << ", both are " << to_text(left);
    throw failure(out.str());
}

template <class First, class Second>
void check_near(const First& first, const Second& second, double tolerance,
                const char* first_text, const char* second_text,
                const char* file, int line)
{
    const double difference = static_cast<double>(first)
        - static_cast<double>(second);
    const double magnitude = difference < 0 ? -difference : difference;
    if (magnitude <= tolerance)
        return;

    std::ostringstream out;
    out << file << ':' << line << ": expected " << first_text << " ~= "
        << second_text << " within " << tolerance << ", got "
        << to_text(first) << " and " << to_text(second);
    throw failure(out.str());
}

} // namespace vss_test

#define VSS_DETAIL_CONCAT_INNER(a, b) a##b
#define VSS_DETAIL_CONCAT(a, b) VSS_DETAIL_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                        \
    static void VSS_DETAIL_CONCAT(vss_test_body_, __LINE__)();                 \
    static void VSS_DETAIL_CONCAT(vss_test_function_, __LINE__)()              \
    {                                                                          \
        VSS_PROFILE_ZONE(name);                                                \
        VSS_DETAIL_CONCAT(vss_test_body_, __LINE__)();                         \
    }                                                                          \
    static ::vss_test::registrar VSS_DETAIL_CONCAT(vss_test_registrar_,        \
                                                   __LINE__)(                  \
        name, __FILE__, __LINE__,                                              \
        &VSS_DETAIL_CONCAT(vss_test_function_, __LINE__));                     \
    static void VSS_DETAIL_CONCAT(vss_test_body_, __LINE__)()

#define REQUIRE(expression)                                                 \
    do                                                                      \
    {                                                                       \
        if (!(expression))                                                  \
            ::vss_test::raise(#expression, __FILE__, __LINE__);             \
    } while (false)

#define REQUIRE_EQ(left, right)                                             \
    ::vss_test::check_equal((left), (right), #left, #right, __FILE__,       \
                            __LINE__)

#define REQUIRE_NE(left, right)                                             \
    ::vss_test::check_not_equal((left), (right), #left, #right, __FILE__,   \
                                __LINE__)

#define REQUIRE_NEAR(first, second, tolerance)                               \
    ::vss_test::check_near((first), (second), (tolerance), #first, #second,  \
                           __FILE__, __LINE__)

#define REQUIRE_THROWS(expression)                                          \
    do                                                                      \
    {                                                                       \
        bool vss_threw = false;                                             \
        try                                                                 \
        {                                                                   \
            static_cast<void>(expression);                                  \
        }                                                                   \
        catch (...)                                                         \
        {                                                                   \
            vss_threw = true;                                               \
        }                                                                   \
        if (!vss_threw)                                                     \
            ::vss_test::raise("expected an exception: " #expression,        \
                              __FILE__, __LINE__);                          \
    } while (false)

#define REQUIRE_THROWS_AS(expression, type)                                 \
    do                                                                      \
    {                                                                       \
        bool vss_threw_expected = false;                                    \
        try                                                                 \
        {                                                                   \
            static_cast<void>(expression);                                  \
        }                                                                   \
        catch (const type&)                                                 \
        {                                                                   \
            vss_threw_expected = true;                                      \
        }                                                                   \
        catch (...)                                                         \
        {                                                                   \
        }                                                                   \
        if (!vss_threw_expected)                                            \
            ::vss_test::raise("expected " #type ": " #expression, __FILE__, \
                              __LINE__);                                    \
    } while (false)

#define REQUIRE_NOTHROW(expression)                                         \
    do                                                                      \
    {                                                                       \
        try                                                                 \
        {                                                                   \
            static_cast<void>(expression);                                  \
        }                                                                   \
        catch (const std::exception& vss_error)                             \
        {                                                                   \
            ::vss_test::raise(                                              \
                std::string("unexpected exception: ") + vss_error.what(),   \
                __FILE__, __LINE__);                                        \
        }                                                                   \
    } while (false)

#define VSS_SKIP(reason) throw ::vss_test::skip_signal(reason)

#endif // VSS_TEST_FRAMEWORK_HPP
