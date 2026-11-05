#include "client/Panels.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"
#include "core/model/Army.h"

namespace kot::client {

// ---- ResourceBar

ResourceBar::ResourceBar(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setObjectName("resourceBar");
    setMinimumHeight(30);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ResourceBar::setView(const GameView& view) {
    m_items.clear();
    for (const core::ResourceDef& r : m_content->resources()) {
        const int amount = view.resources.value(r.id), income = view.income.value(r.id);
        if (amount == 0 && income == 0 && !r.isCurrency) continue;
        m_items.append({r.key, r.name, amount, income, {}});
    }
    update();
}

QSize ResourceBar::sizeHint() const {
    return {120 * int(std::max<qsizetype>(1, m_items.size())), 30};
}

void ResourceBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont big = font();
    big.setPixelSize(15);
    big.setBold(true);
    QFont small = font();
    small.setPixelSize(11);
    int x = 4;
    const int h = height();
    for (Item& it : m_items) {
        const QPixmap& icon = art::resourceIcon(it.id, h - 6);
        const QString amount = QString::number(it.amount);
        const QString income = it.income > 0 ? QStringLiteral("+%1").arg(it.income) : QString();
        const int w = icon.width() + 6 + QFontMetrics(big).horizontalAdvance(amount) + 4 + QFontMetrics(small).horizontalAdvance(income) + 16;
        it.rect = QRect(x, 0, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(it.rect.adjusted(0, 2, -6, -2), 6, 6);
        p.drawPixmap(x + 3, 3, icon);
        int tx = x + icon.width() + 8;
        p.setFont(big);
        p.setPen(QColor("#f6e7c1"));
        p.drawText(QRect(tx, 0, 200, h), Qt::AlignVCenter | Qt::AlignLeft, amount);
        tx += QFontMetrics(big).horizontalAdvance(amount) + 4;
        if (!income.isEmpty()) {
            p.setFont(small);
            p.setPen(QColor("#9fd27a"));
            p.drawText(QRect(tx, 2, 100, h), Qt::AlignVCenter | Qt::AlignLeft, income);
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
    const QList<int>& xp = m_content->xpThresholds();
    const int prev = h->level >= 2 ? xp.value(h->level - 2) : 0;
    const int next = h->level - 1 < xp.size() ? xp[h->level - 1] : 0;
    bar(p, QRect(0, 98, width(), 17), next > prev ? double(h->experience - prev) / (next - prev) : 1.0, QColor("#4f9a3a"),
        next > 0 ? tr("Experience %1 / %2").arg(h->experience).arg(next) : tr("Experience %1 (top level)").arg(h->experience));
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
