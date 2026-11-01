//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/error.hpp>

#include <string>

namespace vss {
namespace {

class error_category_impl final : public boost::system::error_category
{
public:
    const char* name() const noexcept override { return "vss"; }

    std::string message(int value) const override
    {
        switch (static_cast<errc>(value))
        {
        case errc::success: return "success";
        case errc::not_found: return "not found";
        case errc::already_exists: return "already exists";
        case errc::invalid_argument: return "invalid argument";
        case errc::invalid_uri: return "invalid URI";
        case errc::unsupported: return "unsupported by this transport";
        case errc::not_open: return "endpoint is not open";
        case errc::already_open: return "endpoint is already open";
        case errc::cycle_detected: return "link would create a cycle";
        case errc::queue_full: return "write queue is full";
        case errc::timeout: return "operation timed out";
        case errc::canceled: return "operation canceled";
        case errc::io_error: return "I/O error";
        case errc::permission_denied: return "permission denied";
        case errc::config_error: return "configuration error";
        case errc::device_busy: return "device is busy";
        case errc::port_exhausted: return "no ports of this kind are available";
        case errc::internal: return "internal error";
        }
        return "unknown vss error";
    }
};

} // namespace

const boost::system::error_category& error_category() noexcept
{
    static const error_category_impl instance;
    return instance;
}

boost::system::error_code make_error_code(errc value) noexcept
{
    return {static_cast<int>(value), error_category()};
}

} // namespace vss
