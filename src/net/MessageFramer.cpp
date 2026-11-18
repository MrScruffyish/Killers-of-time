#include "net/MessageFramer.h"

#include <QJsonDocument>
#include <QtEndian>

#include "net/Protocol.h"

namespace kot::net {

QByteArray MessageFramer::encode(const Message& message) {
    const QByteArray json = QJsonDocument(message.toJson()).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    // Bigger messages (game views: tens of KB of JSON) go zlib-compressed (qCompress:
    // 4-byte big-endian original size, then the zlib stream); the length's top bit says so.
    if (json.size() >= kCompressFromBytes) {
        const QByteArray packed = qCompress(json, 1);  // level 1: fast, most of the gain
        qToBigEndian<quint32>(static_cast<quint32>(packed.size()) | kCompressedFlag, frame.data());
        frame.append(packed);
        return frame;
    }
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
    const quint32 header = qFromBigEndian<quint32>(m_buffer.constData());
    const bool compressed = (header & kCompressedFlag) != 0;
    const quint32 length = header & ~kCompressedFlag;
    if (length == 0 || length > kMaxFrameBytes)
        return fail(QStringLiteral("invalid frame length %1").arg(length));
    if (static_cast<quint32>(m_buffer.size() - 4) < length) return Status::NeedMore;

    QByteArray payload = m_buffer.mid(4, static_cast<qsizetype>(length));
    m_buffer.remove(0, 4 + static_cast<qsizetype>(length));
    if (compressed) {
        // The unpacked size is checked before unpacking (no huge allocation from a small frame).
        if (payload.size() < 4) return fail(QStringLiteral("compressed frame too short"));
        const quint32 unpacked = qFromBigEndian<quint32>(payload.constData());
        if (unpacked == 0 || unpacked > kMaxFrameBytes)
            return fail(QStringLiteral("invalid unpacked size %1").arg(unpacked));
        payload = qUncompress(payload);
        if (payload.isEmpty()) return fail(QStringLiteral("cannot unpack the compressed frame"));
    }

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
