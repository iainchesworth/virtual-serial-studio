//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "qt_wait.hpp"
#include "test_framework.hpp"

#include "backend/engine_studio_backend.hpp"
#include "backend/fake_studio_backend.hpp"
#include "flows/add_port_dialog.hpp"
#include "flows/attach_dialog.hpp"
#include "flows/pair_dialog.hpp"
#include "flows/port_ready_dialog.hpp"
#include "flows/watch_dialog.hpp"
#include "shell/launcher.hpp"
#include "shell/shell_window.hpp"
#include "theme/theme.hpp"
#include "widgets/chips.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"
#include "widgets/notes.hpp"

#include <vss/studio/studio.hpp>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace
{

using namespace vss::gui;

void clear_settings()
{
    QSettings settings;
    settings.clear();
}

void settle()
{
    QApplication::processEvents();
}

vss::gui::device_view make_device(const QString& path, bool com0com,
                                  bool virtual_device, bool in_use, int pair,
                                  const QString& side)
{
    vss::gui::device_view device;
    device.path = path;
    device.description = QStringLiteral("Serial device %1").arg(path);
    device.com0com = com0com;
    device.virtual_device = virtual_device;
    device.in_use = in_use;
    device.com0com_pair = pair;
    device.com0com_side = side;
    return device;
}

void use_two_free_pairs(vss::gui::FakeStudioBackend& backend)
{
    backend.set_devices({make_device(QStringLiteral("COM7"), true, true, false,
                                     1, QStringLiteral("A")),
                         make_device(QStringLiteral("COM8"), true, true, false,
                                     1, QStringLiteral("B")),
                         make_device(QStringLiteral("COM9"), true, true, false,
                                     2, QStringLiteral("A")),
                         make_device(QStringLiteral("COM10"), true, true, false,
                                     2, QStringLiteral("B"))});
    vss::gui::setup_facts facts;
    facts.com0com_found = true;
    facts.com0com_pairs = 2;
    facts.platform = QStringLiteral("windows");
    backend.set_facts(facts);
}

struct modal_probe
{
    QWidget* widget = nullptr;
    QString title;
    QStringList buttons;
    bool add_port = false;
    bool pair_dialog = false;
    bool attach_dialog = false;
    bool watch_dialog = false;
    bool error_dialog = false;
};

/// Closes the next modal dialog that opens. The closer cannot know when the
/// dialog will be up, so it looks again until one is, or the deadline passes.
/// A single look after a fixed delay misses the dialog on a slow machine, and
/// the test then waits in the dialog's event loop for ever.
void close_next_modal(modal_probe* probe = nullptr, int delay_ms = 10,
                      int remaining_ms = 30000)
{
    QTimer::singleShot(
        delay_ms,
        [probe, delay_ms, remaining_ms]
        {
            auto* modal = QApplication::activeModalWidget();
            if (modal == nullptr)
            {
                if (remaining_ms > delay_ms)
                    close_next_modal(probe, delay_ms, remaining_ms - delay_ms);
                return;
            }
            if (probe != nullptr)
            {
                probe->widget = modal;
                probe->title = modal->windowTitle();
                probe->add_port =
                    qobject_cast<vss::gui::AddPortDialog*>(modal) != nullptr;
                probe->pair_dialog =
                    qobject_cast<vss::gui::PairDialog*>(modal) != nullptr;
                probe->attach_dialog =
                    qobject_cast<vss::gui::AttachDialog*>(modal) != nullptr;
                probe->watch_dialog =
                    qobject_cast<vss::gui::WatchDialog*>(modal) != nullptr;
                probe->error_dialog =
                    qobject_cast<vss::gui::ErrorDialog*>(modal) != nullptr;
                for (auto* button : modal->findChildren<QPushButton*>())
                {
                    if (button->isVisibleTo(modal))
                        probe->buttons << button->text();
                }
            }
            modal->close();
        });
}

QPushButton* button_with_text(QWidget* parent, const QString& text)
{
    for (auto* button : parent->findChildren<QPushButton*>())
    {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

vss::gui::LauncherCard* launcher_card(ShellWindow& window, const QString& title)
{
    for (auto* card : window.findChildren<vss::gui::LauncherCard*>())
    {
        if (card->title() == title)
            return card;
    }
    return nullptr;
}

void activate_card(vss::gui::LauncherCard* card)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(card, &event);
}

} // namespace

TEST_CASE("flows: add port validates, suffixes and keeps every input")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto existing = backend.create_virtual_port(
        QStringLiteral("port-1"), vss::virtual_port_kind::loopback, true);
    REQUIRE(existing.has_value());

    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::linux);
    REQUIRE_EQ(dialog.primary_button()->text(), QStringLiteral("Create port"));
    REQUIRE_EQ(dialog.kind_combo()->count(), 3);

    dialog.name_edit()->setText(QStringLiteral("port-1"));
    dialog.count_spin()->setValue(33);
    REQUIRE_EQ(dialog.count_spin()->accessibleDescription(),
               QStringLiteral("Choose a number from 1 to 32."));
    dialog.primary_button()->click();
    REQUIRE(dialog.result() != QDialog::Accepted);
    REQUIRE_EQ(dialog.name_edit()->text(), QStringLiteral("port-1"));
    REQUIRE_EQ(dialog.count_spin()->value(), 33);
    REQUIRE_EQ(backend.ports().size(), 1);

    dialog.count_spin()->setValue(0);
    dialog.primary_button()->click();
    REQUIRE(dialog.result() != QDialog::Accepted);
    REQUIRE_EQ(dialog.count_spin()->value(), 0);

    dialog.count_spin()->setValue(2);
    REQUIRE(dialog.count_spin()->accessibleDescription().isEmpty());
    REQUIRE_EQ(dialog.name_edit()->text(), QStringLiteral("port-1"));
    REQUIRE(dialog.result_sentence()->accessibleName().contains(
        QStringLiteral("This creates 2 ports named port-1-2 and port-2.")));
    REQUIRE(
        dialog.result_sentence()->text().contains(QStringLiteral("port-1-2")));

    dialog.name_edit()->setText(QStringLiteral("serial"));
    REQUIRE(dialog.result_sentence()->accessibleName().contains(
        QStringLiteral("serial-1 and serial-2")));
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(dialog.created_ports().size(), 2);
    REQUIRE_EQ(dialog.created_kind_token(), QStringLiteral("auto"));

    const auto ports = backend.ports();
    const auto serial_1 = std::find_if(
        ports.begin(), ports.end(), [](const vss::gui::port_view& port)
        { return port.name == QStringLiteral("serial-1"); });
    const auto serial_2 = std::find_if(
        ports.begin(), ports.end(), [](const vss::gui::port_view& port)
        { return port.name == QStringLiteral("serial-2"); });
    REQUIRE(serial_1 != ports.end());
    REQUIRE(serial_2 != ports.end());
}

