//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the files and directories named on the command line through a fuzz
// harness, once each. It stands in for libFuzzer's main() in ordinary builds,
// so the seed corpus and the inputs of past crashes are checked on every
// platform and compiler.
//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace
{

namespace fs = std::filesystem;

bool collect(const fs::path& root, std::vector<fs::path>& found)
{
    std::error_code ec;
    if (fs::is_regular_file(root, ec))
    {
        found.push_back(root);
        return true;
    }
    if (!fs::is_directory(root, ec))
    {
        std::cerr << "fuzz replay: cannot read " << root.string() << '\n';
        return false;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root, ec))
    {
        // Dot files (.gitkeep) keep an empty directory in version control.
        if (entry.is_regular_file() &&
            !entry.path().filename().string().starts_with('.'))
        {
            found.push_back(entry.path());
        }
    }
    if (ec)
    {
        std::cerr << "fuzz replay: " << root.string() << ": " << ec.message()
                  << '\n';
        return false;
    }
    return true;
}

// An unreadable input must fail the replay, not run as an empty one.
std::optional<std::vector<char>> read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<char> bytes{std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>()};
    if (in.bad())
        return std::nullopt;
    return bytes;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i)
    {
        if (!collect(argv[i], files))
        {
            return 1;
        }
    }
    // A misspelt directory must not turn into a test that checks nothing.
    if (files.empty())
    {
        std::cerr << "fuzz replay: no input files\n";
        return 1;
    }
    std::ranges::sort(files);

    for (const auto& file : files)
    {
        const auto contents = read_file(file);
        if (!contents)
        {
            std::cerr << "fuzz replay: cannot read " << file.string() << '\n';
            return 1;
        }
        const auto& bytes = *contents;
        static const std::uint8_t empty = 0;
        const auto* data =
            bytes.empty() ? &empty
                          : reinterpret_cast<const std::uint8_t*>(bytes.data());
        if (LLVMFuzzerTestOneInput(data, bytes.size()) != 0)
        {
            std::cerr << "fuzz replay: rejected " << file.string() << '\n';
            return 1;
        }
    }
    std::cout << "fuzz replay: " << files.size() << " inputs\n";
    return 0;
}
