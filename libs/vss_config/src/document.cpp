//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/config/document.hpp>
#include <vss/core/profile.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace vss::config {
namespace json = boost::json;

namespace {

json::object serial_to_json(const serial_settings& settings)
{
    json::object out;
    out["baud_rate"] = settings.baud_rate;
    out["data_bits"] = settings.data_bits;
    out["parity"] = to_string(settings.parity);
    out["stop_bits"] = to_string(settings.stop_bits);
    out["flow_control"] = to_string(settings.flow_control);
    out["read_timeout_ms"] = settings.read_timeout.count();
    out["write_timeout_ms"] = settings.write_timeout.count();
    out["exclusive"] = settings.exclusive;
    return out;
}

error_code serial_from_json(const json::value& value, serial_settings& out)
{
    if (value.is_null())
        return {};
    if (!value.is_object())
        return make_error_code(errc::config_error);

    const json::object& object = value.as_object();

    if (const auto* item = object.if_contains("baud_rate"))
    {
        if (!item->is_int64() || item->as_int64() < 0
            || item->as_int64() > 40'000'000)
            return make_error_code(errc::config_error);
        out.baud_rate = static_cast<unsigned>(item->as_int64());
    }
    if (const auto* item = object.if_contains("data_bits"))
    {
        if (!item->is_int64())
            return make_error_code(errc::config_error);
        out.data_bits = static_cast<unsigned>(item->as_int64());
    }
    if (const auto* item = object.if_contains("parity"))
    {
        if (!item->is_string())
            return make_error_code(errc::config_error);
        const auto parsed = parse_parity(item->as_string());
        if (!parsed)
            return make_error_code(errc::config_error);
        out.parity = *parsed;
    }
    if (const auto* item = object.if_contains("stop_bits"))
    {
        if (!item->is_string())
            return make_error_code(errc::config_error);
        const auto parsed = parse_stop_bits(item->as_string());
        if (!parsed)
            return make_error_code(errc::config_error);
        out.stop_bits = *parsed;
    }
    if (const auto* item = object.if_contains("flow_control"))
    {
        if (!item->is_string())
            return make_error_code(errc::config_error);
        const auto parsed = parse_flow_control(item->as_string());
        if (!parsed)
            return make_error_code(errc::config_error);
        out.flow_control = *parsed;
    }
    if (const auto* item = object.if_contains("read_timeout_ms"))
    {
        if (!item->is_int64() || item->as_int64() < 0)
            return make_error_code(errc::config_error);
        out.read_timeout = std::chrono::milliseconds(item->as_int64());
    }
    if (const auto* item = object.if_contains("write_timeout_ms"))
    {
        if (!item->is_int64() || item->as_int64() < 0)
            return make_error_code(errc::config_error);
        out.write_timeout = std::chrono::milliseconds(item->as_int64());
    }
    if (const auto* item = object.if_contains("exclusive"))
    {
        if (!item->is_bool())
            return make_error_code(errc::config_error);
        out.exclusive = item->as_bool();
    }

    return validate(out);
}

std::string string_member(const json::object& object, std::string_view key,
                          std::string fallback = {})
{
    const auto* item = object.if_contains(key);
    if (!item || !item->is_string())
        return fallback;
    return std::string(item->as_string());
}

bool bool_member(const json::object& object, std::string_view key, bool fallback)
{
    const auto* item = object.if_contains(key);
    if (!item || !item->is_bool())
        return fallback;
    return item->as_bool();
}

result<std::vector<std::string>> string_array(const json::object& object,
                                              std::string_view key)
{
    std::vector<std::string> out;
    const auto* item = object.if_contains(key);
    if (!item)
        return out;
    if (!item->is_array())
        return make_error_code(errc::config_error);
    for (const auto& element : item->as_array())
    {
        if (!element.is_string())
            return make_error_code(errc::config_error);
        out.emplace_back(element.as_string());
    }
    return out;
}

bool contains_kind(std::string_view value)
{
    for (const char* kind : port_kinds)
    {
        if (value == kind)
            return true;
    }
    return false;
}

bool contains_virtual_kind(std::string_view value)
{
    for (const char* kind : virtual_port_kinds)
    {
        if (value == kind)
            return true;
    }
    return false;
}

bool contains_link_mode(std::string_view value)
{
    for (const char* mode : link_modes)
    {
        if (value == mode)
            return true;
    }
    return false;
}

void pretty_write(std::string& out, const json::value& value, int depth)
{
    const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');

    if (value.is_object())
    {
        const auto& object = value.as_object();
        if (object.empty())
        {
            out += "{}";
            return;
        }
        out += "{\n";
        bool first = true;
        for (const auto& entry : object)
        {
            if (!first)
                out += ",\n";
            first = false;
            out += pad;
            out += "  ";
            out += json::serialize(json::value(entry.key()));
            out += ": ";
            pretty_write(out, entry.value(), depth + 1);
        }
        out += '\n';
        out += pad;
        out += '}';
        return;
    }

    if (value.is_array())
    {
        const auto& array = value.as_array();
        if (array.empty())
        {
            out += "[]";
            return;
        }
        out += "[\n";
        for (std::size_t index = 0; index < array.size(); ++index)
        {
            if (index != 0)
                out += ",\n";
            out += pad;
            out += "  ";
            pretty_write(out, array[index], depth + 1);
        }
        out += '\n';
        out += pad;
        out += ']';
        return;
    }

    out += json::serialize(value);
}

} // namespace

