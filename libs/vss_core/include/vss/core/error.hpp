//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_ERROR_HPP
#define VSS_CORE_ERROR_HPP

#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <type_traits>

namespace vss {

/// Application level error values.
///
/// Transport failures keep their `boost::system` value (the serial library's
/// own category, `boost::asio::error` and the OS category); these values
/// describe mistakes in the topology, the configuration or the API contract.
enum class errc
{
    success = 0,
    not_found,
    already_exists,
    invalid_argument,
    invalid_uri,
    unsupported,
    not_open,
    already_open,
    cycle_detected,
    queue_full,
    timeout,
    canceled,
    io_error,
    permission_denied,
    config_error,
    device_busy,
    port_exhausted,
    internal
};

const boost::system::error_category& error_category() noexcept;

boost::system::error_code make_error_code(errc value) noexcept;

using error_code = boost::system::error_code;

/// `result<T>` keeps the error path explicit without exceptions.
template <class T>
using result = boost::system::result<T>;

} // namespace vss

namespace boost {
namespace system {

template <>
struct is_error_code_enum<vss::errc> : std::true_type
{
};

} // namespace system
} // namespace boost

#endif // VSS_CORE_ERROR_HPP
