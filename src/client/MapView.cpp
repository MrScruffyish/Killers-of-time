#include "client/MapView.h"

#include <QCursor>
#include <QFontMetrics>
#include <QHash>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QPen>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QTransform>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

#include "client/Art.h"
#include "net/ActionTiming.h"
#include "core/rules/MapSites.h"
#include "core/content/ContentRegistry.h"
#include "core/model/Entities.h"

namespace kot::client {

MapView::MapView(const core::ContentRegistry* content, QWidget* parent)
    : QAbstractScrollArea(parent), m_content(content) {
    // A tile of the view is a terrain id: an index into these (keys name the art).
    if (m_content)
        for (const core::TerrainDef& t : m_content->terrain()) {
            m_terrainKeys.append(t.key);
        }
    viewport()->setMouseTracking(true);
    m_showGrid = QSettings().value(QStringLiteral("map/grid"), true).toBool();
    horizontalScrollBar()->setSingleStep(m_tile);
    verticalScrollBar()->setSingleStep(m_tile);
    m_keyScroll = new QTimer(this);
    m_keyScroll->setInterval(16);
    connect(m_keyScroll, &QTimer::timeout, this, [this]() {
        const double dt = std::min(0.1, m_keyClock.restart() / 1000.0);
        const double speed = std::min(3000.0, 900.0 + 1500.0 * m_keyHeld.elapsed() / 1000.0);  // pixels per second
        const int dx = (m_keyDirections & 2 ? 1 : 0) - (m_keyDirections & 1 ? 1 : 0);
        const int dy = (m_keyDirections & 8 ? 1 : 0) - (m_keyDirections & 4 ? 1 : 0);
        m_keyRest += QPointF(dx, dy) * speed * dt;
        const QPoint step(int(m_keyRest.x()), int(m_keyRest.y()));
        m_keyRest -= step;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + step.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + step.y());
    });
    m_prefetch = new QTimer(this);
    m_prefetch->setSingleShot(true);
    m_prefetch->setInterval(5);
    connect(m_prefetch, &QTimer::timeout, this, &MapView::prefetchChunk);
    m_walkTimer = new QTimer(this);
    m_walkTimer->setInterval(16);
    connect(m_walkTimer, &QTimer::timeout, this, [this]() {
        const double dt = m_walkClock.restart() / 1000.0;
        QList<core::Pos> dirty;  // the ways of the walking heroes (and what they pick up)
        for (const Walk& w : std::as_const(m_walks)) dirty << w.steps;
        for (auto it = m_walks.begin(); it != m_walks.end();) {
            Walk& w = it.value();
            const double perStep = std::min(0.09, 1.2 / std::max<qsizetype>(1, w.steps.size() - 1)) * w.slowness;
            w.progress += dt / perStep;
            if (w.progress >= w.steps.size() - 1) it = m_walks.erase(it);
            else ++it;
        }
        if (m_walks.isEmpty()) m_walkTimer->stop();
        updateTiles(dirty);
    });
    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(30);
    connect(m_edgeTimer, &QTimer::timeout, this, &MapView::edgeScroll);
    // Dragging a scroll bar is the player scrolling: stop keeping a centre.
    for (QScrollBar* bar : {horizontalScrollBar(), verticalScrollBar()})
        connect(bar, &QScrollBar::sliderPressed, this, [this]() { m_wantCentre.reset(); });
}

void MapView::setView(const GameView& view) {
    const int groundBefore = m_groundVersion;
    // Heroes that moved walk there: along the route that was shown for them, else
    // in a straight line (other players' heroes).
    if (m_view.width == view.width && m_view.height == view.height) {
        for (const ViewHero& h : view.heroes) {
            const ViewHero* before = m_view.hero(h.id);
            if (!before || before->pos == h.pos) continue;
            QList<core::Pos> steps{before->pos};
            const MovePlan& route = (h.id == m_selectedHero && m_routePlan.found) ? m_routePlan : m_plan;
            const qsizetype end = route.found ? route.steps.indexOf(h.pos) : -1;
            if (h.id == m_selectedHero && end >= 0) steps.append(route.steps.mid(0, end + 1));
            else steps.append(h.pos);
            if (std::max(std::abs(h.pos.x - before->pos.x), std::abs(h.pos.y - before->pos.y)) > 30) continue;  // a jump
            const WalkSpeed speed = walkSpeed(h.owner == view.you);
            if (speed == WalkSpeed::Instant) continue;
            Walk walk{steps, 0.0, {}, speed == WalkSpeed::Slow ? 2.2 : speed == WalkSpeed::Fast ? 0.45 : 1.0};
            // What the hero picks up at the end stays on the map until he gets there.
            for (const ViewObject& o : m_view.objects) {
                bool still = false;
                for (const ViewObject& n : view.objects) still = still || n.id == o.id;
                if (!still && steps.contains(o.pos)) walk.pickups.append(o);
            }
            m_walks.insert(h.id, walk);
        }
        if (!m_walks.isEmpty() && !m_walkTimer->isActive()) {
            m_walkClock.restart();
            m_walkTimer->start();
        }
    }
    // Ground chunks whose part of the map changed: the terrain known, what is in sight,
    // and where things stand (they hide the decorations under them).
    if (view.width != m_view.width || view.height != m_view.height) {
        m_chunks.clear();
        m_chunkUse.clear();
        m_shadeChunks.clear();
        ++m_groundVersion;
    } else {
        const auto taken = [](const GameView& v) {
            QSet<core::Pos> s;
            for (const ViewTown& t : v.towns)
                for (const core::Pos p2 : core::townTiles(t.pos, v.width, v.height)) s.insert(p2);
            for (const ViewMine& m2 : v.mines) s.insert(m2.pos);
            for (const ViewDwelling& d : v.dwellings) s.insert(d.pos);
            for (const ViewObject& o : v.objects) s.insert(o.pos);
            return s;  // (heroes and monsters move all the time: they just stand on the decorations)
        };
        const QSet<core::Pos> before = taken(m_view), after = taken(view);
        QSet<core::Pos> changed = (before - after) + (after - before);
        for (int y = 0; y < view.height; ++y)
            for (int x = 0; x < view.width; ++x) {
                const int i = y * view.width + x;
                if (view.tiles.value(i) != m_view.tiles.value(i)) changed.insert({x, y});
            }
        dropChunksAround(changed);
        dropChunksAround(changed, &m_shadeChunks);  // newly known tiles are shaded too
        // What is in sight: only the shade chunks around the tiles that changed.
        QSet<core::Pos> sight;
        if (view.visible != m_view.visible)
            for (int i = 0; i < view.width * view.height; ++i)
                if (view.visible.mid(i, 1) != m_view.visible.mid(i, 1)) sight.insert({i % view.width, i / view.width});
        dropChunksAround(sight, &m_shadeChunks);
    }
    // What to repaint: everything when the ground changed (the screen picture is put
    // together again anyway) or the turn changed hands; else only around the things
    // that changed and the selected hero's lit tiles and paths, old and new.
    const bool full = m_groundVersion != groundBefore || view.width != m_view.width || view.height != m_view.height ||
                      view.isMyTurn() != m_view.isMyTurn() || view.you != m_view.you;
    QList<core::Pos> dirty = changedThings(m_view, view);
    const auto overlay = [this, &dirty]() {
        for (const core::Pos t : m_planner.reachableToday()) dirty << t;
        for (const core::Pos t : m_planner.dangerTiles()) dirty << t;
        dirty << m_plan.steps << m_routePlan.steps;
    };
    overlay();
    m_view = view;
    replan();
    overlay();
    updateScrollBars();
    if (full) viewport()->update();
    else updateTiles(dirty);
}

