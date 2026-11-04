#include "client/GameOverDialog.h"

#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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

}  // namespace

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
