#pragma once

#include <QString>

namespace kot::server {

// Makes a self-signed certificate and key for the server in `directory` if they are
// not there yet (openssl's default names: server-cert.pem / server-key.pem).
// TLS only encrypts the game: players prove who they are with their game token, so
// the certificate does not need to be signed by anyone.
bool ensureTlsCertificate(const QString& directory, QString* certPath, QString* keyPath, QString* error);

}  // namespace kot::server
