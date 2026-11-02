//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_LEVEL_SERVICE_HPP
#define VSS_GUI_SHELL_LEVEL_SERVICE_HPP

#include "shell/action_registry.hpp"

#include <QObject>
#include <QSettings>

namespace vss::gui
{

/// Simple or Advanced, persisted in `ui/advanced` and shared by every pane.
class LevelService final : public QObject
{
    Q_OBJECT

public:
    explicit LevelService(QObject* parent = nullptr);

    bool advanced() const noexcept { return advanced_; }
    action_level level() const noexcept
    {
        return advanced_ ? action_level::advanced : action_level::simple;
    }

    void set_advanced(bool advanced);

Q_SIGNALS:
    void changed(bool advanced);

private:
    bool advanced_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_LEVEL_SERVICE_HPP
