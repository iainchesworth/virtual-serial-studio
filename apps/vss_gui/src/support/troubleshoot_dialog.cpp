//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/troubleshoot_dialog.hpp"

#include "support/check_row.hpp"
#include "support/troubleshoot.hpp"
#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui::support
{
namespace
{

row_tone tone_for(check_state state)
{
    switch (state)
    {
    case check_state::passed:
        return row_tone::ok;
    case check_state::attention:
        return row_tone::warn;
    case check_state::not_checked:
        break;
    }
    return row_tone::neutral;
}

QVector<port_view> to_ports(const std::vector<port_view>& ports)
{
    QVector<port_view> out;
    out.reserve(static_cast<qsizetype>(ports.size()));
    for (const port_view& port : ports)
        out.push_back(port);
    return out;
}

QVector<link_view> to_links(const std::vector<link_view>& links)
{
    QVector<link_view> out;
    out.reserve(static_cast<qsizetype>(links.size()));
    for (const link_view& link : links)
        out.push_back(link);
    return out;
}

QString escape(const QString& text)
{
    return text.toHtmlEscaped();
}

QWidget* separator(QWidget* parent)
{
    auto* line = new QWidget(parent);
    line->setFixedHeight(1);
    const auto apply_style = [line]
    {
        line->setStyleSheet(QStringLiteral("background: %1")
                                .arg(Theme::instance()
                                         .colour(colour_role::line)
                                         .name(QColor::HexRgb)));
    };
    apply_style();
    QObject::connect(&Theme::instance(), &Theme::theme_changed, line,
                     [apply_style] { apply_style(); });
    return line;
}

} // namespace

TroubleshootDialog::TroubleshootDialog(StudioBackend& backend,
                                       vss::endpoint_id port, QWidget* parent)
    : QDialog(parent), backend_(backend), port_id_(port)
{
    build();
    rebuild();
}

void TroubleshootDialog::build()
{
    setWindowTitle(QCoreApplication::translate("Troubleshoot",
                                               "Why is nothing arriving?"));
    setAccessibleName(QCoreApplication::translate("Troubleshoot",
                                                  "Why is nothing arriving?"));
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.small")),
        760);
    setMinimumWidth(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.small")));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QWidget(this);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(24, 20, 24, 14);
    header_layout->setSpacing(4);
    title_ = new QLabel(header);
    title_->setFont(Theme::instance().font(text_style::title));
    title_->setWordWrap(true);
    header_layout->addWidget(title_);
    sub_ = new QLabel(
        QCoreApplication::translate(
            "Troubleshoot",
            "Studio checked each step between the device and your program."),
        header);
    sub_->setFont(Theme::instance().font(text_style::body));
    sub_->setProperty("muted", true);
    sub_->setWordWrap(true);
    polish(sub_);
    header_layout->addWidget(sub_);
    outer->addWidget(header);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 4, 24, 16);
    body_layout->setSpacing(14);

    auto* checks = new QFrame(body);
    checks->setProperty("card", true);
    polish(checks);
    auto* checks_layout = new QVBoxLayout(checks);
    checks_layout->setContentsMargins(0, 0, 0, 0);
    checks_layout->setSpacing(0);
    checks_host_ = checks;
    checks_layout_ = checks_layout;
    body_layout->addWidget(checks);

    auto* try_card = new QFrame(body);
    try_card->setProperty("card", true);
    polish(try_card);
    try_card_ = try_card;
    auto* try_layout = new QVBoxLayout(try_card);
    try_layout->setContentsMargins(16, 14, 16, 14);
    try_layout->setSpacing(8);
    auto* eyebrow = new QLabel(
        QCoreApplication::translate("Troubleshoot", "Try this"), try_card);
    eyebrow->setFont(Theme::instance().font(text_style::caption));
    QFont bold = eyebrow->font();
    bold.setWeight(QFont::DemiBold);
    eyebrow->setFont(bold);
    try_layout->addWidget(eyebrow);

    steps_ = new QLabel(try_card);
    steps_->setTextFormat(Qt::RichText);
    steps_->setWordWrap(true);
    steps_->setFont(Theme::instance().font(text_style::body));
    steps_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    try_layout->addWidget(steps_);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    copy_ = make_dialog_button(
        QCoreApplication::translate("Troubleshoot", "Copy port path"), nullptr,
        try_card);
    copy_->setProperty("size", QStringLiteral("small"));
    copy_->setIcon(IconProvider::instance().icon(QStringLiteral("copy"),
                                                 colour_role::ink, 16));
    polish(copy_);
    copy_->setAutoDefault(false);
    connect(copy_, &QPushButton::clicked, this, &TroubleshootDialog::copy_path);
    buttons->addWidget(copy_);
    auto* again = make_dialog_button(
        QCoreApplication::translate("Troubleshoot", "Check again"), nullptr,
        try_card);
    again->setProperty("size", QStringLiteral("small"));
    again->setIcon(IconProvider::instance().icon(QStringLiteral("refresh"),
                                                 colour_role::ink, 16));
    polish(again);
    again->setAutoDefault(false);
    connect(again, &QPushButton::clicked, this,
            &TroubleshootDialog::refresh_checks);
    buttons->addWidget(again);
    buttons->addStretch(1);
    try_layout->addLayout(buttons);
    body_layout->addWidget(try_card);

    auto* muted = new QLabel(
        QCoreApplication::translate(
            "Troubleshoot",
            "Still stuck? A diagnostic bundle gathers the setup, the log and "
            "the environment for whoever helps you. Traffic contents are left "
            "out unless you choose to include them."),
        body);
    muted->setFont(Theme::instance().font(text_style::caption));
    muted->setProperty("muted", true);
    muted->setWordWrap(true);
    polish(muted);
    body_layout->addWidget(muted);
    body_layout->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_dialog_footer(this, footer_layout);
    auto* bundle = make_dialog_button(
        QCoreApplication::translate("Troubleshoot", "Create diagnostic bundle"),
        "quiet", footer);
    bundle->setAutoDefault(false);
    connect(bundle, &QPushButton::clicked, this,
            &TroubleshootDialog::bundle_requested);
    footer_layout->addWidget(bundle);
    auto* close =
        make_dialog_button(QCoreApplication::translate("Troubleshoot", "Close"),
                           "primary", footer);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(close);
    outer->addWidget(footer);

    close->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, close,
                       [close] { close->setFocus(Qt::OtherFocusReason); });
}

