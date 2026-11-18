#include <QDir>
#include <QDirIterator>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

#include "artgen/ArtGen.h"
#include "client/Art.h"
#include "core/content/ContentRegistry.h"

using namespace kot;
using namespace kot::client;
using kot::core::ContentRegistry;

namespace {
// Mean and largest difference per channel (0..255) between two pictures of one size.
std::pair<double, int> difference(const QImage& a, const QImage& b) {
    const QImage x = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    long long sum = 0;
    int worst = 0;
    for (int row = 0; row < x.height(); ++row)
        for (int col = 0; col < x.width(); ++col) {
            const QRgb p = x.pixel(col, row), q = y.pixel(col, row);
            for (const int d : {qRed(p) - qRed(q), qGreen(p) - qGreen(q), qBlue(p) - qBlue(q), qAlpha(p) - qAlpha(q)}) {
                sum += std::abs(d);
                worst = std::max(worst, std::abs(d));
            }
        }
    return {double(sum) / (x.width() * x.height() * 4), worst};
}

// How many clearly visible (non-transparent) pixels a picture has: catches a blank
// picture from a drawing branch that never ran.
int inkPixels(const QImage& img) {
    const QImage x = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int n = 0;
    for (int row = 0; row < x.height(); ++row)
        for (int col = 0; col < x.width(); ++col)
            if (qAlpha(x.pixel(col, row)) > 24) ++n;
    return n;
}

QImage renderArt(const std::function<void(QPainter&, const QRectF&)>& draw) {
    return artgen::renderMaster(draw, 128);
}
}  // namespace

class TstArt : public QObject {
    Q_OBJECT
private slots:
    // A picture with the owner's colour is kept as base + tint (any colour from two
    // files): put together by the client it must look like the picture drawn in that
    // colour directly.
    void ownerColourIsPutTogetherFaithfully() {
        using DrawFn = std::function<void(QPainter&, const QRectF&, const QColor&)>;
        const QList<std::pair<QString, DrawFn>> pictures = {
            {"town", [](QPainter& p, const QRectF& r, const QColor& c) { artgen::drawTown(p, r, c); }},
            {"dwelling", [](QPainter& p, const QRectF& r, const QColor& c) { artgen::drawDwelling(p, r, c); }},
            {"hero", [](QPainter& p, const QRectF& r, const QColor& c) { artgen::drawHero(p, r, c, false, false, "knight"); }},
            {"mine", [](QPainter& p, const QRectF& r, const QColor& c) { artgen::drawSawmill(p, r, c); }},
            {"town2", [](QPainter& p, const QRectF& r, const QColor& c) { artgen::drawTownIcon(p, r, art::TownLook{c, "humans", 2, 6}); }},
        };
        QTemporaryDir dir;
        art::setArtDirectory(dir.path());
        for (const auto& [name, draw] : pictures) {
            const auto render = [&](const QColor& c) { return artgen::renderMaster([&](QPainter& p, const QRectF& r) { draw(p, r, c); }); };
            const QImage base = render(Qt::black);
            QVERIFY(base.save(dir.filePath(name + ".png")));
            QVERIFY(artgen::tintOf(base, render(Qt::white)).save(dir.filePath(name + ".tint.png")));
            for (const char* colour : {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#e377c2", "#9a9a9a"}) {
                const QColor c(colour);
                const QImage put = art::ownerSprite(name, c, 128).toImage();
                const QImage drawn = render(c).scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                const auto [mean, worst] = difference(put, drawn);
                // Owner colours must be used linearly (artgen::towardsWhite, not QColor::lighter).
                QVERIFY2(mean < 0.5 && worst < 8, qPrintable(QStringLiteral("%1 in %2: mean %3, worst %4").arg(name, colour).arg(mean).arg(worst)));
            }
        }
        art::setArtDirectory(QString());
    }

    // Pictures come from their files, at any size; a missing one is a placeholder.
    void picturesAreLoadedFromFiles() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("test"));
        QImage green(art::kMasterSize, art::kMasterSize, QImage::Format_ARGB32_Premultiplied);
        green.fill(Qt::darkGreen);
        QVERIFY(green.save(dir.filePath("test/square.png")));
        art::setArtDirectory(dir.path());
        const QPixmap pm = art::sprite("test/square", 48);
        QCOMPARE(pm.size(), QSize(48, 48));
        QVERIFY(pm.toImage().pixelColor(24, 24).green() > 60);
        QVERIFY(art::hasPicture("test/square"));
        QVERIFY(!art::hasPicture("test/none"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("art: no picture .*test/none.png"));
        const QPixmap missing = art::sprite("test/none", 32);
        QCOMPARE(missing.size(), QSize(32, 32));
        QVERIFY(art::decoration("nothing", 1, 32).isNull());  // optional pictures: none, no placeholder
        art::setArtDirectory(QString());
    }