MapView::WalkSpeed MapView::walkSpeed(bool own) {
    const int v = QSettings().value(own ? QStringLiteral("map/ownWalkSpeed") : QStringLiteral("map/otherWalkSpeed"), 1).toInt();
    return WalkSpeed(std::clamp(v, 0, 3));
}

void MapView::setWalkSpeed(bool own, WalkSpeed speed) {
    QSettings().setValue(own ? QStringLiteral("map/ownWalkSpeed") : QStringLiteral("map/otherWalkSpeed"), int(speed));
}

void MapView::setSelectedHero(int heroId) {
    if (heroId == m_selectedHero) return;  // (setView has planned for him already)
    m_selectedHero = heroId;
    replan();
    viewport()->update();
}

void MapView::replan() {
    if (m_content) m_planner.prepare(m_view, *m_content, m_selectedHero);
    m_plan = m_planner.plan(m_hover);
    m_routePlan = m_route ? m_planner.plan(*m_route) : MovePlan();
}

void MapView::showBanner(const QString& title, const QString& subtitle) {
    m_bannerTitle = title;
    m_bannerSub = subtitle;
    m_bannerClock.start();
    if (!m_bannerTimer) {
        m_bannerTimer = new QTimer(this);
        m_bannerTimer->setInterval(33);
        connect(m_bannerTimer, &QTimer::timeout, this, [this]() {
            if (m_bannerClock.elapsed() > 2600) {
                m_bannerTimer->stop();
                m_bannerTitle.clear();
            }
            viewport()->update();
        });
    }
    m_bannerTimer->start();
    viewport()->update();
}

void MapView::setRoute(std::optional<core::Pos> target) {
    if (target == m_route) return;
    QList<core::Pos> dirty = m_routePlan.steps;
    m_route = target;
    m_routePlan = m_route ? m_planner.plan(*m_route) : MovePlan();
    dirty << m_routePlan.steps;
    updateTiles(dirty);
}

