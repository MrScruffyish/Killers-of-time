#include <QJsonDocument>
#include <QtTest>

#include "core/data/JsonReader.h"

using namespace kot::core;

static QJsonObject obj(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

class TstJsonReader : public QObject {
    Q_OBJECT
private slots:
    void validObject() {
        QStringList errors;
        JsonReader r(obj(R"({"id":"gold","n":5,"flag":true,"tags":["a","b"],"cost":{"gold":3}})"),
                     "ctx", &errors);
        QCOMPARE(r.requireId("id"), QString("gold"));
        QCOMPARE(r.requireInt("n", 1, 10), 5);
        QCOMPARE(r.requireBool("flag"), true);
        QCOMPARE(r.optionalStringList("tags"), QStringList({"a", "b"}));
        QCOMPARE(r.optionalIntMap("cost", 1, 100).value("gold"), 3);
        r.finish();
        QVERIFY2(r.ok(), qPrintable(errors.join("\n")));
        QVERIFY(errors.isEmpty());
    }

    void missingRequiredField() {
        QStringList errors;
        JsonReader r(obj("{}"), "units/a.json", &errors);
        r.requireString("name");
        QVERIFY(!r.ok());
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0].contains("units/a.json"));
        QVERIFY(errors[0].contains("missing required field 'name'"));
    }

    void wrongType() {
        QStringList errors;
        JsonReader r(obj(R"({"n":"five","s":3,"b":"yes"})"), "c", &errors);
        r.requireInt("n", 0, 10);
        r.requireString("s");
        r.requireBool("b");
        QCOMPARE(errors.size(), 3);
    }

    void intRangeAndFraction() {
        QStringList errors;
        JsonReader r(obj(R"({"low":0,"high":11,"frac":2.5})"), "c", &errors);
        r.requireInt("low", 1, 10);
        r.requireInt("high", 1, 10);
        r.requireInt("frac", 1, 10);
        QCOMPARE(errors.size(), 3);
        QVERIFY(errors[0].contains("between 1 and 10"));
    }

    void optionalDefaults() {
        QStringList errors;
        JsonReader r(obj("{}"), "c", &errors);
        QCOMPARE(r.optionalInt("x", 7, 0, 10), 7);
        QCOMPARE(r.optionalBool("b", true), true);
        QCOMPARE(r.optionalString("s", "d"), QString("d"));
        QVERIFY(r.optionalStringList("l").isEmpty());
        QVERIFY(r.ok());
    }

    void badIdAndColor() {
        QStringList errors;
        JsonReader r(obj(R"({"id":"Bad Id","color":"red"})"), "c", &errors);
        r.requireId("id");
        r.requireColor("color");
        QCOMPARE(errors.size(), 2);
    }

    void unknownFieldsReportedButUnderscoreIgnored() {
        QStringList errors;
        JsonReader r(obj(R"({"id":"a","atack":4,"_comment":"note"})"), "c", &errors);
        r.requireId("id");
        r.finish();
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0].contains("unknown field 'atack'"));
    }

    void validIds() {
        QVERIFY(isValidId("gold"));
        QVERIFY(isValidId("battle_snail2"));
        QVERIFY(!isValidId(""));
        QVERIFY(!isValidId("2fast"));
        QVERIFY(!isValidId("Gold"));
        QVERIFY(!isValidId("a-b"));
    }
};

QTEST_APPLESS_MAIN(TstJsonReader)
#include "tst_jsonreader.moc"