TEST_CASE("flows: add port switches mode, primary text and Advanced cards")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    use_two_free_pairs(backend);
    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::windows);

    REQUIRE(dialog.mode_card(vss::gui::AddPortDialog::mode::network_endpoint)
                ->isHidden());
    REQUIRE_EQ(dialog.primary_button()->text(), QStringLiteral("Create port"));

    dialog.set_mode(vss::gui::AddPortDialog::mode::linked_pair);
    REQUIRE_EQ(dialog.selected_mode(),
               vss::gui::AddPortDialog::mode::linked_pair);
    REQUIRE_EQ(dialog.primary_button()->text(), QStringLiteral("Create pair"));
    REQUIRE(dialog.primary_button()->isEnabled());

    dialog.set_mode(vss::gui::AddPortDialog::mode::real_device);
    REQUIRE_EQ(dialog.primary_button()->text(), QStringLiteral("Next"));
    REQUIRE(dialog.primary_button()->isEnabled());

    dialog.set_advanced(true);
    REQUIRE(!dialog.mode_card(vss::gui::AddPortDialog::mode::network_endpoint)
                 ->isHidden());
    dialog.set_mode(vss::gui::AddPortDialog::mode::network_endpoint);
    REQUIRE(!dialog.primary_button()->isEnabled());
    REQUIRE_EQ(dialog.primary_button()->text(), QStringLiteral("Create port"));
}