void MapView::mouseMoveEvent(QMouseEvent* event) {
    // Holding the left button and dragging moves the map along.
    if (event->buttons() & Qt::LeftButton) {
        if (!m_dragging && (event->pos() - m_pressPos).manhattanLength() > 6) {
            m_dragging = true;
            m_dragLast = m_pressPos;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragging) {
            const QPoint d = event->pos() - m_dragLast;
            m_dragLast = event->pos();
            m_wantCentre.reset();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
            return;
        }
    }
    if (!m_edgeTimer->isActive()) {
        const QRect area = viewport()->rect();
        const int band = std::clamp(area.width() / 25, 12, 40);
        const QPoint at = event->pos();
        if (at.x() < band || at.y() < band || at.x() > area.width() - band || at.y() > area.height() - band)
            m_edgeTimer->start();
    }
    const core::Pos t = tileAt(event->pos());
    if (t == m_hover) return;
    QList<core::Pos> dirty = m_plan.steps;
    dirty << m_hover << t;
    m_hover = t;
    m_plan = m_planner.plan(t);
    dirty << m_plan.steps;
    updatePointer();
    QString info = describeTile(t);
    emit hoverInfo(info.replace('\n', QStringLiteral("  \u00B7  ")));
    updateTiles(dirty);
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
    QList<core::Pos> dirty = m_plan.steps;
    dirty << m_hover;
    m_hover = {-1, -1};
    m_plan = MovePlan();
    updateTiles(dirty);
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
    const core::TerrainDef* terrain = m_content && index >= 0 && index < m_content->terrain().size() ? &m_content->terrain(core::TerrainId(index)) : nullptr;
    QStringList lines;
    lines << (terrain ? terrain->name : m_terrainKeys.value(index));
    const auto ownerName = [this](int owner) {
        const ViewPlayer* p = m_view.player(owner);
        return p ? p->name : tr("neutral");
    };
    const auto unitName = [this](core::UnitTypeId id) {
        const core::UnitDef* u = m_content ? m_content->unit(id) : nullptr;
        return u ? u->name : QString();
    };
    if (const ViewTown* town = m_view.townCovering(t))
        lines << tr("Town %1 (%2)").arg(town->name, ownerName(town->owner))
              << (town->mine ? tr("Right click or Shift+click: open the town") : tr("Walk to its gate to visit or attack it"));
    for (const ViewMine& m : m_view.mines)
        if (m.pos == t) lines << tr("%1 mine (%2)").arg(m_content->resource(m.resource).key, ownerName(m.owner));
    for (const ViewDwelling& d : m_view.dwellings)
        if (d.pos == t) lines << tr("%1 dwelling (%2)").arg(unitName(d.unit), ownerName(d.owner));
    for (const ViewObject& o : m_view.objects) {
        if (o.pos != t) continue;
        if (!o.site.isEmpty()) {
            const bool gift = m_selectedHero ? o.giftFor.contains(m_selectedHero) : !o.giftFor.isEmpty();
            if (!o.quest.isEmpty()) lines << tr("%1: %2").arg(core::siteName(o.site), o.quest);
            else lines << tr("%1: %2 %3").arg(core::siteName(o.site), core::siteDescription(o.site),
                                         gift ? tr("(something for this hero)") : tr("(nothing more for this hero)"));
            continue;
        }
        if (o.artifact) {
            const core::ArtifactDef* a = m_content ? m_content->artifact(o.artifactId) : nullptr;
            lines << tr("Artifact: %1 (%2)").arg(a ? a->name : QString(), a ? a->description : QString());
        } else {
            QStringList res;
            for (const auto [r, amount] : o.resources) res << QStringLiteral("%1 %2").arg(amount).arg(m_content->resource(r).key);
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

QPoint MapView::mapOrigin() const {
    return m_groundArea ? m_groundArea->topLeft() : QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
}

QSize MapView::paintSize() const { return m_groundArea ? m_groundArea->size() : viewport()->size(); }

QRect MapView::tileRect(core::Pos t) const {
    const QPoint o = mapOrigin();
    return QRect(t.x * m_tile - o.x(), t.y * m_tile - o.y(), m_tile, m_tile);
}

QRegion MapView::tilesRegion(const QList<core::Pos>& tiles) const {
    QRegion region;
    for (const core::Pos t : tiles) {
        if (t.x < 0 || t.y < 0) continue;
        region += tileRect(t).adjusted(-2 * m_tile, -2 * m_tile, 2 * m_tile, m_tile);  // (labels can be wide)
    }
    return region;
}

void MapView::updateTiles(const QList<core::Pos>& tiles) {
    const QRegion region = tilesRegion(tiles) & viewport()->rect();
    if (region.isEmpty()) return;
    // A region of many pieces clips slowly: then its bounding box.
    if (region.rectCount() > 24) viewport()->update(region.boundingRect());
    else viewport()->update(region);
}

QList<core::Pos> MapView::changedThings(const GameView& a, const GameView& b) {
    // Each thing drawn on the map, keyed by what it is, with what its drawing depends on.
    const auto things = [](const GameView& v) {
        QHash<QString, std::pair<core::Pos, QByteArray>> out;
        const auto put = [&](const QString& key, core::Pos pos, const QByteArray& look) { out.insert(key, {pos, look}); };
        for (const ViewHero& h : v.heroes)
            put(QStringLiteral("h%1").arg(h.id), h.pos, QByteArray::number(h.owner) + ',' + QByteArray::number(h.inBoat) + ',' + QByteArray::number(h.classId.index()));
        for (const ViewArmy& a : v.armies) {
            QByteArray look = QByteArray::number(a.owner) + ',' + QByteArray::number(a.hero);
            for (const ViewStack& s : a.stacks) look += ';' + QByteArray::number(s.unit.index()) + ':' + QByteArray::number(s.count);
            put(QStringLiteral("a%1").arg(a.id), a.pos, look);
        }
        for (const ViewTown& t : v.towns)
            put(QStringLiteral("t%1").arg(t.id), t.pos, QByteArray::number(t.owner) + ',' + QByteArray::number(t.walls) + ',' + QByteArray::number(t.size) + ',' + t.name.toUtf8());
        for (const ViewMine& m : v.mines) put(QStringLiteral("m%1").arg(m.id), m.pos, QByteArray::number(m.owner));
        for (const ViewDwelling& d : v.dwellings) put(QStringLiteral("d%1").arg(d.id), d.pos, QByteArray::number(d.owner));
        for (const ViewObject& o : v.objects)
            put(QStringLiteral("o%1").arg(o.id), o.pos, o.site.toUtf8() + ',' + QByteArray::number(o.artifact) + QByteArray::number(o.artifactId.index()) + ',' + QByteArray::number(o.giftFor.size()) + ',' + QByteArray::number(o.questDone));
        return out;
    };
    const auto before = things(a), after = things(b);
    QList<core::Pos> tiles;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        const auto n = after.constFind(it.key());
        if (n == after.cend() || n->first != it->first || n->second != it->second) {
            tiles << it->first;
            if (n != after.cend()) tiles << n->first;
        }
    }
    for (auto it = after.cbegin(); it != after.cend(); ++it)
        if (!before.contains(it.key())) tiles << it->first;
    return tiles;
}

void MapView::dropChunksAround(const QSet<core::Pos>& tiles, QHash<quint32, QPixmap>* shade) {
    if (tiles.isEmpty() || (shade ? shade->isEmpty() : m_chunks.isEmpty())) return;
    // A tile's look reaches into its neighbours (edges, coasts, trees, the soft shade).
    QSet<quint32> drop;
    for (const core::Pos t : tiles) {
        const QRect r((t.x - 1) * m_tile, (t.y - 1) * m_tile, 3 * m_tile, 3 * m_tile);
        for (int cy = std::max(0, r.top()) / kChunk; cy <= r.bottom() / kChunk; ++cy)
            for (int cx = std::max(0, r.left()) / kChunk; cx <= r.right() / kChunk; ++cx) drop.insert(quint32(cy) << 16 | quint32(cx));
    }
    for (const quint32 k : drop) {
        if (shade) {
            shade->remove(k);
            continue;
        }
        m_chunks.remove(k);
        m_chunkUse.removeOne(k);
    }
    ++m_groundVersion;
}

const QPixmap& MapView::shadeChunk(int cx, int cy, qreal dpr) {
    const quint32 key = quint32(cy) << 16 | quint32(cx);
    auto it = m_shadeChunks.find(key);
    if (it != m_shadeChunks.end()) return it.value();
    if (m_shadeChunks.size() > 2000) m_shadeChunks.clear();  // a big map seen at a small zoom: start over
    // Nothing out of sight in it (the usual case near the heroes): kept as an empty picture.
    const int tx0 = std::max(0, cx * kChunk / m_tile - 1), ty0 = std::max(0, cy * kChunk / m_tile - 1);
    const int tx1 = std::min(m_view.width - 1, ((cx + 1) * kChunk) / m_tile + 1), ty1 = std::min(m_view.height - 1, ((cy + 1) * kChunk) / m_tile + 1);
    bool any = false;
    for (int y = ty0; y <= ty1 && !any; ++y)
        for (int x = tx0; x <= tx1 && !any; ++x) any = m_view.tiles[y * m_view.width + x] >= 0 && !m_view.inSight({x, y});
    if (!any) return m_shadeChunks.insert(key, QPixmap()).value();
    QPixmap pm(QSize(kChunk, kChunk) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        m_groundArea = QRect(cx * kChunk, cy * kChunk, kChunk, kChunk);
        paintShade(p);
        m_groundArea.reset();
    }
    return m_shadeChunks.insert(key, pm).value();
}

void MapView::prefetchChunk() {
    // The first chunk not painted yet in a ring of one chunk around the screen.
    if (m_view.width == 0 || m_chunkKey.isEmpty()) return;
    const int hx = horizontalScrollBar()->value(), vy = verticalScrollBar()->value();
    const int cx0 = std::max(0, hx / kChunk - 1), cy0 = std::max(0, vy / kChunk - 1);
    const int cx1 = std::min((m_view.width * m_tile - 1) / kChunk, (hx + viewport()->width()) / kChunk + 1);
    const int cy1 = std::min((m_view.height * m_tile - 1) / kChunk, (vy + viewport()->height()) / kChunk + 1);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            if (!m_chunks.contains(quint32(cy) << 16 | quint32(cx))) {
                chunk(cx, cy, viewport()->devicePixelRatioF());
                m_prefetch->start();  // more next time the event loop is idle
                return;
            }
}

const QPixmap& MapView::chunk(int cx, int cy, qreal dpr) {
    const quint32 key = quint32(cy) << 16 | quint32(cx);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        m_chunkUse.removeOne(key);
        m_chunkUse.append(key);
        return it.value();
    }
    constexpr int kMaxChunks = 600;  // ~150 MB at 256x256
    while (m_chunkUse.size() >= kMaxChunks) m_chunks.remove(m_chunkUse.takeFirst());
    QPixmap pm(QSize(kChunk, kChunk) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        const qint64 t0 = net::timing::now();
        QPainter p(&pm);
        m_groundArea = QRect(cx * kChunk, cy * kChunk, kChunk, kChunk);
        paintGround(p);
        m_groundArea.reset();
        p.end();
        net::timing::add("chunk", net::timing::now() - t0);
    }
    m_chunkUse.append(key);
    return m_chunks.insert(key, pm).value();
}

QColor MapView::ownerColor(int owner) const {
    const ViewPlayer* p = m_view.player(owner);
    return p ? QColor(p->color) : QColor("#999999");  // neutral
}

void MapView::paintEvent(QPaintEvent*) {
    net::timing::paintStarted();
    QPainter screen(viewport());
    if (m_view.width == 0) {
        paintFog(screen);
        return;
    }
    // The unknown's clouds (they drift slower than the map), then the ground from its
    // chunks (painted once, see setView), then what stands on it.
    const qreal dpr = viewport()->devicePixelRatioF();
    const QString key = QStringLiteral("%1|%2|%3").arg(m_showGrid).arg(m_tile).arg(dpr);
    if (key != m_chunkKey) {
        m_chunks.clear();
        m_chunkUse.clear();
        m_shadeChunks.clear();
        m_chunkKey = key;
        ++m_groundVersion;
    }
    const int hx = horizontalScrollBar()->value(), vy = verticalScrollBar()->value();
    const QString screenKey = QStringLiteral("%1|%2,%3|%4x%5").arg(m_groundVersion).arg(hx).arg(vy).arg(viewport()->width()).arg(viewport()->height());
    if (screenKey != m_screenKey) {
        m_screenGround = QPixmap(viewport()->size() * dpr);
        m_screenGround.setDevicePixelRatio(dpr);
        QPainter gp(&m_screenGround);
        {
            const net::timing::Scope t("map fog");
            paintFog(gp);
        }
        const net::timing::Scope t("map blocks");
        const int mapW = m_view.width * m_tile, mapH = m_view.height * m_tile;
        for (int cy = vy / kChunk; cy * kChunk < std::min(mapH, vy + viewport()->height()); ++cy)
            for (int cx = hx / kChunk; cx * kChunk < std::min(mapW, hx + viewport()->width()); ++cx)
                gp.drawPixmap(cx * kChunk - hx, cy * kChunk - vy, chunk(cx, cy, dpr));
        const net::timing::Scope ts("map shade");
        for (int cy = vy / kChunk; cy * kChunk < std::min(mapH, vy + viewport()->height()); ++cy)
            for (int cx = hx / kChunk; cx * kChunk < std::min(mapW, hx + viewport()->width()); ++cx)
                if (const QPixmap& shade = shadeChunk(cx, cy, dpr); !shade.isNull()) gp.drawPixmap(cx * kChunk - hx, cy * kChunk - vy, shade);
        m_screenKey = screenKey;
        m_prefetch->start();
    }
    screen.drawPixmap(0, 0, m_screenGround);
    const qint64 t0 = net::timing::now();
    paintObjects(screen);
    net::timing::add("objects", net::timing::now() - t0);
    net::timing::painted();
}

void MapView::paintFog(QPainter& p) {
    // The unknown: dark clouds that drift with the map (slower: they are far below).
    QBrush fog(art::fogTexture());
    fog.setTransform(QTransform::fromTranslate(-horizontalScrollBar()->value() * 0.6, -verticalScrollBar()->value() * 0.6));
    p.fillRect(viewport()->rect(), fog);
}

void MapView::paintShade(QPainter& p) {
    // Over the ground chunks, in its own chunks (shadeChunk): what is in sight changes
    // with every step, the ground does not.
    if (m_view.width == 0) return;
    const auto terrainAt = [&](int x, int y) { return m_view.inBounds({x, y}) ? m_view.tiles[y * m_view.width + x] : -1; };
    const QPoint origin0 = mapOrigin();
    const int hx = origin0.x(), vy = origin0.y();
    const int x0 = hx / m_tile, y0 = vy / m_tile;
    const int x1 = std::min(m_view.width - 1, (hx + paintSize().width()) / m_tile);
    const int y1 = std::min(m_view.height - 1, (vy + paintSize().height()) / m_tile);
    // Out of sight: dimmed, with soft edges (one pixel per tile, scaled up smoothly;
    // pixel centres sit on tile centres).
    {
        const int gx0 = x0 - 1, gy0 = y0 - 1, gw = x1 - x0 + 3, gh = y1 - y0 + 3;
        QImage dim(gw, gh, QImage::Format_ARGB32_Premultiplied);
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx) {
                const core::Pos t{gx0 + gx, gy0 + gy};
                const core::Pos c{std::clamp(t.x, 0, m_view.width - 1), std::clamp(t.y, 0, m_view.height - 1)};
                const bool dark = terrainAt(c.x, c.y) >= 0 && !m_view.inSight(c);
                dim.setPixel(gx, gy, dark ? qPremultiply(qRgba(10, 8, 20, 140)) : 0);
            }
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRect origin = tileRect({gx0, gy0});
        p.drawImage(QRectF(origin.left() + m_tile / 2.0, origin.top() + m_tile / 2.0, (gw - 1) * double(m_tile), (gh - 1) * double(m_tile)),
                    dim, QRectF(0.5, 0.5, gw - 1, gh - 1));
        p.restore();
    }
}

