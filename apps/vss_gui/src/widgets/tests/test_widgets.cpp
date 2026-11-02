//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "theme/theme.hpp"
#include "widgets/widgets.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace
{

QString effective_name(const QAbstractButton* button)
{
    if (!button->accessibleName().isEmpty())
        return button->accessibleName();
    return button->text();
}

} // namespace

TEST_CASE("widgets: gallery renders both themes and both densities")
{
    vss::gui::WidgetGallery gallery;
    gallery.resize(1240, 1600);
    gallery.show();
    QApplication::processEvents();

    const QString directory = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("../gui-shots"));
    REQUIRE(QDir().mkpath(directory));

    gallery.set_theme(vss::gui::theme_mode::light);
    QApplication::processEvents();
    REQUIRE(!vss::gui::Theme::instance().is_dark());
    REQUIRE(!gallery.grab().isNull());
    REQUIRE(gallery.grab().save(
        QDir(directory).filePath(QStringLiteral("gallery-light.png"))));

    gallery.set_theme(vss::gui::theme_mode::dark);
    QApplication::processEvents();
    REQUIRE(vss::gui::Theme::instance().is_dark());
    REQUIRE(!gallery.grab().isNull());
    REQUIRE(gallery.grab().save(
        QDir(directory).filePath(QStringLiteral("gallery-dark.png"))));

    gallery.set_density(vss::gui::density_mode::compact);
    QApplication::processEvents();
    REQUIRE_EQ(vss::gui::Theme::instance().row_height(), 28);

    gallery.set_theme(vss::gui::theme_mode::light);
    gallery.set_density(vss::gui::density_mode::comfortable);
    QApplication::processEvents();
    gallery.close();
}

TEST_CASE("widgets: every visible button has a name and a 24px target")
{
    vss::gui::WidgetGallery gallery;
    gallery.resize(1240, 900);
    gallery.show();
    QApplication::processEvents();

    const auto buttons = gallery.findChildren<QAbstractButton*>();
    REQUIRE(!buttons.isEmpty());
    int checked = 0;
    for (const QAbstractButton* button : buttons)
    {
        if (!button->isVisibleTo(&gallery))
            continue;
        ++checked;
        REQUIRE(!effective_name(button).isEmpty());
        REQUIRE(button->height() >= 24 || button->minimumHeight() >= 24 ||
                button->sizeHint().height() >= 24);
    }
    REQUIRE(checked > 20);
    gallery.close();
}

TEST_CASE("widgets: chips, badges and tags expose their text")
{
    vss::gui::StatusChip waiting(QStringLiteral("Waiting for program"),
                                 vss::gui::StatusChip::kind::waiting);
    REQUIRE_EQ(waiting.accessibleName(), QStringLiteral("Waiting for program"));
    REQUIRE_EQ(waiting.sizeHint().height(), 22);

    vss::gui::StatusChip failed(QStringLiteral("Failed"),
                                vss::gui::StatusChip::kind::failed);
    REQUIRE_EQ(failed.get_kind(), vss::gui::StatusChip::kind::failed);
    failed.set_small(true);
    REQUIRE(failed.is_small());

    vss::gui::DirectionBadge received(vss::gui::DirectionBadge::direction::rx);
    REQUIRE_EQ(received.accessibleName(), QStringLiteral("received"));
    REQUIRE_EQ(received.text(), QStringLiteral("RX"));

    vss::gui::Tag tag(QStringLiteral("New"), vss::gui::Tag::variant::n);
    REQUIRE_EQ(tag.accessibleName(), QStringLiteral("New"));
    REQUIRE_EQ(tag.sizeHint().height(), 20);

    vss::gui::Badge badge;
    REQUIRE_EQ(badge.text(), QStringLiteral("ADVANCED"));

    vss::gui::KeyCap cap(QStringLiteral("Ctrl+K"));
    REQUIRE_EQ(cap.accessibleName(), QStringLiteral("Ctrl+K"));
}

TEST_CASE("widgets: switches, segments, cards and the stepper behave")
{
    vss::gui::Switch dtr(QStringLiteral("DTR"));
    REQUIRE_EQ(dtr.accessibleName(), QStringLiteral("DTR"));
    REQUIRE(dtr.text().contains(QStringLiteral("off")));
    dtr.setChecked(true);
    REQUIRE(dtr.text().contains(QStringLiteral("on")));
    REQUIRE(dtr.sizeHint().height() >= 24);

    vss::gui::SegmentedControl segments;
    const int tcp = segments.add_item(QStringLiteral("TCP"));
    const int udp = segments.add_item(QStringLiteral("UDP"));
    REQUIRE_EQ(segments.count(), 2);
    REQUIRE_EQ(segments.current(), tcp);
    segments.set_current(udp);
    REQUIRE_EQ(segments.current(), udp);

    vss::gui::PickCard card(QStringLiteral("Virtual port"),
                            QStringLiteral("A port other programs open."));
    REQUIRE_EQ(card.radio()->accessibleName(), QStringLiteral("Virtual port"));
    card.set_selected(true);
    REQUIRE(card.is_selected());
    REQUIRE(card.property("selected").toBool());

    vss::gui::Stepper stepper;
    stepper.set_steps({QStringLiteral("Device"), QStringLiteral("Programs"),
                       QStringLiteral("Ready")});
    stepper.set_current(1);
    REQUIRE_EQ(stepper.accessibleName(),
               QStringLiteral("Step 2 of 3, Programs"));
    REQUIRE(stepper.sizeHint().height() >= 24);
}