void TroubleshootDialog::rebuild()
{
    const QVector<port_view> ports = to_ports(backend_.ports());
    if (port_id_ == 0 && !ports.isEmpty())
        port_id_ = ports.first().id;

    const port_view* selected = nullptr;
    for (const port_view& port : ports)
    {
        if (port.id == port_id_)
        {
            selected = &port;
            break;
        }
    }
    path_ = selected != nullptr ? port_display_path(*selected) : QString();

    const QString title =
        path_.isEmpty()
            ? QCoreApplication::translate("Troubleshoot",
                                          "Why is nothing arriving?")
            : QCoreApplication::translate("Troubleshoot",
                                          "Why is nothing arriving on %1?")
                  .arg(path_);
    title_->setText(title);
    setWindowTitle(title);
    setAccessibleName(title);
    if (copy_ != nullptr)
    {
        copy_->setText(
            path_.isEmpty()
                ? QCoreApplication::translate("Troubleshoot", "Copy port path")
                : QCoreApplication::translate("Troubleshoot", "Copy %1")
                      .arg(path_));
        copy_->setAccessibleName(copy_->text());
        copy_->setToolTip(copy_->text());
        copy_->setVisible(!path_.isEmpty());
    }
    if (try_card_ != nullptr)
        try_card_->setVisible(!path_.isEmpty());
    if (steps_ != nullptr && !path_.isEmpty())
    {
        const QString shown = escape(path_);
        steps_->setText(
            QCoreApplication::translate(
                "Troubleshoot",
                "1. In your program, choose <b>%1</b> as the serial port.<br>"
                "2. If the program shows an error, another program may be "
                "using "
                "%1. Close it, then try again.<br>"
                "3. Some programs only list ports when they start. Restart the "
                "program after creating the port.")
                .arg(shown));
        steps_->setAccessibleName(
            QCoreApplication::translate(
                "Troubleshoot",
                "1. In your program, choose %1 as the serial port. 2. If the "
                "program shows an error, another program may be using %1. "
                "Close "
                "it, then try again. 3. Some programs only list ports when "
                "they "
                "start. Restart the program after creating the port.")
                .arg(path_));
    }

    troubleshoot_facts facts;
    facts.port = port_id_;
    facts.ports = ports;
    facts.links = to_links(backend_.links());
    facts.peer_open_known = backend_.caps().peer_open;
    facts.now = vss::clock_type::now();
    const troubleshoot_report report = build_troubleshoot_report(facts);

    while (QLayoutItem* item = checks_layout_->takeAt(0))
    {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    rows_.clear();

    bool first = true;
    for (const check_row& row : report.rows)
    {
        if (!first)
            checks_layout_->addWidget(separator(checks_host_));
        first = false;
        CheckRowWidget::spec spec;
        spec.tone = tone_for(row.state);
        spec.icon = row.icon;
        spec.title = row.title;
        spec.chip = row.chip;
        spec.sentence = row.sentence;
        spec.highlight = row.highlight;
        spec.framed = false;
        auto* widget = new CheckRowWidget(spec, checks_host_);
        widget->setAccessibleName(tr("%1, %2").arg(row.title, row.chip));
        checks_layout_->addWidget(widget);
        rows_.push_back(widget);
    }
}

QString TroubleshootDialog::title_text() const
{
    return title_ != nullptr ? title_->text() : QString();
}

CheckRowWidget* TroubleshootDialog::check_widget(int index) const
{
    if (index < 0 || index >= rows_.size())
        return nullptr;
    return rows_.at(index);
}

void TroubleshootDialog::refresh_checks()
{
    rebuild();
}

void TroubleshootDialog::copy_path()
{
    if (path_.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(path_);
    ToastManager::instance().show(
        this,
        QCoreApplication::translate("Troubleshoot", "%1 copied.").arg(path_));
}

} // namespace vss::gui::support
