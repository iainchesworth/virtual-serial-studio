//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_ACTION_REGISTRY_HPP
#define VSS_GUI_SHELL_ACTION_REGISTRY_HPP

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace vss::gui
{

enum class action_level
{
    simple,
    advanced
};

struct action_spec
{
    QString id;
    QString text;
    /// Space-separated terms the command palette searches besides the text.
    QString keywords;
    /// Icon name from `IconProvider`, or empty.
    QString icon;
    QKeySequence shortcut;
    action_level level = action_level::simple;
    bool checkable = false;
    std::function<void()> handler;
    /// Optional enabled predicate, evaluated by refresh().
    std::function<bool()> enabled;
    /// False for a placement-only action (for example a toolbar duplicate of
    /// a menu command) that must not appear twice in the command palette.
    bool palette = true;
};

/// One registry for every menu, toolbar and command-palette action.
class ActionRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit ActionRegistry(QObject* parent = nullptr);

    QAction* add(action_spec spec);
    QAction* action(const QString& id) const;
    QVector<QAction*> actions() const { return order_; }
    QVector<QAction*> visible(action_level level) const;
    QVector<QAction*> search(const QString& query) const;

    void set_level(action_level level);
    action_level level() const noexcept { return level_; }

    /// Re-evaluates every enabled predicate.
    void refresh();
    /// True when no two non-empty shortcuts are equal.
    bool shortcuts_unique() const;
    QStringList duplicate_shortcuts() const;

Q_SIGNALS:
    void changed();

private:
    QHash<QString, QAction*> by_id_;
    QHash<QAction*, action_spec> specs_;
    QVector<QAction*> order_;
    action_level level_ = action_level::simple;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_ACTION_REGISTRY_HPP
