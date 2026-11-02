//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ABOUT_DIALOG_HPP
#define VSS_GUI_ABOUT_DIALOG_HPP

#include <QDialog>

class QTabWidget;

namespace vss::gui {

/// One window with the product, environment, licence and font information a
/// support call always ends up asking for.
class AboutDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    QTabWidget* tabs_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ABOUT_DIALOG_HPP
