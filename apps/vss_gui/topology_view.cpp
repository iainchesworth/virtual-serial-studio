//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology_view.hpp"

#include "theme.hpp"

#include <vss/core/control_lines.hpp>
#include <vss/core/serial_settings.hpp>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace vss::gui {
namespace {

constexpr qreal kNodeWidth = 224.0;
constexpr qreal kNodeHeight = 86.0;
constexpr qreal kHorizontalGap = 74.0;
constexpr qreal kVerticalGap = 64.0;
constexpr qreal kMargin = 48.0;

QPointF rect_edge(const QPointF& centre, const QPointF& unit)
{
    const qreal horizontal =
        std::abs(unit.x()) > 1e-6
        ? (kNodeWidth * 0.5) / std::abs(unit.x())
        : 1e12;
    const qreal vertical =
        std::abs(unit.y()) > 1e-6
        ? (kNodeHeight * 0.5) / std::abs(unit.y())
        : 1e12;
    return centre + unit * std::min(horizontal, vertical);
}

QLineF trimmed_line(const QPointF& from, const QPointF& to)
{
    const QPointF delta = to - from;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length < 1.0)
        return {};

    const QPointF unit(delta.x() / length, delta.y() / length);
    return QLineF(rect_edge(from, unit), rect_edge(to, -unit));
}

void draw_arrow(QPainter* painter, const QLineF& line, bool at_end)
{
    const QPointF tip = at_end ? line.p2() : line.p1();
    const QPointF other = at_end ? line.p1() : line.p2();
    const QLineF axis(tip, other);
    const qreal length = axis.length();
    if (length < 1e-6)
        return;

    const QPointF unit((other.x() - tip.x()) / length,
                       (other.y() - tip.y()) / length);
    const QPointF perpendicular(-unit.y(), unit.x());
    const QPointF base = tip + unit * 9.0;

    QPolygonF arrow;
    arrow << tip << base + perpendicular * 4.5 << base - perpendicular * 4.5;
    painter->drawPolygon(arrow);
}

QString link_mode_label(const vss::link_info& info)
{
    if (info.mode == vss::link_mode::duplex)
    {
        if (info.sources.size() == 1 && info.targets.size() > 1)
            return QStringLiteral("split");
        return QStringLiteral("duplex");
    }
    if (!info.options.emulate_modem_lines)
        return QStringLiteral("monitor");
    if (info.sources.size() > 1)
        return QStringLiteral("join");
    return QStringLiteral("redirect");
}

} // namespace

