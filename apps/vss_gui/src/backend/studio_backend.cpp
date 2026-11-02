//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/studio_backend.hpp"

namespace vss::gui
{

StudioBackend::StudioBackend(QObject* parent) : QObject(parent) {}

StudioBackend::~StudioBackend() = default;

QString error_text(const vss::error_code& error)
{
    if (!error)
        return {};
    const QString message = QString::fromStdString(error.message());
    if (!message.isEmpty())
        return message;
    return QStringLiteral("Error %1").arg(error.value());
}

} // namespace vss::gui
