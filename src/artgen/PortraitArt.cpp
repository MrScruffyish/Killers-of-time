// Hero portraits drawn in code: a head and shoulders in the owner's colour with
// headgear by class (knight, wizard, death knight, necromancer, ranger, druid;
// others get a plain helmet), on a round plate.

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "artgen/ArtGen.h"

namespace kot::artgen {

void drawPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const QColor ink(25, 16, 8);
    const QPen pen(ink, std::max(1.0, w / 60));
    // Background plate.
    QRadialGradient bg(QPointF(X(0.4), Y(0.3)), w * 0.75);
    bg.setColorAt(0, owner.lighter(180));
    bg.setColorAt(0.55, owner);
    bg.setColorAt(1, owner.darker(190));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, w * 0.12, w * 0.12);
    p.setPen(QPen(QColor(255, 255, 255, 40), std::max(1.0, w / 60)));  // faint rays behind the head
    for (int i = 0; i < 12; ++i) {
        const double a = M_PI * 2 * i / 12;
        p.drawLine(QPointF(X(0.5), Y(0.42)), QPointF(X(0.5) + std::cos(a) * w * 0.7, Y(0.42) + std::sin(a) * h * 0.7));
    }
    p.setPen(Qt::NoPen);
    const bool undead = classId == "death_knight" || classId == "necromancer";
    const bool orc = classId == "warlord" || classId == "shaman_chief";
    const bool ash = classId == "hellknight" || classId == "pyromancer";
    const bool tide = classId == "captain" || classId == "tidecaller";
    const QColor skin = ash ? QColor("#c8483a") : tide && seed % 2 ? QColor("#8ac8c0") : orc ? QColor("#7aa04a") : undead ? QColor("#c8d0c0") : (seed % 3 == 0 ? QColor("#e8b890") : seed % 3 == 1 ? QColor("#f0c8a0") : QColor("#b07850"));
    // Shoulders in the class's clothes.
    QColor coat = owner.darker(120);
    if (classId == "wizard") coat = QColor("#3a4aa0");
    if (classId == "necromancer") coat = QColor("#2a2030");
    if (classId == "ranger") coat = QColor("#3e6a30");
    if (classId == "druid") coat = QColor("#6a5030");
    if (classId == "pyromancer") coat = QColor("#5a1a1a");
    if (classId == "hellknight") coat = QColor("#2a1a1a");
    if (classId == "captain") coat = QColor("#1a2a4a");
    if (classId == "tidecaller") coat = QColor("#2a6a8a");
    p.setPen(pen);
    QLinearGradient coatShade(QPointF(X(0.1), Y(0.7)), QPointF(X(0.9), Y(1.0)));
    coatShade.setColorAt(0, coat.lighter(135));
    coatShade.setColorAt(1, coat.darker(140));
    p.setBrush(coatShade);
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
    // Neck (shadowed under the chin), ears and the head, lit from the left.
    p.setBrush(skin.darker(125));
    p.drawRect(QRectF(X(0.42), Y(0.55), w * 0.16, h * 0.16));
    p.setBrush(skin.darker(108));
    p.drawEllipse(QRectF(X(0.27), Y(0.38), w * 0.07, h * 0.11));
    p.drawEllipse(QRectF(X(0.66), Y(0.38), w * 0.07, h * 0.11));
    QRadialGradient face(QPointF(X(0.42), Y(0.36)), w * 0.32);
    face.setColorAt(0, skin.lighter(115));
    face.setColorAt(0.7, skin);
    face.setColorAt(1, skin.darker(125));
    p.setBrush(face);
    p.drawEllipse(QRectF(X(0.3), Y(0.22), w * 0.4, h * 0.42));
    // Cheeks, eyes with a glint, brows, a nose and the mouth.
    p.setPen(Qt::NoPen);
    if (!undead) {
        p.setBrush(QColor(220, 80, 60, 45));
        p.drawEllipse(QPointF(X(0.38), Y(0.49)), w * 0.045, h * 0.03);
        p.drawEllipse(QPointF(X(0.62), Y(0.49)), w * 0.045, h * 0.03);
    }
    const QColor iris = undead ? QColor("#60ff90") : ash ? QColor("#ffd040") : (seed % 3 == 0 ? QColor("#3a6a9a") : seed % 3 == 1 ? QColor("#5a3a1e") : QColor("#3a7a4a"));
    for (const double ex : {0.415, 0.585}) {
        p.setBrush(undead || ash ? iris.darker(200) : QColor(250, 248, 240));
        p.drawEllipse(QPointF(X(ex), Y(0.42)), w * 0.045, h * 0.03);
        p.setBrush(iris);
        p.drawEllipse(QPointF(X(ex + 0.005), Y(0.42)), w * 0.025, h * 0.027);
        p.setBrush(ink);
        p.drawEllipse(QPointF(X(ex + 0.005), Y(0.42)), w * 0.011, h * 0.012);
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawEllipse(QPointF(X(ex + 0.014), Y(0.41)), w * 0.007, h * 0.007);
    }
    p.setPen(QPen(ink, std::max(1.0, w / 60), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(X(0.37), Y(0.375)), QPointF(X(0.45), Y(0.365)));  // brows
    p.drawLine(QPointF(X(0.55), Y(0.365)), QPointF(X(0.63), Y(0.375)));
    p.setPen(QPen(skin.darker(150), std::max(1.0, w / 90), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(X(0.5), Y(0.43)), QPointF(X(0.485), Y(0.49)));  // the nose
    p.drawLine(QPointF(X(0.485), Y(0.49)), QPointF(X(0.51), Y(0.495)));
    p.setPen(QPen(QColor(120, 40, 30), std::max(1.0, w / 70), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(X(0.44), Y(0.5), w * 0.12, h * 0.06), 200 * 16, 140 * 16);
    // A beard for some.
    if (classId == "thane" || classId == "runesmith") {  // a great dwarf beard
        p.setPen(pen);
        p.setBrush(QColor("#c86a2a"));
        QPainterPath beard;
        beard.moveTo(X(0.3), Y(0.44));
        beard.quadTo(X(0.5), Y(0.95), X(0.7), Y(0.44));
        beard.quadTo(X(0.5), Y(0.56), X(0.3), Y(0.44));
        p.drawPath(beard);
    }
    if (orc) {  // tusks
        p.setPen(QPen(ink, std::max(1.0, w / 80)));
        p.setBrush(QColor("#f4f0e0"));
        p.drawEllipse(QRectF(X(0.42), Y(0.5), w * 0.04, h * 0.06));
        p.drawEllipse(QRectF(X(0.54), Y(0.5), w * 0.04, h * 0.06));
    }
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
    if (ash) {  // horns; the pyromancer has a flame over them
        p.setBrush(QColor("#2a1a14"));
        p.drawPolygon(QPolygonF{QPointF(X(0.34), Y(0.3)), QPointF(X(0.2), Y(0.06)), QPointF(X(0.42), Y(0.25))});
        p.drawPolygon(QPolygonF{QPointF(X(0.66), Y(0.3)), QPointF(X(0.8), Y(0.06)), QPointF(X(0.58), Y(0.25))});
        if (classId == "pyromancer") {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#ff8a20"));
            p.drawEllipse(QPointF(X(0.5), Y(0.12)), w * 0.06, h * 0.09);
            p.setBrush(QColor("#ffe060"));
            p.drawEllipse(QPointF(X(0.5), Y(0.14)), w * 0.03, h * 0.05);
        } else {
            p.setBrush(QColor("#3a2a2a"));
            p.drawRect(QRectF(X(0.27), Y(0.33), w * 0.46, h * 0.05));
        }
    } else if (classId == "captain") {  // a tricorn
        p.setBrush(QColor("#1a1a24"));
        p.drawPolygon(QPolygonF{QPointF(X(0.14), Y(0.34)), QPointF(X(0.5), Y(0.06)), QPointF(X(0.86), Y(0.34)), QPointF(X(0.5), Y(0.26))});
        p.setBrush(QColor("#ffd84a"));
        p.drawEllipse(QPointF(X(0.5), Y(0.18)), w * 0.04, w * 0.04);
    } else if (classId == "tidecaller") {  // a crown of shells and coral
        for (int i = 0; i < 5; ++i) {
            p.setBrush(i % 2 ? QColor("#f0a0a0") : QColor("#f4ecd8"));
            p.drawEllipse(QRectF(X(0.28 + i * 0.09), Y(0.18 - (i == 2 ? 0.05 : 0)), w * 0.09, h * 0.11));
        }
    } else if (classId == "wizard") {
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
    } else if (classId == "runesmith" || classId == "shaman_chief") {
        // A hood with a glowing rune (runesmith) or a bone headdress (shaman).
        p.setBrush(classId == "runesmith" ? QColor("#5a4a6a") : QColor("#ece4cc"));
        p.drawChord(QRectF(X(0.26), Y(0.12), w * 0.48, h * 0.34), 0, 180 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(classId == "runesmith" ? QColor("#80c0ff") : QColor("#c03a2a"));
        p.drawEllipse(QPointF(X(0.5), Y(0.2)), w * 0.04, w * 0.04);
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
        const bool dark = classId == "death_knight" || classId == "warlord";
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

namespace {
// A gold bevel around a portrait.
void portraitFrame(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    const double w = r.width();
    p.setPen(QPen(QColor("#5a4010"), w * 0.05));
    p.drawRoundedRect(r.adjusted(w * 0.025, w * 0.025, -w * 0.025, -w * 0.025), w * 0.11, w * 0.11);
    QLinearGradient gold(r.topLeft(), r.bottomRight());
    gold.setColorAt(0, QColor("#fff0a0"));
    gold.setColorAt(0.5, QColor("#d8a830"));
    gold.setColorAt(1, QColor("#8a6010"));
    p.setPen(QPen(QBrush(gold), w * 0.03));
    p.drawRoundedRect(r.adjusted(w * 0.03, w * 0.03, -w * 0.03, -w * 0.03), w * 0.1, w * 0.1);
    p.restore();
}
}  // namespace

void drawFramedPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed) {
    p.save();
    {
        // Inside the frame, clipped to its rounded shape.
        QPainterPath clip;
        clip.addRoundedRect(r, r.width() * 56 / 512, r.height() * 56 / 512);
        p.setClipPath(clip);
        drawPortrait(p, r, classId, owner, seed);
        p.setClipping(false);
    }
    portraitFrame(p, r);
    p.restore();
}

}  // namespace kot::artgen
