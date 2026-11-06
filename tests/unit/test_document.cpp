//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/config/document.hpp>

#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

using namespace vss;
using namespace vss::config;

namespace {

document sample_document()
{
    document value;
    value.name = "lab";
    value.description = "A representative topology";

    port_entry virtual_port;
    virtual_port.name = "virtual";
    virtual_port.kind = "virtual";
    virtual_port.virtual_kind = "loopback";
    virtual_port.serial.baud_rate = 57600;
    value.ports.push_back(virtual_port);

    port_entry physical;
    physical.name = "real";
    physical.kind = "physical";
    physical.uri = "serial://COM3?baud=115200";
    physical.serial.baud_rate = 9600;
    physical.serial.parity = parity_mode::even;
    physical.serial.read_timeout = std::chrono::milliseconds(150);
    physical.capture = false;
    value.ports.push_back(physical);

    port_entry mock;
    mock.name = "fake";
    mock.kind = "mock";
    mock.mock_target = "modem";
    value.ports.push_back(mock);

    link_entry split;
    split.mode = "split";
    split.sources = {"virtual"};
    split.targets = {"real", "fake"};
    value.links.push_back(split);

    return value;
}

} // namespace

TEST_CASE("document: roundtrip through JSON")
{
    const document original = sample_document();
    const auto text = to_json(original, false);
    REQUIRE(text);

    const auto parsed = from_json(*text);
    REQUIRE(parsed);

    REQUIRE_EQ(parsed->name, original.name);
    REQUIRE_EQ(parsed->description, original.description);
    REQUIRE_EQ(parsed->ports.size(), original.ports.size());
    REQUIRE_EQ(parsed->links.size(), original.links.size());

    REQUIRE_EQ(parsed->ports[1].name, std::string("real"));
    REQUIRE_EQ(parsed->ports[1].uri, std::string("serial://COM3?baud=115200"));
    REQUIRE_EQ(parsed->ports[1].serial.baud_rate, 9600u);
    REQUIRE_EQ(parsed->ports[1].serial.parity, parity_mode::even);
    REQUIRE_EQ(parsed->ports[1].serial.read_timeout.count(), std::int64_t{150});
    REQUIRE(!parsed->ports[1].capture);

    REQUIRE_EQ(parsed->ports[2].mock_target, std::string("modem"));
    REQUIRE_EQ(parsed->links[0].mode, std::string("split"));
    REQUIRE_EQ(parsed->links[0].targets.size(), std::size_t{2});
    REQUIRE(parsed->links[0].emulate_modem_lines);
}

TEST_CASE("document: pretty and compact output")
{
    const auto pretty = to_json(sample_document(), true);
    REQUIRE(pretty);
    REQUIRE(pretty->find('\n') != std::string::npos);
    REQUIRE(pretty->find("  \"version\"") != std::string::npos);
    REQUIRE_EQ(pretty->back(), '\n');

    const auto compact = to_json(sample_document(), false);
    REQUIRE(compact);
    REQUIRE(compact->find('\n') == std::string::npos);
}

