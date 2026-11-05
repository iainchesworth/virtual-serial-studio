//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../com0com_platform.hpp"

#include <vss/ports/com0com.hpp>

#include <QString>

#include <algorithm>
#include <string>

namespace vss::gui::detail
{

bool platform_supports_com0com_pair_creation() noexcept
{
    return true;
}

vss::result<void> platform_create_com0com_pair(vss::studio& studio)
{
    const vss::com0com_installation installation = studio.detect_com0com();
    if (!installation.found)
        return vss::make_error_code(vss::errc::unsupported);

    // Pick the two COM numbers above every number the OS already reports.
    int highest = 0;
    for (const auto& device : studio.serial_devices())
    {
        const QString path = QString::fromStdString(device.path);
        if (!path.startsWith(QStringLiteral("COM"), Qt::CaseInsensitive))
            continue;
        bool ok = false;
        const int number = path.mid(3).toInt(&ok);
        if (ok)
            highest = std::max(highest, number);
    }
    const std::string first = "COM" + std::to_string(highest + 1);
    const std::string second = "COM" + std::to_string(highest + 2);
    return vss::create_com0com_pair(installation, first, second, true);
}

} // namespace vss::gui::detail
