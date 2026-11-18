#include "core/rules/Chests.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

namespace kot::core {

int chestGold(const MapObject& chest, const ContentRegistry& content) {
    return chest.resources.value(content.currency());
}

bool openChest(GameState& state, const ContentRegistry& content, HeroId heroId, MapObjectId chestId,
               bool takeExperience) {
    Hero* hero = state.hero(heroId);
    Player* player = hero ? state.player(hero->owner) : nullptr;
    if (!player) return false;
    MapObject* chest = nullptr;
    for (MapObject& o : state.mapObjects())
        if (o.id == chestId && o.kind == MapObject::Kind::Chest) chest = &o;
    if (!chest) return false;

    if (takeExperience) {
        addExperience(*hero, content, chest->experience);
    } else {
        player->resources[content.currency()] += chestGold(*chest, content);
    }
    state.mapObjects().remove(chestId);
    return true;
}

}  // namespace kot::core
