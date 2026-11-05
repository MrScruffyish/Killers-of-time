#include "client/Art.h"

#include <QCache>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/content/ContentRegistry.h"

namespace kot::client::art {

namespace {

QString g_artDir;
const core::ContentRegistry* g_content = nullptr;

QString fileOf(const QString& path) { return QDir(g_artDir).filePath(path + QStringLiteral(".png")); }

// A missing picture: a dark square with a magenta frame and a question mark, so it
// is plain to see which one is missing (and a warning names the file).
QImage placeholder(const QString& path) {
    static QSet<QString> warned;
    if (!warned.contains(path)) {
        warned.insert(path);
        qWarning("art: no picture %s", qPrintable(fileOf(path)));
    }
    QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(40, 20, 40, 200));
    QPainter p(&img);
    p.setPen(QPen(QColor("#ff40ff"), 4));
    p.drawRect(QRectF(2, 2, 60, 60));
    QFont f = p.font();
    f.setPixelSize(40);
    f.setBold(true);
    p.setFont(f);
    p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("?"));
    return img;
}

// The master of a path (kept for a while: a picture is seen at several sizes and
// colours; at most kMasterCacheMB of them, the least used go first). Null when there is
// no file (remembered, so a missing optional picture is looked for once).
constexpr int kMasterCacheMB = 384;
QImage master(const QString& path) {
    static QCache<QString, QImage> cache(kMasterCacheMB * 1024);  // cost in KB
    static QSet<QString> missing;
    const QString file = fileOf(path);
    if (const QImage* img = cache.object(file)) return *img;
    if (missing.contains(file)) return {};
    QImage img(file);
    if (img.isNull()) {
        missing.insert(file);
        return img;
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    cache.insert(file, new QImage(img), std::max<qsizetype>(1, img.sizeInBytes() / 1024));
    return img;
}

QImage masterOrPlaceholder(const QString& path) {
    const QImage m = master(path);
    if (!m.isNull()) return m;
    static QHash<QString, QImage> placeholders;
    auto it = placeholders.constFind(path);
    if (it == placeholders.cend()) it = placeholders.insert(path, placeholder(path));
    return it.value();
}

QPixmap scaledTo(const QImage& img, int w, int h) {
    return QPixmap::fromImage(img.size() == QSize(w, h) ? img : img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

// The size (w, h) as part of a cache key.
QString sized(const QString& path, int w, int h) { return QStringLiteral("%1@%2x%3").arg(path).arg(w).arg(h); }

}  // namespace

void setArtDirectory(const QString& dir) { g_artDir = dir; }
const QString& artDirectory() { return g_artDir; }

void setContent(const core::ContentRegistry* content) { g_content = content; }

QString unitKey(core::UnitTypeId unit) {
    const core::UnitDef* u = g_content ? g_content->unit(unit) : nullptr;
    return u ? u->key : QString();
}
QString heroClassKey(core::HeroClassId heroClass) {
    const core::HeroClassDef* c = g_content ? g_content->heroClass(heroClass) : nullptr;
    return c ? c->key : QStringLiteral("knight");
}
QString raceKey(core::RaceId race) { return g_content && race.isValid() ? g_content->race(race).key : QString(); }

bool hasPicture(const QString& path) { return !master(path).isNull(); }

const QPixmap& sprite(const QString& path, int size) { return sprite(path, size, size); }

const QPixmap& sprite(const QString& path, int width, int height) {
    static QHash<QString, QPixmap> cache;
    const QString key = sized(path, width, height);
    auto it = cache.constFind(key);
    if (it == cache.cend()) it = cache.insert(key, scaledTo(masterOrPlaceholder(path), width, height));
    return it.value();
}

const QPixmap& ownerSprite(const QString& path, const QColor& owner, int size) { return ownerSprite(path, owner, size, size); }

const QPixmap& ownerSprite(const QString& path, const QColor& owner, int width, int height) {
    static QHash<QString, QPixmap> cache;
    const QString key = sized(path, width, height) + owner.name();
    auto it = cache.constFind(key);
    if (it != cache.cend()) return it.value();
    const QImage base = masterOrPlaceholder(path);
    const QImage tint = master(path + QStringLiteral(".tint"));
    if (tint.isNull() || tint.size() != base.size()) return cache.insert(key, scaledTo(base, width, height)).value();
    // picture = base + tint * owner (per channel, premultiplied).
    QImage picture = base;
    const int cr = owner.red(), cg = owner.green(), cb = owner.blue();
    for (int y = 0; y < picture.height(); ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(picture.scanLine(y));
        const QRgb* t = reinterpret_cast<const QRgb*>(tint.constScanLine(y));
        for (int x = 0; x < picture.width(); ++x) {
            const int a = qAlpha(out[x]);
            out[x] = qRgba(std::min(a, qRed(out[x]) + qRed(t[x]) * cr / 255), std::min(a, qGreen(out[x]) + qGreen(t[x]) * cg / 255),
                           std::min(a, qBlue(out[x]) + qBlue(t[x]) * cb / 255), a);
        }
    }
    return cache.insert(key, scaledTo(picture, width, height)).value();
}

// ---- the map

int variantFor(int x, int y) {
    quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u;
    return int((h >> 7) % kTileVariants);
}

const QPixmap& terrainTile(const QString& terrain, int size, int variant) { return sprite(path::terrainTile(terrain, variant), size); }

const QPixmap& terrainEdge(const QString& neighbour, int side, int size) { return sprite(path::terrainEdge(neighbour, side), size); }

int decorationAt(int x, int y) {
    const quint32 h = (quint32(x) * 2654435761u) ^ (quint32(y) * 40503u + 0x9e37u);
    return (h >> 9) % 6 == 0 ? int((h >> 13) % kDecorationVariants) : -1;  // variants 4 and 5 are the rarer, bigger pieces
}

const QPixmap& decoration(const QString& terrain, int variant, int size) {
    static const QPixmap none;
    const QString p = path::decoration(terrain, variant);
    return hasPicture(p) ? sprite(p, size) : none;
}

const QPixmap& fogTexture() {
    static const QPixmap tex = QPixmap::fromImage(masterOrPlaceholder(path::fog()).scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    return tex;
}

const QPixmap& terrainFeature(const QString& terrain, int size, int variant) {
    static const QPixmap none;
    const QString p = path::feature(terrain, variant);
    return hasPicture(p) ? sprite(p, 2 * size) : none;
}

const QPixmap& shoreUnder(int mask, int variant, int size) { return sprite(path::shoreUnder(canonicalShoreMask(mask), variant), size); }

const QPixmap& shoreLand(const QString& land, int landVariant, int direction, int variant, int size) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(land).arg(landVariant).arg(direction).arg(variant).arg(size);
    auto it = cache.constFind(key);
    if (it != cache.cend()) return it.value();
    // The land's tile where the piece's mask is (the tile pattern repeats every tile,
    // so the neighbour's tile continues here unchanged).
    QImage piece = terrainTile(land, size, landVariant).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&piece);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawPixmap(0, 0, sprite(path::shoreLand(direction, variant), size));
    }
    return cache.insert(key, QPixmap::fromImage(piece)).value();
}

