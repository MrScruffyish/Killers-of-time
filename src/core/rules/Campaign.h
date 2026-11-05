#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// A campaign: scenarios played one after the other (data/campaigns/<id>.json):
//   {"id", "name", "description", "scenarios": [ids], "carryOver": {"heroes": 1, "army": false}}
// When the human team wins a chapter, the best heroes of the first human player
// (level, experience, traits, spells, artifacts; the army too if `army`) are kept and
// lead the first human player's army in the next chapter.
struct CampaignDef {
    QString id;
    QString name;
    QString description;
    QStringList scenarios;
    int carryHeroes = 1;
    bool carryArmy = false;
};

// Where a group stands in a campaign (kept by the server in its saves folder).
struct CampaignProgress {
    QString campaign;
    int next = 0;               // index of the chapter to play
    QList<QJsonObject> heroes;  // carried heroes (heroSnapshot)
    bool finished(const CampaignDef& def) const { return next >= def.scenarios.size(); }
};

bool loadCampaign(const QString& path, CampaignDef* out, QStringList* errors);
QList<CampaignDef> listCampaigns(const QString& dir);
bool loadProgress(const QString& path, CampaignProgress* out);  // false: none yet
bool saveProgress(const QString& path, const CampaignProgress& progress, QString* error);

// A hero (and his army) as plain JSON, for carrying over.
QJsonObject heroSnapshot(const GameState& state, const ContentRegistry& content, HeroId hero);
// The strongest heroes of `player` (by level, then experience), at most `count`.
QList<HeroId> bestHeroes(const GameState& state, PlayerId player, int count);
// Gives `player`'s heroes (in id order) the carried heroes' name, class, level,
// experience, traits, spells and artifacts, and their army if `withArmy`. Extra
// carried heroes are created next to the first one. Returns how many were applied.
int applyCarriedHeroes(GameState& state, const ContentRegistry& content, PlayerId player,
                       const QList<QJsonObject>& heroes, bool withArmy);

}  // namespace kot::core
