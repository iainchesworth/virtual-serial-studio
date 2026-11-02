//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "dialogs.hpp"

#include <vss/core/error.hpp>
#include <vss/ports/com0com.hpp>
#include <vss/ports/serial_enumeration.hpp>
#include <vss/studio/studio.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace vss::gui {
namespace {

const char* const note_style = "color: #6e7781;";
const char* const error_style = "color: #cf222e;";

void prepare(QDialog* dialog, const QString& title, int minimum_width)
{
    dialog->setWindowTitle(title);
    dialog->setModal(true);
    dialog->setMinimumWidth(minimum_width);
}

QLabel* make_error(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setStyleSheet(QString::fromLatin1(error_style));
    label->setMinimumHeight(18);
    return label;
}

QDialogButtonBox* make_buttons(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                     &QDialog::reject);
    return buttons;
}

void populate_baud(QComboBox* combo)
{
    combo->setEditable(true);
    combo->addItems({QStringLiteral("9600"), QStringLiteral("19200"),
                     QStringLiteral("38400"), QStringLiteral("57600"),
                     QStringLiteral("115200"), QStringLiteral("230400"),
                     QStringLiteral("460800"), QStringLiteral("921600")});
    combo->setCurrentText(QStringLiteral("115200"));
    combo->setValidator(new QIntValidator(50, 40000000, combo));
    combo->setToolTip(
        QObject::tr("Line rate in bits per second (50 to 40000000)."));
}

void populate_data_bits(QComboBox* combo)
{
    for (int bits = 5; bits <= 8; ++bits)
        combo->addItem(QString::number(bits), bits);
    combo->setCurrentIndex(combo->findData(8));
}

void populate_parity(QComboBox* combo)
{
    combo->addItem(QObject::tr("None"), static_cast<int>(vss::parity_mode::none));
    combo->addItem(QObject::tr("Odd"), static_cast<int>(vss::parity_mode::odd));
    combo->addItem(QObject::tr("Even"), static_cast<int>(vss::parity_mode::even));
    combo->addItem(QObject::tr("Mark"), static_cast<int>(vss::parity_mode::mark));
    combo->addItem(QObject::tr("Space"),
                   static_cast<int>(vss::parity_mode::space));
}

void populate_stop_bits(QComboBox* combo)
{
    combo->addItem(QStringLiteral("1"),
                   static_cast<int>(vss::stop_bits_mode::one));
    combo->addItem(QStringLiteral("1.5"),
                   static_cast<int>(vss::stop_bits_mode::one_point_five));
    combo->addItem(QStringLiteral("2"),
                   static_cast<int>(vss::stop_bits_mode::two));
}

void populate_flow(QComboBox* combo)
{
    combo->addItem(QObject::tr("None"),
                   static_cast<int>(vss::flow_control_mode::none));
    combo->addItem(QObject::tr("XON/XOFF"),
                   static_cast<int>(vss::flow_control_mode::software));
    combo->addItem(QObject::tr("RTS/CTS"),
                   static_cast<int>(vss::flow_control_mode::hardware));
}

unsigned current_baud(const QComboBox* combo)
{
    bool ok = false;
    const unsigned value = combo->currentText().trimmed().toUInt(&ok);
    return ok ? value : 115200u;
}

vss::serial_settings settings_from_widgets(const QComboBox* baud,
                                           const QComboBox* data_bits,
                                           const QComboBox* parity,
                                           const QComboBox* stop_bits,
                                           const QComboBox* flow)
{
    vss::serial_settings settings;
    settings.baud_rate = current_baud(baud);
    settings.data_bits =
        static_cast<unsigned>(data_bits->currentData().toInt());
    settings.parity =
        static_cast<vss::parity_mode>(parity->currentData().toInt());
    settings.stop_bits =
        static_cast<vss::stop_bits_mode>(stop_bits->currentData().toInt());
    settings.flow_control =
        static_cast<vss::flow_control_mode>(flow->currentData().toInt());
    return settings;
}

