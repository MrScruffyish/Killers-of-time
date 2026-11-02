#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"

using namespace kot::core;

class TstDataDir : public QObject {
    Q_OBJECT
private slots:
    void explicitDirectoryIsUsedWhenItHasResources() {
        QTemporaryDir tmp;
        QFile f(tmp.filePath("resources.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QCOMPARE(findDataDirectory(tmp.path()), QDir(tmp.path()).canonicalPath());
    }

    void explicitDirectoryWithoutResourcesIsRejectedNotSearched() {
        QTemporaryDir tmp;
        QVERIFY(findDataDirectory(tmp.path()).isEmpty());
        QVERIFY(findDataDirectory("/nonexistent/dir").isEmpty());
    }

    void environmentVariableIsUsed() {
        qputenv("KOT_DATA_DIR", KOT_DATA_DIR);
        QCOMPARE(findDataDirectory(), QDir(KOT_DATA_DIR).canonicalPath());
        qunsetenv("KOT_DATA_DIR");
    }

    void raceIdsComeFromTheHeroClasses() {
        ContentRegistry reg;
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(reg.raceIds(), QStringList({"humans", "sylvan", "undead"}));
    }
};

QTEST_GUILESS_MAIN(TstDataDir)
#include "tst_datadir.moc"
