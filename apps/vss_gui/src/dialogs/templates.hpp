//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_TEMPLATES_HPP
#define VSS_GUI_DIALOGS_TEMPLATES_HPP

#include <vss/config/document.hpp>

#include <QString>
#include <QVector>

namespace vss::gui
{

/// One setup bundled as a Qt resource and validated by `vss_config`.
struct bundled_template
{
    QString id;
    QString title;
    QString description;
    QString level;
    int ports = 0;
    int links = 0;
    /// The setup opens a real device and needs the Attach flow first.
    bool requires_device = false;
    QString resource;
};

QVector<bundled_template> bundled_templates();

/// Loads and validates the resource for `id`.
vss::result<vss::config::document> load_bundled_template(const QString& id);

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_TEMPLATES_HPP
