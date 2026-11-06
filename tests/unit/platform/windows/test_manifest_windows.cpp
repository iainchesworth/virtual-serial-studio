//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every Windows program the project builds carries an application manifest that
// says it never needs administrator rights, and a version resource that names
// it (cmake/VssWindowsResources.cmake). This test reads both from the running
// test program, which is one of those programs.
//

#include "test_framework.hpp"

#include <string>
#include <string_view>

// clang-format off
#include <windows.h>
// clang-format on

namespace {

// The resource types by number (RT_MANIFEST and RT_VERSION are ANSI or wide
// pointers depending on UNICODE, which this project does not define).
constexpr WORD manifest_type = 24;
constexpr WORD version_type = 16;

std::string own_resource(WORD type, WORD id)
{
    const HRSRC found = ::FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(type));
    if (found == nullptr)
        return {};
    const HGLOBAL loaded = ::LoadResource(nullptr, found);
    if (loaded == nullptr)
        return {};
    const void* data = ::LockResource(loaded);
    const DWORD size = ::SizeofResource(nullptr, found);
    if (data == nullptr || size == 0)
        return {};
    return std::string(static_cast<const char*>(data), size);
}

bool contains(const std::string& text, std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("manifest: the program asks to run as the invoking user")
{
    const std::string manifest = own_resource(manifest_type, 1);
    REQUIRE(!manifest.empty());
    REQUIRE(contains(manifest, "requestedExecutionLevel level=\"asInvoker\""));
    REQUIRE(contains(manifest, "uiAccess=\"false\""));
    // The program must never ask for more, whatever else the file holds.
    REQUIRE(!contains(manifest, "requireAdministrator"));
    REQUIRE(!contains(manifest, "highestAvailable"));
}

TEST_CASE("manifest: long paths and UTF-8 are declared")
{
    const std::string manifest = own_resource(manifest_type, 1);
    REQUIRE(contains(manifest, "<ws2016:longPathAware>true</ws2016:longPathAware>"));
    REQUIRE(contains(manifest, "<ws2019:activeCodePage>UTF-8</ws2019:activeCodePage>"));
}

TEST_CASE("manifest: the program has a version resource that names it")
{
    const std::string version = own_resource(version_type, 1);
    REQUIRE(!version.empty());
    // The strings are UTF-16 inside the block, so look for the first letters of
    // the product name as wide characters.
    const std::wstring wide(reinterpret_cast<const wchar_t*>(version.data()),
                            version.size() / sizeof(wchar_t));
    REQUIRE(wide.find(L"Virtual Serial Studio") != std::wstring::npos);
    REQUIRE(wide.find(L"OriginalFilename") != std::wstring::npos);
}