void MapView::paintGround(QPainter& p) {
    // The part of the map in mapOrigin() / paintSize() (the screen, or a chunk); where
    // nothing is known it stays transparent (the fog shows through).
    if (m_view.width == 0) return;
    const QPoint origin = mapOrigin();
    const int hx = origin.x(), vy = origin.y();
    const int x0 = hx / m_tile, y0 = vy / m_tile;
    const int x1 = std::min(m_view.width - 1, (hx + paintSize().width()) / m_tile);
    const int y1 = std::min(m_view.height - 1, (vy + paintSize().height()) / m_tile);

    // Terrain (textured tiles) and fog: remembered tiles dimmed, unexplored stays black
    // with a soft edge.
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int index = m_view.tiles[y * m_view.width + x];
            if (index < 0) continue;
            const QRect r = tileRect({x, y});
            p.drawPixmap(r.topLeft(), art::terrainTile(m_terrainKeys.value(index), m_tile, art::variantFor(x, y)));
        }
    }
    // Small decorations on free tiles (not under objects).
    if (m_tile >= 24) {
        QSet<core::Pos> taken;
        for (const ViewTown& t : m_view.towns)
            for (const core::Pos p2 : core::townTiles(t.pos, m_view.width, m_view.height)) taken.insert(p2);
        for (const ViewMine& m2 : m_view.mines) taken.insert(m2.pos);
        for (const ViewDwelling& d : m_view.dwellings) taken.insert(d.pos);
        for (const ViewObject& o : m_view.objects) taken.insert(o.pos);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int index = m_view.tiles[y * m_view.width + x];
                const int variant = art::decorationAt(x, y);
                if (index < 0 || variant < 0 || taken.contains({x, y})) continue;
                const QPixmap pm = art::decoration(m_terrainKeys.value(index), variant, m_tile);
                if (!pm.isNull()) p.drawPixmap(tileRect({x, y}).topLeft(), pm);
            }
    }
    // Soft edges where terrains meet (the higher layer spreads over the lower one),
    // surf where water meets land; then the fog over remembered tiles.
    const auto terrainAt = [&](int x, int y) {
        return m_view.inBounds({x, y}) ? m_view.tiles[y * m_view.width + x] : -1;
    };
    const auto isWater = [&](int index) {
        const core::TerrainDef* t = m_content && index >= 0 && index < m_content->terrain().size() ? &m_content->terrain(core::TerrainId(index)) : nullptr;
        return t && t->sailable;
    };
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int here = terrainAt(x, y);
            if (here < 0 || m_tile < 12) continue;
            const QString hereId = m_terrainKeys.value(here);
            const QRect r = tileRect({x, y});
            static const int dx[] = {-1, 0, 1, 0}, dy[] = {0, -1, 0, 1};
            for (int side = 0; side < 4; ++side) {
                const int there = terrainAt(x + dx[side], y + dy[side]);
                if (there < 0 || there == here) continue;
                const QString thereId = m_terrainKeys.value(there);
                if (isWater(here) && !isWater(there)) {
                    // the coast is drawn below
                } else if (art::terrainLayer(thereId) > art::terrainLayer(hereId) && isWater(there) == isWater(here)) {
                    p.drawPixmap(r.topLeft(), art::terrainEdge(thereId, side, m_tile));
                }
            }
        }
    // Coasts: the land reaches into its water neighbours with a wavy beach and foam.
    if (m_tile >= 12)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int here = terrainAt(x, y);
                if (here < 0 || !isWater(here)) continue;
                static const int ddx[] = {-1, 0, 1, 0, -1, 1, 1, -1}, ddy[] = {0, -1, 0, 1, -1, -1, 1, 1};
                int mask = 0;
                int landAt[8];
                for (int d = 0; d < 8; ++d) {
                    landAt[d] = terrainAt(x + ddx[d], y + ddy[d]);
                    if (landAt[d] >= 0 && !isWater(landAt[d])) mask |= 1 << d;
                }
                if (!mask) continue;
                const QRect r = tileRect({x, y});
                const int variant = art::variantFor(x, y);
                p.drawPixmap(r.topLeft(), art::shoreUnder(mask, variant, m_tile));
                for (int d = 0; d < 8; ++d) {
                    if (!(mask & (1 << d))) continue;
                    p.drawPixmap(r.topLeft(), art::shoreLand(m_terrainKeys.value(landAt[d]), art::variantFor(x + ddx[d], y + ddy[d]), d, variant, m_tile));
                }
            }
    // Trees, hills and peaks stand up out of their tiles, back rows first (one
    // row below the screen too, since they reach upwards); never over the unknown.
    QRegion known;
    for (int y = y0; y <= y1; ++y) {
        int run = -1;
        for (int x = x0; x <= x1 + 1; ++x) {
            const bool k = x <= x1 && terrainAt(x, y) >= 0;
            if (k && run < 0) run = x;
            if (!k && run >= 0) {
                known += QRect(tileRect({run, y}).topLeft(), QSize((x - run) * m_tile, m_tile));
                run = -1;
            }
        }
    }
    p.save();
    p.setClipRegion(known, Qt::IntersectClip);
    for (int y = y0; y <= std::min(m_view.height - 1, y1 + 1); ++y)
        for (int x = std::max(0, x0 - 1); x <= std::min(m_view.width - 1, x1 + 1); ++x) {
            const int index = terrainAt(x, y);
            if (index < 0) continue;
            const QPixmap pm = art::terrainFeature(m_terrainKeys.value(index), m_tile, art::variantFor(x, y));
            if (pm.isNull()) continue;
            const QRect r = tileRect({x, y});
            p.drawPixmap(r.left() - m_tile / 2, r.top() - m_tile, pm);
        }
    p.restore();
    // The unknown's soft edges reach into the neighbours: one tile around too.
    for (int y = std::max(0, y0 - 1); y <= std::min(m_view.height - 1, y1 + 1); ++y)
        for (int x = std::max(0, x0 - 1); x <= std::min(m_view.width - 1, x1 + 1); ++x) {
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
    // A faint grid at the bigger zoom steps, so the squares are easy to see (sharp
    // one-pixel lines: no antialiasing).
    if (m_tile >= 48 && m_showGrid) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(0, 0, 0, 28), 1));
        for (int x = x0; x <= x1 + 1; ++x) p.drawLine(tileRect({x, y0}).topLeft(), tileRect({x, y1}).bottomLeft());
        for (int y = y0; y <= y1 + 1; ++y) p.drawLine(tileRect({x0, y}).topLeft(), tileRect({x1, y}).topRight());
        p.restore();
    }
}

