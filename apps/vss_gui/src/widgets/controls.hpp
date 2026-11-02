//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_CONTROLS_HPP
#define VSS_GUI_WIDGETS_CONTROLS_HPP

#include "widgets/common.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QEnterEvent;
class QHBoxLayout;
class QKeyEvent;
class QMouseEvent;
class QPropertyAnimation;
class QPushButton;
class QRadioButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace vss::gui
{

/// A row of joined buttons with radio-group semantics.
class SegmentedControl final : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentedControl(QWidget* parent = nullptr);

    int add_item(const QString& text, int id = -1);
    void set_current(int id);
    int current() const;
    int count() const;
    /// 24 px variant for the status bar; the default is 32 px.
    void set_compact(bool compact);
    bool is_compact() const noexcept { return compact_; }

Q_SIGNALS:
    void current_changed(int id);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void apply_style();

    QHBoxLayout* layout_ = nullptr;
    QButtonGroup* group_ = nullptr;
    int next_id_ = 0;
    int current_id_ = -1;
    bool compact_ = false;
};

/// A card with a radio button, title, description and optional meta line.
class PickCard final : public QFrame
{
    Q_OBJECT

public:
    explicit PickCard(QWidget* parent = nullptr);
    PickCard(const QString& title, const QString& description,
             QWidget* parent = nullptr);

    void set_title(const QString& title);
    void set_description(const QString& description);
    void set_meta(const QString& meta);
    void set_selected(bool selected);
    bool is_selected() const;
    QRadioButton* radio() const noexcept { return radio_; }

Q_SIGNALS:
    void selected();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void refresh();

    QRadioButton* radio_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* meta_ = nullptr;
};

/// A 34 by 18 switch with a label that states its state.
class Switch final : public QCheckBox
{
public:
    explicit Switch(const QString& label = {}, QWidget* parent = nullptr);

    void set_label(const QString& label);
    QString label() const { return label_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool hitButton(const QPoint& position) const override;

private:
    void refresh_text();

    QString label_;
};

/// Numbered step circles joined by lines.
class Stepper final : public QWidget
{
public:
    explicit Stepper(QWidget* parent = nullptr);

    void set_steps(const QStringList& steps);
    void set_current(int index);
    int current() const noexcept { return current_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void update_accessible_name();

    QStringList steps_;
    int current_ = 0;
};

/// A chevron header that reveals a content widget over the motion interval.
class Disclosure final : public QWidget
{
    Q_OBJECT

public:
    explicit Disclosure(const QString& title = {}, QWidget* parent = nullptr);

    void set_title(const QString& title);
    void set_content(QWidget* content);
    QWidget* content() const noexcept { return content_; }
    void set_expanded(bool expanded);
    bool is_expanded() const;

Q_SIGNALS:
    void expanded_changed(bool expanded);

private:
    void toggle();
    void apply_state(bool expanded, bool animate);

    QToolButton* header_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QPropertyAnimation* animation_ = nullptr;
};

/// Label, field, hint and inline error in one block.
class FormRow final : public QWidget
{
public:
    FormRow(const QString& label, QWidget* field, const QString& hint = {},
            QWidget* parent = nullptr);

    void set_label(const QString& label);
    void set_hint(const QString& hint);
    void set_error(const QString& error);
    void clear_error();
    QWidget* field() const noexcept { return field_; }

private:
    QLabel* label_ = nullptr;
    QWidget* field_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* error_ = nullptr;
};

/// One transient message at the bottom of a window.
class Toast final : public QFrame
{
    Q_OBJECT

public:
    explicit Toast(QWidget* parent = nullptr);

    void set_message(const QString& text, const QString& action_text = {},
                     std::function<void()> action = {});
    void dismiss();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void restart_timer();

    QLabel* label_ = nullptr;
    QPushButton* action_ = nullptr;
    QTimer* timer_ = nullptr;
    std::function<void()> callback_;
};

/// Stacks toasts bottom-centre and owns their lifetime.
class ToastManager final : public QObject
{
    Q_OBJECT

public:
    static ToastManager& instance();

    void show(QWidget* anchor, const QString& text,
              const QString& action_text = {},
              std::function<void()> action = {});

private:
    explicit ToastManager(QObject* parent = nullptr);
    void position_toasts(QWidget* window);

    QList<QPointer<Toast>> toasts_;
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_CONTROLS_HPP
