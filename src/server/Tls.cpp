#include "server/Tls.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace kot::server {

bool ensureTlsCertificate(const QString& directory, QString* certPath, QString* keyPath, QString* error) {
    const QString dir = directory;
    const QString cert = QDir(dir).filePath(QStringLiteral("server-cert.pem"));
    const QString key = QDir(dir).filePath(QStringLiteral("server-key.pem"));
    if (certPath) *certPath = cert;
    if (keyPath) *keyPath = key;
    if (QFileInfo::exists(cert) && QFileInfo::exists(key)) return true;

    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("cannot make the TLS folder %1").arg(dir);
        return false;
    }
    const QString openssl = QStandardPaths::findExecutable(QStringLiteral("openssl"));
    if (openssl.isEmpty()) {
        if (error) *error = QStringLiteral("openssl is not installed: it is needed once to make the server's certificate");
        return false;
    }
    QProcess process;
    process.start(openssl, {QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"), QStringLiteral("ec"),
                            QStringLiteral("-pkeyopt"), QStringLiteral("ec_paramgen_curve:prime256v1"),
                            QStringLiteral("-sha256"), QStringLiteral("-nodes"), QStringLiteral("-days"), QStringLiteral("3650"),
                            QStringLiteral("-keyout"), key, QStringLiteral("-out"), cert,
                            QStringLiteral("-subj"), QStringLiteral("/CN=Killers of Time")});
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("could not make the server's TLS certificate: %1")
                         .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        QFile::remove(cert);
        QFile::remove(key);
        return false;
    }
    QFile::setPermissions(key, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

}  // namespace kot::server
