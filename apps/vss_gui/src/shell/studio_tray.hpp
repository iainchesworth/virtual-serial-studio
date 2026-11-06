//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace vss::gui
{

/// Studio's icon in the notification area (the menu bar on macOS).
///
/// It is how a Studio whose window is closed is found again, and the place
/// to stop the ports.  Shown only while background running is on and the
/// platform has a notification area.
class StudioTray final : public QObject
{
    Q_OBJECT

public:
    explicit StudioTray(QObject* parent = nullptr);
    ~StudioTray() override;

    /// True when the desktop has a notification area to put the icon in.
    static bool available();

    void show();
    void hide();
    bool visible() const;

    /// "3 ports, 2 links running" in the tooltip and the menu, and the warn
    /// icon while a port is lost or failed.
    void set_state(int ports, int links, bool warn);
    QString status_text() const;
    bool warn() const noexcept { return warn_; }

    /// The sign-in item: shown checked or not, or disabled with "Not
    /// available in this build".
    void set_sign_in(bool available, bool enabled);

    /// One notification from the icon.  Ignored where the platform has none.
    void notify(const QString& title, const QString& text, bool warn = false);

    QMenu* menu() const noexcept { return menu_; }

Q_SIGNALS:
    void open_requested();
    void quit_requested();
    void sign_in_toggled(bool enabled);

private:
    void apply_icon();

    QSystemTrayIcon* icon_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* status_ = nullptr;
    QAction* sign_in_ = nullptr;
    QString text_;
    bool warn_ = false;
};

} // namespace vss::gui
