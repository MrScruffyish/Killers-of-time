#include "core/data/JsonReader.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>

namespace kot::core {

bool isValidId(const QString& id) {
    static const QRegularExpression re(QStringLiteral("^[a-z][a-z0-9_]*$"));
    return re.match(id).hasMatch();
}

JsonReader::JsonReader(const QJsonObject& object, QString context, QStringList* errors)
    : m_object(object), m_context(std::move(context)), m_errors(errors) {}

void JsonReader::addError(const QString& message) {
    ++m_errorCount;
    if (m_errors) m_errors->append(m_context + QStringLiteral(": ") + message);
}

std::optional<QString> JsonReader::parseString(const QString& key) {
    m_used.insert(key);
    const QJsonValue v = m_object.value(key);
    if (!v.isString()) {
        addError(QStringLiteral("field '%1' must be a string").arg(key));
        return std::nullopt;
    }
    return v.toString();
}

QString JsonReader::requireString(const QString& key) {
    if (!has(key)) {
        addError(QStringLiteral("missing required field '%1'").arg(key));
        return {};
    }
    const auto s = parseString(key);
    if (s && s->isEmpty()) {
        addError(QStringLiteral("field '%1' must not be empty").arg(key));
        return {};
    }
    return s.value_or(QString());
}

QString JsonReader::requireId(const QString& key) {
    const QString s = requireString(key);
    if (!s.isEmpty() && !isValidId(s)) {
        addError(QStringLiteral("field '%1' must start with a lowercase letter and contain only "
                                "lowercase letters, digits and '_' (got '%2')")
                     .arg(key, s));
        return {};
    }
    return s;
}

QString JsonReader::optionalString(const QString& key, const QString& def) {
    if (!has(key)) return def;
    return parseString(key).value_or(def);
}

QString JsonReader::requireColor(const QString& key) {
    const QString s = requireString(key);
    static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if (!s.isEmpty() && !re.match(s).hasMatch()) {
        addError(QStringLiteral("field '%1' must be a color like #RRGGBB (got '%2')").arg(key, s));
        return {};
    }
    return s;
}

std::optional<int> JsonReader::parseInt(const QString& key, int min, int max) {
    m_used.insert(key);
    const QJsonValue v = m_object.value(key);
    if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble())) {
        addError(QStringLiteral("field '%1' must be an integer").arg(key));
        return std::nullopt;
    }
    const double d = v.toDouble();
    if (d < min || d > max) {
        addError(QStringLiteral("field '%1' must be between %2 and %3 (got %4)")
                     .arg(key)
                     .arg(min)
                     .arg(max)
                     .arg(d));
        return std::nullopt;
    }
    return static_cast<int>(d);
}

int JsonReader::requireInt(const QString& key, int min, int max) {
    if (!has(key)) {
        addError(QStringLiteral("missing required field '%1'").arg(key));
        return min;
    }
    return parseInt(key, min, max).value_or(min);
}

int JsonReader::optionalInt(const QString& key, int def, int min, int max) {
    if (!has(key)) return def;
    return parseInt(key, min, max).value_or(def);
}

bool JsonReader::requireBool(const QString& key) {
    if (!has(key)) {
        addError(QStringLiteral("missing required field '%1'").arg(key));
        return false;
    }
    return optionalBool(key, false);
}

bool JsonReader::optionalBool(const QString& key, bool def) {
    if (!has(key)) return def;
    m_used.insert(key);
    const QJsonValue v = m_object.value(key);
    if (!v.isBool()) {
        addError(QStringLiteral("field '%1' must be true or false").arg(key));
        return def;
    }
    return v.toBool();
}

QStringList JsonReader::optionalStringList(const QString& key) {
    QStringList result;
    if (!has(key)) return result;
    m_used.insert(key);
    const QJsonValue v = m_object.value(key);
    if (!v.isArray()) {
        addError(QStringLiteral("field '%1' must be a list of strings").arg(key));
        return result;
    }
    for (const QJsonValue& item : v.toArray()) {
        if (!item.isString()) {
            addError(QStringLiteral("field '%1' must contain only strings").arg(key));
            return {};
        }
        result.append(item.toString());
    }
    return result;
}

QMap<QString, int> JsonReader::optionalIntMap(const QString& key, int min, int max) {
    QMap<QString, int> result;
    if (!has(key)) return result;
    m_used.insert(key);
    const QJsonValue v = m_object.value(key);
    if (!v.isObject()) {
        addError(QStringLiteral("field '%1' must be an object of name: number").arg(key));
        return result;
    }
    const QJsonObject obj = v.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const double d = it.value().toDouble(-1);
        if (!it.value().isDouble() || d != std::floor(d) || d < min || d > max) {
            addError(QStringLiteral("field '%1': entry '%2' must be an integer between %3 and %4")
                         .arg(key, it.key())
                         .arg(min)
                         .arg(max));
            continue;
        }
        result.insert(it.key(), static_cast<int>(d));
    }
    return result;
}

QJsonArray JsonReader::requireArray(const QString& key) {
    if (!has(key)) {
        addError(QStringLiteral("missing required field '%1'").arg(key));
        return {};
    }
    m_used.insert(key);
    if (!m_object.value(key).isArray()) {
        addError(QStringLiteral("field '%1' must be a list").arg(key));
        return {};
    }
    return m_object.value(key).toArray();
}

QJsonObject JsonReader::requireObject(const QString& key) {
    if (!has(key)) {
        addError(QStringLiteral("missing required field '%1'").arg(key));
        return {};
    }
    m_used.insert(key);
    if (!m_object.value(key).isObject()) {
        addError(QStringLiteral("field '%1' must be an object").arg(key));
        return {};
    }
    return m_object.value(key).toObject();
}

void JsonReader::finish() {
    const QStringList keys = m_object.keys();
    for (const QString& key : keys) {
        if (key.startsWith(QLatin1Char('_')) || m_used.contains(key)) continue;
        addError(QStringLiteral("unknown field '%1'").arg(key));
        m_used.insert(key);
    }
}

}  // namespace kot::core
