//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_INSPECTOR_PANEL_HPP
#define VSS_GUI_INSPECTOR_PANEL_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/types.hpp>

#include <QWidget>

#include <cstdint>
#include <vector>

class QCheckBox;
class QGroupBox;
class QLabel;

namespace vss {
class studio;
}

namespace vss::gui {

/// Details, live counters, throughput and control lines for one endpoint.
///
/// The panel never runs on a hub thread: the main window hands it snapshots
/// and the elapsed time between them, so rates are pure GUI-side arithmetic.
class InspectorPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit InspectorPanel(vss::studio& studio, QWidget* parent = nullptr);

    void set_endpoint(vss::endpoint_id id);
    vss::endpoint_id endpoint_id() const noexcept { return id_; }

    /// Refreshes details and counters from a studio snapshot.
    void update_from(const std::vector<vss::endpoint_info>& endpoints,
                     double elapsed_seconds);

Q_SIGNALS:
    void status_message(const QString& text, bool error);

private:
    void apply_line(vss::control_line line, bool state);
    void clear();
    void populate(const vss::endpoint_info& info);
    void set_input_label(QLabel* label, const char* name, bool on);
    void set_rate_labels(double rx_rate, double tx_rate);

    vss::studio& studio_;
    vss::endpoint_id id_ = 0;

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* id_value_ = nullptr;
    QLabel* kind_value_ = nullptr;
    QLabel* path_value_ = nullptr;
    QLabel* description_value_ = nullptr;
    QLabel* serial_value_ = nullptr;
    QLabel* state_value_ = nullptr;
    QLabel* open_value_ = nullptr;

    QLabel* rx_total_ = nullptr;
    QLabel* tx_total_ = nullptr;
    QLabel* rx_rate_ = nullptr;
    QLabel* tx_rate_ = nullptr;
    QLabel* errors_value_ = nullptr;
    QLabel* operations_value_ = nullptr;

    QGroupBox* controls_group_ = nullptr;
    QLabel* controls_note_ = nullptr;
    QCheckBox* dtr_ = nullptr;
    QCheckBox* rts_ = nullptr;
    QCheckBox* break_ = nullptr;
    QLabel* cts_ = nullptr;
    QLabel* dsr_ = nullptr;
    QLabel* dcd_ = nullptr;
    QLabel* ri_ = nullptr;

    bool have_previous_ = false;
    vss::endpoint_stats previous_{};
};

} // namespace vss::gui

#endif // VSS_GUI_INSPECTOR_PANEL_HPP
