//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/engine_studio_backend.hpp"
#include "shell/shell_window.hpp"
#include "theme/theme.hpp"
#include "widgets/gallery.hpp"

#include <vss/studio/studio.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QPixmap>

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
        vss::gui::EngineStudioBackend backend(*studio);
        vss::gui::ShellWindow window(backend);

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
            if (qEnvironmentVariable("VSS_SCREENSHOT_THEME") ==
                QLatin1String("dark"))
                vss::gui::Theme::instance().set_mode(
                    vss::gui::theme_mode::dark);
            else if (qEnvironmentVariable("VSS_SCREENSHOT_THEME") ==
                     QLatin1String("light"))
                vss::gui::Theme::instance().set_mode(
                    vss::gui::theme_mode::light);

            // A representative topology so the documentation image shows the
            // real application rather than an empty window.
            if (!qEnvironmentVariableIsSet("VSS_SCREENSHOT_EMPTY"))
            {
                const auto console = backend.create_virtual_port(
                    QStringLiteral("console"), vss::virtual_port_kind::loopback,
                    true);
                if (!qEnvironmentVariableIsSet("VSS_SCREENSHOT_ONE"))
                {
                    const auto logger = backend.create_virtual_port(
                        QStringLiteral("logger"),
                        vss::virtual_port_kind::loopback, true);
                    if (console && logger)
                    {
                        static_cast<void>(
                            backend.connect_ports(*console, *logger, {}));
                        static_cast<void>(backend.send(
                            *console, QByteArrayLiteral("AT+VERSION\r\n")));
                    }
                    static_cast<void>(
                        backend.create_mock_port(QStringLiteral("modem"), {},
                                                 QStringLiteral("scripted")));
                }
            }

            window.resize(1280, 800);
            window.show();
            for (int frame = 0; frame < 8; ++frame)
            {
                application.processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            application.processEvents();

            const QPixmap image = window.grab();
            const QString path = parser.value(screenshot_option);
            if (!image.save(path))
            {
                std::fprintf(stderr, "cannot write %s\n",
                             path.toUtf8().constData());
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
            QMessageBox::critical(nullptr,
                                  QStringLiteral("Virtual Serial Studio"),
                                  QString::fromUtf8(error.what()));
        }
        return 1;
    }
}
