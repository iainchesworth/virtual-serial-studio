//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_LINE_PRESETS_HPP
#define VSS_GUI_ADVANCED_LINE_PRESETS_HPP

#include "backend/view_types.hpp"

#include <QString>
#include <QStringList>

#include <array>

namespace vss::gui
{

/// The three wiring presets in the fault dialog's segmented control.
enum class wiring_preset
{
    standard_null_modem,
    loopback,
    custom
};

/// One preset, kept in the single data table the design asks for.  `name` is
/// untranslated; the UI wraps it in a translation context.
struct wiring_preset_entry
{
    wiring_preset id;
    const char* name;
    line_wiring wiring;
};

/// The one table.  The project docs disagree about DCD; the engine's
/// `null_modem_peer()` is DSR<-DTR, CTS<-RTS and DCD<-DTR, so the Standard
/// preset follows the code.  Loopback is the minimal handshake loop, which the
/// bundle does not define; see design-deviations.md.
const std::array<wiring_preset_entry, 3>& wiring_presets();

const wiring_preset_entry& preset_for(wiring_preset id);

/// The translated name of a preset.
QString preset_name(wiring_preset id);

/// The preset a wiring value matches, or `custom` when it matches none.
wiring_preset preset_for_wiring(const line_wiring& wiring);

/// "With this wiring, raising DTR on modem raises DSR and DCD on terminal.
/// Raising RTS raises CTS."
QString wiring_sentence(const line_wiring& wiring, const QString& this_name,
                        const QString& other_name);

/// "DSR", "DSR and DCD", "DSR, CTS and DCD".
QString join_names(const QStringList& names);

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_LINE_PRESETS_HPP
