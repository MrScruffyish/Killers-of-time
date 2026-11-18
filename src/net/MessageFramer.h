#pragma once

#include <QByteArray>
#include <QString>

#include "net/Message.h"

namespace kot::net {

// Turns a byte stream into messages and back. Pure logic, no sockets.
// Frame = 4-byte big-endian length + UTF-8 JSON object.
class MessageFramer {
public:
    enum class Status { Message, NeedMore, Error };

    static QByteArray encode(const Message& message);

    void append(const QByteArray& data) { m_buffer.append(data); }
    // Extracts the next complete message; call repeatedly until it returns NeedMore.
    // After an Error the framer stays in the error state (the stream is out of sync).
    Status next(Message* out, QString* error);

private:
    QByteArray m_buffer;
    bool m_failed = false;
    QString m_failure;
};

}  // namespace kot::net
