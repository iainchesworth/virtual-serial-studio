//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/inspector_panel.hpp"

#include "panes/format.hpp"
#include "panes/port_model.hpp"
#include "theme/theme.hpp"
#include "widgets/chips.hpp"
#include "widgets/controls.hpp"
#include "widgets/notes.hpp"

#include <vss/core/serial_settings.hpp>

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

namespace vss::gui
{
namespace
{

QFont semibold(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(Theme::instance().font(text_style::caption));
    label->setProperty("muted", true);
    polish(label);
    return label;
}

QString line_name(vss::control_line line)
{
    switch (line)
    {
    case vss::control_line::dtr:
        return QStringLiteral("DTR");
    case vss::control_line::rts:
        return QStringLiteral("RTS");
    case vss::control_line::cts:
        return QStringLiteral("CTS");
    case vss::control_line::dsr:
        return QStringLiteral("DSR");
    case vss::control_line::dcd:
        return QStringLiteral("DCD");
    case vss::control_line::ri:
        return QStringLiteral("RI");
    case vss::control_line::break_signal:
        return QStringLiteral("Break");
    case vss::control_line::count:
        break;
    }
    return {};
}

StatusChip::kind chip_kind(port_state_kind kind)
{
    switch (kind)
    {
    case port_state_kind::open:
    case port_state_kind::connected:
        return StatusChip::kind::healthy;
    case port_state_kind::waiting:
        return StatusChip::kind::waiting;
    case port_state_kind::studio_only:
        return StatusChip::kind::studio_only;
    case port_state_kind::closed:
        return StatusChip::kind::closed;
    case port_state_kind::failed:
        return StatusChip::kind::failed;
    case port_state_kind::fixture:
        return StatusChip::kind::fixture;
    }
    return StatusChip::kind::closed;
}

QString flow_text(vss::flow_control_mode mode)
{
    switch (mode)
    {
    case vss::flow_control_mode::none:
        return QStringLiteral("no flow control");
    case vss::flow_control_mode::software:
        return QStringLiteral("software flow control");
    case vss::flow_control_mode::hardware:
        return QStringLiteral("hardware flow control");
    }
    return {};
}

} // namespace

InspectorPanel::InspectorPanel(StudioBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend)
{
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Inspector"));
    build();
    connect(&backend_, &StudioBackend::ports_changed, this,
            [this] { refresh(); });
    connect(&backend_, &StudioBackend::lines_changed, this,
            [this](vss::endpoint_id id)
            {
                if (id != selected_)
                    return;
                // The adapter rebuilds its cache after the batch signal.
                QTimer::singleShot(0, this, [this] { refresh(); });
            });
    update_content();
}

void InspectorPanel::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    empty_ = new QLabel(QStringLiteral("Select a port to inspect it."), this);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setFont(Theme::instance().font(text_style::body));
    empty_->setProperty("muted", true);
    empty_->setContentsMargins(16, 16, 16, 16);
    root->addWidget(empty_, 1);

    scroll_ = new QScrollArea(this);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_ = new QWidget(scroll_);
    scroll_->setWidget(content_);
    root->addWidget(scroll_, 1);

    auto* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    name_ = new QLabel(content_);
    name_->setFont(semibold(Theme::instance().font(text_style::bodyLarge)));
    name_->setWordWrap(true);
    layout->addWidget(name_);

    auto* form = new QGridLayout();
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(4);
    form->setColumnStretch(1, 1);

    int row = 0;
    form->addWidget(caption(QStringLiteral("State"), content_), row, 0,
                    Qt::AlignTop);
    state_ = new StatusChip(content_);
    form->addWidget(state_, row++, 1, Qt::AlignLeft);

    form->addWidget(caption(QStringLiteral("Path"), content_), row, 0,
                    Qt::AlignTop);
    auto* path_row = new QWidget(content_);
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(6);
    path_ = new QLabel(path_row);
    path_->setFont(Theme::instance().font(text_style::data));
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_->setWordWrap(true);
    copy_ = new CopyButton(path_row);
    copy_->set_copy_text({}, QStringLiteral("path"));
    path_layout->addWidget(path_, 1);
    path_layout->addWidget(copy_, 0, Qt::AlignTop);
    form->addWidget(path_row, row++, 1);

    form->addWidget(caption(QStringLiteral("Traffic"), content_), row, 0);
    traffic_ = new QLabel(content_);
    traffic_->setFont(Theme::instance().font(text_style::data));
    form->addWidget(traffic_, row++, 1);

    serial_row_ = new QWidget(content_);
    auto* serial_layout = new QHBoxLayout(serial_row_);
    serial_layout->setContentsMargins(0, 0, 0, 0);
    serial_layout->setSpacing(8);
    serial_layout->addWidget(caption(QStringLiteral("Serial"), content_));
    serial_label_ = new QLabel(content_);
    serial_label_->setFont(Theme::instance().font(text_style::data));
    serial_label_->setWordWrap(true);
    serial_layout->addWidget(serial_label_, 1);
    form->addWidget(serial_row_, row++, 0, 1, 2);