result<endpoint_kind> to_endpoint_kind(std::string_view kind)
{
    if (kind == "virtual")
        return endpoint_kind::virtual_port;
    if (kind == "physical")
        return endpoint_kind::physical;
    if (kind == "tcp")
        return endpoint_kind::tcp;
    if (kind == "udp")
        return endpoint_kind::udp;
    if (kind == "rfc2217")
        return endpoint_kind::rfc2217;
    if (kind == "mock")
        return endpoint_kind::mock;
    return make_error_code(errc::config_error);
}

error_code validate(const document& value)
{
    if (value.name.empty())
        return make_error_code(errc::config_error);

    std::set<std::string> names;
    for (const auto& port : value.ports)
    {
        if (port.name.empty())
            return make_error_code(errc::config_error);
        if (!names.insert(port.name).second)
            return make_error_code(errc::already_exists);
        if (!contains_kind(port.kind))
            return make_error_code(errc::config_error);
        if (!contains_virtual_kind(port.virtual_kind))
            return make_error_code(errc::config_error);

        if (port.kind == "virtual" || port.kind == "mock")
        {
            if (!port.uri.empty())
                return make_error_code(errc::config_error);
        }
        else if (port.uri.empty())
        {
            return make_error_code(errc::config_error);
        }

        if (const error_code settings_error = vss::validate(port.serial))
            return settings_error;
    }

    for (const auto& link : value.links)
    {
        if (!contains_link_mode(link.mode))
            return make_error_code(errc::config_error);

        if (link.mode == "bus")
        {
            // One broadcast domain: members only, no modem lines, no owner.
            if (!link.sources.empty() || !link.targets.empty())
                return make_error_code(errc::config_error);
            if (link.members.size() < 2 ||
                link.members.size() > max_bus_members)
                return make_error_code(errc::config_error);
            if (link.emulate_modem_lines ||
                link.policy != merge_policy::interleave)
                return make_error_code(errc::config_error);

            std::set<std::string> members;
            for (const auto& name : link.members)
            {
                if (names.find(name) == names.end())
                    return make_error_code(errc::not_found);
                if (!members.insert(name).second)
                    return make_error_code(errc::config_error);
            }
            continue;
        }
        if (!link.members.empty())
            return make_error_code(errc::config_error);
        if (link.sources.empty() || link.targets.empty())
            return make_error_code(errc::config_error);

        for (const auto& name : link.sources)
        {
            if (names.find(name) == names.end())
                return make_error_code(errc::not_found);
        }
        for (const auto& name : link.targets)
        {
            if (names.find(name) == names.end())
                return make_error_code(errc::not_found);
        }

        if (link.mode == "duplex" || link.mode == "redirect"
            || link.mode == "monitor")
        {
            if (link.sources.size() != 1 || link.targets.size() != 1)
                return make_error_code(errc::config_error);
        }
        else if (link.mode == "split")
        {
            if (link.sources.size() != 1)
                return make_error_code(errc::config_error);
        }
        else if (link.mode == "join")
        {
            if (link.targets.size() != 1)
                return make_error_code(errc::config_error);
        }
    }

    return {};
}

