// kot_viewbench: how fast is the adventure map drawn? The client's MapView (a widget
// that paints everything itself, with the ground cached in one picture) against a
// QGraphicsView scene (one item per tile and per thing on the map), on the same
// 128x128 game with the whole map explored, in a real window.
//
//   kot_viewbench [--data dir] [--size n] [--frames n] [--tile px] [--gl]
//
// Per zoom step: scrolling (a new part of the map every frame) and a still map with
// one sprite moving (a hero walking). Times are per frame, painting included
// (paint: the paint itself; frame: from the change until painted, window system included). --gl draws the scene with OpenGL (QOpenGLWidget).

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QOpenGLWidget>
#include <QWindow>
#include <QScrollBar>
#include <algorithm>
#include <cstdio>

#include "client/Art.h"
#include "client/MapView.h"
#include "client/ViewModel.h"
#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/model/PlayerView.h"
#include "core/rules/GameSetup.h"

using namespace kot;

namespace {

struct Stats {
    double mean = 0, median = 0, p95 = 0;
};
Stats stats(QList<double> ms) {
    Stats s;
    if (ms.isEmpty()) return s;
    std::sort(ms.begin(), ms.end());
    for (const double m : ms) s.mean += m;
    s.mean /= ms.size();
    s.median = ms[ms.size() / 2];
    s.p95 = ms[std::min<qsizetype>(ms.size() - 1, ms.size() * 95 / 100)];
    return s;
}

// Watches a viewport's paint events: how long each paint takes (it repaints the
// widget itself, between taking the event and handing it on).
class PaintProbe : public QObject {
public:
    explicit PaintProbe(QWidget* viewport) : m_viewport(viewport) { viewport->installEventFilter(this); }
    int paints = 0;
    double lastMs = 0;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj != m_viewport || event->type() != QEvent::Paint || m_inside) return false;
        m_inside = true;
        QElapsedTimer clock;
        clock.start();
        QCoreApplication::sendEvent(obj, event);  // the widget paints
        lastMs = clock.nsecsElapsed() / 1e6;
        m_inside = false;
        ++paints;
        return true;
    }

private:
    QWidget* m_viewport;
    bool m_inside = false;
};