int terrainLayer(const QString& id) {
    static const QHash<QString, int> layers = {{"sea", 0}, {"river", 1}, {"lava", 2}, {"swamp", 3}, {"sand", 4},
                                               {"dirt", 5}, {"road", 6}, {"grass", 7}, {"snow", 8}, {"forest", 9},
                                               {"hills", 10}, {"mountains", 11}};
    return layers.value(id, 5);
}

int nextZoom(int tile, int direction) {
    const int n = int(std::size(kZoomSteps));
    int index = 0;
    for (int i = 0; i < n; ++i)
        if (kZoomSteps[i] <= tile) index = i;
    return kZoomSteps[std::clamp(index + direction, 0, n - 1)];
}

// ---- map objects

const QPixmap& townSprite(const QColor& owner, int size) { return ownerSprite(path::town(), owner, size); }
const QPixmap& townSprite(const TownLook& look, int size) {
    const QString race = look.race.isEmpty() ? QStringLiteral("humans") : look.race;
    return ownerSprite(path::town(race, std::clamp(look.walls, 0, kMaxWalls), townStage(look.size)), look.owner, size);
}
const QPixmap& mineSprite(const QString& resource, const QColor& owner, int size) { return ownerSprite(path::mine(resource), owner, size); }
const QPixmap& dwellingSprite(const QColor& owner, int size) { return ownerSprite(path::dwelling(), owner, size); }
const QPixmap& dwellingSprite(const QColor& owner, core::UnitTypeId unit, int size) {
    return ownerSprite(path::dwelling(unitKey(unit)), owner, size);
}
const QPixmap& heroSprite(const QColor& owner, bool inBoat, int size, core::HeroClassId heroClass) {
    return ownerSprite(path::hero(heroClassKey(heroClass), inBoat), owner, size);
}
const QPixmap& monsterSprite(int size) { return sprite(path::monster(), size); }
const QPixmap& chestSprite(const QString& resource, int size) { return sprite(path::chest(resource), size); }
const QPixmap& artifactSprite(int size) { return sprite(path::artifactOnMap(), size); }
const QPixmap& siteSprite(const QString& site, bool glowing, int size) { return sprite(path::site(site, glowing), size); }

