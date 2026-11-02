//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "dialogs/share_plan.hpp"
#include "dialogs/share_wizard.hpp"

#include <QApplication>
#include <QPushButton>
#include <QRadioButton>

#include <memory>
#include <vector>

namespace
{

vss::gui::device_view serial_device(const QString& path)
{
    vss::gui::device_view device;
    device.path = path;
    device.description = QStringLiteral("Serial device");
    return device;
}

vss::gui::device_view pair_end(const QString& path, int pair,
                               const QString& side)
{
    vss::gui::device_view device;
    device.path = path;
    device.description = QStringLiteral("com0com pair end");
    device.virtual_device = true;
    device.com0com = true;
    device.com0com_pair = pair;
    device.com0com_side = side;
    return device;
}

void use_windows(vss::gui::FakeStudioBackend& backend, int pairs)
{
    vss::gui::setup_facts facts;
    facts.platform = QStringLiteral("windows");
    facts.com0com_found = pairs > 0;
    facts.com0com_pairs = pairs;
    backend.set_facts(facts);
}

std::vector<vss::gui::device_view> standard_devices(int pairs)
{
    std::vector<vss::gui::device_view> devices;
    devices.push_back(serial_device(QStringLiteral("COM3")));
    for (int index = 0; index < pairs; ++index)
    {
        const int first = 7 + index * 2;
        devices.push_back(pair_end(QStringLiteral("COM%1").arg(first), index,
                                   QStringLiteral("A")));
        devices.push_back(pair_end(QStringLiteral("COM%1").arg(first + 1),
                                   index, QStringLiteral("B")));
    }
    return devices;
}

void show(QDialog& dialog)
{
    dialog.show();
    QApplication::processEvents();
}

std::unique_ptr<vss::gui::ShareWizard>
make_wizard(vss::gui::FakeStudioBackend& backend, int devices_pairs,
            int free_pairs)
{
    use_windows(backend, devices_pairs);
    backend.set_devices(standard_devices(devices_pairs));
    backend.set_free_pairs(free_pairs);
    auto wizard = std::make_unique<vss::gui::ShareWizard>(backend);
    show(*wizard);
    return wizard;
}

} // namespace

TEST_CASE("share plan: free pairs, warning copy and planned paths")
{
    vss::gui::FakeStudioBackend backend;
    use_windows(backend, 2);
    backend.set_devices(standard_devices(2));

    REQUIRE_EQ(vss::gui::known_com0com_pairs(backend.devices()).size(), 2);
    const QStringList free =
        vss::gui::free_com0com_pair_paths(backend.devices(), backend.ports());
    REQUIRE_EQ(free.size(), 2);
    REQUIRE_EQ(free.at(0), QStringLiteral("COM8"));
    REQUIRE_EQ(free.at(1), QStringLiteral("COM10"));

    const vss::gui::share_plan three = vss::gui::make_share_plan(3, 2, true);
    REQUIRE(three.needs_warning());
    REQUIRE_EQ(three.openable, 2);
    REQUIRE_EQ(three.studio_only, 1);
    REQUIRE_EQ(
        vss::gui::share_warning_plain(three),
        QStringLiteral("Only 2 of the 3 programs can open their port. Each "
                       "outside program needs its own driver pair, and Studio "
                       "found 2 free pairs. The third port works only inside "
                       "Studio."));

    const QStringList planned = vss::gui::planned_share_paths(three, free);
    REQUIRE_EQ(planned.size(), 3);
    REQUIRE_EQ(planned.at(0), QStringLiteral("COM8"));
    REQUIRE_EQ(planned.at(1), QStringLiteral("COM10"));
    REQUIRE_EQ(planned.at(2), QStringLiteral("loopback:listener-3"));

    REQUIRE(!vss::gui::make_share_plan(1, 2, true).needs_warning());
    REQUIRE(!vss::gui::make_share_plan(2, 2, true).needs_warning());

    const vss::gui::share_plan none = vss::gui::make_share_plan(1, 0, true);
    REQUIRE(none.needs_warning());
    REQUIRE_EQ(none.openable, 0);
    REQUIRE_EQ(
        vss::gui::share_warning_plain(none),
        QStringLiteral("Studio cannot create a port another program can open. "
                       "Each outside program needs its own driver pair, and "
                       "Studio found no free pairs. The port works only inside "
                       "Studio."));

    REQUIRE_EQ(vss::gui::ordinal_word(3), QStringLiteral("third"));
    REQUIRE_EQ(vss::gui::ordinal_word(21), QStringLiteral("twenty-first"));

    const vss::gui::share_plan linux_plan =
        vss::gui::make_share_plan(3, 0, false);
    REQUIRE(!linux_plan.needs_warning());
    REQUIRE_EQ(linux_plan.openable, 3);
}

