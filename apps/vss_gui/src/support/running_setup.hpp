//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/config/document.hpp>

#include <QString>

#include <optional>

namespace vss::gui::support
{

/// What was running when Studio last stopped, kept so that a restart, a
/// sign-out or a crash brings the same ports back.
///
/// It is not a file the user names or opens: File, Save still writes the
/// setups they keep.  The file holds the facts below and, under `setup`, the
/// same JSON as a saved setup, so the facts and the ports are always written
/// together.  See `docs/gui-design/docs/10-running-in-the-background.md`.
struct running_state
{
    /// The saved setup the running one was opened from or last saved as.
    QString document_path;
    /// The running setup has changes that `document_path` does not.
    bool dirty = false;
    /// The user quit Studio, which stops the ports on purpose.  Studio then
    /// offers to start them again instead of doing it by itself.
    bool stopped = false;
};

/// `<AppData>/running-setup.json`.
QString running_setup_path();

/// Writes the running setup and its facts.  Writes through a temporary file,
/// so a crash in the middle leaves the previous file whole.
bool save_running_setup(const QString& path,
                        const vss::config::document& document,
                        const running_state& state);

/// Changes only the facts of the file at `path`.  False without a readable
/// file.
bool save_running_state(const QString& path, const running_state& state);

/// The facts of the file at `path`; the defaults without a readable file.
running_state load_running_state(const QString& path);

/// The outcome of reading the running setup at start.
struct running_setup
{
    vss::config::document document;
    running_state state;
    int ports = 0;
    int links = 0;
};

/// Reads the running setup.  None when there is no file or it has no ports.
/// A file that cannot be read is moved aside to `running-setup.bad.json`
/// and `damaged` is set, so it is never half-applied and never read again.
std::optional<running_setup> load_running_setup(const QString& path,
                                                bool* damaged = nullptr);

} // namespace vss::gui::support
