//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_FAKE_STUDIO_BACKEND_HPP
#define VSS_GUI_BACKEND_FAKE_STUDIO_BACKEND_HPP

#include "backend/studio_backend.hpp"

#include <QByteArray>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace vss::gui
{

/// In-memory `StudioBackend` for dialog and model tests.
///
/// It mirrors the real backend's contract, including unique port names,
/// owner validation, event signals and the tri-state peer value.  Failures
/// can be injected per call with `fail_next`, so error paths are testable
/// without touching the engine.
class FakeStudioBackend final : public StudioBackend
{
public:
    explicit FakeStudioBackend(QObject* parent = nullptr);

    void set_capabilities(const capabilities& value) { caps_ = value; }
    void set_peer_state(vss::endpoint_id id, peer_state state);
    void set_devices(std::vector<device_view> devices);
    void set_facts(const setup_facts& facts) { facts_ = facts; }
    /// The next fallible call returns this error and consumes it.
    void fail_next(vss::error_code error) { next_error_ = error; }

    const std::vector<QByteArray>& sent() const noexcept { return sent_; }

    capabilities caps() const override { return caps_; }

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
    std::vector<device_view> devices() const override { return devices_; }
    setup_facts facts() const override { return facts_; }
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

    bool hub_running() const override { return true; }
    void refresh() override;
    void set_refresh_interval(int milliseconds) override;

private:
    QString unique_name(const QString& base) const;
    std::optional<vss::error_code> take_error();
    port_view* find_port(vss::endpoint_id id);
    link_view* find_link(vss::link_id id);
    vss::link_id add_link(link_shape shape, vss::link_mode mode,
                          const std::vector<vss::endpoint_id>& sources,
                          const std::vector<vss::endpoint_id>& targets,
                          const vss::link_options& options);

    capabilities caps_;
    std::vector<port_view> ports_;
    std::vector<link_view> links_;
    std::vector<device_view> devices_;
    setup_facts facts_;
    std::vector<QByteArray> sent_;
    std::optional<vss::error_code> next_error_;
    vss::endpoint_id next_port_ = 1;
    vss::link_id next_link_ = 1;
    int refresh_interval_ms_ = 250;
};

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_FAKE_STUDIO_BACKEND_HPP
