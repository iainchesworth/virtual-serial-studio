//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_TOPOLOGY_LINKS_LIST_HPP
#define VSS_GUI_TOPOLOGY_LINKS_LIST_HPP

#include "backend/view_types.hpp"

#include <vss/core/types.hpp>

#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QListWidget;

namespace vss::gui::topology
{

/// The optional "Show links as a list" view.
///
/// QGraphicsItem content is invisible to screen readers, so every link is
/// also exposed here with its names and state, selectable with the keyboard
/// and kept in sync with the shared selection.
class LinksList final : public QWidget
{
    Q_OBJECT

public:
    explicit LinksList(QWidget* parent = nullptr);

    void set_links(const std::vector<link_view>& links,
                   const std::vector<port_view>& ports);
    void set_selected(vss::link_id id);
    vss::link_id selected() const;

    /// The number of real links (the empty-state row is not one).
    int count() const noexcept { return count_; }
    QStringList item_texts() const;

Q_SIGNALS:
    void link_selected(vss::link_id id);

private:
    QListWidget* list_ = nullptr;
    int count_ = 0;
    bool updating_ = false;
};

} // namespace vss::gui::topology

#endif // VSS_GUI_TOPOLOGY_LINKS_LIST_HPP