TEST_CASE("document: validation rejects broken topologies")
{
    REQUIRE(!validate(sample_document()));

    {
        document value = sample_document();
        value.name.clear();
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.ports[1].name = "virtual";
        REQUIRE_EQ(validate(value), make_error_code(errc::already_exists));
    }
    {
        document value = sample_document();
        value.ports[0].kind = "teleport";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.ports[0].virtual_kind = "magic";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.ports[1].uri.clear();
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.ports[0].uri = "serial://COM1";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.ports[1].serial.baud_rate = 0;
        REQUIRE_EQ(validate(value), make_error_code(errc::invalid_argument));
    }
    {
        document value = sample_document();
        value.links[0].mode = "carrier-pigeon";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.links[0].sources = {"ghost"};
        REQUIRE_EQ(validate(value), make_error_code(errc::not_found));
    }
    {
        document value = sample_document();
        value.links[0].targets = {"ghost"};
        REQUIRE_EQ(validate(value), make_error_code(errc::not_found));
    }
    {
        document value = sample_document();
        value.links[0].sources.clear();
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.links[0].mode = "duplex";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.links[0].mode = "split";
        value.links[0].sources = {"virtual", "real"};
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
    {
        document value = sample_document();
        value.links[0].mode = "join";
        REQUIRE_EQ(validate(value), make_error_code(errc::config_error));
    }
}

TEST_CASE("document: every malformed field is reported")
{
    struct broken
    {
        const char* json;
        const char* why;
    };

    const broken cases[] = {
        {R"({"ports":[{"name":"a","kind":"virtual","serial":42}]})",
         "serial is not an object"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"baud_rate":"fast"}}]})",
         "baud rate is not a number"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"baud_rate":-1}}]})",
         "baud rate is negative"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"data_bits":"8"}}]})",
         "data bits is not a number"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"parity":1}}]})",
         "parity is not a string"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"stop_bits":1}}]})",
         "stop bits is not a string"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"flow_control":1}}]})",
         "flow control is not a string"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"read_timeout_ms":-5}}]})",
         "read timeout is negative"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"write_timeout_ms":"x"}}]})",
         "write timeout is not a number"},
        {R"({"ports":[{"name":"a","kind":"virtual","serial":{"exclusive":1}}]})",
         "exclusive is not a bool"},
        {R"({"ports":[{"name":"a","kind":"virtual"}],"links":[{"mode":"duplex","sources":"a","targets":["a"]}]})",
         "sources is not an array"},
        {R"({"ports":[{"name":"a","kind":"virtual"}],"links":[{"mode":"duplex","sources":["a"],"targets":"a"}]})",
         "targets is not an array"},
        {R"({"ports":[{"name":"a","kind":"virtual"}],"links":[{"mode":"duplex","sources":[1],"targets":["a"]}]})",
         "source is not a string"},
    };

    for (const auto& entry : cases)
    {
        const auto parsed = from_json(entry.json);
        REQUIRE(!parsed);
        REQUIRE_EQ(parsed.error(), make_error_code(errc::config_error));
    }

    // to_json refuses to serialise an invalid document.
    document invalid;
    invalid.name.clear();
    REQUIRE(!to_json(invalid));
}

