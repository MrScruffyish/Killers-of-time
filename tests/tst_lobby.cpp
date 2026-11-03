#include <QtTest>

#include "server/Lobby.h"

using namespace kot::server;
using kot::net::SlotKind;

class TstLobby : public QObject {
    Q_OBJECT
    Lobby lobby{{"humans", "undead"}, {"passive", "rusher"}, 4};

    const kot::net::SlotInfo& slot(int i) { return lobby.state().slotList[i]; }

    // Two humans (1 host, 2), both ready, on different teams.
    void readyTwoPlayers() {
        QVERIFY(lobby.addMember(1, "Ann").ok);
        QVERIFY(lobby.addMember(2, "Bob").ok);
        QVERIFY(lobby.setReady(1, true).ok);
        QVERIFY(lobby.setReady(2, true).ok);
    }

private slots:
    void init() { lobby = Lobby({"humans", "undead"}, {"passive", "rusher"}, 4); }

    void firstMemberIsHostAndTakesFirstSlot() {
        QVERIFY(lobby.addMember(1, "Ann").ok);
        QCOMPARE(lobby.hostId(), 1);
        QCOMPARE(lobby.slotOf(1), 0);
        QCOMPARE(slot(0).kind, SlotKind::Human);
        QCOMPARE(slot(0).name, QString("Ann"));
        QVERIFY(lobby.addMember(2, "Bob").ok);
        QCOMPARE(lobby.hostId(), 1);
        QCOMPARE(lobby.slotOf(2), 1);
    }

    void duplicateJoinRejected() {
        lobby.addMember(1, "Ann");
        QCOMPARE(lobby.addMember(1, "Ann").code, QString("already_joined"));
    }

    void membersBeyondSlotsStayWithoutSlot() {
        for (int i = 1; i <= 5; ++i) lobby.addMember(i, QString("P%1").arg(i));
        QCOMPARE(lobby.state().members.size(), 5);
        QCOMPARE(lobby.slotOf(5), -1);
        QVERIFY(lobby.claimSlot(5, 0).code == "slot_taken");
    }

    void claimMovesToAnotherOpenSlot() {
        lobby.addMember(1, "Ann");
        QVERIFY(lobby.claimSlot(1, 3).ok);
        QCOMPARE(lobby.slotOf(1), 3);
        QCOMPARE(slot(0).kind, SlotKind::Open);  // the old slot is free again
    }

