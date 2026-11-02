#include "client/Art.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <cmath>

namespace kot::client::art {

namespace {

// A small deterministic random sequence (art only; the game rules never use it).
struct Dice {
    quint32 state;
    explicit Dice(quint32 seed) : state(seed * 2654435761u + 1) {}
    double next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state % 10000) / 10000.0;
    }
};

void grassTufts(QPainter& p, int s, Dice& d, const QColor& c, int count) {
    p.setPen(QPen(c.darker(135), std::max(1.0, s / 24.0)));
    for (int i = 0; i < count; ++i) {
        const double x = d.next() * s, y = d.next() * s, h = s * (0.08 + 0.06 * d.next());
        p.drawLine(QPointF(x, y), QPointF(x - h * 0.3, y - h));
        p.drawLine(QPointF(x, y), QPointF(x + h * 0.3, y - h));
    }
}

void speckles(QPainter& p, int s, Dice& d, const QColor& c, int count, double size) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < count; ++i) p.drawEllipse(QPointF(d.next() * s, d.next() * s), s * size, s * size);
}

QPixmap makeTile(const QString& id, const QColor& base, int s, int variant) {
    QPixmap pm(s, s);
    pm.fill(base);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    Dice d(qHash(id) + variant * 7919);
    // Soft light from the top left.
    QLinearGradient shade(0, 0, s, s);
    shade.setColorAt(0, QColor(255, 255, 255, 18));
    shade.setColorAt(1, QColor(0, 0, 0, 22));
    p.fillRect(pm.rect(), shade);
    if (s < 8) return pm;

    if (id == "grass") {
        speckles(p, s, d, base.lighter(115), 5, 0.05);
        grassTufts(p, s, d, base, 4);
    } else if (id == "dirt" || id == "road") {
        speckles(p, s, d, base.darker(120), id == "road" ? 3 : 7, 0.04);
        speckles(p, s, d, base.lighter(115), 4, 0.03);
    } else if (id == "sand") {
        p.setPen(QPen(base.darker(112), std::max(1.0, s / 20.0)));
        for (int i = 0; i < 3; ++i) {
            const double y = s * (0.25 + 0.25 * i) + d.next() * s * 0.08;
            QPainterPath dune;
            dune.moveTo(0, y);
            dune.quadTo(s * 0.5, y - s * 0.12, s, y);
            p.drawPath(dune);
        }
    } else if (id == "forest") {
        speckles(p, s, d, base.darker(130), 4, 0.07);
        for (int i = 0; i < 3; ++i) {  // small conifers
            const double x = s * (0.2 + 0.3 * i) + (d.next() - 0.5) * s * 0.12;
            const double y = s * (0.35 + 0.35 * d.next());
            const double h = s * 0.42, w = s * 0.2;
            QPainterPath tree;
            tree.moveTo(x, y - h * 0.6);
            tree.lineTo(x + w, y + h * 0.3);
            tree.lineTo(x - w, y + h * 0.3);
            tree.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(base.darker(150));
            p.drawPath(tree.translated(s * 0.03, s * 0.03));
            p.setBrush(base.lighter(125));
            p.drawPath(tree);
            p.setBrush(QColor("#5b3a1e"));
            p.drawRect(QRectF(x - s * 0.025, y + h * 0.3, s * 0.05, s * 0.08));
        }
    } else if (id == "hills") {
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 2; ++i) {
            const QRectF hill(s * (0.05 + 0.45 * i), s * (0.35 + 0.2 * d.next()), s * 0.55, s * 0.5);
            p.setBrush(base.lighter(118));
            p.drawChord(hill, 0, 180 * 16);
            p.setBrush(base.darker(118));
            p.drawChord(hill.adjusted(hill.width() * 0.5, 0, 0, 0), 0, 90 * 16);
        }
    } else if (id == "mountains") {
        p.setPen(Qt::NoPen);
        const double peak = s * (0.08 + 0.1 * d.next());
        QPainterPath m;
        m.moveTo(s * 0.05, s * 0.92);
        m.lineTo(s * 0.5, peak);
        m.lineTo(s * 0.95, s * 0.92);
        m.closeSubpath();
        p.setBrush(base.darker(125));
        p.drawPath(m);
        QPainterPath lit;
        lit.moveTo(s * 0.05, s * 0.92);
        lit.lineTo(s * 0.5, peak);
        lit.lineTo(s * 0.45, s * 0.92);
        lit.closeSubpath();
        p.setBrush(base.lighter(125));
        p.drawPath(lit);
        QPainterPath cap;  // snow cap
        cap.moveTo(s * 0.5, peak);
        cap.lineTo(s * 0.62, peak + s * 0.2);
        cap.lineTo(s * 0.5, peak + s * 0.15);
        cap.lineTo(s * 0.38, peak + s * 0.2);
        cap.closeSubpath();
        p.setBrush(QColor("#f4f6f8"));
        p.drawPath(cap);
    } else if (id == "swamp") {
        speckles(p, s, d, QColor("#3c5a6e"), 3, 0.09);
        grassTufts(p, s, d, base.lighter(130), 5);
    } else if (id == "snow") {
        speckles(p, s, d, QColor("#d0dbe6"), 6, 0.05);
    } else if (id == "sea" || id == "river") {
        p.setPen(QPen(base.lighter(135), std::max(1.0, s / 22.0)));
        for (int i = 0; i < 2; ++i) {
            const double x = d.next() * s * 0.6, y = s * (0.3 + 0.4 * i) + d.next() * s * 0.1;
            QPainterPath wave;
            wave.moveTo(x, y);
            wave.quadTo(x + s * 0.1, y - s * 0.08, x + s * 0.2, y);
            wave.quadTo(x + s * 0.3, y + s * 0.08, x + s * 0.4, y);
            p.drawPath(wave);
        }
    } else if (id == "lava") {
        speckles(p, s, d, QColor("#ffb000"), 4, 0.07);
        speckles(p, s, d, base.darker(150), 4, 0.06);
    } else {
        speckles(p, s, d, base.darker(115), 5, 0.04);
    }
    return pm;
}