    layout->addLayout(form);

    auto* help = new QWidget(content_);
    auto* help_layout = new QVBoxLayout(help);
    help_layout->setContentsMargins(0, 0, 0, 0);
    help_layout->setSpacing(2);
    help_text_ = new QLabel(help);
    help_text_->setFont(Theme::instance().font(text_style::body));
    help_text_->setWordWrap(true);
    help_link_ = new QLabel(
        QStringLiteral("<a href=\"troubleshoot\">Why is nothing arriving?</a>"),
        help);
    help_link_->setTextFormat(Qt::RichText);
    help_link_->setFont(Theme::instance().font(text_style::body));
    connect(help_link_, &QLabel::linkActivated, this,
            [this](const QString&) { Q_EMIT troubleshoot_requested(); });
    help_layout->addWidget(help_text_);
    help_layout->addWidget(help_link_);
    layout->addWidget(help);

    auto* controls_title = caption(QStringLiteral("Control lines"), content_);
    layout->addWidget(controls_title);

    simple_lines_ = new QWidget(content_);
    auto* simple_grid = new QGridLayout(simple_lines_);
    simple_grid->setContentsMargins(0, 0, 0, 0);
    simple_grid->setSpacing(6);
    for (int index = 0; index < static_cast<int>(line_chips_.size()); ++index)
    {
        auto* chip = new StatusChip(simple_lines_);
        chip->set_small(true);
        line_chips_[static_cast<std::size_t>(index)] = chip;
        simple_grid->addWidget(chip, index / 3, index % 3);
    }
    layout->addWidget(simple_lines_);

    advanced_lines_ = new QWidget(content_);
    auto* advanced_layout = new QVBoxLayout(advanced_lines_);
    advanced_layout->setContentsMargins(0, 0, 0, 0);
    advanced_layout->setSpacing(6);
    auto* switch_row = new QHBoxLayout();
    switch_row->setSpacing(12);
    dtr_ = new Switch(QStringLiteral("DTR"), advanced_lines_);
    rts_ = new Switch(QStringLiteral("RTS"), advanced_lines_);
    break_ = new QPushButton(QStringLiteral("Break"), advanced_lines_);
    break_->setProperty("size", QStringLiteral("small"));
    polish(break_);
    switch_row->addWidget(dtr_);
    switch_row->addWidget(rts_);
    switch_row->addStretch(1);
    switch_row->addWidget(break_);
    advanced_layout->addLayout(switch_row);

    auto* input_grid = new QGridLayout();
    input_grid->setContentsMargins(0, 0, 0, 0);
    input_grid->setSpacing(6);
    for (int index = 0; index < static_cast<int>(input_chips_.size()); ++index)
    {
        auto* chip = new StatusChip(advanced_lines_);
        chip->set_small(true);
        input_chips_[static_cast<std::size_t>(index)] = chip;
        input_grid->addWidget(chip, 0, index);
    }
    advanced_layout->addLayout(input_grid);
    layout->addWidget(advanced_lines_);

    advanced_link_ = new QLabel(
        QStringLiteral("<a href=\"faults\">Open line matrix and faults</a>"),
        content_);
    advanced_link_->setTextFormat(Qt::RichText);
    advanced_link_->setFont(Theme::instance().font(text_style::body));
    connect(advanced_link_, &QLabel::linkActivated, this,
            [this](const QString&) { Q_EMIT advanced_tools_requested(); });
    layout->addWidget(advanced_link_);
    layout->addStretch(1);

    const auto connect_switch = [this](Switch* toggle, vss::control_line line)
    {
        connect(toggle, &Switch::toggled, this,
                [this, toggle, line](bool on)
                {
                    if (refreshing_)
                        return;
                    const auto result = backend_.set_line(selected_, line, on);
                    if (!result)
                    {
                        const QSignalBlocker blocker(toggle);
                        toggle->setChecked(!on);
                        Q_EMIT error_message(error_text(result.error()));
                    }
                });
    };
    connect_switch(dtr_, vss::control_line::dtr);
    connect_switch(rts_, vss::control_line::rts);

    connect(break_, &QPushButton::pressed, this,
            [this] { set_line(vss::control_line::break_signal, true); });
    connect(break_, &QPushButton::released, this,
            [this] { set_line(vss::control_line::break_signal, false); });
}

void InspectorPanel::set_advanced(bool advanced)
{
    if (advanced_ == advanced)
        return;
    advanced_ = advanced;
    simple_lines_->setVisible(!advanced);
    advanced_lines_->setVisible(advanced);
    serial_row_->setVisible(advanced && port_ &&
                            port_->kind == vss::endpoint_kind::physical);
    advanced_link_->setVisible(advanced);
    update_content();
}