void MapView::paintObjects(QPainter& p) {
    const int hx = horizontalScrollBar()->value(), vy = verticalScrollBar()->value();
    const int x0 = hx / m_tile, y0 = vy / m_tile;
    const int x1 = std::min(m_view.width - 1, (hx + viewport()->width()) / m_tile);
    const int y1 = std::min(m_view.height - 1, (vy + viewport()->height()) / m_tile);
    // Things off the screen are skipped (towns and figures reach up to two tiles out).
    const auto onScreen = [&](core::Pos t) { return t.x >= x0 - 2 && t.x <= x1 + 2 && t.y >= y0 - 1 && t.y <= y1 + 2; };

    const auto shown = [&](core::Pos t) { return m_view.known(t); };
    p.setRenderHint(QPainter::Antialiasing, true);
    const double m = std::max(1, m_tile / 10);  // margin inside a tile
    const auto inner = [&](core::Pos t) { return QRectF(tileRect(t)).adjusted(m, m, -m, -m); };
    const auto ownerOrNeutral = [&](int owner) { return owner ? ownerColor(owner) : QColor("#9a9a9a"); };

    Q_UNUSED(inner);
    // Who owns it, at a glance: a ring on the ground in the owner's colour under
    // everything owned (heroes, towns, mines, dwellings); neutral things have none.
    const auto ownerRing = [&](const QRectF& foot, int owner) {
        if (!owner) return;
        const QColor c = ownerColor(owner);
        const double w = std::max(2.0, m_tile / 12.0);
        p.setPen(QPen(QColor(0, 0, 0, 150), w + 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(foot);
        QColor fill = c;
        fill.setAlpha(95);
        p.setPen(QPen(c.lighter(115), w));
        p.setBrush(fill);
        p.drawEllipse(foot);
    };
    const auto tileFoot = [&](core::Pos t) { return QRectF(tileRect(t)).adjusted(m_tile * 0.04, m_tile * 0.6, -m_tile * 0.04, -m_tile * 0.01); };
    // Sprites are drawn big once and scaled to the zoom step (art::sprite).
    const auto put = [&](core::Pos t, const QPixmap& pm) { p.drawPixmap(tileRect(t).topLeft(), pm); };
    for (const ViewMine& mine : m_view.mines)
        if (shown(mine.pos) && onScreen(mine.pos)) {
            ownerRing(tileFoot(mine.pos), mine.owner);
            const QString& key = m_content->resource(mine.resource).key;
            put(mine.pos, art::mineSprite(key, ownerOrNeutral(mine.owner), m_tile));
        }
    for (const ViewDwelling& d : m_view.dwellings)
        if (shown(d.pos) && onScreen(d.pos)) {
            ownerRing(tileFoot(d.pos), d.owner);
            put(d.pos, art::dwellingSprite(ownerOrNeutral(d.owner), d.unit, m_tile));
        }
    QList<ViewObject> objects = m_view.objects;
    for (const Walk& w : m_walks) objects.append(w.pickups);  // not reached yet on screen
    for (const ViewObject& o : objects) {
        if (!shown(o.pos) || !onScreen(o.pos)) continue;
        if (!o.site.isEmpty()) {
            // A quest hut glows until its wish is granted; the others while they have something for the hero.
            const bool glow = o.site == QLatin1String("quest_hut") ? !o.questDone
                              : m_selectedHero ? o.giftFor.contains(m_selectedHero) : !o.giftFor.isEmpty();
            put(o.pos, art::siteSprite(o.site, glow, m_tile));
        }
        else if (o.artifact) put(o.pos, art::artifactSprite(m_tile));
        else put(o.pos, art::chestSprite(o.resources.isEmpty() ? QString() : m_content->resource((*o.resources.begin()).first).key, m_tile));
    }
    // Towns cover 2x2 tiles: the gate (their position) and the tiles right of it and above it.
    for (const ViewTown& t : m_view.towns) {
        if (!shown(t.pos) || !onScreen(t.pos)) continue;
        const QRect gate = tileRect(t.pos);
        ownerRing(QRectF(gate.left() - m_tile * 0.02, gate.top() + m_tile * 0.45, 2.04 * m_tile, m_tile * 0.6), t.owner);
        p.drawPixmap(QPoint(gate.left(), gate.top() - m_tile),
                     art::townSprite(art::TownLook{ownerOrNeutral(t.owner), art::raceKey(t.race), t.walls, t.size}, 2 * m_tile));
        if (m_tile >= 32) {
            QFont f = p.font();
            f.setPixelSize(std::max(10, m_tile / 4));
            f.setBold(true);
            p.setFont(f);
            const QRect label(gate.left() - m_tile / 2, gate.bottom() + 1, 3 * m_tile, f.pixelSize() + 6);
            const int textWidth = QFontMetrics(f).horizontalAdvance(t.name) + 10;
            const QRect plate(label.center().x() - textWidth / 2 + m_tile / 2, label.top(), textWidth, label.height());
            // The name plate in the owner's colour (dark for neutral towns).
            const QColor own = t.owner ? ownerColor(t.owner) : QColor(20, 12, 4);
            QColor fill = t.owner ? own.darker(170) : QColor(20, 12, 4);
            fill.setAlpha(220);
            p.setPen(QPen(t.owner ? own.lighter(120) : QColor(120, 110, 90), 2));
            p.setBrush(fill);
            p.drawRoundedRect(plate, 4, 4);
            p.setPen(QColor("#f6e7c1"));
            p.drawText(plate, Qt::AlignCenter, t.name);
        }
    }
    // Hero-less armies outside towns: neutral monsters (or garrison-less groups),
    // with a word for how many there are.
    for (const ViewArmy& a : m_view.armies) {
        if (a.hero || !shown(a.pos) || !onScreen(a.pos) || m_view.townAt(a.pos)) continue;
        if (!a.stacks.isEmpty()) {
            // A group: more figures for more monsters (1 few .. 5 a horde), back
            // ones smaller and higher, so the size shows at a glance.
            int total = 0;
            for (const ViewStack& s : a.stacks) total += s.count;
            const int figures = total < 5 ? 1 : total < 10 ? 2 : total < 20 ? 3 : total < 50 ? 4 : 5;
            const QRect r = tileRect(a.pos);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(QRectF(r).adjusted(m_tile * 0.06, m_tile * 0.7, -m_tile * 0.06, -m_tile * 0.02));
            // Places in the tile (x, y of the feet, size), back to front.
            static const double spots[5][5][3] = {
                {{0.5, 1.0, 1.0}},
                {{0.34, 0.9, 0.78}, {0.64, 1.0, 0.86}},
                {{0.5, 0.82, 0.7}, {0.28, 0.98, 0.76}, {0.72, 1.0, 0.78}},
                {{0.32, 0.8, 0.64}, {0.68, 0.8, 0.64}, {0.24, 0.99, 0.72}, {0.7, 1.0, 0.74}},
                {{0.5, 0.74, 0.58}, {0.24, 0.84, 0.62}, {0.76, 0.84, 0.62}, {0.36, 1.0, 0.7}, {0.66, 1.0, 0.7}}};
            for (int i = 0; i < figures; ++i) {
                // The army's stacks in turn, the strongest in front.
                const ViewStack& st = a.stacks[(figures - 1 - i) % a.stacks.size()];
                const core::UnitDef* u = m_content ? m_content->unit(st.unit) : nullptr;
                const int size = std::max(8, int(m_tile * spots[figures - 1][i][2]));
                const QPointF feet(r.left() + m_tile * spots[figures - 1][i][0], r.top() + m_tile * spots[figures - 1][i][1]);
                p.drawPixmap(QPointF(feet.x() - size / 2.0, feet.y() - size), art::unitIcon(st.unit, u ? u->rank : 0, size));
            }
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
        if (!m_walks.contains(h.id)) ownerRing(tileFoot(h.pos), h.owner);
        if (h.id == m_selectedHero && !m_walks.contains(h.id)) {  // a gold ring on the ground under the selected hero
            p.setPen(QPen(QColor("#ffd84a"), std::max(2, m_tile / 14)));
            p.setBrush(QColor(255, 216, 74, 60));
            p.drawEllipse(QRectF(r).adjusted(m_tile * 0.08, m_tile * 0.68, -m_tile * 0.08, -m_tile * 0.02));
        }
        // Walking heroes are drawn between the tiles of their way.
        const auto walk = m_walks.constFind(h.id);
        if (walk != m_walks.cend()) {
            const int i = std::min(int(walk->progress), int(walk->steps.size()) - 2);
            const double f = walk->progress - i;
            const QPointF a = tileRect(walk->steps[i]).topLeft(), b = tileRect(walk->steps[i + 1]).topLeft();
            p.drawPixmap(a + (b - a) * f, art::heroSprite(ownerColor(h.owner), h.inBoat, m_tile, h.classId));
            continue;
        }
        put(h.pos, art::heroSprite(ownerColor(h.owner), h.inBoat, m_tile, h.classId));
    }
    // The day banner: in, hold, fade out.
    if (!m_bannerTitle.isEmpty()) {
        const double t = m_bannerClock.elapsed() / 1000.0;
        const double alpha = t < 0.3 ? t / 0.3 : t > 1.8 ? std::max(0.0, 1 - (t - 1.8) / 0.8) : 1.0;
        const QRectF band(0, viewport()->height() * 0.12, viewport()->width(), 90);
        QLinearGradient g(band.topLeft(), band.topRight());
        g.setColorAt(0, QColor(20, 12, 4, 0));
        g.setColorAt(0.25, QColor(20, 12, 4, int(190 * alpha)));
        g.setColorAt(0.75, QColor(20, 12, 4, int(190 * alpha)));
        g.setColorAt(1, QColor(20, 12, 4, 0));
        p.fillRect(band, g);
        QFont f = p.font();
        f.setPixelSize(40);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 226, 122, int(255 * alpha)));
        p.drawText(band.adjusted(0, 4, 0, -34), Qt::AlignCenter, m_bannerTitle);
        f.setPixelSize(16);
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(240, 225, 195, int(230 * alpha)));
        p.drawText(band.adjusted(0, 52, 0, 0), Qt::AlignHCenter | Qt::AlignTop, m_bannerSub);
    }
}