void shadow(QPainter& p, const QRectF& r) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(r.left() + r.width() * 0.1, r.bottom() - r.height() * 0.18, r.width() * 0.8, r.height() * 0.2));
}

}  // namespace

int variantFor(int x, int y) {
    quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u;
    return int((h >> 7) % 4);
}

const QPixmap& terrainTile(const QString& id, const QColor& base, int size, int variant) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(id, base.name()).arg(size).arg(variant);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.insert(key, makeTile(id, base, size, variant));
    return it.value();
}

void drawTown(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    const QColor stone("#c9c0ae");
    p.setPen(QPen(QColor("#3a3328"), std::max(1.0, w / 18)));
    // Walls and three towers.
    p.setBrush(stone);
    p.drawRect(QRectF(r.left() + w * 0.12, r.top() + h * 0.45, w * 0.76, h * 0.45));
    for (const double x : {0.08, 0.40, 0.72}) {
        const QRectF tower(r.left() + w * x, r.top() + h * (x == 0.40 ? 0.18 : 0.3), w * 0.2, h * (x == 0.40 ? 0.72 : 0.6));
        p.setBrush(stone.lighter(108));
        p.drawRect(tower);
        QPainterPath roof;
        roof.moveTo(tower.left() - w * 0.03, tower.top());
        roof.lineTo(tower.center().x(), tower.top() - h * 0.18);
        roof.lineTo(tower.right() + w * 0.03, tower.top());
        roof.closeSubpath();
        p.setBrush(owner);
        p.drawPath(roof);
    }
    p.setBrush(QColor("#4a3520"));  // gate
    p.drawChord(QRectF(r.left() + w * 0.4, r.top() + h * 0.62, w * 0.2, h * 0.5), 0, 180 * 16);
    p.restore();
}

