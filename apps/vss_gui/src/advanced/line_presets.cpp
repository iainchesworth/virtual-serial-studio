//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/line_presets.hpp"

#include <QCoreApplication>

namespace vss::gui
{
namespace
{

QString translated(const char* text)
{
    return QCoreApplication::translate("LineWiring", text);
}

} // namespace

const std::array<wiring_preset_entry, 3>& wiring_presets()
{
    static const std::array<wiring_preset_entry, 3> table = {{
        {wiring_preset::standard_null_modem,
         QT_TRANSLATE_NOOP("LineWiring", "Standard null-modem"),
         line_wiring{true, false, true, false, false, true, false, false}},
        {wiring_preset::loopback, QT_TRANSLATE_NOOP("LineWiring", "Loopback"),
         line_wiring{true, false, false, false, false, true, false, false}},
        {wiring_preset::custom, QT_TRANSLATE_NOOP("LineWiring", "Custom"),
         line_wiring{}},
    }};
    return table;
}

const wiring_preset_entry& preset_for(wiring_preset id)
{
    const auto& table = wiring_presets();
    for (const auto& entry : table)
    {
        if (entry.id == id)
            return entry;
    }
    return table[0];
}

QString preset_name(wiring_preset id)
{
    return translated(preset_for(id).name);
}

wiring_preset preset_for_wiring(const line_wiring& wiring)
{
    for (const auto& entry : wiring_presets())
    {
        if (entry.id != wiring_preset::custom && entry.wiring == wiring)
            return entry.id;
    }
    return wiring_preset::custom;
}

QString join_names(const QStringList& names)
{
    if (names.isEmpty())
        return {};
    if (names.size() == 1)
        return names.first();
    const QStringList head = names.mid(0, names.size() - 1);
    return QCoreApplication::translate("LineWiring", "%1 and %2")
        .arg(head.join(QStringLiteral(", ")), names.last());
}

QString wiring_sentence(const line_wiring& wiring, const QString& this_name,
                        const QString& other_name)
{
    QStringList dtr_inputs;
    if (wiring.dtr_to_dsr)
        dtr_inputs << QStringLiteral("DSR");
    if (wiring.dtr_to_cts)
        dtr_inputs << QStringLiteral("CTS");
    if (wiring.dtr_to_dcd)
        dtr_inputs << QStringLiteral("DCD");
    if (wiring.dtr_to_ri)
        dtr_inputs << QStringLiteral("RI");

    QStringList rts_inputs;
    if (wiring.rts_to_dsr)
        rts_inputs << QStringLiteral("DSR");
    if (wiring.rts_to_cts)
        rts_inputs << QStringLiteral("CTS");
    if (wiring.rts_to_dcd)
        rts_inputs << QStringLiteral("DCD");
    if (wiring.rts_to_ri)
        rts_inputs << QStringLiteral("RI");

    const QString dtr_text = join_names(dtr_inputs);
    const QString rts_text = join_names(rts_inputs);

    if (!dtr_text.isEmpty() && !rts_text.isEmpty())
    {
        return QCoreApplication::translate(
                   "LineWiring",
                   "With this wiring, raising DTR on %1 raises %2 on %3. "
                   "Raising RTS raises %4.")
            .arg(this_name, dtr_text, other_name, rts_text);
    }
    if (!dtr_text.isEmpty())
    {
        return QCoreApplication::translate(
                   "LineWiring",
                   "With this wiring, raising DTR on %1 raises %2 on %3.")
            .arg(this_name, dtr_text, other_name);
    }
    if (!rts_text.isEmpty())
    {
        return QCoreApplication::translate(
                   "LineWiring",
                   "With this wiring, raising RTS on %1 raises %2 on %3.")
            .arg(this_name, rts_text, other_name);
    }
    return QCoreApplication::translate(
        "LineWiring", "With this wiring, no output lines are connected.");
}

} // namespace vss::gui
