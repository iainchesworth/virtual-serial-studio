//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The studio's side of the vsscom driver: finding its ports and opening the hub
// end of one. drivers/windows/vsscom/public.h says what the driver expects.
//

#include "../vcom_driver_platform.hpp"

#include <vss/core/error.hpp>

#include <boost/asio/windows/stream_handle.hpp>

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
// Defines the interface GUID of public.h in this file only.
#include <initguid.h>

#include <vsscom/public.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace vss::detail
{
namespace
{

std::string to_utf8(const std::wstring& text)
{
    if (text.empty())
        return {};

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                          static_cast<int>(text.size()), out.data(), size,
                          nullptr, nullptr);
    return out;
}

/// One port of the driver: the COM name an application opens and the path of
/// the hub end.
struct hub_interface
{
    std::wstring port;
    std::wstring path;
};

std::wstring read_port_name(HDEVINFO set, SP_DEVINFO_DATA& info)
{
    const HKEY key = ::SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0,
                                            DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE)
        return {};

    wchar_t buffer[32]{};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG status = ::RegQueryValueExW(
        key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(buffer),
        &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ)
        return {};
    return buffer;
}

/// The present ports of the driver, in the order Windows lists them.
std::vector<hub_interface> enumerate_hubs()
{
    std::vector<hub_interface> hubs;
    GUID guid = GUID_DEVINTERFACE_VSSCOM;
    const HDEVINFO set = ::SetupDiGetClassDevsW(
        &guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return hubs;

    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = sizeof(data);
        if (!::SetupDiEnumDeviceInterfaces(set, nullptr, &guid, index, &data))
            break;

        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        DWORD required = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &required,
                                           &info);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            continue;

        std::vector<unsigned char> storage(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!::SetupDiGetDeviceInterfaceDetailW(set, &data, detail, required,
                                                nullptr, &info))
            continue;

        hubs.push_back({read_port_name(set, info), detail->DevicePath});
    }

    ::SetupDiDestroyDeviceInfoList(set);
    return hubs;
}

/// Opens the hub end. An overlapped handle, so that asio can use it.
HANDLE open_hub(const std::wstring& path, DWORD& error)
{
    const HANDLE handle = ::CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    error = handle == INVALID_HANDLE_VALUE ? ::GetLastError() : 0;
    return handle;
}

/// A control request on an overlapped handle, waited for here. The low bit of
/// the event handle keeps the completion off the I/O completion port that asio
/// reads, which would not know what to do with it.
bool control(HANDLE handle, DWORD code, void* input, DWORD input_size,
             void* output, DWORD output_size, DWORD* returned = nullptr)
{
    const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr)
        return false;

    OVERLAPPED overlapped{};
    overlapped.hEvent = reinterpret_cast<HANDLE>(
        reinterpret_cast<ULONG_PTR>(event) | 1);

    DWORD count = 0;
    BOOL ok = ::DeviceIoControl(handle, code, input, input_size, output,
                                output_size, nullptr, &overlapped);
    if (!ok && ::GetLastError() != ERROR_IO_PENDING)
    {
        ::CloseHandle(event);
        return false;
    }
    ok = ::GetOverlappedResult(handle, &overlapped, &count, TRUE);
    ::CloseHandle(event);

    if (returned != nullptr)
        *returned = count;
    return ok != FALSE;
}

bool read_state(HANDLE handle, VSSCOM_HUB_STATE& state)
{
    DWORD returned = 0;
    return control(handle, IOCTL_VSSCOM_HUB_GET_STATE, nullptr, 0, &state,
                   sizeof(state), &returned) &&
           returned == sizeof(state);
}

bool read_version(HANDLE handle, VSSCOM_VERSION_INFO& version)
{
    DWORD returned = 0;
    return control(handle, IOCTL_VSSCOM_GET_VERSION, nullptr, 0, &version,
                   sizeof(version), &returned) &&
           returned == sizeof(version);
}

/// The hub end of one driver port, as the byte channel under an endpoint.
///
/// What the application writes arrives through reads, and what is written
/// here is what the application reads. From the application's side this end
/// plays the device, so CTS, DSR, DCD and RI are the lines it sets, and DTR,
/// RTS and break are the lines the application drives.
class driver_transport final : public transport
{
public:
    driver_transport(const executor_type& executor, HANDLE handle,
                     std::string port, std::string name)
        : handle_(executor, handle), raw_(handle), port_(std::move(port)),
          name_(std::move(name))
    {
        refresh_lines();
    }

    ~driver_transport() override { close(); }