TEST_CASE("document: parsing rejects malformed JSON")
{
    REQUIRE_EQ(from_json("{"), make_error_code(errc::config_error));
    REQUIRE_EQ(from_json("[]"), make_error_code(errc::config_error));
    REQUIRE_EQ(from_json("\"text\""), make_error_code(errc::config_error));

    REQUIRE_EQ(from_json(R"({"name":"x","ports":42})").error(),
               make_error_code(errc::config_error));
    REQUIRE_EQ(from_json(R"({"name":"x","links":42})").error(),
               make_error_code(errc::config_error));
    REQUIRE_EQ(from_json(R"({"name":"x","ports":[42]})").error(),
               make_error_code(errc::config_error));
    REQUIRE_EQ(from_json(
                   R"({"ports":[{"name":"a","kind":"physical","uri":"serial://C","serial":{"baud_rate":"fast"}}]})")
                   .error(),
               make_error_code(errc::config_error));
    REQUIRE_EQ(from_json(
                   R"({"ports":[{"name":"a","kind":"virtual","serial":{"parity":"triple"}}]})")
                   .error(),
               make_error_code(errc::config_error));
    REQUIRE_EQ(from_json(
                   R"({"ports":[{"name":"a","kind":"virtual"}],"links":[{"mode":"duplex","sources":["a"],"targets":["a"],"policy":"anarchy"}]})")
                   .error(),
               make_error_code(errc::config_error));
}

TEST_CASE("document: defaults are applied when fields are missing")
{
    const auto parsed = from_json(
        R"({"ports":[{"name":"v","kind":"virtual"}]})");
    REQUIRE(parsed);
    REQUIRE_EQ(parsed->name, std::string("untitled"));
    REQUIRE_EQ(parsed->ports.size(), std::size_t{1});
    REQUIRE_EQ(parsed->ports[0].virtual_kind, std::string("auto"));
    REQUIRE_EQ(parsed->ports[0].serial.baud_rate, 115200u);
    REQUIRE(parsed->ports[0].capture);
    REQUIRE(parsed->links.empty());
}

TEST_CASE("document: optional fields tolerate null and foreign types")
{
    const auto null_serial = from_json(
        R"({"ports":[{"name":"a","kind":"virtual","serial":null}]})");
    REQUIRE(null_serial);
    REQUIRE_EQ(null_serial->ports[0].serial.baud_rate, 115200u);

    const auto loose = from_json(
        R"({"name":42,"ports":[{"name":"a","kind":"virtual","capture":"yes"}]})");
    REQUIRE(loose);
    REQUIRE_EQ(loose->name, std::string("untitled"));
    REQUIRE(loose->ports[0].capture);
}

TEST_CASE("document: every virtual port kind is accepted")
{
    for (const char* kind : virtual_port_kinds)
    {
        document value;
        value.name = "kinds";
        port_entry port;
        port.name = "port";
        port.kind = "virtual";
        port.virtual_kind = kind;
        value.ports.push_back(port);

        REQUIRE(!validate(value));
        REQUIRE(to_json(value, false));
    }
}

TEST_CASE("document: kind mapping")
{
    REQUIRE_EQ(*to_endpoint_kind("virtual"), endpoint_kind::virtual_port);
    REQUIRE_EQ(*to_endpoint_kind("physical"), endpoint_kind::physical);
    REQUIRE_EQ(*to_endpoint_kind("tcp"), endpoint_kind::tcp);
    REQUIRE_EQ(*to_endpoint_kind("udp"), endpoint_kind::udp);
    REQUIRE_EQ(*to_endpoint_kind("rfc2217"), endpoint_kind::rfc2217);
    REQUIRE_EQ(*to_endpoint_kind("mock"), endpoint_kind::mock);
    REQUIRE_EQ(to_endpoint_kind("serial").error(),
               make_error_code(errc::config_error));
}

TEST_CASE("document: save and load a file")
{
    const auto path = std::filesystem::current_path() / "vss-document-test.json";
    std::filesystem::remove(path);

    const auto saved = save_file(path.string(), sample_document(), true);
    REQUIRE(saved);
    REQUIRE(std::filesystem::exists(path));

    const auto loaded = load_file(path.string());
    REQUIRE(loaded);
    REQUIRE_EQ(loaded->name, std::string("lab"));
    REQUIRE_EQ(loaded->ports.size(), std::size_t{3});

    std::filesystem::remove(path);

    REQUIRE_EQ(load_file(path.string()).error(),
               make_error_code(errc::not_found));

    REQUIRE_EQ(load_file(std::filesystem::current_path().string()).error(),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(load_file("").error(), make_error_code(errc::not_found));

    const auto missing_directory =
        (std::filesystem::current_path() / "no-such-directory" / "file.json")
            .string();
    REQUIRE_EQ(save_file(missing_directory, sample_document()).error(),
               make_error_code(errc::permission_denied));
}

TEST_CASE("document: owner policy roundtrips")
{
    document value = sample_document();
    value.links[0].mode = "join";
    value.links[0].sources = {"virtual", "fake"};
    value.links[0].targets = {"real"};
    value.links[0].policy = merge_policy::owner;
    value.links[0].emulate_modem_lines = false;

    const auto text = to_json(value, false);
    REQUIRE(text);
    const auto parsed = from_json(*text);
    REQUIRE(parsed);
    REQUIRE_EQ(parsed->links[0].policy, merge_policy::owner);
    REQUIRE(!parsed->links[0].emulate_modem_lines);
    REQUIRE_EQ(parsed->links[0].mode, std::string("join"));
}