// ---- icons and pictures

const QPixmap& resourceIcon(const QString& resource, int size) { return sprite(path::resource(resource), size); }
const QPixmap& artifactIcon(const QString& artifact, int size) { return sprite(path::artifact(artifact), size); }
const QPixmap& spellIcon(const QString& spell, int size) { return sprite(path::spell(spell), size); }
const QPixmap& unitIcon(const QString& unit, int rank, int size) {
    const QString p = path::unit(unit, rank);
    // A rank without its own picture (shown before an upgrade): the regular one.
    return sprite(rank != 0 && !hasPicture(p) ? path::unit(unit, 0) : p, size);
}
const QPixmap& unitIcon(core::UnitTypeId unit, int rank, int size) { return unitIcon(unitKey(unit), rank, size); }
const QPixmap& portrait(core::HeroClassId heroClass, const QColor& owner, int seed, int size) {
    return ownerSprite(path::portrait(heroClassKey(heroClass), std::abs(seed) % kPortraitFaces), owner, size);
}

QIcon buttonIcon(const QString& name) {
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(name);
    if (it == cache.cend()) it = cache.insert(name, QIcon(sprite(path::button(name), 128)));
    return it.value();
}

QPixmap pointerImage(Pointer kind) {
    for (const auto& [k, name] : pointerNames())
        if (k == kind) return sprite(path::pointer(name), kPointerSize);
    return sprite(path::pointer(QStringLiteral("normal")), kPointerSize);
}

// ---- composed pictures

void drawTownScene(QPainter& p, const QRectF& r, const TownLook& look, const QList<SceneBuilding>& buildings) {
    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QString race = look.race.isEmpty() ? QStringLiteral("humans") : look.race;
    const int w = int(r.width()), h = int(r.height());
    bool water = false;
    int walls = 0;
    for (const SceneBuilding& b : buildings) {
        water = water || b.kind == SceneBuilding::Kind::Shipyard;
        walls += b.kind == SceneBuilding::Kind::Walls;
    }
    p.drawPixmap(r.topLeft(), sprite(path::sceneBackdrop(race, water), w, h));
    // Plots from left to right; the hall in the middle.
    const double unit = h * kSceneUnit;
    QList<SceneBuilding> sorted = buildings;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SceneBuilding& a, const SceneBuilding& b) { return int(a.kind) < int(b.kind); });
    int plot = 0;
    const int plots = std::max(8, int(sorted.size()));
    const auto plotX = [&](int i) {
        // Spread around the centre: 0 centre, then left/right alternately.
        const int side = (i % 2) ? -1 : 1;
        const int step = (i + 1) / 2;
        return 0.47 + side * step * (0.86 / plots);
    };
    const int bw = int(std::lround(kSceneCanvasW * unit)), bh = int(std::lround(kSceneCanvasH * unit));
    for (const SceneBuilding& b : sorted) {
        if (b.kind == SceneBuilding::Kind::Walls || b.kind == SceneBuilding::Kind::Shipyard) continue;
        const double cx = r.left() + r.width() * plotX(plot++);
        const double base = r.top() + r.height() * kSceneBase;
        p.drawPixmap(QPointF(cx - kSceneAnchorX * unit, base - kSceneAnchorY * unit),
                     ownerSprite(path::sceneBuilding(race, sceneBuildingName(b)), look.owner, bw, bh));
        if (b.kind == SceneBuilding::Kind::Dwelling && !b.unit.isEmpty()) {  // the unit that lives here, on its sign
            const int s = int(unit * 0.5);
            const double hgt = unit * dwellingHeight(b.level);
            p.drawPixmap(QPointF(cx - s / 2.0, base - hgt - s * 0.72), unitIcon(b.unit, b.level, s));
        }
    }
    if (water) p.drawPixmap(r.topLeft(), sprite(path::sceneBoat(race), w, h));
    p.drawPixmap(r.topLeft(), ownerSprite(path::sceneWalls(race, std::clamp(walls, 0, kMaxWalls)), look.owner, w, h));
    p.restore();
}

