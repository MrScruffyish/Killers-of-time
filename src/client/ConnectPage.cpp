#include "client/ConnectPage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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
    form->addRow(tr("Your name"), m_name);
    form->addRow(tr("Server"), m_host);
    form->addRow(tr("Port"), m_port);
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
        emit joinRequested(m_host->text().trimmed(), static_cast<quint16>(m_port->value()), m_name->text().trimmed());
    });
    connect(host, &QPushButton::clicked, this, [this]() {
        emit hostRequested(static_cast<quint16>(m_port->value()), m_name->text().trimmed());
    });
}

void ConnectPage::showMessage(const QString& text, bool error) {
    m_message->setStyleSheet(error ? "color: #c00000;" : "color: #404040;");
    m_message->setText(text);
}

}  // namespace kot::client
