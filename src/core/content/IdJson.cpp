#include "core/content/IdJson.h"

#include <QString>

namespace kot::core {

QJsonArray amountsToJson(const ResourceAmounts& amounts) {
    QJsonArray a;
    int last = -1;
    for (const auto [r, amount] : amounts) last = r.index();
    for (int i = 0; i <= last; ++i) a.append(amounts.value(ResourceId(i)));
    return a;
}

ResourceAmounts amountsFromJson(const QJsonArray& json, int resourceCount, QString* error) {
    ResourceAmounts out;
    for (int i = 0; i < json.size(); ++i) {
        if (i >= resourceCount || i >= ResourceAmounts::kMax) {
            if (error) *error = QStringLiteral("%1 amounts; there are %2 resources").arg(json.size()).arg(resourceCount);
            break;
        }
        if (!json[i].isDouble() || json[i].toDouble() != double(json[i].toInt())) {
            if (error) *error = QStringLiteral("amount %1 is not a whole number").arg(i);
            continue;
        }
        out[ResourceId(i)] = json[i].toInt();
    }
    return out;
}

}  // namespace kot::core