TEST_CASE("share wizard: step 1 defaults, the step 2 table and the warning")
{
    vss::gui::FakeStudioBackend backend;
    const auto wizard = make_wizard(backend, 2, 2);

    REQUIRE_EQ(wizard->current_step(), 0);
    REQUIRE_EQ(wizard->selected_device_path(), QStringLiteral("COM3"));
    REQUIRE_EQ(wizard->device_settings_summary(),
               QStringLiteral("115200 baud, 8 data bits, no parity, 1 stop "
                              "bit, no flow control"));
    REQUIRE(wizard->step1_info_text().contains(QStringLiteral("COM3")));
    REQUIRE(wizard->primary_enabled());

    REQUIRE(wizard->next());
    REQUIRE_EQ(wizard->current_step(), 1);
    REQUIRE_EQ(wizard->programs(), 3);
    REQUIRE(wizard->warning_visible());
    REQUIRE_EQ(
        wizard->warning_text(),
        QStringLiteral("Only 2 of the 3 programs can open their port. Each "
                       "outside program needs its own driver pair, and Studio "
                       "found 2 free pairs. The third port works only inside "
                       "Studio."));
    REQUIRE_EQ(wizard->planned_paths().at(0), QStringLiteral("COM8"));
    REQUIRE_EQ(wizard->planned_paths().at(1), QStringLiteral("COM10"));
    REQUIRE_EQ(wizard->planned_paths().at(2),
               QStringLiteral("loopback:listener-3"));
    REQUIRE(wizard->share_info_text().contains(QStringLiteral("all 3 ports")));

    bool keep_third = false;
    for (QPushButton* button : wizard->findChildren<QPushButton*>())
    {
        if (button->text() == QStringLiteral("Keep the third as Studio only"))
            keep_third = true;
    }
    REQUIRE(keep_third);

    wizard->set_programs(2);
    REQUIRE(!wizard->warning_visible());
    wizard->set_programs(3);
    REQUIRE(wizard->warning_visible());
    REQUIRE(wizard->acknowledge_studio_only());
    REQUIRE(!wizard->warning_visible());
    wizard->set_programs(4);
    REQUIRE(wizard->warning_visible());
    REQUIRE(wizard->warning_text().startsWith(
        QStringLiteral("Only 2 of the 4 programs")));

    REQUIRE(wizard->create_another_pair());
    REQUIRE(wizard->warning_visible());
    REQUIRE_EQ(wizard->free_pair_paths().size(), 3);
    REQUIRE(wizard->warning_text().startsWith(
        QStringLiteral("Only 3 of the 4 programs")));
    wizard->set_programs(3);
    REQUIRE(!wizard->warning_visible());
    wizard->close();
}

TEST_CASE("share wizard: one, two and three programs share successfully")
{
    for (const int programs : {1, 2, 3})
    {
        vss::gui::FakeStudioBackend backend;
        const auto wizard = make_wizard(backend, 3, programs);
        wizard->set_programs(programs);
        REQUIRE(wizard->next());
        REQUIRE(wizard->share());
        REQUIRE_EQ(wizard->current_step(), 2);
        REQUIRE_EQ(wizard->listener_count(), programs);
        REQUIRE_EQ(backend.links().size(), std::size_t{1});
        const QString expected_title =
            programs == 1 ? QStringLiteral("COM3 is shared with 1 program")
                          : QStringLiteral("COM3 is shared with %1 programs")
                                .arg(programs);
        REQUIRE_EQ(wizard->step3_title(), expected_title);
        REQUIRE_EQ(wizard->row_state(0), QStringLiteral("Open"));
        if (programs == 1)
        {
            REQUIRE_EQ(wizard->done_note_text(),
                       QStringLiteral("Everything COM3 receives is now copied "
                                      "to 1 port."));
        }
        else
        {
            REQUIRE_EQ(wizard->done_note_text(),
                       QStringLiteral("Everything COM3 receives is now copied "
                                      "to all %1 ports.")
                           .arg(programs));
        }
        wizard->close();
    }
}

