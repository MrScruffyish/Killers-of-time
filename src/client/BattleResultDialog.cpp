#include "client/BattleResultDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

namespace {

// The emblem: drawn in code like the rest of the art.
QPixmap emblem(BattleResultDialog::Outcome outcome, int size) {
    using O = BattleResultDialog::Outcome;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;
    const QPointF c(s / 2, s / 2);
    const QColor ink(25, 16, 8);
    // A round shield behind everything.
    QRadialGradient g(c - QPointF(s * 0.12, s * 0.15), s * 0.6);
    const QColor field = outcome == O::Victory ? QColor("#2f6a3a") : outcome == O::Defeat ? QColor("#6a1f1f") : QColor("#55555a");
    g.setColorAt(0, field.lighter(150));
    g.setColorAt(1, field.darker(150));
    p.setPen(QPen(outcome == O::Victory ? QColor("#ffd84a") : QColor("#8a7b63"), s * 0.03));
    p.setBrush(g);
    p.drawEllipse(c, s * 0.36, s * 0.36);
    const auto sword = [&](double angle, bool broken) {
        p.save();
        p.translate(c);
        p.rotate(angle);
        p.setPen(QPen(ink, s * 0.012));
        p.setBrush(QColor("#dfe3ee"));
        QPainterPath blade;
        const double tip = broken ? -s * 0.06 : -s * 0.34;
        blade.moveTo(-s * 0.025, s * 0.12);
        blade.lineTo(-s * 0.025, tip + s * 0.04);
        if (broken) {  // a jagged break
            blade.lineTo(-s * 0.005, tip);
            blade.lineTo(s * 0.01, tip + s * 0.03);
            blade.lineTo(s * 0.025, tip - s * 0.01);
        } else {
            blade.lineTo(0, tip);
        }
        blade.lineTo(s * 0.025, tip + (broken ? 0 : s * 0.04));
        blade.lineTo(s * 0.025, s * 0.12);
        blade.closeSubpath();
        p.drawPath(blade);
        p.setBrush(QColor("#c9a24a"));
        p.drawRect(QRectF(-s * 0.09, s * 0.12, s * 0.18, s * 0.03));  // guard
        p.setBrush(QColor("#6a4a28"));
        p.drawRect(QRectF(-s * 0.02, s * 0.15, s * 0.04, s * 0.1));  // grip
        p.setBrush(QColor("#c9a24a"));
        p.drawEllipse(QPointF(0, s * 0.27), s * 0.03, s * 0.03);
        p.restore();
    };
    if (outcome == O::Victory) {
        // A laurel wreath around crossed swords.
        p.setPen(QPen(QColor("#3a2a08"), s * 0.006));
        for (const int side : {-1, 1})
            for (int i = 0; i < 9; ++i) {
                const double a = M_PI / 2 + side * (0.35 + i * 0.27);
                const QPointF at(c.x() + std::cos(a) * s * 0.43, c.y() + std::sin(a) * s * 0.43);
                p.save();
                p.translate(at);
                p.rotate(a * 180 / M_PI + 90 * side);
                p.setBrush(i % 2 ? QColor("#ffd84a") : QColor("#e0b030"));
                p.drawEllipse(QRectF(-s * 0.025, -s * 0.06, s * 0.05, s * 0.12));
                p.restore();
            }
        sword(-35, false);
        sword(35, false);
        // A star above.
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = M_PI / 5 * i - M_PI / 2;
            const double r = (i % 2 ? 0.03 : 0.07) * s;
            star << QPointF(c.x() + r * std::cos(a), c.y() - s * 0.24 + r * std::sin(a));
        }
        p.setPen(QPen(ink, s * 0.008));
        p.setBrush(QColor("#fff0a0"));
        p.drawPolygon(star);
    } else if (outcome == O::Defeat) {
        sword(20, true);
        // Drops falling from the break, and a torn banner edge.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 30, 30, 200));
        for (int i = 0; i < 3; ++i) p.drawEllipse(QPointF(c.x() + s * (0.08 + i * 0.05), c.y() + s * (0.02 + i * 0.07)), s * 0.015, s * 0.022);
    } else {
        sword(-35, false);
        sword(35, false);
    }
    return pm;
}

}  // namespace

BattleResultDialog::Outcome BattleResultDialog::outcomeFor(const QJsonObject& b, int you) {
    const bool attacker = b["attacker"].toObject()["owner"].toInt() == you;
    const QString outcome = b["outcome"].toString();
    if (outcome == "draw") return Outcome::Draw;
    return outcome == (attacker ? "attacker" : "defender") ? Outcome::Victory : Outcome::Defeat;
}

