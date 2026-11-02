//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_NOTES_HPP
#define VSS_GUI_WIDGETS_NOTES_HPP

#include "widgets/common.hpp"

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QString>

#include <functional>

class QEnterEvent;
class QKeyEvent;
class QPushButton;

namespace vss::gui
{

/// A square icon button that copies text and announces "Copied".
///
/// Painted directly rather than styled as a QToolButton: the global toolbar
/// padding would otherwise clip the 16px glyph inside a 24px target.
class CopyButton final : public QAbstractButton
{
    Q_OBJECT

public:
    explicit CopyButton(QWidget* parent = nullptr);

    void set_copy_text(const QString& text, const QString& thing);
    QString copy_text() const { return text_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void copied();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void do_copy();

    QString text_;
    QString thing_;
};

/// Monospaced, selectable text in a sunken block with a copy button.
class CodeBlock final : public QFrame
{
    Q_OBJECT

public:
    explicit CodeBlock(QWidget* parent = nullptr);

    void set_text(const QString& text, const QString& access_name);
    void set_text(const QString& text);
    QString text() const;

Q_SIGNALS:
    void copied();

private:
    QLabel* label_ = nullptr;
    CopyButton* copy_ = nullptr;
    QString access_name_ = QStringLiteral("Address");
};

/// Info, ok, warn or danger note with an optional action.
class NoteBanner : public QFrame
{
    Q_OBJECT

public:
    enum class kind
    {
        info,
        ok,
        warn,
        danger
    };

    explicit NoteBanner(QWidget* parent = nullptr);
    NoteBanner(kind value, const QString& text, QWidget* parent = nullptr);

    void set_kind(kind value);
    kind get_kind() const noexcept { return kind_; }
    void set_text(const QString& text);
    /// Rich text for display, with a plain sentence for assistive technology.
    void set_text(const QString& text, const QString& accessible_text);
    void set_text_format(Qt::TextFormat format);
    QString text() const;

    void set_action(const QString& text, std::function<void()> callback = {});
    void clear_action();
    QPushButton* action_button() const noexcept { return action_; }

Q_SIGNALS:
    void action_triggered();

private:
    void refresh_accessible_name();
    void apply_kind();

    kind kind_ = kind::info;
    QLabel* icon_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* action_ = nullptr;
    QString accessible_text_;
};

/// A live result sentence; an info note whose text follows the form.
class ResultSentence final : public NoteBanner
{
public:
    explicit ResultSentence(QWidget* parent = nullptr);

    void set_result_text(const QString& text);
    /// Rich text for the visible sentence, with the plain sentence used for
    /// assistive technology.
    void set_result_text(const QString& text, const QString& accessible_text);
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_NOTES_HPP
