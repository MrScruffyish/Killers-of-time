#include <QtTest>

#include "client/Sound.h"

using namespace kot::client;

// Plays many overlapping effects through the real audio output. Off in normal
// test runs (no sound in CI); run by hand with KOT_SOUND_TEST=1 to check that a
// burst of effects (archers shooting twice, many battle events) does not crash.
class TstSound : public QObject {
    Q_OBJECT
private slots:
    void burstOfEffectsDoesNotCrash() {
        if (qEnvironmentVariableIsEmpty("KOT_SOUND_TEST")) QSKIP("set KOT_SOUND_TEST=1 to play sounds");
        if (!sound::available()) QSKIP("no sound here");
        sound::setMuted(false);
        using sound::Effect;
        for (int i = 0; i < 40; ++i) {
            sound::play(i % 2 ? Effect::Clash : Effect::Spell);
            if (i % 5 == 0) QTest::qWait(30);
        }
        QTest::qWait(1500);
        sound::play(Effect::Fanfare);
        QTest::qWait(500);
        // Off closes the output in the middle of a sound; on opens it again.
        sound::setMuted(true);
        sound::play(Effect::Bell);
        QTest::qWait(200);
        sound::setMuted(false);
        sound::play(Effect::Bell);
        QTest::qWait(800);
    }
};

QTEST_MAIN(TstSound)
#include "tst_sound.moc"
