//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_COMMAND_PALETTE_HPP
#define VSS_GUI_SHELL_COMMAND_PALETTE_HPP

#include "shell/action_registry.hpp"
#include "shell/level_service.hpp"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace vss::gui
{

/// Ctrl+K search over every registered action, grouped by level.
class CommandPalette final : public QDialog
{
    Q_OBJECT

public:
    CommandPalette(ActionRegistry& registry, LevelService& level,
                   QWidget* parent = nullptr);

    void set_query(const QString& query);
    void activate_current();

    int available_count() const;
    int hidden_count() const;
    QAction* selected_action() const;

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild();
    void update_footer();
    void add_group(const QString& title, const QVector<QAction*>& actions);
    QListWidgetItem* add_action(QAction* action);

    ActionRegistry& registry_;
    LevelService& level_;
    QLineEdit* search_ = nullptr;
    QListWidget* results_ = nullptr;
    QLabel* footer_ = nullptr;
    QPushButton* turn_on_ = nullptr;
    int available_ = 0;
    int hidden_ = 0;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_COMMAND_PALETTE_HPP