void MapView::setShowGrid(bool on) {
    m_showGrid = on;
    QSettings().setValue(QStringLiteral("map/grid"), on);
    viewport()->update();
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
    m_dragging = false;
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
    if (std::exchange(m_dragging, false)) {  // that was a drag, not a click
        updatePointer();
        return;
    }
    if (std::exchange(m_doubleClicked, false)) return;
    if ((event->pos() - m_pressPos).manhattanLength() > 6) return;
    const core::Pos t = tileAt(event->pos());
    if (!m_view.inBounds(t)) return;
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) emit tileShiftClicked(t);
    else if (event->button() == Qt::LeftButton) emit tileClicked(t);
    else if (event->button() == Qt::RightButton) emit tileRightClicked(t);
}

namespace {
int arrowBit(int key) {
    switch (key) {
    case Qt::Key_Left: return 1;
    case Qt::Key_Right: return 2;
    case Qt::Key_Up: return 4;
    case Qt::Key_Down: return 8;
    default: return 0;
    }
}
}  // namespace

void MapView::keyPressEvent(QKeyEvent* event) {
    const int bit = arrowBit(event->key());
    if (!bit || (event->modifiers() & ~Qt::KeypadModifier)) return QAbstractScrollArea::keyPressEvent(event);
    event->accept();
    if (event->isAutoRepeat()) return;
    if (!m_keyDirections) {
        // A tap moves one tile at once; held, the map glides on.
        const int dx = bit == 2 ? 1 : bit == 1 ? -1 : 0, dy = bit == 8 ? 1 : bit == 4 ? -1 : 0;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx * m_tile);
        verticalScrollBar()->setValue(verticalScrollBar()->value() + dy * m_tile);
        m_keyHeld.start();
        m_keyClock.start();
        m_keyRest = {};
        m_keyScroll->start();
    }
    m_keyDirections |= bit;
}

void MapView::keyReleaseEvent(QKeyEvent* event) {
    const int bit = arrowBit(event->key());
    if (!bit) return QAbstractScrollArea::keyReleaseEvent(event);
    event->accept();
    if (event->isAutoRepeat()) return;
    m_keyDirections &= ~bit;
    if (!m_keyDirections) m_keyScroll->stop();
}

void MapView::focusOutEvent(QFocusEvent* event) {
    m_keyDirections = 0;
    m_keyScroll->stop();
    QAbstractScrollArea::focusOutEvent(event);
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
