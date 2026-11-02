#include "client/MapView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::client {

namespace {
QColor resourceColor(const QString& id) {
    static const QMap<QString, QColor> colors = {
        {"gold", "#ffd700"}, {"wood", "#8b5a2b"}, {"stone", "#aaaaaa"}, {"grain", "#e6c35c"},
        {"gems", "#ff4fd8"}, {"sulphur", "#e8e800"}, {"coal", "#333333"}, {"mercury", "#a0a0ff"},
        {"papyrus", "#d2b48c"}};
    return colors.value(id, QColor::fromHsv(static_cast<int>(qHash(id) % 360), 200, 220));
}
}  // namespace

MapView::MapView(const core::ContentRegistry* content, QWidget* parent)
    : QAbstractScrollArea(parent), m_content(content) {
    viewport()->setMouseTracking(false);
    horizontalScrollBar()->setSingleStep(m_tile);
    verticalScrollBar()->setSingleStep(m_tile);
}

void MapView::setView(const GameView& view) {
    m_view = view;
    m_paletteColors.clear();
    for (const QString& terrain : m_view.terrainPalette) {
        const core::TerrainDef* t = m_content ? m_content->terrainType(terrain) : nullptr;
        m_paletteColors.append(t ? QColor(t->color) : QColor(Qt::magenta));
    }
    updateScrollBars();
    viewport()->update();
}

void MapView::setSelectedHero(int heroId) {
    m_selectedHero = heroId;
    viewport()->update();
}

void MapView::setTileSize(int pixels) {
    m_tile = std::clamp(pixels, 6, 48);
    horizontalScrollBar()->setSingleStep(m_tile);
    verticalScrollBar()->setSingleStep(m_tile);
    updateScrollBars();
    viewport()->update();
}

void MapView::updateScrollBars() {
    const QSize content(m_view.width * m_tile, m_view.height * m_tile);
    horizontalScrollBar()->setRange(0, std::max(0, content.width() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(0, std::max(0, content.height() - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void MapView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void MapView::centerOn(core::Pos tile) {
    horizontalScrollBar()->setValue(tile.x * m_tile + m_tile / 2 - viewport()->width() / 2);
    verticalScrollBar()->setValue(tile.y * m_tile + m_tile / 2 - viewport()->height() / 2);
}

core::Pos MapView::tileAt(const QPoint& p) const {
    return core::Pos{(p.x() + horizontalScrollBar()->value()) / m_tile,
                     (p.y() + verticalScrollBar()->value()) / m_tile};
}

QRect MapView::tileRect(core::Pos t) const {
    return QRect(t.x * m_tile - horizontalScrollBar()->value(), t.y * m_tile - verticalScrollBar()->value(),
                 m_tile, m_tile);
}

QColor MapView::ownerColor(int owner) const {
    const ViewPlayer* p = m_view.player(owner);
    return p ? QColor(p->color) : QColor("#999999");  // neutral
}

void MapView::paintEvent(QPaintEvent*) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), Qt::black);
    if (m_view.width == 0) return;

    const int hx = horizontalScrollBar()->value(), vy = verticalScrollBar()->value();
    const int x0 = hx / m_tile, y0 = vy / m_tile;
    const int x1 = std::min(m_view.width - 1, (hx + viewport()->width()) / m_tile);
    const int y1 = std::min(m_view.height - 1, (vy + viewport()->height()) / m_tile);

    // Terrain and fog.
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int index = m_view.tiles[y * m_view.width + x];
            if (index < 0) continue;  // unexplored: stays black
            QColor c = m_paletteColors.value(index, Qt::magenta);
            if (!m_view.inSight({x, y})) c = c.darker(230);  // remembered, not in sight
            p.fillRect(tileRect({x, y}), c);
        }
    }

    const auto shown = [&](core::Pos t) { return m_view.known(t); };
    p.setRenderHint(QPainter::Antialiasing, true);
    const int m = std::max(1, m_tile / 8);  // margin inside a tile

    for (const ViewMine& mine : m_view.mines) {
        if (!shown(mine.pos)) continue;
        const QRect r = tileRect(mine.pos).adjusted(m, m, -m, -m);
        QPolygon diamond;
        diamond << QPoint(r.center().x(), r.top()) << QPoint(r.right(), r.center().y())
                << QPoint(r.center().x(), r.bottom()) << QPoint(r.left(), r.center().y());
        p.setPen(QPen(mine.owner ? ownerColor(mine.owner) : Qt::black, 2));
        p.setBrush(resourceColor(mine.resource));
        p.drawPolygon(diamond);
    }
    for (const ViewDwelling& d : m_view.dwellings) {
        if (!shown(d.pos)) continue;
        const QRect r = tileRect(d.pos).adjusted(m, m, -m, -m);
        QPolygon tri;
        tri << QPoint(r.center().x(), r.top()) << QPoint(r.right(), r.bottom()) << QPoint(r.left(), r.bottom());
        p.setPen(QPen(d.owner ? ownerColor(d.owner) : Qt::black, 2));
        p.setBrush(Qt::white);
        p.drawPolygon(tri);
    }
    for (const ViewTown& t : m_view.towns) {
        if (!shown(t.pos)) continue;
        const QRect r = tileRect(t.pos).adjusted(1, 1, -1, -1);
        p.setPen(QPen(Qt::black, 2));
        p.setBrush(ownerColor(t.owner));
        p.drawRect(r);
        if (m_tile >= 14) {
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, "T");
        }
    }
    for (const ViewHero& h : m_view.heroes) {
        if (!shown(h.pos)) continue;
        const QRect r = tileRect(h.pos).adjusted(m, m, -m, -m);
        const bool selected = (h.id == m_selectedHero);
        p.setPen(QPen(selected ? QColor("#ffff00") : Qt::white, selected ? 3 : 2));
        p.setBrush(ownerColor(h.owner));
        p.drawEllipse(r);
    }
}

void MapView::mousePressEvent(QMouseEvent* event) {
    m_pressPos = event->pos();
    event->accept();
}

void MapView::mouseReleaseEvent(QMouseEvent* event) {
    if ((event->pos() - m_pressPos).manhattanLength() > 6) return;
    const core::Pos t = tileAt(event->pos());
    if (!m_view.inBounds(t)) return;
    if (event->button() == Qt::LeftButton) emit tileClicked(t);
    else if (event->button() == Qt::RightButton) emit tileRightClicked(t);
}

void MapView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        setTileSize(m_tile + (event->angleDelta().y() > 0 ? 2 : -2));
        event->accept();
    } else {
        QAbstractScrollArea::wheelEvent(event);
    }
}

}  // namespace kot::client
