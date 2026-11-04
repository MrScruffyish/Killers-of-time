#include "client/MiniMap.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "core/content/ContentRegistry.h"

namespace kot::client {

MiniMap::MiniMap(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setMinimumHeight(190);
    setToolTip(tr("Click to look there"));
}

void MiniMap::setView(const GameView& view) {
    m_view = view;
    m_colors.clear();
    m_kinds.clear();
    m_water.clear();
    for (const QString& id : m_view.terrainPalette) {
        const core::TerrainDef* t = m_content ? m_content->terrainType(id) : nullptr;
        m_colors.append(t ? QColor(t->color) : QColor(Qt::magenta));
        m_kinds.append(id);
        m_water.append(t && t->sailable);
    }
    rebuildImage();
    update();
}

namespace {
constexpr int kPixels = 4;
quint32 tileHash(int x, int y, int i) { return (quint32(x) * 73856093u) ^ (quint32(y) * 19349663u) ^ (quint32(i) * 83492791u); }
}  // namespace

void MiniMap::rebuildImage() {
    const int w = m_view.width, h = m_view.height;
    if (w <= 0 || h <= 0) {
        m_image = QImage();
        return;
    }
    // Show what is explored, with a margin, at least kMinArea tiles across and in
    // the widget's shape, so a fresh game does not show a speck in the dark.
    constexpr int kMinArea = 24, kMargin = 3;
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (m_view.tiles.value(y * w + x, -1) >= 0) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
    if (maxX < 0) {
        m_area = QRect(0, 0, w, h);
    } else {
        QRect a(QPoint(minX - kMargin, minY - kMargin), QPoint(maxX + kMargin, maxY + kMargin));
        const double aspect = double(std::max(1, width() - 8)) / std::max(1, height() - 8);
        int aw = std::max({a.width(), kMinArea, int(std::ceil(a.height() * aspect))});
        int ah = std::max({a.height(), int(std::ceil(kMinArea / aspect)), int(std::ceil(aw / aspect))});
        aw = std::min(aw, w);
        ah = std::min(ah, h);
        const QPoint c = a.center();
        m_area = QRect(std::clamp(c.x() - aw / 2, 0, w - aw), std::clamp(c.y() - ah / 2, 0, h - ah), aw, ah);
    }
    m_image = QImage(w * kPixels, h * kPixels, QImage::Format_RGB32);
    // The unknown: dark, cloudy.
    for (int y = 0; y < m_image.height(); ++y)
        for (int x = 0; x < m_image.width(); ++x) {
            const int n = int((tileHash(x / 3, y / 3, 7) >> 11) % 9);
            m_image.setPixel(x, y, qRgb(12 + n, 11 + n, 20 + n));
        }
    const auto indexAt = [&](int x, int y) { return (x < 0 || y < 0 || x >= w || y >= h) ? -1 : m_view.tiles.value(y * w + x, -1); };
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            const int index = indexAt(tx, ty);
            if (index < 0) continue;
            const QColor base = m_colors.value(index, Qt::magenta);
            const QString& kind = m_kinds.value(index);
            const bool water = m_water.value(index);
            const bool seen = m_view.inSight({tx, ty});
            for (int py = 0; py < kPixels; ++py)
                for (int px = 0; px < kPixels; ++px) {
                    const quint32 hsh = tileHash(tx * kPixels + px, ty * kPixels + py, index);
                    QColor c = base;
                    // A little grain, so land is not flat colour.
                    const int grain = int((hsh >> 9) % 13) - 6;
                    c = QColor(std::clamp(c.red() + grain, 0, 255), std::clamp(c.green() + grain, 0, 255), std::clamp(c.blue() + grain, 0, 255));
                    if (kind == "forest" && (hsh >> 5) % 3 == 0) c = c.darker(135);
                    if (kind == "hills" && py == 1 && (px == 1 || px == 2)) c = c.lighter(125);
                    if (kind == "mountains") {
                        // A little peak: light top, dark right flank.
                        if (py == 0 && (px == 1 || px == 2)) c = QColor("#f2f4f8");
                        else if (px >= 2) c = c.darker(135);
                        else c = c.lighter(115);
                    }
                    if (water) {
                        // A light coast where the water touches land.
                        const bool edge = (px == 0 && indexAt(tx - 1, ty) >= 0 && !m_water.value(indexAt(tx - 1, ty))) ||
                                          (px == kPixels - 1 && indexAt(tx + 1, ty) >= 0 && !m_water.value(indexAt(tx + 1, ty))) ||
                                          (py == 0 && indexAt(tx, ty - 1) >= 0 && !m_water.value(indexAt(tx, ty - 1))) ||
                                          (py == kPixels - 1 && indexAt(tx, ty + 1) >= 0 && !m_water.value(indexAt(tx, ty + 1)));
                        if (edge) c = QColor("#d8c890");
                        else if ((hsh >> 7) % 7 == 0) c = c.lighter(118);
                    }
                    if (!seen) c = QColor((c.red() + 30) / 3, (c.green() + 26) / 3, (c.blue() + 44) / 3);  // remembered: dim and cool
                    m_image.setPixelColor(tx * kPixels + px, ty * kPixels + py, c);
                }
        }
}