void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#2b2b2b"), std::max(1.0, w / 18)));
    p.setBrush(QColor("#7a6a55"));  // rock heap
    p.drawChord(QRectF(r.left() + w * 0.05, r.top() + h * 0.25, w * 0.9, h * 1.2), 0, 180 * 16);
    p.setBrush(QColor("#1d1610"));  // entrance
    p.drawChord(QRectF(r.left() + w * 0.33, r.top() + h * 0.5, w * 0.34, h * 0.7), 0, 180 * 16);
    p.setBrush(resource);  // the resource it gives
    p.drawEllipse(QRectF(r.left() + w * 0.66, r.top() + h * 0.08, w * 0.28, h * 0.28));
    p.setPen(QPen(owner, std::max(1.5, w / 10)));  // owner flag
    p.drawLine(QPointF(r.left() + w * 0.15, r.top() + h * 0.55), QPointF(r.left() + w * 0.15, r.top() + h * 0.05));
    p.setPen(Qt::NoPen);
    p.setBrush(owner);
    p.drawRect(QRectF(r.left() + w * 0.15, r.top() + h * 0.05, w * 0.22, h * 0.15));
    p.restore();
}

void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#3a2a18"), std::max(1.0, w / 18)));
    p.setBrush(QColor("#a8864f"));  // hut
    p.drawRect(QRectF(r.left() + w * 0.2, r.top() + h * 0.5, w * 0.6, h * 0.4));
    QPainterPath roof;
    roof.moveTo(r.left() + w * 0.1, r.top() + h * 0.52);
    roof.lineTo(r.center().x(), r.top() + h * 0.12);
    roof.lineTo(r.right() - w * 0.1, r.top() + h * 0.52);
    roof.closeSubpath();
    p.setBrush(owner);
    p.drawPath(roof);
    p.setBrush(QColor("#2a1d10"));
    p.drawRect(QRectF(r.center().x() - w * 0.08, r.top() + h * 0.65, w * 0.16, h * 0.25));
    p.restore();
}

void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    if (selected) {
        p.setPen(QPen(QColor("#ffd84a"), std::max(2.0, w / 8)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(-w * 0.06, -h * 0.06, w * 0.06, h * 0.06));
    }
    // A banner on a pole.
    p.setPen(QPen(QColor("#3b2a17"), std::max(1.5, w / 10)));
    p.drawLine(QPointF(r.left() + w * 0.3, r.bottom() - h * 0.05), QPointF(r.left() + w * 0.3, r.top() + h * 0.05));
    QPainterPath flag;
    flag.moveTo(r.left() + w * 0.32, r.top() + h * 0.08);
    flag.lineTo(r.right() - w * 0.05, r.top() + h * 0.2);
    flag.lineTo(r.left() + w * 0.62, r.top() + h * 0.33);
    flag.lineTo(r.right() - w * 0.05, r.top() + h * 0.46);
    flag.lineTo(r.left() + w * 0.32, r.top() + h * 0.56);
    flag.closeSubpath();
    p.setPen(QPen(owner.darker(160), std::max(1.0, w / 20)));
    p.setBrush(owner);
    p.drawPath(flag);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffd84a"));
    p.drawEllipse(QPointF(r.left() + w * 0.3, r.top() + h * 0.05), w * 0.07, w * 0.07);
    p.restore();
}

