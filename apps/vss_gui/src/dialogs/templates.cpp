//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "dialogs/templates.hpp"

#include <QFile>
#include <QStringList>

// Q_INIT_RESOURCE must run at global scope so the generated symbol is found.
void vss_init_template_resources()
{
    Q_INIT_RESOURCE(templates);
}

namespace vss::gui
{

QVector<bundled_template> bundled_templates()
{
    return {
        {QStringLiteral("test-bench"),
         QStringLiteral("Test bench, no hardware"),
         QStringLiteral("Two Studio-only ports and a one-way link, so a fresh "
                        "PC can show the whole data path."),
         QStringLiteral("Basic"), 2, 1, false,
         QStringLiteral(":/templates/test-bench.json")},
        {QStringLiteral("share-three-programs"),
         QStringLiteral("Share a device with three programs"),
         QStringLiteral("One real device split to three ports, each ready for "
                        "its own program."),
         QStringLiteral("Intermediate"), 4, 1, true,
         QStringLiteral(":/templates/share-three-programs.json")},
        {QStringLiteral("device-on-the-network"),
         QStringLiteral("Device on the network"),
         QStringLiteral("A serial device bridged to a TCP port, both "
                        "directions visible in the monitor."),
         QStringLiteral("Intermediate"), 2, 1, true,
         QStringLiteral(":/templates/device-on-the-network.json")},
        {QStringLiteral("two-programs-owner"),
         QStringLiteral("Two programs, one device, one at a time"),
         QStringLiteral("Two programs joined into one device, with an owner so "
                        "writes never collide."),
         QStringLiteral("Advanced"), 3, 1, false,
         QStringLiteral(":/templates/two-programs-owner.json")},
    };
}

vss::result<vss::config::document> load_bundled_template(const QString& id)
{
    vss_init_template_resources();

    for (const bundled_template& entry : bundled_templates())
    {
        if (entry.id != id)
            continue;
        QFile file(entry.resource);
        if (!file.open(QIODevice::ReadOnly))
            return vss::make_error_code(vss::errc::not_found);
        const QByteArray contents = file.readAll();
        const auto document = vss::config::from_json(std::string_view(
            contents.constData(), static_cast<std::size_t>(contents.size())));
        if (!document)
            return document.error();
        return *document;
    }
    return vss::make_error_code(vss::errc::not_found);
}

} // namespace vss::gui
