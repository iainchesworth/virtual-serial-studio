//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct options
{
    std::vector<std::string> filters;
    std::vector<std::string> excludes;
    std::string junit_path;
    bool list = false;
    bool verbose = false;
    int repeat = 1;
    bool tracy = false; ///< profile the run (the same as --profile)
};

struct outcome
{
    enum class status
    {
        passed,
        failed,
        skipped
    };

    const vss_test::test_info* test = nullptr;
    status result = status::failed;
    std::string message;
    std::chrono::microseconds duration{0};
};

std::string xml_escape(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char value : text)
    {
        switch (value)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(value); break;
        }
    }
    return out;
}

bool matches(const std::string& name, const std::vector<std::string>& filters)
{
    if (filters.empty())
        return true;
    return std::any_of(filters.begin(), filters.end(),
                       [&name](const std::string& filter) {
                           return name.find(filter) != std::string::npos;
                       });
}

options parse(int argc, char** argv)
{
    options parsed;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const auto value = [&](const std::string& prefix) -> std::string {
            return argument.size() > prefix.size() ? argument.substr(prefix.size())
                                                   : std::string();
        };

        if (argument == "--list")
            parsed.list = true;
        else if (argument == "--verbose" || argument == "-v")
            parsed.verbose = true;
        else if (argument.rfind("--filter=", 0) == 0)
            parsed.filters.push_back(value("--filter="));
        else if (argument.rfind("--exclude=", 0) == 0)
            parsed.excludes.push_back(value("--exclude="));
        else if (argument.rfind("--junit=", 0) == 0)
            parsed.junit_path = value("--junit=");
        else if (argument.rfind("--repeat=", 0) == 0)
            parsed.repeat = std::max(1, std::stoi(value("--repeat=")));
        else if (argument == "--tracy")
            parsed.tracy = true;
    }
    return parsed;
}

bool write_junit(const std::string& path, const std::vector<outcome>& outcomes,
                 std::chrono::milliseconds elapsed)
{
    std::size_t failures = 0;
    std::size_t skipped = 0;
    for (const auto& entry : outcomes)
    {
        if (entry.result == outcome::status::failed)
            ++failures;
        else if (entry.result == outcome::status::skipped)
            ++skipped;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;

    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<testsuites tests=\"" << outcomes.size() << "\" failures=\""
           << failures << "\" skipped=\"" << skipped << "\" time=\""
           << (static_cast<double>(elapsed.count()) / 1000.0) << "\">\n";
    stream << "  <testsuite name=\"vss\" tests=\"" << outcomes.size()
           << "\" failures=\"" << failures << "\" skipped=\"" << skipped
           << "\">\n";

    for (const auto& entry : outcomes)
    {
        stream << "    <testcase classname=\"vss\" name=\""
               << xml_escape(entry.test->name) << "\" time=\""
               << (static_cast<double>(entry.duration.count()) / 1'000'000.0)
               << "\"";
        if (entry.result == outcome::status::passed)
        {
            stream << "/>\n";
            continue;
        }

        stream << ">\n";
        if (entry.result == outcome::status::skipped)
        {
            stream << "      <skipped message=\""
                   << xml_escape(entry.message) << "\"/>\n";
        }
        else
        {
            stream << "      <failure message=\""
                   << xml_escape(entry.message) << "\">"
                   << xml_escape(entry.message) << "</failure>\n";
        }
        stream << "    </testcase>\n";
    }

    stream << "  </testsuite>\n</testsuites>\n";
    stream.flush();
    return static_cast<bool>(stream);
}

} // namespace

int main(int argc, char** argv)
{
    const options configuration = parse(argc, argv);

    // --tracy, --profile[=PORT], --profile-wait=S and VSS_PROFILE=1 turn
    // profiling on for this run. Each case is a zone named after it, and the
    // duration of each lands on the test.duration_us plot.
    {
        auto profile = vss::profiling::request_from_environment(false);
        for (int index = 1; index < argc; ++index)
            static_cast<void>(
                vss::profiling::take_argument(argv[index], profile));
        if (configuration.tracy)
            profile.enable = true;
        if (profile.enable)
        {
            const auto started = vss::profiling::start(profile);
            if (started)
                std::cerr << vss::profiling::describe(*started) << '\n';
            else
                std::cerr << "cannot start profiling: "
                          << started.error().message() << '\n';
        }
    }
    VSS_PROFILE_THREAD("test.main");

    std::vector<const vss_test::test_info*> selected;
    for (const auto& test : vss_test::registry())
    {
        const std::string name = test.name;
        if (!matches(name, configuration.filters))
            continue;
        if (!configuration.excludes.empty()
            && matches(name, configuration.excludes))
            continue;
        selected.push_back(&test);
    }

    std::cout << std::unitbuf; // CI captures the stream; never lose a line

    if (configuration.list)
    {
        for (const auto* test : selected)
            std::cout << test->name << '\n';
        return 0;
    }

    const auto started = std::chrono::steady_clock::now();
    std::vector<outcome> outcomes;
    std::size_t failures = 0;
    std::size_t skips = 0;

    for (int repetition = 0; repetition < configuration.repeat; ++repetition)
    {
        for (const auto* test : selected)
        {
            outcome entry;
            entry.test = test;

            // Announce the case before it runs: if it hangs, the last line in
            // the log names it.
            if (configuration.verbose)
                std::cout << "[RUN ] " << test->name << '\n';

            const auto case_started = std::chrono::steady_clock::now();
            try
            {
                test->function();
                entry.result = outcome::status::passed;
            }
            catch (const vss_test::skip_signal& skipped)
            {
                entry.result = outcome::status::skipped;
                entry.message = skipped.reason();
                ++skips;
            }
            catch (const vss_test::failure& error)
            {
                entry.result = outcome::status::failed;
                entry.message = error.what();
                ++failures;
            }
            catch (const std::exception& error)
            {
                entry.result = outcome::status::failed;
                entry.message = std::string("unexpected exception: ")
                    + error.what();
                ++failures;
            }
            catch (...)
            {
                entry.result = outcome::status::failed;
                entry.message = "unknown exception";
                ++failures;
            }

            entry.duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - case_started);
            VSS_PROFILE_PLOT("test.duration_us", entry.duration.count());
            VSS_PROFILE_FRAME_NAMED("test.case");
            if (entry.result == outcome::status::failed)
                VSS_PROFILE_MESSAGE_TEXT(std::string("test.failed ") +
                                         test->name);

            if (configuration.verbose
                || entry.result != outcome::status::passed)
            {
                const char* label =
                    entry.result == outcome::status::passed ? "PASS"
                    : entry.result == outcome::status::skipped ? "SKIP"
                                                               : "FAIL";
                std::cout << '[' << label << "] " << test->name;
                if (entry.result != outcome::status::passed)
                    std::cout << "\n        " << entry.message;
                std::cout << '\n';
            }

            outcomes.push_back(std::move(entry));
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    // A report that was asked for and cannot be written must not pass quietly.
    bool report_written = true;
    if (!configuration.junit_path.empty() &&
        !write_junit(configuration.junit_path, outcomes, elapsed))
    {
        std::cerr << "cannot write the JUnit report "
                  << configuration.junit_path << '\n';
        report_written = false;
    }

    std::cout << "\n"
              << selected.size() << " tests, "
              << (outcomes.size() - failures - skips) << " passed, " << failures
              << " failed, " << skips << " skipped in " << elapsed.count()
              << " ms\n";

    return failures == 0 && report_written ? 0 : 1;
}