    void claimErrors() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        QCOMPARE(lobby.claimSlot(2, 0).code, QString("slot_taken"));
        QCOMPARE(lobby.claimSlot(2, 9).code, QString("no_such_slot"));
        QCOMPARE(lobby.claimSlot(99, 2).code, QString("unknown_client"));
        QVERIFY(lobby.editSlot(1, 2, {{"kind", "closed"}}).ok);
        QCOMPARE(lobby.claimSlot(2, 2).code, QString("slot_closed"));
    }

    void releaseSlot() {
        lobby.addMember(1, "Ann");
        QVERIFY(lobby.releaseSlot(1).ok);
        QCOMPARE(lobby.slotOf(1), -1);
        QCOMPARE(lobby.releaseSlot(1).code, QString("no_slot"));
    }

    void leavingFreesTheSlotAndPassesTheHost() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        lobby.removeMember(1);
        QCOMPARE(lobby.hostId(), 2);
        QCOMPARE(slot(0).kind, SlotKind::Open);
        QVERIFY(lobby.state().members.first().isHost);
        lobby.removeMember(2);
        QVERIFY(lobby.state().members.isEmpty());
        QCOMPARE(lobby.hostId(), 0);
    }

    // ---- editing slots
    void hostMakesAnAiSlot() {
        lobby.addMember(1, "Ann");
        QVERIFY(lobby.editSlot(1, 2, {{"kind", "ai"}}).ok);
        const auto& s = slot(2);
        QCOMPARE(s.kind, SlotKind::Ai);
        QCOMPARE(s.name, QString("AI 3"));
        QCOMPARE(s.aiProfile, QString("passive"));
        QCOMPARE(s.difficulty, QString("normal"));
        QVERIFY(s.ready);
        QVERIFY(lobby.editSlot(1, 2, {{"difficulty", "hard"}, {"aiProfile", "rusher"}, {"name", "Bot"}}).ok);
        QCOMPARE(slot(2).difficulty, QString("hard"));
        QCOMPARE(slot(2).name, QString("Bot"));
        QVERIFY(lobby.editSlot(1, 2, {{"kind", "open"}}).ok);
        QCOMPARE(slot(2).kind, SlotKind::Open);
        QVERIFY(slot(2).aiProfile.isEmpty());
    }

    void hostCanKickByChangingKind() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        QVERIFY(lobby.editSlot(1, 1, {{"kind", "ai"}}).ok);
        QCOMPARE(lobby.slotOf(2), -1);
        QVERIFY(lobby.hasMember(2));
    }

    void playersChangeOnlyTheirOwnRaceTeamColor() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        QVERIFY(lobby.editSlot(2, 1, {{"race", "undead"}, {"team", 2}, {"color", "#123456"}}).ok);
        QCOMPARE(slot(1).race, QString("undead"));
        QCOMPARE(slot(1).team, 2);
        QCOMPARE(slot(1).color, QString("#123456"));
        QCOMPARE(lobby.editSlot(2, 0, {{"race", "undead"}}).code, QString("not_your_slot"));
        QCOMPARE(lobby.editSlot(2, 1, {{"kind", "closed"}}).code, QString("not_host"));
        QCOMPARE(lobby.editSlot(2, 1, {{"aiProfile", "x"}}).code, QString("not_host"));
    }

    void invalidValues() {
        lobby.addMember(1, "Ann");
        QCOMPARE(lobby.editSlot(1, 0, {{"race", "elves"}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 0, {{"team", 0}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 0, {{"team", 9}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 0, {{"color", "red"}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 0, {{"kind", "human"}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 0, {{"colour", "#ffffff"}}).code, QString("invalid_value"));
        QCOMPARE(lobby.editSlot(1, 2, {{"difficulty", "hard"}}).code, QString("invalid_value"));  // not an AI
        QCOMPARE(lobby.editSlot(1, 7, {{"race", "humans"}}).code, QString("no_such_slot"));
    }

    void failedEditChangesNothing() {
        lobby.addMember(1, "Ann");
        const QString before = slot(0).race;
        QVERIFY(!lobby.editSlot(1, 0, {{"race", "undead"}, {"team", 99}}).ok);
        QCOMPARE(slot(0).race, before);
    }

    void coloursMustBeUnique() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        const QString bobsColor = slot(1).color;
        QCOMPARE(lobby.editSlot(1, 0, {{"color", bobsColor}}).code, QString("color_taken"));
        // Open slots do not reserve colours.
        QVERIFY(lobby.editSlot(1, 0, {{"color", slot(3).color}}).ok);
    }

    void claimingAvoidsAColourClash() {
        lobby.addMember(1, "Ann");
        lobby.editSlot(1, 0, {{"color", slot(1).color}});  // Ann takes slot 1's default colour
        lobby.addMember(2, "Bob");                          // Bob lands in slot 1
        QVERIFY(slot(1).color.compare(slot(0).color, Qt::CaseInsensitive) != 0);
    }

    void changingSettingsClearsReady() {
        lobby.addMember(1, "Ann");
        lobby.setReady(1, true);
        QVERIFY(slot(0).ready);
        lobby.editSlot(1, 0, {{"team", 2}});
        QVERIFY(!slot(0).ready);
    }

    void readyNeedsASlot() {
        lobby.addMember(1, "Ann");
        lobby.releaseSlot(1);
        QCOMPARE(lobby.setReady(1, true).code, QString("no_slot"));
    }

    // ---- settings
    void hostChangesTheMapSettings() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        lobby.setReady(2, true);
        QVERIFY(lobby.setSettings(1, {{"mapWidth", 80}, {"mapHeight", 48}, {"seed", "12345678901234"}}).ok);
        QCOMPARE(lobby.state().settings.mapWidth, 80);
        QCOMPARE(lobby.state().settings.seed, quint64(12345678901234ULL));
        QVERIFY(!slot(1).ready);  // the map changed: confirm again
        QVERIFY(lobby.setSettings(1, {{"seed", 42}}).ok);
        QCOMPARE(lobby.state().settings.seed, quint64(42));
    }

    void unchangedSettingsKeepTheReadyFlags() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        lobby.setReady(2, true);
        QVERIFY(lobby.setSettings(1, {{"mapWidth", 64}, {"seed", 1}}).ok);  // same as before
        QVERIFY(slot(1).ready);
    }

    void settingsRejections() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        QCOMPARE(lobby.setSettings(2, {{"mapWidth", 80}}).code, QString("not_host"));
        QCOMPARE(lobby.setSettings(1, {{"mapWidth", 5}}).code, QString("invalid_value"));
        QCOMPARE(lobby.setSettings(1, {{"mapWidth", 999}}).code, QString("invalid_value"));
        QCOMPARE(lobby.setSettings(1, {{"seed", "abc"}}).code, QString("invalid_value"));
        QCOMPARE(lobby.setSettings(1, {{"speed", 1}}).code, QString("invalid_value"));
        QCOMPARE(lobby.state().settings.mapWidth, 64);
        QCOMPARE(lobby.setSettings(1, {{"difficulty", "insane"}}).code, QString("invalid_value"));
        QVERIFY(lobby.setSettings(1, {{"difficulty", "expert"}}).ok);
        QCOMPARE(lobby.state().settings.difficulty, QString("expert"));
        kot::net::LobbyState copy;
        QVERIFY(kot::net::LobbyState::fromJson(lobby.state().toJson(), &copy, nullptr));
        QCOMPARE(copy.settings.difficulty, QString("expert"));
    }

    // ---- starting
    void startsWhenEveryoneIsReady() {
        readyTwoPlayers();
        QVERIFY(lobby.checkStart(1).ok);
        QVERIFY(lobby.start(1).ok);
        QVERIFY(lobby.state().started);
    }

    void startRules() {
        lobby.addMember(1, "Ann");
        lobby.addMember(2, "Bob");
        QCOMPARE(lobby.start(2).code, QString("not_host"));
        QCOMPARE(lobby.start(1).code, QString("not_ready"));
        lobby.setReady(1, true);
        lobby.setReady(2, true);
        lobby.editSlot(2, 1, {{"team", 1}});  // same team as Ann...
        lobby.setReady(2, true);
        QCOMPARE(lobby.start(1).code, QString("cannot_start"));  // ...only one team
        QVERIFY(lobby.start(1).message.contains("two teams"));
    }

    void theHostNeedNotBeReady() {
        lobby.addMember(1, "Ann");
        lobby.editSlot(1, 1, {{"kind", "ai"}});
        QVERIFY(!slot(0).ready);
        QVERIFY2(lobby.checkStart(1).ok, qPrintable(lobby.checkStart(1).message));
        // Another human still has to confirm.
        lobby.addMember(2, "Bob");
        QCOMPARE(lobby.checkStart(1).code, QString("not_ready"));
        lobby.setReady(2, true);
        QVERIFY(lobby.checkStart(1).ok);
    }

    void needsTwoPlayers() {
        lobby.addMember(1, "Ann");
        lobby.setReady(1, true);
        QVERIFY(lobby.start(1).message.contains("two players"));
        QVERIFY(lobby.editSlot(1, 1, {{"kind", "ai"}}).ok);
        QVERIFY(lobby.start(1).ok);  // human + AI on separate teams
    }

    void aiOnlyIsNotAllowed() {
        lobby.addMember(1, "Ann");
        lobby.releaseSlot(1);
        lobby.editSlot(1, 0, {{"kind", "ai"}});
        lobby.editSlot(1, 1, {{"kind", "ai"}});
        QVERIFY(lobby.start(1).message.contains("human"));
    }

    void nothingChangesAfterTheStart() {
        readyTwoPlayers();
        lobby.start(1);
        QCOMPARE(lobby.claimSlot(1, 2).code, QString("already_started"));
        QCOMPARE(lobby.editSlot(1, 0, {{"team", 3}}).code, QString("already_started"));
        QCOMPARE(lobby.setReady(1, false).code, QString("already_started"));
        QCOMPARE(lobby.setSettings(1, {{"seed", 1}}).code, QString("already_started"));
        QCOMPARE(lobby.start(1).code, QString("already_started"));
    }
};

QTEST_APPLESS_MAIN(TstLobby)
#include "tst_lobby.moc"