/// A box representing one endpoint.
class EndpointItem final : public QGraphicsItem
{
public:
    explicit EndpointItem(vss::endpoint_id id)
        : id_(id)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(Qt::PointingHandCursor);
        setZValue(1.0);
    }

    vss::endpoint_id id() const noexcept { return id_; }

    QPointF centre() const noexcept
    {
        return pos() + QPointF(kNodeWidth * 0.5, kNodeHeight * 0.5);
    }

    void update_info(const vss::endpoint_info& info)
    {
        name_ = QString::fromStdString(info.name);
        kind_ = info.kind;
        state_ = info.state;
        path_ = QString::fromStdString(info.path);

        QString tooltip = name_;
        tooltip += QStringLiteral("\n%1 · %2").arg(kind_text(kind_),
                                                   state_text(state_));
        if (!info.path.empty())
        {
            tooltip += QStringLiteral("\n%1").arg(
                QString::fromStdString(info.path));
        }
        if (!info.description.empty())
        {
            tooltip += QStringLiteral("\n%1").arg(
                QString::fromStdString(info.description));
        }
        tooltip += QStringLiteral("\n%1").arg(
            QString::fromStdString(vss::to_label(info.serial)));
        tooltip += QStringLiteral("\nRX %1 · TX %2")
                       .arg(format_count(info.stats.bytes_rx),
                            format_count(info.stats.bytes_tx));
        setToolTip(tooltip);
        update();
    }

    QRectF boundingRect() const override
    {
        return QRectF(-3.0, -3.0, kNodeWidth + 6.0, kNodeHeight + 6.0);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        painter->setRenderHint(QPainter::Antialiasing, true);

        QColor border = state_colour(state_);
        QColor fill(0xff, 0xff, 0xff);
        if (state_ == vss::endpoint_state::failed)
            fill = QColor(0xff, 0xf4, 0xf4);
        else if (state_ == vss::endpoint_state::closed
                 || state_ == vss::endpoint_state::created)
            fill = QColor(0xf8, 0xf9, 0xfb);
        if (isSelected())
            border = QColor(0x09, 0x69, 0xda);

        painter->setPen(QPen(border, isSelected() ? 2.2 : 1.4));
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(0.0, 0.0, kNodeWidth, kNodeHeight),
                                 9.0, 9.0);

        painter->setPen(Qt::NoPen);
        painter->setBrush(state_colour(state_));
        painter->drawEllipse(QPointF(16.0, 20.0), 4.5, 4.5);

        QFont title = painter->font();
        title.setBold(true);
        title.setPointSizeF(10.0);
        painter->setFont(title);
        painter->setPen(QColor(0x1f, 0x23, 0x28));
        const QFontMetrics title_metrics(title);
        painter->drawText(
            QRectF(28.0, 8.0, kNodeWidth - 42.0, 24.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            title_metrics.elidedText(name_, Qt::ElideRight,
                                     static_cast<int>(kNodeWidth - 42.0)));

        QFont small = painter->font();
        small.setBold(false);
        small.setPointSizeF(8.0);
        painter->setFont(small);
        const QFontMetrics small_metrics(small);

        painter->setPen(QColor(0x6e, 0x77, 0x81));
        painter->drawText(
            QRectF(15.0, 40.0, 118.0, 18.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            small_metrics.elidedText(kind_text(kind_), Qt::ElideRight, 116));

        painter->setPen(state_colour(state_));
        painter->drawText(QRectF(kNodeWidth - 116.0, 40.0, 102.0, 18.0),
                          Qt::AlignRight | Qt::AlignVCenter,
                          state_text(state_));

        const QString path = path_.isEmpty()
            ? QStringLiteral("(no advertised path)")
            : path_;
        painter->setPen(QColor(0x57, 0x60, 0x6a));
        painter->drawText(
            QRectF(15.0, 60.0, kNodeWidth - 30.0, 18.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            small_metrics.elidedText(path, Qt::ElideMiddle,
                                     static_cast<int>(kNodeWidth - 30.0)));
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            if (QGraphicsScene* owner = scene())
            {
                owner->clearSelection();
                setSelected(true);
            }
            event->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(event);
    }

private:
    vss::endpoint_id id_;
    QString name_;
    vss::endpoint_kind kind_ = vss::endpoint_kind::virtual_port;
    vss::endpoint_state state_ = vss::endpoint_state::created;
    QString path_;
};

/// A labelled line (or fan of lines) representing one link.
class LinkItem final : public QGraphicsItem
{
public:
    explicit LinkItem(vss::link_id id)
        : id_(id)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(Qt::PointingHandCursor);
        setZValue(-1.0);
    }

    vss::link_id id() const noexcept { return id_; }

    void set_endpoints(const std::vector<QPointF>& sources,
                       const std::vector<QPointF>& targets)
    {
        lines_.clear();
        path_ = QPainterPath();
        midpoint_ = QPointF();
        int segments = 0;

        for (const auto& source : sources)
        {
            for (const auto& target : targets)
            {
                const QLineF line = trimmed_line(source, target);
                if (line.length() <= 0.0)
                    continue;

                lines_.push_back(line);
                path_.moveTo(line.p1());
                path_.lineTo(line.p2());
                midpoint_ += (line.p1() + line.p2()) * 0.5;
                ++segments;
            }
        }

        if (segments > 0)
        {
            midpoint_ = QPointF(midpoint_.x() / segments,
                                midpoint_.y() / segments);
        }
        update();
    }

    void update_info(const vss::link_info& info)
    {
        duplex_ = info.mode == vss::link_mode::duplex;
        text_ = QStringLiteral("%1 · %2")
                    .arg(link_mode_label(info), format_bytes(info.stats.bytes));

        QFont font;
        font.setPointSizeF(8.0);
        const QFontMetricsF metrics(font);
        const qreal width =
            std::min<qreal>(metrics.horizontalAdvance(text_) + 14.0, 240.0);
        label_rect_ = QRectF(midpoint_.x() - width * 0.5,
                             midpoint_.y() - 25.0, width, 18.0);

        QString tooltip = QStringLiteral("Link #%1 · %2")
                              .arg(static_cast<qulonglong>(info.id))
                              .arg(link_mode_label(info));
        tooltip += QStringLiteral("\n%1 sources → %2 targets")
                       .arg(info.sources.size())
                       .arg(info.targets.size());
        tooltip += QStringLiteral("\n%1 bytes in %2 messages")
                       .arg(format_count(info.stats.bytes),
                            format_count(info.stats.messages));
        tooltip += QStringLiteral("\nArbitration: %1")
                       .arg(QString::fromLatin1(
                           vss::to_string(info.options.policy)));
        if (!info.options.emulate_modem_lines)
            tooltip += QStringLiteral("\nModem line emulation off (tap)");
        setToolTip(tooltip);
        update();
    }

    QRectF boundingRect() const override
    {
        QRectF bounds = path_.boundingRect();
        if (bounds.isNull())
            bounds = QRectF(midpoint_, QSizeF(1.0, 1.0));
        bounds = bounds.united(label_rect_);
        return bounds.adjusted(-15.0, -15.0, 15.0, 15.0);
    }

    QPainterPath shape() const override
    {
        QPainterPath outline;
        QPainterPathStroker stroker;
        stroker.setWidth(10.0);
        outline = stroker.createStroke(path_);
        outline.addRect(label_rect_);
        return outline;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        if (lines_.empty())
            return;

        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor colour = isSelected() ? QColor(0x09, 0x69, 0xda)
                                           : QColor(0x8c, 0x95, 0x9f);
        QPen pen(colour, isSelected() ? 2.4 : 1.6);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        for (const auto& line : lines_)
            painter->drawLine(line);

        painter->setPen(Qt::NoPen);
        painter->setBrush(colour);
        for (const auto& line : lines_)
        {
            draw_arrow(painter, line, true);
            if (duplex_)
                draw_arrow(painter, line, false);
        }

        QFont font;
        font.setPointSizeF(8.0);
        painter->setFont(font);
        const QFontMetricsF metrics(font);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xff, 0xff, 0xff, 238));
        painter->drawRoundedRect(label_rect_, 4.0, 4.0);
        painter->setPen(QColor(0x57, 0x60, 0x6a));
        painter->setBrush(Qt::NoBrush);
        painter->drawText(
            label_rect_, Qt::AlignCenter,
            metrics.elidedText(text_, Qt::ElideRight, label_rect_.width() - 8.0));
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            if (QGraphicsScene* owner = scene())
            {
                owner->clearSelection();
                setSelected(true);
            }
            event->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(event);
    }

