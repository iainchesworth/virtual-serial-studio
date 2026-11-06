//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_STUDIO_STUDIO_HPP
#define VSS_STUDIO_STUDIO_HPP

#include <vss/config/document.hpp>
#include <vss/core/endpoint.hpp>
#include <vss/core/hub.hpp>
#include <vss/ports/com0com.hpp>
#include <vss/ports/serial_enumeration.hpp>
#include <vss/ports/peer_open.hpp>
#include <vss/ports/serial_transport.hpp>
#include <vss/ports/stable_link.hpp>
#include <vss/ports/virtual_port.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vss {

/// A fault a mock fixture can raise.  Each one is reported as an error on the
/// fixture's next read, which stops the endpoint and emits an error event.
enum class fault_kind
{
    parity,
    framing,
    overrun,
    break_condition,
    hangup,
    disconnect,
};

struct studio_options
{
    hub_options hub;
    /// Start the hub's worker threads immediately.
    bool start_hub = true;
    /// Capture traffic in the ring buffer by default.
    bool capture = true;
    /// Give every pseudo terminal a stable name (a symbolic link named after
    /// the port) and advertise that name instead of `/dev/pts/N`.  Linux and
    /// macOS only; creating a port fails if the link cannot be made.
    bool stable_links = false;
    /// Directory for the links.  Empty means `resolve_link_directory`.
    std::string stable_dir;
};

/// The application service shared by the command line tool and the GUI.
///
/// A studio owns a #hub, remembers where every endpoint came from (so the
/// topology can be saved and reloaded) and offers the composite operations
/// the UI is built from: create a virtual port, share a real one, join many
/// into one, bridge to the network.
class studio
{
public:
    explicit studio(const studio_options& options = {});
    ~studio();

    studio(const studio&) = delete;
    studio& operator=(const studio&) = delete;

    vss::hub& engine() noexcept { return *hub_; }
    const studio_options& options() const noexcept { return options_; }

    void start();
    void stop();

    // -- ports -------------------------------------------------------------

    /// Creates an OS virtual port.  `auto` picks the best available: a
    /// pseudo terminal on POSIX, a com0com pair on Windows when one exists,
    /// and an in-process loopback otherwise.
    result<std::shared_ptr<endpoint>> create_virtual_port(
        const std::string& name,
        virtual_port_kind kind = virtual_port_kind::pty,
        endpoint_options options = {});

    /// Attaches a real serial port or a network backend from a URI.
    result<std::shared_ptr<endpoint>> attach_serial(const std::string& name,
                                                    const std::string& uri,
                                                    endpoint_options options = {});

    result<std::shared_ptr<endpoint>> attach_serial(const std::string& name,
                                                    const std::string& uri,
                                                    const serial_settings& settings,
                                                    endpoint_options options = {});

    /// Creates a scriptable in-memory device.
    result<std::shared_ptr<endpoint>> create_mock_port(
        const std::string& name, const serial_settings& settings = {},
        const std::string& target = "scripted");

    /// Makes a mock fixture report `fault` on its next read.  The call is
    /// queued on the endpoint's strand, so it is safe from any thread and
    /// returns before the fault is delivered.  `errc::not_found` for an
    /// unknown endpoint and `errc::unsupported` for a port that is not a
    /// fixture.
    result<void> inject_fault(endpoint_id id, fault_kind fault);

    result<void> remove_port(endpoint_id id);
    result<void> remove_port(std::string_view name);

    /// Gives a port a new name, in the hub and in the studio's records, so
    /// events, links and a saved snapshot all use it.  A pseudo terminal is
    /// advertised by a link named after the port; the link is made again
    /// under the new name and the old one removed.  Every other kind keeps
    /// the path it was created with.  `errc::invalid_argument` for an empty
    /// name, `errc::already_exists` when another port has the name and
    /// `errc::not_found` for an unknown port.
    result<void> rename_port(endpoint_id id, const std::string& name);