TEST_CASE("widgets: disclosure, form row and code block work")
{
    vss::gui::Disclosure disclosure(QStringLiteral("More options"));
    auto* content = new QWidget();
    auto* label = new QLabel(QStringLiteral("Signal wiring"));
    auto* content_layout = new QVBoxLayout(content);
    content_layout->addWidget(label);
    disclosure.set_content(content);
    REQUIRE(!disclosure.is_expanded());

    disclosure.set_expanded(true);
    QApplication::processEvents();
    REQUIRE(disclosure.is_expanded());
    REQUIRE(disclosure.accessibleName().isEmpty() == false ||
            disclosure.findChild<QToolButton*>() != nullptr);

    disclosure.set_expanded(false);
    QApplication::processEvents();
    REQUIRE(!disclosure.is_expanded());

    auto* field = new QLineEdit(QStringLiteral("COM3"));
    vss::gui::FormRow row(QStringLiteral("Port path"), field,
                          QStringLiteral("The path another program opens."));
    REQUIRE_EQ(field->accessibleName(), QStringLiteral("Port path"));
    row.set_error(QStringLiteral("COM3 is already used. Try COM9."));
    REQUIRE(field->property("invalid").toBool());
    REQUIRE_EQ(field->accessibleDescription(),
               QStringLiteral("COM3 is already used. Try COM9."));
    row.clear_error();
    REQUIRE(!field->property("invalid").toBool());

    vss::gui::CodeBlock code;
    code.set_text(QStringLiteral("vss_cli virtual --kind=auto"),
                  QStringLiteral("Command"));
    REQUIRE_EQ(code.text(), QStringLiteral("vss_cli virtual --kind=auto"));
    auto* copy = code.findChild<vss::gui::CopyButton*>();
    REQUIRE(copy != nullptr);
    copy->click();
    QApplication::processEvents();
    REQUIRE_EQ(QGuiApplication::clipboard()->text(),
               QStringLiteral("vss_cli virtual --kind=auto"));
}

TEST_CASE("widgets: notes, result sentences and dialogs")
{
    vss::gui::NoteBanner warn(vss::gui::NoteBanner::kind::warn,
                              QStringLiteral("Windows will ask for "
                                             "administrator permission."));
    REQUIRE(warn.accessibleName().startsWith(QStringLiteral("Warning: ")));
    REQUIRE_EQ(warn.text(), QStringLiteral("Windows will ask for "
                                           "administrator permission."));

    vss::gui::ResultSentence sentence;
    sentence.set_result_text(QStringLiteral("This creates port-1."));
    REQUIRE_EQ(sentence.text(), QStringLiteral("This creates port-1."));

    vss::gui::ConfirmDialog confirm(
        QStringLiteral("Remove terminal?"),
        QStringLiteral("Its link to device is removed too."),
        QStringLiteral("Remove"), QStringLiteral("Cancel"), true);
    REQUIRE_EQ(confirm.windowTitle(), QStringLiteral("Remove terminal?"));
    REQUIRE(confirm.findChildren<QPushButton*>().size() >= 2);

    vss::gui::ErrorDialog error(QStringLiteral("Could not open COM3"),
                                QStringLiteral("Another program is using it."),
                                QStringLiteral("asio error 5: access denied"));
    REQUIRE_EQ(error.windowTitle(), QStringLiteral("Could not open COM3"));
    auto* copy = error.findChild<QPushButton*>();
    REQUIRE(copy != nullptr);

    auto* anchor = new QWidget();
    anchor->resize(500, 300);
    anchor->show();
    vss::gui::ToastManager::instance().show(
        anchor, QStringLiteral("Removed terminal and its link."),
        QStringLiteral("Undo"), [] {});
    QApplication::processEvents();
    const auto toasts = anchor->findChildren<vss::gui::Toast*>();
    REQUIRE(!toasts.isEmpty());
    for (auto* toast : toasts)
        toast->dismiss();
    QApplication::processEvents();
    delete anchor;
}
