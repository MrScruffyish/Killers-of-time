// Hero portraits drawn in code: a head and shoulders in the owner's colour with
// headgear by class (knight, wizard, death knight, necromancer, ranger, druid;
// others get a plain helmet), on a round plate.

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "client/Art.h"

namespace kot::client::art {

void drawPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const QColor ink(25, 16, 8);
    const QPen pen(ink, std::max(1.0, w / 60));
    // Background plate.
    QRadialGradient bg(QPointF(X(0.5), Y(0.4)), w * 0.6);
    bg.setColorAt(0, owner.lighter(170));
    bg.setColorAt(1, owner.darker(160));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, w * 0.12, w * 0.12);
    const bool undead = classId == "death_knight" || classId == "necromancer";
    const QColor skin = undead ? QColor("#c8d0c0") : (seed % 3 == 0 ? QColor("#e8b890") : seed % 3 == 1 ? QColor("#f0c8a0") : QColor("#b07850"));
    // Shoulders in the class's clothes.
    QColor coat = owner.darker(120);
    if (classId == "wizard") coat = QColor("#3a4aa0");
    if (classId == "necromancer") coat = QColor("#2a2030");
    if (classId == "ranger") coat = QColor("#3e6a30");
    if (classId == "druid") coat = QColor("#6a5030");
    p.setPen(pen);
    p.setBrush(coat);
    QPainterPath shoulders;
    shoulders.moveTo(X(0.08), Y(1.0));
    shoulders.quadTo(X(0.12), Y(0.7), X(0.5), Y(0.68));
    shoulders.quadTo(X(0.88), Y(0.7), X(0.92), Y(1.0));
    shoulders.closeSubpath();
    p.drawPath(shoulders);
    if (classId == "knight" || classId == "death_knight") {  // pauldrons
        p.setBrush(classId == "knight" ? QColor("#c8ccd8") : QColor("#4a4650"));
        p.drawEllipse(QRectF(X(0.06), Y(0.7), w * 0.3, h * 0.2));
        p.drawEllipse(QRectF(X(0.64), Y(0.7), w * 0.3, h * 0.2));
    }
    // Neck and head.
    p.setBrush(skin);
    p.drawRect(QRectF(X(0.42), Y(0.55), w * 0.16, h * 0.16));
    p.drawEllipse(QRectF(X(0.3), Y(0.22), w * 0.4, h * 0.42));
    // Eyes and mouth.
    p.setPen(Qt::NoPen);
    p.setBrush(undead ? QColor("#60ff90") : ink);
    p.drawEllipse(QPointF(X(0.42), Y(0.42)), w * 0.025, h * 0.025);
    p.drawEllipse(QPointF(X(0.58), Y(0.42)), w * 0.025, h * 0.025);
    p.setPen(QPen(ink, std::max(1.0, w / 80)));
    p.drawLine(QPointF(X(0.45), Y(0.54)), QPointF(X(0.55), Y(0.54)));
    // A beard for some.
    if (classId == "wizard" || classId == "druid" || (classId == "knight" && seed % 2)) {
        p.setPen(pen);
        p.setBrush(classId == "wizard" ? QColor("#e8e8f0") : QColor("#7a5a30"));
        QPainterPath beard;
        beard.moveTo(X(0.33), Y(0.48));
        beard.quadTo(X(0.5), Y(classId == "wizard" ? 0.86 : 0.7), X(0.67), Y(0.48));
        beard.quadTo(X(0.5), Y(0.58), X(0.33), Y(0.48));
        p.drawPath(beard);
    }
    // Headgear.
    p.setPen(pen);
    if (classId == "wizard") {
        p.setBrush(QColor("#3a4aa0"));
        p.drawPolygon(QPolygonF{QPointF(X(0.22), Y(0.3)), QPointF(X(0.62), Y(-0.02)), QPointF(X(0.78), Y(0.3))});
        p.setBrush(QColor("#2a3a80"));
        p.drawEllipse(QRectF(X(0.18), Y(0.26), w * 0.64, h * 0.08));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffe27a"));
        p.drawEllipse(QPointF(X(0.52), Y(0.16)), w * 0.025, w * 0.025);
    } else if (classId == "necromancer") {
        p.setBrush(QColor("#2a2030"));
        QPainterPath hood;
        hood.moveTo(X(0.22), Y(0.62));
        hood.quadTo(X(0.18), Y(0.08), X(0.5), Y(0.08));
        hood.quadTo(X(0.82), Y(0.08), X(0.78), Y(0.62));
        hood.quadTo(X(0.7), Y(0.3), X(0.5), Y(0.3));
        hood.quadTo(X(0.3), Y(0.3), X(0.22), Y(0.62));
        p.drawPath(hood);
    } else if (classId == "ranger") {
        p.setBrush(QColor("#3e6a30"));
        QPainterPath hood;
        hood.moveTo(X(0.26), Y(0.5));
        hood.quadTo(X(0.24), Y(0.12), X(0.5), Y(0.1));
        hood.quadTo(X(0.76), Y(0.12), X(0.74), Y(0.5));
        hood.quadTo(X(0.66), Y(0.28), X(0.5), Y(0.27));
        hood.quadTo(X(0.34), Y(0.28), X(0.26), Y(0.5));
        p.drawPath(hood);
    } else if (classId == "druid") {
        // A crown of leaves.
        for (int i = 0; i < 7; ++i) {
            const double a = M_PI * (0.1 + 0.8 * i / 6.0);
            p.save();
            p.translate(X(0.5) - std::cos(a) * w * 0.22, Y(0.3) - std::sin(a) * h * 0.1);
            p.rotate(-90 + (a * 180 / M_PI - 90) * -1);
            p.setBrush(i % 2 ? QColor("#5aa84a") : QColor("#3e8a3a"));
            p.drawEllipse(QRectF(-w * 0.04, -h * 0.09, w * 0.08, h * 0.16));
            p.restore();
        }
    } else {
        // Helmets: steel for knights, black with a skull look for death knights.
        const bool dark = classId == "death_knight";
        p.setBrush(dark ? QColor("#3a3640") : QColor("#c8ccd8"));
        p.drawChord(QRectF(X(0.27), Y(0.16), w * 0.46, h * 0.36), 0, 180 * 16);
        p.drawRect(QRectF(X(0.27), Y(0.33), w * 0.46, h * 0.05));
        p.setBrush(dark ? QColor("#8a1a1a") : owner);  // crest
        p.drawPolygon(QPolygonF{QPointF(X(0.47), Y(0.17)), QPointF(X(0.5), Y(0.02)), QPointF(X(0.53), Y(0.17))});
        if (dark) {
            p.setBrush(QColor("#ece4cc"));  // horns
            p.drawPolygon(QPolygonF{QPointF(X(0.3), Y(0.26)), QPointF(X(0.18), Y(0.08)), QPointF(X(0.36), Y(0.2))});
            p.drawPolygon(QPolygonF{QPointF(X(0.7), Y(0.26)), QPointF(X(0.82), Y(0.08)), QPointF(X(0.64), Y(0.2))});
        }
    }
    // A thin gold frame.
    p.setPen(QPen(QColor("#c9a24a"), std::max(1.5, w / 40)));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(w * 0.01, h * 0.01, -w * 0.01, -h * 0.01), w * 0.12, w * 0.12);
    p.restore();
}

const QPixmap& portrait(const QString& classId, const QColor& owner, int seed, int size) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(classId, owner.name()).arg(seed % 6).arg(size);
    auto it = cache.find(key);
    if (it == cache.end()) {
        QPixmap master(256, 256);
        master.fill(Qt::transparent);
        QPainter p(&master);
        drawPortrait(p, QRectF(0, 0, 256, 256), classId, owner, seed);
        p.end();
        it = cache.insert(key, master.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    return it.value();
}

}  // namespace kot::client::art
