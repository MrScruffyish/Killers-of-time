#include "core/data/DataDir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace kot::core {

namespace {
bool looksLikeDataDir(const QString& path) {
    return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath(QStringLiteral("resources.json")));
}
}  // namespace

QString findDataDirectory(const QString& explicitDir) {
    if (!explicitDir.isEmpty())
        return looksLikeDataDir(explicitDir) ? QDir(explicitDir).canonicalPath() : QString();

    QStringList candidates;
    candidates << qEnvironmentVariable("KOT_DATA_DIR");
    candidates << QDir::current().filePath(QStringLiteral("data"));
    const QString app = QCoreApplication::applicationDirPath();
    for (const char* rel : {"data", "../data", "../../data", "../../../data"})
        candidates << QDir(app).filePath(QString::fromLatin1(rel));
    for (const QString& c : candidates) {
        if (looksLikeDataDir(c)) return QDir(c).canonicalPath();
    }
    return {};
}

}  // namespace kot::core
