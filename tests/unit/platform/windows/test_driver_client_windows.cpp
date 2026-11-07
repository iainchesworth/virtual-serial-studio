//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The studio's side of the vsscom driver. A hosted runner has no driver, so
// most of these cases pin the protocol the guest scripts and the driver share,
// and what the client reports when the driver is absent. One case runs the
// client against the real driver; it is skipped unless VSS_TEST_DRIVER=1, which
// the Windows guest script sets.
//

#include <vss/ports/vcom_driver.hpp>

#include "test_framework.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <winioctl.h>
#include <devpropdef.h>

#include <vsscom/public.h>
#include <vsscom_helper/protocol.h>

using namespace vss;

TEST_CASE("driver client: the control codes are the ones the guest scripts use")
{
    // test-driver.ps1 spells these out; a change here must change it too.
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_GET_VERSION), 0x80002000ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_GET_STATE), 0x80002004ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_WAIT_STATE), 0x80002008ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_SET_LINES), 0x8000200Cul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_REVOKE), 0x80002010ul);
    REQUIRE_EQ(static_cast<int>(VSSCOM_VERSION), 3);
}

TEST_CASE("driver client: the hub state has the layout the driver fills")
{
    // Twelve 32-bit words, in the order the guest script reads them.
    REQUIRE_EQ(sizeof(VSSCOM_HUB_STATE), std::size_t{48});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Generation), std::size_t{0});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, AppOpen), std::size_t{4});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Dtr), std::size_t{24});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Lines), std::size_t{36});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, TxQueued), std::size_t{40});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, AppProcessId), std::size_t{44});
    REQUIRE_EQ(sizeof(VSSCOM_VERSION_INFO), std::size_t{8});

    // The line bits are the modem status bits Windows uses.
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_MASK), 0xF0u);
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_CTS), static_cast<unsigned>(MS_CTS_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_DSR), static_cast<unsigned>(MS_DSR_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_RI), static_cast<unsigned>(MS_RING_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_DCD), static_cast<unsigned>(MS_RLSD_ON));
}

TEST_CASE("driver client: the control device has the codes and layout the guest scripts use")
{
    // test-driver.ps1 spells these out in CtlIo; a change here must change it too.
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_GET_VERSION), 0x80002040ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_CREATE_PORT), 0x80002044ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_DESTROY_PORT), 0x80002048ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_LIST_PORTS), 0x8000204Cul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_GET_OWNER), 0x80002050ul);
    REQUIRE_EQ(sizeof(VSSCOM_CTL_OWNER), std::size_t{8 + VSSCOM_CTL_MAX_SID_BYTES});
    REQUIRE_EQ(offsetof(VSSCOM_CTL_OWNER, Sid), std::size_t{8});

    REQUIRE_EQ(sizeof(VSSCOM_CTL_VERSION_INFO), std::size_t{8});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_CREATE), std::size_t{8});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_PORT), std::size_t{4});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_LIST_ENTRY), std::size_t{8});
    REQUIRE_EQ(offsetof(VSSCOM_CTL_LIST, Ports), std::size_t{4});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_LIST),
               std::size_t{4 + 8 * VSSCOM_CTL_MAX_PORTS});
    REQUIRE_EQ(static_cast<int>(VSSCOM_CTL_MAX_PORTS), 32);
    REQUIRE_EQ(static_cast<int>(VSSCOM_CTL_MAX_PORTS_PER_USER), 16);
    REQUIRE_EQ(static_cast<int>(VSSCOM_CTL_VERSION), 1);
}

TEST_CASE("driver client: the instance id of a port made on demand is predictable")
{
    // The client looks for this device instance id after CREATE_PORT.
    wchar_t suffix[16]{};
    std::swprintf(suffix, 16, VSSCOM_PORT_INSTANCE_FORMAT, 10ul);
    REQUIRE(std::wstring(suffix) == L"000A");
    REQUIRE(std::wstring(VSSCOM_PORT_HARDWARE_ID) == L"VSSCOM\\PORT");
}

namespace
{

using namespace std::chrono_literals;

/// Reads once from a transport and waits for the answer on this thread.
std::size_t transport_read(transport& port, void* data, std::size_t size)
{
    using answer = std::pair<boost::system::error_code, std::size_t>;
    auto promise = std::make_shared<std::promise<answer>>();
    auto future = promise->get_future();
    port.async_read_some(boost::asio::buffer(data, size),
                         [promise](boost::system::error_code ec, std::size_t n)
                         { promise->set_value({ec, n}); });
    REQUIRE(future.wait_for(10s) == std::future_status::ready);
    const answer got = future.get();
    REQUIRE(!got.first);
    return got.second;
}

void transport_write(transport& port, const char* text)
{
    using answer = std::pair<boost::system::error_code, std::size_t>;
    auto promise = std::make_shared<std::promise<answer>>();
    auto future = promise->get_future();
    port.async_write_some(boost::asio::buffer(text, std::strlen(text)),
                          [promise](boost::system::error_code ec, std::size_t n)
                          { promise->set_value({ec, n}); });
    REQUIRE(future.wait_for(10s) == std::future_status::ready);
    const answer got = future.get();
    REQUIRE(!got.first);
    REQUIRE_EQ(got.second, std::strlen(text));
}

HANDLE open_com(const std::string& name)
{
    return ::CreateFileA(("\\\\.\\" + name).c_str(), GENERIC_READ | GENERIC_WRITE,
                         0, nullptr, OPEN_EXISTING, 0, nullptr);
}

/// Waits until the COM name cannot be opened any more.
bool wait_gone(const std::string& name)
{
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until)
    {
        const auto handle = open_com(name);
        if (handle == INVALID_HANDLE_VALUE)
            return true;
        ::CloseHandle(handle);
        std::this_thread::sleep_for(250ms);
    }
    return false;
}

} // namespace