// Waits until the window is on the screen (the window system shows it).
void waitExposed(QWidget* w) {
    QElapsedTimer clock;
    clock.start();
    while ((!w->windowHandle() || !w->windowHandle()->isExposed()) && clock.elapsed() < 5000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    while (clock.elapsed() < 300) QApplication::processEvents(QEventLoop::AllEvents, 20);  // settle
}

struct Times {
    QList<double> paint, frame;
};
// Times `frames` frames: `step(i)` changes something (a scroll, a moved sprite), then
// the widget repaints what it thinks changed, as in the game (its own update calls).
// paint: the paint itself (CPU work); frame: from the change until the paint is done
// (includes waiting for the window system, e.g. the screen's refresh).
template <typename Step>
Times timeFrames(QAbstractScrollArea* area, int frames, Step step) {
    PaintProbe probe(area->viewport());
    Times t;
    QElapsedTimer clock;
    for (int i = 0; i < frames; ++i) {
        const int before = probe.paints;
        clock.start();
        step(i);
        while (probe.paints == before && clock.elapsed() < 2000) QApplication::processEvents(QEventLoop::AllEvents, 5);
        if (probe.paints == before) {
            std::printf("  (no paint within 2 s: is the window on the screen?)\n");
            break;
        }
        t.frame << clock.nsecsElapsed() / 1e6;
        t.paint << probe.lastMs;
    }
    return t;
}

void print(const char* what, int tile, const char* test, const Times& t) {
    const Stats p = stats(t.paint), f = stats(t.frame);
    std::printf("%-11s tile %2d  %-6s  paint mean %6.2f  median %6.2f  p95 %6.2f ms   frame median %6.2f  p95 %6.2f ms\n", what, tile, test,
                p.mean, p.median, p.p95, f.median, f.p95);
    std::fflush(stdout);
}

// The scroll position of frame i: diagonally over the map and back.
QPoint scrollAt(QAbstractScrollArea* area, int i, int frames) {
    const int hx = area->horizontalScrollBar()->maximum(), vy = area->verticalScrollBar()->maximum();
    const double t = (i % frames) / double(frames);
    const double f = t < 0.5 ? 2 * t : 2 * (1 - t);
    return QPoint(int(hx * f), int(vy * f));
}

// The map as a QGraphicsScene at one tile size: what MapView draws (terrain, edges
// between terrains, coasts, decorations, trees/hills/peaks, the out-of-sight shade,
// map objects, heroes), minus the grid, owner rings and labels.
QGraphicsScene* buildScene(const client::GameView& v, const core::ContentRegistry& content, int tile, QGraphicsPixmapItem** walker) {
    namespace art = client::art;
    auto* scene = new QGraphicsScene;
    scene->setSceneRect(0, 0, v.width * tile, v.height * tile);
    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    const auto add = [&](const QPixmap& pm, double x, double y, double z) {
        auto* item = scene->addPixmap(pm);
        item->setPos(x, y);
        item->setZValue(z);
        return item;
    };
    for (int y = 0; y < v.height; ++y)
        for (int x = 0; x < v.width; ++x) {
            const int index = v.tiles.value(y * v.width + x, -1);
            if (index < 0) continue;
            const QString key = content.terrain(core::TerrainId(index)).key;
            add(art::terrainTile(key, tile, art::variantFor(x, y)), x * tile, y * tile, 0);
            if (const int d = art::decorationAt(x, y); d >= 0) {
                const QPixmap& pm = art::decoration(key, d, tile);
                if (!pm.isNull()) add(pm, x * tile, y * tile, 1);
            }
            const QPixmap& feature = art::terrainFeature(key, tile, art::variantFor(x, y));
            if (!feature.isNull()) add(feature, x * tile - tile / 2.0, y * tile - tile, 2 + y * 0.001);
        }
    // Edges between terrains and coasts, as MapView draws them.
    const auto terrainAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < v.width && y < v.height ? v.tiles.value(y * v.width + x, -1) : -1; };
    const auto isWater = [&](int index) { return index >= 0 && content.terrain(core::TerrainId(index)).sailable; };
    const auto keyOf = [&](int index) { return content.terrain(core::TerrainId(index)).key; };
    for (int y = 0; y < v.height; ++y)
        for (int x = 0; x < v.width; ++x) {
            const int here = terrainAt(x, y);
            if (here < 0) continue;
            static const int dx[] = {-1, 0, 1, 0}, dy[] = {0, -1, 0, 1};
            for (int side = 0; side < 4; ++side) {
                const int there = terrainAt(x + dx[side], y + dy[side]);
                if (there < 0 || there == here || (isWater(here) && !isWater(there))) continue;
                if (art::terrainLayer(keyOf(there)) > art::terrainLayer(keyOf(here)) && isWater(there) == isWater(here))
                    add(art::terrainEdge(keyOf(there), side, tile), x * tile, y * tile, 0.5);
            }
            if (!isWater(here)) continue;
            static const int ddx[] = {-1, 0, 1, 0, -1, 1, 1, -1}, ddy[] = {0, -1, 0, 1, -1, -1, 1, 1};
            int mask = 0;
            for (int d = 0; d < 8; ++d) {
                const int land = terrainAt(x + ddx[d], y + ddy[d]);
                if (land >= 0 && !isWater(land)) mask |= 1 << d;
            }
            if (!mask) continue;
            const int variant = art::variantFor(x, y);
            add(art::shoreUnder(mask, variant, tile), x * tile, y * tile, 0.6);
            for (int d = 0; d < 8; ++d)
                if (mask & (1 << d))
                    add(art::shoreLand(keyOf(terrainAt(x + ddx[d], y + ddy[d])), art::variantFor(x + ddx[d], y + ddy[d]), d, variant, tile),
                        x * tile, y * tile, 0.7);
        }
    // Out of sight: one pixel per tile, scaled up smoothly (pixel centres on tile centres).
    {
        QImage dim(v.width, v.height, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x)
                dim.setPixel(x, y, terrainAt(x, y) >= 0 && !v.inSight({x, y}) ? qPremultiply(qRgba(10, 8, 20, 140)) : 0);
        auto* shade = scene->addPixmap(QPixmap::fromImage(dim));
        shade->setTransformationMode(Qt::SmoothTransformation);
        shade->setTransform(QTransform::fromScale(tile, tile));
        shade->setZValue(2.9);
    }
    const QColor owner("#d62728");
    for (const client::ViewTown& t : v.towns)
        add(art::townSprite(art::TownLook{owner, art::raceKey(t.race), t.walls, t.size}, 2 * tile), t.pos.x * tile, (t.pos.y - 1) * tile, 3);
    for (const client::ViewMine& m : v.mines) add(art::mineSprite(content.resource(m.resource).key, owner, tile), m.pos.x * tile, m.pos.y * tile, 3);
    for (const client::ViewDwelling& d : v.dwellings) add(art::dwellingSprite(owner, d.unit, tile), d.pos.x * tile, d.pos.y * tile, 3);
    for (const client::ViewObject& o : v.objects) add(art::chestSprite(QString(), tile), o.pos.x * tile, o.pos.y * tile, 3);
    for (const client::ViewArmy& a : v.armies)
        if (!a.hero) add(art::monsterSprite(tile), a.pos.x * tile, a.pos.y * tile, 3);
    *walker = nullptr;
    for (const client::ViewHero& h : v.heroes) {
        auto* item = add(art::heroSprite(owner, false, tile, h.classId), h.pos.x * tile, h.pos.y * tile, 4);
        if (!*walker) *walker = item;
    }
    return scene;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Adventure map drawing speed: MapView against QGraphicsView."));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data"), QStringLiteral("The game data folder."), QStringLiteral("dir"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Map width and height (default 128)."), QStringLiteral("n"), "128");
    QCommandLineOption framesOption(QStringLiteral("frames"), QStringLiteral("Frames per test (default 200)."), QStringLiteral("n"), "200");
    QCommandLineOption tileOption(QStringLiteral("tile"), QStringLiteral("Only this zoom step (default 16, 48 and 96)."), QStringLiteral("px"));
    QCommandLineOption shotsOption(QStringLiteral("shots"), QStringLiteral("Save a picture of each view here (to compare them)."), QStringLiteral("dir"));
    QCommandLineOption glOption(QStringLiteral("gl"), QStringLiteral("Also the scene drawn with OpenGL."));
    parser.addOption(dataOption);
    parser.addOption(sizeOption);
    parser.addOption(framesOption);
    parser.addOption(tileOption);
    parser.addOption(glOption);
    parser.addOption(shotsOption);
    parser.process(app);

    const QString dataDir = core::findDataDirectory(parser.value(dataOption));
    core::ContentRegistry content;
    QStringList errors;
    if (dataDir.isEmpty() || !content.loadFromDirectory(dataDir, &errors)) {
        std::fprintf(stderr, "kot_viewbench: cannot load the data: %s\n", qPrintable(errors.join("; ")));
        return 1;
    }
    client::art::setArtDirectory(QDir(dataDir).filePath(QStringLiteral("art")));
    client::art::setContent(&content);

    // A game with the whole map explored by the first player.
    core::GameSetupParams params;
    params.mapWidth = params.mapHeight = parser.value(sizeOption).toInt();
    params.seed = 7;
    params.players = {{"A", false, {}, "humans", 1, "#d62728"}, {"B", true, "simple", "orcs", 2, "#1f77b4"},
                      {"C", true, "simple", "undead", 3, "#2ca02c"}, {"D", true, "simple", "sylvan", 4, "#ff7f0e"}};
    core::GameState state;
    QList<core::PlayerId> ids;
    QString error;
    if (!core::createGame(content, params, &state, &ids, &error)) {
        std::fprintf(stderr, "kot_viewbench: %s\n", qPrintable(error));
        return 1;
    }
    state.exploredMap(state.player(ids[0])->team).fill(1);
    client::GameView view;
    if (!client::parseGameView(core::buildPlayerView(state, content, ids[0]), &view, &error)) {
        std::fprintf(stderr, "kot_viewbench: %s\n", qPrintable(error));
        return 1;
    }
    const int frames = parser.value(framesOption).toInt();
    QList<int> tiles = {16, 48, 96};
    if (parser.isSet(tileOption)) tiles = {parser.value(tileOption).toInt()};
    std::printf("kot_viewbench: %dx%d map, window 1280x800, %d frames per test (%s)\n", view.width, view.height, frames,
                qPrintable(QGuiApplication::platformName()));

    for (const int tile : tiles) {
        // The client's map.
        {
            client::MapView map(&content);
            map.resize(1280, 800);
            map.setView(view);
            map.setTileSize(tile);
            map.show();
            waitExposed(&map);
            timeFrames(&map, 5, [&](int) { map.viewport()->update(); });
            if (parser.isSet(shotsOption)) {
                map.horizontalScrollBar()->setValue(0);
                map.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
            }
            if (parser.isSet(shotsOption)) map.viewport()->grab().save(QDir(parser.value(shotsOption)).filePath(QStringLiteral("mapview_%1.png").arg(tile)));  // warm up: pictures loaded, caches filled
            print("MapView", tile, "scroll", (timeFrames(&map, frames, [&](int i) {
                      const QPoint p = scrollAt(&map, i + 1, frames);
                      map.horizontalScrollBar()->setValue(p.x());
                      map.verticalScrollBar()->setValue(p.y());
                  })));
            print("MapView", tile, "walk", (timeFrames(&map, frames, [&](int) { map.viewport()->update(); })));  // its walk timer updates the viewport
        }
        // The same as a scene.
        for (const bool gl : {false, true}) {
            if (gl && !parser.isSet(glOption)) continue;
            QGraphicsPixmapItem* walker = nullptr;
            QElapsedTimer build;
            build.start();
            QGraphicsScene* scene = buildScene(view, content, tile, &walker);
            const double buildMs = build.nsecsElapsed() / 1e6;
            QGraphicsView gv(scene);
            if (gl) gv.setViewport(new QOpenGLWidget);
            gv.setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
            gv.setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
            gv.resize(1280, 800);
            gv.show();
            waitExposed(&gv);
            const char* name = gl ? "Scene (GL)" : "Scene";
            std::printf("%-14s tile %2d  %d items, built in %.0f ms\n", name, tile, int(scene->items().size()), buildMs);
            timeFrames(&gv, 5, [&](int) { gv.viewport()->update(); });
            if (parser.isSet(shotsOption)) {
                gv.horizontalScrollBar()->setValue(0);
                gv.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
            }
            if (parser.isSet(shotsOption)) gv.viewport()->grab().save(QDir(parser.value(shotsOption)).filePath(QStringLiteral("scene%1_%2.png").arg(gl ? "gl" : "").arg(tile)));
            print(name, tile, "scroll", (timeFrames(&gv, frames, [&](int i) {
                      const QPoint p = scrollAt(&gv, i + 1, frames);
                      gv.horizontalScrollBar()->setValue(p.x());
                      gv.verticalScrollBar()->setValue(p.y());
                  })));
            if (walker) {
                gv.centerOn(walker);
                const QPointF start = walker->pos();
                print(name, tile, "walk", (timeFrames(&gv, frames, [&](int i) {
                          walker->setPos(start + QPointF((i % 20) * tile / 10.0, 0));
                      })));
            }
            delete scene;
        }
    }
    return 0;
}
