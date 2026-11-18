#include "client/Toast.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

namespace kot::client {

Toast::Toast(QWidget* host, const QPixmap& icon, const QString& html) : QWidget(host), m_icon(icon) {
    setObjectName("toast");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(icon.isNull() ? 16 : 84, 10, 18, 10);
    m_text = new QLabel(html, this);
    m_text->setObjectName("toastText");
    m_text->setWordWrap(true);
    m_text->setStyleSheet("QLabel { color: #f6e7c1; background: transparent; font-size: 11pt; }");
    m_text->setMaximumWidth(420);
    row->addWidget(m_text);
    adjustSize();
    setMinimumHeight(icon.isNull() ? 0 : 84);
    adjustSize();
}

Toast* Toast::show(QWidget* host, const QPixmap& icon, const QString& html, int ms) {
    if (!host) return nullptr;
    auto* t = new Toast(host, icon, html);
    t->place();
    auto* fade = new QGraphicsOpacityEffect(t);
    fade->setOpacity(0);
    t->setGraphicsEffect(fade);
    auto* seq = new QSequentialAnimationGroup(t);
    auto* in = new QPropertyAnimation(fade, "opacity");
    in->setDuration(250);
    in->setEndValue(1.0);
    seq->addAnimation(in);
    seq->addPause(std::max(500, ms - 1100));
    auto* out = new QPropertyAnimation(fade, "opacity");
    out->setDuration(850);
    out->setEndValue(0.0);
    seq->addAnimation(out);
    connect(seq, &QAbstractAnimation::finished, t, &QWidget::close);
    t->QWidget::show();
    t->raise();
    seq->start(QAbstractAnimation::DeleteWhenStopped);
    return t;
}

void Toast::place() {
    // Top middle of the host, below any toasts already showing.
    int y = 70;
    for (QObject* o : parentWidget()->children())
        if (auto* other = qobject_cast<Toast*>(o); other && other != this && other->isVisible()) y = std::max(y, other->geometry().bottom() + 8);
    move((parentWidget()->width() - width()) / 2, y);
}

void Toast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, QColor(58, 40, 22, 240));
    g.setColorAt(1, QColor(30, 20, 10, 240));
    p.setPen(QPen(QColor("#c9a24a"), 2));
    p.setBrush(g);
    p.drawRoundedRect(r, 12, 12);
    if (!m_icon.isNull()) {
        const QRectF slot(12, (height() - 64) / 2.0, 64, 64);
        QRadialGradient glow(slot.center(), 40);
        glow.setColorAt(0, QColor(255, 220, 120, 110));
        glow.setColorAt(1, QColor(255, 220, 120, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(slot.center(), 40, 40);
        p.drawPixmap(slot.toRect(), m_icon);
    }
}

}  // namespace kot::client
