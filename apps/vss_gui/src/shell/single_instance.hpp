//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace vss::gui
{

/// One Studio per user.
///
/// The first Studio listens on a local socket (a named pipe on Windows, a
/// socket file elsewhere) that only this user can open; it never touches the
/// network.  A second Studio finds it, asks it to show its window and exits,
/// so opening Studio from the Start menu while it runs in the notification
/// area brings back the one that holds the ports.
class SingleInstance final : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString& name, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// The per-user name the real Studio uses.
    static QString default_name();

    /// Asks a running Studio to show itself.  True when one answered.
    bool activate_running() const;

    /// Starts answering.  `wait_ms` waits that long for a Studio that is
    /// quitting to let go first (a restart as administrator).  False when
    /// another Studio keeps the name; this one then runs without the lock.
    bool listen(int wait_ms = 0);

    void close();

Q_SIGNALS:
    void activation_requested();

private:
    void on_connection();
    /// Something listens on the name.  Sends nothing.
    bool answering() const;

    QString name_;
    QLocalServer* server_ = nullptr;
};

} // namespace vss::gui