void InspectorPanel::set_selected_port(vss::endpoint_id id)
{
    selected_ = id;
    refresh();
}

void InspectorPanel::refresh()
{
    if (selected_ == 0)
    {
        port_.reset();
        update_content();
        return;
    }
    port_ = backend_.port(selected_);
    update_content();
}

void InspectorPanel::update_content()
{
    const bool has_port = port_.has_value();
    empty_->setVisible(!has_port);
    scroll_->setVisible(has_port);
    if (!has_port)
        return;

    refreshing_ = true;
    const port_view& port = *port_;
    name_->setText(port.name);
    state_->set_kind(chip_kind(PortModel::state_kind(port)));
    state_->set_text(PortModel::state_text(port));
    state_->setToolTip(port.state == vss::endpoint_state::failed
                           ? QStringLiteral("The engine reported a failure.")
                           : QString());
    path_->setText(port.path);
    copy_->set_copy_text(port.path, QStringLiteral("path"));
    traffic_->setText(QStringLiteral("RX %1, TX %2")
                          .arg(format_bytes(port.stats.bytes_rx),
                               format_bytes(port.stats.bytes_tx)));
    serial_row_->setVisible(advanced_ &&
                            port.kind == vss::endpoint_kind::physical);
    serial_label_->setText(serial_description());

    const bool waiting = port.open && port.peer == peer_state::not_open;
    help_text_->setVisible(waiting);
    help_text_->setText(
        waiting ? QStringLiteral("No program has opened %1 yet.").arg(port.path)
                : QString());
    help_link_->setText(QStringLiteral(
        "<a href=\"troubleshoot\">Why is nothing arriving?</a>"));

    simple_lines_->setVisible(!advanced_);
    advanced_lines_->setVisible(advanced_);
    advanced_link_->setVisible(advanced_);

    update_lines();
    const bool can_drive = port.supports_control_lines;
    dtr_->setEnabled(can_drive);
    rts_->setEnabled(can_drive);
    break_->setEnabled(can_drive);
    const QString tooltip =
        can_drive ? QString()
                  : QStringLiteral("This port does not support control lines.");
    dtr_->setToolTip(tooltip);
    rts_->setToolTip(tooltip);
    break_->setToolTip(tooltip);

    if (advanced_)
    {
        const QSignalBlocker dtr_blocker(dtr_);
        const QSignalBlocker rts_blocker(rts_);
        dtr_->setChecked(port.lines.dtr());
        rts_->setChecked(port.lines.rts());
    }
    refreshing_ = false;
}

void InspectorPanel::update_lines()
{
    if (!port_)
        return;
    const port_view& port = *port_;
    const std::array<vss::control_line, 6> lines = {
        vss::control_line::dtr, vss::control_line::rts, vss::control_line::cts,
        vss::control_line::dsr, vss::control_line::dcd, vss::control_line::ri};
    for (int index = 0; index < static_cast<int>(line_chips_.size()); ++index)
    {
        const auto line = lines[static_cast<std::size_t>(index)];
        const bool on = port.lines.test(line);
        StatusChip* chip = line_chips_[static_cast<std::size_t>(index)];
        chip->set_text(QStringLiteral("%1 %2").arg(line_name(line),
                                                   on ? QStringLiteral("on")
                                                      : QStringLiteral("off")));
        chip->set_kind(on ? StatusChip::kind::healthy
                          : StatusChip::kind::closed);
    }

    const std::array<vss::control_line, 4> inputs = {
        vss::control_line::cts, vss::control_line::dsr, vss::control_line::dcd,
        vss::control_line::ri};
    for (int index = 0; index < static_cast<int>(input_chips_.size()); ++index)
    {
        const auto line = inputs[static_cast<std::size_t>(index)];
        const bool on = port.lines.test(line);
        StatusChip* chip = input_chips_[static_cast<std::size_t>(index)];
        chip->set_text(QStringLiteral("%1 %2").arg(line_name(line),
                                                   on ? QStringLiteral("on")
                                                      : QStringLiteral("off")));
        chip->set_kind(on ? StatusChip::kind::healthy
                          : StatusChip::kind::closed);
    }
}

void InspectorPanel::set_line(vss::control_line line, bool state)
{
    if (selected_ == 0)
        return;
    const auto result = backend_.set_line(selected_, line, state);
    if (!result)
        Q_EMIT error_message(error_text(result.error()));
}

QString InspectorPanel::serial_description() const
{
    if (!port_ || port_->kind != vss::endpoint_kind::physical)
        return {};
    return QStringLiteral("%1, %2").arg(
        QString::fromStdString(vss::to_label(port_->serial)),
        flow_text(port_->serial.flow_control));
}

} // namespace vss::gui