void drawUnitToken(QPainter& p, const QPointF& c, double rad, const QColor& side, const QString& name, int count,
                   int stars, bool active, bool target, const QString& unitId, int rank) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 80));
    p.drawEllipse(c + QPointF(rad * 0.08, rad * 0.12), rad, rad);
    QRadialGradient g(c - QPointF(rad * 0.3, rad * 0.3), rad * 1.4);
    g.setColorAt(0, side.lighter(150));
    g.setColorAt(1, side.darker(140));
    p.setBrush(g);
    p.setPen(QPen(active ? QColor("#ffd84a") : target ? QColor("#ff3030") : QColor("#20180c"), active || target ? 3 : 1.5));
    p.drawEllipse(c, rad, rad);
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rad * 0.8, rad * 0.8);
    if (!unitId.isEmpty()) {
        // The unit's picture inside the shield.
        const int size = std::max(8, int(rad * 1.5));
        p.drawPixmap(QPointF(c.x() - size / 2.0, c.y() - rad * 0.92), unitIcon(unitId, rank, size));
    }
    if (rad >= 10) {
        QFont f = p.font();
        f.setPixelSize(std::max(7, int(rad * 0.36)));
        f.setBold(true);
        p.setFont(f);
        if (unitId.isEmpty()) {
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - rad, c.y() - rad * 0.7, 2 * rad, rad * 0.8), Qt::AlignCenter, name.left(8));
        }
        // Rank chevrons (advanced one, elite two) on the left.
        p.setPen(QPen(QColor("#ffd84a"), std::max(1.5, rad / 9)));
        for (int i = 0; i < rank; ++i) {
            const double y = c.y() + rad * (0.05 - 0.22 * i);
            p.drawLine(QPointF(c.x() - rad * 0.92, y - rad * 0.1), QPointF(c.x() - rad * 0.78, y));
            p.drawLine(QPointF(c.x() - rad * 0.78, y), QPointF(c.x() - rad * 0.64, y - rad * 0.1));
        }
        // The count on a small plate.
        const QRectF plate(c.x() - rad * 0.55, c.y() + rad * 0.42, rad * 1.1, rad * 0.5);
        p.setPen(QPen(QColor("#20180c"), 1));
        p.setBrush(QColor("#f1e4c0"));
        p.drawRoundedRect(plate, 3, 3);
        p.setPen(QColor("#20180c"));
        p.drawText(plate, Qt::AlignCenter, QString::number(count));
        if (stars > 0) {
            p.setPen(QColor("#ffd84a"));
            p.drawText(QRectF(c.x() - rad, c.y() - rad * 1.15, 2 * rad, rad * 0.5), Qt::AlignCenter, QString(stars, QChar(0x2605)));
        }
    }
    p.restore();
}

