//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/profile.hpp>

#include "../serial_enumeration_platform.hpp"

#include <windows.h>

#include <devguid.h>
#include <setupapi.h>

#include <string>
#include <utility>

namespace vss::detail
{
namespace
{

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

std::string read_string_property(HDEVINFO info, SP_DEVINFO_DATA& data,
                                 DWORD property)
{
    DWORD type = 0;
    DWORD required = 0;
    ::SetupDiGetDeviceRegistryPropertyW(info, &data, property, &type, nullptr,
                                        0, &required);
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
    if (!::SetupDiGetDeviceInstanceIdW(info, &data, buffer.data(), required,
                                       nullptr))
        return {};
    buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
    return to_utf8(buffer.c_str());
}

} // namespace

std::vector<serial_device_info> platform_enumerate_serial_devices()
{
    VSS_PROFILE_ZONE("ports.enumerate.windows");
    std::vector<serial_device_info> devices;

    HDEVINFO info = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr,
                                           nullptr, DIGCF_PRESENT);
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

        if (instance.rfind("COM0COM", 0) == 0 ||
            hardware.find("com0com") != std::string::npos ||
            hardware.find("COM0COM") != std::string::npos)
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

} // namespace vss::detail
