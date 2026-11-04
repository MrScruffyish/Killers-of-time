#include "client/ConnectPage.h"

#include <QFormLayout>
#include <QPainterPath>
#include <QPainter>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QSettings>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "client/Sound.h"
#include "core/Version.h"
#include "net/Protocol.h"

namespace kot::client {

namespace {

// The title picture: a castle on a hill at dusk under the game's name.
class TitleBanner : public QWidget {
public:
    explicit TitleBanner(QWidget* parent) : QWidget(parent) {
        setMinimumHeight(230);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const double w = width(), h = height();
        QLinearGradient sky(0, 0, 0, h);
        sky.setColorAt(0, QColor("#1a1838"));
        sky.setColorAt(0.55, QColor("#7a3a4a"));
        sky.setColorAt(1, QColor("#e0904a"));
        p.fillRect(rect(), sky);
        // Stars and a moon.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 240, 180));
        for (int i = 0; i < 40; ++i) {
            const double x = (i * 97 % 1000) / 1000.0 * w, y = (i * 53 % 1000) / 1000.0 * h * 0.45;
            p.drawEllipse(QPointF(x, y), 1.1, 1.1);
        }
        p.setBrush(QColor("#f4ecd0"));
        p.drawEllipse(QPointF(w * 0.82, h * 0.2), h * 0.08, h * 0.08);
        p.setBrush(QColor("#1a1838"));
        p.drawEllipse(QPointF(w * 0.82 + h * 0.035, h * 0.2 - h * 0.02), h * 0.07, h * 0.07);
        // Far hills, near hill.
        QPainterPath far;
        far.moveTo(0, h * 0.75);
        for (int i = 0; i <= 10; ++i) far.quadTo(w * (i - 0.5) / 10, h * (0.58 + 0.06 * (i % 3)), w * i / 10, h * 0.72);
        far.lineTo(w, h);
        far.lineTo(0, h);
        p.setBrush(QColor("#3a2a3a"));
        p.drawPath(far);
        QPainterPath hill;
        hill.moveTo(0, h);
        hill.quadTo(w * 0.75, h * 0.62, w * 1.2, h);
        p.setBrush(QColor("#22182a"));
        p.drawPath(hill);
        // The castle on the hill: towers, walls, lit windows, a banner.
        const double cx = w * 0.76, base = h * 0.84, u = h * 0.055;
        p.setBrush(QColor("#141020"));
        p.drawRect(QRectF(cx - u * 5, base - u * 2.5, u * 10, u * 2.5));
        for (const double tx : {-5.5, -1.2, 3.6}) {
            const double th = tx == -1.2 ? 6 : 4;
            p.drawRect(QRectF(cx + tx * u, base - th * u, u * 1.8, th * u));
            p.drawPolygon(QPolygonF{QPointF(cx + tx * u - u * 0.3, base - th * u), QPointF(cx + (tx + 0.9) * u, base - (th + 1.8) * u),
                                    QPointF(cx + (tx + 2.1) * u, base - th * u)});
        }
        p.setBrush(QColor("#ffc860"));
        for (const auto& [x, y] : {std::pair{-4.9, 3.0}, std::pair{-0.6, 4.6}, std::pair{-0.6, 3.2}, std::pair{4.2, 2.8}, std::pair{2.0, 1.6}})
            p.drawRect(QRectF(cx + x * u, base - y * u, u * 0.45, u * 0.6));
        p.setPen(QPen(QColor("#141020"), 2));
        p.drawLine(QPointF(cx - 0.3 * u, base - 7.8 * u), QPointF(cx - 0.3 * u, base - 9.6 * u));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#c03a3a"));
        p.drawPolygon(QPolygonF{QPointF(cx - 0.3 * u, base - 9.6 * u), QPointF(cx + 1.6 * u, base - 9.1 * u), QPointF(cx - 0.3 * u, base - 8.6 * u)});
        // The name in gold with a dark edge.
        QFont f = font();
        f.setPixelSize(int(h * 0.2));
        f.setBold(true);
        QPainterPath text;
        const QString name = QString::fromLatin1(core::kGameName);
        text.addText(0, 0, f, name);
        const QRectF tb = text.boundingRect();
        text.translate(w / 2 - tb.width() / 2 - tb.left(), h * 0.3 - tb.top() - tb.height() / 2);
        p.setPen(QPen(QColor(20, 10, 4), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(text);
        QLinearGradient gold(0, h * 0.18, 0, h * 0.42);
        gold.setColorAt(0, QColor("#fff0a0"));
        gold.setColorAt(1, QColor("#c98a12"));
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        p.drawPath(text);
        f.setPixelSize(std::max(11, int(h * 0.06)));
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(240, 220, 190, 200));
        p.drawText(QRectF(0, h * 0.42, w, h * 0.1), Qt::AlignCenter, tr("version %1").arg(QString::fromLatin1(core::versionText())));
    }
};

}  // namespace

ConnectPage::ConnectPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);

