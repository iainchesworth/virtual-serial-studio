//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_SELECTION_SERVICE_HPP
#define VSS_GUI_SHELL_SELECTION_SERVICE_HPP

#include <vss/core/types.hpp>

#include <QObject>

namespace vss::gui
{

/// The one selected port and link, shared by Ports, Monitor and Topology.
class SelectionService final : public QObject
{
    Q_OBJECT

public:
    explicit SelectionService(QObject* parent = nullptr) : QObject(parent) {}

    vss::endpoint_id port() const noexcept { return port_; }
    vss::link_id link() const noexcept { return link_; }

    void select_port(vss::endpoint_id id)
    {
        if (port_ == id)
            return;
        port_ = id;
        Q_EMIT port_changed(id);
    }

    void select_link(vss::link_id id)
    {
        if (link_ == id)
            return;
        link_ = id;
        Q_EMIT link_changed(id);
    }

    void clear()
    {
        select_port(0);
        select_link(0);
    }

Q_SIGNALS:
    void port_changed(vss::endpoint_id id);
    void link_changed(vss::link_id id);

private:
    vss::endpoint_id port_ = 0;
    vss::link_id link_ = 0;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_SELECTION_SERVICE_HPP
