//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The runner of the UI suite. It differs from the other GUI runners in three
// ways: QSettings go to a private directory of this process, so two GUI test
// processes never share recent files or the saved mode; it prints the time
// of every journey; and --list names the journeys for the docs.
//

#include "qt_settings.hpp"
#include "test_framework.hpp"

#include "ui_harness.hpp"
#include "ui_platform.hpp"

#include "theme/theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

namespace
{

bool matches(const std::string& name, const std::vector<std::string>& filters)
{
    if (filters.empty())
        return true;
    for (const auto& filter : filters)
    {
        if (name.find(filter) != std::string::npos)
            return true;
    }
    return false;
}

/// One line per journey for the performance report: journey, outcome, time
/// (ms), steps, the longest wall time and the most CPU time of the GUI thread
/// in one gap (ms), the budget it was held to and the step of the CPU peak.
void record_perf(const std::string& path, const char* name,
                 long long milliseconds, const char* outcome)
{
    if (path.empty())
        return;
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file)
        return;
    const auto& probe = vss::gui::ui::Probe::instance();
    file << '"' << name << "\"," << outcome << ',' << milliseconds << ','
         << probe.steps() << ',' << probe.worst_milliseconds() << ','
         << probe.worst_cpu_milliseconds() << ',' << probe.budget() << ",\""
         << probe.worst_cpu_step() << "\"\n";
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    vss::gui::ui::platform::prepare_environment();
    vss::gui::ui::install_crash_reporter();
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VSS GUI UI Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("VSS GUI UI Tests"));

    // Settings live in a directory only this process knows (see
    // tests/framework/qt_settings.hpp), so nothing reaches the registry and a
    // second test process cannot read them.
    vss_test::isolated_settings isolated;
    // Anything a journey saves without being given a place (Save writes
    // "lab.json" to the current directory) lands here, not in the repository.
    QTemporaryDir working_directory;
    if (working_directory.isValid())
        QDir::setCurrent(working_directory.path());
    QSettings().clear();

    vss::gui::ui::platform::prepare_application();
    vss::gui::Theme::instance().initialise();

    // VSS_UI_PERF_REPORT names a CSV file to append one line per journey to.
    const std::string perf_path =
        qEnvironmentVariable("VSS_UI_PERF_REPORT").toStdString();
    if (!perf_path.empty())
        QDir().mkpath(
            QFileInfo(QString::fromStdString(perf_path)).absolutePath());
    std::vector<std::string> filters;
    bool verbose = false;
    bool list = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--verbose" || argument == "-v")
            verbose = true;
        else if (argument == "--list")
            list = true;
        else if (argument.rfind("--filter=", 0) == 0)
            filters.push_back(argument.substr(std::strlen("--filter=")));
    }

    std::size_t selected = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    const auto begin = std::chrono::steady_clock::now();

    for (const auto& test : vss_test::registry())
    {
        if (!matches(test.name, filters))
            continue;
        if (list)
        {
            std::printf("%s\n", test.name);
            continue;
        }
        ++selected;
        if (verbose)
            std::printf("[RUN ] %s\n", test.name);
        std::fflush(stdout);
        const char* outcome_of_last_test = "error";
        const auto started = std::chrono::steady_clock::now();
        const auto elapsed = [&started]
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                .count();
        };
        try
        {
            test.function();
            ++passed;
            outcome_of_last_test = "pass";
            if (verbose)
                std::printf("[PASS] %s (%lld ms)\n", test.name,
                            static_cast<long long>(elapsed()));
        }
        catch (const vss_test::skip_signal& skip)
        {
            ++skipped;
            outcome_of_last_test = "skip";
            std::printf("[SKIP] %s: %s\n", test.name, skip.what());
        }
        catch (const vss_test::failure& failure)
        {
            ++failed;
            outcome_of_last_test = "fail";
            std::printf("[FAIL] %s (%lld ms)\n        %s\n", test.name,
                        static_cast<long long>(elapsed()), failure.what());
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::printf("[FAIL] %s\n        unexpected exception: %s\n",
                        test.name, error.what());
        }
        catch (...)
        {
            ++failed;
            std::printf("[FAIL] %s\n        unknown exception\n", test.name);
        }
        std::fflush(stdout);
        record_perf(perf_path, test.name, elapsed(), outcome_of_last_test);
    }

    if (list)
        return 0;
    const auto total = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - begin)
                           .count();
    std::printf("\n%zu tests, %zu passed, %zu failed, %zu skipped, %lld s\n",
                selected, passed, failed, skipped,
                static_cast<long long>(total));
    return failed == 0 ? 0 : 1;
}
