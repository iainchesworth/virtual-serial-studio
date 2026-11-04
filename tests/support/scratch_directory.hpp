//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_TESTS_SCRATCH_DIRECTORY_HPP
#define VSS_TESTS_SCRATCH_DIRECTORY_HPP

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace vss_test_support
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
        path_ = std::filesystem::temp_directory_path() /
                ("vss-link-test-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~scratch_directory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    scratch_directory(const scratch_directory&) = delete;
    scratch_directory& operator=(const scratch_directory&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace vss_test_support

#endif // VSS_TESTS_SCRATCH_DIRECTORY_HPP
