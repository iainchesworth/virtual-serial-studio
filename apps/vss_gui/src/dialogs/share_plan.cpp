//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "dialogs/share_plan.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QObject>

#include <algorithm>

namespace vss::gui
{

QVector<com0com_pair_paths>
known_com0com_pairs(const std::vector<device_view>& devices)
{
    QHash<int, QStringList> by_pair;
    for (const device_view& device : devices)
    {
        if (!device.com0com || device.com0com_pair < 0 || device.path.isEmpty())
        {
            continue;
        }
        QStringList& ends = by_pair[device.com0com_pair];
        if (!ends.contains(device.path, Qt::CaseInsensitive))
            ends << device.path;
    }

    QList<int> pairs = by_pair.keys();
    std::sort(pairs.begin(), pairs.end());
    QVector<com0com_pair_paths> out;
    out.reserve(pairs.size());
    for (const int pair : pairs)
    {
        com0com_pair_paths entry;
        entry.pair = pair;
        entry.ends = by_pair.value(pair);
        out.push_back(entry);
    }
    return out;
}

QStringList free_com0com_pair_paths(const std::vector<device_view>& devices,
                                    const std::vector<port_view>& ports)
{
    QStringList claimed;
    for (const port_view& port : ports)
    {
        if (!port.path.isEmpty())
            claimed << port.path;
    }

    QStringList advertised;
    for (const com0com_pair_paths& pair : known_com0com_pairs(devices))
    {
        bool used = false;
        for (const QString& end : pair.ends)
        {
            for (const QString& path : claimed)
            {
                if (path.compare(end, Qt::CaseInsensitive) == 0)
                    used = true;
            }
        }
        if (used)
            continue;
        // The engine tries one end as the attached side and advertises the
        // other; report that second end.
        advertised << (pair.ends.size() >= 2 ? pair.ends.at(1)
                                             : pair.ends.first());
    }
    return advertised;
}

share_plan make_share_plan(int programs, int free_pairs, bool windows)
{
    share_plan plan;
    plan.programs = std::max(1, std::min(32, programs));
    plan.windows = windows;
    plan.free_pairs = windows ? std::max(0, free_pairs) : plan.programs;
    plan.openable =
        windows ? std::min(plan.programs, plan.free_pairs) : plan.programs;
    plan.studio_only = plan.programs - plan.openable;
    return plan;
}

share_plan make_driver_share_plan(int programs, int ports_held)
{
    share_plan plan;
    plan.programs = std::max(1, std::min(32, programs));
    plan.windows = true;
    plan.studio_driver = true;
    plan.driver_ports_held = std::max(0, ports_held);
    plan.free_pairs = 0;
    const int available =
        std::max(0, kStudioDriverUserLimit - plan.driver_ports_held);
    plan.openable = std::min(plan.programs, available);
    plan.studio_only = plan.programs - plan.openable;
    return plan;
}

QString ordinal_word(int value)
{
    // Each word is its own catalogue entry, so a language can inflect it to fit
    // the sentence around it. The context is "ordinal".
    static const char* const words[] = {
        QT_TRANSLATE_NOOP("ordinal", "first"),
        QT_TRANSLATE_NOOP("ordinal", "second"),
        QT_TRANSLATE_NOOP("ordinal", "third"),
        QT_TRANSLATE_NOOP("ordinal", "fourth"),
        QT_TRANSLATE_NOOP("ordinal", "fifth"),
        QT_TRANSLATE_NOOP("ordinal", "sixth"),
        QT_TRANSLATE_NOOP("ordinal", "seventh"),
        QT_TRANSLATE_NOOP("ordinal", "eighth"),
        QT_TRANSLATE_NOOP("ordinal", "ninth"),
        QT_TRANSLATE_NOOP("ordinal", "tenth"),
        QT_TRANSLATE_NOOP("ordinal", "eleventh"),
        QT_TRANSLATE_NOOP("ordinal", "twelfth"),
        QT_TRANSLATE_NOOP("ordinal", "thirteenth"),
        QT_TRANSLATE_NOOP("ordinal", "fourteenth"),
        QT_TRANSLATE_NOOP("ordinal", "fifteenth"),
        QT_TRANSLATE_NOOP("ordinal", "sixteenth"),
        QT_TRANSLATE_NOOP("ordinal", "seventeenth"),
        QT_TRANSLATE_NOOP("ordinal", "eighteenth"),
        QT_TRANSLATE_NOOP("ordinal", "nineteenth"),
        QT_TRANSLATE_NOOP("ordinal", "twentieth"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-first"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-second"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-third"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-fourth"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-fifth"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-sixth"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-seventh"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-eighth"),
        QT_TRANSLATE_NOOP("ordinal", "twenty-ninth"),
        QT_TRANSLATE_NOOP("ordinal", "thirtieth"),
        QT_TRANSLATE_NOOP("ordinal", "thirty-first"),
        QT_TRANSLATE_NOOP("ordinal", "thirty-second")};
    if (value < 1 || value > 32)
        return QString::number(value);
    return QCoreApplication::translate("ordinal", words[value - 1]);
}

QString share_warning_rich(const share_plan& plan)
{
    if (!plan.needs_warning())
        return {};

    QString headline;
    if (plan.openable <= 0)
    {
        headline = QObject::tr(
            "Studio cannot create a port another program can open.");
    }
    else
    {
        headline =
            QObject::tr("Only %1 of the %2 programs can open their port.")
                .arg(plan.openable)
                .arg(plan.programs);
    }

    QString body;
    if (plan.studio_driver)
    {
        body = plan.driver_ports_held > 0
                   ? QObject::tr("The Studio driver gives one user at most 16 "
                                 "ports, and Studio already holds %1.")
                         .arg(plan.driver_ports_held)
                   : QObject::tr("The Studio driver gives one user at most 16 "
                                 "ports.");
    }
    else if (plan.free_pairs == 1)
    {
        body = QObject::tr("Each outside program needs its own driver pair, "
                           "and Studio found 1 free pair.");
    }
    else if (plan.free_pairs == 0)
    {
        body = QObject::tr("Each outside program needs its own driver pair, "
                           "and Studio found no free pairs.");
    }
    else
    {
        body = QObject::tr("Each outside program needs its own driver pair, "
                           "and Studio found %1 free pairs.")
                   .arg(plan.free_pairs);
    }

    if (plan.openable == 0)
    {
        body += QLatin1Char(' ');
        body += QObject::tr(plan.studio_only == 1
                                ? "The port works only inside Studio."
                                : "The ports work only inside Studio.");
    }
    else if (plan.studio_only == 1)
    {
        body += QLatin1Char(' ');
        body += QObject::tr("The %1 port works only inside Studio.")
                    .arg(ordinal_word(plan.openable + 1));
    }
    else
    {
        body += QLatin1Char(' ');
        body += QObject::tr("The remaining %1 ports work only inside Studio.")
                    .arg(plan.studio_only);
    }

    return QStringLiteral("<b>%1</b> %2")
        .arg(headline.toHtmlEscaped(), body.toHtmlEscaped());
}

QString share_warning_plain(const share_plan& plan)
{
    if (!plan.needs_warning())
        return {};
    const QString rich = share_warning_rich(plan);
    QString plain = rich;
    plain.remove(QStringLiteral("<b>"));
    plain.remove(QStringLiteral("</b>"));
    return plain;
}

QStringList planned_share_paths(const share_plan& plan,
                                const QStringList& free_pair_paths)
{
    QStringList paths;
    for (int index = 0; index < plan.programs; ++index)
    {
        if (!plan.windows)
        {
            paths << QCoreApplication::translate("ShareWizard", "PTY");
            continue;
        }
        if (index < plan.openable && !plan.studio_driver &&
            index < free_pair_paths.size())
        {
            paths << free_pair_paths.at(index);
        }
        else if (index < plan.openable)
        {
            // The driver picks the COM number when it makes the port, and a
            // pair with no path to show yet is also "a free port".
            paths << QCoreApplication::translate("ShareWizard", "A free port");
        }
        else
        {
            paths << QStringLiteral("loopback:listener-%1").arg(index + 1);
        }
    }
    return paths;
}

} // namespace vss::gui
