//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_ENGINE_STUDIO_BACKEND_HPP
#define VSS_GUI_BACKEND_ENGINE_STUDIO_BACKEND_HPP

#include "backend/studio_backend.hpp"

#include <memory>
#include <vector>

namespace vss
{
class studio;
struct endpoint_info;
struct link_info;
} // namespace vss

namespace vss::gui
{

class EventBridge;

/// The real adapter over `vss::studio`.
///
/// The studio must outlive the backend and the backend must outlive the
/// engine's event deliveries, which the application's construction order
/// already guarantees: the studio is created before the main window.
class EngineStudioBackend final : public StudioBackend
{
public:
    explicit EngineStudioBackend(vss::studio& studio,
                                 QObject* parent = nullptr);
    ~EngineStudioBackend() override;

    capabilities caps() const override;

    vss::result<vss::endpoint_id>
    create_virtual_port(const QString& name, vss::virtual_port_kind kind,
                        bool capture) override;
    vss::result<vss::endpoint_id> create_auto_port(const QString& name,
                                                   bool capture) override;
    vss::result<vss::endpoint_id>
    create_mock_port(const QString& name, const vss::serial_settings& settings,
                     const QString& target) override;
    vss::result<vss::endpoint_id>
    attach_serial(const QString& name, const QString& uri,
                  const vss::serial_settings& settings, bool capture) override;
    vss::result<void> remove_port(vss::endpoint_id id) override;

    std::vector<port_view> ports() const override { return ports_; }
    std::vector<link_view> links() const override { return links_; }
    std::vector<device_view> devices() const override;
    std::vector<traffic_view> traffic(int limit) const override;
    setup_facts facts() const override;
    std::optional<port_view> port(vss::endpoint_id id) const override;
    std::optional<link_view> link(vss::link_id id) const override;

    vss::result<vss::link_id>
    connect_ports(vss::endpoint_id first, vss::endpoint_id second,
                  const vss::link_options& options) override;
    vss::result<vss::link_id>
    split(vss::endpoint_id source, const std::vector<vss::endpoint_id>& targets,
          const vss::link_options& options) override;
    vss::result<vss::link_id> join(const std::vector<vss::endpoint_id>& sources,
                                   vss::endpoint_id target,
                                   const vss::link_options& options) override;
    vss::result<vss::link_id>
    redirect(vss::endpoint_id from, vss::endpoint_id to,
             const vss::link_options& options) override;
    vss::result<vss::link_id> monitor(vss::endpoint_id from,
                                      vss::endpoint_id to) override;
    vss::result<void> disconnect(vss::link_id id) override;
    vss::result<void> set_owner(vss::link_id id,
                                vss::endpoint_id owner) override;
    vss::result<void> release_owner(vss::link_id id) override;

    vss::result<void> send(vss::endpoint_id id,
                           const QByteArray& data) override;
    vss::result<void> set_line(vss::endpoint_id id, vss::control_line line,
                               bool state) override;
    void refresh_lines(vss::endpoint_id id) override;

    vss::config::document snapshot() const override;
    vss::result<void> apply(const vss::config::document& document,
                            bool rollback_on_error) override;

    vss::result<share_view> share_serial(const QString& uri,
                                         int listeners) override;
    vss::result<vss::link_id> bridge(const QString& from_uri,
                                     const QString& to_uri) override;
    vss::result<void> create_com0com_pair() override;

    bool hub_running() const override;
    void refresh() override;
    void set_refresh_interval(int milliseconds) override;

private:
    void rebuild();

    vss::studio& studio_;
    std::unique_ptr<EventBridge> bridge_;
    std::vector<port_view> ports_;
    std::vector<link_view> links_;
};

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_ENGINE_STUDIO_BACKEND_HPP
