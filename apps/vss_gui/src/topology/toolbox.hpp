//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "backend/canvas_edit.hpp"
#include "backend/capabilities.hpp"

#include <QListWidget>
#include <QString>
#include <QStringList>

#include <optional>

class QMimeData;

namespace vss::gui::topology
{

/// The MIME type a tile carries while it is dragged onto the canvas.
QString toolbox_mime_type();
/// The tile a drag carries, if it is one.
std::optional<canvas_item> item_from_mime(const QMimeData* data);

/// The tile's one-word label, its "What it builds" sentence and its icon.
QString item_label(canvas_item item);
QString item_description(canvas_item item);
QString item_icon(canvas_item item);

/// The Advanced canvas toolbox: a rail of port and bundle tiles to drag onto
/// the canvas, or to add with Enter, Space or a double-click.  A tile the build
/// cannot make is disabled with "Not available in this build".  See
/// `docs/gui-design/docs/screens/07-canvas-editor.md`.
class Toolbox final : public QListWidget
{
    Q_OBJECT

public:
    explicit Toolbox(QWidget* parent = nullptr);

    void set_capabilities(const capabilities& caps);

    /// The tiles in order, for the tests.
    QStringList tile_labels() const;
    bool tile_enabled(canvas_item item) const;
    /// Selects a tile, as the arrow keys would.
    void focus_item(canvas_item item);

Q_SIGNALS:
    /// Enter, Space or a double-click on a tile: add it at a free spot.
    void add_requested(vss::gui::canvas_item item);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild();

    capabilities caps_;
};

} // namespace vss::gui::topology
