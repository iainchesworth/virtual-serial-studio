//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The studio's side of the vsscom driver: asking the control device for a port,
// finding the ports that exist, and opening the hub end of one.
// drivers/windows/vsscom/public.h says what the driver expects.
//

#include "../vcom_driver_platform.hpp"

#include <vss/core/error.hpp>

#include <boost/asio/windows/stream_handle.hpp>

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h> // MAX_DEVICE_ID_LEN
// Defines the interface GUID of public.h in this file only.
#include <initguid.h>

#include <vsscom/public.h>

#include <atomic>
#include <chrono>
#include <cwchar>
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

/// One port of the driver: the COM name an application opens, the path of the
/// hub end, and the device instance id (VSSCOM\PORT\0000 for a port that the
/// control device made).
struct hub_interface
{
    std::wstring port;
    std::wstring path;
    std::wstring instance;
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

        wchar_t instance[MAX_DEVICE_ID_LEN]{};
        if (!::SetupDiGetDeviceInstanceIdW(set, &info, instance,
                                           MAX_DEVICE_ID_LEN, nullptr))
            instance[0] = L'\0';

        hubs.push_back({read_port_name(set, info), detail->DevicePath, instance});
    }

    ::SetupDiDestroyDeviceInfoList(set);
    return hubs;
}

