#include "client/MiniMap.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::client {

MiniMap::MiniMap(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setMinimumHeight(120);
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
    const double scale = std::min(double(width()) / m_view.width, double(height()) / m_view.height);
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
    img.fill(Qt::black);
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
    for (const ViewTown& t : m_view.towns) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (m_view.inBounds({t.pos.x + dx, t.pos.y + dy})) img.setPixelColor(t.pos.x + dx, t.pos.y + dy, colourOf(t.owner));
    }
    for (const ViewHero& h : m_view.heroes)
        if (m_view.inBounds(h.pos)) img.setPixelColor(h.pos.x, h.pos.y, colourOf(h.owner).lighter(130));
    p.drawImage(r, img);
    if (!m_visible.isEmpty()) {
        const double sx = r.width() / m_view.width, sy = r.height() / m_view.height;
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(r.x() + m_visible.x() * sx, r.y() + m_visible.y() * sy, m_visible.width() * sx,
                          m_visible.height() * sy));
    }
}

void MiniMap::mousePressEvent(QMouseEvent* e) {
    const core::Pos t = tileAt(e->position());
    if (t.x >= 0) emit centerRequested(t);
}

void MiniMap::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
}

}  // namespace kot::client
