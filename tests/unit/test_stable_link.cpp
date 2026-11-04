//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/stable_link.hpp>

#include "test_framework.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace vss;
namespace fs = std::filesystem;

namespace
{

/// A fresh directory under the system temp directory, removed on exit.  It is
/// made with the default mode, so a test that wants the module to make its own
/// private directory names a child of it that does not exist yet.
class scratch_directory
{
public:
    scratch_directory()
    {
        static std::atomic<unsigned> counter{0};
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("vss-link-test-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1)));
        fs::create_directories(path_);
    }

    ~scratch_directory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    scratch_directory(const scratch_directory&) = delete;
    scratch_directory& operator=(const scratch_directory&) = delete;

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

env_lookup fake_environment(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](
               std::string_view name) -> std::optional<std::string>
    {
        const auto found = values.find(std::string(name));
        if (found == values.end())
            return std::nullopt;
        return found->second;
    };
}

bool owner_only(const fs::path& directory)
{
    using fs::perms;
    const auto bits = fs::status(directory).permissions();
    return (bits & (perms::group_all | perms::others_all)) == perms::none;
}

} // namespace

TEST_CASE("stable link: Windows has no link layer")
{
    if (stable_links_supported())
        return;

    scratch_directory scratch;
    REQUIRE_EQ(make_stable_link(scratch.path() / "x", "a", "/dev/null"),
               make_error_code(errc::unsupported));
    REQUIRE_EQ(resolve_link_directory(fake_environment({})),
               make_error_code(errc::unsupported));
}

TEST_CASE("stable link: names are one safe path component")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";

    for (const char* bad : {"", ".", "..", "a/b", "a b", "a\\b", "tty:0",
                            "caf\xc3\xa9", "../escape"})
    {
        REQUIRE_EQ(make_stable_link(directory, bad, "/dev/null"),
                   make_error_code(errc::invalid_argument));
    }
    REQUIRE_EQ(make_stable_link(directory, std::string(65, 'a'), "/dev/null"),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(make_stable_link(directory, "ok", ""),
               make_error_code(errc::invalid_argument));

    // Nothing was created for a rejected name.
    REQUIRE(!fs::exists(directory / "escape"));

    const auto longest =
        make_stable_link(directory, std::string(64, 'a'), "/dev/null");
    REQUIRE(longest.has_value());
    const auto dotted = make_stable_link(directory, "vss_tty-0.a", "/dev/null");
    REQUIRE(dotted.has_value());
}

TEST_CASE("stable link: a link is made in a private directory")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";
    const auto made = make_stable_link(directory, "ttyVSS0", "/dev/null");
    REQUIRE(made.has_value());
    REQUIRE_EQ(made->path, (directory / "ttyVSS0").string());
    REQUIRE_EQ(made->target, std::string("/dev/null"));
    REQUIRE(fs::is_symlink(directory / "ttyVSS0"));
    REQUIRE_EQ(fs::read_symlink(directory / "ttyVSS0").string(),
               std::string("/dev/null"));
    REQUIRE(owner_only(directory));

    // Asking again for the same name and target changes nothing.
    const auto again = make_stable_link(directory, "ttyVSS0", "/dev/null");
    REQUIRE(again.has_value());
    REQUIRE(*again == *made);
}

TEST_CASE("stable link: a stale link is replaced and a live one is refused")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";
    const auto gone = (scratch.path() / "no-such-device").string();

    // The first process died and left a link to a device that is gone.
    const auto first = make_stable_link(directory, "port", gone);
    REQUIRE(first.has_value());
    const auto second = make_stable_link(directory, "port", "/dev/null");
    REQUIRE(second.has_value());
    REQUIRE_EQ(fs::read_symlink(directory / "port").string(),
               std::string("/dev/null"));

    // The link now names a live character device, so a different target must
    // not take it over.
    REQUIRE_EQ(make_stable_link(directory, "port", gone),
               make_error_code(errc::already_exists));
    REQUIRE_EQ(fs::read_symlink(directory / "port").string(),
               std::string("/dev/null"));

    // A link to something that exists but is not a device is also stale.
    const auto plain = scratch.path() / "plain-file";
    std::ofstream(plain) << "not a device";
    const auto third = make_stable_link(directory, "file", plain.string());
    REQUIRE(third.has_value());
    const auto fourth = make_stable_link(directory, "file", "/dev/null");
    REQUIRE(fourth.has_value());
}

TEST_CASE("stable link: a real file or directory is never replaced")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";
    REQUIRE(make_stable_link(directory, "first", "/dev/null").has_value());

    std::ofstream(directory / "file") << "mine";
    REQUIRE_EQ(make_stable_link(directory, "file", "/dev/null"),
               make_error_code(errc::already_exists));
    REQUIRE(fs::is_regular_file(directory / "file"));

    fs::create_directory(directory / "folder");
    REQUIRE_EQ(make_stable_link(directory, "folder", "/dev/null"),
               make_error_code(errc::already_exists));
    REQUIRE(fs::is_directory(directory / "folder"));
}

