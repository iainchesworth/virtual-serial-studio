//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_FORMAT_HPP
#define VSS_GUI_PANES_FORMAT_HPP

#include <vss/core/bytes.hpp>

#include <QString>

#include <cstdint>

namespace vss::gui
{

inline QString format_bytes(std::uint64_t value)
{
    QString text = QString::fromStdString(vss::format_byte_size(value));
    // The copy deck uses kB, not the engine's KB.
    text.replace(QStringLiteral(" KB"), QStringLiteral(" kB"));
    return text;
}

} // namespace vss::gui

#endif // VSS_GUI_PANES_FORMAT_HPP
