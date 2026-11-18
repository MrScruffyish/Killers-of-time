#pragma once

#include <QLoggingCategory>

// One logging category per module. Usage: qCInfo(lcKotServer) << "text";
// Filter at runtime, e.g.:  QT_LOGGING_RULES="kot.net.debug=true"
Q_DECLARE_LOGGING_CATEGORY(lcKotCore)
Q_DECLARE_LOGGING_CATEGORY(lcKotNet)
Q_DECLARE_LOGGING_CATEGORY(lcKotServer)
Q_DECLARE_LOGGING_CATEGORY(lcKotClient)
Q_DECLARE_LOGGING_CATEGORY(lcKotAi)