QString derive_name(const QString& uri)
{
    QString value = uri.trimmed();
    const int scheme = value.indexOf(QStringLiteral("://"));
    if (scheme >= 0)
        value = value.mid(scheme + 3);
    const int query = value.indexOf(QLatin1Char('?'));
    if (query >= 0)
        value = value.left(query);

    QString out;
    for (const QChar character : value)
    {
        if (character.isLetterOrNumber() || character == QLatin1Char('-')
            || character == QLatin1Char('_') || character == QLatin1Char('.'))
        {
            out += character;
        }
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
        {
            out += QLatin1Char('-');
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("port") : out;
}

} // namespace

AddVirtualPortDialog::AddVirtualPortDialog(QWidget* parent)
    : QDialog(parent)
{
    prepare(this, tr("Add virtual port"), 460);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    name_ = new QLineEdit(this);
    name_->setPlaceholderText(QStringLiteral("virtual-port"));
    name_->setToolTip(tr("A unique suffix is added automatically if the name "
                         "is already in use."));
    form->addRow(tr("Name"), name_);

    kind_ = new QComboBox(this);
    kind_->addItem(tr("Auto (best available)"), 0);
    kind_->addItem(vss::supports(vss::virtual_port_kind::pty)
                       ? tr("PTY (pseudo terminal)")
                       : tr("PTY (falls back to Auto here)"),
                   1);
    kind_->addItem(tr("Loopback (in-process)"), 2);
    kind_->setToolTip(tr("Auto uses a com0com pair on Windows when one is "
                         "available and an in-process loopback otherwise."));
    form->addRow(tr("Kind"), kind_);

    count_ = new QSpinBox(this);
    count_->setRange(1, 32);
    count_->setValue(1);
    count_->setToolTip(
        tr("Create several ports at once; when more than one is "
           "requested the names get a numeric suffix."));
    form->addRow(tr("How many"), count_);

    layout->addLayout(form);

    auto* note = new QLabel(
        tr("A loopback port echoes everything written to it. A PTY is a real "
           "operating-system terminal other applications can open."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QString::fromLatin1(note_style));
    layout->addWidget(note);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &AddVirtualPortDialog::accept);
    layout->addWidget(buttons);
}

QString AddVirtualPortDialog::port_name() const
{
    return name_->text().trimmed();
}

vss::virtual_port_kind AddVirtualPortDialog::kind() const
{
    return kind_->currentData().toInt() == 2
        ? vss::virtual_port_kind::loopback
        : vss::virtual_port_kind::pty;
}

int AddVirtualPortDialog::count() const
{
    return count_ != nullptr ? count_->value() : 1;
}

bool AddVirtualPortDialog::automatic() const
{
    return kind_->currentData().toInt() == 0;
}

void AddVirtualPortDialog::accept()
{
    if (name_->text().trimmed().isEmpty())
        name_->setText(QStringLiteral("virtual-port"));
    QDialog::accept();
}

AddMockDialog::AddMockDialog(int default_baud, QWidget* parent)
    : QDialog(parent)
{
    prepare(this, tr("Add mock device"), 440);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    name_ = new QLineEdit(this);
    name_->setPlaceholderText(QStringLiteral("mock-device"));
    form->addRow(tr("Name"), name_);

    baud_ = new QComboBox(this);
    populate_baud(baud_);
    if (baud_->findText(QString::number(default_baud)) < 0)
        baud_->addItem(QString::number(default_baud));
    baud_->setCurrentText(QString::number(default_baud));
    form->addRow(tr("Baud rate"), baud_);

    target_ = new QLineEdit(QStringLiteral("scripted"), this);
    target_->setToolTip(
        tr("Fixture name recorded by the serial transport; the in-process mock "
           "does not need a file."));
    form->addRow(tr("Fixture"), target_);

    layout->addLayout(form);

    auto* note = new QLabel(
        tr("Mock devices are deterministic in-memory ports. They accept writes "
           "without an operating-system COM port and do not echo."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QString::fromLatin1(note_style));
    layout->addWidget(note);

    error_ = make_error(this);
    layout->addWidget(error_);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &AddMockDialog::accept);
    layout->addWidget(buttons);
}

QString AddMockDialog::port_name() const
{
    return name_->text().trimmed();
}

QString AddMockDialog::target() const
{
    const QString value = target_->text().trimmed();
    return value.isEmpty() ? QStringLiteral("scripted") : value;
}

vss::serial_settings AddMockDialog::settings() const
{
    vss::serial_settings settings;
    settings.baud_rate = current_baud(baud_);
    return settings;
}

void AddMockDialog::accept()
{
    if (name_->text().trimmed().isEmpty())
        name_->setText(QStringLiteral("mock-device"));

    bool ok = false;
    const unsigned rate = baud_->currentText().trimmed().toUInt(&ok);
    if (!ok || rate < 50 || rate > 40000000)
    {
        error_->setText(tr("Baud rate must be between 50 and 40000000."));
        return;
    }
    QDialog::accept();
}

AttachSerialDialog::AttachSerialDialog(vss::studio& studio,
                                       int default_baud, QWidget* parent)
    : QDialog(parent)
    , studio_(studio)
{
    prepare(this, tr("Attach serial port"), 560);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(tr("Derived from the URI when empty"));
    form->addRow(tr("Name"), name_);

    device_ = new QComboBox(this);
    device_->addItem(tr("Custom URI / network endpoint"), QString());
    const auto devices = studio_.serial_devices();
    for (const auto& device : devices)
    {
        QString label = QString::fromStdString(device.path);
        if (!device.description.empty())
        {
            label += QStringLiteral("  —  ");
            label += QString::fromStdString(device.description);
        }
        if (device.com0com)
        {
            label += QStringLiteral("  [com0com %1]")
                         .arg(QString::fromStdString(device.com0com_side));
        }
        device_->addItem(label, QString::fromStdString(device.path));
    }
    device_->setToolTip(
        tr("Serial devices enumerated from the operating system."));
    form->addRow(tr("Device"), device_);

    uri_ = new QLineEdit(this);
    uri_->setPlaceholderText(
        QStringLiteral("serial://COM3?baud=115200, rfc2217://host:7001, "
                       "tcp://host:4001, udp://host:4002"));
    uri_->setToolTip(
        tr("Free-form URI. serial:// opens a local port, rfc2217:// speaks "
           "Telnet COM Port Control, tcp:// and udp:// bridge raw bytes."));
    form->addRow(tr("URI"), uri_);
    layout->addLayout(form);

    if (devices.empty())
    {
        auto* none = new QLabel(
            tr("No serial devices were detected. Enter a URI to attach a "
               "network endpoint or install a virtual COM port driver."),
            this);
        none->setWordWrap(true);
        none->setStyleSheet(QString::fromLatin1(note_style));
        layout->addWidget(none);
    }

    auto* settings_group = new QGroupBox(tr("Line settings"), this);
    auto* settings_form = new QFormLayout(settings_group);
    baud_ = new QComboBox(settings_group);
    populate_baud(baud_);
    if (baud_->findText(QString::number(default_baud)) < 0)
        baud_->addItem(QString::number(default_baud));
    baud_->setCurrentText(QString::number(default_baud));
    data_bits_ = new QComboBox(settings_group);
    populate_data_bits(data_bits_);
    parity_ = new QComboBox(settings_group);
    populate_parity(parity_);
    stop_bits_ = new QComboBox(settings_group);
    populate_stop_bits(stop_bits_);
    flow_ = new QComboBox(settings_group);
    populate_flow(flow_);
    settings_form->addRow(tr("Baud rate"), baud_);
    settings_form->addRow(tr("Data bits"), data_bits_);
    settings_form->addRow(tr("Parity"), parity_);
    settings_form->addRow(tr("Stop bits"), stop_bits_);
    settings_form->addRow(tr("Flow control"), flow_);
    layout->addWidget(settings_group);

    capture_ = new QCheckBox(tr("Capture traffic in the monitor"), this);
    capture_->setChecked(true);
    layout->addWidget(capture_);

    auto* com0com_group = new QGroupBox(tr("com0com"), this);
    auto* com0com_layout = new QVBoxLayout(com0com_group);
    auto* com0com_label = new QLabel(com0com_group);
    com0com_label->setWordWrap(true);
    const auto installation = studio_.detect_com0com();
    QStringList com0com_lines;
    if (installation.found)
    {
        com0com_lines << tr("Detected at %1").arg(
            QString::fromStdString(installation.install_dir));
        if (installation.pairs.empty())
        {
            com0com_lines << tr("No virtual COM pairs are configured yet.");
        }
        else
        {
            for (const auto& pair : installation.pairs)
            {
                com0com_lines << tr("Pair %1: %2 ↔ %3")
                                     .arg(pair.index)
                                     .arg(QString::fromStdString(pair.a),
                                          QString::fromStdString(pair.b));
            }
        }
    }
    else
    {
        com0com_lines
            << tr("com0com was not detected on this machine.")
            << tr("Install com0com to create real virtual COM port pairs, or "
                  "use an in-process loopback virtual port.");
    }
    com0com_label->setText(com0com_lines.join(QLatin1Char('\n')));
    com0com_layout->addWidget(com0com_label);
    if (!installation.setupc_path.empty())
    {
        com0com_group->setToolTip(QString::fromStdString(
            installation.setupc_path));
    }
    layout->addWidget(com0com_group);

    error_ = make_error(this);
    layout->addWidget(error_);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &AttachSerialDialog::accept);
    layout->addWidget(buttons);

    connect(device_, &QComboBox::currentIndexChanged, this,
            [this](int index) { on_device_changed(index); });
    connect(uri_, &QLineEdit::textEdited, this, [this](const QString&) {
        uri_generated_ = false;
        error_->clear();
    });
    connect(baud_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (uri_generated_ && !updating_uri_)
            rebuild_uri();
        error_->clear();
    });
    connect(parity_, &QComboBox::currentIndexChanged, this,
            [this](int) { error_->clear(); });
    connect(stop_bits_, &QComboBox::currentIndexChanged, this,
            [this](int) { error_->clear(); });
    connect(flow_, &QComboBox::currentIndexChanged, this,
            [this](int) { error_->clear(); });

    if (devices.empty())
        device_->setCurrentIndex(0);
    else
        device_->setCurrentIndex(1);
    on_device_changed(device_->currentIndex());
}

