#include "client/ConnectPage.h"

#include <QFormLayout>
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

ConnectPage::ConnectPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);

    auto* title = new QLabel(QStringLiteral("<h1>%1</h1>").arg(QString::fromLatin1(core::kGameName)), this);
    title->setAlignment(Qt::AlignCenter);
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