// The one case that needs the driver. It runs in a Windows guest with the
// driver installed (drivers/windows/test/test-client.ps1 sets the variable) and
// is skipped everywhere else.
TEST_CASE("driver client: against the real driver, a port is made, carries bytes and goes")
{
    char enabled[8]{};
    const DWORD length = ::GetEnvironmentVariableA("VSS_TEST_DRIVER", enabled,
                                                   sizeof(enabled));
    if (length != 1 || enabled[0] != '1')
        VSS_SKIP("needs the driver: set VSS_TEST_DRIVER=1 in a guest that has it");

    REQUIRE(driver_available());
    REQUIRE(!driver_version().empty());

    boost::asio::io_context io;
    auto guard = boost::asio::make_work_guard(io);
    std::thread runner([&io] { io.run(); });
    auto stop = [&]
    {
        guard.reset();
        io.stop();
        runner.join();
    };

    try
    {
        auto first = create_driver_port(io.get_executor(), "guest test one");
        REQUIRE(first.has_value());
        const std::string name = first->path;
        REQUIRE(name.rfind("COM", 0) == 0);
        REQUIRE(first->kind == virtual_port_kind::windows_driver);
        transport& hub = *first->port;
        REQUIRE(hub.is_open());

        // A second request makes another port, and the two do not meet.
        auto second = create_driver_port(io.get_executor(), "guest test two");
        REQUIRE(second.has_value());
        REQUIRE(second->path != name);

        const auto app = open_com(name);
        REQUIRE(app != INVALID_HANDLE_VALUE);
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 100;
        timeouts.ReadTotalTimeoutConstant = 5000;
        REQUIRE(::SetCommTimeouts(app, &timeouts) != FALSE);

        // Application to hub.
        DWORD done = 0;
        REQUIRE(::WriteFile(app, "hello hub", 9, &done, nullptr) != FALSE);
        REQUIRE_EQ(done, DWORD{9});
        char buffer[64]{};
        std::size_t got = 0;
        while (got < 9)
            got += transport_read(hub, buffer + got, sizeof(buffer) - got);
        REQUIRE(std::string(buffer, got) == "hello hub");

        // Hub to application.
        transport_write(hub, "hello app");
        char reply[64]{};
        REQUIRE(::ReadFile(app, reply, 9, &done, nullptr) != FALSE);
        REQUIRE_EQ(done, DWORD{9});
        REQUIRE(std::string(reply, done) == "hello app");

        // The lines the application drives reach the hub, and the hub's reach
        // the application.
        REQUIRE(hub.supports_control_lines());
        REQUIRE(::EscapeCommFunction(app, SETDTR) != FALSE);
        hub.refresh_lines();
        REQUIRE(hub.lines().dtr());
        REQUIRE(!hub.lines().rts());
        hub.set_control_line(control_line::cts, true);
        DWORD status = 0;
        REQUIRE(::GetCommModemStatus(app, &status) != FALSE);
        REQUIRE((status & MS_CTS_ON) != 0);

        // Closing the transport takes its port away.
        ::CloseHandle(app);
        hub.close();
        REQUIRE(wait_gone(name));

        // The other port is untouched.
        REQUIRE(second->port->is_open());
        const std::string other = second->path;
        second->port->close();
        REQUIRE(wait_gone(other));
    }
    catch (...)
    {
        stop();
        throw;
    }
    stop();
}

TEST_CASE("driver client: without the driver it says so and takes nothing")
{
    if (driver_available())
        return; // a machine with the driver installed: covered in the guests

    boost::asio::io_context io;
    REQUIRE(driver_version().empty());
    REQUIRE_EQ(create_driver_port(io.get_executor(), "x"),
               make_error_code(errc::unsupported));
}

namespace
{

/// A COM number nothing resolves, from `first` up.
unsigned free_com_number(unsigned first)
{
    for (unsigned number = first; number <= 255; ++number)
    {
        char target[256];
        if (::QueryDosDeviceA(("COM" + std::to_string(number)).c_str(), target,
                              sizeof(target)) == 0)
            return number;
    }
    return 0;
}

bool helper_service_present()
{
    return ::WaitNamedPipeW(VssComHelperPipeName, 1) != FALSE ||
           ::GetLastError() != ERROR_FILE_NOT_FOUND;
}

} // namespace