TEST_CASE("flows: add port exposes names, target sizes and keyboard dismissal")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    // Focus lands after the window system activates the window.
    static_cast<void>(vss_test::wait_until(
        [&dialog]
        { return QApplication::focusWidget() == dialog.name_edit(); }));
    REQUIRE_EQ(QApplication::focusWidget(), dialog.name_edit());

    for (auto* button : dialog.findChildren<QAbstractButton*>())
    {
        if (!button->isVisibleTo(&dialog))
            continue;
        REQUIRE(!button->accessibleName().isEmpty() ||
                !button->text().isEmpty());
        REQUIRE(button->height() >= 24 ||
                button->minimumSizeHint().height() >= 24 ||
                button->sizeHint().height() >= 24);
    }
    for (auto* field : dialog.findChildren<QLineEdit*>())
    {
        if (!field->isVisibleTo(&dialog))
            continue;
        // QSpinBox and QComboBox own an internal line edit; the labelled
        // field is the spin box or combo itself.
        if (qobject_cast<QAbstractSpinBox*>(field->parentWidget()) != nullptr ||
            qobject_cast<QComboBox*>(field->parentWidget()) != nullptr)
            continue;
        REQUIRE(!field->accessibleName().isEmpty());
    }

    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::dark);
    settle();
    REQUIRE(!dialog.grab().isNull());
    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::light);
    settle();

    auto* focus = QApplication::focusWidget();
    REQUIRE(focus != nullptr);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(focus, &escape);
    settle();
    REQUIRE_EQ(dialog.result(), QDialog::Rejected);
}

TEST_CASE("flows: cancel leaves the studio unchanged")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::linux);
    dialog.name_edit()->setText(QStringLiteral("cancelled"));
    dialog.show();
    settle();
    REQUIRE(dialog.cancel_button() != nullptr);
    dialog.cancel_button()->click();
    settle();
    REQUIRE_EQ(dialog.result(), QDialog::Rejected);
    REQUIRE(backend.ports().empty());
}

TEST_CASE("flows: enter triggers the primary action")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::linux);
    dialog.name_edit()->setText(QStringLiteral("enter-test"));
    dialog.show();
    settle();
    // Focus lands after the window system activates the window.
    static_cast<void>(vss_test::wait_until(
        [&dialog]
        { return QApplication::focusWidget() == dialog.name_edit(); }));
    REQUIRE_EQ(QApplication::focusWidget(), dialog.name_edit());

    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(dialog.name_edit(), &enter);
    settle();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(backend.ports().size(), 1);
}

TEST_CASE("flows: port ready copies, details and platform variants")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto created =
        backend.create_auto_port(QStringLiteral("port-1"), true);
    REQUIRE(created.has_value());
    const auto view = backend.port(*created);
    REQUIRE(view.has_value());

    vss::gui::PortReadyDialog dialog(backend, *created, QStringLiteral("auto"),
                                     false, vss::gui::platform_kind::linux);
    dialog.show();
    settle();
    REQUIRE_EQ(dialog.windowTitle(), view->path);
    REQUIRE(dialog.copy_button() != nullptr);
    REQUIRE(!dialog.details()->is_expanded());
    REQUIRE(dialog.findChild<vss::gui::NoteBanner*>(
                QStringLiteral("portReadyLinuxNote")) != nullptr);
    REQUIRE(dialog.details()->findChildren<vss::gui::CodeBlock*>().size() >= 2);

    dialog.copy_button()->click();
    QGuiApplication::clipboard()->clear();
    dialog.copy_button()->click();
    settle();
    REQUIRE_EQ(QGuiApplication::clipboard()->text(), view->path);
    bool copied_toast = false;
    for (auto* toast : dialog.findChildren<vss::gui::Toast*>())
    {
        if (toast->accessibleName() ==
            QStringLiteral("%1 copied.").arg(view->path))
            copied_toast = true;
        toast->dismiss();
    }
    REQUIRE(copied_toast);
    settle();

    clear_settings();
    vss::gui::PortReadyDialog advanced(backend, *created,
                                       QStringLiteral("auto"), true,
                                       vss::gui::platform_kind::linux);
    advanced.show();
    settle();
    REQUIRE(advanced.details()->is_expanded());
    bool json_found = false;
    bool command_found = false;
    for (auto* code : advanced.findChildren<vss::gui::CodeBlock*>())
    {
        if (code->text().contains(QStringLiteral("\"virtual_kind\": \"auto\"")))
            json_found = true;
        if (code->text() == QStringLiteral("vss_cli virtual --kind=auto"))
            command_found = true;
    }
    REQUIRE(json_found);
    REQUIRE(command_found);
    advanced.close();

    const auto loopback = backend.create_virtual_port(
        QStringLiteral("inside"), vss::virtual_port_kind::loopback, true);
    REQUIRE(loopback.has_value());
    vss::gui::PortReadyDialog studio_only(backend, *loopback,
                                          QStringLiteral("loopback"), false,
                                          vss::gui::platform_kind::windows);
    studio_only.show();
    settle();
    REQUIRE_EQ(studio_only.windowTitle(),
               QStringLiteral("inside is ready inside Studio"));
    REQUIRE(studio_only.copy_button() == nullptr);
    auto* warning = studio_only.findChild<vss::gui::NoteBanner*>(
        QStringLiteral("portReadyDriverNote"));
    REQUIRE(warning != nullptr);
    REQUIRE_EQ(warning->text(),
               QStringLiteral("Other programs cannot open this port. Install a "
                              "driver to create ports they can open."));
    REQUIRE(warning->action_button() != nullptr);
    REQUIRE_EQ(warning->action_button()->text(),
               QStringLiteral("Open setup check"));
    studio_only.close();
}

