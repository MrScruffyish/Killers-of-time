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
};

QTEST_MAIN(TstArt)
#include "tst_art.moc"
