#include <QHash>
#include <QtTest>
#include <type_traits>

#include "core/util/Ids.h"

using namespace kot::core;

// Different id types must not convert to each other, nor from plain integers.
static_assert(!std::is_convertible_v<PlayerId, HeroId>);
static_assert(!std::is_convertible_v<std::uint32_t, PlayerId>);
static_assert(!std::is_constructible_v<PlayerId, HeroId>);

class TstStrongId : public QObject {
    Q_OBJECT
private slots:
    void defaultIsInvalid() {
        PlayerId id;
        QVERIFY(!id.isValid());
        QCOMPARE(id.value(), 0u);
    }

    void valueAndValidity() {
        PlayerId id(5);
        QVERIFY(id.isValid());
        QCOMPARE(id.value(), 5u);
    }

    void comparison() {
        QVERIFY(PlayerId(1) == PlayerId(1));
        QVERIFY(PlayerId(1) != PlayerId(2));
        QVERIFY(PlayerId(1) < PlayerId(2));
    }

    void usableAsHashKey() {
        QHash<HeroId, QString> heroes;
        heroes.insert(HeroId(1), QStringLiteral("Arthur"));
        heroes.insert(HeroId(2), QStringLiteral("Morgana"));
        QCOMPARE(heroes.value(HeroId(2)), QStringLiteral("Morgana"));
        QVERIFY(!heroes.contains(HeroId(3)));
    }
};

QTEST_APPLESS_MAIN(TstStrongId)
#include "tst_strongid.moc"
