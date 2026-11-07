//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/profile.hpp>

#include "../com0com_platform.hpp"

#include <vss/ports/serial_enumeration.hpp>

#include <windows.h>

#include <shellapi.h>

#include <string>

namespace vss::detail
{

namespace
{

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = ::MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                          static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string read_install_dir()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\com0com",
                        0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\com0com", 0,
                            KEY_READ, &key) != ERROR_SUCCESS)
            return {};
    }

    wchar_t buffer[MAX_PATH * 2]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG status =
        ::RegQueryValueExW(key, L"Install_Dir", nullptr, &type,
                           reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};

    std::wstring value(buffer);
    if (!value.empty() && value.back() == L'\\')
        value.pop_back();

    const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                          static_cast<int>(value.size()), out.data(), length,
                          nullptr, nullptr);
    return out;
}

bool is_elevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &size);
    ::CloseHandle(token);
    return ok != FALSE && elevation.TokenIsElevated != 0;
}

result<void> run_setupc(const com0com_installation& installation,
                        const std::string& arguments, bool request_elevation)
{
    if (!installation.found)
        return make_error_code(errc::unsupported);

    // Never hand a missing binary to the shell: ShellExecuteEx blocks with an
    // error dialog when the target cannot be found, which is fatal in a
    // service or CI context.
    const DWORD attributes =
        ::GetFileAttributesA(installation.setupc_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return make_error_code(errc::not_found);

    if (!request_elevation && !is_elevated())
        return make_error_code(errc::permission_denied);

    const std::wstring file = widen(installation.setupc_path);
    const std::wstring args = widen(arguments);

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = request_elevation ? L"runas" : L"open";
    info.lpFile = file.c_str();
    info.lpParameters = args.c_str();
    info.lpDirectory = nullptr;
    info.nShow = SW_HIDE;

    if (!::ShellExecuteExW(&info))
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_CANCELLED)
            return make_error_code(errc::canceled);
        return error_code(static_cast<int>(error),
                          boost::system::system_category());
    }

    if (!info.hProcess)
        return {};

    const DWORD waited = ::WaitForSingleObject(info.hProcess, 60'000);
    DWORD exit_code = 0;
    ::GetExitCodeProcess(info.hProcess, &exit_code);
    ::CloseHandle(info.hProcess);

    if (waited != WAIT_OBJECT_0)
        return make_error_code(errc::timeout);
    if (exit_code != 0)
        return make_error_code(errc::io_error);
    return {};
}

} // namespace

com0com_installation platform_detect_com0com()
{
    VSS_PROFILE_ZONE("ports.com0com.detect_windows");
    com0com_installation installation;

    const std::string install_dir = read_install_dir();
    if (!install_dir.empty())
    {
        installation.install_dir = install_dir;
        installation.setupc_path = install_dir + "\\setupc.exe";
        installation.found = true;
    }
    else
    {
        const std::string fallback = "C:\\Program Files (x86)\\com0com";
        const DWORD attributes =
            ::GetFileAttributesA((fallback + "\\setupc.exe").c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            installation.install_dir = fallback;
            installation.setupc_path = fallback + "\\setupc.exe";
            installation.found = true;
        }
    }

    installation.pairs = group_com0com_pairs(enumerate_serial_devices());
    return installation;
}

result<void>
platform_create_com0com_pair(const com0com_installation& installation,
                             std::string_view port_a, std::string_view port_b,
                             bool request_elevation)
{
    VSS_PROFILE_ZONE("ports.com0com.create_windows");
    if (port_a.empty() || port_b.empty())
        return make_error_code(errc::invalid_argument);
    if (port_a == port_b)
        return make_error_code(errc::invalid_argument);

    const std::string arguments = "install PortName=" + std::string(port_a) +
                                  " PortName=" + std::string(port_b);
    return run_setupc(installation, arguments, request_elevation);
}

result<void>
platform_remove_com0com_pair(const com0com_installation& installation,
                             int index, bool request_elevation)
{
    VSS_PROFILE_ZONE("ports.com0com.remove_windows");
    if (index < 0)
        return make_error_code(errc::invalid_argument);
    return run_setupc(installation, "remove " + std::to_string(index),
                      request_elevation);
}

} // namespace vss::detail
