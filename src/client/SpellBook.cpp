#include "client/SpellBook.h"

#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

QPixmap schoolRune(const QString& school, int size) {
    static QHash<QString, QPixmap> cache;
    const QString key = school + QString::number(size);
    if (cache.contains(key)) return cache.value(key);
    static const QHash<QString, QColor> colours = {{"air", QColor("#8ad0ff")}, {"water", QColor("#3a8ae0")}, {"earth", QColor("#b08a4a")},
                                                   {"nature", QColor("#5ab84a")}, {"dark", QColor("#9a4ac8")},
                                                   {"fire", QColor("#e8602a")}};
    const QColor c = colours.value(school, QColor("#ffd84a"));
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;
    QRadialGradient g(QPointF(s * 0.4, s * 0.35), s * 0.6);
    g.setColorAt(0, c.lighter(150));
    g.setColorAt(1, c.darker(160));
    p.setPen(QPen(QColor(25, 16, 8), std::max(1.0, s / 24)));
    p.setBrush(g);
    p.drawEllipse(QRectF(s * 0.05, s * 0.05, s * 0.9, s * 0.9));
    p.setPen(QPen(QColor(255, 250, 230), std::max(1.5, s / 14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto X = [&](double f) { return s * f; };
    if (school == "air") {  // swirls
        p.drawArc(QRectF(X(0.25), X(0.25), X(0.5), X(0.5)), 30 * 16, 270 * 16);
        p.drawArc(QRectF(X(0.38), X(0.38), X(0.24), X(0.24)), 210 * 16, 270 * 16);
    } else if (school == "water") {  // a drop
        QPainterPath drop;
        drop.moveTo(X(0.5), X(0.22));
        drop.quadTo(X(0.75), X(0.55), X(0.5), X(0.75));
        drop.quadTo(X(0.25), X(0.55), X(0.5), X(0.22));
        p.drawPath(drop);
    } else if (school == "earth") {  // a mountain
        p.drawPolyline(QPolygonF{QPointF(X(0.22), X(0.72)), QPointF(X(0.45), X(0.3)), QPointF(X(0.58), X(0.5)),
                                 QPointF(X(0.66), X(0.4)), QPointF(X(0.8), X(0.72))});
    } else if (school == "nature") {  // a leaf
        QPainterPath leaf;
        leaf.moveTo(X(0.3), X(0.72));
        leaf.quadTo(X(0.25), X(0.3), X(0.72), X(0.26));
        leaf.quadTo(X(0.72), X(0.66), X(0.3), X(0.72));
        p.drawPath(leaf);
        p.drawLine(QPointF(X(0.3), X(0.72)), QPointF(X(0.6), X(0.4)));
    } else if (school == "fire") {  // a flame
        QPainterPath flame;
        flame.moveTo(X(0.5), X(0.78));
        flame.cubicTo(X(0.22), X(0.7), X(0.3), X(0.42), X(0.46), X(0.22));
        flame.cubicTo(X(0.5), X(0.4), X(0.62), X(0.4), X(0.6), X(0.3));
        flame.cubicTo(X(0.8), X(0.48), X(0.76), X(0.72), X(0.5), X(0.78));
        p.drawPath(flame);
    } else if (school == "dark") {  // an eye
        p.drawArc(QRectF(X(0.2), X(0.35), X(0.6), X(0.4)), 0, 180 * 16);
        p.drawArc(QRectF(X(0.2), X(0.25), X(0.6), X(0.4)), 180 * 16, 180 * 16);
        p.setBrush(QColor(255, 250, 230));
        p.drawEllipse(QPointF(X(0.5), X(0.5)), X(0.07), X(0.07));
    } else {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = M_PI / 5 * i - M_PI / 2, r = (i % 2 ? 0.12 : 0.28) * s;
            star << QPointF(X(0.5) + r * std::cos(a), X(0.5) + r * std::sin(a));
        }
        p.drawPolygon(star);
    }
    p.end();
    cache.insert(key, pm);
    return pm;
}

SpellBook::SpellBook(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent)
    : QDialog(parent) {
    setObjectName("spellBook");
    setWindowTitle(title);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(QStringLiteral("<b style='font-size:13pt'>%1</b>").arg(title.toHtmlEscaped()), this));
    auto* grid = new QGridLayout();
    int i = 0;
    for (const Entry& e : spells) {
        const core::SpellDef* s = content->spell(e.id);
        if (!s) continue;
        auto* card = new QPushButton(this);
        card->setObjectName(QStringLiteral("spell_%1").arg(e.id));
        card->setIcon(QIcon(art::spellIcon(s->id, s->school, 128)));
        card->setIconSize(QSize(52, 52));
        card->setText(tr("%1   (%2 SP, level %3)\n%4").arg(s->name).arg(s->cost).arg(s->level).arg(s->description));
        card->setStyleSheet("QPushButton { text-align: left; padding: 8px; }");
        card->setMinimumSize(320, 62);
        card->setEnabled(e.castable);
        if (!e.castable && !e.why.isEmpty()) card->setToolTip(e.why);
        const QString id = e.id;
        connect(card, &QPushButton::clicked, this, [this, id]() {
            m_chosen = id;
            accept();
        });
        grid->addWidget(card, i / 2, i % 2);
        ++i;
    }
    v->addLayout(grid);
    auto* cancel = new QPushButton(tr("Close the book"), this);
    cancel->setMinimumHeight(34);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    v->addWidget(cancel, 0, Qt::AlignRight);
}

QString SpellBook::chooseSpell(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent) {
    SpellBook book(content, spells, title, parent);
    return book.exec() == QDialog::Accepted ? book.m_chosen : QString();
}

}  // namespace kot::client
