//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_THEME_HPP
#define VSS_GUI_THEME_HPP

#include <vss/core/bytes.hpp>
#include <vss/core/types.hpp>

#include <QColor>
#include <QString>
#include <QLocale>
#include <QString>

#include <cmath>
#include <cstdint>

namespace vss::gui {

/// Applies the persisted appearance: "system", "light" or "dark".
void apply_theme(const QString& name);

/// The single source of truth for the state colours used by the port table,
/// the topology boxes and the inspector.
inline QColor state_colour(endpoint_state state) noexcept
{
    switch (state)
    {
    case endpoint_state::open:
        return QColor(0x1a, 0x7f, 0x37);
    case endpoint_state::opening:
        return QColor(0x9a, 0x67, 0x00);
    case endpoint_state::failed:
        return QColor(0xcf, 0x22, 0x2e);
    case endpoint_state::closed:
        return QColor(0x6e, 0x77, 0x81);
    case endpoint_state::created:
        return QColor(0x8b, 0x94, 0x9e);
    }
    return QColor(0x6e, 0x77, 0x81);
}

inline QString kind_text(endpoint_kind kind)
{
    return QString::fromLatin1(to_string(kind));
}

inline QString state_text(endpoint_state state)
{
    return QString::fromLatin1(to_string(state));
}

inline QString format_count(std::uint64_t value)
{
    return QLocale().toString(static_cast<qulonglong>(value));
}

inline QString format_bytes(std::uint64_t value)
{
    return QString::fromStdString(vss::format_byte_size(value));
}

/// `"1.5 MB/s"`; rates below one kilobyte stay in bytes and use rounding.
inline QString format_rate(double bytes_per_second)
{
    if (!std::isfinite(bytes_per_second) || bytes_per_second < 0.0)
        bytes_per_second = 0.0;
    if (bytes_per_second < 1024.0)
        return QStringLiteral("%1 B/s").arg(
            static_cast<qulonglong>(std::llround(bytes_per_second)));
    return QStringLiteral("%1/s").arg(
        format_bytes(static_cast<std::uint64_t>(bytes_per_second)));
}

} // namespace vss::gui

#endif // VSS_GUI_THEME_HPP
