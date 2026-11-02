//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_STUDIO_BACKEND_HPP
#define VSS_GUI_BACKEND_STUDIO_BACKEND_HPP

#include "backend/capabilities.hpp"
#include "backend/view_types.hpp"

#include <vss/config/document.hpp>
#include <vss/core/control_lines.hpp>
#include <vss/core/error.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/types.hpp>
#include <vss/ports/virtual_port.hpp>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>
#include <vector>

namespace vss::gui
{

/// The one narrow interface between every screen and the engine.
///
/// Screens call this, never `vss::studio` directly.  The real implementation
/// (`EngineStudioBackend`) wraps a studio; `FakeStudioBackend` is an
/// in-memory test double.  All signals are emitted on the thread that owns
/// the backend (the GUI thread).
class StudioBackend : public QObject
{
    Q_OBJECT

public:
    explicit StudioBackend(QObject* parent = nullptr);
    ~StudioBackend() override;

    /// What this build can do.  Constant for the lifetime of the backend.
    virtual capabilities caps() const = 0;

    // -- ports -------------------------------------------------------------

    virtual vss::result<vss::endpoint_id>
    create_virtual_port(const QString& name, vss::virtual_port_kind kind,
                        bool capture) = 0;

    /// The "Best available" kind.  The studio treats `pty` as its auto
    /// selector on platforms without a pseudo terminal and falls back to the
    /// best available port, so this is the real auto path.
    virtual vss::result<vss::endpoint_id> create_auto_port(const QString& name,
                                                           bool capture) = 0;

    virtual vss::result<vss::endpoint_id>
    create_mock_port(const QString& name, const vss::serial_settings& settings,
                     const QString& target) = 0;

    virtual vss::result<vss::endpoint_id>
    attach_serial(const QString& name, const QString& uri,
                  const vss::serial_settings& settings, bool capture) = 0;

    virtual vss::result<void> remove_port(vss::endpoint_id id) = 0;

    // -- snapshots ---------------------------------------------------------

    virtual std::vector<port_view> ports() const = 0;
    virtual std::vector<link_view> links() const = 0;
    virtual std::vector<device_view> devices() const = 0;
    /// The engine's bounded traffic ring, oldest first.  A limit of zero
    /// returns everything retained.  Used by the diagnostic bundle only when
    /// the user opts in to traffic contents.
    virtual std::vector<traffic_view> traffic(int limit) const = 0;
    virtual setup_facts facts() const = 0;
    virtual std::optional<port_view> port(vss::endpoint_id id) const = 0;
    virtual std::optional<link_view> link(vss::link_id id) const = 0;

    // -- links -------------------------------------------------------------

    virtual vss::result<vss::link_id>
    connect_ports(vss::endpoint_id first, vss::endpoint_id second,
                  const vss::link_options& options) = 0;
    virtual vss::result<vss::link_id>
    split(vss::endpoint_id source, const std::vector<vss::endpoint_id>& targets,
          const vss::link_options& options) = 0;
    virtual vss::result<vss::link_id>
    join(const std::vector<vss::endpoint_id>& sources, vss::endpoint_id target,
         const vss::link_options& options) = 0;
    virtual vss::result<vss::link_id>
    redirect(vss::endpoint_id from, vss::endpoint_id to,
             const vss::link_options& options) = 0;
    virtual vss::result<vss::link_id> monitor(vss::endpoint_id from,
                                              vss::endpoint_id to) = 0;
    virtual vss::result<void> disconnect(vss::link_id id) = 0;
    virtual vss::result<void> set_owner(vss::link_id id,
                                        vss::endpoint_id owner) = 0;
    virtual vss::result<void> release_owner(vss::link_id id) = 0;

    // -- traffic and control lines ----------------------------------------

    /// Queues bytes for an open endpoint.  Completion arrives as
    /// `send_finished` on the GUI thread.
    virtual vss::result<void> send(vss::endpoint_id id,
                                   const QByteArray& data) = 0;
    virtual vss::result<void> set_line(vss::endpoint_id id,
                                       vss::control_line line, bool state) = 0;
    virtual void refresh_lines(vss::endpoint_id id) = 0;

    // -- documents ---------------------------------------------------------

    virtual vss::config::document snapshot() const = 0;
    virtual vss::result<void> apply(const vss::config::document& document,
                                    bool rollback_on_error) = 0;

    // -- scenarios ---------------------------------------------------------

    virtual vss::result<share_view> share_serial(const QString& uri,
                                                 int listeners) = 0;
    virtual vss::result<vss::link_id> bridge(const QString& from_uri,
                                             const QString& to_uri) = 0;

    // -- lifecycle ---------------------------------------------------------

    /// True while the engine hub is running.  A display fact, not a feature
    /// capability: the GUI only reports it.
    virtual bool hub_running() const = 0;

    /// Re-reads the engine snapshots and emits the changed signals.
    virtual void refresh() = 0;

    /// Coalescing interval for engine events, in milliseconds.
    virtual void set_refresh_interval(int milliseconds) = 0;

Q_SIGNALS:
    void ports_changed();
    void links_changed();
    void lines_changed(vss::endpoint_id id);
    /// One coalesced batch of engine events, on the GUI thread.
    void batch_ready(const vss::gui::event_batch& events);
};

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_STUDIO_BACKEND_HPP
