#include "client/ConnectPage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QPainterPath>
#include <QPainter>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QSettings>
#include <QPushButton>
#include <QShortcut>
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

    // Your name and the sound choice are for every way of playing.
    auto* nameForm = new QFormLayout();
    m_name = new QLineEdit(qEnvironmentVariable("USER", QStringLiteral("Player")), this);
    m_name->setObjectName("nameEdit");
    QSettings settings;
    m_name->setText(settings.value(QStringLiteral("connect/name"), m_name->text()).toString());
    nameForm->addRow(tr("Your name"), m_name);
    auto* soundBox = new QCheckBox(tr("Sound effects (also in the game's menu)"), this);
    soundBox->setObjectName("soundCheck");
    soundBox->setChecked(!sound::muted());
    soundBox->setEnabled(sound::available());
    if (!sound::available()) soundBox->setToolTip(tr("No sound in this build or this run"));
    connect(soundBox, &QCheckBox::toggled, this, [](bool on) { sound::setMuted(!on); });
    nameForm->addRow(QString(), soundBox);
    auto* nameHolder = new QWidget(this);
    nameHolder->setLayout(nameForm);
    nameHolder->setMaximumWidth(420);
    outer->addWidget(nameHolder, 0, Qt::AlignHCenter);

    // The three ways to play, side by side.
    auto* buttons = new QHBoxLayout();
    auto* solo = new QPushButton(tr("Solo game"), this);
    solo->setObjectName("soloButton");
    solo->setToolTip(tr("A game on this computer: you against the computer."));
    solo->setDefault(true);
    auto* hotseat = new QPushButton(tr("Hot seat"), this);
    hotseat->setObjectName("hotseatButton");
    hotseat->setToolTip(tr("Several players take turns at this computer."));
    auto* network = new QPushButton(tr("Network game"), this);
    network->setObjectName("networkButton");
    network->setToolTip(tr("Join a server: set the address, port and game token below."));
    auto* load = new QPushButton(tr("Load game"), this);
    load->setObjectName("loadGameButton");
    load->setToolTip(tr("Load a game saved on this computer."));
    buttons->addStretch(1);
    for (QPushButton* button : {solo, hotseat, network, load}) {
        button->setMinimumHeight(44);
        button->setStyleSheet(
            "QPushButton { font-size: 12pt; font-weight: bold; padding: 6px 18px; }");
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    outer->addLayout(buttons);

    // The server fields: only a network game uses them.
    auto* networkBox = new QGroupBox(tr("Network game"), this);
    networkBox->setObjectName("networkBox");
    auto* form = new QFormLayout(networkBox);
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"), networkBox);
    m_host->setObjectName("hostEdit");
    m_port = new QSpinBox(networkBox);
    m_port->setObjectName("portEdit");
    m_port->setRange(1, 65535);
    m_port->setValue(net::kDefaultPort);
    m_host->setText(settings.value(QStringLiteral("connect/host"), m_host->text()).toString());
    m_port->setValue(settings.value(QStringLiteral("connect/port"), m_port->value()).toInt());
    form->addRow(tr("Server"), m_host);
    form->addRow(tr("Port"), m_port);
    m_token = new QLineEdit(networkBox);
    m_token->setObjectName("tokenEdit");
    m_token->setPlaceholderText(tr("optional: a game token from another computer"));
    m_token->setToolTip(tr("To play your seats from another computer, enter the game token from that game's "
                           "Game menu there. Leave it empty on the computer that played them first: it makes "
                           "and keeps a token by itself."));
    form->addRow(tr("Game token"), m_token);
    m_tls = new QCheckBox(tr("Encrypt the connection (TLS)"), networkBox);
    m_tls->setObjectName("tlsCheck");
    m_tls->setChecked(settings.value(QStringLiteral("connect/tls"), false).toBool());
    m_tls->setToolTip(tr("Only when the server was started with --tls. A local game started here "
                         "uses it too when this is checked."));
    form->addRow(QString(), m_tls);
    auto* connectButton = new QPushButton(tr("Connect"), networkBox);
    connectButton->setObjectName("connectButton");
    connectButton->setMinimumHeight(40);
    form->addRow(QString(), connectButton);
    networkBox->setMaximumWidth(420);
    networkBox->setVisible(false);  // only shown after "Network game" is chosen
    outer->addWidget(networkBox, 0, Qt::AlignHCenter);

    m_message = new QLabel(this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    outer->addWidget(m_message);
    outer->addStretch(2);

    connect(solo, &QPushButton::clicked, this, [this, networkBox]() {
        networkBox->hide();
        remember();
        emit soloRequested(static_cast<quint16>(m_port->value()), m_name->text().trimmed(),
                           m_tls->isChecked());
    });
    connect(hotseat, &QPushButton::clicked, this, [this, networkBox]() {
        networkBox->hide();
        remember();
        emit hotseatRequested(static_cast<quint16>(m_port->value()), m_name->text().trimmed(),
                              m_tls->isChecked());
    });
    connect(network, &QPushButton::clicked, this, [this, networkBox]() {
        networkBox->show();  // first choose the way to play, then fill in the server
    });
    connect(connectButton, &QPushButton::clicked, this, [this]() {
        remember();
        emit joinRequested(m_host->text().trimmed(), static_cast<quint16>(m_port->value()),
                           m_name->text().trimmed(), m_token->text(), m_tls->isChecked());
    });
    connect(load, &QPushButton::clicked, this, &ConnectPage::requestLoad);
    // Ctrl+L takes the same way in as the Load game button.
    auto* loadKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    loadKey->setObjectName(QStringLiteral("loadShortcut"));
    loadKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(loadKey, &QShortcut::activated, this, &ConnectPage::requestLoad);
}

void ConnectPage::requestLoad() {
    if (auto* networkBox = findChild<QGroupBox*>(QStringLiteral("networkBox")))
        networkBox->hide();
    remember();
    emit loadRequested(static_cast<quint16>(m_port->value()), m_name->text().trimmed(),
                       m_tls->isChecked());
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
    settings.setValue(QStringLiteral("connect/tls"), m_tls->isChecked());
}

}  // namespace kot::client
