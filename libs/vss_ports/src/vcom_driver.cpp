//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/vcom_driver.hpp>

#if defined(_WIN32)
#  include <windows.h>

#  include <string>
#endif

namespace vss {
namespace {

#if defined(_WIN32)
// The INF for the filter driver writes this key when it installs.  Its
// presence means the control device and the serial PDO factory are available.
constexpr wchar_t driver_key[] = L"SOFTWARE\\Virtual Serial Studio\\Driver";

std::string to_utf8(const wchar_t* text)
{
    if (!text || !*text)
        return {};

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                           nullptr, nullptr);
    if (size <= 1)
        return {};

    std::string out(static_cast<std::size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr,
                          nullptr);
    return out;
}
#endif

} // namespace

bool driver_available()
{
#if defined(_WIN32)
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, driver_key, 0, KEY_READ, &key)
        != ERROR_SUCCESS)
        return false;
    ::RegCloseKey(key);
    return true;
#else
    return false;
#endif
}

std::string driver_version()
{
#if defined(_WIN32)
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, driver_key, 0, KEY_READ, &key)
        != ERROR_SUCCESS)
        return {};

    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG status = ::RegQueryValueExW(
        key, L"Version", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return to_utf8(buffer);
#else
    return {};
#endif
}

result<virtual_port_result> create_driver_port(
    const transport::executor_type&, const std::string&)
{
    // The driver client (control device IOCTL + shared ring transport) is the
    // next milestone.  Reporting unsupported rather than failing later keeps
    // the auto-selection path honest.
    return make_error_code(errc::unsupported);
}

} // namespace vss