/// The path of the control device's interface, or empty when the control
/// device is not installed.
std::wstring find_control_path()
{
    GUID guid = GUID_DEVINTERFACE_VSSCOMCTL;
    const HDEVINFO set = ::SetupDiGetClassDevsW(
        &guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return {};

    std::wstring path;
    SP_DEVICE_INTERFACE_DATA data{};
    data.cbSize = sizeof(data);
    if (::SetupDiEnumDeviceInterfaces(set, nullptr, &guid, 0, &data))
    {
        DWORD required = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &required,
                                           nullptr);
        if (required >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
        {
            std::vector<unsigned char> storage(required);
            auto* detail =
                reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
                    storage.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (::SetupDiGetDeviceInterfaceDetailW(set, &data, detail, required,
                                                   nullptr, nullptr))
                path = detail->DevicePath;
        }
    }

    ::SetupDiDestroyDeviceInfoList(set);
    return path;
}

/// A handle that is closed when it goes out of scope.
struct handle_closer
{
    void operator()(HANDLE handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
    }
};

using unique_handle = std::unique_ptr<void, handle_closer>;

/// Opens the control device. A plain handle: its requests are short and waited
/// for. A port made through it lives as long as this handle does.
HANDLE open_control(const std::wstring& path, DWORD& error)
{
    const HANDLE handle = ::CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    error = handle == INVALID_HANDLE_VALUE ? ::GetLastError() : 0;
    return handle;
}

bool control_request(HANDLE handle, DWORD code, void* input, DWORD input_size,
                     void* output, DWORD output_size, DWORD& error)
{
    DWORD returned = 0;
    const BOOL ok = ::DeviceIoControl(handle, code, input, input_size, output,
                                      output_size, &returned, nullptr);
    error = ok ? 0 : ::GetLastError();
    return ok != FALSE && returned == output_size;
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
    return control(handle, static_cast<DWORD>(IOCTL_VSSCOM_HUB_GET_STATE), nullptr, 0, &state,
                   sizeof(state), &returned) &&
           returned == sizeof(state);
}

bool read_version(HANDLE handle, VSSCOM_VERSION_INFO& version)
{
    DWORD returned = 0;
    return control(handle, static_cast<DWORD>(IOCTL_VSSCOM_GET_VERSION), nullptr, 0, &version,
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
    /// `control` is the control handle that made the port, or empty for a port
    /// that was there already. It is closed after the hub end, and closing it
    /// removes the port.
    driver_transport(const executor_type& executor, HANDLE handle,
                     std::string port, std::string name,
                     unique_handle control = {})
        : handle_(executor, handle), raw_(handle), port_(std::move(port)),
          name_(std::move(name)), control_(std::move(control))
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
        // The port was made for this transport and goes with it.
        control_.reset();
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
            !control(raw_, static_cast<DWORD>(IOCTL_VSSCOM_HUB_SET_LINES), &next, sizeof(next),
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
    unique_handle control_;
};

/// Asks the control device for a port and waits for Windows to build it.
///
/// Windows installs the driver on the new device before the hub end exists,
/// which takes a second or two.
result<virtual_port_result>
create_port_on_demand(const transport::executor_type& executor,
                      const std::string& name, const std::wstring& control_path)
{
    DWORD error = 0;
    unique_handle control(open_control(control_path, error));
    if (control.get() == INVALID_HANDLE_VALUE)
    {
        // The closer ignores an invalid handle, so there is nothing to release.
        return make_error_code(error == ERROR_ACCESS_DENIED
                                   ? errc::permission_denied
                                   : errc::io_error);
    }

    VSSCOM_CTL_VERSION_INFO info{};
    if (!control_request(control.get(),
                         static_cast<DWORD>(IOCTL_VSSCOM_CTL_GET_VERSION),
                         nullptr, 0, &info, sizeof(info), error) ||
        info.Version != VSSCOM_CTL_VERSION)
        return make_error_code(errc::unsupported);

    VSSCOM_CTL_CREATE request{};
    VSSCOM_CTL_PORT made{};
    if (!control_request(control.get(),
                         static_cast<DWORD>(IOCTL_VSSCOM_CTL_CREATE_PORT),
                         &request, sizeof(request), &made, sizeof(made),
                         error))
    {
        // No slot left on the machine, or this user holds the most they may.
        if (error == ERROR_NO_SYSTEM_RESOURCES ||
            error == ERROR_NOT_ENOUGH_QUOTA)
            return make_error_code(errc::port_exhausted);
        return make_error_code(error == ERROR_ACCESS_DENIED
                                   ? errc::permission_denied
                                   : errc::io_error);
    }

    // From here, leaving closes the control handle, which removes the port.
    wchar_t suffix[16]{};
    std::swprintf(suffix, 16, VSSCOM_PORT_INSTANCE_FORMAT,
                  static_cast<unsigned long>(made.Slot));
    const std::wstring instance =
        std::wstring(VSSCOM_PORT_HARDWARE_ID) + L"\\" + suffix;

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(30);
    for (;;)
    {
        for (const auto& hub : enumerate_hubs())
        {
            if (hub.port.empty() ||
                ::_wcsicmp(hub.instance.c_str(), instance.c_str()) != 0)
                continue;

            const HANDLE handle = open_hub(hub.path, error);
            if (handle == INVALID_HANDLE_VALUE)
                return make_error_code(errc::io_error);

            VSSCOM_VERSION_INFO version{};
            if (!read_version(handle, version) ||
                version.Version != VSSCOM_VERSION)
            {
                ::CloseHandle(handle);
                return make_error_code(errc::unsupported);
            }

            virtual_port_result out;
            out.path = to_utf8(hub.port);
            out.kind = virtual_port_kind::windows_driver;
            out.port = std::make_shared<driver_transport>(
                executor, handle, out.path, name, std::move(control));
            return out;
        }

        if (clock::now() >= deadline)
            return make_error_code(errc::timeout);
        ::Sleep(100);
    }
}

} // namespace

bool platform_driver_available()
{
    return !find_control_path().empty() || !enumerate_hubs().empty();
}

std::string platform_driver_version()
{
    // Both devices ship together; the control device speaks version 1 of its
    // own interface alongside version 2 of the ports.
    const std::wstring control_path = find_control_path();
    if (!control_path.empty())
    {
        DWORD error = 0;
        unique_handle control(open_control(control_path, error));
        VSSCOM_CTL_VERSION_INFO info{};
        if (control.get() != INVALID_HANDLE_VALUE &&
            control_request(control.get(),
                            static_cast<DWORD>(IOCTL_VSSCOM_CTL_GET_VERSION),
                            nullptr, 0, &info, sizeof(info), error) &&
            info.Version == VSSCOM_CTL_VERSION)
            return std::to_string(VSSCOM_VERSION);
    }

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
    // With the control device installed, make a port of our own: it is ours
    // alone and goes when the transport does. An older driver without one
    // (errc::unsupported) falls back to the ports that exist.
    const std::wstring control_path = find_control_path();
    if (!control_path.empty())
    {
        auto made = create_port_on_demand(executor, name, control_path);
        if (made || made.error() != make_error_code(errc::unsupported))
            return made;
    }

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
