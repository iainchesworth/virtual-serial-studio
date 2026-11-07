//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "ui_platform.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFont>
#include <QFontDatabase>
#include <QString>

#include <crtdbg.h>
#include <cstdio>
#include <stdlib.h>
#include <windows.h>

namespace vss::gui::ui::platform
{
namespace
{

void (*report_crash)(const char*) = nullptr;

[[maybe_unused]] int assertion_hook(int type, char* text, int*)
{
    if (type == _CRT_ASSERT || type == _CRT_ERROR)
        report_crash(text != nullptr ? text : "a runtime assertion");
    return FALSE;
}

} // namespace

std::int64_t thread_cpu_ns()
{
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    const auto join = [](const FILETIME& time)
    {
        return static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
            time.dwLowDateTime);
    };
    // A FILETIME counts 100 ns.
    return (join(kernel) + join(user)) * 100;
}

void prepare_environment()
{
    if (qgetenv("QT_QPA_FONTDIR").isEmpty())
        qputenv("QT_QPA_FONTDIR", QByteArrayLiteral("C:/Windows/Fonts"));
}

void prepare_application()
{
    if (QFontDatabase::families().contains(QStringLiteral("Segoe UI")))
        QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
}

void install_crash_hooks(void (*crash)(const char* what))
{
    report_crash = crash;
    SetUnhandledExceptionFilter(
        [](EXCEPTION_POINTERS* info) -> LONG
        {
            char what[96];
            std::snprintf(what, sizeof what, "unhandled exception 0x%08lx",
                          static_cast<unsigned long>(
                              info->ExceptionRecord->ExceptionCode));
            report_crash(what);
            return EXCEPTION_EXECUTE_HANDLER;
        });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // A failed assertion of the debug runtime, which Qt's Q_ASSERT also ends
    // in, opens a message box and waits for a click nobody gives. Print it
    // with the step instead. (The release runtime has no such assertions and
    // these calls compile to nothing.)
    _CrtSetReportHook(assertion_hook);
    for ([[maybe_unused]] const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
}

} // namespace vss::gui::ui::platform
