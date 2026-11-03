#include "client/EquipmentView.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

namespace {
constexpr double kBox = 64, kSmall = 56, kWidth = 520, kDollHeight = 400;

// Where each place sits around the figure (top left of its box).
QList<std::pair<QString, QPointF>> slotPlaces() {
    return {{"head", {228, 8}},     {"neck", {318, 52}},   {"weapon", {112, 124}}, {"body", {228, 124}},
            {"shield", {344, 124}}, {"ring", {112, 210}},  {"ring", {344, 210}},   {"feet", {228, 244}},
            {"misc", {150, 326}},   {"misc", {228, 326}},  {"misc", {306, 326}}};
}

QString slotName(const QString& slot) {
    if (slot == "head") return EquipmentView::tr("Head");
    if (slot == "neck") return EquipmentView::tr("Neck");
    if (slot == "body") return EquipmentView::tr("Body");
    if (slot == "weapon") return EquipmentView::tr("Weapon");
    if (slot == "shield") return EquipmentView::tr("Shield");
    if (slot == "feet") return EquipmentView::tr("Feet");
    if (slot == "ring") return EquipmentView::tr("Ring");
    return EquipmentView::tr("Other");
}

// A faint picture of what goes in an empty place.
QString ghostOf(const QString& slot) {
    if (slot == "head") return "iron_helm";
    if (slot == "neck") return "amulet_of_life";
    if (slot == "body") return "chain_mail";
    if (slot == "weapon") return "iron_sword";
    if (slot == "shield") return "oak_shield";
    if (slot == "feet") return "boots_of_travel";
    if (slot == "ring") return "travellers_ring";
    return "spyglass";
}
}  // namespace

EquipmentView::EquipmentView(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
    setObjectName("equipmentView");
    setMouseTracking(true);
}

void EquipmentView::setArtifacts(const QStringList& worn, const QStringList& backpack, bool canChange) {
    m_worn = worn;
    m_backpack = backpack;
    m_canChange = canChange;
    m_hover = -1;
    layoutBoxes();
    updateGeometry();
    update();
}

QSize EquipmentView::sizeHint() const {
    const int rows = std::max<qsizetype>(1, (m_backpack.size() + 7) / 8);
    return QSize(int(kWidth), int(kDollHeight + 30 + rows * (kSmall + 8) + 8));
}

void EquipmentView::layoutBoxes() {
    m_boxes.clear();
    m_layoutWidth = width();
    const double dx = std::max(0.0, (width() - kWidth) / 2);
    // Worn ones fill their places in order.
    QHash<QString, QStringList> bySlot;
    for (const QString& id : m_worn) {
        const core::ArtifactDef* a = m_content ? m_content->artifact(id) : nullptr;
        bySlot[a ? a->slot : QStringLiteral("misc")].append(id);
    }
    for (const auto& [slot, at] : slotPlaces()) {
        Box b;
        b.rect = QRectF(at.x() + dx, at.y(), kBox, kBox);
        b.slot = slot;
        if (!bySlot[slot].isEmpty()) b.artifact = bySlot[slot].takeFirst();
        m_boxes << b;
    }
    // More worn than there are places (old saves): shown in the backpack row too.
    QStringList carried = m_backpack;
    for (auto it = bySlot.begin(); it != bySlot.end(); ++it) carried = it.value() + carried;
    const double top = kDollHeight + 30;
    for (int i = 0; i < std::max<qsizetype>(8, carried.size()); ++i) {
        Box b;
        b.rect = QRectF(dx + 6 + (i % 8) * (kSmall + 8), top + (i / 8) * (kSmall + 8), kSmall, kSmall);
        b.worn = false;
        if (i < carried.size()) {
            b.artifact = carried[i];
            const core::ArtifactDef* a = m_content ? m_content->artifact(b.artifact) : nullptr;
            b.slot = a ? a->slot : QStringLiteral("misc");
        }
        m_boxes << b;
    }
}

int EquipmentView::boxAt(QPointF p) {
    if (m_layoutWidth != width()) layoutBoxes();
    for (int i = 0; i < m_boxes.size(); ++i)
        if (m_boxes[i].rect.contains(p)) return i;
    return -1;
}

