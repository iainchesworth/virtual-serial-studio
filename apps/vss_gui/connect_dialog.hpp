//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_CONNECT_DIALOG_HPP
#define VSS_GUI_CONNECT_DIALOG_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/types.hpp>

#include <QDialog>
#include <QString>
#include <QStringList>

#include <vector>

class QComboBox;
class QLabel;
class QListWidget;

namespace vss::gui {

/// How two ports should be wired together.
enum class connect_mode
{
    duplex,  ///< Both directions, the default.
    redirect,///< One way, source to target.
    monitor, ///< One way, and the monitor is told it is observing.
    split,   ///< One source fanned out to several targets.
    join     ///< Several sources merged into one target.
};

struct connect_request
{
    connect_mode mode = connect_mode::duplex;
    QStringList sources;
    QString target;
};

/// A guided replacement for hand-editing links: pick ports, pick how they are
/// wired, and read a plain-language summary of the result.
class ConnectDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(std::vector<vss::endpoint_info> endpoints,
                           QWidget* parent = nullptr);

    connect_request request() const { return request_; }

public Q_SLOTS:
    void accept() override;

private:
    void on_mode_changed();
    void update_summary();
    void set_error(const QString& text);

    std::vector<vss::endpoint_info> endpoints_;
    connect_request request_;

    QComboBox* mode_ = nullptr;
    QComboBox* source_ = nullptr;
    QComboBox* target_ = nullptr;
    QListWidget* sources_ = nullptr;
    QListWidget* targets_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* error_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_CONNECT_DIALOG_HPP
