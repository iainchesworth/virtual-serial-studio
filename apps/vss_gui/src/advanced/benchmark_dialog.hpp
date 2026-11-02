//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_BENCHMARK_DIALOG_HPP
#define VSS_GUI_ADVANCED_BENCHMARK_DIALOG_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace vss::gui
{

class CodeBlock;
class NoteBanner;
class StatusChip;

/// "Benchmark a connection".
///
/// The benchmark core lives in `vss_cli` and extracting it needs maintainer
/// approval, so this build ships the dialog with `Capabilities::benchmark`
/// false: the Run action is disabled with the degraded tooltip and no result
/// is ever invented.  The linked-port refusal is real and is shown even when
/// the capability is absent, so the rule is testable now.
class BenchmarkDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit BenchmarkDialog(StudioBackend& backend, QWidget* parent = nullptr);

    /// Why Run cannot start, or empty when it could.  The capability reason is
    /// the last resort, so an actionable link problem is named first.
    QString block_reason() const;
    /// True when the selected ports are already joined by a link.
    bool ports_linked() const;
    bool can_run() const;
    void refresh();

    QComboBox* from_combo() const noexcept { return from_; }
    QComboBox* to_combo() const noexcept { return to_; }
    QComboBox* amount_combo() const noexcept { return amount_; }
    QComboBox* chunk_combo() const noexcept { return chunk_; }
    QPushButton* run_button() const noexcept { return run_; }
    QPushButton* copy_result_button() const noexcept { return copy_result_; }
    QProgressBar* progress_bar() const noexcept { return progress_; }
    StatusChip* result_chip() const noexcept { return result_; }
    QString result_summary() const;
    QString command() const;
    QString block_note_text() const;

Q_SIGNALS:
    void message(const QString& text);

private:
    void build();
    void update_state();
    void update_command();
    void run();

    StudioBackend& backend_;
    QComboBox* from_ = nullptr;
    QComboBox* to_ = nullptr;
    QComboBox* amount_ = nullptr;
    QComboBox* chunk_ = nullptr;
    StatusChip* result_ = nullptr;
    QLabel* finished_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* metrics_[4] = {};
    NoteBanner* linked_note_ = nullptr;
    CodeBlock* command_ = nullptr;
    QPushButton* copy_result_ = nullptr;
    QPushButton* run_ = nullptr;
    bool has_result_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_BENCHMARK_DIALOG_HPP
