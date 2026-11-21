#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// A campaign: scenarios played one after the other (data/campaigns/<id>.json):
//   {"id", "name", "description", "scenarios": [ids],
//    "carryOver": {"heroes": 1, "maxLevel": 0}}
// When the human team wins a chapter, the best heroes of the first human player are
// recorded. Only their name, class and level travel to the next chapter: the army, spells,
// artifacts, traits and experience are the new chapter's own. `maxLevel` caps a carried
// level (0 = no cap).
struct CampaignDef {
    QString id;
    QString name;
    QString description;
    QStringList scenarios;
    int carryHeroes = 1;
    int maxLevel = 0;  // 0 = no limit
};

// Where a group stands in a campaign (kept by the server in its saves folder).
struct CampaignProgress {
    QString campaign;
    int next = 0;               // index of the chapter to play
    QList<QJsonObject> heroes;  // carried heroes (heroSnapshot)
    QStringList artifacts;      // keys of artifacts won as chapter goals
    bool finished(const CampaignDef& def) const { return next >= def.scenarios.size(); }
};

bool loadCampaign(const QString& path, CampaignDef* out, QStringList* errors);
QList<CampaignDef> listCampaigns(const QString& dir);
bool loadProgress(const QString& path, CampaignProgress* out);  // false: none yet
bool saveProgress(const QString& path, const CampaignProgress& progress, QString* error);

// A hero's name, class and level as plain JSON, for carrying over.
QJsonObject heroSnapshot(const GameState& state, const ContentRegistry& content, HeroId hero);
// The strongest heroes of `player` (by level, then experience), at most `count`.
QList<HeroId> bestHeroes(const GameState& state, PlayerId player, int count);
// Gives `player`'s heroes (in id order) the carried heroes' name, class and level,
// capped at `maxLevel` (0 = no cap). Their army, spells, artifacts, traits and
// experience stay the new chapter's own. Extra carried heroes are created next to
// the first one. Returns how many were applied.
int applyCarriedHeroes(GameState& state, const ContentRegistry& content, PlayerId player,
                       const QList<QJsonObject>& heroes, int maxLevel = 0);
// Gives the player's first hero the artifacts won as goals in earlier chapters.
int giveGoalArtifacts(GameState& state, const ContentRegistry& content, PlayerId player, const QStringList& keys);

}  // namespace kot::core