    // The art folder has every picture the client asks for (run kot_artexport after
    // changing the content or the drawings), and the export writes the same files.
    void exportWritesEveryPicture() {
        ContentRegistry content;
        QStringList errors;
        QVERIFY2(content.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        QTemporaryDir dir;
        artgen::ExportStats stats;
        QString error;
        QVERIFY2(artgen::exportAll(content, dir.path(), &stats, &error), qPrintable(error));
        QVERIFY(stats.files > 1000);
        QStringList inData, inExport;
        const QDir art(QDir(KOT_DATA_DIR).filePath("art")), out(dir.path());
        for (QDirIterator it(dir.path(), {"*.png"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
            inExport << out.relativeFilePath(it.next());
        for (QDirIterator it(art.path(), {"*.png"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
            inData << art.relativeFilePath(it.next());
        inExport.sort();
        inData.sort();
        QCOMPARE(inData, inExport);
        QCOMPARE(QImage(out.filePath(art::path::unit("militia", 0) + ".png")).size(), QSize(art::kMasterSize, art::kMasterSize));
    }

    // Iron and bronze have their own map colours and ingot icons.
    void metalResourcesHaveTheirOwnArt() {
        const QColor iron = artgen::resourceColor("iron");
        const QColor bronze = artgen::resourceColor("bronze");
        QVERIFY2(iron.isValid() && bronze.isValid(), "iron and bronze need map colours");
        QVERIFY2(iron != bronze, "iron and bronze must have different colours");
        QVERIFY2(iron != artgen::resourceColor("gold"), "iron must not reuse gold's colour");
        QVERIFY2(bronze != artgen::resourceColor("gold"), "bronze must not reuse gold's colour");
        const auto icon = [](const QString& id) {
            return renderArt([&](QPainter& p, const QRectF& r) {
                artgen::drawResourceIcon(p, r, id, artgen::resourceColor(id));
            });
        };
        const QImage i = icon("iron"), b = icon("bronze");
        QVERIFY2(inkPixels(i) > 300, "iron ingot icon must not be blank");
        QVERIFY2(inkPixels(b) > 300, "bronze ingot icon must not be blank");
        const auto [mean, worst] = difference(i, b);
        QVERIFY2(mean > 0.5, qPrintable(QStringLiteral("iron/bronze ingots too alike: mean %1 worst %2").arg(mean).arg(worst)));
    }

    // Every unit of the three new races has its own picture, not the generic fallback.
    void newRaceUnitsHaveTheirOwnArt() {
        const QStringList ids = {
            "bone_acolyte", "bone_zealot", "crypt_creeper", "crypt_stalker", "soul_scribe", "soul_archivist",
            "sepulchral_guard", "sepulchral_lord", "bone_colossus", "bone_titan", "bone_overlord",
            "young_giant", "giant_warrior", "cave_troll", "cave_ogre", "boulder_thrower", "bombardier_giant",
            "ice_giant", "glacier_giant", "sky_giant", "storm_giant", "storm_king",
            "halfling_sling", "halfling_marksman", "pony_rider", "pony_lancer", "burrow_guard", "burrow_captain",
            "stronghold_guard", "stronghold_champion", "clan_champion", "clan_hero", "clan_legend"};
        const auto unit = [](const QString& id) {
            return renderArt([&](QPainter& p, const QRectF& r) { artgen::drawUnitIcon(p, r, id, 0); });
        };
        for (const QString& id : ids)
            QVERIFY2(inkPixels(unit(id)) > 300, qPrintable(id + QStringLiteral(": unit art is blank")));
        // One very different figure per race must not collapse to the same picture.
        const QImage necro = unit("bone_acolyte"), giant = unit("young_giant"), small = unit("halfling_sling");
        QVERIFY2(difference(necro, giant).first > 1.0, "necromancer and giant unit art must differ");
        QVERIFY2(difference(giant, small).first > 1.0, "giant and halfling unit art must differ");
        QVERIFY2(difference(necro, small).first > 1.0, "necromancer and halfling unit art must differ");
        // The elite ranks add a crown to their predecessor's shape.
        for (const auto& [base, elite] : {std::pair{QString("storm_giant"), QString("storm_king")},
                                          std::pair{QString("clan_hero"), QString("clan_legend")},
                                          std::pair{QString("bone_titan"), QString("bone_overlord")}})
            QVERIFY2(difference(unit(base), unit(elite)).first > 0.2,
                     qPrintable(QStringLiteral("%1 and its elite %2 must differ").arg(base, elite)));
    }

    // The six new hero classes have distinct portraits and rider pictures.
    void newClassesHaveTheirOwnPortraits() {
        const QStringList classes = {"boneweaver", "grave_knight", "stone_caller", "sky_shaman", "sheriff", "burrow_seer"};
        const auto portrait = [](const QString& cls) {
            return renderArt([&](QPainter& p, const QRectF& r) {
                artgen::drawPortrait(p, r, cls, QColor("#405060"), 1);
            });
        };
        for (const QString& cls : classes)
            QVERIFY2(inkPixels(portrait(cls)) > 300, qPrintable(cls + QStringLiteral(": portrait is blank")));
        const QImage bone = portrait("boneweaver"), stone = portrait("stone_caller"), sheriff = portrait("sheriff");
        QVERIFY2(difference(bone, stone).first > 0.5, "boneweaver and stone_caller portraits must differ");
        QVERIFY2(difference(stone, sheriff).first > 0.5, "stone_caller and sheriff portraits must differ");
        // Riders of the new classes also get their own look.
        const auto rider = [](const QString& cls) {
            return renderArt([&](QPainter& p, const QRectF& r) { artgen::drawHeroRider(p, r, QColor("#405060"), cls); });
        };
        QVERIFY2(inkPixels(rider("sky_shaman")) > 300, "sky_shaman rider art is blank");
        QVERIFY2(difference(rider("boneweaver"), rider("sheriff")).first > 0.3, "new class riders must differ");
    }

    // The three new races have their own town and scene art.
    void newRacesHaveTheirOwnTownArt() {
        const auto town = [](const QString& race) {
            return renderArt([&](QPainter& p, const QRectF& r) {
                artgen::drawTownIcon(p, r, art::TownLook{QColor("#c04040"), race, 2, 6});
            });
        };
        const QImage necro = town("necroman"), giant = town("giants"), small = town("halflings");
        QVERIFY2(inkPixels(necro) > 300 && inkPixels(giant) > 300 && inkPixels(small) > 300,
                 "new race town art is blank");
        QVERIFY2(difference(necro, giant).first > 1.0, "necroman and giants towns must differ");
        QVERIFY2(difference(giant, small).first > 1.0, "giants and halflings towns must differ");
        QVERIFY2(difference(necro, small).first > 1.0, "necroman and halflings towns must differ");
        const auto scene = [](const QString& race) {
            return renderArt([&](QPainter& p, const QRectF& r) { artgen::drawSceneBackdrop(p, r, race, false); });
        };
        const QImage sn = scene("necroman"), sg = scene("giants"), sh = scene("halflings");
        QVERIFY2(difference(sn, sg).first > 1.0, "necroman and giants scenes must differ");
        QVERIFY2(difference(sg, sh).first > 1.0, "giants and halflings scenes must differ");
    }
};

QTEST_MAIN(TstArt)
#include "tst_art.moc"
