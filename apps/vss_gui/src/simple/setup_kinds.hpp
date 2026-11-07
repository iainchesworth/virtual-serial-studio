//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "backend/view_types.hpp"
#include "shell/setup_group.hpp"

#include <QString>
#include <QVector>

#include <optional>
#include <vector>

namespace vss::gui::simple
{

/// The jobs Simple mode offers, and `custom` for a setup no job describes.
enum class job
{
    port,
    pair,
    attach,
    share,
    talk,
    network,
    bus,
    combine,
    turns,
    custom
};

/// One setup as Simple mode shows it, read back from the ports and links so a
/// setup made in Advanced mode or loaded from a file shows the same way.
struct setup_view
{
    job kind = job::custom;
    setup_group group;
    /// The serial device: a real device, or a network device Studio reaches.
    vss::endpoint_id device = 0;
    /// The virtual ports programs open, in link order.
    QVector<vss::endpoint_id> programs;
    /// Bus members and senders into one, in link order (programs or devices).
    QVector<vss::endpoint_id> members;
    /// The receiver of "send several into one".
    vss::endpoint_id receiver = 0;
    /// The echoing fixture on the other end of a lone program port.
    vss::endpoint_id echo = 0;
    /// The network port other computers connect to.
    vss::endpoint_id listener = 0;
    /// The one link of the setup, or 0.
    vss::link_id link = 0;
    /// The program that sends now, when programs take turns.
    vss::endpoint_id owner = 0;

    /// The first port of the group: what identifies the setup between two
    /// snapshots.
    vss::endpoint_id key() const
    {
        return group.ports.isEmpty() ? 0 : group.ports.front();
    }
};

/// Every setup in the studio, in the order of their first port.
std::vector<setup_view> list_setups(const std::vector<port_view>& ports,
                                    const std::vector<link_view>& links);

/// The setup that holds `group`.
setup_view describe_setup(const setup_group& group,
                          const std::vector<port_view>& ports,
                          const std::vector<link_view>& links);

/// The label a program port carries, or empty when its name is one Studio
/// made up (program-2, port-1 and the like).
QString program_label(const port_view& port);

/// The name shown for a port: its path where it has one, otherwise its name.
QString port_shown_name(const port_view& port);

/// The title of a setup, such as "COM3 shared with 2 programs".
QString setup_title(const setup_view& setup,
                    const std::vector<port_view>& ports);

/// The job's title, as on its tile and its form.
QString job_title(job kind);

/// The job's one- or two-sentence description.
QString job_description(job kind);

/// The ports a program must open, for the list rows and the steps.
QStringList program_names(const setup_view& setup,
                          const std::vector<port_view>& ports);

} // namespace vss::gui::simple
