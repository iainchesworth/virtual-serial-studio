//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "inspector_panel.hpp"

#include "theme.hpp"

#include <vss/core/control_lines.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/studio/studio.hpp>

#include <QCheckBox>
#include <QColor>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui {
namespace {

QLabel* make_value(QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

void set_label_colour(QLabel* label, const QColor& colour)
{
    label->setStyleSheet(QStringLiteral("color: %1;")
                             .arg(colour.name(QColor::HexRgb)));
}

const char* line_name(vss::control_line line) noexcept
{
    switch (line)
    {
    case vss::control_line::dtr: return "DTR";
    case vss::control_line::rts: return "RTS";
    case vss::control_line::cts: return "CTS";
    case vss::control_line::dsr: return "DSR";
    case vss::control_line::dcd: return "DCD";
    case vss::control_line::ri: return "RI";
    case vss::control_line::break_signal: return "BREAK";
    case vss::control_line::count: break;
    }
    return "line";
}

} // namespace

InspectorPanel::InspectorPanel(vss::studio& studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    setObjectName(QStringLiteral("inspector_panel"));
    setMinimumWidth(300);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(10);

    title_ = new QLabel(tr("No port selected"), this);
    QFont title_font = title_->font();
    if (title_font.pointSizeF() > 0.0)
        title_font.setPointSizeF(title_font.pointSizeF() + 3.0);
    title_font.setBold(true);
    title_->setFont(title_font);
    title_->setWordWrap(true);
    outer->addWidget(title_);

    subtitle_ = new QLabel(tr("Select a port in the table or the topology."),
                           this);
    subtitle_->setWordWrap(true);
    subtitle_->setStyleSheet(QStringLiteral("color: #6e7781;"));
    outer->addWidget(subtitle_);

    auto* details_group = new QGroupBox(tr("Port"), this);
    auto* details = new QFormLayout(details_group);
    details->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    id_value_ = make_value(details_group);
    kind_value_ = make_value(details_group);
    path_value_ = make_value(details_group);
    description_value_ = make_value(details_group);
    serial_value_ = make_value(details_group);
    state_value_ = make_value(details_group);
    open_value_ = make_value(details_group);
    details->addRow(tr("Identifier"), id_value_);
    details->addRow(tr("Kind"), kind_value_);
    details->addRow(tr("Advertised path"), path_value_);
    details->addRow(tr("Description"), description_value_);
    details->addRow(tr("Line settings"), serial_value_);
    details->addRow(tr("State"), state_value_);
    details->addRow(tr("Open"), open_value_);
    outer->addWidget(details_group);

    auto* counters_group = new QGroupBox(tr("Traffic"), this);
    auto* counters = new QFormLayout(counters_group);
    counters->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    rx_total_ = make_value(counters_group);
    tx_total_ = make_value(counters_group);
    rx_rate_ = make_value(counters_group);
    tx_rate_ = make_value(counters_group);
    errors_value_ = make_value(counters_group);
    operations_value_ = make_value(counters_group);
    counters->addRow(tr("RX total"), rx_total_);
    counters->addRow(tr("TX total"), tx_total_);
    counters->addRow(tr("RX rate"), rx_rate_);
    counters->addRow(tr("TX rate"), tx_rate_);
    counters->addRow(tr("Errors"), errors_value_);
    counters->addRow(tr("Operations"), operations_value_);
    outer->addWidget(counters_group);

    controls_group_ = new QGroupBox(tr("Control lines"), this);
    auto* controls = new QVBoxLayout(controls_group_);
    controls->setSpacing(6);

    auto* output_row = new QHBoxLayout();
    output_row->setSpacing(12);
    dtr_ = new QCheckBox(tr("DTR"), controls_group_);
    rts_ = new QCheckBox(tr("RTS"), controls_group_);
    break_ = new QCheckBox(tr("BREAK"), controls_group_);
    dtr_->setToolTip(tr("Drive the DTR output line."));
    rts_->setToolTip(tr("Drive the RTS output line."));
    break_->setToolTip(tr("Assert the break condition."));
    output_row->addWidget(dtr_);
    output_row->addWidget(rts_);
    output_row->addWidget(break_);
    output_row->addStretch(1);
    controls->addLayout(output_row);

    controls_note_ = new QLabel(
        tr("This transport does not expose control lines."), controls_group_);
    controls_note_->setWordWrap(true);
    controls_note_->setStyleSheet(QStringLiteral("color: #8b949e;"));
    controls->addWidget(controls_note_);

    auto* input_title = new QLabel(tr("Peer inputs"), controls_group_);
    input_title->setStyleSheet(QStringLiteral("color: #6e7781;"));
    controls->addWidget(input_title);

    auto* input_row = new QGridLayout();
    input_row->setHorizontalSpacing(14);
    cts_ = new QLabel(tr("CTS: off"), controls_group_);
    dsr_ = new QLabel(tr("DSR: off"), controls_group_);
    dcd_ = new QLabel(tr("DCD: off"), controls_group_);
    ri_ = new QLabel(tr("RI: off"), controls_group_);
    input_row->addWidget(cts_, 0, 0);
    input_row->addWidget(dsr_, 0, 1);
    input_row->addWidget(dcd_, 1, 0);
    input_row->addWidget(ri_, 1, 1);
    controls->addLayout(input_row);
    outer->addWidget(controls_group_);

    outer->addStretch(1);

    connect(dtr_, &QCheckBox::toggled, this, [this](bool on) {
        apply_line(vss::control_line::dtr, on);
    });
    connect(rts_, &QCheckBox::toggled, this, [this](bool on) {
        apply_line(vss::control_line::rts, on);
    });
    connect(break_, &QCheckBox::toggled, this, [this](bool on) {
        apply_line(vss::control_line::break_signal, on);
    });

    clear();
}

void InspectorPanel::set_endpoint(vss::endpoint_id id)
{
    id_ = id;
    have_previous_ = false;
    previous_ = vss::endpoint_stats{};

    if (id == 0)
    {
        clear();
        return;
    }

    const auto endpoints = studio_.endpoint_infos();
    const auto it = std::find_if(endpoints.begin(), endpoints.end(),
                                 [id](const vss::endpoint_info& info) {
                                     return info.id == id;
                                 });
    if (it == endpoints.end())
    {
        id_ = 0;
        clear();
        return;
    }

    populate(*it);
    previous_ = it->stats;
    have_previous_ = true;
    set_rate_labels(0.0, 0.0);
}

void InspectorPanel::update_from(
    const std::vector<vss::endpoint_info>& endpoints, double elapsed_seconds)
{
    if (id_ == 0)
        return;

    const auto it = std::find_if(endpoints.begin(), endpoints.end(),
                                 [this](const vss::endpoint_info& info) {
                                     return info.id == id_;
                                 });
    if (it == endpoints.end())
    {
        id_ = 0;
        clear();
        return;
    }

    populate(*it);

    double rx_rate = 0.0;
    double tx_rate = 0.0;
    if (have_previous_ && elapsed_seconds > 0.0)
    {
        if (it->stats.bytes_rx >= previous_.bytes_rx)
        {
            rx_rate = static_cast<double>(it->stats.bytes_rx
                                          - previous_.bytes_rx)
                / elapsed_seconds;
        }
        if (it->stats.bytes_tx >= previous_.bytes_tx)
        {
            tx_rate = static_cast<double>(it->stats.bytes_tx
                                          - previous_.bytes_tx)
                / elapsed_seconds;
        }
    }
    set_rate_labels(rx_rate, tx_rate);
    previous_ = it->stats;
    have_previous_ = true;
}

void InspectorPanel::populate(const vss::endpoint_info& info)
{
    title_->setText(QString::fromStdString(info.name));
    subtitle_->setText(
        QStringLiteral("%1 · %2 · %3")
            .arg(kind_text(info.kind), state_text(info.state),
                 QString::fromStdString(vss::to_label(info.serial))));

    id_value_->setText(QStringLiteral("#%1").arg(
        static_cast<qulonglong>(info.id)));
    kind_value_->setText(kind_text(info.kind));
    path_value_->setText(info.path.empty() ? tr("—")
                                           : QString::fromStdString(info.path));
    description_value_->setText(
        info.description.empty() ? tr("—")
                                 : QString::fromStdString(info.description));
    serial_value_->setText(
        QString::fromStdString(vss::to_label(info.serial)));
    state_value_->setText(state_text(info.state));
    set_label_colour(state_value_, state_colour(info.state));
    open_value_->setText(info.open ? tr("yes") : tr("no"));
    set_label_colour(open_value_,
                     info.open ? QColor(0x1a, 0x7f, 0x37)
                               : QColor(0x8b, 0x94, 0x9e));

    rx_total_->setText(format_bytes(info.stats.bytes_rx));
    tx_total_->setText(format_bytes(info.stats.bytes_tx));
    errors_value_->setText(format_count(info.stats.errors));
    operations_value_->setText(QStringLiteral("%1 read · %2 write")
                                   .arg(format_count(info.stats.read_ops),
                                        format_count(info.stats.write_ops)));

    controls_group_->setEnabled(true);
    dtr_->setEnabled(info.supports_control_lines);
    rts_->setEnabled(info.supports_control_lines);
    break_->setEnabled(info.supports_control_lines);
    controls_note_->setVisible(!info.supports_control_lines);

    const auto set_output = [](QCheckBox* box, bool on) {
        const QSignalBlocker blocker(box);
        box->setChecked(on);
    };
    set_output(dtr_, info.lines.dtr());
    set_output(rts_, info.lines.rts());
    set_output(break_, info.lines.break_active());

    set_input_label(cts_, "CTS", info.lines.cts());
    set_input_label(dsr_, "DSR", info.lines.dsr());
    set_input_label(dcd_, "DCD", info.lines.dcd());
    set_input_label(ri_, "RI", info.lines.ri());
}

void InspectorPanel::set_input_label(QLabel* label, const char* name, bool on)
{
    label->setText(QStringLiteral("%1: %2")
                       .arg(QString::fromLatin1(name),
                            on ? tr("on") : tr("off")));
    label->setStyleSheet(
        on ? QStringLiteral("color: #1a7f37; font-weight: 600;")
           : QStringLiteral("color: #8b949e;"));
}

void InspectorPanel::set_rate_labels(double rx_rate, double tx_rate)
{
    rx_rate_->setText(format_rate(rx_rate));
    tx_rate_->setText(format_rate(tx_rate));
}

void InspectorPanel::apply_line(vss::control_line line, bool state)
{
    if (id_ == 0)
        return;

    const auto endpoint = studio_.engine().find_endpoint(id_);
    if (!endpoint)
    {
        Q_EMIT status_message(tr("The selected port no longer exists."), true);
        id_ = 0;
        clear();
        return;
    }

    if (const vss::error_code failure = endpoint->set_line(line, state))
    {
        Q_EMIT status_message(tr("Could not change %1: %2")
                                .arg(QString::fromLatin1(line_name(line)),
                                     QString::fromStdString(failure.message())),
                            true);
        set_endpoint(id_);
        return;
    }

    Q_EMIT status_message(tr("%1 set to %2")
                            .arg(QString::fromLatin1(line_name(line)),
                                 state ? tr("on") : tr("off")),
                        false);
}

void InspectorPanel::clear()
{
    title_->setText(tr("No port selected"));
    subtitle_->setText(tr("Select a port in the table or the topology."));
    for (QLabel* label : {id_value_, kind_value_, path_value_,
                          description_value_, serial_value_, state_value_,
                          open_value_, rx_total_, tx_total_, rx_rate_,
                          tx_rate_, errors_value_, operations_value_})
    {
        label->setText(QStringLiteral("—"));
        label->setStyleSheet(QString());
    }
    controls_group_->setEnabled(true);
    dtr_->setEnabled(false);
    rts_->setEnabled(false);
    break_->setEnabled(false);
    controls_note_->setVisible(true);
    {
        const QSignalBlocker dtr_blocker(dtr_);
        const QSignalBlocker rts_blocker(rts_);
        const QSignalBlocker break_blocker(break_);
        dtr_->setChecked(false);
        rts_->setChecked(false);
        break_->setChecked(false);
    }
    set_input_label(cts_, "CTS", false);
    set_input_label(dsr_, "DSR", false);
    set_input_label(dcd_, "DCD", false);
    set_input_label(ri_, "RI", false);
    have_previous_ = false;
}

} // namespace vss::gui
