//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/level_service.hpp"

namespace vss::gui
{

LevelService::LevelService(QObject* parent) : QObject(parent)
{
    QSettings settings;
    advanced_ = settings.value(QStringLiteral("ui/advanced"), false).toBool();
}

void LevelService::set_advanced(bool advanced)
{
    if (advanced_ == advanced)
        return;
    advanced_ = advanced;
    QSettings settings;
    settings.setValue(QStringLiteral("ui/advanced"), advanced_);
    Q_EMIT changed(advanced_);
}

} // namespace vss::gui
