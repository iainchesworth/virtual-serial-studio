//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_HPP
#define VSS_GUI_DIALOGS_HPP

#include <vss/core/serial_settings.hpp>
#include <vss/ports/virtual_port.hpp>

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace vss {
class studio;
}

namespace vss::gui {

/// Name and OS virtual port kind for `studio::create_virtual_port`.
class AddVirtualPortDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddVirtualPortDialog(QWidget* parent = nullptr);

    QString port_name() const;
    vss::virtual_port_kind kind() const;
    /// True when the "Auto" entry is selected.
    bool automatic() const;
    /// How many ports to create in one step.
    int count() const;

public Q_SLOTS:
    void accept() override;

private:
    QLineEdit* name_ = nullptr;
    QComboBox* kind_ = nullptr;
    QSpinBox* count_ = nullptr;
};

/// Name, baud and fixture target for `studio::create_mock_port`.
class AddMockDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddMockDialog(int default_baud = 115200,
                           QWidget* parent = nullptr);

    QString port_name() const;
    QString target() const;
    vss::serial_settings settings() const;

public Q_SLOTS:
    void accept() override;

private:
    QLineEdit* name_ = nullptr;
    QComboBox* baud_ = nullptr;
    QLineEdit* target_ = nullptr;
    QLabel* error_ = nullptr;
};

/// Detected serial devices, framing settings and a free-form URI field.
class AttachSerialDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AttachSerialDialog(vss::studio& studio,
                                int default_baud = 115200,
                                QWidget* parent = nullptr);

    QString port_name() const;
    QString uri() const;
    vss::serial_settings settings() const;
    bool capture() const;

public Q_SLOTS:
    void accept() override;

private:
    void on_device_changed(int index);
    void rebuild_uri();
    void show_error(const QString& text);
    QString derived_name() const;

    vss::studio& studio_;
    QLineEdit* name_ = nullptr;
    QComboBox* device_ = nullptr;
    QLineEdit* uri_ = nullptr;
    QComboBox* baud_ = nullptr;
    QComboBox* data_bits_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* stop_bits_ = nullptr;
    QComboBox* flow_ = nullptr;
    QCheckBox* capture_ = nullptr;
    QLabel* error_ = nullptr;
    bool uri_generated_ = false;
    bool updating_uri_ = false;
    QString auto_device_name_;
};

/// URI and listener count for `studio::share_serial`.
class ShareSerialDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ShareSerialDialog(QWidget* parent = nullptr);

    QString uri() const;
    int listeners() const;

public Q_SLOTS:
    void accept() override;

private:
    QLineEdit* uri_ = nullptr;
    QSpinBox* listeners_ = nullptr;
    QLabel* error_ = nullptr;
};

/// Publisher URIs and one target URI for `studio::join_ports`.
class JoinPublishersDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit JoinPublishersDialog(QWidget* parent = nullptr);

    QStringList source_uris() const;
    QString target_uri() const;

public Q_SLOTS:
    void accept() override;

private:
    QPlainTextEdit* sources_ = nullptr;
    QLineEdit* target_ = nullptr;
    QLabel* error_ = nullptr;
};

/// Two URIs for `studio::bridge`.
class BridgeDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit BridgeDialog(QWidget* parent = nullptr);

    QString from_uri() const;
    QString to_uri() const;

public Q_SLOTS:
    void accept() override;

private:
    QLineEdit* from_ = nullptr;
    QLineEdit* to_ = nullptr;
    QLabel* error_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_HPP