QString AttachSerialDialog::port_name() const
{
    return name_->text().trimmed();
}

QString AttachSerialDialog::uri() const
{
    return uri_->text().trimmed();
}

vss::serial_settings AttachSerialDialog::settings() const
{
    return settings_from_widgets(baud_, data_bits_, parity_, stop_bits_,
                                 flow_);
}

bool AttachSerialDialog::capture() const
{
    return capture_->isChecked();
}

void AttachSerialDialog::on_device_changed(int index)
{
    Q_UNUSED(index);
    if (updating_uri_)
        return;

    const QString path = device_->currentData().toString();
    if (path.isEmpty())
        return;

    const QString current_name = name_->text().trimmed();
    if (current_name.isEmpty() || current_name == auto_device_name_)
    {
        name_->setText(path);
        auto_device_name_ = path;
    }

    uri_generated_ = true;
    rebuild_uri();
}

void AttachSerialDialog::rebuild_uri()
{
    if (updating_uri_)
        return;

    const QString path = device_->currentData().toString();
    if (path.isEmpty())
        return;

    updating_uri_ = true;
    uri_->setText(QStringLiteral("serial://%1?baud=%2")
                      .arg(path, baud_->currentText().trimmed()));
    updating_uri_ = false;
    uri_generated_ = true;
}