private:
    vss::link_id id_;
    std::vector<QLineF> lines_;
    QPainterPath path_;
    QPointF midpoint_;
    QRectF label_rect_;
    QString text_;
    bool duplex_ = true;
};

TopologyView::TopologyView(QWidget* parent)
    : QGraphicsView(parent)
{
    setObjectName(QStringLiteral("topology_view"));
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setBackgroundBrush(QColor(0xf7, 0xf8, 0xfa));
    setMinimumSize(420, 300);

    connect(scene_, &QGraphicsScene::selectionChanged, this,
            &TopologyView::on_scene_selection_changed);
}

void TopologyView::set_topology(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links)
{
    bool structure_changed =
        nodes_.size() != static_cast<int>(endpoints.size())
        || links_.size() != static_cast<int>(links.size());

    if (!structure_changed)
    {
        for (const auto& info : endpoints)
        {
            if (!nodes_.contains(static_cast<quint64>(info.id)))
            {
                structure_changed = true;
                break;
            }
        }
    }
    if (!structure_changed)
    {
        for (const auto& link : links)
        {
            if (!links_.contains(static_cast<quint64>(link.id)))
            {
                structure_changed = true;
                break;
            }
        }
    }

    if (structure_changed)
        rebuild(endpoints, links);
    else
        update_items(endpoints, links);
}

