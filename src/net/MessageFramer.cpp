#include "net/MessageFramer.h"

#include <QJsonDocument>
#include <QtEndian>

#include "net/Protocol.h"

namespace kot::net {

QByteArray MessageFramer::encode(const Message& message) {
    const QByteArray json = QJsonDocument(message.toJson()).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data());
    frame.append(json);
    return frame;
}

MessageFramer::Status MessageFramer::next(Message* out, QString* error) {
    const auto fail = [&](const QString& why) {
        m_failed = true;
        m_failure = why;
        if (error) *error = why;
        return Status::Error;
    };
    if (m_failed) {
        if (error) *error = m_failure;
        return Status::Error;
    }
    if (m_buffer.size() < 4) return Status::NeedMore;
    const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
    if (length == 0 || length > kMaxFrameBytes)
        return fail(QStringLiteral("invalid frame length %1").arg(length));
    if (static_cast<quint32>(m_buffer.size() - 4) < length) return Status::NeedMore;

    const QByteArray payload = m_buffer.mid(4, static_cast<qsizetype>(length));
    m_buffer.remove(0, 4 + static_cast<qsizetype>(length));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    if (!doc.isObject()) return fail(QStringLiteral("a message must be a JSON object"));
    QString why;
    if (!Message::fromJson(doc.object(), out, &why)) return fail(why);
    return Status::Message;
}

}  // namespace kot::net
