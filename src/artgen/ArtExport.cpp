// Draws every picture the content can need and saves it as PNG (kot_artexport).

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <functional>

#include "artgen/ArtGen.h"
#include "core/content/ContentRegistry.h"

namespace kot::artgen {

namespace path = client::art::path;
using client::art::kMasterSize;

QImage tintOf(const QImage& black, const QImage& whiteImage) {
    const QImage base = black.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage white = whiteImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage tint(base.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < white.height(); ++y) {
        const QRgb* b = reinterpret_cast<const QRgb*>(base.constScanLine(y));
        const QRgb* w = reinterpret_cast<const QRgb*>(white.constScanLine(y));
        QRgb* t = reinterpret_cast<QRgb*>(tint.scanLine(y));
        for (int x = 0; x < white.width(); ++x) {
            // Premultiplied: the difference is at most the pixel's alpha.
            const int a = qAlpha(w[x]);
            t[x] = qRgba(std::clamp(qRed(w[x]) - qRed(b[x]), 0, a), std::clamp(qGreen(w[x]) - qGreen(b[x]), 0, a),
                         std::clamp(qBlue(w[x]) - qBlue(b[x]), 0, a), a);
        }
    }
    return tint;
}

namespace {

// Where the pictures go; with an empty folder they are only drawn (timing).
class Sink {
public:
    Sink(QString dir, ExportStats* stats) : m_dir(std::move(dir)), m_stats(stats) {}
    bool ok() const { return m_error.isEmpty(); }
    const QString& error() const { return m_error; }