    auto* title = new TitleBanner(this);
    title->setObjectName("titleBanner");
    outer->addWidget(title);

    auto* form = new QFormLayout();
    m_name = new QLineEdit(qEnvironmentVariable("USER", QStringLiteral("Player")), this);
    m_name->setObjectName("nameEdit");
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_host->setObjectName("hostEdit");
    m_port = new QSpinBox(this);
    m_port->setObjectName("portEdit");
    m_port->setRange(1, 65535);
    m_port->setValue(net::kDefaultPort);
    // The last name, server and port are remembered.
    QSettings settings;
    m_name->setText(settings.value(QStringLiteral("connect/name"), m_name->text()).toString());
    m_host->setText(settings.value(QStringLiteral("connect/host"), m_host->text()).toString());
    m_port->setValue(settings.value(QStringLiteral("connect/port"), m_port->value()).toInt());
    form->addRow(tr("Your name"), m_name);
    form->addRow(tr("Server"), m_host);
    form->addRow(tr("Port"), m_port);
    auto* soundBox = new QCheckBox(tr("Sound effects (also in the game's menu)"), this);
    soundBox->setObjectName("soundCheck");
    soundBox->setChecked(!sound::muted());
    soundBox->setEnabled(sound::available());
    if (!sound::available()) soundBox->setToolTip(tr("No sound in this build or this run"));
    connect(soundBox, &QCheckBox::toggled, this, [](bool on) { sound::setMuted(!on); });
    form->addRow(QString(), soundBox);
    auto* formHolder = new QWidget(this);
    formHolder->setLayout(form);
    formHolder->setMaximumWidth(420);
    outer->addWidget(formHolder, 0, Qt::AlignHCenter);

    auto* buttons = new QHBoxLayout();
    auto* join = new QPushButton(tr("Join game"), this);
    join->setObjectName("joinButton");
    auto* host = new QPushButton(tr("Host a game on this computer"), this);
    host->setObjectName("hostButton");
    host->setDefault(true);
    host->setMinimumHeight(44);
    host->setStyleSheet("QPushButton#hostButton { font-size: 13pt; font-weight: bold; padding: 6px 18px; }");
    buttons->addStretch(1);
    buttons->addWidget(join);
    buttons->addWidget(host);
    buttons->addStretch(1);
    outer->addLayout(buttons);

    m_message = new QLabel(this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    outer->addWidget(m_message);
    outer->addStretch(2);

    connect(join, &QPushButton::clicked, this, [this]() {
        remember();
        emit joinRequested(m_host->text().trimmed(), static_cast<quint16>(m_port->value()), m_name->text().trimmed());
    });
    connect(host, &QPushButton::clicked, this, [this]() {
        remember();
        emit hostRequested(static_cast<quint16>(m_port->value()), m_name->text().trimmed());
    });
}

void ConnectPage::showMessage(const QString& text, bool error) {
    m_message->setStyleSheet(error ? "color: #c00000;" : "color: #404040;");
    m_message->setText(text);
}

void ConnectPage::remember() {
    QSettings settings;
    settings.setValue(QStringLiteral("connect/name"), m_name->text().trimmed());
    settings.setValue(QStringLiteral("connect/host"), m_host->text().trimmed());
    settings.setValue(QStringLiteral("connect/port"), m_port->value());
}

}  // namespace kot::client
