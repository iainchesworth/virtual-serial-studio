//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/studio/studio.hpp>

#include "scratch_directory.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <string>

using namespace vss;
using vss_test_support::scratch_directory;
namespace fs = std::filesystem;

namespace
{

studio_options linked_options(const fs::path& directory)
{
    studio_options options;
    options.stable_links = true;
    options.stable_dir = directory.string();
    return options;
}

} // namespace

TEST_CASE("studio links: a pseudo terminal is advertised by its stable name")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";

    studio app(linked_options(directory));
    const auto created =
        app.create_virtual_port("ttyVSS0", virtual_port_kind::pty);
    REQUIRE(created);

    const auto link = directory / "ttyVSS0";
    REQUIRE_EQ(app.advertised_path((*created)->id()), link.string());
    REQUIRE(fs::is_symlink(link));
    // The link points at the real slave, which the endpoint still reports.
    REQUIRE_EQ(fs::read_symlink(link).string(), (*created)->public_path());
    REQUIRE_EQ(app.endpoint_infos().front().path, link.string());
}

TEST_CASE("studio links: removing the port or the studio removes the link")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";

    {
        studio app(linked_options(directory));
        const auto first =
            app.create_virtual_port("one", virtual_port_kind::pty);
        const auto second =
            app.create_virtual_port("two", virtual_port_kind::pty);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(fs::is_symlink(directory / "one"));
        REQUIRE(fs::is_symlink(directory / "two"));

        REQUIRE(app.remove_port("one"));
        REQUIRE(!fs::exists(fs::symlink_status(directory / "one")));
        REQUIRE(fs::is_symlink(directory / "two"));
    }

    // The studio is gone, and so is the name that was left.
    REQUIRE(!fs::exists(fs::symlink_status(directory / "two")));
}

TEST_CASE("studio links: a port name becomes a safe link name")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";

    studio app(linked_options(directory));
    const auto created =
        app.create_virtual_port("Virtual 1/a:b", virtual_port_kind::pty);
    REQUIRE(created);
    REQUIRE_EQ(app.advertised_path((*created)->id()),
               (directory / "Virtual-1-a-b").string());
}

TEST_CASE("studio links: a link that cannot be made leaves no port behind")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;

    // The directory is open to everyone, so the link layer refuses it.
    const auto open = scratch.path() / "open";
    fs::create_directory(open);
    fs::permissions(open, fs::perms::all, fs::perm_options::replace);

    studio app(linked_options(open));
    const auto created = app.create_virtual_port("x", virtual_port_kind::pty);
    REQUIRE(!created);
    REQUIRE_EQ(created.error(), make_error_code(errc::permission_denied));
    REQUIRE(app.endpoint_infos().empty());

    // A name another live port already holds is refused, not taken over.
    const auto good = scratch.path() / "good";
    studio first(linked_options(good));
    studio second(linked_options(good));
    REQUIRE(first.create_virtual_port("same", virtual_port_kind::pty));
    const auto clash =
        second.create_virtual_port("same", virtual_port_kind::pty);
    REQUIRE(!clash);
    REQUIRE_EQ(clash.error(), make_error_code(errc::already_exists));
    REQUIRE(second.endpoint_infos().empty());
}

TEST_CASE("studio links: without the option nothing is linked")
{
    scratch_directory scratch;
    studio_options options;
    options.stable_dir = (scratch.path() / "links").string();

    studio app(options);
    const auto created =
        app.create_virtual_port("plain", virtual_port_kind::loopback);
    REQUIRE(created);
    REQUIRE_EQ(app.advertised_path((*created)->id()),
               std::string("loopback:plain"));
    REQUIRE(!fs::exists(scratch.path() / "links"));
}