bool TopologyView::resolve_positions(const vss::link_info& link,
                                     std::vector<QPointF>& sources,
                                     std::vector<QPointF>& targets) const
{
    sources.clear();
    targets.clear();

    for (const auto id : link.sources)
    {
        const auto it = nodes_.constFind(static_cast<quint64>(id));
        if (it == nodes_.constEnd())
            return false;
        sources.push_back(it.value()->centre());
    }
    for (const auto id : link.targets)
    {
        const auto it = nodes_.constFind(static_cast<quint64>(id));
        if (it == nodes_.constEnd())
            return false;
        targets.push_back(it.value()->centre());
    }
    return !sources.empty() && !targets.empty();
}

void TopologyView::apply_layout()
{
    if (nodes_.isEmpty())
        return;

    const int count = static_cast<int>(nodes_.size());
    const int columns =
        count <= 1
        ? 1
        : std::min(4, static_cast<int>(
                         std::ceil(std::sqrt(static_cast<double>(count)))));

    QList<quint64> ids = nodes_.keys();
    std::sort(ids.begin(), ids.end());

    int index = 0;
    for (const auto id : ids)
    {
        EndpointItem* node = nodes_.value(id);
        if (node == nullptr)
            continue;
        const int row = index / columns;
        const int column = index % columns;
        node->setPos(kMargin + column * (kNodeWidth + kHorizontalGap),
                     kMargin + row * (kNodeHeight + kVerticalGap));
        ++index;
    }
}

void TopologyView::rebuild(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links)
{
    const vss::endpoint_id keep_endpoint = selected_endpoint_;
    const vss::link_id keep_link = selected_link_;

    updating_ = true;
    scene_->clear();
    nodes_.clear();
    links_.clear();
    selected_endpoint_ = 0;
    selected_link_ = 0;

    for (const auto& info : endpoints)
    {
        auto* node = new EndpointItem(info.id);
        node->update_info(info);
        scene_->addItem(node);
        nodes_.insert(static_cast<quint64>(info.id), node);
    }
    apply_layout();

    for (const auto& link : links)
    {
        std::vector<QPointF> sources;
        std::vector<QPointF> targets;
        if (!resolve_positions(link, sources, targets))
            continue;

        auto* item = new LinkItem(link.id);
        item->set_endpoints(sources, targets);
        item->update_info(link);
        scene_->addItem(item);
        links_.insert(static_cast<quint64>(link.id), item);
    }

    if (endpoints.empty())
    {
        auto* placeholder = scene_->addText(
            QStringLiteral("No ports yet.\n"
                           "Use Ports → Add virtual port to begin."));
        placeholder->setDefaultTextColor(QColor(0x8b, 0x94, 0x9e));
        QFont font = placeholder->font();
        font.setPointSizeF(11.0);
        placeholder->setFont(font);
        placeholder->setPos(kMargin, kMargin);
    }

    QRectF content = scene_->itemsBoundingRect();
    if (content.isEmpty())
        content = QRectF(0.0, 0.0, 820.0, 560.0);
    content = content.adjusted(-70.0, -70.0, 70.0, 70.0);
    if (content.width() < 820.0)
        content.setWidth(820.0);
    if (content.height() < 560.0)
        content.setHeight(560.0);
    scene_->setSceneRect(content);

    if (keep_endpoint != 0)
    {
        const auto it = nodes_.find(static_cast<quint64>(keep_endpoint));
        if (it != nodes_.end())
        {
            it.value()->setSelected(true);
            selected_endpoint_ = keep_endpoint;
        }
    }
    if (selected_endpoint_ == 0 && keep_link != 0)
    {
        const auto it = links_.find(static_cast<quint64>(keep_link));
        if (it != links_.end())
        {
            it.value()->setSelected(true);
            selected_link_ = keep_link;
        }
    }

    updating_ = false;

    if (!fitted_ && !nodes_.isEmpty())
    {
        fitted_ = true;
        QTimer::singleShot(0, this, &TopologyView::fit_content);
    }
    viewport()->update();
}

