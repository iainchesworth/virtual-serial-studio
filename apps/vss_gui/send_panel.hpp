//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SEND_PANEL_HPP
#define VSS_GUI_SEND_PANEL_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/types.hpp>

#include <QString>
#include <QWidget>

#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;

namespace vss {
class studio;
}

namespace vss::gui {

/// Composer for writing bytes into an open endpoint.
class SendPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit SendPanel(vss::studio& studio, QWidget* parent = nullptr);

    void set_endpoints(const std::vector<vss::endpoint_info>& endpoints);
    void set_selected_endpoint(vss::endpoint_id id);
    int open_endpoint_count() const noexcept { return open_count_; }

Q_SIGNALS:
    void status_message(const QString& text, bool error);

private:
    void send();
    void update_enabled_state();
    void update_placeholder();

    vss::studio& studio_;
    QComboBox* endpoint_combo_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QCheckBox* hex_check_ = nullptr;
    QCheckBox* crlf_check_ = nullptr;
    QPushButton* send_button_ = nullptr;

    std::vector<std::pair<vss::endpoint_id, QString>> present_;
    int open_count_ = 0;
};

} // namespace vss::gui

#endif // VSS_GUI_SEND_PANEL_HPP