void EquipmentView::paintEvent(QPaintEvent*) {
    if (m_layoutWidth != width()) layoutBoxes();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double dx = std::max(0.0, (width() - kWidth) / 2);
    // The ground: a dark panel with a soft light behind the figure.
    const QRectF doll(dx, 0, kWidth, kDollHeight);
    QRadialGradient light(doll.center() - QPointF(0, 30), kDollHeight * 0.6);
    light.setColorAt(0, QColor("#4a3a26"));
    light.setColorAt(1, QColor("#1e1810"));
    p.setPen(QPen(QColor("#6a5030"), 1.5));
    p.setBrush(light);
    p.drawRoundedRect(doll.adjusted(1, 1, -1, -1), 12, 12);
    // The figure: a shadowy hero standing in the middle.
    {
        const double cx = dx + 260;
        QPainterPath body;
        body.addEllipse(QPointF(cx, 66), 30, 34);                                     // head
        body.addRoundedRect(QRectF(cx - 58, 108, 116, 140), 30, 30);                 // chest
        body.addRoundedRect(QRectF(cx - 92, 116, 30, 120), 14, 14);                  // arms
        body.addRoundedRect(QRectF(cx + 62, 116, 30, 120), 14, 14);
        body.addRoundedRect(QRectF(cx - 46, 236, 40, 110), 16, 16);                  // legs
        body.addRoundedRect(QRectF(cx + 6, 236, 40, 110), 16, 16);
        QLinearGradient g(QPointF(cx, 30), QPointF(cx, 350));
        g.setColorAt(0, QColor(110, 90, 66, 150));
        g.setColorAt(1, QColor(60, 46, 30, 120));
        p.setPen(QPen(QColor(150, 120, 80, 90), 2));
        p.setBrush(g);
        p.drawPath(body.simplified());
    }
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    // Where the carried one under the mouse would go.
    QString target;
    if (m_hover >= 0 && !m_boxes[m_hover].worn && !m_boxes[m_hover].artifact.isEmpty()) target = m_boxes[m_hover].slot;
    for (int i = 0; i < m_boxes.size(); ++i) {
        const Box& b = m_boxes[i];
        const bool lit = i == m_hover || (b.worn && b.slot == target);
        const core::ArtifactDef* a = b.artifact.isEmpty() || !m_content ? nullptr : m_content->artifact(b.artifact);
        // A framed box; its frame tells how strong the artifact is.
        const QColor frame = !a ? QColor("#6a5a44") : a->value >= 3 ? QColor("#f0c030") : a->value == 2 ? QColor("#c8ccd8") : QColor("#c08a50");
        QLinearGradient g(b.rect.topLeft(), b.rect.bottomLeft());
        g.setColorAt(0, a ? QColor("#3e3020") : QColor("#2a2218"));
        g.setColorAt(1, a ? QColor("#22180e") : QColor("#18120c"));
        if (lit) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 216, 74, 70));
            p.drawRoundedRect(b.rect.adjusted(-5, -5, 5, 5), 12, 12);
        }
        p.setPen(QPen(lit ? QColor("#ffd84a") : frame, lit ? 2.6 : a ? 2.0 : 1.2));
        p.setBrush(g);
        p.drawRoundedRect(b.rect, 9, 9);
        const QRect inner = b.rect.adjusted(5, 5, -5, -5).toRect();
        if (a) {
            p.drawPixmap(inner, art::artifactIcon(a->id, a->slot, inner.width()));
        } else if (b.worn) {
            p.setOpacity(0.16);
            p.drawPixmap(inner, art::artifactIcon(ghostOf(b.slot), b.slot, inner.width()));
            p.setOpacity(1.0);
            p.setFont(small);
            p.setPen(QColor("#8a7a60"));
            p.drawText(b.rect.adjusted(0, 0, 0, -3), Qt::AlignHCenter | Qt::AlignBottom, slotName(b.slot));
        }
    }
    // The backpack's heading.
    p.setFont(font());
    p.setPen(QColor("#d8c8a8"));
    p.drawText(QRectF(dx + 6, kDollHeight + 6, kWidth - 12, 20), Qt::AlignLeft | Qt::AlignVCenter,
               m_backpack.isEmpty() ? tr("Backpack (empty)") : tr("Backpack: click to wear"));
}

QString EquipmentView::tooltipFor(const Box& b) const {
    const core::ArtifactDef* a = b.artifact.isEmpty() || !m_content ? nullptr : m_content->artifact(b.artifact);
    if (!a) return b.worn ? tr("%1: nothing worn here").arg(slotName(b.slot)) : QString();
    QString tip = QStringLiteral("<b>%1</b><br>%2<br><i>%3</i>").arg(a->name.toHtmlEscaped(), a->description.toHtmlEscaped(), slotName(a->slot));
    if (!m_canChange) return tip;
    if (b.worn) return tip + tr("<br>Click to take it off (into the backpack).");
    QStringList inSlot;
    int count = 0;
    for (const QString& id : m_worn)
        if (const core::ArtifactDef* w = m_content->artifact(id); w && w->slot == a->slot) {
            inSlot << w->name;
            ++count;
        }
    if (count >= core::artifactSlotCount(a->slot) && !inSlot.isEmpty()) return tip + tr("<br>Click to wear it instead of the %1.").arg(inSlot.first());
    return tip + tr("<br>Click to wear it.");
}

void EquipmentView::mouseMoveEvent(QMouseEvent* e) {
    const int h = boxAt(e->position());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 && m_canChange && !m_boxes[h].artifact.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EquipmentView::leaveEvent(QEvent*) {
    m_hover = -1;
    update();
}

void EquipmentView::mousePressEvent(QMouseEvent* e) {
    const int i = boxAt(e->position());
    if (i < 0 || !m_canChange || e->button() != Qt::LeftButton || m_boxes[i].artifact.isEmpty()) return;
    const Box& b = m_boxes[i];
    if (b.worn || !m_backpack.contains(b.artifact)) emit unequipRequested(b.artifact);
    else emit equipRequested(b.artifact);
}

bool EquipmentView::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int i = boxAt(help->pos());
        const QString tip = i >= 0 ? tooltipFor(m_boxes[i]) : QString();
        if (!tip.isEmpty()) QToolTip::showText(help->globalPos(), tip, this, m_boxes[i].rect.toRect());
        else QToolTip::hideText();
        return true;
    }
    if (e->type() == QEvent::Resize) layoutBoxes();
    return QWidget::event(e);
}

}  // namespace kot::client