void drawMonster(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#2a0a0a"), std::max(1.0, w / 16)));
    p.setBrush(QColor("#8b1a1a"));
    p.drawEllipse(QRectF(r.left() + w * 0.12, r.top() + h * 0.2, w * 0.76, h * 0.7));
    p.setBrush(QColor("#e8d9a8"));  // horns
    for (const double side : {-1.0, 1.0}) {
        QPainterPath horn;
        const double x = r.center().x() + side * w * 0.22;
        horn.moveTo(x - w * 0.07, r.top() + h * 0.3);
        horn.lineTo(x + side * w * 0.12, r.top() + h * 0.02);
        horn.lineTo(x + w * 0.07, r.top() + h * 0.3);
        horn.closeSubpath();
        p.drawPath(horn);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffe14a"));  // eyes
    p.drawEllipse(QPointF(r.center().x() - w * 0.13, r.top() + h * 0.5), w * 0.07, w * 0.07);
    p.drawEllipse(QPointF(r.center().x() + w * 0.13, r.top() + h * 0.5), w * 0.07, w * 0.07);
    p.restore();
}

void drawChest(QPainter& p, const QRectF& r, const QColor& resource) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    const QRectF box(r.left() + w * 0.15, r.top() + h * 0.4, w * 0.7, h * 0.45);
    p.setPen(QPen(QColor("#3a2410"), std::max(1.0, w / 16)));
    p.setBrush(QColor("#8a5a2b"));
    p.drawRect(box);
    p.setBrush(resource);  // what is inside, spilling out
    p.drawChord(QRectF(box.left(), box.top() - h * 0.22, box.width(), h * 0.44), 0, 180 * 16);
    p.setBrush(QColor("#d4a017"));
    p.drawRect(QRectF(box.center().x() - w * 0.05, box.top(), w * 0.1, h * 0.14));
    p.restore();
}

void drawArtifact(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = r.center();
    QRadialGradient glow(c, r.width() * 0.55);
    glow.setColorAt(0, QColor(255, 235, 140, 200));
    glow.setColorAt(1, QColor(255, 235, 140, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(c, r.width() * 0.55, r.height() * 0.55);
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const double a = M_PI / 5 * i - M_PI / 2;
        const double rad = (i % 2 ? 0.18 : 0.42) * r.width();
        star << QPointF(c.x() + rad * std::cos(a), c.y() + rad * std::sin(a));
    }
    p.setPen(QPen(QColor("#4a1a6a"), std::max(1.0, r.width() / 18)));
    p.setBrush(QColor("#c070f0"));
    p.drawPolygon(star);
    p.restore();
}

void drawUnitToken(QPainter& p, const QPointF& c, double rad, const QColor& side, const QString& name, int count,
                   int stars, bool active, bool target) {
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
    if (rad >= 10) {
        QFont f = p.font();
        f.setPixelSize(std::max(7, int(rad * 0.36)));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(c.x() - rad, c.y() - rad * 0.7, 2 * rad, rad * 0.8), Qt::AlignCenter, name.left(8));
        // The count on a small plate.
        const QRectF plate(c.x() - rad * 0.55, c.y() + rad * 0.15, rad * 1.1, rad * 0.55);
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

QString styleSheet() {
    return QStringLiteral(R"(
QWidget { background-color: #2b2118; color: #eadfc8; font-size: 10pt; }
QLabel { background: transparent; }
QPushButton {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6b4a2b, stop:1 #4a3119);
    border: 1px solid #c9a24a; border-radius: 5px; padding: 5px 10px; color: #f6e7c1;
}
QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #85603a, stop:1 #5b3d20); }
QPushButton:pressed { background-color: #3a2614; }
QPushButton:disabled { color: #8a7b63; border-color: #5e5030; background-color: #3a2c1e; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QListWidget, QTableWidget {
    background-color: #1d1610; border: 1px solid #7a6235; border-radius: 3px; color: #eadfc8;
    selection-background-color: #8a6a2a;
}
QComboBox QAbstractItemView { background-color: #1d1610; color: #eadfc8; }
QTabWidget::pane { border: 1px solid #7a6235; }
QTabBar::tab { background: #3a2c1e; border: 1px solid #7a6235; padding: 5px 12px; color: #d8c8a8; }
QTabBar::tab:selected { background: #5b4025; color: #ffe9b0; }
QHeaderView::section { background-color: #3a2c1e; color: #f0d898; border: 1px solid #5e4a2a; padding: 3px; }
QScrollBar:vertical, QScrollBar:horizontal { background: #1d1610; }
QScrollBar::handle { background: #6b4a2b; border-radius: 4px; }
QToolTip { background-color: #f1e4c0; color: #20180c; border: 1px solid #7a6235; }
QCheckBox::indicator { width: 14px; height: 14px; }
QDialog, QMessageBox { background-color: #2b2118; }
)");
}

}  // namespace kot::client::art
