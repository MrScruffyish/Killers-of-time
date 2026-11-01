# kot_core: data model and rules. No GUI, no networking. Only QtCore.
add_library(kot_core STATIC
    Version.h
    util/StrongId.h
    util/Ids.h
    util/Rng.h
    util/Rng.cpp
    util/Log.h
    util/Log.cpp
)
target_include_directories(kot_core PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_core PUBLIC Qt6::Core PRIVATE kot_warnings)
