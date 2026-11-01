#pragma once

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

namespace kot::core {

// True for ids like "gold", "battle_snail": lowercase letter first, then
// lowercase letters, digits and '_'.
bool isValidId(const QString& id);

// Reads typed fields from one JSON object and reports problems (missing
// field, wrong type, out of range, unknown field) as readable messages:
//     "units/eagle.json: field 'maxHp' must be between 1 and 100000 (got 0)"
// All problems are collected, so a data file can be fixed in one pass.
// Keys starting with '_' are ignored (use them for comments in data files).
class JsonReader {
public:
    JsonReader(const QJsonObject& object, QString context, QStringList* errors);

    QString requireId(const QString& key);
    QString requireString(const QString& key);
    QString optionalString(const QString& key, const QString& def = QString());
    QString requireColor(const QString& key);  // "#RRGGBB"

    int requireInt(const QString& key, int min, int max);
    int optionalInt(const QString& key, int def, int min, int max);
    bool requireBool(const QString& key);
    bool optionalBool(const QString& key, bool def);

    QStringList optionalStringList(const QString& key);
    // Object of name -> integer, e.g. a cost {"gold": 100, "wood": 2}.
    QMap<QString, int> optionalIntMap(const QString& key, int min, int max);

    bool has(const QString& key) const { return m_object.contains(key); }
    void addError(const QString& message);
    // Call last: reports fields nobody asked for (usually typos).
    void finish();
    bool ok() const { return m_errorCount == 0; }

private:
    std::optional<int> parseInt(const QString& key, int min, int max);
    std::optional<QString> parseString(const QString& key);

    QJsonObject m_object;
    QString m_context;
    QStringList* m_errors;
    QSet<QString> m_used;
    int m_errorCount = 0;
};

}  // namespace kot::core
