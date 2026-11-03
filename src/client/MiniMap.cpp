#include "client/MiniMap.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::client {

MiniMap::MiniMap(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setMinimumHeight(190);
    setToolTip(tr("Click to look there"));
}

void MiniMap::setView(const GameView& view) {
    m_view = view;
    m_colors.clear();
    for (const QString& id : m_view.terrainPalette) {
        const core::TerrainDef* t = m_content ? m_content->terrainType(id) : nullptr;
        m_colors.append(t ? QColor(t->color) : QColor(Qt::magenta));
    }
    update();
}

void MiniMap::setVisibleTiles(const QRect& tiles) {
    m_visible = tiles;
    update();
}

QRectF MiniMap::mapRect() const {
    if (m_view.width <= 0 || m_view.height <= 0) return {};
    const double scale = std::min(double(width() - 8) / m_view.width, double(height() - 8) / m_view.height);  // room for the frame
    const double w = m_view.width * scale, h = m_view.height * scale;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

core::Pos MiniMap::tileAt(QPointF p) const {
    const QRectF r = mapRect();
    if (r.isEmpty()) return {-1, -1};
    return {std::clamp(int((p.x() - r.x()) / r.width() * m_view.width), 0, m_view.width - 1),
            std::clamp(int((p.y() - r.y()) / r.height() * m_view.height), 0, m_view.height - 1)};
}

void MiniMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const QRectF r = mapRect();
    if (r.isEmpty()) return;
    QImage img(m_view.width, m_view.height, QImage::Format_RGB32);
    img.fill(QColor(14, 12, 20));  // the unknown
    for (int y = 0; y < m_view.height; ++y)
        for (int x = 0; x < m_view.width; ++x) {
            const int index = m_view.tiles.value(y * m_view.width + x, -1);
            if (index < 0) continue;
            QColor c = m_colors.value(index, Qt::magenta);
            if (!m_view.inSight({x, y})) c = c.darker(200);
            img.setPixelColor(x, y, c);
        }
    const auto colourOf = [this](int owner) {
        const ViewPlayer* pl = m_view.player(owner);
        return pl ? QColor(pl->color) : QColor("#cccccc");
    };
    p.drawImage(r, img);
    // Markers on top, sized for the eye rather than the map: towns as little keeps,
    // mines as dots, monsters as red dots, heroes as bright rings.
    p.setRenderHint(QPainter::Antialiasing, true);
    const double sx = r.width() / m_view.width, sy = r.height() / m_view.height;
    const auto at = [&](core::Pos t) { return QPointF(r.x() + (t.x + 0.5) * sx, r.y() + (t.y + 0.5) * sy); };
    const double unit = std::max(2.5, std::min(sx, sy) * 1.6);
    for (const ViewMine& m : m_view.mines) {
        if (!m_view.known(m.pos)) continue;
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.setBrush(m.owner ? colourOf(m.owner) : QColor("#8a8a8a"));
        p.drawEllipse(at(m.pos), unit * 0.55, unit * 0.55);
    }
    for (const ViewArmy& a : m_view.armies) {
        if (a.hero || !m_view.inSight(a.pos) || m_view.townAt(a.pos)) continue;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e03030"));
        p.drawEllipse(at(a.pos), unit * 0.45, unit * 0.45);
    }
    for (const ViewTown& t : m_view.towns) {
        const QPointF c = at({t.pos.x, t.pos.y}) + QPointF(sx * 0.5, -sy * 0.5);  // the middle of its 2x2 block
        const QRectF keep(c.x() - unit * 1.2, c.y() - unit * 1.0, unit * 2.4, unit * 2.0);
        p.setPen(QPen(QColor(20, 12, 4), 1.2));
        p.setBrush(t.owner ? colourOf(t.owner) : QColor("#bcbcbc"));
        p.drawRect(keep);
        p.drawRect(QRectF(keep.left() - unit * 0.3, keep.top() - unit * 0.5, unit * 0.7, unit * 0.7));
        p.drawRect(QRectF(keep.right() - unit * 0.4, keep.top() - unit * 0.5, unit * 0.7, unit * 0.7));
    }
    for (const ViewHero& h : m_view.heroes) {
        if (!m_view.inBounds(h.pos)) continue;
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(colourOf(h.owner));
        p.drawEllipse(at(h.pos), unit * 0.8, unit * 0.8);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    if (!m_visible.isEmpty()) {
        p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
        p.setBrush(QColor(255, 255, 255, 25));
        p.drawRect(QRectF(r.x() + m_visible.x() * sx, r.y() + m_visible.y() * sy, m_visible.width() * sx,
                          m_visible.height() * sy));
    }
    // A gold frame.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#c9a24a"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 4, 4);
}

void MiniMap::mousePressEvent(QMouseEvent* e) {
    const core::Pos t = tileAt(e->position());
    if (t.x >= 0) emit centerRequested(t);
}

void MiniMap::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
}

}  // namespace kot::client