TEST_CASE("flows: port ready peer states respect the capability")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto created =
        backend.create_auto_port(QStringLiteral("port-1"), true);
    REQUIRE(created.has_value());

    vss::gui::capabilities caps;
    caps.peer_open = false;
    backend.set_capabilities(caps);
    backend.set_peer_state(*created, vss::gui::peer_state::not_open);
    {
        vss::gui::PortReadyDialog dialog(backend, *created,
                                         QStringLiteral("auto"), false,
                                         vss::gui::platform_kind::linux);
        dialog.show();
        settle();
        REQUIRE_EQ(dialog.peer_chip()->text(), QStringLiteral("Open"));
        REQUIRE(dialog.peer_text()->text().isEmpty());
        dialog.close();
    }

    caps.peer_open = true;
    backend.set_capabilities(caps);
    vss::gui::PortReadyDialog dialog(backend, *created, QStringLiteral("auto"),
                                     false, vss::gui::platform_kind::linux);
    dialog.show();
    settle();
    REQUIRE_EQ(dialog.peer_chip()->text(), QStringLiteral("Waiting"));
    REQUIRE(dialog.peer_text()->text().startsWith(
        QStringLiteral("No program has opened")));

    backend.set_peer_state(*created, vss::gui::peer_state::open);
    settle();
    REQUIRE_EQ(dialog.peer_chip()->text(), QStringLiteral("Program connected"));
    REQUIRE_EQ(dialog.peer_text()->text(),
               QStringLiteral("A program is using %1.")
                   .arg(backend.port(*created)->path));
}

TEST_CASE("flows: pair creates two ports and a null-modem link")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    use_two_free_pairs(backend);
    vss::gui::PairDialog dialog(backend, false,
                                vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    REQUIRE_EQ(dialog.panel()->suggested_first(), QStringLiteral("COM8"));
    REQUIRE_EQ(dialog.panel()->suggested_second(), QStringLiteral("COM10"));
    REQUIRE(dialog.primary_button()->isEnabled());
    REQUIRE(dialog.panel()->admin_note() != nullptr);
    REQUIRE(dialog.panel()->admin_note()->text().contains(
        QStringLiteral("does not ask for administrator permission")));
    REQUIRE(dialog.panel()->result_sentence()->accessibleName().contains(
        QStringLiteral("COM8 sends arrives at COM10")));

    dialog.panel()->pair_name_edit()->clear();
    dialog.primary_button()->click();
    REQUIRE(dialog.result() != QDialog::Accepted);
    REQUIRE(backend.ports().empty());

    dialog.panel()->pair_name_edit()->setText(QStringLiteral("pair-1"));
    dialog.panel()->first_port_edit()->setText(
        dialog.panel()->suggested_second());
    dialog.primary_button()->click();
    REQUIRE(dialog.result() != QDialog::Accepted);
    REQUIRE(dialog.panel()->first_port_edit()->accessibleDescription().contains(
        QStringLiteral("is already used. Try COM8.")));
    REQUIRE(backend.ports().empty());

    dialog.panel()->first_port_edit()->setText(QStringLiteral("COM8"));
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE(dialog.outcome().has_value());
    REQUIRE_EQ(backend.ports().size(), 2);
    REQUIRE_EQ(backend.links().size(), 1);
    REQUIRE_EQ(backend.links().front().shape, vss::gui::link_shape::two_way);
    REQUIRE(backend.links().front().options.emulate_modem_lines);
    REQUIRE_EQ(dialog.outcome()->first_path,
               backend.port(dialog.outcome()->first)->path);
}

