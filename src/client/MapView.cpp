#include "client/MapView.h"

#include <QCursor>
#include <QFontMetrics>
#include <QHash>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

using art::resourceColor;

MapView::MapView(const core::ContentRegistry* content, QWidget* parent)
    : QAbstractScrollArea(parent), m_content(content) {
    viewport()->setMouseTracking(true);
    horizontalScrollBar()->setSingleStep(m_tile);
    verticalScrollBar()->setSingleStep(m_tile);
    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(30);
    connect(m_edgeTimer, &QTimer::timeout, this, &MapView::edgeScroll);
    // Dragging a scroll bar is the player scrolling: stop keeping a centre.
    for (QScrollBar* bar : {horizontalScrollBar(), verticalScrollBar()})
        connect(bar, &QScrollBar::sliderPressed, this, [this]() { m_wantCentre.reset(); });
}

void MapView::setView(const GameView& view) {
    m_view = view;
    m_paletteColors.clear();
    for (const QString& terrain : m_view.terrainPalette) {
        const core::TerrainDef* t = m_content ? m_content->terrainType(terrain) : nullptr;
        m_paletteColors.append(t ? QColor(t->color) : QColor(Qt::magenta));
    }
    replan();
    updateScrollBars();
    viewport()->update();
}

void MapView::setSelectedHero(int heroId) {
    m_selectedHero = heroId;
    replan();
}

void MapView::replan() {
    if (m_content) m_planner.prepare(m_view, *m_content, m_selectedHero);
    m_plan = m_planner.plan(m_hover);
    m_routePlan = m_route ? m_planner.plan(*m_route) : MovePlan();
    viewport()->update();
}

void MapView::setRoute(std::optional<core::Pos> target) {
    if (target == m_route) return;
    m_route = target;
    m_routePlan = m_route ? m_planner.plan(*m_route) : MovePlan();
    viewport()->update();
}

void MapView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_edgeTimer->isActive()) {
        const QRect area = viewport()->rect();
        const int band = std::clamp(area.width() / 25, 12, 40);
        const QPoint at = event->pos();
        if (at.x() < band || at.y() < band || at.x() > area.width() - band || at.y() > area.height() - band)
            m_edgeTimer->start();
    }
    const core::Pos t = tileAt(event->pos());
    if (t == m_hover) return;
    m_hover = t;
    m_plan = m_planner.plan(t);
    updatePointer();
    QString info = describeTile(t);
    emit hoverInfo(info.replace('\n', QStringLiteral("  \u00B7  ")));
    viewport()->update();
}

void MapView::updatePointer() {
    using art::Pointer;
    static QHash<int, QCursor> cursors;
    const auto cursor = [](Pointer kind) {
        auto it = cursors.find(int(kind));
        if (it == cursors.end()) {
            const bool tip = kind == Pointer::Normal || kind == Pointer::Select;
            it = cursors.insert(int(kind), QCursor(art::pointerImage(kind), tip ? 3 : 16, tip ? 2 : 16));
        }
        return it.value();
    };
    Pointer kind = Pointer::Normal;
    const core::Pos t = m_hover;
    const ViewHero* hero = m_view.heroAt(t);
    const ViewPlayer* me = m_view.player(m_view.you);
    const auto hostile = [&](int owner) {
        const ViewPlayer* o = m_view.player(owner);
        return owner == 0 || !o || !me || o->team != me->team;
    };
    if (hero && hero->owner == m_view.you) {
        kind = Pointer::Select;
    } else if (m_view.inBounds(t) && m_view.known(t) && m_view.isMyTurn() && m_planner.ready()) {
        bool enemy = false;
        for (const ViewArmy& a : m_view.armies)
            if (a.pos == t && hostile(a.owner) && (a.hero || !m_view.townAt(t))) enemy = true;
        const ViewTown* town = m_view.townCovering(t);
        if (town && hostile(town->owner)) enemy = true;
        if (!m_plan.found) kind = Pointer::Blocked;
        else if (enemy || m_plan.ambushAt >= 0) kind = Pointer::Attack;
        else if (town) kind = Pointer::Enter;
        else kind = m_plan.affordable == m_plan.steps.size() ? Pointer::Move : Pointer::MoveLater;
    }
    viewport()->setCursor(cursor(kind));
}

