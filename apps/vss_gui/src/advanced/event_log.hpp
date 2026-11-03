//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_EVENT_LOG_HPP
#define VSS_GUI_ADVANCED_EVENT_LOG_HPP

#include "backend/view_types.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace vss::gui
{

/// The Advanced Log tab: state changes and errors since the setup was opened.
///
/// It is fed by the backend's coalesced batch on the GUI thread and keeps a
/// fixed row cap.  Traffic rows live in the Monitor, not here; the engine
/// publishes no drop event, so this log makes no claim about owner drops.
class EventLogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit EventLogPanel(QWidget* parent = nullptr);

    void append(const event_batch& events);
    /// A GUI-side event, for example "Applied the topology JSON.".
    void add_local(const QString& text);
    void clear();
    int row_count() const;

    QTableWidget* table() const noexcept { return table_; }
    QLabel* empty_label() const noexcept { return empty_; }

private:
    void add_row(const QString& time, const QString& port, const QString& event,
                 const QString& detail);
    void update_empty();

    QTableWidget* table_ = nullptr;
    QLabel* empty_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_EVENT_LOG_HPP
