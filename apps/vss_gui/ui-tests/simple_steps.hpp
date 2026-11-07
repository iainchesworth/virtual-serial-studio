//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The steps a person takes in Simple mode, shared by the Simple journeys and
// by the journeys that start from a Simple setup.
//

#pragma once

#include "ui_harness.hpp"

#include "backend/view_types.hpp"
#include "simple/job_form.hpp"
#include "simple/job_grid.hpp"
#include "simple/setup_page.hpp"
#include "simple/simple_page.hpp"

#include <QString>

namespace vss::gui::ui::steps
{

/// Clicks the tile of a job on the "What do you want to do?" page and
/// returns its form.
simple::JobForm* open_job(Session& session, simple::job kind);

/// The visible "Which program" fields of the form, top to bottom.
std::vector<QLineEdit*> program_fields(simple::JobForm* form);

/// The visible add buttons and selectors of the form, by their text.
QPushButton* form_button(simple::JobForm* form, const QString& text);

/// Clicks Create and waits for the setup page of the new setup.
simple::SetupPage* create(Session& session);

/// The setup page shown now, or throws.
simple::SetupPage* setup_page(Session& session);

/// Clicks the first setup in the "Your setups" list.
void select_setup_row(Session& session, int row);

/// Removes the setup shown, through its Remove setup button and the
/// confirmation, and checks the window is back at the jobs.
void remove_shown_setup(Session& session);

/// Counts: ports and links as the backend holds them.
struct counts
{
    std::size_t ports = 0;
    std::size_t links = 0;
};

void require_counts(Session& session, const counts& expected);

/// Switches to Advanced mode through the View menu, checks that the Ports dock
/// and the canvas show the same ports and links as the backend, and switches
/// back to Simple mode.
void verify_in_advanced(Session& session);

/// Picks a device in the form's device list by clicking its entry. `which`
/// counts the visible lists, top to bottom.
void choose_device(simple::JobForm* form, const QString& path, int which = 0);

/// Makes these the serial devices the machine lists.
void plug_in_devices(Session& session, const QStringList& paths);

} // namespace vss::gui::ui::steps
