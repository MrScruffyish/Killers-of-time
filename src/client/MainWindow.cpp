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
    connect(&m_socket, &QTcpSocket::connected, this, &MainWindow::onConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &MainWindow::onDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    connect(&m_socket, &QAbstractSocket::errorOccurred, this, &MainWindow::onError);

    updateButton();
    log(tr("Client started. Protocol version %1.").arg(net::kProtocolVersion));
}

void MainWindow::onConnectClicked() {
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        log(tr("Connecting to %1:%2 ...").arg(m_host->text()).arg(m_port->value()));
        m_socket.connectToHost(m_host->text(), static_cast<quint16>(m_port->value()));
    } else {
        m_socket.disconnectFromHost();
    }
    updateButton();
}

void MainWindow::onConnected() {
    log(tr("Connected."));
    updateButton();
}

void MainWindow::onDisconnected() {
    log(tr("Disconnected."));
    updateButton();
}

void MainWindow::onReadyRead() {
    while (m_socket.canReadLine()) {
        log(tr("Server says: %1").arg(QString::fromUtf8(m_socket.readLine()).trimmed()));
    }
}

void MainWindow::onError(QAbstractSocket::SocketError) {
    log(tr("Error: %1").arg(m_socket.errorString()));
    updateButton();
}

void MainWindow::log(const QString& text) {
    m_log->appendPlainText(text);
}

void MainWindow::updateButton() {
    const bool idle = m_socket.state() == QAbstractSocket::UnconnectedState;
    m_connectButton->setText(idle ? tr("Connect") : tr("Disconnect"));
    m_host->setEnabled(idle);
    m_port->setEnabled(idle);
}

}  // namespace kot::client