// Needs the driver and the helper service in a guest, like the case above, and
// changes the COM numbers of ports it makes. test-client.ps1 runs it as C2.
TEST_CASE("driver client: COM numbers with the helper service")
{
    char enabled[8]{};
    const DWORD length = ::GetEnvironmentVariableA("VSS_TEST_DRIVER", enabled,
                                                   sizeof(enabled));
    if (length != 1 || enabled[0] != '1')
        VSS_SKIP("needs the driver: set VSS_TEST_DRIVER=1 in a guest that has it");
    if (!helper_service_present())
        VSS_SKIP("needs the helper service (install-driver.ps1 -Helper)");

    const unsigned first = free_com_number(100);
    const unsigned second = free_com_number(first + 1);
    REQUIRE(first != 0 && second != 0);

    boost::asio::io_context io;
    auto guard = boost::asio::make_work_guard(io);
    std::thread runner([&io] { io.run(); });
    auto stop = [&]
    {
        guard.reset();
        io.stop();
        runner.join();
    };

    try
    {
        // A port made with a number has it.
        auto made = create_driver_port(io.get_executor(), "numbered", first);
        REQUIRE(made.has_value());
        const std::string name1 = "COM" + std::to_string(first);
        const std::string name2 = "COM" + std::to_string(second);
        REQUIRE(made->path == name1);
        transport& hub = *made->port;
        REQUIRE(hub.public_path() == name1);

        // A number another device has is refused, and no port is left over.
        auto taken = create_driver_port(io.get_executor(), "taken", first);
        REQUIRE(!taken.has_value());
        REQUIRE_EQ(taken.error(), make_error_code(errc::already_exists));

        // The driver names the program that opens the port, and the transport
        // logs it with the time.
        std::mutex notes_mutex;
        std::vector<std::string> notes;
        hub.set_notice_sink(
            [&](std::string text)
            {
                const std::lock_guard lock(notes_mutex);
                notes.push_back(std::move(text));
            });
        const auto noted = [&](const std::string& what)
        {
            const auto until = std::chrono::steady_clock::now() + 10s;
            while (std::chrono::steady_clock::now() < until)
            {
                {
                    const std::lock_guard lock(notes_mutex);
                    for (const auto& note : notes)
                    {
                        if (note.find(what) != std::string::npos)
                            return true;
                    }
                }
                std::this_thread::sleep_for(50ms);
            }
            return false;
        };

        {
            const auto idle = driver_peer(name1);
            REQUIRE(idle.has_value());
            REQUIRE(!idle->open);
            REQUIRE_EQ(idle->process_id, std::uint32_t{0});
        }
        HANDLE app = open_com(name1);
        REQUIRE(app != INVALID_HANDLE_VALUE);
        REQUIRE(noted("a program opened the port"));
        {
            const auto busy = driver_peer(name1);
            REQUIRE(busy.has_value());
            REQUIRE(busy->open);
            REQUIRE_EQ(busy->process_id, static_cast<std::uint32_t>(::GetCurrentProcessId()));
        }

        // With a program on the port the number cannot change.
        REQUIRE_EQ(set_driver_com_number(hub, second),
                   make_error_code(errc::device_busy));
        ::CloseHandle(app);
        REQUIRE(noted("the program closed the port"));
        REQUIRE(!vss_test::require_value(driver_peer(name1)).open);

        // A read that is waiting survives the change.
        std::promise<std::pair<boost::system::error_code, std::size_t>> answered;
        auto future = answered.get_future();
        char buffer[16]{};
        hub.async_read_some(boost::asio::buffer(buffer, sizeof(buffer)),
                            [&](boost::system::error_code ec, std::size_t n)
                            { answered.set_value({ec, n}); });

        const auto renamed = set_driver_com_number(hub, second);
        REQUIRE(renamed.has_value());
        REQUIRE(*renamed == name2);
        REQUIRE(hub.public_path() == name2);
        REQUIRE(driver_peer(name2).has_value());
        REQUIRE(!driver_peer(name1).has_value());

        // The old name is gone, the new one opens, and the read is still ours.
        const auto old_name = open_com(name1);
        REQUIRE(old_name == INVALID_HANDLE_VALUE);
        app = open_com(name2);
        REQUIRE(app != INVALID_HANDLE_VALUE);
        DWORD done = 0;
        REQUIRE(::WriteFile(app, "after", 5, &done, nullptr) != FALSE);
        REQUIRE(future.wait_for(10s) == std::future_status::ready);
        const auto got = future.get();
        REQUIRE(!got.first);
        REQUIRE(std::string(buffer, got.second) == "after");
        ::CloseHandle(app);

        // The number it left is free for the next port.
        auto reused = create_driver_port(io.get_executor(), "reused", first);
        REQUIRE(reused.has_value());
        REQUIRE(reused->path == name1);

        // A number outside the range is refused before anything happens.
        REQUIRE_EQ(set_driver_com_number(hub, 0),
                   make_error_code(errc::invalid_argument));

        reused->port->close();
        hub.close();
        REQUIRE(wait_gone(name2));
        REQUIRE(wait_gone(name1));
    }
    catch (...)
    {
        stop();
        throw;
    }
    stop();
}
