//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "main_window.hpp"

#include "theme/theme.hpp"
#include "widgets/gallery.hpp"

#include <vss/studio/studio.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <thread>

int main(int argc, char* argv[])
{
    bool selftest = false;
    bool screenshot = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--selftest") == 0)
            selftest = true;
        if (std::strncmp(argv[index], "--screenshot", 12) == 0)
            screenshot = true;
    }

    // The self-test and the documentation screenshot are headless by
    // contract: set the platform plugin before QApplication reads it.
    if (selftest || screenshot)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Virtual Serial Studio"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("Virtual Serial Studio"));
    QApplication::setApplicationVersion(QStringLiteral(VSS_GUI_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Virtual Serial Studio"));
    QApplication::setOrganizationDomain(
        QStringLiteral("virtualserial.example"));

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Virtual Serial Studio - a playground for real, virtual "
                       "and networked serial ports."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption selftest_option(
        QStringLiteral("selftest"),
        QStringLiteral("Run the headless end-to-end check (offscreen "
                       "platform), print the result and exit."));
    parser.addOption(selftest_option);
    const QCommandLineOption screenshot_option(
        QStringLiteral("screenshot"),
        QStringLiteral("Render the main window to <file> and exit (headless)."),
        QStringLiteral("file"));
    parser.addOption(screenshot_option);
    parser.process(application);

    if (qEnvironmentVariableIsSet("VSS_WIDGET_GALLERY"))
    {
        vss::gui::Theme::instance().initialise();
        vss::gui::WidgetGallery gallery;
        gallery.resize(1240, 900);
        gallery.show();
        return application.exec();
    }

    try
    {
        vss::studio_options options;
        auto studio = std::make_unique<vss::studio>(options);
        vss::gui::MainWindow window(*studio);

        if (parser.isSet(selftest_option))
        {
            const bool passed = window.run_selftest();
            if (passed)
            {
                std::fputs("selftest ok\n", stdout);
                std::fflush(stdout);
                return 0;
            }
            std::fflush(stderr);
            return 1;
        }

        if (parser.isSet(screenshot_option))
        {
            // A representative topology so the documentation image shows the
            // real application rather than an empty window.
            studio->create_virtual_port("console",
                                        vss::virtual_port_kind::loopback);
            studio->create_virtual_port("logger",
                                        vss::virtual_port_kind::loopback);
            studio->create_mock_port("modem");
            if (const auto console = studio->find("console"))
            {
                if (const auto logger = studio->find("logger"))
                    studio->redirect("console", "logger");
                console->write(vss::as_bytes("AT+VERSION\r\n"));
                console->write(vss::as_bytes("Virtual Serial Studio 0.1.0\r\n"));
            }

            window.resize(1400, 880);
            window.show();
            window.refresh_now();
            for (int frame = 0; frame < 8; ++frame)
            {
                application.processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            window.refresh_now();
            application.processEvents();

            const QPixmap image = window.grab();
            const QString path = parser.value(screenshot_option);
            if (!image.save(path))
            {
                std::fprintf(stderr, "cannot write %s\n", path.toUtf8().constData());
                return 1;
            }
            std::printf("wrote %s (%dx%d)\n", path.toUtf8().constData(),
                        image.width(), image.height());
            return 0;
        }

        window.show();
        return application.exec();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "vss_gui: fatal: %s\n", error.what());
        if (!selftest)
        {
            QMessageBox::critical(
                nullptr, QStringLiteral("Virtual Serial Studio"),
                QString::fromUtf8(error.what()));
        }
        return 1;
    }
}