void TopologyView::update_items(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links)
{
    for (const auto& info : endpoints)
    {
        const auto it = nodes_.find(static_cast<quint64>(info.id));
        if (it != nodes_.end())
            it.value()->update_info(info);
    }

    for (const auto& link : links)
    {
        const auto it = links_.find(static_cast<quint64>(link.id));
        if (it == links_.end())
            continue;

        std::vector<QPointF> sources;
        std::vector<QPointF> targets;
        if (!resolve_positions(link, sources, targets))
            continue;
        it.value()->set_endpoints(sources, targets);
        it.value()->update_info(link);
    }
}

void TopologyView::select_endpoint(vss::endpoint_id id)
{
    if (id == selected_endpoint_)
    {
        if (id == 0)
            return;
        const auto current = nodes_.find(static_cast<quint64>(id));
        if (current != nodes_.end() && current.value()->isSelected())
            return;
    }

    updating_ = true;
    scene_->clearSelection();
    selected_endpoint_ = 0;
    selected_link_ = 0;
    if (id != 0)
    {
        const auto it = nodes_.find(static_cast<quint64>(id));
        if (it != nodes_.end())
        {
            it.value()->setSelected(true);
            selected_endpoint_ = id;
        }
    }
    updating_ = false;
}

void TopologyView::clear_selection()
{
    if (selected_endpoint_ == 0 && selected_link_ == 0
        && scene_->selectedItems().isEmpty())
        return;

    updating_ = true;
    scene_->clearSelection();
    selected_endpoint_ = 0;
    selected_link_ = 0;
    updating_ = false;
}

void TopologyView::on_scene_selection_changed()
{
    if (updating_)
        return;

    vss::endpoint_id endpoint = 0;
    vss::link_id link = 0;
    const QList<QGraphicsItem*> selected = scene_->selectedItems();
    for (QGraphicsItem* item : selected)
    {
        if (auto* node = dynamic_cast<EndpointItem*>(item))
        {
            endpoint = node->id();
            break;
        }
        if (auto* edge = dynamic_cast<LinkItem*>(item))
            link = edge->id();
    }

    if (endpoint == selected_endpoint_ && link == selected_link_)
        return;

    selected_endpoint_ = endpoint;
    selected_link_ = link;
    Q_EMIT selection_changed(static_cast<quint64>(endpoint),
                           static_cast<quint64>(link));
}

int TopologyView::node_count() const
{
    return static_cast<int>(nodes_.size());
}

int TopologyView::link_count() const
{
    return static_cast<int>(links_.size());
}

void TopologyView::fit_content()
{
    QRectF content = scene_->itemsBoundingRect();
    if (content.isEmpty())
    {
        resetTransform();
        return;
    }

    content = content.adjusted(-50.0, -50.0, 50.0, 50.0);
    const qreal available_width =
        std::max<qreal>(80.0, static_cast<qreal>(viewport()->width()) - 20.0);
    const qreal available_height =
        std::max<qreal>(80.0, static_cast<qreal>(viewport()->height()) - 20.0);

    qreal factor = std::min(available_width / content.width(),
                            available_height / content.height());
    factor = std::clamp(factor, 0.15, 1.25);

    resetTransform();
    scale(factor, factor);
    centerOn(content.center());
}

void TopologyView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, QColor(0xf7, 0xf8, 0xfa));

    constexpr int grid = 28;
    QPen pen(QColor(0xe6, 0xe9, 0xee));
    pen.setWidth(0);
    painter->setPen(pen);

    const qreal left = std::floor(rect.left() / grid) * grid;
    const qreal top = std::floor(rect.top() / grid) * grid;
    for (qreal x = left; x <= rect.right(); x += grid)
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = top; y <= rect.bottom(); y += grid)
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
}

void TopologyView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        const QPoint delta = event->angleDelta();
        if (!delta.isNull())
        {
            const double factor = std::pow(1.0015, delta.y());
            const double current = transform().m11();
            if ((factor > 1.0 && current < 4.0)
                || (factor < 1.0 && current > 0.1))
            {
                scale(factor, factor);
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

} // namespace vss::gui