void MapView::leaveEvent(QEvent* event) {
    m_edgeTimer->stop();
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
    if (const ViewTown* town = m_view.townCovering(t))
        lines << tr("Town %1 (%2)").arg(town->name, ownerName(town->owner))
              << (town->mine ? tr("Right click or Shift+click: open the town") : tr("Walk to its gate to visit or attack it"));
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
                      : tr("%1 of %2 steps today; the rest on the next days").arg(m_plan.affordable).arg(m_plan.steps.size()));
        if (m_plan.ambushAt >= 0)
            lines << tr("Attacked on the way after %1 steps (zone of control)").arg(m_plan.ambushAt + 1);
        lines << (m_route == t ? tr("Double click or Enter: go") : tr("Click: plot the route"));
    }
    if (m_planner.dangerTiles().contains(t)) lines << tr("Too close to a hostile army: it attacks here");
    return lines.join("\n");
}

void MapView::setTileSize(int pixels) {
    const QPoint centre = viewport()->rect().center();
    const QPointF world = m_wantCentre ? *m_wantCentre
                                       : QPointF((centre.x() + horizontalScrollBar()->value()) / double(m_tile),
                                                 (centre.y() + verticalScrollBar()->value()) / double(m_tile));
    m_tile = art::nextZoom(std::clamp(pixels, art::kZoomSteps[0], 4096), 0);
    horizontalScrollBar()->setSingleStep(m_tile);
    verticalScrollBar()->setSingleStep(m_tile);
    updateScrollBars();
    // Keep the same spot in the middle of the view.
    horizontalScrollBar()->setValue(int(world.x() * m_tile) - centre.x());
    verticalScrollBar()->setValue(int(world.y() * m_tile) - centre.y());
    viewport()->update();
    emit zoomChanged(m_tile);
}

void MapView::zoomStep(int direction) {
    setTileSize(art::nextZoom(m_tile, direction));
}

