#pragma once

#include <QJsonArray>

#include "core/content/ContentIds.h"

namespace kot::core {

// Resource amounts in saves and network messages: one number per resource id, in id
// order, trailing zeros left out ([1500, 0, 20] = 1500 of resource 0, 20 of resource 2).
QJsonArray amountsToJson(const ResourceAmounts& amounts);
// The reverse; entries past `resourceCount`, or that are not whole numbers, are
// reported in `error` (and left out).
ResourceAmounts amountsFromJson(const QJsonArray& json, int resourceCount, QString* error = nullptr);

}  // namespace kot::core
