# kot_core: data model and rules. No GUI, no networking. Only QtCore.
add_library(kot_core STATIC
    Version.h
    util/StrongId.h
    util/Ids.h
    util/Rng.h
    util/Rng.cpp
    util/Log.h
    util/Log.cpp
    model/UnitStack.h
    model/UnitStack.cpp
    model/Position.h
    model/GameMap.h
    model/GameMap.cpp
    model/Army.h
    model/Army.cpp
    model/Entities.h
    model/GameState.h
    model/GameState.cpp
    model/GameStateJson.cpp
    rules/HeroRules.h
    rules/HeroRules.cpp
    rules/Pathfinding.h
    rules/Pathfinding.cpp
    rules/Movement.h
    rules/Movement.cpp
    rules/MapGenerator.h
    rules/MapGenerator.cpp
    data/JsonReader.h
    data/JsonReader.cpp
    content/Defs.h
    content/ContentRegistry.h
    content/ContentRegistry.cpp
)
target_include_directories(kot_core PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_core PUBLIC Qt6::Core PRIVATE kot_warnings)
