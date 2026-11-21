#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// One chapter of a campaign's storyline. Any thread can be empty; a chapter may award
// several at once. All content is by key so progress outlives data reorderings.
struct LegacyChapter {
    QString story;        // shown when this chapter is won
    QStringList spells;   // spell keys learned from here on
    QStringList monuments;  // building keys raised in the player's capital from here on
};

// A campaign's persistent storyline ("legacy" in the campaign JSON): the prose that ties
// the chapters together and the rewards each win adds to the legacy. Every thread is
// optional, so a campaign can be about spells, buildings, records, or just the story.
struct CampaignLegacy {
    QString kind;       // free-form theme id, e.g. "codex", "monument", "chronicle"
    QString title;      // headline, e.g. "The Deathless Codex"
    QString intro;      // shown before chapter 1
    QString epilogue;   // shown after the last chapter
    QList<LegacyChapter> chapters;  // parallel to CampaignDef::scenarios
    bool empty() const { return title.isEmpty() && intro.isEmpty() && epilogue.isEmpty() && chapters.isEmpty(); }
};

// A campaign: scenarios played one after the other (data/campaigns/<id>.json):
//   {"id", "name", "description", "scenarios": [ids],
//    "carryOver": {"heroes": 1, "maxLevel": 0}, "legacy": {...}}
// When the human team wins a chapter, the best heroes of the first human player are
// recorded. Only their name, class and level travel to the next chapter: the army, spells,
// artifacts, traits and experience are the new chapter's own. `maxLevel` caps a carried
// level (0 = no cap). The chapter's legacy awards are added on top (goal artifacts as
// well as anything in `legacy`).
struct CampaignDef {
    QString id;
    QString name;
    QString description;
    QStringList scenarios;
    int carryHeroes = 1;
    int maxLevel = 0;  // 0 = no limit
    CampaignLegacy legacy;
};

// Where a group stands in a campaign (kept by the server in its saves folder).
struct CampaignProgress {
    QString campaign;
    int next = 0;               // index of the chapter to play
    QList<QJsonObject> heroes;  // carried heroes (heroSnapshot)
    QStringList artifacts;      // keys of artifacts won as chapter goals
    QStringList spells;         // keys of spells learned as legacy awards
    QStringList monuments;      // keys of buildings raised as legacy awards
    QList<int> records;         // best win day per chapter (0 = never won)
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
// Applies the legacy the player has built up: spells to the first hero, monuments to his
// towns. Returns how many things were granted (for tests and logging).
int applyLegacy(GameState& state, const ContentRegistry& content, PlayerId player, const CampaignProgress& progress);

}  // namespace kot::core