TEST_CASE("flows: pair degrades without a driver or free pairs")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::PairDialog dialog(backend, false,
                                vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    REQUIRE(!dialog.primary_button()->isEnabled());
    REQUIRE(dialog.panel()->first_port_edit()->isReadOnly());
    auto* note = dialog.panel()->driver_note();
    REQUIRE(note != nullptr);
    REQUIRE(!note->isHidden());
    REQUIRE_EQ(note->text(),
               QStringLiteral("Linked pairs need a driver. Open the setup "
                              "check."));

    vss::gui::FakeStudioBackend linux_backend;
    vss::gui::PairDialog linux_dialog(linux_backend, false,
                                      vss::gui::platform_kind::linux);
    REQUIRE(linux_dialog.primary_button()->isEnabled());
    REQUIRE(linux_dialog.panel()->first_port_edit()->isReadOnly());
    REQUIRE_EQ(linux_dialog.panel()->first_port_edit()->placeholderText(),
               QStringLiteral("Chosen when you create the pair"));
    linux_dialog.primary_button()->click();
    REQUIRE_EQ(linux_dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(linux_backend.ports().size(), 2);
    REQUIRE_EQ(linux_backend.links().size(), 1);
}

TEST_CASE("flows: pair failure rolls back and uses ErrorDialog")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    use_two_free_pairs(backend);
    vss::gui::PairDialog dialog(backend, false,
                                vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    backend.fail_next(vss::make_error_code(vss::errc::permission_denied));

    QString title;
    QObject::connect(&dialog, &vss::gui::PairDialog::error_shown,
                     [&title](const QString& value) { title = value; });
    modal_probe probe;
    close_next_modal(&probe);
    dialog.primary_button()->click();
    REQUIRE_EQ(title, QStringLiteral("Could not create the pair"));
    REQUIRE(probe.error_dialog);
    REQUIRE(probe.buttons.contains(QStringLiteral("Copy details")));
    REQUIRE(backend.ports().empty());
    REQUIRE(backend.links().empty());
    REQUIRE(dialog.result() != QDialog::Accepted);
}

TEST_CASE("flows: attach enumerates, explains rows and refreshes")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    backend.set_devices(
        {make_device(QStringLiteral("COM3"), false, false, false, -1, {}),
         make_device(QStringLiteral("COM4"), false, false, true, -1, {}),
         make_device(QStringLiteral("COM5"), true, true, false, 1,
                     QStringLiteral("A"))});
    vss::gui::AttachDialog dialog(backend, false,
                                  vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    REQUIRE_EQ(dialog.device_row_count(), 3);
    REQUIRE(dialog.device_radio(0) != nullptr);
    REQUIRE(dialog.device_radio(0)->isEnabled());
    REQUIRE(!dialog.device_radio(1)->isEnabled());
    REQUIRE(!dialog.device_radio(2)->isEnabled());
    REQUIRE(dialog.device_radio(1)->accessibleName().contains(
        QStringLiteral("Another program is using it.")));
    REQUIRE(dialog.device_radio(2)->accessibleName().contains(
        QStringLiteral("com0com pair end")));
    REQUIRE(dialog.address_block()->text() ==
            QStringLiteral("serial://COM3?baud=115200"));
    REQUIRE(!dialog.settings_disclosure()->is_expanded());
    REQUIRE(dialog.result_sentence()->accessibleName().contains(
        QStringLiteral("Studio will open COM3 at 115200 baud")));
    REQUIRE(dialog.result_sentence()->text().contains(
        QStringLiteral("<b>COM3</b>")));

    backend.set_devices(
        {make_device(QStringLiteral("COM6"), false, false, false, -1, {})});
    dialog.refresh_button()->click();
    settle();
    REQUIRE_EQ(dialog.device_row_count(), 1);
    REQUIRE(dialog.device_radio(0)->isEnabled());
    REQUIRE_EQ(dialog.address_block()->text(),
               QStringLiteral("serial://COM6?baud=115200"));
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE(dialog.logical_port() != 0);
    REQUIRE(dialog.physical_port() != 0);
    REQUIRE_EQ(backend.ports().size(), 2);
    REQUIRE_EQ(backend.links().size(), 1);
    REQUIRE_EQ(backend.links().front().shape, vss::gui::link_shape::split);
}

TEST_CASE("flows: attach empty state accepts a manual path")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::AttachDialog dialog(backend, false,
                                  vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    REQUIRE(dialog.empty_note() != nullptr);
    REQUIRE(!dialog.empty_note()->isHidden());
    REQUIRE(!dialog.primary_button()->isEnabled());
    REQUIRE_EQ(dialog.address_block()->text(), QString());

    auto* manual =
        button_with_text(&dialog, QStringLiteral("Enter a path instead"));
    REQUIRE(manual != nullptr);
    manual->click();
    settle();
    dialog.manual_path_edit()->setText(QStringLiteral("COM9"));
    settle();
    REQUIRE(dialog.primary_button()->isEnabled());
    REQUIRE_EQ(dialog.address_block()->text(),
               QStringLiteral("serial://COM9?baud=115200"));
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(backend.ports().size(), 2);
    REQUIRE_EQ(backend.links().size(), 1);
}

TEST_CASE("flows: attach advanced address wins over the settings fields")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    backend.set_devices(
        {make_device(QStringLiteral("COM3"), false, false, false, -1, {})});
    vss::gui::AttachDialog dialog(backend, true,
                                  vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    auto* edit = button_with_text(&dialog, QStringLiteral("Edit address"));
    REQUIRE(edit != nullptr);
    edit->click();
    settle();
    REQUIRE(!dialog.address_edit()->isHidden());
    dialog.address_edit()->setText(QStringLiteral("serial://COM3?baud=9600"));
    settle();
    REQUIRE(dialog.result_sentence()->accessibleName().contains(
        QStringLiteral("at 9600 baud")));
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    const auto physical = backend.port(dialog.physical_port());
    REQUIRE(physical.has_value());
    REQUIRE_EQ(physical->serial.baud_rate, 9600u);
}

TEST_CASE("flows: attach failure offers recovery and changes nothing")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    backend.set_devices(
        {make_device(QStringLiteral("COM3"), false, false, false, -1, {})});
    vss::gui::AttachDialog dialog(backend, false,
                                  vss::gui::platform_kind::windows);
    dialog.show();
    settle();
    backend.fail_next(vss::make_error_code(vss::errc::device_busy));
    QString title;
    QObject::connect(&dialog, &vss::gui::AttachDialog::error_shown,
                     [&title](const QString& value) { title = value; });
    modal_probe probe;
    close_next_modal(&probe);
    dialog.primary_button()->click();
    REQUIRE_EQ(title, QStringLiteral("Could not open COM3"));
    REQUIRE(probe.error_dialog);
    REQUIRE(probe.buttons.contains(QStringLiteral("Try again")));
    REQUIRE(probe.buttons.contains(QStringLiteral("Choose another")));
    REQUIRE(probe.buttons.contains(QStringLiteral("Copy details")));
    REQUIRE(backend.ports().empty());
    REQUIRE(dialog.result() != QDialog::Accepted);
}

