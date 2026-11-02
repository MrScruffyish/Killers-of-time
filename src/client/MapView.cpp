#include "client/MapView.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "client/Art.h"
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
    viewport()->setMouseTracking(true);
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
    if (m_content) m_planner.prepare(m_view, *m_content, m_selectedHero);
    m_plan = m_planner.plan(m_hover);
    updateScrollBars();
    viewport()->update();
}

void MapView::setSelectedHero(int heroId) {
    m_selectedHero = heroId;
    if (m_content) m_planner.prepare(m_view, *m_content, m_selectedHero);
    m_plan = m_planner.plan(m_hover);
    viewport()->update();
}

void MapView::mouseMoveEvent(QMouseEvent* event) {
    const core::Pos t = tileAt(event->pos());
    if (t == m_hover) return;
    m_hover = t;
    m_plan = m_planner.plan(t);
    viewport()->update();
}

void MapView::leaveEvent(QEvent* event) {
    m_hover = {-1, -1};
    m_plan = MovePlan();
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void MapView::scrollContentsBy(int dx, int dy) {
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    emit scrolled();
}

QRect MapView::visibleTiles() const {
    return QRect(horizontalScrollBar()->value() / m_tile, verticalScrollBar()->value() / m_tile,
                 viewport()->width() / m_tile + 1, viewport()->height() / m_tile + 1);
}

bool MapView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString text = describeTile(tileAt(help->pos()));
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(help->globalPos(), text, viewport());
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

QString MapView::describeTile(core::Pos t) const {
    if (!m_view.inBounds(t) || !m_view.known(t)) return {};
    const int index = m_view.tiles.value(t.y * m_view.width + t.x, -1);
    const core::TerrainDef* terrain = m_content ? m_content->terrainType(m_view.terrainPalette.value(index)) : nullptr;
    QStringList lines;
    lines << (terrain ? terrain->name : m_view.terrainPalette.value(index));
    const auto ownerName = [this](int owner) {
        const ViewPlayer* p = m_view.player(owner);
        return p ? p->name : tr("neutral");
    };
    const auto unitName = [this](const QString& id) {
        const core::UnitDef* u = m_content ? m_content->unit(id) : nullptr;
        return u ? u->name : id;
    };
    if (const ViewTown* town = m_view.townAt(t))
        lines << tr("Town %1 (%2)").arg(town->name, ownerName(town->owner));
    for (const ViewMine& m : m_view.mines)
        if (m.pos == t) lines << tr("%1 mine (%2)").arg(m.resource, ownerName(m.owner));
    for (const ViewDwelling& d : m_view.dwellings)
        if (d.pos == t) lines << tr("%1 dwelling (%2)").arg(unitName(d.unit), ownerName(d.owner));
    for (const ViewObject& o : m_view.objects) {
        if (o.pos != t) continue;
        if (o.artifact) {
            const core::ArtifactDef* a = m_content ? m_content->artifact(o.artifactId) : nullptr;
            lines << tr("Artifact: %1 (%2)").arg(a ? a->name : o.artifactId, a ? a->description : QString());
        } else {
            QStringList res;
            for (auto it = o.resources.begin(); it != o.resources.end(); ++it) res << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
            lines << tr("Treasure: %1").arg(res.join(", "));
        }
    }
    for (const ViewArmy& a : m_view.armies) {
        if (a.pos != t || (m_view.townAt(t) && !a.hero)) continue;
        QStringList stacks;
        for (const ViewStack& s : a.stacks) stacks << QStringLiteral("%1 %2").arg(s.count).arg(unitName(s.unit));
        const ViewHero* h = a.hero ? m_view.hero(a.hero) : nullptr;
        const QString who = h ? tr("%1 (%2)").arg(h->name, ownerName(a.owner)) : (a.owner ? ownerName(a.owner) : tr("Monsters"));
        lines << QStringLiteral("%1: %2").arg(who, stacks.isEmpty() ? tr("no units") : stacks.join(", "));
    }
    if (!m_view.inSight(t)) lines << tr("(not in sight: remembered)");
    if (m_plan.found && m_plan.steps.last() == t) {
        lines << (m_plan.affordable == m_plan.steps.size()
                      ? tr("Reachable today (%1 steps)").arg(m_plan.steps.size())
                      : tr("%1 of %2 steps today").arg(m_plan.affordable).arg(m_plan.steps.size()));
    }
    return lines.join("\n");
}

void MapView::setTileSize(int pixels) {
    m_tile = std::clamp(pixels, 6, 64);
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

    // Terrain (textured tiles) and fog: remembered tiles dimmed, unexplored stays black
    // with a soft edge.
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int index = m_view.tiles[y * m_view.width + x];
            if (index < 0) continue;
            const QRect r = tileRect({x, y});
            p.drawPixmap(r.topLeft(), art::terrainTile(m_view.terrainPalette.value(index), m_paletteColors.value(index, Qt::magenta),
                                                       m_tile, art::variantFor(x, y)));
            if (!m_view.inSight({x, y})) p.fillRect(r, QColor(10, 8, 20, 140));
        }
    }
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (m_view.tiles[y * m_view.width + x] >= 0) continue;
            const QRect r = tileRect({x, y});
            const auto dark = [&](int dx, int dy) {
                const core::Pos n{x + dx, y + dy};
                return !m_view.inBounds(n) || m_view.tiles[n.y * m_view.width + n.x] < 0;
            };
            const int f = std::max(2, m_tile / 3);
            QLinearGradient g;
            const auto edge = [&](QRect band, QPointF from, QPointF to) {
                g = QLinearGradient(from, to);
                g.setColorAt(0, QColor(0, 0, 0, 255));
                g.setColorAt(1, QColor(0, 0, 0, 0));
                p.fillRect(band, g);
            };
            if (!dark(-1, 0)) edge(QRect(r.left() - f, r.top(), f, r.height()), QPointF(r.left(), 0), QPointF(r.left() - f, 0));
            if (!dark(1, 0)) edge(QRect(r.right() + 1, r.top(), f, r.height()), QPointF(r.right(), 0), QPointF(r.right() + f, 0));
            if (!dark(0, -1)) edge(QRect(r.left(), r.top() - f, r.width(), f), QPointF(0, r.top()), QPointF(0, r.top() - f));
            if (!dark(0, 1)) edge(QRect(r.left(), r.bottom() + 1, r.width(), f), QPointF(0, r.bottom()), QPointF(0, r.bottom() + f));
        }

    const auto shown = [&](core::Pos t) { return m_view.known(t); };
    p.setRenderHint(QPainter::Antialiasing, true);
    const double m = std::max(1, m_tile / 10);  // margin inside a tile
    const auto inner = [&](core::Pos t) { return QRectF(tileRect(t)).adjusted(m, m, -m, -m); };
    const auto ownerOrNeutral = [&](int owner) { return owner ? ownerColor(owner) : QColor("#9a9a9a"); };

    for (const ViewMine& mine : m_view.mines)
        if (shown(mine.pos)) art::drawMine(p, inner(mine.pos), resourceColor(mine.resource), ownerOrNeutral(mine.owner));
    for (const ViewDwelling& d : m_view.dwellings)
        if (shown(d.pos)) art::drawDwelling(p, inner(d.pos), ownerOrNeutral(d.owner));
    for (const ViewObject& o : m_view.objects) {
        if (!shown(o.pos)) continue;
        if (o.artifact) art::drawArtifact(p, inner(o.pos));
        else art::drawChest(p, inner(o.pos), o.resources.isEmpty() ? QColor("#d4a017") : resourceColor(o.resources.firstKey()));
    }
    for (const ViewTown& t : m_view.towns)
        if (shown(t.pos)) art::drawTown(p, QRectF(tileRect(t.pos)).adjusted(-m, -m * 2, m, 0), ownerOrNeutral(t.owner));
    // Hero-less armies outside towns: neutral monsters (or garrison-less groups).
    for (const ViewArmy& a : m_view.armies) {
        if (a.hero || !shown(a.pos)) continue;
        bool inTown = false;
        for (const ViewTown& t : m_view.towns) inTown = inTown || t.pos == a.pos;
        if (!inTown) art::drawMonster(p, inner(a.pos));
    }
    // Where the selected hero can go today, and the planned path under the mouse.
    if (m_view.isMyTurn() && m_planner.ready()) {
        for (const core::Pos t : m_planner.reachableToday())
            if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1) p.fillRect(tileRect(t), QColor(255, 255, 255, 40));
        if (m_plan.found) {
            for (int i = 0; i < m_plan.steps.size(); ++i) {
                const QRect r = tileRect(m_plan.steps[i]);
                const int d = std::max(3, m_tile / 4);
                p.setPen(Qt::NoPen);
                p.setBrush(i < m_plan.affordable ? QColor("#ffffff") : QColor("#ff4040"));
                p.drawEllipse(r.center(), d / 2, d / 2);
            }
        }
    }
    for (const ViewHero& h : m_view.heroes)
        if (shown(h.pos)) art::drawHero(p, inner(h.pos), ownerColor(h.owner), h.id == m_selectedHero);
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