void AttachSerialDialog::show_error(const QString& text)
{
    error_->setText(text);
}

QString AttachSerialDialog::derived_name() const
{
    return derive_name(uri());
}

void AttachSerialDialog::accept()
{
    if (uri().isEmpty())
    {
        show_error(tr("Choose a detected device or enter a URI such as "
                      "serial://COM3?baud=115200."));
        return;
    }

    bool ok = false;
    const unsigned rate = baud_->currentText().trimmed().toUInt(&ok);
    if (!ok || rate < 50 || rate > 40000000)
    {
        show_error(tr("Baud rate must be between 50 and 40000000."));
        return;
    }

    const vss::serial_settings value = settings();
    if (const vss::error_code failure = vss::validate(value))
    {
        show_error(tr("Invalid line settings: %1")
                       .arg(QString::fromStdString(failure.message())));
        return;
    }

    if (name_->text().trimmed().isEmpty())
        name_->setText(derived_name());

    QDialog::accept();
}

ShareSerialDialog::ShareSerialDialog(QWidget* parent)
    : QDialog(parent)
{
    prepare(this, tr("Share a serial port"), 500);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    uri_ = new QLineEdit(this);
    uri_->setPlaceholderText(
        QStringLiteral("serial://COM3?baud=115200, rfc2217://host:7001"));
    form->addRow(tr("Source URI"), uri_);

    listeners_ = new QSpinBox(this);
    listeners_->setRange(1, 64);
    listeners_->setValue(2);
    listeners_->setSuffix(tr(" listeners"));
    form->addRow(tr("Listeners"), listeners_);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("The source is opened once and every listener gets its own virtual "
           "port with a split link. Replies from all listeners are merged "
           "back to the source."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QString::fromLatin1(note_style));
    layout->addWidget(note);

    error_ = make_error(this);
    layout->addWidget(error_);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &ShareSerialDialog::accept);
    layout->addWidget(buttons);

    connect(uri_, &QLineEdit::textEdited, this,
            [this](const QString&) { error_->clear(); });
}