TEST_CASE("flows: watch creates a loopback watcher and monitor link")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto device =
        backend.create_auto_port(QStringLiteral("device"), true);
    const auto terminal =
        backend.create_auto_port(QStringLiteral("terminal"), true);
    REQUIRE(device.has_value());
    REQUIRE(terminal.has_value());

    vss::gui::WatchDialog dialog(backend, *terminal,
                                 vss::gui::platform_kind::linux);
    dialog.show();
    settle();
    REQUIRE_EQ(dialog.watcher_name_edit()->text(),
               QStringLiteral("terminal-monitor"));
    REQUIRE(dialog.open_monitor_check()->isChecked());
    REQUIRE(dialog.primary_button()->isEnabled());
    REQUIRE(dialog.primary_button()->text() ==
            QStringLiteral("Start watching"));

    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(dialog.source_id(), *terminal);
    REQUIRE(dialog.watcher_id() != 0);
    REQUIRE(dialog.link_id() != 0);
    REQUIRE_EQ(backend.ports().size(), 3);
    REQUIRE_EQ(backend.links().size(), 1);
    REQUIRE_EQ(backend.links().front().shape, vss::gui::link_shape::monitor);
    REQUIRE(!backend.links().front().options.emulate_modem_lines);
    REQUIRE(backend.port(dialog.watcher_id())->studio_only);
}

TEST_CASE("flows: watch failure rolls back and hide when links are unavailable")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto source =
        backend.create_auto_port(QStringLiteral("device"), true);
    REQUIRE(source.has_value());
    backend.fail_next(vss::make_error_code(vss::errc::io_error));
    vss::gui::WatchDialog dialog(backend, *source,
                                 vss::gui::platform_kind::linux);
    dialog.show();
    settle();
    QString title;
    QObject::connect(&dialog, &vss::gui::WatchDialog::error_shown,
                     [&title](const QString& value) { title = value; });
    modal_probe probe;
    close_next_modal(&probe);
    dialog.primary_button()->click();
    REQUIRE(title.startsWith(QStringLiteral("Could not watch")));
    REQUIRE(probe.error_dialog);
    REQUIRE_EQ(backend.ports().size(), 1);
    REQUIRE(backend.links().empty());
    dialog.close();

    vss::gui::capabilities caps;
    caps.links = false;
    backend.set_capabilities(caps);
    vss::gui::WatchDialog unavailable(backend, *source,
                                      vss::gui::platform_kind::linux);
    unavailable.show();
    settle();
    REQUIRE(!unavailable.primary_button()->isEnabled());
    REQUIRE(!unavailable.unavailable_note()->isHidden());
    REQUIRE_EQ(unavailable.unavailable_note()->text(),
               QStringLiteral("Watching needs link support in this build."));
}

