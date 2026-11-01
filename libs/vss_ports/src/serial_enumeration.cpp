//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/serial_enumeration.hpp>

#include <algorithm>
#include <cctype>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <devguid.h>
#  include <setupapi.h>
#else
#  include <filesystem>
#endif

namespace vss {
namespace {

std::string extract_com_name(std::string_view text)
{
    const auto open = text.rfind('(');
    const auto close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos
        || close < open)
        return {};

    std::string candidate(text.substr(open + 1, close - open - 1));
    if (candidate.size() < 4)
        return {};
    if (candidate.compare(0, 3, "COM") != 0 && candidate.compare(0, 3, "com") != 0)
        return {};
    for (std::size_t index = 3; index < candidate.size(); ++index)
    {
        if (!std::isdigit(static_cast<unsigned char>(candidate[index])))
            return {};
    }
    return candidate;
}

bool parse_com0com_instance(std::string_view instance, int& pair, std::string& side)
{
    const auto marker = instance.rfind("CNC");
    if (marker == std::string_view::npos || marker + 4 > instance.size())
        return false;

    const char which = instance[marker + 3];
    if (which != 'A' && which != 'B' && which != 'a' && which != 'b')
        return false;

    side.assign(1, static_cast<char>(std::toupper(static_cast<unsigned char>(which))));

    int value = 0;
    bool any = false;
    for (std::size_t index = marker + 4; index < instance.size(); ++index)
    {
        const char digit = instance[index];
        if (!std::isdigit(static_cast<unsigned char>(digit)))
        {
            if (any)
                break;
            return false;
        }
        any = true;
        value = value * 10 + (digit - '0');
    }

    if (!any)
        return false;
    pair = value;
    return true;
}

#if defined(_WIN32)

std::string to_utf8(const wchar_t* text)
{
    if (!text || !*text)
        return {};

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                           nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::string read_string_property(HDEVINFO info, SP_DEVINFO_DATA& data, DWORD property)
{
    DWORD type = 0;
    DWORD required = 0;
    ::SetupDiGetDeviceRegistryPropertyW(info, &data, property, &type, nullptr, 0,
                                        &required);
    if (required == 0)
        return {};

    std::wstring buffer(required / sizeof(wchar_t) + 1, L'\0');
    if (!::SetupDiGetDeviceRegistryPropertyW(
            info, &data, property, &type,
            reinterpret_cast<PBYTE>(buffer.data()), required, nullptr))
        return {};

    buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
    return to_utf8(buffer.c_str());
}

std::string read_instance_id(HDEVINFO info, SP_DEVINFO_DATA& data)
{
    DWORD required = 0;
    ::SetupDiGetDeviceInstanceIdW(info, &data, nullptr, 0, &required);
    if (required == 0)
        return {};
    std::wstring buffer(required + 1, L'\0');
    if (!::SetupDiGetDeviceInstanceIdW(info, &data, buffer.data(), required, nullptr))
        return {};
    buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
    return to_utf8(buffer.c_str());
}

std::vector<serial_device_info> enumerate_windows()
{
    std::vector<serial_device_info> devices;

    HDEVINFO info = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr,
                                           DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE)
        return devices;

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);

    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(info, index, &data); ++index)
    {
        const std::string friendly =
            read_string_property(info, data, SPDRP_FRIENDLYNAME);
        const std::string hardware =
            read_string_property(info, data, SPDRP_HARDWAREID);
        const std::string instance = read_instance_id(info, data);

        serial_device_info device;
        device.path = extract_com_name(friendly);
        device.description = friendly;
        device.hardware_id = hardware;

        if (device.path.empty())
            continue;

        if (instance.rfind("COM0COM", 0) == 0
            || hardware.find("com0com") != std::string::npos
            || hardware.find("COM0COM") != std::string::npos)
        {
            device.com0com = true;
            device.virtual_device = true;
            parse_com0com_instance(instance, device.com0com_pair,
                                   device.com0com_side);
        }

        devices.push_back(std::move(device));
    }

    ::SetupDiDestroyDeviceInfoList(info);
    return devices;
}

#endif // _WIN32

#if !defined(_WIN32)

bool looks_like_serial(const std::string& name)
{
    static constexpr const char* prefixes[] = {"ttyUSB", "ttyACM", "ttyAMA",
                                               "ttyS", "ttyXR", "ttyCH",
                                               "ttymxc", "cu.usb", "tty.usb"};
    for (const char* prefix : prefixes)
    {
        if (name.rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}

std::vector<serial_device_info> enumerate_posix()
{
    std::vector<serial_device_info> devices;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec))
    {
        if (ec)
            break;
        const std::string name = entry.path().filename().string();
        if (!looks_like_serial(name))
            continue;

        serial_device_info device;
        device.path = entry.path().string();
        device.description = name;
        device.virtual_device = false;
        devices.push_back(std::move(device));
    }

    return devices;
}

#endif

} // namespace

std::string parse_com_port_name(std::string_view description)
{
    return extract_com_name(description);
}

std::optional<com0com_end> parse_com0com_instance_id(
    std::string_view instance_id)
{
    // Instance ids are upper case in the driver, but PnP consumers on other
    // paths have been seen lower case; normalise before matching.
    std::string normalised(instance_id);
    std::transform(normalised.begin(), normalised.end(), normalised.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::toupper(value));
                   });

    com0com_end parsed;
    if (!parse_com0com_instance(normalised, parsed.pair, parsed.side))
        return std::nullopt;
    return parsed;
}

std::vector<serial_device_info> normalise(std::vector<serial_device_info> devices)
{
    std::vector<serial_device_info> out;
    out.reserve(devices.size());

    for (auto& device : devices)
    {
        if (device.path.empty())
            continue;
        const bool duplicate = std::any_of(
            out.begin(), out.end(), [&device](const serial_device_info& existing) {
                return existing.path == device.path;
            });
        if (!duplicate)
            out.push_back(std::move(device));
    }

    std::sort(out.begin(), out.end(),
              [](const serial_device_info& lhs, const serial_device_info& rhs) {
                  return lhs.path < rhs.path;
              });
    return out;
}

std::vector<serial_device_info> enumerate_serial_devices()
{
#if defined(_WIN32)
    return normalise(enumerate_windows());
#else
    return normalise(enumerate_posix());
#endif
}

std::vector<std::string> enumerate_serial_port_names()
{
    std::vector<std::string> names;
    for (const auto& device : enumerate_serial_devices())
        names.push_back(device.path);
    return names;
}

} // namespace vss
