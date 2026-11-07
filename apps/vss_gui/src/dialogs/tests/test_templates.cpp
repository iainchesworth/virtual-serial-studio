//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "dialogs/recent_files.hpp"
#include "dialogs/templates.hpp"
#include "dialogs/templates_dialog.hpp"
#include "widgets/notes.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

namespace
{

void show(QDialog& dialog)
{
    dialog.show();
    QApplication::processEvents();
}

QString temp_json(const QTemporaryDir& directory, const QString& name)
{
    return QDir(directory.path()).filePath(name);
}

} // namespace

TEST_CASE("templates: every bundled setup loads and validates")
{
    const auto templates = vss::gui::bundled_templates();
    REQUIRE_EQ(templates.size(), 4);
    for (const auto& entry : templates)
    {
        const auto document = vss::gui::load_bundled_template(entry.id);
        REQUIRE(document.has_value());
        REQUIRE(!vss::config::validate(*document));
        REQUIRE_EQ(static_cast<int>(document->ports.size()), entry.ports);
        REQUIRE_EQ(static_cast<int>(document->links.size()), entry.links);
        REQUIRE(!entry.title.isEmpty());
        REQUIRE(!entry.description.isEmpty());
        REQUIRE(!entry.level.isEmpty());

        const auto json = vss::config::to_json(*document);
        REQUIRE(json.has_value());
        const auto round_trip = vss::config::from_json(*json);
        REQUIRE(round_trip.has_value());
        REQUIRE(*round_trip == *document);
    }

    REQUIRE(!vss::gui::load_bundled_template(QStringLiteral("missing"))
                 .has_value());
}

TEST_CASE("templates: the four cards carry the design copy")
{
    QSettings().clear();
    vss::gui::FakeStudioBackend backend;
    vss::gui::TemplatesDialog dialog(backend);
    show(dialog);
    REQUIRE_EQ(dialog.template_count(), 4);
    REQUIRE_EQ(dialog.template_title(0),
               QStringLiteral("Test bench, no hardware"));
    REQUIRE_EQ(dialog.template_title(1),
               QStringLiteral("Share a device with three programs"));
    REQUIRE_EQ(dialog.template_title(2),
               QStringLiteral("Device on the network"));
    REQUIRE_EQ(dialog.template_title(3),
               QStringLiteral("Two programs, one device, one at a time"));
    REQUIRE_EQ(dialog.template_meta(0),
               QStringLiteral("Basic, 2 ports, 1 link"));
    REQUIRE_EQ(dialog.template_meta(3),
               QStringLiteral("Advanced, 3 ports, 1 link"));
    REQUIRE(dialog.template_meta(1).contains(
        QStringLiteral("Needs a real device")));
    REQUIRE_EQ(dialog.current_template(), 0);
    REQUIRE(dialog.primary_button()->isEnabled());
}

TEST_CASE("templates: a real-device setup requires the Attach flow")
{
    QSettings().clear();
    vss::gui::FakeStudioBackend backend;
    backend.set_devices({});
    vss::gui::TemplatesDialog dialog(backend);
    show(dialog);
    dialog.select_template(1);
    REQUIRE(dialog.requires_device_selected());
    REQUIRE(!dialog.primary_button()->isEnabled());
    REQUIRE(!dialog.use_current());
    REQUIRE(dialog.error_visible());
    REQUIRE_EQ(dialog.error_title(),
               QStringLiteral("This setup needs a real device"));
    REQUIRE(
        dialog.error_body().contains(QStringLiteral("Attach a real device")));
    REQUIRE(!dialog.result_document().has_value());
}

TEST_CASE("templates: using the test bench returns an unsaved setup")
{
    QSettings().clear();
    vss::gui::FakeStudioBackend backend;
    vss::gui::TemplatesDialog dialog(backend);
    show(dialog);
    dialog.select_template(0);
    REQUIRE(dialog.use_current());
    REQUIRE(dialog.result_document().has_value());
    REQUIRE(dialog.result_is_template());
    REQUIRE(dialog.result_path().isEmpty());
    REQUIRE_EQ(dialog.result_title(),
               QStringLiteral("Test bench, no hardware"));
    REQUIRE_EQ(static_cast<int>(dialog.result_document()->ports.size()), 2);
    REQUIRE_EQ(static_cast<int>(dialog.result_document()->links.size()), 1);
}

TEST_CASE("templates: search filters cards and the Recent list")
{
    QSettings().clear();
    vss::gui::FakeStudioBackend backend;
    vss::gui::TemplatesDialog dialog(backend);
    show(dialog);

    dialog.set_search(QStringLiteral("owner"));
    REQUIRE(dialog.current_template() == 0);
    REQUIRE(!dialog.primary_button()->isEnabled());
    dialog.select_template(3);
    REQUIRE_EQ(dialog.current_template(), 3);
    REQUIRE(dialog.primary_button()->isEnabled());
    REQUIRE(dialog.use_current());
    REQUIRE_EQ(vss_test::require_value(dialog.result_document()).name,
               "two-programs-owner");
}

TEST_CASE("templates: open and save use the vss_config document")
{
    QSettings().clear();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    const auto source =
        vss::gui::load_bundled_template(QStringLiteral("test-bench"));
    REQUIRE(source.has_value());
    const QString source_path =
        temp_json(directory, QStringLiteral("bench.json"));
    REQUIRE(
        vss::config::save_file(source_path.toStdString(), *source).has_value());

    vss::gui::FakeStudioBackend backend;
    {
        vss::gui::TemplatesDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.open_file(source_path));
        REQUIRE(!dialog.result_is_template());
        REQUIRE_EQ(dialog.result_path(), source_path);
        REQUIRE(dialog.result_document().has_value());
    }

    const auto port = backend.create_virtual_port(
        QStringLiteral("terminal"), vss::virtual_port_kind::pty, true);
    REQUIRE(port.has_value());

    const QString saved_path =
        temp_json(directory, QStringLiteral("saved-template.json"));
    {
        vss::gui::TemplatesDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.save_current_template(saved_path));
        REQUIRE(dialog.status_visible());
        REQUIRE_EQ(dialog.status_text(),
                   QStringLiteral("Saved saved-template.json."));
        const auto loaded = vss::config::load_file(saved_path.toStdString());
        REQUIRE(loaded.has_value());
        REQUIRE_EQ(static_cast<int>(loaded->ports.size()), 1);
    }
    REQUIRE(QFile::exists(saved_path));

    vss::gui::TemplatesDialog recent(backend);
    show(recent);
    recent.set_tab(1);
    REQUIRE_EQ(recent.tab(), 1);
    REQUIRE_EQ(recent.recent_count(), 1);
    REQUIRE_EQ(recent.recent_name(0), QStringLiteral("saved-template.json"));
    REQUIRE(!recent.primary_button()->isEnabled());
}

TEST_CASE("templates: an invalid file fails visibly and keeps the dialog")
{
    QSettings().clear();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = temp_json(directory, QStringLiteral("broken.json"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{ \"ports\": ");
    file.close();

    vss::gui::FakeStudioBackend backend;
    vss::gui::TemplatesDialog dialog(backend);
    show(dialog);
    REQUIRE(!dialog.open_file(path));
    REQUIRE(dialog.error_visible());
    REQUIRE_EQ(dialog.error_title(),
               QStringLiteral("Could not open broken.json"));
    REQUIRE(!dialog.result_document().has_value());
}