    void save(const QString& name, const QImage& image) {
        if (m_dir.isEmpty() || !ok()) return;
        saveOne(name, image);
        for (const int d : client::art::kSmallerCopies) {
            const QSize small = image.size() / d;
            if (std::min(small.width(), small.height()) < client::art::kMinCopySide) break;
            // The tint keeps its suffix last: "<path>@2.tint" would not be found, so "<path>.tint" -> "<path>@2.tint".
            const qsizetype dot = name.endsWith(QLatin1String(".tint")) ? name.size() - 5 : name.size();
            const QString copy = client::art::copyPath(name.left(dot), d) + name.mid(dot);
            saveOne(copy, image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
    }
    void saveOne(const QString& name, const QImage& image) {
        if (!ok()) return;
        const QString file = QDir(m_dir).filePath(name + QStringLiteral(".png"));
        QDir().mkpath(QFileInfo(file).absolutePath());
        if (!image.save(file, "PNG")) {
            m_error = QStringLiteral("cannot write %1").arg(file);
            return;
        }
        if (m_stats) {
            ++m_stats->files;
            m_stats->bytes += QFileInfo(file).size();
        }
    }
    // A picture with the owner's colour: drawn with black and with white as that
    // colour; saved as base (black) and tint (white minus black), so the client can
    // put it together for any colour: picture = base + tint * colour.
    void saveOwner(const QString& name, const std::function<QImage(const QColor&)>& render) {
        const QImage base = render(QColor(0, 0, 0)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage tint = tintOf(base, render(QColor(255, 255, 255)));
        save(name, base);
        save(name + QStringLiteral(".tint"), tint);
    }

private:
    QString m_dir;
    ExportStats* m_stats;
    QString m_error;
};

// A transparent picture `w` x `h` drawn by `draw` (antialiased).
QImage canvas(int w, int h, const std::function<void(QPainter&)>& draw) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p);
    return img;
}

void drawMineFor(QPainter& p, const QRectF& r, const QString& resource, const QColor& owner) {
    const QColor colour = resourceColor(resource);
    if (resource == "wood") return drawSawmill(p, r, owner);
    if (resource == "stone") return drawQuarry(p, r, owner);
    if (resource == "gems") return drawCrystalCave(p, r, colour, owner);
    drawMine(p, r, colour, owner);
    drawResourceIcon(p, QRectF(r.left() + r.width() * 0.58, r.top() - r.height() * 0.02, r.width() * 0.42, r.height() * 0.42),
                     resource, colour);
}

// A dwelling with an empty round sign (the client puts the unit on it: dwellingSignRect).
void drawDwellingWithSign(QPainter& p, const QRectF& r, const QColor& owner) {
    drawDwelling(p, r.adjusted(0, r.height() * 0.08, -r.width() * 0.2, 0), owner);
    const QRectF sign(r.left() + r.width() * 0.52, r.top(), r.width() * 0.48, r.height() * 0.48);
    p.setPen(QPen(QColor(30, 20, 8), std::max(1.0, r.width() / 40)));
    p.setBrush(QColor(240, 228, 192));
    p.drawEllipse(sign);
}

// Every building figure of the town screen.
QList<SceneBuilding> sceneBuildings(const core::ContentRegistry& content) {
    using K = SceneBuilding::Kind;
    QList<SceneBuilding> list;
    for (int level = 0; level <= 2; ++level) list.append({K::Hall, level, {}});
    for (int level = 1; level <= client::art::kMaxMageGuild; ++level) list.append({K::MageGuild, level, {}});
    for (int level = 0; level <= 2; ++level) list.append({K::Dwelling, level, {}});
    for (const K kind : {K::Tavern, K::Market, K::Granary, K::Watchtower, K::Workshop, K::Other}) list.append({kind, 0, {}});
    Q_UNUSED(content);
    return list;
}

void exportEverything(const core::ContentRegistry& content, Sink& out) {
    const auto master = [](const std::function<void(QPainter&, const QRectF&)>& draw) { return renderMaster(draw); };
    const auto ownerMaster = [&](const QString& name, const std::function<void(QPainter&, const QRectF&, const QColor&)>& draw) {
        out.saveOwner(name, [&](const QColor& c) { return master([&](QPainter& p, const QRectF& r) { draw(p, r, c); }); });
    };

    // Units, each at its own rank (upgrades: advanced, elite), smaller for the low tiers.
    for (const core::UnitDef& u : content.units())
        out.save(path::unit(u.key, u.rank), master([&](QPainter& p, const QRectF& r) {
            drawUnitPicture(p, r, u.key, u.rank, unitScale(u.tier, u.rank));
        }));
    // Dwellings: plain, and with a sign for the unit (one for all units).
    ownerMaster(path::dwelling(), [](QPainter& p, const QRectF& r, const QColor& c) { drawDwelling(p, r, c); });
    ownerMaster(path::dwellingSign(), [](QPainter& p, const QRectF& r, const QColor& c) { drawDwellingWithSign(p, r, c); });

    // Towns on the map: the plain one, and per race its body by stage and its walls
    // (drawn in front by the client), not every combination.
    ownerMaster(path::town(), [](QPainter& p, const QRectF& r, const QColor& c) { drawTown(p, r, c); });
    const QStringList races = content.playableRaceKeys();
    for (const QString& race : races) {
        for (int stage = 0; stage <= client::art::kMaxTownStage; ++stage)
            ownerMaster(path::townBody(race, stage), [&](QPainter& p, const QRectF& r, const QColor& c) {
                drawTownIcon(p, r, TownLook{c, race, 0, stage * 3}, TownPart::Body);
            });
        for (int walls = 0; walls <= client::art::kMaxWalls; ++walls)
            ownerMaster(path::townWalls(race, walls), [&](QPainter& p, const QRectF& r, const QColor& c) {
                drawTownIcon(p, r, TownLook{c, race, walls, 0}, TownPart::Walls);
            });
    }

    // Mines (the ones with their own look, and the generic one: the client adds the
    // resource's icon), the chest (the same), resource symbols.
    for (const core::ResourceDef& res : content.resources()) {
        if (client::art::hasOwnMine(res.key))
            ownerMaster(path::mine(res.key), [&](QPainter& p, const QRectF& r, const QColor& c) { drawMineFor(p, r, res.key, c); });
        out.save(path::resource(res.key), master([&](QPainter& p, const QRectF& r) { drawResourceIcon(p, r, res.key, resourceColor(res.key)); }));
    }
    ownerMaster(path::genericMine(), [](QPainter& p, const QRectF& r, const QColor& c) { drawMine(p, r, QColor("#b8a888"), c); });
    out.save(path::chest(), master([](QPainter& p, const QRectF& r) { drawChest(p, r, QColor("#d4a017")); }));

    // Heroes on foot and in a boat, and their portraits.
    for (const core::HeroClassDef& c : content.heroClasses()) {
        for (const bool boat : {false, true})
            ownerMaster(path::hero(c.key, boat), [&](QPainter& p, const QRectF& r, const QColor& colour) {
                drawHero(p, r, colour, false, boat, c.key);
            });
        for (int face = 0; face < client::art::kPortraitFaces; ++face)
            out.saveOwner(path::portrait(c.key, face), [&](const QColor& colour) {
                return canvas(kMasterSize, kMasterSize, [&](QPainter& p) {
                    drawFramedPortrait(p, QRectF(0, 0, kMasterSize, kMasterSize), c.key, colour, face);
                });
            });
    }

    // Other map objects, artifacts, spells, sites, buttons, pointers.
    out.save(path::monster(), master([](QPainter& p, const QRectF& r) { drawMonster(p, r); }));
    out.save(path::artifactOnMap(), master([](QPainter& p, const QRectF& r) { drawArtifact(p, r); }));
    for (const core::ArtifactDef& a : content.artifacts())
        out.save(path::artifact(a.key), master([&](QPainter& p, const QRectF& r) { drawArtifactIcon(p, r, a.key, a.slot); }));
    for (const core::SpellDef& s : content.spells())
        out.save(path::spell(s.key), master([&](QPainter& p, const QRectF& r) { drawSpellIcon(p, r, s.key, s.school); }));
    for (const QString& site : client::art::siteNames())
        out.save(path::site(site), master([&](QPainter& p, const QRectF& r) { drawSite(p, r, site, false); }));
    out.save(path::siteGlow(), canvas(kMasterSize, kMasterSize, [](QPainter& p) {  // behind a site with something for the hero
        QRadialGradient g(QPointF(kMasterSize * 0.5, kMasterSize * 0.55), kMasterSize * 0.48);
        g.setColorAt(0, QColor(255, 230, 140, 190));
        g.setColorAt(0.6, QColor(255, 210, 90, 90));
        g.setColorAt(1, QColor(255, 200, 80, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(kMasterSize * 0.5, kMasterSize * 0.55), kMasterSize * 0.48, kMasterSize * 0.42);
    }));
    for (const QString& name : client::art::buttonNames())
        out.save(path::button(name), master([&](QPainter& p, const QRectF& r) { drawButtonIcon(p, r, name); }));
    for (const auto& [kind, name] : client::art::pointerNames()) out.save(path::pointer(name), drawPointer(kind, 4 * client::art::kPointerSize));
    for (const bool up : {true, false}) out.save(path::arrow(up), drawArrow(up));

    // Terrain: tiles, decorations, trees/hills/peaks, the bands where terrains meet.
    for (const core::TerrainDef& t : content.terrain()) {
        const QColor colour(t.color);
        for (int v = 0; v < client::art::kTileVariants; ++v) {
            out.save(path::terrainTile(t.key, v), drawTerrainTile(t.key, colour, kMasterSize, v).toImage());
            if (hasFeature(t.key)) out.save(path::feature(t.key, v), drawTerrainFeature(t.key, colour, kMasterSize, v).toImage());
        }
        for (int v = 0; v < client::art::kDecorationVariants; ++v)
            if (hasDecoration(t.key, v))
                out.save(path::decoration(t.key, v), master([&](QPainter& p, const QRectF& r) { drawDecoration(p, r, t.key, v); }));
    }
    for (int side = 0; side < 4; ++side)  // white: the client colours it with the neighbour's ground
        out.save(path::terrainEdge(side), drawTerrainEdge(Qt::white, side, kMasterSize, false).toImage());
    out.save(path::fog(), drawFog(kMasterSize));
    // Coasts.
    for (int v = 0; v < client::art::kShoreVariants; ++v) {
        for (const int mask : client::art::canonicalShoreMasks()) out.save(path::shoreUnder(mask, v), drawShoreUnder(mask, v, kMasterSize));
        for (int d = 0; d < 8; ++d) out.save(path::shoreLand(d, v), drawShoreLand(d, v, kMasterSize));
    }

    // The town screen per race: backdrop (with and without water), buildings, walls, boat.
    const int sw = int(client::art::kSceneWidth * client::art::kSceneScale), sh = int(client::art::kSceneHeight * client::art::kSceneScale);
    const QRectF scene(0, 0, client::art::kSceneWidth, client::art::kSceneHeight);
    const double unit = client::art::kSceneHeight * client::art::kSceneUnit;
    const int bw = int(client::art::kSceneCanvasW * unit * client::art::kSceneScale), bh = int(client::art::kSceneCanvasH * unit * client::art::kSceneScale);
    const QList<SceneBuilding> buildings = sceneBuildings(content);
    out.save(path::sceneBoat(), canvas(sw, sh, [&](QPainter& p) {  // one boat for all
        p.scale(client::art::kSceneScale, client::art::kSceneScale);
        drawSceneBoat(p, scene, QStringLiteral("humans"));
    }));
    {  // a dwelling's sign (the client puts the unit's picture on it)
        const int side = int(2 * client::art::kSceneSignRadius * unit * client::art::kSceneScale) + 4;
        out.save(path::sceneSign(), canvas(side, side, [&](QPainter& p) {
            p.setPen(QPen(QColor(30, 22, 12), std::max(1.0, side / 40.0)));
            p.setBrush(QColor(240, 228, 192));
            p.drawEllipse(QRectF(2, 2, side - 4, side - 4));
        }));
    }
    for (const QString& race : races) {
        for (const bool water : {false, true})
            out.save(path::sceneBackdrop(race, water), canvas(sw, sh, [&](QPainter& p) {
                p.scale(client::art::kSceneScale, client::art::kSceneScale);
                drawSceneBackdrop(p, scene, race, water);
            }));
        for (int walls = 0; walls <= client::art::kMaxWalls; ++walls)
            out.saveOwner(path::sceneWalls(race, walls), [&](const QColor& c) {
                return canvas(sw, sh, [&](QPainter& p) {
                    p.scale(client::art::kSceneScale, client::art::kSceneScale);
                    drawSceneWalls(p, scene, TownLook{c, race, walls, 0}, walls);
                });
            });
        for (const SceneBuilding& b : buildings)
            out.saveOwner(path::sceneBuilding(race, client::art::sceneBuildingName(b)), [&](const QColor& c) {
                return canvas(bw, bh, [&](QPainter& p) {
                    p.scale(client::art::kSceneScale, client::art::kSceneScale);
                    drawSceneBuilding(p, client::art::kSceneAnchorX * unit, client::art::kSceneAnchorY * unit, unit, b, TownLook{c, race, 0, 0});
                });
            });
    }
}

}  // namespace

bool exportAll(const core::ContentRegistry& content, const QString& outDir, ExportStats* stats, QString* error) {
    Sink sink(outDir, stats);
    exportEverything(content, sink);
    if (!sink.ok() && error) *error = sink.error();
    return sink.ok();
}

void drawAll(const core::ContentRegistry& content) {
    Sink sink(QString(), nullptr);
    exportEverything(content, sink);
}

}  // namespace kot::artgen
