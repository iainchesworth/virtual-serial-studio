//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "main_window.hpp"

#include "inspector_panel.hpp"
#include "monitor_model.hpp"
#include "port_model.hpp"
#include "send_panel.hpp"
#include "theme.hpp"
#include "topology_view.hpp"

#include <vss/config/document.hpp>
#include <vss/studio/studio.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QStringList>
#include <QTableView>
#include <QThread>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace vss::gui {
namespace {

void report(const QString& text)
{
    std::fprintf(stderr, "selftest: %s\n", text.toUtf8().constData());
}

const vss::endpoint_info* lookup(
    const std::vector<vss::endpoint_info>& endpoints, std::string_view name)
{
    for (const auto& info : endpoints)
    {
        if (info.name == name)
            return &info;
    }
    return nullptr;
}

} // namespace

bool MainWindow::run_selftest()
{
    QStringList failures;
    const auto fail = [&failures](const QString& text) {
        failures << text;
        report(QStringLiteral("FAIL: %1").arg(text));
    };

    const auto finish = [this, &failures]() -> bool {
        if (failures.isEmpty())
            return true;
        report(QStringLiteral("endpoint snapshot:\n")
               + describe_endpoints(studio_.endpoint_infos()));
        for (const auto& link : studio_.link_infos())
        {
            report(QStringLiteral("link #%1: %2 source(s), %3 target(s), "
                                  "%4 bytes, %5 message(s)")
                       .arg(static_cast<qulonglong>(link.id))
                       .arg(static_cast<int>(link.sources.size()))
                       .arg(static_cast<int>(link.targets.size()))
                       .arg(format_count(link.stats.bytes))
                       .arg(format_count(link.stats.messages)));
        }
        return false;
    };

    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
    {
        fail(QStringLiteral("expected the offscreen platform, got \"%1\"")
                 .arg(QGuiApplication::platformName()));
        return finish();
    }

    // Show the real widget tree on the offscreen plugin: layout and painting
    // are then part of the exercised path, not just model construction.
    show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    // Start from a clean studio even if the caller pre-populated one.
    for (const auto& info : studio_.endpoint_infos())
        static_cast<void>(studio_.remove_port(info.id));
    refresh_now();

    if (port_model_->rowCount() != 0 || topology_->node_count() != 0
        || topology_->link_count() != 0)
    {
        fail(QStringLiteral("the initial topology was not empty"));
    }

    const std::string payload = "vss selftest payload 0123456789";
    const auto first = studio_.create_virtual_port(
        "selftest-a", vss::virtual_port_kind::loopback);
    if (!first)
    {
        fail(QStringLiteral("create_virtual_port(selftest-a): %1")
                 .arg(error_text(first.error())));
        return finish();
    }
    const auto second = studio_.create_virtual_port(
        "selftest-b", vss::virtual_port_kind::loopback);
    if (!second)
    {
        fail(QStringLiteral("create_virtual_port(selftest-b): %1")
                 .arg(error_text(second.error())));
        static_cast<void>(studio_.remove_port((*first)->id()));
        return finish();
    }

    const auto link = studio_.redirect((*first)->name(), (*second)->name());
    if (!link)
    {
        fail(QStringLiteral("redirect(selftest-a, selftest-b): %1")
                 .arg(error_text(link.error())));
        static_cast<void>(studio_.remove_port((*first)->id()));
        static_cast<void>(studio_.remove_port((*second)->id()));
        return finish();
    }

    refresh_now();

    if (port_model_->rowCount() != 2)
        fail(QStringLiteral("port model shows %1 rows, expected 2")
                 .arg(port_model_->rowCount()));
    if (topology_->node_count() != 2)
        fail(QStringLiteral("topology shows %1 nodes, expected 2")
                 .arg(topology_->node_count()));
    if (topology_->link_count() != 1)
        fail(QStringLiteral("topology shows %1 links, expected 1")
                 .arg(topology_->link_count()));
    if (send_panel_->open_endpoint_count() != 2)
        fail(QStringLiteral("send panel sees %1 open endpoints, expected 2")
                 .arg(send_panel_->open_endpoint_count()));

    const vss::endpoint_id first_id = (*first)->id();
    const vss::endpoint_id second_id = (*second)->id();

    (*first)->write(vss::as_bytes(payload));

    bool flowed = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        refresh_now();

        const auto endpoints = studio_.endpoint_infos();
        const auto* a = lookup(endpoints, "selftest-a");
        const auto* b = lookup(endpoints, "selftest-b");
        const auto links = studio_.link_infos();
        const bool link_flowed =
            !links.empty() && links.front().stats.bytes >= payload.size();
        if (a != nullptr && b != nullptr
            && a->stats.bytes_tx >= payload.size()
            && a->stats.bytes_rx >= payload.size()
            && b->stats.bytes_rx >= payload.size() && link_flowed)
        {
            flowed = true;
            break;
        }
        QThread::msleep(5);
    }

    if (!flowed)
    {
        const auto endpoints = studio_.endpoint_infos();
        const auto* a = lookup(endpoints, "selftest-a");
        const auto* b = lookup(endpoints, "selftest-b");
        const auto links = studio_.link_infos();
        fail(QStringLiteral(
                 "payload did not flow within 3000 ms (a tx=%1 rx=%2, "
                 "b rx=%3, link bytes=%4)")
                 .arg(a ? format_count(a->stats.bytes_tx) : QStringLiteral("?"))
                 .arg(a ? format_count(a->stats.bytes_rx) : QStringLiteral("?"))
                 .arg(b ? format_count(b->stats.bytes_rx) : QStringLiteral("?"))
                 .arg(links.empty()
                          ? QStringLiteral("?")
                          : format_count(links.front().stats.bytes)));
    }
    else
    {
        report(QStringLiteral(
                   "data flowed: %1 bytes written, %2 bytes delivered, "
                   "%3 link bytes")
                   .arg(static_cast<qulonglong>(payload.size()))
                   .arg(static_cast<qulonglong>(payload.size()))
                   .arg(static_cast<qulonglong>(studio_.link_infos().front()
                                                    .stats.bytes)));
    }

    if (monitor_model_->rowCount() <= 0)
    {
        fail(QStringLiteral("monitor shows no traffic rows"));
    }
    else
    {
        const QString ascii_preview = monitor_model_
                                          ->data(monitor_model_->index(
                                                     0, MonitorModel::PreviewColumn),
                                                 Qt::DisplayRole)
                                          .toString();
        if (ascii_preview.isEmpty())
            fail(QStringLiteral("monitor preview is empty"));

        monitor_model_->set_hex_view(true);
        const QString hex_preview = monitor_model_
                                        ->data(monitor_model_->index(
                                                   0, MonitorModel::PreviewColumn),
                                               Qt::DisplayRole)
                                        .toString();
        if (!ascii_preview.isEmpty() && ascii_preview == hex_preview)
            fail(QStringLiteral("hex/ASCII toggle did not change the preview"));
        monitor_model_->set_hex_view(false);
    }

    // Selecting through the table must drive the inspector and the topology,
    // exactly as a mouse click does.
    const QModelIndex second_index = port_model_->index_for_id(second_id);
    if (!second_index.isValid())
        fail(QStringLiteral("port model lost selftest-b"));
    else
        ports_view_->selectRow(second_index.row());

    if (inspector_->endpoint_id() != second_id)
        fail(QStringLiteral("inspector did not follow the table selection"));
    if (topology_->selected_endpoint() != second_id)
        fail(QStringLiteral("topology did not follow the table selection"));

    // A loopback transport models the control lines; driving one must succeed
    // and be visible in the next snapshot (the inspector renders that data).
    if (const auto line_result =
            (*second)->set_line(vss::control_line::dtr, true))
    {
        fail(QStringLiteral("set_line(DTR, true) failed: %1")
                 .arg(error_text(line_result)));
    }
    else
    {
        refresh_now();
        const auto endpoints = studio_.endpoint_infos();
        const auto* b = lookup(endpoints, "selftest-b");
        if (b == nullptr || !b->lines.dtr())
            fail(QStringLiteral("DTR did not appear in the endpoint snapshot"));
    }

    // The document produced by snapshot() must validate, serialise and parse.
    const auto document = studio_.snapshot();
    if (document.ports.size() != 2 || document.links.size() != 1)
    {
        fail(QStringLiteral("snapshot has %1 ports and %2 links, expected 2/1")
                 .arg(static_cast<int>(document.ports.size()))
                 .arg(static_cast<int>(document.links.size())));
    }
    const auto json = vss::config::to_json(document);
    if (!json)
    {
        fail(QStringLiteral("to_json failed: %1")
                 .arg(error_text(json.error())));
    }
    else
    {
        const auto parsed = vss::config::from_json(*json);
        if (!parsed)
        {
            fail(QStringLiteral("from_json failed: %1")
                     .arg(error_text(parsed.error())));
        }
        else if (parsed->ports.size() != document.ports.size()
                 || parsed->links.size() != document.links.size())
        {
            fail(QStringLiteral("JSON round-trip changed the topology"));
        }
    }

    const int ports_before_cleanup = port_model_->rowCount();
    const int links_before_cleanup = topology_->link_count();

    static_cast<void>(studio_.remove_port(first_id));
    static_cast<void>(studio_.remove_port(second_id));
    refresh_now();

    if (port_model_->rowCount() != 0)
        fail(QStringLiteral("port model still shows %1 rows after removal")
                 .arg(port_model_->rowCount()));
    if (topology_->node_count() != 0 || topology_->link_count() != 0)
        fail(QStringLiteral("topology did not drop removed ports and links"));
    if (inspector_->endpoint_id() != 0)
        fail(QStringLiteral("inspector kept a removed endpoint"));

    if (!failures.isEmpty())
        return finish();

    report(QStringLiteral(
               "passed: %1 ports, %2 links, %3 monitor row(s), payload %4 "
               "bytes")
               .arg(ports_before_cleanup)
               .arg(links_before_cleanup)
               .arg(monitor_model_->rowCount())
               .arg(static_cast<qulonglong>(payload.size())));
    return true;
}

} // namespace vss::gui
