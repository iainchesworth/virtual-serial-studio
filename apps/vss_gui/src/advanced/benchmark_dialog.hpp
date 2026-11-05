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

#include <atomic>
#include <thread>

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
/// Run sends the chosen amount from one port and counts what arrives on the
/// other, through `StudioBackend::run_benchmark`: the same core as
/// `vss_cli bench`.  The run is on a worker thread, so the dialog stays
/// responsive; while it runs the Run button becomes Cancel.  A backend without
/// `Capabilities::benchmark` leaves Run disabled with the degraded tooltip and
/// no result is ever invented.  The linked-port refusal is shown either way.
class BenchmarkDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit BenchmarkDialog(StudioBackend& backend, QWidget* parent = nullptr);
    /// Stops a run in progress and waits for its thread.
    ~BenchmarkDialog() override;

    /// True from the click on Run until the result is shown.
    bool running() const noexcept { return running_; }
    /// The text Copy result puts on the clipboard, or empty before a result.
    QString summary() const { return summary_; }
    /// The value shown for Sent (0), Received (1), Lost (2) or Throughput (3).
    QString metric_text(int index) const;
    /// The line under the result chip: progress, the finish time or the error.
    QString finished_text() const;
    /// Closing the dialog while a run is going asks it to stop.
    void done(int result) override;

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
    void start();
    void on_progress(const vss::benchmark_progress& progress);
    void on_finished(const vss::result<vss::benchmark_result>& outcome);
    void set_metrics(const QString& sent, const QString& received,
                     const QString& lost, const QString& throughput);

    StudioBackend& backend_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    bool running_ = false;
    QString summary_;
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
