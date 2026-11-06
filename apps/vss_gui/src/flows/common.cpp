//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "flows/common.hpp"

#include <vss/ports/permissions.hpp>

#include <QObject>

namespace vss::gui
{
namespace
{

bool is_studio_only_path(const QString& path)
{
    return path.startsWith(QStringLiteral("loopback:")) || path.isEmpty();
}

} // namespace

platform_kind host_platform()
{
#if defined(Q_OS_WIN)
    return platform_kind::windows;
#elif defined(Q_OS_MACOS)
    return platform_kind::macos;
#else
    return platform_kind::linux;
#endif
}

bool windows_platform(platform_kind platform)
{
    return platform == platform_kind::windows;
}

bool pseudo_terminal_platform(platform_kind platform)
{
    return platform == platform_kind::linux || platform == platform_kind::macos;
}

QFont flow_semibold(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

QString bold(const QString& text)
{
    return QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped());
}

QString port_display_name(const port_view& port)
{
    if (!port.studio_only && !is_studio_only_path(port.path))
        return port.path;
    return port.name;
}

QString error_title(const QString& verb, const QString& thing)
{
    return QObject::tr("Could not %1 %2").arg(verb, thing);
}

QString error_body(const vss::error_code& error, const QString& cause,
                   const QString& next_step)
{
    if (error == vss::errc::permission_denied)
    {
        if (vss::permissions::host_name() != "windows")
        {
            return QObject::tr(
                "The system did not allow this. Nothing was changed. Open "
                "Tools, Setup check: it says whether your account is missing "
                "the serial device group or AppArmor, SELinux or a sandbox is "
                "in the way, and shows the command to fix it.");
        }
        return QObject::tr("Windows did not allow this change. Nothing was "
                           "changed. Run Virtual Serial Studio as "
                           "administrator and try again.");
    }
    if (error == vss::errc::device_busy)
    {
        return QObject::tr("Another program may be using the device. Nothing "
                           "was changed. Close that program and try again.");
    }
    return QStringLiteral("%1 %2").arg(cause, next_step);
}

} // namespace vss::gui
