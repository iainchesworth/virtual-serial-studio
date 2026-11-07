//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_FORMAT_HPP
#define VSS_GUI_SHELL_FORMAT_HPP

#include <vss/core/bytes.hpp>

#include <QLocale>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vss::gui
{

inline QString format_count(std::uint64_t value)
{
    return QLocale().toString(static_cast<qulonglong>(value));
}

inline QString format_bytes(std::uint64_t value)
{
    QString text = QString::fromStdString(vss::format_byte_size(value));
    text.replace(QStringLiteral(" KB"), QStringLiteral(" kB"));
    return text;
}

/// `"1.5 MB/s"`; rates below one kilobyte stay in bytes and use rounding.
inline QString format_rate(double bytes_per_second)
{
    if (!std::isfinite(bytes_per_second) || bytes_per_second < 0.0)
        bytes_per_second = 0.0;
    if (bytes_per_second < 1024.0)
    {
        return QStringLiteral("%1 B/s").arg(
            static_cast<qulonglong>(std::llround(bytes_per_second)));
    }
    // A double above the range of std::uint64_t cannot be converted.
    constexpr double kLargest = 1.0e19;
    return QStringLiteral("%1/s").arg(format_bytes(
        static_cast<std::uint64_t>(std::min(bytes_per_second, kLargest))));
}

} // namespace vss::gui

#endif // VSS_GUI_SHELL_FORMAT_HPP
