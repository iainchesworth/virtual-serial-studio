//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "theme/theme.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QString>

#include <cstdio>
#include <cstring>
#include <exception>
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

} // namespace

int main(int argc, char** argv)
try
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#if defined(Q_OS_WIN)
    // The offscreen platform has no native font database on Windows.  The
    // product uses the system font; the test points at the system font
    // directory rather than bundling a font.
    if (qgetenv("QT_QPA_FONTDIR").isEmpty())
        qputenv("QT_QPA_FONTDIR", QByteArrayLiteral("C:/Windows/Fonts"));
#endif
    QApplication application(argc, argv);
#if defined(Q_OS_WIN)
    // Widgets that do not set an explicit Theme font (fields, plain buttons)
    // use the application font.  On the offscreen platform that is the basic
    // database's first family; use the real Windows UI face for review shots.
    if (QFontDatabase::families().contains(QStringLiteral("Segoe UI")))
        QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
#endif
    vss::gui::Theme::instance().initialise();

    std::vector<std::string> filters;
    bool verbose = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--verbose" || argument == "-v")
            verbose = true;
        else if (argument.rfind("--filter=", 0) == 0)
            filters.push_back(argument.substr(std::strlen("--filter=")));
    }

    std::size_t selected = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;

    for (const auto& test : vss_test::registry())
    {
        if (!matches(test.name, filters))
            continue;
        ++selected;
        if (verbose)
            std::printf("[RUN ] %s\n", test.name);
        try
        {
            test.function();
            ++passed;
            if (verbose)
                std::printf("[PASS] %s\n", test.name);
        }
        catch (const vss_test::skip_signal& skip)
        {
            ++skipped;
            std::printf("[SKIP] %s: %s\n", test.name, skip.what());
        }
        catch (const vss_test::failure& failure)
        {
            ++failed;
            std::printf("[FAIL] %s\n        %s\n", test.name, failure.what());
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
    }

    std::printf("\n%zu tests, %zu passed, %zu failed, %zu skipped\n", selected,
                passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "error: %s\n", error.what());
    return 2;
}
catch (...)
{
    std::fputs("error: unknown exception\n", stderr);
    return 2;
}