TEST_CASE("flows: every dialog passes the accessible-name and target walk")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    use_two_free_pairs(backend);
    backend.set_devices(
        {make_device(QStringLiteral("COM3"), false, false, false, -1, {}),
         make_device(QStringLiteral("COM7"), true, true, false, 1,
                     QStringLiteral("A")),
         make_device(QStringLiteral("COM8"), true, true, false, 1,
                     QStringLiteral("B")),
         make_device(QStringLiteral("COM9"), true, true, false, 2,
                     QStringLiteral("A")),
         make_device(QStringLiteral("COM10"), true, true, false, 2,
                     QStringLiteral("B"))});
    const auto port = backend.create_auto_port(QStringLiteral("port-1"), true);
    const auto terminal =
        backend.create_auto_port(QStringLiteral("terminal"), true);
    REQUIRE(port.has_value());
    REQUIRE(terminal.has_value());

    QVector<QWidget*> dialogs;
    auto* add = new vss::gui::AddPortDialog(
        backend, true, vss::gui::AddPortDialog::mode::virtual_port,
        vss::gui::platform_kind::windows);
    auto* pair = new vss::gui::PairDialog(backend, true,
                                          vss::gui::platform_kind::windows);
    auto* attach = new vss::gui::AttachDialog(backend, true,
                                              vss::gui::platform_kind::windows);
    auto* watch = new vss::gui::WatchDialog(backend, *terminal,
                                            vss::gui::platform_kind::linux);
    auto* ready =
        new vss::gui::PortReadyDialog(backend, *port, QStringLiteral("auto"),
                                      true, vss::gui::platform_kind::linux);
    dialogs << add << pair << attach << watch << ready;
    for (auto* dialog : dialogs)
    {
        dialog->show();
        settle();
        for (auto* button : dialog->findChildren<QAbstractButton*>())
        {
            if (!button->isVisibleTo(dialog))
                continue;
            REQUIRE(!button->accessibleName().isEmpty() ||
                    !button->text().isEmpty());
            REQUIRE(button->height() >= 24 ||
                    button->minimumSizeHint().height() >= 24 ||
                    button->sizeHint().height() >= 24);
        }
        for (auto* field : dialog->findChildren<QWidget*>())
        {
            if (!field->isVisibleTo(dialog) ||
                field->focusPolicy() == Qt::NoFocus)
                continue;
            if (qobject_cast<QAbstractButton*>(field) != nullptr)
                continue;
            if (qobject_cast<QScrollArea*>(field) != nullptr)
                continue;
            if (qobject_cast<QAbstractSpinBox*>(field->parentWidget()) !=
                    nullptr ||
                qobject_cast<QComboBox*>(field->parentWidget()) != nullptr)
                continue;
            REQUIRE(!field->accessibleName().isEmpty());
        }
        dialog->close();
        delete dialog;
    }
}

TEST_CASE("flows: shell actions open the real dialogs")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::ShellWindow window(backend);
    window.show();
    settle();

    for (const auto& [id, member] :
         std::initializer_list<std::pair<const char*, bool modal_probe::*>>{
             {"ports.add", &modal_probe::add_port},
             {"ports.pair", &modal_probe::pair_dialog},
             {"ports.attach", &modal_probe::attach_dialog},
             {"topology.watch", &modal_probe::watch_dialog}})
    {
        QAction* action = window.findChild<QAction*>(QString::fromLatin1(id));
        REQUIRE(action != nullptr);
        modal_probe probe;
        close_next_modal(&probe);
        action->trigger();
        settle();
        REQUIRE(probe.widget != nullptr);
        REQUIRE(probe.*member);
    }

    auto* launcher = window.findChild<vss::gui::Launcher*>();
    REQUIRE(launcher != nullptr);
    auto* add_card =
        launcher_card(window, QStringLiteral("Give my program a serial "
                                             "port"));
    REQUIRE(add_card != nullptr);
    {
        modal_probe probe;
        close_next_modal(&probe);
        activate_card(add_card);
        settle();
        REQUIRE(probe.add_port);
    }

    auto* pair_card =
        launcher_card(window, QStringLiteral("Connect two programs"));
    REQUIRE(pair_card != nullptr);
    {
        modal_probe probe;
        close_next_modal(&probe);
        activate_card(pair_card);
        settle();
        REQUIRE(probe.add_port);
        REQUIRE(probe.widget != nullptr);
    }

    auto* watch_card = launcher_card(window, QStringLiteral("Watch traffic"));
    REQUIRE(watch_card != nullptr);
    {
        modal_probe probe;
        close_next_modal(&probe);
        activate_card(watch_card);
        settle();
        REQUIRE(probe.watch_dialog);
    }
    // The stack ShellWindow is destroyed without a closeEvent here: the Fake
    // backend emits ports_changed when AttachDialog refreshes its list, and
    // that would otherwise raise the real unsaved-changes prompt.
}