result<std::string> to_json(const document& value, bool pretty)
{
    VSS_PROFILE_ZONE("config.to_json");

    if (const error_code invalid = validate(value))
        return invalid;

    json::object root;
    root["version"] = 1;
    root["name"] = value.name;
    if (!value.description.empty())
        root["description"] = value.description;

    json::array ports;
    for (const auto& port : value.ports)
    {
        json::object object;
        object["name"] = port.name;
        object["kind"] = port.kind;
        if (!port.uri.empty())
            object["uri"] = port.uri;
        if (port.kind == "virtual")
            object["virtual_kind"] = port.virtual_kind;
        if (port.kind == "mock")
            object["mock_target"] = port.mock_target;
        object["serial"] = serial_to_json(port.serial);
        object["capture"] = port.capture;
        ports.push_back(std::move(object));
    }
    root["ports"] = std::move(ports);

    json::array links;
    for (const auto& link : value.links)
    {
        json::object object;
        object["mode"] = link.mode;
        if (link.mode == "bus")
        {
            json::array members;
            for (const auto& name : link.members)
                members.push_back(json::value(name));
            object["members"] = std::move(members);
            object["emulate_modem_lines"] = false;
            object["policy"] = to_string(link.policy);
            links.push_back(std::move(object));
            continue;
        }
        json::array sources;
        for (const auto& name : link.sources)
            sources.push_back(json::value(name));
        json::array targets;
        for (const auto& name : link.targets)
            targets.push_back(json::value(name));
        object["sources"] = std::move(sources);
        object["targets"] = std::move(targets);
        object["emulate_modem_lines"] = link.emulate_modem_lines;
        object["policy"] = to_string(link.policy);
        links.push_back(std::move(object));
    }
    root["links"] = std::move(links);

    const json::value root_value(std::move(root));
    if (!pretty)
        return json::serialize(root_value);

    std::string out;
    pretty_write(out, root_value, 0);
    out.push_back('\n');
    return out;
}

result<document> from_json(std::string_view text)
{
    VSS_PROFILE_ZONE("config.from_json");

    boost::system::error_code ec;
    const json::value root = json::parse(text, ec);
    if (ec)
        return make_error_code(errc::config_error);
    if (!root.is_object())
        return make_error_code(errc::config_error);

    const json::object& object = root.as_object();
    document out;
    out.name = string_member(object, "name", "untitled");
    out.description = string_member(object, "description");

    const auto* ports = object.if_contains("ports");
    if (ports && !ports->is_array())
        return make_error_code(errc::config_error);

    if (ports)
    {
        for (const auto& element : ports->as_array())
        {
            if (!element.is_object())
                return make_error_code(errc::config_error);
            const json::object& entry = element.as_object();

            port_entry port;
            port.name = string_member(entry, "name");
            port.kind = string_member(entry, "kind", "virtual");
            port.uri = string_member(entry, "uri");
            port.virtual_kind = string_member(entry, "virtual_kind", "auto");
            port.mock_target = string_member(entry, "mock_target", "scripted");
            port.capture = bool_member(entry, "capture", true);

            if (const auto* serial = entry.if_contains("serial"))
            {
                if (const error_code parse_error =
                        serial_from_json(*serial, port.serial))
                    return parse_error;
            }

            out.ports.push_back(std::move(port));
        }
    }

    const auto* links = object.if_contains("links");
    if (links && !links->is_array())
        return make_error_code(errc::config_error);

    if (links)
    {
        for (const auto& element : links->as_array())
        {
            if (!element.is_object())
                return make_error_code(errc::config_error);
            const json::object& entry = element.as_object();

            link_entry link;
            link.mode = string_member(entry, "mode", "duplex");
            // A bus has no modem lines, so absent means off there.
            link.emulate_modem_lines =
                bool_member(entry, "emulate_modem_lines", link.mode != "bus");

            auto sources = string_array(entry, "sources");
            if (!sources)
                return make_error_code(errc::config_error);
            link.sources = std::move(*sources);

            auto targets = string_array(entry, "targets");
            if (!targets)
                return make_error_code(errc::config_error);
            link.targets = std::move(*targets);

            auto members = string_array(entry, "members");
            if (!members)
                return make_error_code(errc::config_error);
            link.members = std::move(*members);

            const std::string policy = string_member(entry, "policy", "interleave");
            if (policy == "owner")
                link.policy = merge_policy::owner;
            else if (policy == "interleave")
                link.policy = merge_policy::interleave;
            else
                return make_error_code(errc::config_error);

            out.links.push_back(std::move(link));
        }
    }

    if (const error_code invalid = validate(out))
        return invalid;
    return out;
}

result<document> load_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return make_error_code(errc::not_found);

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return from_json(buffer.str());
}

result<void> save_file(const std::string& path, const document& value,
                       bool pretty)
{
    const auto text = to_json(value, pretty);
    if (!text)
        return text.error();

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return make_error_code(errc::permission_denied);
    stream << *text;
    if (!stream)
        return make_error_code(errc::io_error);
    return {};
}

} // namespace vss::config