    /// For each pseudo terminal port, whether a program other than the studio
    /// has its device open.  A port the platform cannot observe is left out,
    /// so a missing entry means "unknown", never "not open".  The process
    /// table is read once for every port, so call it for all of them
    /// together and not more than about once a second.
    std::unordered_map<endpoint_id, peer_open_state> peer_states() const;

    /// What other applications should open for this endpoint.
    std::string advertised_path(endpoint_id id) const;

    endpoint_options default_options() const;

    std::shared_ptr<endpoint> find(std::string_view name) const;
    std::vector<endpoint_info> endpoint_infos() const;
    std::vector<link_info> link_infos() const;

    // -- topology ----------------------------------------------------------

    result<link_id> connect(std::string_view a, std::string_view b);
    result<link_id> split(std::string_view source,
                          const std::vector<std::string>& targets);
    result<link_id> join(const std::vector<std::string>& sources,
                         std::string_view target);
    result<link_id> redirect(std::string_view from, std::string_view to);
    result<link_id> monitor(std::string_view from, std::string_view to);

    /// Puts the named ports on one shared multi-drop bus: each hears every
    /// other and none hears itself.  Modem lines are not carried.  See
    /// #hub::bus for the rules, including how a bus meets other links.
    result<link_id> bus(const std::vector<std::string>& members);
    result<void> disconnect(link_id id);

    // -- scenarios ---------------------------------------------------------

    struct share_result
    {
        endpoint_id source = 0;
        std::vector<endpoint_id> listeners;
        link_id link = 0;

        friend bool operator==(const share_result&, const share_result&) = default;
    };

    /// Opens `uri` once and gives every listener its own virtual port.
    result<share_result> share_serial(const std::string& uri, int listeners);

    /// Merges several ports into one endpoint named `target_name`.
    result<link_id> join_ports(const std::vector<std::string>& source_uris,
                               const std::string& target_uri);

    /// Connects two endpoints named by URI and returns the link.
    result<link_id> bridge(const std::string& from_uri,
                           const std::string& to_uri);

    // -- persistence -------------------------------------------------------

    result<void> apply(const config::document& value,
                       bool rollback_on_error = true);
    config::document snapshot() const;

    // -- environment -------------------------------------------------------

    com0com_installation detect_com0com() const;
    std::vector<serial_device_info> serial_devices() const;

private:
    struct port_record
    {
        endpoint_id id = 0;
        std::string name;
        std::string kind = "virtual";
        std::string uri;
        std::string target;
        std::string virtual_kind = "auto";
        std::string advertised;
        std::optional<stable_link> link;
        serial_settings serial;
        bool capture = true;
        /// The fixture behind a mock port, or empty for every other kind.
        std::weak_ptr<serial_transport> fixture;
    };

    result<std::shared_ptr<endpoint>> register_port(
        port_record record, std::shared_ptr<transport> transport,
        endpoint_options options);

    result<std::shared_ptr<endpoint>> create_auto_virtual_port(
        const std::string& name, endpoint_options options);

    /// Makes the stable link for a new pseudo terminal when the options ask
    /// for one, and points the record's advertised path at it.
    result<void> link_pty(port_record& record);

    std::string unique_name(std::string_view base) const;
    std::vector<endpoint_id> resolve_all(const std::vector<std::string>& names,
                                         error_code& ec) const;

    /// True when a physical path or an advertised path is already claimed by
    /// this studio, so the same OS port is never wired into the hub twice.
    bool path_in_use(const std::string& path) const;
    void claim_paths(const std::string& target, const std::string& advertised);

    studio_options options_;
    std::shared_ptr<vss::hub> hub_;
    mutable std::mutex record_mutex_;
    std::unordered_map<endpoint_id, port_record> records_;
    std::unordered_set<std::string> claimed_paths_;
};

} // namespace vss

#endif // VSS_STUDIO_STUDIO_HPP