BattleResultDialog::BattleResultDialog(const core::ContentRegistry* content, const QJsonObject& b, int you, QWidget* parent)
    : QDialog(parent), m_outcome(outcomeFor(b, you)) {
    setObjectName("battleResult");
    setWindowTitle(tr("Battle"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    const bool attacker = b["attacker"].toObject()["owner"].toInt() == you;
    const QJsonObject mine = b[attacker ? "attacker" : "defender"].toObject();
    const QJsonObject theirs = b[attacker ? "defender" : "attacker"].toObject();

    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    auto* picture = new QLabel(this);
    picture->setObjectName("battleEmblem");
    picture->setPixmap(emblem(m_outcome, 160));
    top->addWidget(picture);
    auto* titles = new QVBoxLayout();
    auto* title = new QLabel(this);
    title->setObjectName("battleOutcome");
    const QString colour = m_outcome == Outcome::Victory ? "#ffd84a" : m_outcome == Outcome::Defeat ? "#ff6a5a" : "#cfcfcf";
    title->setText(QStringLiteral("<span style='font-size:26pt; font-weight:bold; color:%1'>%2</span>")
                       .arg(colour, m_outcome == Outcome::Victory ? tr("Victory!") : m_outcome == Outcome::Defeat ? tr("Defeat") : tr("Undecided")));
    titles->addWidget(title);
    QStringList lines;
    lines << (attacker ? tr("You attacked.") : tr("You were attacked."));
    lines << tr("%1 rounds").arg(b["rounds"].toInt());
    if (m_outcome == Outcome::Victory && b["experience"].toInt() > 0) lines << tr("+%1 experience").arg(b["experience"].toInt());
    if (m_outcome == Outcome::Victory && b["levelsGained"].toInt() > 0) lines << tr("<b>Level up!</b>");
    if (b["loserHeroKilled"].toBool()) lines << (m_outcome == Outcome::Victory ? tr("The enemy hero fell.") : tr("<b>Your hero fell.</b>"));
    if (b["retreated"].toBool())
        lines << (m_outcome == Outcome::Victory ? tr("The enemy fled.")
                  : b.contains("retreatedTo") ? tr("Your hero escaped to %1 (without his army).").arg(b["retreatedTo"].toString())
                                              : tr("Your hero escaped (without his army)."));
    if (b["surrendered"].toBool())
        lines << (m_outcome == Outcome::Victory ? tr("The enemy surrendered and paid <b>%1 gold</b>.").arg(b["surrenderGold"].toInt())
                                                : tr("You surrendered and paid %1 gold; your army lives.").arg(b["surrenderGold"].toInt()));
    auto* info = new QLabel(lines.join("<br>"), this);
    info->setTextFormat(Qt::RichText);
    titles->addWidget(info);
    titles->addStretch(1);
    top->addLayout(titles, 1);
    layout->addLayout(top);

    // Losses: unit pictures with how many fell.
    const auto losses = [&](const QString& heading, const QJsonObject& side) {
        auto* box = new QVBoxLayout();
        box->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(heading), this));
        auto* grid = new QGridLayout();
        const QJsonObject l = side["losses"].toObject();
        int i = 0;
        for (auto it = l.begin(); it != l.end(); ++it, ++i) {
            const core::UnitDef* u = content->unit(it.key());
            auto* icon = new QLabel(this);
            icon->setPixmap(art::unitIcon(it.key(), u ? u->rank : 0, 44));
            icon->setToolTip(u ? u->name : it.key());
            grid->addWidget(icon, i / 4 * 2, i % 4, Qt::AlignCenter);
            grid->addWidget(new QLabel(QStringLiteral("<b>-%1</b> %2").arg(it.value().toInt()).arg(u ? u->name : it.key()), this),
                            i / 4 * 2 + 1, i % 4, Qt::AlignCenter);
        }
        if (l.isEmpty()) grid->addWidget(new QLabel(tr("no losses"), this), 0, 0);
        box->addLayout(grid);
        box->addStretch(1);
        return box;
    };
    auto* both = new QHBoxLayout();
    both->addLayout(losses(tr("Your losses"), mine), 1);
    both->addSpacing(20);
    both->addLayout(losses(tr("Enemy losses"), theirs), 1);
    layout->addLayout(both);

    auto* ok = new QPushButton(m_outcome == Outcome::Victory ? tr("Onward!") : tr("OK"), this);
    ok->setObjectName("battleResultOk");
    ok->setMinimumSize(160, 40);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addStretch(1);
    layout->addLayout(buttons);
}

}  // namespace kot::client