TEST_CASE("flows: add port uses the real engine adapter for a loopback")
{
    clear_settings();
    vss::studio studio;
    vss::gui::EngineStudioBackend backend(studio);
    vss::gui::AddPortDialog dialog(backend, false,
                                   vss::gui::AddPortDialog::mode::virtual_port,
                                   vss::gui::platform_kind::linux);
    dialog.name_edit()->setText(QStringLiteral("flow-smoke"));
    dialog.kind_combo()->setCurrentIndex(2);
    dialog.primary_button()->click();
    REQUIRE_EQ(dialog.result(), QDialog::Accepted);
    REQUIRE_EQ(dialog.created_ports().size(), 1);
    const auto created = backend.port(dialog.created_ports().first());
    REQUIRE(created.has_value());
    REQUIRE_EQ(created->name, QStringLiteral("flow-smoke"));
    REQUIRE(created->path.startsWith(QStringLiteral("loopback:")));
    REQUIRE(created->studio_only);
    const auto removed = backend.remove_port(created->id);
    REQUIRE(removed.has_value());
}

TEST_CASE("flows: review screenshots for both themes")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    use_two_free_pairs(backend);
    backend.set_devices(
        {make_device(QStringLiteral("COM3"), false, false, false, -1, {}),
         make_device(QStringLiteral("COM4"), false, false, true, -1, {}),
         make_device(QStringLiteral("COM7"), true, true, false, 1,
                     QStringLiteral("A")),
         make_device(QStringLiteral("COM8"), true, true, false, 1,
                     QStringLiteral("B")),
         make_device(QStringLiteral("COM9"), true, true, false, 2,
                     QStringLiteral("A")),
         make_device(QStringLiteral("COM10"), true, true, false, 2,
                     QStringLiteral("B"))});
    const auto port = backend.create_auto_port(QStringLiteral("port-1"), true);
    REQUIRE(port.has_value());

    const QString directory = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("../gui-shots"));
    REQUIRE(QDir().mkpath(directory));
    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::light);

    auto shoot = [&directory](QWidget& widget, const QString& name, bool dark)
    {
        widget.show();
        settle();
        if (dark)
            vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::dark);
        else
            vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::light);
        settle();
        REQUIRE(widget.grab().save(QDir(directory).filePath(
            QStringLiteral("%1-%2.png")
                .arg(name, dark ? QStringLiteral("dark")
                                : QStringLiteral("light")))));
        widget.close();
    };

    vss::gui::AddPortDialog add_virtual(
        backend, false, vss::gui::AddPortDialog::mode::virtual_port,
        vss::gui::platform_kind::windows);
    shoot(add_virtual, QStringLiteral("add-port"), false);
    shoot(add_virtual, QStringLiteral("add-port"), true);

    vss::gui::AddPortDialog add(backend, false,
                                vss::gui::AddPortDialog::mode::linked_pair,
                                vss::gui::platform_kind::windows);
    shoot(add, QStringLiteral("add-pair"), false);
    shoot(add, QStringLiteral("add-pair"), true);

    vss::gui::PairDialog pair(backend, false, vss::gui::platform_kind::windows);
    shoot(pair, QStringLiteral("pair"), false);
    shoot(pair, QStringLiteral("pair"), true);

    vss::gui::AttachDialog attach(backend, false,
                                  vss::gui::platform_kind::windows);
    shoot(attach, QStringLiteral("attach"), false);
    shoot(attach, QStringLiteral("attach"), true);

    vss::gui::WatchDialog watch(backend, *port, vss::gui::platform_kind::linux);
    shoot(watch, QStringLiteral("watch"), false);
    shoot(watch, QStringLiteral("watch"), true);

    vss::gui::PortReadyDialog ready(backend, *port, QStringLiteral("auto"),
                                    false, vss::gui::platform_kind::linux);
    shoot(ready, QStringLiteral("ready"), false);
    shoot(ready, QStringLiteral("ready"), true);

    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::light);
}
