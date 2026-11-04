#include "net/Message.h"

namespace kot::net {

QJsonObject Message::toJson() const {
    QJsonObject json = body;
    json.insert(QStringLiteral("type"), type);
    if (seq > 0) json.insert(QStringLiteral("seq"), seq);
    return json;
}

bool Message::fromJson(const QJsonObject& json, Message* out, QString* error) {
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString().isEmpty()) {
        if (error) *error = QStringLiteral("message has no \"type\"");
        return false;
    }
    int seq = 0;
    if (json.contains(QStringLiteral("seq"))) {
        const QJsonValue s = json.value(QStringLiteral("seq"));
        if (!s.isDouble() || s.toDouble() < 0 || s.toDouble() > 2000000000.0) {
            if (error) *error = QStringLiteral("\"seq\" must be a non-negative integer");
            return false;
        }
        seq = static_cast<int>(s.toDouble());
    }
    Message m;
    m.type = type.toString();
    m.seq = seq;
    m.body = json;
    m.body.remove(QStringLiteral("type"));
    m.body.remove(QStringLiteral("seq"));
    *out = m;
    return true;
}

}  // namespace kot::net
