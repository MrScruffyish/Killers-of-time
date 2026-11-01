#include "client/MainWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/Version.h"
#include "net/Protocol.h"

namespace kot::client {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("%1 %2")
                       .arg(QString::fromLatin1(core::kGameName),
                            QString::fromLatin1(core::kGameVersion)));
    resize(900, 600);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Name:"), central));
    m_name = new QLineEdit(QStringLiteral("Player"), central);
    row->addWidget(m_name);
    row->addWidget(new QLabel(tr("Server:"), central));
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"), central);
    row->addWidget(m_host, 1);
    m_port = new QSpinBox(central);
    m_port->setRange(1, 65535);
    m_port->setValue(net::kDefaultPort);
    row->addWidget(m_port);
    m_connectButton = new QPushButton(central);
    row->addWidget(m_connectButton);
    layout->addLayout(row);

    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    layout->addWidget(m_log, 1);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(&m_client, &net::LobbyClient::connected, this, [this]() {
        m_open = true;
        log(tr("Connected as client %1.").arg(m_client.clientId()));
        updateButton();
    });
    connect(&m_client, &net::LobbyClient::lobbyChanged, this, &MainWindow::onLobbyChanged);
    connect(&m_client, &net::LobbyClient::errorReceived, this,
            [this](const QString& code, const QString& text, int) { log(tr("Server: %1 (%2)").arg(text, code)); });
    connect(&m_client, &net::LobbyClient::connectionFailed, this,
            [this](const QString& why) { log(tr("Connection problem: %1").arg(why)); });
    connect(&m_client, &net::LobbyClient::disconnected, this, [this]() {
        m_open = false;
        log(tr("Disconnected."));
        updateButton();
    });

    updateButton();
    log(tr("Client started. Protocol version %1.").arg(net::kProtocolVersion));
}

void MainWindow::onConnectClicked() {
    if (m_open) {
        m_client.disconnectFromServer();
        m_open = false;
        log(tr("Disconnected."));
    } else {
        log(tr("Connecting to %1:%2 ...").arg(m_host->text()).arg(m_port->value()));
        m_client.connectToServer(m_host->text(), static_cast<quint16>(m_port->value()), m_name->text());
    }
    updateButton();
}

void MainWindow::onLobbyChanged() {
    const net::LobbyState& lobby = m_client.lobby();
    QStringList lines;
    for (const net::SlotInfo& s : lobby.slotList) {
        lines << tr("  slot %1: %2 %3 (team %4, %5)%6")
                     .arg(s.index + 1)
                     .arg(net::slotKindName(s.kind), s.name, QString::number(s.team), s.race,
                          s.ready && s.occupied() ? tr(", ready") : QString());
    }
    log(tr("Lobby: %1 members\n%2").arg(lobby.members.size()).arg(lines.join('\n')));
}

void MainWindow::log(const QString& text) {
    m_log->appendPlainText(text);
}

void MainWindow::updateButton() {
    m_connectButton->setText(m_open ? tr("Disconnect") : tr("Connect"));
    m_name->setEnabled(!m_open);
    m_host->setEnabled(!m_open);
    m_port->setEnabled(!m_open);
}

}  // namespace kot::client