void drawBattleFigure(QPainter& p, const QPointF& c, double r, const QColor& side, const QString& unitId, int rank,
                      int count, int stars, bool active, bool target, bool faceLeft) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // The base: a shadow, a disc in the side's colour, a ring.
    const QPointF bc(c.x(), c.y() + r * 0.42);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(bc + QPointF(r * 0.06, r * 0.08), r * 0.74, r * 0.28);
    QLinearGradient base(bc - QPointF(0, r * 0.26), bc + QPointF(0, r * 0.26));
    base.setColorAt(0, side.lighter(150));
    base.setColorAt(1, side.darker(150));
    p.setBrush(base);
    p.setPen(QPen(active ? QColor("#ffd84a") : target ? QColor("#ff3030") : QColor(20, 12, 4, 220), active || target ? std::max(2.0, r * 0.1) : 1.5));
    p.drawEllipse(bc, r * 0.7, r * 0.26);
    if (active) {  // a soft glow around the one acting
        QRadialGradient glow(bc, r * 1.1);
        glow.setColorAt(0, QColor(255, 216, 74, 90));
        glow.setColorAt(1, QColor(255, 216, 74, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(bc, r * 1.1, r * 0.5);
    }
    // The figure, standing on the base.
    const int size = std::max(12, int(r * 2.45));
    const QPixmap& pm = unitIcon(unitId, rank, size);
    const QPointF topLeft(c.x() - size / 2.0, bc.y() + r * 0.12 - size);
    if (faceLeft) {
        p.save();
        p.translate(c.x(), 0);
        p.scale(-1, 1);
        p.drawPixmap(QPointF(-size / 2.0, topLeft.y()), pm);
        p.restore();
    } else {
        p.drawPixmap(topLeft, pm);
    }
    if (r >= 8) {
        QFont f = p.font();
        f.setPixelSize(std::max(8, int(r * 0.36)));
        f.setBold(true);
        p.setFont(f);
        // The count on a plate at the front of the base.
        const QString text = QString::number(count);
        const double tw = std::max(r * 0.7, QFontMetrics(f).horizontalAdvance(text) + r * 0.25);
        const QRectF plate(c.x() + (faceLeft ? -r * 0.95 : r * 0.95 - tw), bc.y() - r * 0.02, tw, r * 0.44);
        p.setPen(QPen(QColor("#20180c"), 1.2));
        QLinearGradient pg(plate.topLeft(), plate.bottomLeft());
        pg.setColorAt(0, QColor("#fff4d8"));
        pg.setColorAt(1, QColor("#d8c8a0"));
        p.setBrush(pg);
        p.drawRoundedRect(plate, r * 0.12, r * 0.12);
        p.setPen(QColor("#20180c"));
        p.drawText(plate, Qt::AlignCenter, text);
        // Rank chevrons on the other side of the base.
        p.setPen(QPen(QColor("#ffd84a"), std::max(1.5, r / 9), Qt::SolidLine, Qt::RoundCap));
        const double chx = c.x() + (faceLeft ? r * 0.6 : -r * 0.6);
        for (int i = 0; i < rank; ++i) {
            const double y = bc.y() + r * (0.12 - 0.16 * i);
            p.drawLine(QPointF(chx - r * 0.12, y - r * 0.08), QPointF(chx, y));
            p.drawLine(QPointF(chx, y), QPointF(chx + r * 0.12, y - r * 0.08));
        }
        if (stars > 0) {  // veterancy over the head
            p.setPen(QColor(20, 12, 4));
            const QRectF sr(c.x() - r, topLeft.y() - r * 0.1, 2 * r, r * 0.4);
            p.drawText(sr.translated(1, 1), Qt::AlignCenter, QString(stars, QChar(0x2605)));
            p.setPen(QColor("#ffd84a"));
            p.drawText(sr, Qt::AlignCenter, QString(stars, QChar(0x2605)));
        }
    }
    p.restore();
}

namespace {
// The style sheet's arrow images (files: it cannot draw them itself).
QString arrowFile(bool up) { return fileOf(path::arrow(up)); }
}  // namespace

QString styleSheet() {
    QString css = QStringLiteral(R"(
QWidget { background-color: #2b2118; color: #eadfc8; font-size: 10pt; }
QLabel { background: transparent; }
QPushButton {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6e4e2f, stop:0.5 #5a3d22, stop:1 #48301a);
    border: 1px solid #9a7a3a; border-top-color: #c9a24a; border-bottom-color: #6a5028;
    border-radius: 8px; padding: 6px 14px; color: #f6e7c1;
}
QPushButton:hover { border-color: #e0bc62; color: #fff4d8; }
QPushButton:pressed { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a2614, stop:1 #4a3119); padding-top: 7px; padding-bottom: 5px; }
QPushButton:focus { outline: none; }
QPushButton:default { border-color: #d8b45a; }
QPushButton:disabled { color: #857660; border-color: #4e4228; background-color: #352819; }
QPushButton#endTurnButton { font-weight: bold; padding: 8px 10px; }
QPushButton#levelUpButton {
    font-weight: bold; color: #2b1a08; border: 2px solid #fff0b0;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffe27a, stop:1 #d8a020);
}
QPushButton#endTurnButton[ready="true"] {
    font-size: 12pt; color: #2b1a08; border: 2px solid #fff0b0;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffd75a, stop:1 #c98a12);
}
QPushButton#endTurnButton[ready="true"][pulse="true"] {
    border: 2px solid #ffffff;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fff09a, stop:1 #e8a820);
}
QTableView QPushButton { padding: 2px 8px; border-radius: 6px; margin: 2px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; }
QToolButton:hover { border-color: #9a7a3a; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QListWidget {
    background-color: #1f1812; border: 1px solid #6a5530; border-radius: 6px; color: #eadfc8;
    selection-background-color: #7a5a24; padding: 2px 4px;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #c9a24a; }
QComboBox QAbstractItemView { background-color: #1f1812; color: #eadfc8; border: 1px solid #6a5530; outline: none;
                              selection-background-color: #6a4a22; }
QListWidget { outline: none; }
QListWidget::item { padding: 3px 4px; border-radius: 4px; }
QListWidget::item:selected { background: #6a4a22; color: #fff0c8; }
QListWidget::item:hover { background: #3a2c1e; }
QTableWidget, QTableView {
    background-color: #241b13; alternate-background-color: #2c2218; border: 1px solid #5e4a2a; border-radius: 8px;
    gridline-color: transparent; color: #eadfc8; selection-background-color: #5a4020; outline: none;
}
QTableView::item { padding: 4px 6px; border: none; border-bottom: 1px solid #33281c; }
QTableView::item:selected { background: #4a3620; color: #fff0c8; }
QTableCornerButton::section { background: transparent; border: none; }
QHeaderView { background: transparent; }
QHeaderView::section {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #45341f, stop:1 #382a1a);
    color: #f0d898; border: none; border-bottom: 1px solid #7a6235; padding: 5px 6px; font-weight: bold;
}
QTabWidget::pane { border: 1px solid #6a5530; border-radius: 8px; top: -1px; background: #2b2118; }
QTabBar::tab {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3e2f20, stop:1 #33271a);
    border: 1px solid #5e4a2a; border-bottom: none; border-top-left-radius: 8px; border-top-right-radius: 8px;
    padding: 6px 16px; margin-right: 3px; color: #cdbd9c;
}
QTabBar::tab:hover { color: #f0e0c0; }
QTabBar::tab:selected {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a4a2a, stop:1 #2b2118);
    border-color: #a8873e; color: #ffe9b0;
}
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: #6b4a2b; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #8a643a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip { background-color: #f1e4c0; color: #20180c; border: 1px solid #a8873e; border-radius: 6px; padding: 4px 6px; }
QComboBox { min-width: 7em; padding: 3px 22px 3px 8px; }
QComboBox::drop-down { width: 18px; border: none; }
QComboBox::down-arrow { image: url(%DOWN%); width: 10px; height: 10px; }
QSpinBox { padding-right: 18px; min-width: 3em; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: transparent; border: none; }
QSpinBox::up-arrow { image: url(%UP%); width: 8px; height: 8px; }
QSpinBox::down-arrow { image: url(%DOWN%); width: 8px; height: 8px; }
QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #c9a24a; border-radius: 4px; background: #1d1610; }
QCheckBox::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.6, fx:0.5, fy:0.5,
                               stop:0 #ffe07a, stop:0.55 #c9a24a, stop:0.6 #1d1610); }
QCheckBox::indicator:disabled { border-color: #5e5030; }
QGroupBox { border: 1px solid #5e4a2a; border-radius: 8px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #f0d898; }
QMenu { background-color: #2b2118; border: 1px solid #7a6235; border-radius: 6px; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 22px; border-radius: 4px; }
QMenu::item:selected { background: #5a4020; color: #fff0c8; }
QMenu::separator { height: 1px; background: #5e4a2a; margin: 4px 8px; }
QDialog, QMessageBox { background-color: #2b2118; }
)");
    css.replace(QStringLiteral("%UP%"), arrowFile(true));
    css.replace(QStringLiteral("%DOWN%"), arrowFile(false));
    return css;
}

}  // namespace kot::client::art
