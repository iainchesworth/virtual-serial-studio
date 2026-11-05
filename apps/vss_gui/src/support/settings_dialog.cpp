//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/settings_dialog.hpp"

#include "support/setup_facts.hpp"
#include "theme/theme.hpp"
#include "widgets/common.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"
#include "widgets/notes.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui::support
{
namespace
{

QLabel* section_title(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = Theme::instance().font(text_style::subtitle);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QLabel* hint_label(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(Theme::instance().font(text_style::caption));
    label->setProperty("muted", true);
    label->setWordWrap(true);
    polish(label);
    return label;
}

QWidget* scroll_page(QWidget* content, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

SettingsDialog::SettingsDialog(StudioBackend& backend,
                               const ui_settings& current, QWidget* parent)
    : QDialog(parent), backend_(backend), settings_(current)
{
    build();
}

void SettingsDialog::build()
{
    setWindowTitle(QCoreApplication::translate("Settings", "Settings"));
    setAccessibleName(QCoreApplication::translate("Settings", "Settings"));
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(Theme::instance().metric(QStringLiteral("layout.dialogWidths.wide")),
           760);
    setMinimumWidth(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.wide")));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(24, 20, 24, 14);
    auto* title =
        new QLabel(QCoreApplication::translate("Settings", "Settings"), header);
    title->setFont(Theme::instance().font(text_style::title));
    header_layout->addWidget(title, 1);
    auto* shortcut = new QLabel(QStringLiteral("Ctrl+,"), header);
    shortcut->setFont(Theme::instance().font(text_style::caption));
    shortcut->setProperty("muted", true);
    polish(shortcut);
    header_layout->addWidget(shortcut, 0, Qt::AlignVCenter);
    outer->addWidget(header);

    auto* content = new QWidget(this);
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);

    auto* nav = new QFrame(content);
    nav->setObjectName(QStringLiteral("settingsNav"));
    nav->setFixedWidth(200);
    nav->setAttribute(Qt::WA_StyledBackground, true);
    const auto apply_nav_style = [nav]
    {
        nav->setStyleSheet(
            QStringLiteral(
                "QFrame#settingsNav { background: %1; border-right: 1px solid "
                "%2; }"
                "QFrame#settingsNav QPushButton { min-height: 34px; border: 0; "
                "border-radius: 4px; padding: 0 12px; text-align: left; "
                "background: transparent; color: %3; }"
                "QFrame#settingsNav QPushButton:hover:!checked { background: "
                "%4; }"
                "QFrame#settingsNav QPushButton:checked { background: %5; "
                "color: %6; font-weight: 600; }")
                .arg(Theme::instance()
                         .colour(colour_role::sunken)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::line)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::ink2)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::panel)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::accentSoft)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::accentStrong)
                         .name(QColor::HexRgb)));
    };
    apply_nav_style();
    connect(&Theme::instance(), &Theme::theme_changed, nav,
            [apply_nav_style] { apply_nav_style(); });
    auto* nav_layout = new QVBoxLayout(nav);
    nav_layout->setContentsMargins(10, 12, 10, 12);
    nav_layout->setSpacing(2);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList sections = {
        QCoreApplication::translate("Settings", "General"),
        QCoreApplication::translate("Settings", "Appearance"),
        QCoreApplication::translate("Settings", "Performance")};
    for (int index = 0; index < sections.size(); ++index)
    {
        auto* button = new QPushButton(sections.at(index), nav);
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(sections.at(index));
        group->addButton(button, index);
        nav_layout->addWidget(button);
    }
    nav_layout->addStretch(1);
    content_layout->addWidget(nav);

    pages_ = new QStackedWidget(content);
    content_layout->addWidget(pages_, 1);

    // -- General -----------------------------------------------------------
    auto* general = new QWidget(pages_);
    auto* general_layout = new QVBoxLayout(general);
    general_layout->setContentsMargins(22, 22, 28, 22);
    general_layout->setSpacing(14);
    general_layout->addWidget(section_title(
        QCoreApplication::translate("Settings", "General"), general));

    advanced_ = new Switch(
        QCoreApplication::translate("Settings", "Advanced mode"), general);
    advanced_->setObjectName(QStringLiteral("advancedSwitch"));
    advanced_->setChecked(settings_.advanced);
    general_layout->addWidget(advanced_);
    auto* advanced_hint = hint_label(
        QCoreApplication::translate(
            "Settings",
            "Adds panels, options and tools to the same window. Nothing you "
            "have built changes."),
        general);
    general_layout->addWidget(advanced_hint);

    auto* baud_holder = new QWidget(general);
    baud_holder->setMaximumWidth(320);
    auto* baud_layout = new QVBoxLayout(baud_holder);
    baud_layout->setContentsMargins(0, 0, 0, 0);
    baud_layout->setSpacing(4);
    baud_ = new QComboBox(baud_holder);
    baud_->setObjectName(QStringLiteral("baudCombo"));
    baud_->addItems(baud_rate_choices());
    const int baud_index =
        baud_->findText(QString::number(settings_.default_baud));
    if (baud_index >= 0)
        baud_->setCurrentIndex(baud_index);
    auto* baud_row = new FormRow(
        QCoreApplication::translate("Settings", "Default baud rate"), baud_,
        QCoreApplication::translate("Settings",
                                    "Offered first when you attach a device."),
        baud_holder);
    baud_layout->addWidget(baud_row);
    general_layout->addWidget(baud_holder);

    ask_on_discard_ =
        new QCheckBox(QCoreApplication::translate(
                          "Settings", "Ask before discarding an unsaved setup"),
                      general);
    ask_on_discard_->setObjectName(QStringLiteral("askOnDiscard"));
    ask_on_discard_->setChecked(settings_.ask_on_discard);
    general_layout->addWidget(ask_on_discard_);
    general_layout->addStretch(1);
    pages_->addWidget(scroll_page(general, pages_));

    // -- Appearance --------------------------------------------------------
    auto* appearance = new QWidget(pages_);
    auto* appearance_layout = new QVBoxLayout(appearance);
    appearance_layout->setContentsMargins(22, 22, 28, 22);
    appearance_layout->setSpacing(14);
    appearance_layout->addWidget(section_title(
        QCoreApplication::translate("Settings", "Appearance"), appearance));

    auto* theme_label = new QLabel(
        QCoreApplication::translate("Settings", "Theme"), appearance);
    theme_label->setFont(Theme::instance().font(text_style::caption));
    QFont theme_bold = theme_label->font();
    theme_bold.setWeight(QFont::DemiBold);
    theme_label->setFont(theme_bold);
    appearance_layout->addWidget(theme_label);

    auto* theme_group = new QButtonGroup(this);
    theme_group->setExclusive(true);
    const auto add_theme = [this, appearance, theme_group](const QString& text,
                                                           theme_mode mode,
                                                           bool checked)
    {
        auto* radio = new QRadioButton(text, appearance);
        radio->setChecked(checked);
        theme_group->addButton(radio);
        connect(radio, &QRadioButton::toggled, this,
                [this, mode](bool on)
                {
                    if (!on)
                        return;
                    settings_.theme = mode;
                    apply_theme();
                });
        return radio;
    };
    theme_system_ =
        add_theme(QCoreApplication::translate("Settings", "Follow the system"),
                  theme_mode::system, settings_.theme == theme_mode::system);
    theme_light_ =
        add_theme(QCoreApplication::translate("Settings", "Light"),
                  theme_mode::light, settings_.theme == theme_mode::light);
    theme_dark_ =
        add_theme(QCoreApplication::translate("Settings", "Dark"),
                  theme_mode::dark, settings_.theme == theme_mode::dark);
    for (QRadioButton* radio : {theme_system_, theme_light_, theme_dark_})
        appearance_layout->addWidget(radio);

    auto* density_label = new QLabel(
        QCoreApplication::translate("Settings", "Density"), appearance);
    density_label->setFont(theme_bold);
    appearance_layout->addWidget(density_label);
    density_ = new SegmentedControl(appearance);
    density_->setObjectName(QStringLiteral("densityControl"));
    density_->add_item(QCoreApplication::translate("Settings", "Comfortable"),
                       0);
    density_->add_item(QCoreApplication::translate("Settings", "Compact"), 1);
    density_->set_current(settings_.density == density_mode::compact ? 1 : 0);
    connect(density_, &SegmentedControl::current_changed, this,
            [this](int id)
            {
                settings_.density =
                    id == 1 ? density_mode::compact : density_mode::comfortable;
                apply_density();
            });
    auto* density_holder = new QWidget(appearance);
    density_holder->setMaximumWidth(320);
    auto* density_layout = new QVBoxLayout(density_holder);
    density_layout->setContentsMargins(0, 0, 0, 0);
    density_layout->addWidget(density_);
    appearance_layout->addWidget(density_holder);

    auto* display_note = new NoteBanner(
        NoteBanner::kind::info,
        QCoreApplication::translate(
            "Settings",
            "Text size, display scale, high contrast and reduced motion follow "
            "your operating system."),
        appearance);
    display_note->set_action(
        QCoreApplication::translate("Settings", "Open system settings"),
        [this]
        {
            if (!open_system_accessibility_settings())
            {
                ErrorDialog dialog(
                    QCoreApplication::translate(
                        "Settings", "Could not open system settings"),
                    QCoreApplication::translate(
                        "Settings", "Open your operating system's "
                                    "display or accessibility settings "
                                    "yourself."),
                    {}, this);
                dialog.exec();
            }
        });
    appearance_layout->addWidget(display_note);
    appearance_layout->addStretch(1);
    pages_->addWidget(scroll_page(appearance, pages_));

    // -- Performance -------------------------------------------------------
    auto* performance = new QWidget(pages_);
    auto* performance_layout = new QVBoxLayout(performance);
    performance_layout->setContentsMargins(22, 22, 28, 22);
    performance_layout->setSpacing(14);
    performance_layout->addWidget(section_title(
        QCoreApplication::translate("Settings", "Performance"), performance));

    auto* refresh_holder = new QWidget(performance);
    refresh_holder->setMaximumWidth(460);
    auto* refresh_layout = new QVBoxLayout(refresh_holder);
    refresh_layout->setContentsMargins(0, 0, 0, 0);
    refresh_layout->setSpacing(4);
    auto* refresh_heading = new QHBoxLayout();
    refresh_heading->setContentsMargins(0, 0, 0, 0);
    auto* refresh_label =
        new QLabel(QCoreApplication::translate("Settings", "Refresh interval"),
                   refresh_holder);
    refresh_label->setFont(theme_bold);
    refresh_heading->addWidget(refresh_label);
    refresh_heading->addStretch(1);
    refresh_value_ = new QLabel(refresh_holder);
    refresh_value_->setFont(Theme::instance().font(text_style::data));
    refresh_value_->setObjectName(QStringLiteral("refreshValue"));
    refresh_heading->addWidget(refresh_value_);
    refresh_layout->addLayout(refresh_heading);

    refresh_ = new QSlider(Qt::Horizontal, refresh_holder);
    refresh_->setObjectName(QStringLiteral("refreshSlider"));
    refresh_->setAccessibleName(
        QCoreApplication::translate("Settings", "Refresh interval"));
    refresh_->setRange(kRefreshMinMs, kRefreshMaxMs);
    refresh_->setSingleStep(10);
    refresh_->setPageStep(250);
    refresh_->setValue(settings_.refresh_ms);
    refresh_layout->addWidget(refresh_);
    refresh_value_->setText(QCoreApplication::translate("Settings", "%1 ms")
                                .arg(settings_.refresh_ms));

    auto* ends = new QWidget(refresh_holder);
    auto* ends_layout = new QHBoxLayout(ends);
    ends_layout->setContentsMargins(0, 0, 0, 0);
    ends_layout->addWidget(hint_label(
        QCoreApplication::translate("Settings", "50 ms, live"), ends));
    ends_layout->addStretch(1);
    ends_layout->addWidget(hint_label(
        QCoreApplication::translate("Settings", "5000 ms, light"), ends));
    refresh_layout->addWidget(ends);
    refresh_layout->addWidget(hint_label(
        QCoreApplication::translate("Settings",
                                    "How often counters and states are read. "
                                    "Shorter feels live. Longer "
                                    "is lighter on a busy machine."),
        refresh_holder));
    performance_layout->addWidget(refresh_holder);
    performance_layout->addStretch(1);
    pages_->addWidget(scroll_page(performance, pages_));

    connect(group, &QButtonGroup::idClicked, this,
            [this](int id) { select_page(id); });
    connect(advanced_, &Switch::toggled, this,
            [this](bool on)
            {
                settings_.advanced = on;
                persist();
                Q_EMIT advanced_changed(on);
            });
    connect(baud_, &QComboBox::currentTextChanged, this,
            [this](const QString& text)
            {
                settings_.default_baud = text.toInt();
                persist();
            });
    connect(ask_on_discard_, &QCheckBox::toggled, this,
            [this](bool on)
            {
                settings_.ask_on_discard = on;
                persist();
            });
    connect(refresh_, &QSlider::valueChanged, this,
            [this](int value)
            {
                settings_.refresh_ms = value;
                apply_refresh();
            });
    apply_refresh();
    outer->addWidget(content, 1);

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_dialog_footer(this, footer_layout);
    auto* immediate = new QLabel(
        QCoreApplication::translate("Settings", "Changes apply immediately."),
        footer);
    immediate->setFont(Theme::instance().font(text_style::caption));
    immediate->setProperty("muted", true);
    polish(immediate);
    const int stretch_index = 0;
    footer_layout->insertWidget(stretch_index, immediate);
    auto* reset = make_dialog_button(
        QCoreApplication::translate("Settings", "Reset to defaults"), "quiet",
        footer);
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this,
            &SettingsDialog::reset_to_defaults);
    footer_layout->addWidget(reset);
    auto* close = make_dialog_button(
        QCoreApplication::translate("Settings", "Close"), "primary", footer);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(close);
    outer->addWidget(footer);

    close->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, close,
                       [close] { close->setFocus(Qt::OtherFocusReason); });
}

