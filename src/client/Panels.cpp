#include "client/Panels.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <limits>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"
#include "core/model/Army.h"

namespace kot::client {

// ---- ResourceBar

ResourceBar::ResourceBar(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setObjectName("resourceBar");
    setFixedHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ResourceBar::setView(const GameView& view) {
    QList<Item> items;
    for (const core::ResourceDef& r : m_content->resources()) {
        const int amount = view.resources.value(r.id), income = view.income.value(r.id);
        if (amount == 0 && income == 0 && !r.isCurrency) continue;
        items.append({r.key, r.name, amount, income, {}});
    }
    bool same = items.size() == m_items.size();
    for (int i = 0; same && i < items.size(); ++i)
        same = items[i].id == m_items[i].id && items[i].amount == m_items[i].amount && items[i].income == m_items[i].income;
    if (same) return;  // (nothing to repaint)
    m_items = items;
    update();
}

QSize ResourceBar::sizeHint() const {
    return {170 * int(std::max<qsizetype>(1, m_items.size())), 40};
}

void ResourceBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont big = font();
    big.setPixelSize(17);
    big.setBold(true);
    QFont small = font();  // the income: what comes in every day
    small.setPixelSize(14);
    small.setBold(true);
    int x = 4;
    const int h = height();
    for (Item& it : m_items) {
        const QPixmap& icon = art::resourceIcon(it.id, h - 6);
        const QString amount = QString::number(it.amount);
        const QString income = it.income > 0 ? tr("+%1/day").arg(it.income) : QString();
        const int w = icon.width() + 6 + QFontMetrics(big).horizontalAdvance(amount) + 6 + QFontMetrics(small).horizontalAdvance(income) + 18;
        it.rect = QRect(x, 0, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(it.rect.adjusted(0, 2, -6, -2), 6, 6);
        p.drawPixmap(x + 3, 3, icon);
        int tx = x + icon.width() + 8;
        p.setFont(big);
        p.setPen(QColor("#f6e7c1"));
        p.drawText(QRect(tx, 0, 200, h), Qt::AlignVCenter | Qt::AlignLeft, amount);
        tx += QFontMetrics(big).horizontalAdvance(amount) + 6;
        if (!income.isEmpty()) {
            p.setFont(small);
            p.setPen(QColor("#a8e07e"));
            p.drawText(QRect(tx, 1, 140, h), Qt::AlignVCenter | Qt::AlignLeft, income);
        }
        x += w;
    }
}

bool ResourceBar::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        const QPoint pos = static_cast<QHelpEvent*>(e)->pos();
        for (const Item& it : m_items)
            if (it.rect.contains(pos)) {
                QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(),
                                   tr("%1: %2, +%3 every day").arg(it.name).arg(it.amount).arg(it.income), this);
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

// ---- WeekBar

WeekBar::WeekBar(QWidget* parent) : QWidget(parent) {
    setObjectName("weekBar");
    setFixedHeight(30);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize WeekBar::sizeHint() const { return {640, 30}; }

void WeekBar::setDay(int day, const QString& goal) {
    m_day = std::max(1, day);
    m_goal = goal;
    const int week = (m_day - 1) / 7 + 1;
    const int weekDay = (m_day - 1) % 7 + 1;
    const int toGrowth = 8 - weekDay;
    const QString growth = toGrowth == 7 ? tr("new units today")
                         : toGrowth == 1 ? tr("new units tomorrow")
                                         : tr("new units in %1 days").arg(toGrowth);
    setToolTip(tr("Week %1, day %2 of 7: %3.<br>Towns and dwellings get new units at the start of each week.%4")
                   .arg(week).arg(weekDay).arg(growth)
                   .arg(m_goal.isEmpty() ? QString() : tr("<br>Goal: %1").arg(m_goal.toHtmlEscaped())));
    update();
}

void WeekBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient bg(panel.topLeft(), panel.bottomLeft());
    bg.setColorAt(0, QColor(44, 32, 18, 220));
    bg.setColorAt(1, QColor(24, 18, 10, 220));
    p.setPen(QPen(QColor(122, 98, 53, 170), 1));
    p.setBrush(bg);
    p.drawRoundedRect(panel, 7, 7);

    const int week = (m_day - 1) / 7 + 1;
    const int weekDay = (m_day - 1) % 7 + 1;
    const int toGrowth = 8 - weekDay;
    const QString left = tr("Week %1").arg(week);
    const QString right = toGrowth == 7 ? tr("new units today")
                        : toGrowth == 1 ? tr("new units tomorrow")
                                        : tr("new units in %1 days").arg(toGrowth);
    QFont small = font();
    small.setPixelSize(11);
    small.setBold(true);
    const QFontMetrics fm(small);
    const int margin = 12;
    int leftWidth = fm.horizontalAdvance(left) + 10;
    int rightWidth = fm.horizontalAdvance(right) + 10;
    if (width() < leftWidth + rightWidth + 7 * 30) leftWidth = rightWidth = 0;  // too narrow: just the days

    const double x0 = margin + leftWidth;
    const double x1 = width() - margin - rightWidth;
    const double step = (x1 - x0) / 6.0;
    const double y = height() / 2.0;
    p.setFont(small);
    if (leftWidth > 0) {
        p.setPen(QColor("#e8c872"));
        p.drawText(QRectF(margin, 0, leftWidth, height()), Qt::AlignVCenter | Qt::AlignLeft, left);
    }
    if (rightWidth > 0) {
        p.setPen(QColor("#a89468"));
        p.drawText(QRectF(width() - margin - rightWidth, 0, rightWidth, height()), Qt::AlignVCenter | Qt::AlignRight, right);
    }

    // The road through the week: dark ahead, gold behind.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawRoundedRect(QRectF(x0, y - 2, x1 - x0, 4), 2, 2);
    if (weekDay > 1) {
        QLinearGradient done(x0, 0, x0 + (weekDay - 1) * step, 0);
        done.setColorAt(0, QColor("#8a6a2a"));
        done.setColorAt(1, QColor("#ffd84a"));
        p.setBrush(done);
        p.drawRoundedRect(QRectF(x0, y - 2, (weekDay - 1) * step, 4), 2, 2);
    }

    QFont number = small;
    number.setPixelSize(9);
    number.setBold(true);
    for (int i = 0; i < 7; ++i) {
        const double cx = x0 + i * step;
        const int d = i + 1;
        const QString text = QString::number(d);
        if (d == weekDay) {
            // Today: a bright diamond with a soft glow.
            QRadialGradient glow(QPointF(cx, y), 18);
            glow.setColorAt(0, QColor(255, 216, 74, 120));
            glow.setColorAt(1, QColor(255, 216, 74, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(QPointF(cx, y), 18, 13);
            p.setBrush(QColor("#ffd84a"));
            p.setPen(QPen(QColor("#fff0a8"), 1.5));
            QPolygonF diamond{QPointF(cx, y - 10), QPointF(cx + 9, y), QPointF(cx, y + 10), QPointF(cx - 9, y)};
            p.drawPolygon(diamond);
            p.setFont(number);
            p.setPen(QColor("#3a2a10"));
            p.drawText(QRectF(cx - 8, y - 8, 16, 16), Qt::AlignCenter, text);
        } else {
            const bool past = d < weekDay;
            p.setBrush(past ? QColor("#b08a3a") : QColor(20, 15, 9, 180));
            p.setPen(QPen(past ? QColor("#e8c872") : QColor(122, 98, 53, 150), 1.2));
            p.drawEllipse(QPointF(cx, y), 7.5, 7.5);
            p.setFont(number);
            p.setPen(past ? QColor("#2a2010") : QColor("#8a7a5a"));
            p.drawText(QRectF(cx - 7, y - 7, 14, 14), Qt::AlignCenter, text);
        }
    }
}

// ---- HeroPanel

namespace {
constexpr int kSlotCols = 5;
constexpr int kSlotSize = 50;
constexpr int kTop = 120;  // where the army slots start

void bar(QPainter& p, const QRect& r, double fraction, const QColor& colour, const QString& text) {
    // A pill: dark sunken track, a glossy fill, the text with a soft shadow.
    const QRectF track = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
    const double radius = track.height() / 2;
    p.setPen(QPen(QColor(122, 98, 53, 150), 1));
    QLinearGradient sunk(track.topLeft(), track.bottomLeft());
    sunk.setColorAt(0, QColor("#120d09"));
    sunk.setColorAt(1, QColor("#2a2016"));
    p.setBrush(sunk);
    p.drawRoundedRect(track, radius, radius);
    const QRectF fill(track.left() + 1.5, track.top() + 1.5, (track.width() - 3) * std::clamp(fraction, 0.0, 1.0), track.height() - 3);
    if (fill.width() > 1) {
        QLinearGradient g(fill.topLeft(), fill.bottomLeft());
        g.setColorAt(0, colour.lighter(150));
        g.setColorAt(0.45, colour);
        g.setColorAt(1, colour.darker(130));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        const double fr = std::min(fill.height() / 2, fill.width() / 2);
        p.drawRoundedRect(fill, fr, fr);
    }
    p.setPen(QColor(0, 0, 0, 150));
    p.drawText(r.translated(1, 1), Qt::AlignCenter, text);
    p.setPen(QColor("#fff4dc"));
    p.drawText(r, Qt::AlignCenter, text);
}
}  // namespace

HeroPanel::HeroPanel(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setObjectName("heroPanel");
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(sizeHint().height());
}

QSize HeroPanel::sizeHint() const {
    const int rows = (core::kMaxArmyStacks + kSlotCols - 1) / kSlotCols;
    return {270, kTop + rows * (kSlotSize + 4) + 4};
}

void HeroPanel::setHero(const GameView& view, int heroId) {
    m_view = view;
    m_heroId = heroId;
    update();
}

QRect HeroPanel::slotRect(int i) const {
    const int w = std::min(kSlotSize, (width() - 4 * (kSlotCols - 1)) / kSlotCols);
    return QRect((i % kSlotCols) * (w + 4), kTop + (i / kSlotCols) * (w + 4), w, w);
}

QString HeroPanel::slotTip(int i) const {
    const ViewHero* h = m_view.hero(m_heroId);
    const ViewArmy* a = h ? m_view.army(h->army) : nullptr;
    if (!a || i >= a->stacks.size()) return tr("Empty slot (an army has %1)").arg(core::kMaxArmyStacks);
    const ViewStack& s = a->stacks[i];
    const core::UnitDef* u = m_content->unit(s.unit);
    if (!u) return QString();
    QString tip = tr("<b>%1 x %2</b>").arg(s.count).arg(u->name);
    if (s.stars > 0) tip += QStringLiteral(" ") + QString(s.stars, QChar(0x2605));
    tip += tr("<br>Attack %1, Defense %2, Damage %3-%4, %5 HP<br>Speed %6, Initiative %7")
               .arg(u->attack).arg(u->defense).arg(u->damageMin).arg(u->damageMax).arg(u->maxHp).arg(u->battleSpeed).arg(u->initiative);
    if (u->range > 0) tip += tr(", shoots (%1 shots)").arg(u->shots);
    if (s.totalHp >= 0 && s.totalHp < s.count * s.maxHp) tip += tr("<br>Wounded: %1 of %2 HP").arg(s.totalHp).arg(s.count * s.maxHp);
    return tip;
}

void HeroPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const ViewHero* h = m_view.hero(m_heroId);
    if (!h) {
        p.setPen(QColor("#eadfc8"));
        p.drawText(rect(), Qt::AlignCenter, tr("No hero"));
        return;
    }
    const core::HeroClassDef* cls = m_content->heroClass(h->classId);
    const ViewPlayer* owner = m_view.player(h->owner);
    // Portrait: the map sprite on a plate.
    const QRect portrait(0, 0, 52, 52);
    p.setPen(QPen(QColor("#c9a24a"), 2));
    p.setBrush(QColor("#1d1610"));
    p.drawRoundedRect(portrait, 6, 6);
    p.drawPixmap(portrait.adjusted(2, 2, -2, -2), art::portrait(h->classId, owner ? QColor(owner->color) : QColor("#999999"), h->id, 48));
    QFont f = font();
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#ffe27a"));
    p.drawText(QRect(60, 0, width() - 60, 20), Qt::AlignLeft | Qt::AlignVCenter, h->name);
    f.setPixelSize(12);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#eadfc8"));
    p.drawText(QRect(60, 20, width() - 60, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Level %1 %2").arg(h->level).arg(cls ? cls->name : QString()));
    p.drawText(QRect(60, 36, width() - 60, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Att %1 · Def %2 · Power %3 · Magic %4")
                   .arg(h->traits.value(m_content->known().attack)).arg(h->traits.value(m_content->known().defense))
                   .arg(h->traits.value(m_content->known().spellPower)).arg(h->traits.value(m_content->known().magic)));
    // Bars.
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    const int maxMove = std::max(h->movementMax, h->movementPoints);
    bar(p, QRect(0, 58, width(), 17), maxMove > 0 ? double(h->movementPoints) / maxMove : 0, QColor("#c98a12"),
        tr("Movement %1 / %2").arg(h->movementPoints / 100.0, 0, 'f', 1).arg(maxMove / 100.0, 0, 'f', 1));
    const int maxSp = h->traits.value(m_content->known().spellPointsMax);
    bar(p, QRect(0, 78, width(), 17), maxSp > 0 ? double(h->spellPoints) / maxSp : 0, QColor("#3a6fd0"),
        tr("Spell points %1 / %2").arg(h->spellPoints).arg(maxSp));
    const qint64 prevIntel = h->level >= 2 ? m_content->xpForNextLevel(h->level - 1) : 0;
    const qint64 nextIntel = m_content->xpForNextLevel(h->level);
    const int prev = static_cast<int>(std::min(prevIntel, qint64(std::numeric_limits<int>::max())));
    const int next = static_cast<int>(std::min(nextIntel, qint64(std::numeric_limits<int>::max())));
    bar(p, QRect(0, 98, width(), 17), next > prev ? double(h->experience - prev) / (next - prev) : 1.0, QColor("#4f9a3a"),
        tr("Experience %1 / %2").arg(h->experience).arg(next));
    // Army slots.
    const ViewArmy* a = m_view.army(h->army);
    for (int i = 0; i < core::kMaxArmyStacks; ++i) {
        const QRect r = slotRect(i);
        // A soft sunken socket for each place in the army.
        QRadialGradient socket(QRectF(r).center(), r.width() * 0.7);
        socket.setColorAt(0, QColor(0, 0, 0, 40));
        socket.setColorAt(1, QColor(0, 0, 0, 110));
        p.setPen(QPen(QColor(122, 98, 53, 110), 1));
        p.setBrush(socket);
        p.drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), r.width() * 0.22, r.width() * 0.22);
        if (!a || i >= a->stacks.size()) continue;
        const ViewStack& s = a->stacks[i];
        const core::UnitDef* u = m_content->unit(s.unit);
        art::drawUnitToken(p, QRectF(r).center(), r.width() * 0.46, owner ? QColor(owner->color) : QColor("#999999"),
                           u ? u->name : QString(), s.count, s.stars, false, false, art::unitKey(s.unit), u ? u->rank : 0);
    }
}

bool HeroPanel::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        for (int i = 0; i < core::kMaxArmyStacks; ++i)
            if (slotRect(i).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), slotTip(i), this);
                return true;
            }
        QToolTip::showText(help->globalPos(), tr("Click: the hero screen (H)"), this);
        return true;
    }
    return QWidget::event(e);
}

void HeroPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked();
}

}  // namespace kot::client