QString ShareSerialDialog::uri() const
{
    return uri_->text().trimmed();
}

int ShareSerialDialog::listeners() const
{
    return listeners_->value();
}

void ShareSerialDialog::accept()
{
    if (uri().isEmpty())
    {
        error_->setText(tr("Enter the URI of the serial port to share."));
        return;
    }
    QDialog::accept();
}

JoinPublishersDialog::JoinPublishersDialog(QWidget* parent)
    : QDialog(parent)
{
    prepare(this, tr("Join publishers"), 520);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    sources_ = new QPlainTextEdit(this);
    sources_->setPlaceholderText(
        QStringLiteral("serial://COM4?baud=115200\n"
                       "rfc2217://lab-host:7001"));
    sources_->setFixedHeight(110);
    form->addRow(tr("Publisher URIs"), sources_);

    target_ = new QLineEdit(this);
    target_->setPlaceholderText(QStringLiteral("serial://COM3?baud=115200"));
    form->addRow(tr("Target URI"), target_);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("Each line is opened as its own endpoint; their traffic is merged "
           "into the target with a one-way join link."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QString::fromLatin1(note_style));
    layout->addWidget(note);

    error_ = make_error(this);
    layout->addWidget(error_);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &JoinPublishersDialog::accept);
    layout->addWidget(buttons);

    connect(target_, &QLineEdit::textEdited, this,
            [this](const QString&) { error_->clear(); });
}

QStringList JoinPublishersDialog::source_uris() const
{
    QStringList uris;
    const QStringList lines = sources_->toPlainText().split(QLatin1Char('\n'));
    for (const QString& line : lines)
    {
        const QString value = line.trimmed();
        if (!value.isEmpty())
            uris << value;
    }
    return uris;
}

QString JoinPublishersDialog::target_uri() const
{
    return target_->text().trimmed();
}

void JoinPublishersDialog::accept()
{
    if (source_uris().isEmpty())
    {
        error_->setText(tr("Enter at least one publisher URI."));
        return;
    }
    if (target_uri().isEmpty())
    {
        error_->setText(tr("Enter the target URI."));
        return;
    }
    QDialog::accept();
}

BridgeDialog::BridgeDialog(QWidget* parent)
    : QDialog(parent)
{
    prepare(this, tr("Bridge to network"), 500);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    from_ = new QLineEdit(this);
    from_->setPlaceholderText(QStringLiteral("serial://COM3?baud=115200"));
    form->addRow(tr("From URI"), from_);

    to_ = new QLineEdit(this);
    to_->setPlaceholderText(QStringLiteral("tcp://192.168.1.50:4001"));
    form->addRow(tr("To URI"), to_);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("Opens both endpoints and connects them with a duplex link."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QString::fromLatin1(note_style));
    layout->addWidget(note);

    error_ = make_error(this);
    layout->addWidget(error_);

    auto* buttons = make_buttons(this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BridgeDialog::accept);
    layout->addWidget(buttons);

    connect(from_, &QLineEdit::textEdited, this,
            [this](const QString&) { error_->clear(); });
    connect(to_, &QLineEdit::textEdited, this,
            [this](const QString&) { error_->clear(); });
}

QString BridgeDialog::from_uri() const
{
    return from_->text().trimmed();
}

QString BridgeDialog::to_uri() const
{
    return to_->text().trimmed();
}

void BridgeDialog::accept()
{
    if (from_uri().isEmpty() || to_uri().isEmpty())
    {
        error_->setText(tr("Both URIs are required."));
        return;
    }
    QDialog::accept();
}

} // namespace vss::gui