void SettingsDialog::select_page(int index)
{
    if (pages_ != nullptr)
        pages_->setCurrentIndex(index);
}

void SettingsDialog::apply_theme()
{
    Theme::instance().set_mode(settings_.theme);
    persist();
}

void SettingsDialog::apply_density()
{
    Theme::instance().set_density(settings_.density);
    persist();
}

void SettingsDialog::apply_refresh()
{
    settings_.refresh_ms =
        std::clamp(settings_.refresh_ms, kRefreshMinMs, kRefreshMaxMs);
    backend_.set_refresh_interval(settings_.refresh_ms);
    if (refresh_value_ != nullptr)
        refresh_value_->setText(QCoreApplication::translate("Settings", "%1 ms")
                                    .arg(settings_.refresh_ms));
    persist();
}

void SettingsDialog::persist()
{
    save_ui_settings(settings_);
    Q_EMIT settings_changed(settings_);
}

void SettingsDialog::reset_to_defaults()
{
    settings_ = default_ui_settings();
    {
        const QSignalBlocker block_advanced(advanced_);
        const QSignalBlocker block_baud(baud_);
        const QSignalBlocker block_ask(ask_on_discard_);
        const QSignalBlocker block_refresh(refresh_);
        advanced_->setChecked(settings_.advanced);
        baud_->setCurrentText(QString::number(settings_.default_baud));
        ask_on_discard_->setChecked(settings_.ask_on_discard);
        refresh_->setValue(settings_.refresh_ms);
        const QSignalBlocker block_density(density_);
        density_->set_current(0);
    }
    {
        const QSignalBlocker block_system(theme_system_);
        const QSignalBlocker block_light(theme_light_);
        const QSignalBlocker block_dark(theme_dark_);
        theme_system_->setChecked(true);
        theme_light_->setChecked(false);
        theme_dark_->setChecked(false);
    }
    Theme::instance().set_mode(settings_.theme);
    Theme::instance().set_density(settings_.density);
    backend_.set_refresh_interval(settings_.refresh_ms);
    if (refresh_value_ != nullptr)
        refresh_value_->setText(QCoreApplication::translate("Settings", "%1 ms")
                                    .arg(settings_.refresh_ms));
    persist();
    Q_EMIT advanced_changed(false);
}

} // namespace vss::gui::support
