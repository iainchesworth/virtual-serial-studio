//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_TEMPLATES_DIALOG_HPP
#define VSS_GUI_DIALOGS_TEMPLATES_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "dialogs/templates.hpp"

#include <vss/config/document.hpp>

#include <QDialog>
#include <QString>
#include <QVector>

#include <optional>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QVBoxLayout;

namespace vss::gui
{

class InlineError;
class NoteBanner;
class PickCard;

/// File, Setups and templates.  Bundled resources plus the Recent list from
/// QSettings; both open through `vss_config`, never a new JSON shape.
class TemplatesDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit TemplatesDialog(StudioBackend& backend, QWidget* parent = nullptr);

    void set_search(const QString& text);
    QString search() const;

    int template_count() const;
    void select_template(int index);
    int current_template() const;
    QString template_title(int index) const;
    QString template_meta(int index) const;
    bool requires_device_selected() const;

    void set_tab(int index);
    int tab() const;
    int recent_count() const;
    QString recent_name(int index) const;

    /// Loads the selected template and accepts.  False when the template
    /// needs a real device (the Attach flow) or cannot be loaded.
    bool use_current();
    bool open_file(const QString& path);
    bool save_current_template(const QString& path);

    std::optional<vss::config::document> result_document() const
    {
        return result_document_;
    }
    QString result_title() const { return result_title_; }
    QString result_path() const { return result_path_; }
    bool result_is_template() const { return result_is_template_; }

    QString error_title() const;
    QString error_body() const;
    bool error_visible() const;
    QString status_text() const;
    bool status_visible() const;

    QPushButton* primary_button() const { return primary_; }

private:
    void build();
    QWidget* build_templates_page();
    QWidget* build_recent_page();
    void build_footer(QVBoxLayout* outer);
    void select_first_template();
    void update_selection();
    void update_primary();
    void apply_filter();
    bool card_visible(int index) const;
    void refresh_recent();
    void show_error(const QString& title, const QString& body,
                    const QString& details = {});
    void clear_error();
    QString meta_for(const bundled_template& entry) const;

    StudioBackend& backend_;
    QVector<bundled_template> templates_;
    QVector<PickCard*> cards_;
    QButtonGroup* group_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* recent_table_ = nullptr;
    QLabel* no_recent_ = nullptr;
    NoteBanner* device_note_ = nullptr;
    InlineError* error_ = nullptr;
    NoteBanner* status_ = nullptr;
    QPushButton* primary_ = nullptr;

    std::optional<vss::config::document> result_document_;
    QString result_title_;
    QString result_path_;
    bool result_is_template_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_TEMPLATES_DIALOG_HPP
