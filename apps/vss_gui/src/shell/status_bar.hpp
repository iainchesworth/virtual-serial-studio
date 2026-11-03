//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_STATUS_BAR_HPP
#define VSS_GUI_SHELL_STATUS_BAR_HPP

#include "backend/studio_backend.hpp"
#include "widgets/chips.hpp"
#include "widgets/controls.hpp"

#include <QElapsedTimer>
#include <QLabel>
#include <QStatusBar>

#include <cstdint>

namespace vss::gui
{

/// Ports, links, rates, hub state, readiness and the level switch.
class StatusBar final : public QStatusBar
{
    Q_OBJECT

public:
    explicit StatusBar(StudioBackend& backend, QWidget* parent = nullptr);

    void set_advanced(bool advanced);
    void refresh();

Q_SIGNALS:
    void level_requested(bool advanced);

private:
    void update_readiness();

    StudioBackend& backend_;
    QLabel* ports_ = nullptr;
    QLabel* links_ = nullptr;
    QLabel* rates_ = nullptr;
    QLabel* hub_ = nullptr;
    StatusChip* readiness_ = nullptr;
    StatusChip* dropped_ = nullptr;
    SegmentedControl* level_ = nullptr;

    QElapsedTimer rate_timer_;
    qint64 last_ns_ = 0;
    std::uint64_t previous_rx_ = 0;
    std::uint64_t previous_tx_ = 0;
    bool advanced_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_STATUS_BAR_HPP
