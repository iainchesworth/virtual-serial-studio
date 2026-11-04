//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/build_info.hpp"

#include "language/language_choice.hpp"

#include <QCoreApplication>
#include <QSysInfo>
#include <QtGlobal>

#if __has_include(<boost/version.hpp>)
#include <boost/version.hpp>
#endif

namespace vss::gui::support
{
namespace
{

#ifndef VSS_GUI_VERSION
#define VSS_GUI_VERSION "unknown"
#endif
#ifndef VSS_GIT_COMMIT
#define VSS_GIT_COMMIT "unknown"
#endif
#ifndef VSS_GIT_BRANCH
#define VSS_GIT_BRANCH "unknown"
#endif
#ifndef VSS_BUILD_DATE
#define VSS_BUILD_DATE "unknown"
#endif
#ifndef VSS_BUILD_TYPE
#define VSS_BUILD_TYPE "unknown"
#endif
#ifndef VSS_COMPILER
#define VSS_COMPILER "unknown"
#endif

QString boost_version()
{
#ifdef BOOST_VERSION
    return QStringLiteral("%1.%2.%3")
        .arg(BOOST_VERSION / 100000)
        .arg((BOOST_VERSION / 100) % 1000)
        .arg(BOOST_VERSION % 100);
#else
    return QStringLiteral("unknown");
#endif
}

} // namespace

build_info current_build_info()
{
    build_info info;
    info.version = QStringLiteral(VSS_GUI_VERSION);
    info.commit = QStringLiteral(VSS_GIT_COMMIT);
    info.branch = QStringLiteral(VSS_GIT_BRANCH);
    info.build_date = QStringLiteral(VSS_BUILD_DATE);
    info.build_type = QStringLiteral(VSS_BUILD_TYPE);
    if (info.build_type.isEmpty())
        info.build_type = QStringLiteral("unknown");
    info.compiler = QStringLiteral(VSS_COMPILER);
    info.qt_runtime = QString::fromLatin1(qVersion());
    info.qt_build = QStringLiteral(QT_VERSION_STR);
    info.boost = boost_version();
    info.operating_system = QSysInfo::prettyProductName();
    info.architecture = QSysInfo::currentCpuArchitecture();
    info.kernel = QStringLiteral("%1 %2").arg(QSysInfo::kernelType(),
                                              QSysInfo::kernelVersion());
    info.language = vss::gui::language_choice_description();
    return info;
}

QVector<environment_row> environment_rows(const build_info& info)
{
    return {
        {QStringLiteral("Commit"), info.commit},
        {QStringLiteral("Branch"), info.branch},
        {QStringLiteral("Build"),
         QStringLiteral("%1, %2").arg(info.build_date, info.build_type)},
        {QStringLiteral("Compiler"), info.compiler},
        {QStringLiteral("Qt"), QStringLiteral("%1, built with %2")
                                   .arg(info.qt_runtime, info.qt_build)},
        {QStringLiteral("Boost"), info.boost},
        {QStringLiteral("Operating system"), info.operating_system},
        {QStringLiteral("Architecture"),
         QStringLiteral("%1, kernel %2").arg(info.architecture, info.kernel)},
        {QStringLiteral("Language"), info.language},
    };
}

QString environment_report(const build_info& info)
{
    QString report =
        QStringLiteral("Virtual Serial Studio %1\n").arg(info.version);
    for (const environment_row& row : environment_rows(info))
        report += QStringLiteral("%1: %2\n").arg(row.label, row.value);
    return report;
}

QString environment_report()
{
    return environment_report(current_build_info());
}

} // namespace vss::gui::support
