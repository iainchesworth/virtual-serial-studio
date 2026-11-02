//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_COMMON_HPP
#define VSS_GUI_DIALOGS_COMMON_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QString>
#include <QVector>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vss::gui
{

/// One parsed serial selection, kept as engine types so the URI the GUI
/// builds is exactly what the engine receives.
struct serial_choice
{
    vss::serial_settings settings;
    QString path;
};

/// The short form used beside a port name: "115200 8N1".
QString serial_short_label(const vss::serial_settings& settings);

/// The sentence used on the share wizard's Device settings card:
/// "115200 baud, 8 data bits, no parity, 1 stop bit, no flow control".
QString serial_settings_summary(const vss::serial_settings& settings);

/// `serial://COM3?baud=115200`, including only non-default framing.
QString serial_uri(const QString& path, const vss::serial_settings& settings);

/// "device (COM3)", or "logger (Studio only)" for an in-process port.
QString port_combo_label(const port_view& port);

/// True when the adapter reports Windows.  Falls back to the compile target
/// when a backend leaves `setup_facts::platform` empty.
bool windows_platform(const setup_facts& facts);

/// "A, B and C"; escaping is the caller's job.
QString join_dialog_names(const QStringList& names);

QString html_escape(const QString& text);
/// A bold port or program name for a result sentence.
QString bold_name(const QString& text);

QFont semibold(QFont font);

/// A dialog surface with the design's panel background and 20 px margins.
void prepare_dialog(QDialog* dialog, const QString& accessible_title, int width,
                    int height);

/// The sunken footer with a top border.  `layout` receives a right aligned
/// button row; add a stretch to push a quiet action to the left.
QFrame* make_dialog_footer(QWidget* parent, QHBoxLayout*& layout);

QPushButton* make_dialog_action_button(const QString& text, const char* role,
                                       QWidget* parent);

QLabel* make_dialog_title(const QString& text, QWidget* parent);
QLabel* make_dialog_subtitle(const QString& text, QWidget* parent);

/// The shared error pattern rendered inside a dialog: a danger note with a
/// title, one sentence of cause and "Copy details" holding the engine text.
/// Keeping it inline is what lets a wizard stay on its step after a failure.
class InlineError final : public QFrame
{
    Q_OBJECT

public:
    explicit InlineError(QWidget* parent = nullptr);

    void show_error(const QString& title, const QString& body,
                    const QString& details = {});
    void clear();
    bool is_showing() const;
    QString title() const;
    QString body() const;
    QString details() const;

private:
    void refresh_accessibility();
    void copy_details();

    QLabel* title_ = nullptr;
    QLabel* body_ = nullptr;
    QPushButton* copy_ = nullptr;
    QString details_;
};

/// The section label used above a control: caption weight 600 in ink2.
QLabel* make_field_label(const QString& text, QWidget* parent);

/// A QComboBox listing every port in `ports`, with the id as item data.
void fill_port_combo(QComboBox* combo, const std::vector<port_view>& ports);
vss::endpoint_id combo_port_id(const QComboBox* combo);
bool select_combo_port(QComboBox* combo, vss::endpoint_id id);

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_COMMON_HPP
