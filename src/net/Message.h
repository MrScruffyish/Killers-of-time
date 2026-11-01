#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace kot::net {

// One protocol message: {"type": "...", "seq": n, ...body fields}.
// `seq` is optional; requests carry one and an error reply names it in "replyTo".
struct Message {
    QString type;
    QJsonObject body;
    int seq = 0;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, Message* out, QString* error);
};

}  // namespace kot::net

Q_DECLARE_METATYPE(kot::net::Message)
