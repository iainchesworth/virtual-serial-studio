//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "about_dialog.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontInfo>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#ifndef VSS_GUI_VERSION
#  define VSS_GUI_VERSION "unknown"
#endif
#ifndef VSS_GIT_COMMIT
#  define VSS_GIT_COMMIT "unknown"
#endif
#ifndef VSS_GIT_BRANCH
#  define VSS_GIT_BRANCH "unknown"
#endif
#ifndef VSS_BUILD_DATE
#  define VSS_BUILD_DATE "unknown"
#endif
#ifndef VSS_COMPILER
#  define VSS_COMPILER "unknown"
#endif
#ifndef VSS_BUILD_TYPE
#  define VSS_BUILD_TYPE "unknown"
#endif

namespace vss::gui {
namespace {

QTextBrowser* make_browser(const QString& html)
{
    auto* browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setHtml(html);
    return browser;
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About Virtual Serial Studio"));
    setModal(true);
    resize(720, 520);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(
        make_browser(
            tr("<h2>Virtual Serial Studio</h2>"
               "<p><b>Version %1</b> — %2</p>"
               "<p>A playground for real, virtual and networked serial "
               "ports. Ports are created on demand, wired together as "
               "split/join/redirect/monitor links, and driven through the "
               "same studio engine the command line and tests use.</p>"
               "<p>Built on Boost.Asio, Boost.VirtualSerial and Qt 6.</p>"
               "<p>Copyright &copy; 2026 Virtual Serial Studio "
               "contributors.</p>"
               "<p>This program is free software: you can redistribute it "
               "and/or modify it under the terms of the GNU General Public "
               "License as published by the Free Software Foundation, "
               "either version 3 of the License, or (at your option) any "
               "later version.</p>"
               "<p>It is distributed in the hope that it will be useful, "
               "but WITHOUT ANY WARRANTY; without even the implied warranty "
               "of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
               "See the GNU General Public License for more details.</p>"
               "<p><a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">"
               "gnu.org/licenses/gpl-3.0.html</a></p>")
                .arg(QStringLiteral(VSS_GUI_VERSION),
                     tr("the serial port workbench"))),
        tr("&About"));

    tabs_->addTab(
        make_browser(
            tr("<h3>Environment</h3>"
               "<table cellpadding=4>"
               "<tr><td><b>Commit</b></td><td>%1</td></tr>"
               "<tr><td><b>Branch</b></td><td>%2</td></tr>"
               "<tr><td><b>Built</b></td><td>%3 (%4)</td></tr>"
               "<tr><td><b>Compiler</b></td><td>%5</td></tr>"
               "<tr><td><b>Qt</b></td><td>%6 (built against %7)</td></tr>"
               "<tr><td><b>Operating system</b></td><td>%8</td></tr>"
               "<tr><td><b>Architecture</b></td><td>%9</td></tr>"
               "<tr><td><b>Kernel</b></td><td>%10</td></tr>"
               "</table>"
               "<p>Include this table in a bug report; it answers the "
               "\"which build?\" question without guesswork.</p>")
                .arg(QStringLiteral(VSS_GIT_COMMIT))
                .arg(QStringLiteral(VSS_GIT_BRANCH))
                .arg(QString::fromLatin1(VSS_BUILD_DATE))
                .arg(QStringLiteral(VSS_BUILD_TYPE))
                .arg(QStringLiteral(VSS_COMPILER))
                .arg(QString::fromLatin1(qVersion()))
                .arg(QString::fromLatin1(QT_VERSION_STR))
                .arg(QSysInfo::prettyProductName())
                .arg(QSysInfo::currentCpuArchitecture())
                .arg(QSysInfo::kernelVersion())),
        tr("&Environment"));

    tabs_->addTab(
        make_browser(
            tr("<h3>Third-party components</h3>"
               "<table cellpadding=4>"
               "<tr><th align=left>Component</th><th align=left>Licence"
               "</th></tr>"
               "<tr><td>Qt 6 (Widgets)</td>"
               "<td>LGPL-3.0-only OR GPL-3.0-only OR commercial; linked "
               "dynamically</td></tr>"
               "<tr><td>Boost.Asio, Boost.System, Boost.JSON</td>"
               "<td>Boost Software License 1.0</td></tr>"
               "<tr><td>Boost.VirtualSerial</td>"
               "<td>Boost Software License 1.0</td></tr>"
               "<tr><td>Tracy (optional profiling client)</td>"
               "<td>BSD-3-Clause</td></tr>"
               "<tr><td>com0com (optional external driver)</td>"
               "<td>GPL-2.0; not distributed with this application</td>"
               "</tr>"
               "</table>"
               "<p>The full notices are in THIRD_PARTY_NOTICES.md in the "
               "source tree.</p>")),
        tr("&Licences"));

    const QFontInfo info(QApplication::font());
    tabs_->addTab(
        make_browser(
            tr("<h3>Fonts</h3>"
               "<p>This application does not distribute any fonts. The "
               "interface uses the platform's system UI font:</p>"
               "<ul><li><b>%1</b> — %2</li></ul>"
               "<p>The traffic monitor's monospaced view uses the platform's "
               "fixed-width font. Both fonts are supplied and licensed with "
               "your operating system; their licences are therefore governed "
               "by the OS, not by this application.</p>"
               "<p>Qt may fall back to other installed fonts when a glyph is "
               "missing; those fallbacks are also the system's.</p>")
                .arg(info.family(),
                     QApplication::font().styleHint() == QFont::Monospace
                         ? tr("monospaced")
                         : tr("proportional"))),
        tr("&Fonts"));

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_);
    layout->addWidget(buttons);
}

} // namespace vss::gui