void MapView::updateScrollBars() {
    const QSize content(m_view.width * m_tile, m_view.height * m_tile);
    horizontalScrollBar()->setRange(0, std::max(0, content.width() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(0, std::max(0, content.height() - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void MapView::resizeEvent(QResizeEvent* event) {
    // Keep the same spot in the middle while the window changes size: the spot
    // centerOn asked for (until the player scrolls), else what was in the middle.
    const QSize old = event->oldSize().isValid() ? event->oldSize() : viewport()->size();
    const QPointF world = m_wantCentre ? *m_wantCentre
                                       : QPointF((horizontalScrollBar()->value() + old.width() / 2.0) / m_tile,
                                                 (verticalScrollBar()->value() + old.height() / 2.0) / m_tile);
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    applyCentre(world);
}

void MapView::applyCentre(QPointF world) {
    horizontalScrollBar()->setValue(int(world.x() * m_tile - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(int(world.y() * m_tile - viewport()->height() / 2.0));
}

void MapView::centerOn(core::Pos tile) {
    // Remembered: before the window has its final size the scroll bars cannot
    // reach the spot yet; every resize centres it again until the player scrolls.
    m_wantCentre = QPointF(tile.x + 0.5, tile.y + 0.5);
    updateScrollBars();
    applyCentre(*m_wantCentre);
}

void MapView::showEvent(QShowEvent* event) {
    QAbstractScrollArea::showEvent(event);
    if (m_wantCentre) applyCentre(*m_wantCentre);
}

void MapView::edgeScroll() {
    // The mouse near the edge of the map scrolls it that way.
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    const QRect area = viewport()->rect();
    const int band = std::clamp(area.width() / 25, 12, 40);
    if (!area.contains(pos) || !isActiveWindow()) {
        m_edgeTimer->stop();
        return;
    }
    const int step = std::max(4, m_tile / 3);
    const int dx = pos.x() < band ? -step : pos.x() > area.width() - band ? step : 0;
    const int dy = pos.y() < band ? -step : pos.y() > area.height() - band ? step : 0;
    if (!dx && !dy) {
        m_edgeTimer->stop();
        return;
    }
    m_wantCentre.reset();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
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

    Q_UNUSED(inner);
    // Sprites are drawn big once and scaled to the zoom step (art::sprite).
    const auto put = [&](core::Pos t, const QPixmap& pm) { p.drawPixmap(tileRect(t).topLeft(), pm); };
    for (const ViewMine& mine : m_view.mines)
        if (shown(mine.pos)) put(mine.pos, art::mineSprite(mine.resource, resourceColor(mine.resource), ownerOrNeutral(mine.owner), m_tile));
    for (const ViewDwelling& d : m_view.dwellings)
        if (shown(d.pos)) put(d.pos, art::dwellingSprite(ownerOrNeutral(d.owner), m_tile));
    for (const ViewObject& o : m_view.objects) {
        if (!shown(o.pos)) continue;
        if (o.artifact) put(o.pos, art::artifactSprite(m_tile));
        else put(o.pos, art::chestSprite(o.resources.isEmpty() ? QColor("#d4a017") : resourceColor(o.resources.firstKey()), m_tile));
    }
    // Towns cover 2x2 tiles: the gate (their position) and the tiles right of it and above it.
    for (const ViewTown& t : m_view.towns) {
        if (!shown(t.pos)) continue;
        const QRect gate = tileRect(t.pos);
        p.drawPixmap(QPoint(gate.left(), gate.top() - m_tile), art::townSprite(ownerOrNeutral(t.owner), 2 * m_tile));
        if (m_tile >= 32) {
            QFont f = p.font();
            f.setPixelSize(std::max(10, m_tile / 4));
            f.setBold(true);
            p.setFont(f);
            const QRect label(gate.left() - m_tile / 2, gate.bottom() + 1, 3 * m_tile, f.pixelSize() + 6);
            const int textWidth = QFontMetrics(f).horizontalAdvance(t.name) + 10;
            const QRect plate(label.center().x() - textWidth / 2 + m_tile / 2, label.top(), textWidth, label.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 12, 4, 190));
            p.drawRoundedRect(plate, 4, 4);
            p.setPen(QColor("#f6e7c1"));
            p.drawText(plate, Qt::AlignCenter, t.name);
        }
    }
    // Hero-less armies outside towns: neutral monsters (or garrison-less groups),
    // with a word for how many there are.
    for (const ViewArmy& a : m_view.armies) {
        if (a.hero || !shown(a.pos) || m_view.townAt(a.pos)) continue;
        if (!a.stacks.isEmpty()) {
            const core::UnitDef* u = m_content ? m_content->unit(a.stacks.first().unit) : nullptr;
            const QRect r = tileRect(a.pos);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(QRectF(r).adjusted(m_tile * 0.12, m_tile * 0.72, -m_tile * 0.12, -m_tile * 0.04));
            p.drawPixmap(r.topLeft(), art::unitIcon(a.stacks.first().unit, u ? u->rank : 0, m_tile));
        } else {
            put(a.pos, art::monsterSprite(m_tile));
        }
        if (m_tile >= 32) {
            int count = 0;
            for (const ViewStack& s : a.stacks) count += s.count;
            const QString word = count < 5 ? tr("few") : count < 10 ? tr("several") : count < 20 ? tr("pack")
                                 : count < 50 ? tr("lots") : tr("horde");
            QFont f = p.font();
            f.setPixelSize(std::max(9, m_tile / 5));
            f.setBold(true);
            p.setFont(f);
            const QRect r = tileRect(a.pos);
            const int textWidth = QFontMetrics(f).horizontalAdvance(word) + 8;
            const QRect plate(r.center().x() - textWidth / 2, r.bottom() - f.pixelSize() - 3, textWidth, f.pixelSize() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(110, 20, 20, 220));
            p.drawRoundedRect(plate, 3, 3);
            p.setPen(Qt::white);
            p.drawText(plate, Qt::AlignCenter, word);
        }
    }
    // Where the selected hero can go today, and the planned path under the mouse.
    if (m_view.isMyTurn() && m_planner.ready()) {
        for (const core::Pos t : m_planner.reachableToday())
            if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1) p.fillRect(tileRect(t), QColor(255, 255, 255, 40));
        for (const core::Pos t : m_planner.dangerTiles())
            if (t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1 && m_view.inSight(t)) p.fillRect(tileRect(t), QColor(255, 60, 0, 45));
        if (m_routePlan.found) drawPlan(p, m_routePlan, true);
        if (m_plan.found && !(m_route && m_plan.steps.last() == *m_route)) drawPlan(p, m_plan, false);
    }
    for (const ViewHero& h : m_view.heroes) {
        if (!shown(h.pos)) continue;
        const QRect r = tileRect(h.pos);
        if (h.id == m_selectedHero) {  // a gold ring on the ground under the selected hero
            p.setPen(QPen(QColor("#ffd84a"), std::max(2, m_tile / 14)));
            p.setBrush(QColor(255, 216, 74, 60));
            p.drawEllipse(QRectF(r).adjusted(m_tile * 0.08, m_tile * 0.68, -m_tile * 0.08, -m_tile * 0.02));
        }
        put(h.pos, art::heroSprite(ownerColor(h.owner), h.inBoat, m_tile));
    }
    // A faint grid at the bigger zoom steps, so the squares are easy to see.
    if (m_tile >= 48) {
        p.setPen(QPen(QColor(0, 0, 0, 28), 1));
        for (int x = x0; x <= x1 + 1; ++x) p.drawLine(tileRect({x, y0}).topLeft(), tileRect({x, y1}).bottomLeft());
        for (int y = y0; y <= y1 + 1; ++y) p.drawLine(tileRect({x0, y}).topLeft(), tileRect({x1, y}).topRight());
    }
}

void MapView::drawPlan(QPainter& p, const MovePlan& plan, bool pinned) const {
    // Pinned route: big dots, white today, red later; a preview: small and faint.
    p.save();
    p.setPen(Qt::NoPen);
    const int last = plan.ambushAt >= 0 ? plan.ambushAt : plan.steps.size() - 1;
    for (int i = 0; i < plan.steps.size(); ++i) {
        const QRect r = tileRect(plan.steps[i]);
        int d = std::max(3, m_tile / (pinned ? 3 : 5));
        QColor c = i < plan.affordable ? QColor("#ffffff") : QColor("#ff4040");
        if (!pinned) c.setAlpha(150);
        if (i > last) c.setAlpha(pinned ? 90 : 60);
        if (i == plan.steps.size() - 1 && pinned) {
            // The goal: a ring.
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, std::max(2, m_tile / 10)));
            p.drawEllipse(r.center(), m_tile / 3, m_tile / 3);
            p.setPen(Qt::NoPen);
            continue;
        }
        p.setBrush(c);
        p.drawEllipse(r.center(), d / 2, d / 2);
    }
    if (plan.ambushAt >= 0) {
        // Where the hero would be attacked: an orange cross.
        const QRect r = tileRect(plan.steps[plan.ambushAt]).adjusted(m_tile / 4, m_tile / 4, -m_tile / 4, -m_tile / 4);
        p.setPen(QPen(QColor("#ff9020"), std::max(2, m_tile / 8), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
    }
    p.restore();
}

void MapView::mousePressEvent(QMouseEvent* event) {
    m_pressPos = event->pos();
    event->accept();
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event) {
    event->accept();
    if (event->button() != Qt::LeftButton) return;
    m_doubleClicked = true;  // the release that follows is not another click
    const core::Pos t = tileAt(event->pos());
    if (m_view.inBounds(t)) emit tileDoubleClicked(t);
}

void MapView::mouseReleaseEvent(QMouseEvent* event) {
    if (std::exchange(m_doubleClicked, false)) return;
    if ((event->pos() - m_pressPos).manhattanLength() > 6) return;
    const core::Pos t = tileAt(event->pos());
    if (!m_view.inBounds(t)) return;
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) emit tileShiftClicked(t);
    else if (event->button() == Qt::LeftButton) emit tileClicked(t);
    else if (event->button() == Qt::RightButton) emit tileRightClicked(t);
}

void MapView::wheelEvent(QWheelEvent* event) {
    m_wantCentre.reset();
    if (event->modifiers() & Qt::ControlModifier) {
        zoomStep(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
    } else {
        QAbstractScrollArea::wheelEvent(event);
    }
}

}  // namespace kot::client
