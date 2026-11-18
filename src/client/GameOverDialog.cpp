#include "client/GameOverDialog.h"

#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "client/Settings.h"

namespace kot::client {

namespace {

// One small line chart: a value per week for every player.
class StatChart : public QWidget {
public:
    StatChart(const GameView& view, const QString& key, const QString& title, QWidget* parent)
        : QWidget(parent), m_view(view), m_key(key), m_title(title) {
        setMinimumSize(300, 170);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(QPen(QColor("#6a5030"), 1.2));
        p.setBrush(QColor("#1e1810"));
        p.drawRoundedRect(frame, 8, 8);
        p.setPen(QColor("#e8d8b0"));
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(frame.adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop, m_title);
        const QRectF plot = frame.adjusted(44, 30, -12, -24);
        const QJsonArray& h = m_view.history;
        if (h.size() < 1) return;
        double maxV = 1;
        for (const QJsonValue& w : h) {
            const QJsonObject players = w.toObject().value("players").toObject();
            for (auto it = players.begin(); it != players.end(); ++it) maxV = std::max(maxV, it.value().toObject().value(m_key).toDouble());
        }
        // Grid and scale.
        p.setFont(font());
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        for (int i = 0; i <= 4; ++i) {
            const double y = plot.bottom() - plot.height() * i / 4;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.save();
            p.setPen(QColor("#9a8a70"));
            const double v = maxV * i / 4;
            const QString label = v >= 10000 ? QStringLiteral("%1k").arg(v / 1000, 0, 'f', 0)
                                  : v >= 1000 ? QStringLiteral("%1k").arg(v / 1000, 0, 'f', 1) : QString::number(int(std::round(v)));
            p.drawText(QRectF(frame.left(), y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, label);
            p.restore();
        }
        p.setPen(QColor("#9a8a70"));
        p.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(), 18), Qt::AlignHCenter, QObject::tr("week 1 .. %1").arg(h.last().toObject().value("week").toInt()));
        const int n = static_cast<int>(h.size());
        const auto x = [&](int i) { return n <= 1 ? plot.center().x() : plot.left() + plot.width() * i / (n - 1); };
        for (const ViewPlayer& pl : m_view.players) {
            QPainterPath line;
            bool any = false;
            for (int i = 0; i < n; ++i) {
                const QJsonObject s = h[i].toObject().value("players").toObject().value(QString::number(pl.id)).toObject();
                if (s.isEmpty()) continue;
                const QPointF pt(x(i), plot.bottom() - plot.height() * s.value(m_key).toDouble() / maxV);
                if (!any) line.moveTo(pt);
                else line.lineTo(pt);
                any = true;
            }
            if (!any) continue;
            const bool me = pl.id == m_view.you;
            p.setPen(QPen(QColor(0, 0, 0, 140), me ? 5 : 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(line);
            p.setPen(QPen(QColor(pl.color), me ? 3 : 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(line);
        }
    }

private:
    GameView m_view;  // a copy: the dialog outlives changes to the game view
    QString m_key, m_title;
};

// Short numbers for the analysis notes ("18k" instead of "18000").
QString shortNumber(double v) {
    const qint64 n = qint64(std::round(v));
    return n >= 10000 ? QStringLiteral("%1k").arg(double(n) / 1000, 0, 'f', 0)
           : n >= 1000 ? QStringLiteral("%1k").arg(double(n) / 1000, 0, 'f', 1)
                       : QString::number(n);
}

}  // namespace

QStringList GameOverDialog::analysis(const GameView& view) {
    QStringList out;
    if (view.history.isEmpty() || view.winnerTeam == 0) return out;
    const ViewPlayer* me = view.player(view.you);
    if (!me) return out;
    const ViewPlayer* winner = nullptr;
    for (const ViewPlayer& p : view.players)
        if (p.team == view.winnerTeam) { winner = &p; break; }
    if (!winner) return out;
    const QString myId = QString::number(me->id), foeId = QString::number(winner->id);
    const auto stats = [&view](int i, const QString& id) {
        return view.history.at(i).toObject().value(QStringLiteral("players")).toObject().value(id).toObject();
    };
    const int last = view.history.size() - 1;
    const QJsonObject mine = stats(last, myId);
    const QJsonObject theirs = stats(last, foeId);
    if (mine.isEmpty() || theirs.isEmpty()) return out;
    const double myArmy = mine.value(QStringLiteral("armyValue")).toDouble();
    const double foeArmy = theirs.value(QStringLiteral("armyValue")).toDouble();
    const double myGold = mine.value(QStringLiteral("gold")).toDouble();
    const int myTowns = mine.value(QStringLiteral("towns")).toInt();
    const int foeTowns = theirs.value(QStringLiteral("towns")).toInt();
    const int myMines = mine.value(QStringLiteral("mines")).toInt();
    const int foeMines = theirs.value(QStringLiteral("mines")).toInt();
    const int myHeroes = mine.value(QStringLiteral("heroes")).toInt();
    const int foeHeroes = theirs.value(QStringLiteral("heroes")).toInt();

    if (view.winnerTeam == me->team) {
        out << QObject::tr("You finished ahead: your army was worth %1 at the end, against %2 for %3.")
                   .arg(shortNumber(myArmy), shortNumber(foeArmy), winner->name);
        if (myGold > myArmy * 0.5)
            out << QObject::tr("There was still %1 gold in the treasury: spending it sooner could have won earlier.")
                       .arg(shortNumber(myGold));
        return out;
    }

    // The direct reasons the numbers give for the defeat.
    if (foeArmy > myArmy * 1.2)
        out << QObject::tr("Their army was worth %1 against your %2: they simply fielded more force.")
                   .arg(shortNumber(foeArmy), shortNumber(myArmy));
    if (foeTowns > myTowns)
        out << QObject::tr("They held %1 towns to your %2, so they could build and recruit faster.")
                   .arg(foeTowns).arg(myTowns);
    if (foeMines > myMines)
        out << QObject::tr("They owned %1 mines to your %2: their income kept growing while yours did not.")
                   .arg(foeMines).arg(myMines);
    if (foeHeroes > myHeroes)
        out << QObject::tr("They ran %1 heroes to your %2: more heroes gather resources and carry the army.")
                   .arg(foeHeroes).arg(myHeroes);

    // "Was I too careful?" A pile of unused gold beside a smaller army, or an army
    // that never grew while the enemy's did, says the player held back.
    if (myGold > myArmy * 0.5 && myArmy < foeArmy)
        out << QObject::tr("You were too careful: %1 gold sat in the treasury while your army was worth only %2. "
                           "Buying units and attacking earlier could have closed the gap.")
                   .arg(shortNumber(myGold), shortNumber(myArmy));
    const QJsonObject firstMine = stats(0, myId), firstFoe = stats(0, foeId);
    if (!firstMine.isEmpty() && !firstFoe.isEmpty()) {
        const double startMine = firstMine.value(QStringLiteral("armyValue")).toDouble();
        const double startFoe = firstFoe.value(QStringLiteral("armyValue")).toDouble();
        if (startMine > 0 && startFoe > 0 && myArmy < startMine * 1.3 && foeArmy > startFoe * 1.8)
            out << QObject::tr("Your army hardly grew while theirs grew several times over: keep buying units every week.");
    }
    if (out.isEmpty())
        out << QObject::tr("The numbers were close: the result came down to the battles themselves.");
    return out;
}

GameOverDialog::GameOverDialog(const GameView& view, const QString& text, QWidget* parent) : QDialog(parent) {
    setObjectName("gameOverDialog");
    setWindowTitle(tr("Game over"));
    resize(760, 600);
    auto* v = new QVBoxLayout(this);
    const ViewPlayer* me = view.player(view.you);
    const bool won = me && view.winnerTeam == me->team;
    auto* title = new QLabel(won ? tr("<span style='font-size:22pt; font-weight:bold; color:#ffd84a'>Victory!</span>")
                                 : tr("<span style='font-size:22pt; font-weight:bold; color:#e06050'>Defeat</span>"), this);
    title->setAlignment(Qt::AlignCenter);
    v->addWidget(title);
    auto* words = new QLabel(text, this);
    words->setWordWrap(true);
    words->setAlignment(Qt::AlignCenter);
    v->addWidget(words);
    // Who played: colour, name, how it ended.
    QStringList who;
    for (const ViewPlayer& p : view.players)
        who << QStringLiteral("<span style='color:%1; font-size:14pt'>&#x25C6;</span> %2%3")
                   .arg(p.color, p.name.toHtmlEscaped(),
                        p.team == view.winnerTeam ? tr(" <b>(won)</b>") : p.eliminated ? tr(" (out)") : QString());
    auto* players = new QLabel(who.join(QStringLiteral(" &nbsp; ")), this);
    players->setAlignment(Qt::AlignCenter);
    v->addWidget(players);
    // Plain-language notes on why the game went the way it did (weekly stats).
    const QStringList notes = analysis(view);
    if (!notes.isEmpty()) {
        QStringList html;
        html << QStringLiteral("<b>%1</b>").arg(tr("How the game was decided"));
        for (const QString& note : notes) html << QStringLiteral("&bull; %1").arg(note.toHtmlEscaped());
        auto* analysisLabel = new QLabel(html.join(QStringLiteral("<br>")), this);
        analysisLabel->setObjectName(QStringLiteral("gameOverAnalysis"));
        analysisLabel->setWordWrap(true);
        analysisLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        analysisLabel->setStyleSheet(QStringLiteral("background:#241c12; color:#e8d8b0; border-radius:6px; padding:8px;"));
        v->addWidget(analysisLabel);
    }
    // How it went.
    auto* grid = new QGridLayout();
    grid->addWidget(new StatChart(view, "armyValue", tr("Army (worth in gold)"), this), 0, 0);
    grid->addWidget(new StatChart(view, "towns", tr("Towns"), this), 0, 1);
    grid->addWidget(new StatChart(view, "mines", tr("Mines"), this), 1, 0);
    grid->addWidget(new StatChart(view, "gold", tr("Gold in the treasury"), this), 1, 1);
    v->addLayout(grid, 1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setMinimumSize(120, 36);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(close, 0, Qt::AlignRight);
}

}  // namespace kot::client
