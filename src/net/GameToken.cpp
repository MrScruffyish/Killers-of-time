#include "net/GameToken.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <cstring>

namespace kot::net {

QString newGameToken() {
    QByteArray bytes(kGameTokenHexDigits / 2, '\0');
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < bytes.size(); i += 4) {
        const quint32 value = rng->generate();
        memcpy(bytes.data() + i, &value, 4);
    }
    return QString::fromLatin1(bytes.toHex());
}

QString normalizeGameToken(const QString& text) {
    QString token;
    token.reserve(text.size());
    for (const QChar c : text) {
        if (c.isSpace() || c == QLatin1Char('-')) continue;
        token.append(c.toLower());
    }
    return token;
}

bool looksLikeGameToken(const QString& token) {
    if (token.size() != kGameTokenHexDigits) return false;
    for (const QChar c : token)
        if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9')) && !(c >= QLatin1Char('a') && c <= QLatin1Char('f')))
            return false;
    return true;
}

QString hashGameToken(const QString& token) {
    if (token.isEmpty()) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
}

}  // namespace kot::net