void MiniMap::setVisibleTiles(const QRect& tiles) {
    m_visible = tiles;
    update();
}

QRectF MiniMap::mapRect() const {
    if (m_area.isEmpty()) return {};
    const double scale = std::min(double(width() - 8) / m_area.width(), double(height() - 8) / m_area.height());  // room for the frame
    const double w = m_area.width() * scale, h = m_area.height() * scale;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

core::Pos MiniMap::tileAt(QPointF p) const {
    const QRectF r = mapRect();
    if (r.isEmpty()) return {-1, -1};
    return {std::clamp(m_area.x() + int((p.x() - r.x()) / r.width() * m_area.width()), 0, m_view.width - 1),
            std::clamp(m_area.y() + int((p.y() - r.y()) / r.height() * m_area.height()), 0, m_view.height - 1)};
}

void MiniMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const QRectF r = mapRect();
    if (r.isEmpty()) return;
    const auto colourOf = [this](int owner) {
        const ViewPlayer* pl = m_view.player(owner);
        return pl ? QColor(pl->color) : QColor("#cccccc");
    };
    // Smooth when shrinking (big maps), crisp pixels when enlarging (small ones).
    const QRect source(m_area.x() * kPixels, m_area.y() * kPixels, m_area.width() * kPixels, m_area.height() * kPixels);
    p.setRenderHint(QPainter::SmoothPixmapTransform, r.width() < source.width());
    p.drawImage(r, m_image, source);
    p.save();
    p.setClipRect(r);
    // Markers on top, sized for the eye rather than the map: towns as little keeps,
    // mines as dots, monsters as red dots, heroes as bright rings.
    p.setRenderHint(QPainter::Antialiasing, true);
    const double sx = r.width() / m_area.width(), sy = r.height() / m_area.height();
    const auto at = [&](core::Pos t) { return QPointF(r.x() + (t.x - m_area.x() + 0.5) * sx, r.y() + (t.y - m_area.y() + 0.5) * sy); };
    const double unit = std::clamp(std::min(sx, sy) * 1.6, 2.5, 5.0);  // readable, but not huge when zoomed in
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
        p.drawRect(QRectF(r.x() + (m_visible.x() - m_area.x()) * sx, r.y() + (m_visible.y() - m_area.y()) * sy, m_visible.width() * sx,
                          m_visible.height() * sy));
    }
    p.restore();
    // A gold frame.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#c9a24a"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 4, 4);
}

void MiniMap::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    rebuildImage();  // the shown area follows the widget's shape
}

void MiniMap::mousePressEvent(QMouseEvent* e) {
    const core::Pos t = tileAt(e->position());
    if (t.x >= 0) emit centerRequested(t);
}

void MiniMap::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
}

}  // namespace kot::client