TEST_CASE("stable link: removal only takes a link that is still ours")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";
    const auto made = make_stable_link(directory, "mine", "/dev/null");
    REQUIRE(made.has_value());

    // An operator replaced it: it points somewhere else now.
    fs::remove(directory / "mine");
    fs::create_symlink("/dev/zero", directory / "mine");
    remove_stable_link(*made);
    REQUIRE(fs::is_symlink(directory / "mine"));

    // A directory with the name is not touched.
    fs::remove(directory / "mine");
    fs::create_directory(directory / "mine");
    remove_stable_link(*made);
    REQUIRE(fs::is_directory(directory / "mine"));
    fs::remove(directory / "mine");

    // A link that still points at the target goes, and removing it twice is
    // harmless.
    const auto again = make_stable_link(directory, "mine", "/dev/null");
    REQUIRE(again.has_value());
    remove_stable_link(*again);
    REQUIRE(!fs::exists(fs::symlink_status(directory / "mine")));
    remove_stable_link(*again);
}

TEST_CASE("stable link: the directory comes from the environment in order")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;
    const auto chosen = scratch.path() / "chosen";
    const auto runtime = scratch.path() / "runtime";
    fs::create_directories(runtime);
    fs::permissions(runtime, fs::perms::owner_all, fs::perm_options::replace);

    // VSS_VPORT_DIR wins over XDG_RUNTIME_DIR.
    const auto first = resolve_link_directory(
        fake_environment({{"VSS_VPORT_DIR", chosen.string()},
                          {"XDG_RUNTIME_DIR", runtime.string()}}));
    REQUIRE(first.has_value());
    REQUIRE_EQ(first->string(), chosen.string());
    REQUIRE(owner_only(chosen));

    // XDG_RUNTIME_DIR gives a vss directory inside it, and an empty
    // VSS_VPORT_DIR counts as unset.
    const auto second = resolve_link_directory(fake_environment(
        {{"VSS_VPORT_DIR", ""}, {"XDG_RUNTIME_DIR", runtime.string()}}));
    REQUIRE(second.has_value());
    REQUIRE_EQ(second->string(), (runtime / "vss").string());
    REQUIRE(owner_only(runtime / "vss"));

    // With neither variable the platform's own directory is used.
    const auto third = resolve_link_directory(fake_environment({}));
    if (third.has_value())
        REQUIRE(owner_only(*third));
}

TEST_CASE("stable link: an unsafe directory is refused, not used")
{
    if (!stable_links_supported())
        return;

    scratch_directory scratch;

    // Open to the group and to everyone.
    const auto open = scratch.path() / "open";
    fs::create_directory(open);
    fs::permissions(open, fs::perms::all, fs::perm_options::replace);
    REQUIRE_EQ(make_stable_link(open, "x", "/dev/null"),
               make_error_code(errc::permission_denied));

    // A named directory has no fallback to the next candidate.
    const auto refused = resolve_link_directory(
        fake_environment({{"VSS_VPORT_DIR", open.string()}}));
    REQUIRE(!refused.has_value());

    // A link in place of the directory is not followed.
    const auto real = scratch.path() / "real";
    fs::create_directory(real);
    fs::permissions(real, fs::perms::owner_all, fs::perm_options::replace);
    const auto alias = scratch.path() / "alias";
    fs::create_directory_symlink(real, alias);
    REQUIRE_EQ(make_stable_link(alias, "x", "/dev/null"),
               make_error_code(errc::permission_denied));
    REQUIRE(!fs::exists(fs::symlink_status(real / "x")));

    // A regular file in the way is refused too.
    const auto file = scratch.path() / "file";
    std::ofstream(file) << "x";
    REQUIRE(!make_stable_link(file, "x", "/dev/null").has_value());
}

TEST_CASE("stable link: a picker lists live links and leaves out dead ones")
{
    if (!stable_links_supported())
    {
        REQUIRE(list_stable_links(fake_environment({})).empty());
        return;
    }

    scratch_directory scratch;
    const auto directory = scratch.path() / "links";
    REQUIRE(make_stable_link(directory, "live", "/dev/null").has_value());
    // The remains of a process that died: a link to nothing.
    REQUIRE(
        make_stable_link(directory, "dead", (scratch.path() / "gone").string())
            .has_value());
    std::ofstream(directory / "file") << "not a link";

    const auto links = list_stable_links(
        fake_environment({{"VSS_VPORT_DIR", directory.string()}}));
    REQUIRE_EQ(links.size(), std::size_t{1});
    REQUIRE_EQ(links.front().path, (directory / "live").string());
    REQUIRE_EQ(links.front().target, std::string("/dev/null"));

    // Looking does not create anything.
    const auto missing = scratch.path() / "missing";
    REQUIRE(list_stable_links(
                fake_environment({{"VSS_VPORT_DIR", missing.string()}}))
                .empty());
    REQUIRE(!fs::exists(missing));

    // A directory that is open to others is not read.
    const auto open = scratch.path() / "open";
    fs::create_directory(open);
    fs::create_symlink("/dev/null", open / "x");
    fs::permissions(open, fs::perms::all, fs::perm_options::replace);
    REQUIRE(
        list_stable_links(fake_environment({{"VSS_VPORT_DIR", open.string()}}))
            .empty());
}