TEST_CASE("share wizard: definite peer states update the step 3 table")
{
    vss::gui::FakeStudioBackend backend;
    const auto wizard = make_wizard(backend, 3, 3);
    wizard->set_programs(3);
    REQUIRE(wizard->next());
    REQUIRE(wizard->share());
    const QVector<vss::endpoint_id> listeners = wizard->listener_ids();
    REQUIRE_EQ(listeners.size(), 3);

    backend.set_peer_state(listeners.at(0), vss::gui::peer_state::not_open);
    QApplication::processEvents();
    REQUIRE_EQ(wizard->row_state(0), QStringLiteral("Waiting for program"));
    REQUIRE_EQ(wizard->row_state(1), QStringLiteral("Open"));
    REQUIRE(!wizard->done_note_text().contains(
        QStringLiteral("The Waiting labels change")));

    backend.set_peer_state(listeners.at(1), vss::gui::peer_state::not_open);
    backend.set_peer_state(listeners.at(2), vss::gui::peer_state::not_open);
    QApplication::processEvents();
    REQUIRE_EQ(wizard->row_state(2), QStringLiteral("Waiting for program"));
    REQUIRE(wizard->done_note_text().contains(
        QStringLiteral("The Waiting labels change")));

    backend.set_peer_state(listeners.at(1), vss::gui::peer_state::open);
    QApplication::processEvents();
    REQUIRE_EQ(wizard->row_state(1), QStringLiteral("Program connected"));
    REQUIRE(wizard->done_note_text().contains(
        QStringLiteral("The Waiting labels change")));
    wizard->close();
}

TEST_CASE("share wizard: limited driver pairs leave Studio-only rows")
{
    vss::gui::FakeStudioBackend backend;
    const auto wizard = make_wizard(backend, 2, 2);
    wizard->set_programs(3);
    REQUIRE(wizard->next());
    REQUIRE(wizard->share());
    REQUIRE_EQ(wizard->listener_count(), 3);
    REQUIRE_EQ(wizard->row_state(0), QStringLiteral("Open"));
    REQUIRE_EQ(wizard->row_state(2), QStringLiteral("Studio only"));
    REQUIRE(wizard->row_path(2).startsWith(QStringLiteral("loopback:")));
    REQUIRE(!wizard->done_note_text().contains(
        QStringLiteral("The Waiting labels change")));

    const auto ports = backend.ports();
    int studio_only = 0;
    for (const auto& port : ports)
    {
        if (port.studio_only)
            ++studio_only;
    }
    REQUIRE_EQ(studio_only, 1);
    wizard->close();
}

TEST_CASE("share wizard: a failed share leaves the studio unchanged")
{
    vss::gui::FakeStudioBackend backend;
    use_windows(backend, 2);
    backend.set_devices(standard_devices(2));
    backend.set_free_pairs(2);
    vss::gui::ShareWizard wizard(backend);
    show(wizard);
    wizard.set_programs(3);
    REQUIRE(wizard.next());

    const std::size_t ports_before = backend.ports().size();
    const std::size_t links_before = backend.links().size();
    backend.fail_share_listener(1, vss::make_error_code(vss::errc::io_error));
    REQUIRE(!wizard.share());
    REQUIRE_EQ(wizard.current_step(), 1);
    REQUIRE(wizard.error_visible());
    REQUIRE_EQ(wizard.last_error(), QStringLiteral("I/O error"));
    REQUIRE_EQ(backend.ports().size(), ports_before);
    REQUIRE_EQ(backend.links().size(), links_before);

    REQUIRE(wizard.share());
    REQUIRE_EQ(wizard.current_step(), 2);
    REQUIRE_EQ(wizard.listener_count(), 3);
}

TEST_CASE("share wizard: a network source needs Advanced and a valid address")
{
    vss::gui::FakeStudioBackend backend;
    backend.set_devices({});
    vss::gui::ShareWizard wizard(backend);
    show(wizard);
    REQUIRE(!wizard.primary_enabled());

    wizard.set_network_source(QStringLiteral("tcp://192.168.1.50:7001"));
    REQUIRE(wizard.network_source());
    REQUIRE(wizard.primary_enabled());
    REQUIRE_EQ(wizard.source_uri(), QStringLiteral("tcp://192.168.1.50:7001"));
    REQUIRE(wizard.next());
    REQUIRE(wizard.share());
    REQUIRE_EQ(wizard.current_step(), 2);
    REQUIRE_EQ(wizard.step3_title(),
               QStringLiteral("192.168.1.50 is shared with 3 programs"));
}

TEST_CASE("share wizard: device settings reach the share URI")
{
    vss::gui::FakeStudioBackend backend;
    backend.set_devices(standard_devices(3));
    vss::gui::ShareWizard wizard(backend);
    show(wizard);

    vss::serial_settings settings;
    settings.baud_rate = 9600;
    settings.data_bits = 7;
    settings.parity = vss::parity_mode::even;
    settings.stop_bits = vss::stop_bits_mode::two;
    settings.flow_control = vss::flow_control_mode::hardware;
    wizard.set_serial_settings(settings);
    REQUIRE_EQ(wizard.device_settings_summary(),
               QStringLiteral("9600 baud, 7 data bits, even parity, 2 stop "
                              "bits, hardware flow control (RTS and CTS)"));
    REQUIRE_EQ(wizard.source_uri(),
               QStringLiteral("serial://COM3?baud=9600&data=7&parity=even&"
                              "stop=2&flow=hardware"));
}