    std::string describe() const override
    {
        return "Virtual Serial Studio COM port " + port_ + " (" + name_ + ")";
    }

    std::string public_path() const override { return port_; }

    bool is_open() const override { return open_.load(); }

    void async_read_some(boost::asio::mutable_buffer buffer,
                         io_handler handler) override
    {
        handle_.async_read_some(buffer, std::move(handler));
    }

    void async_write_some(boost::asio::const_buffer buffer,
                          io_handler handler) override
    {
        handle_.async_write_some(buffer, std::move(handler));
    }

    void close() override
    {
        if (!open_.exchange(false))
            return;
        boost::system::error_code ignored;
        handle_.cancel(ignored);
        handle_.close(ignored);
    }

    void cancel() override
    {
        boost::system::error_code ignored;
        handle_.cancel(ignored);
    }

    bool supports_control_lines() const noexcept override { return true; }

    void set_control_line(control_line line, bool state) override
    {
        // Only the inputs are ours to drive; DTR, RTS and break belong to the
        // application.
        ULONG bit = 0;
        switch (line)
        {
        case control_line::cts: bit = VSSCOM_LINE_CTS; break;
        case control_line::dsr: bit = VSSCOM_LINE_DSR; break;
        case control_line::dcd: bit = VSSCOM_LINE_DCD; break;
        case control_line::ri: bit = VSSCOM_LINE_RI; break;
        default: return;
        }

        std::lock_guard lock(mutex_);
        ULONG next = hub_lines_;
        if (state)
            next |= bit;
        else
            next &= ~bit;
        if (!open_.load() ||
            !control(raw_, IOCTL_VSSCOM_HUB_SET_LINES, &next, sizeof(next),
                     nullptr, 0))
            return;
        hub_lines_ = next;
        lines_.set(line, state);
    }

    control_lines lines() const override
    {
        std::lock_guard lock(mutex_);
        return lines_;
    }

    void refresh_lines() override
    {
        VSSCOM_HUB_STATE state{};
        std::lock_guard lock(mutex_);
        if (!open_.load() || !read_state(raw_, state))
            return;
        lines_.set(control_line::dtr, state.Dtr != 0);
        lines_.set(control_line::rts, state.Rts != 0);
        lines_.set(control_line::break_signal, state.Break != 0);
    }

private:
    boost::asio::windows::stream_handle handle_;
    HANDLE raw_;
    std::string port_;
    std::string name_;
    mutable std::mutex mutex_;
    control_lines lines_;
    ULONG hub_lines_ = 0;
    std::atomic<bool> open_{true};
};

} // namespace

bool platform_driver_available()
{
    return !enumerate_hubs().empty();
}

std::string platform_driver_version()
{
    for (const auto& hub : enumerate_hubs())
    {
        DWORD error = 0;
        const HANDLE handle = open_hub(hub.path, error);
        if (handle == INVALID_HANDLE_VALUE)
            continue;

        VSSCOM_VERSION_INFO version{};
        const bool ok = read_version(handle, version);
        ::CloseHandle(handle);
        if (ok)
            return std::to_string(version.Version);
    }
    return {};
}

result<virtual_port_result>
platform_create_driver_port(const transport::executor_type& executor,
                            const std::string& name)
{
    const auto hubs = enumerate_hubs();
    if (hubs.empty())
        return make_error_code(errc::unsupported);

    // A port is free when nobody has its hub end and no application has the
    // COM port open. Anything else belongs to somebody.
    errc failure = errc::device_busy;
    for (const auto& hub : hubs)
    {
        DWORD error = 0;
        const HANDLE handle = open_hub(hub.path, error);
        if (handle == INVALID_HANDLE_VALUE)
        {
            if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
                failure = errc::io_error;
            continue;
        }

        VSSCOM_VERSION_INFO version{};
        VSSCOM_HUB_STATE state{};
        if (!read_version(handle, version) ||
            version.Version != VSSCOM_VERSION)
        {
            // A driver of another version: the hub speaks version 2.
            ::CloseHandle(handle);
            failure = errc::unsupported;
            continue;
        }
        if (!read_state(handle, state) || state.AppOpen != 0)
        {
            ::CloseHandle(handle);
            continue;
        }

        virtual_port_result out;
        out.path = to_utf8(hub.port);
        out.kind = virtual_port_kind::windows_driver;
        out.port = std::make_shared<driver_transport>(executor, handle,
                                                     out.path, name);
        return out;
    }

    return make_error_code(failure);
}

} // namespace vss::detail
