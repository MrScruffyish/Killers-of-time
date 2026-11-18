#include "client/SpellBook.h"

#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
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

QString SpellBook::targetText(const core::SpellDef& spell) {
    switch (spell.target) {
    case core::SpellDef::Target::Enemy: return tr("Enemy stack, any distance");
    case core::SpellDef::Target::Ally: return tr("Your stack, any distance");
    case core::SpellDef::Target::None: break;
    }
    return tr("Any stack, any distance");
}

SpellBook::SpellBook(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent)
    : QDialog(parent) {
    setObjectName("spellBook");
    setWindowTitle(title);
    setMinimumSize(760, 520);
    // An open book: two lit parchment pages with a dark spine, on a dark desk.
    // The local rules override the game's global button/label colours inside here.
    setStyleSheet(QStringLiteral(
        "QDialog#spellBook { background: #241a10; }"
        "QLabel#spellBookTitle { color: #f3e4bb; background: transparent; }"
        "QFrame#spellBookSpine { border: none; background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        " stop:0 #1a1008, stop:0.4 #7a5c2c, stop:0.6 #7a5c2c, stop:1 #1a1008); }"
        "QFrame#spellBookLeftPage, QFrame#spellBookRightPage { border: 1px solid #6a5028;"
        " background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #c8ad78, stop:0.08 #f0e1ba,"
        " stop:0.92 #f0e1ba, stop:1 #c8ad78); }"
        "QScrollArea#spellBookScroll { background: transparent; border: none; }"
        "QFrame#spellBookLeftPage QLabel, QFrame#spellBookRightPage QLabel { color: #2b1a08; background: transparent; }"
        "QPushButton[spellCard=\"true\"] { background: rgba(255,255,255,0.3); border: 1px solid #a98a52;"
        " border-radius: 5px; color: #2b1a08; text-align: left; padding: 4px; }"
        "QPushButton[spellCard=\"true\"]:disabled { color: #7a6a52; background: rgba(180,170,150,0.25); }"
        "QPushButton#spellBookClose { background-color: #5a3d22; color: #f6e7c1; border: 1px solid #9a7a3a;"
        " border-radius: 6px; padding: 6px 16px; }"));

    auto* v = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("<b style='font-size:13pt'>%1</b>").arg(title.toHtmlEscaped()), this);
    heading->setObjectName("spellBookTitle");
    heading->setAlignment(Qt::AlignCenter);
    v->addWidget(heading);

    auto* book = new QHBoxLayout();
    book->setSpacing(0);
    const auto makePage = [this, book](const char* name, QVBoxLayout*& layout) {
        auto* scroll = new QScrollArea(this);
        scroll->setObjectName("spellBookScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QFrame(scroll);
        page->setObjectName(QString::fromLatin1(name));
        layout = new QVBoxLayout(page);
        layout->setAlignment(Qt::AlignTop);
        scroll->setWidget(page);
        book->addWidget(scroll);
    };
    QVBoxLayout *left = nullptr, *right = nullptr;
    makePage("spellBookLeftPage", left);
    auto* spine = new QFrame(this);
    spine->setObjectName("spellBookSpine");
    spine->setFixedWidth(18);
    book->addWidget(spine);
    makePage("spellBookRightPage", right);

    // Split the spells across the two pages, keeping the cards in data order.
    int count = 0;
    for (const Entry& e : spells)
        if (content->spell(e.id)) ++count;
    int index = 0;
    const int mid = (count + 1) / 2;
    for (const Entry& e : spells) {
        const core::SpellDef* s = content->spell(e.id);
        if (!s) continue;
        auto* card = new QPushButton();
        card->setObjectName(QStringLiteral("spell_%1").arg(s->key));
        card->setProperty("spellCard", true);
        // A button sizes itself from its own (empty) text, not from the layout inside
        // it. Without a minimum height the card collapses to about 13 px and only a
        // thin strip of its 112 px illustration is painted.
        card->setMinimumHeight(132);
        card->setEnabled(e.castable);
        if (!e.castable && !e.why.isEmpty()) card->setToolTip(e.why);

        auto* row = new QHBoxLayout(card);
        row->setSpacing(10);
        auto* illustration = new QLabel(card);
        illustration->setObjectName(QStringLiteral("spellIllustration_%1").arg(s->key));
        illustration->setFixedSize(112, 112);
        const QString path = art::path::spell(s->key);
        // The picture is a replaceable PNG in data/art/spell; a school rune stands
        // in until one is supplied.
        illustration->setPixmap(art::hasPicture(path) ? art::spellIcon(s->key, 112) : schoolRune(s->school, 112));
        auto* text = new QLabel(card);
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setText(QStringLiteral("<b>%1</b> <i>(%2 SP, level %3)</i><br>%4<br>%5")
                          .arg(s->name.toHtmlEscaped()).arg(s->cost).arg(s->level)
                          .arg(targetText(*s).toHtmlEscaped(), s->description.toHtmlEscaped()));
        row->addWidget(illustration);
        row->addWidget(text, 1);

        const core::SpellId id = e.id;
        connect(card, &QPushButton::clicked, this, [this, id]() {
            m_chosen = id;
            accept();
        });
        (index < mid ? left : right)->addWidget(card);
        ++index;
    }
    v->addLayout(book, 1);

    auto* cancel = new QPushButton(tr("Close the book"), this);
    cancel->setObjectName("spellBookClose");
    cancel->setMinimumHeight(34);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    v->addWidget(cancel, 0, Qt::AlignRight);
}

core::SpellId SpellBook::chooseSpell(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent) {
    SpellBook book(content, spells, title, parent);
    return book.exec() == QDialog::Accepted ? book.m_chosen : core::SpellId();
}

}  // namespace kot::client
