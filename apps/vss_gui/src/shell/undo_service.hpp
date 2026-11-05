//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_UNDO_SERVICE_HPP
#define VSS_GUI_SHELL_UNDO_SERVICE_HPP

#include <QObject>
#include <QString>

#include <cstddef>
#include <deque>
#include <functional>

namespace vss::gui
{

/// One reversible change.  Both functions return false when the change can no
/// longer be applied, for example because a port it names has gone.
struct undo_step
{
    QString text;
    std::function<bool()> undo;
    std::function<bool()> redo;
};

/// The Undo and Redo history.
///
/// The engine cannot restore a whole snapshot exactly: a restored port gets a
/// new identity and, for a pseudo terminal, a new path.  So the history does
/// not store snapshots.  It stores changes that can be reversed by an exact
/// inverse operation (connect and disconnect a link, change an owner, rename
/// a port), and the shell clears it when the set of ports changes.  A step
/// that can no longer be applied clears the whole history instead of leaving
/// later steps that assumed it.
class UndoService final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t max_depth = 50;

    explicit UndoService(QObject* parent = nullptr);

    /// Records a change that has just been made.  Anything that could have
    /// been redone is dropped.
    void push(undo_step step);

    bool can_undo() const noexcept { return !undo_.empty(); }
    bool can_redo() const noexcept { return !redo_.empty(); }
    /// The text of the change Undo would reverse, or empty.
    QString undo_text() const;
    /// The text of the change Redo would make again, or empty.
    QString redo_text() const;

    /// Reverses the latest change.  False when there is nothing to undo or the
    /// change could not be applied, in which case the history is cleared.
    bool undo();
    bool redo();

    void clear();

Q_SIGNALS:
    void changed();

private:
    std::deque<undo_step> undo_;
    std::deque<undo_step> redo_;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_UNDO_SERVICE_HPP
